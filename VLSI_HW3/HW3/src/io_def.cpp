#include "design_io.hpp"
#include <iostream>
#include <cstring>
#include <fstream>
#include <sstream>
#include <regex>
#include "defrReader.hpp"
#include "defwWriter.hpp"

using std::string;

static Design* D = nullptr;

static Orient parseOrient(int o) {
    switch(o){
        case 0: return Orient::N;
        case 1: return Orient::S;
        case 2: return Orient::E;
        case 3: return Orient::W;
        case 4: return Orient::FN;
        case 5: return Orient::FS;
        case 6: return Orient::FE;
        case 7: return Orient::FW;
        default:return Orient::N;
    }
}

/* ---------- Callbacks ---------- */
static int unitsCb(defrCallbackType_e, double val, defiUserData){
    if (val > 0.0) D->dbuPerMicron = static_cast<int>(val);
    return 0;
}
static int dieAreaCb(defrCallbackType_e, defiBox* b, defiUserData){
    D->layout.dieArea = RectI{ b->xl(), b->yl(), b->xh(), b->yh() };
    return 0;
}
static int rowCb(defrCallbackType_e, defiRow* r, defiUserData){
    Row R;
    R.name = r->name();
    R.siteName = r->macro();
    R.originX  = r->x();
    R.originY  = r->y();
    R.xStep    = r->xStep();
    R.numSites = r->xNum();
    R.flip = (std::strcmp(r->orientStr(),"FS")==0);
    D->layout.rows.push_back(std::move(R));
    return 0;
}
static int compCb(defrCallbackType_e, defiComponent* c, defiUserData){
    Inst I;
    I.name = c->id();
    auto it = D->macroName2Id.find(c->name());
    I.macroId = (it==D->macroName2Id.end()) ? -1 : it->second;

    if (c->isFixed())  { I.fixed=true;  I.placed=true;  I.x=c->placementX(); I.y=c->placementY(); I.orient=parseOrient(c->placementOrient()); }
    else if (c->isPlaced()){ I.placed=true; I.x=c->placementX(); I.y=c->placementY(); I.orient=parseOrient(c->placementOrient()); }
    else { I.placed=false; }

    D->instName2Id[I.name] = (int)D->insts.size();
    D->insts.push_back(std::move(I));
    return 0;
}
// 這裡先不綁 IO pin；如需精準 IO 位置，可在 PINS callback 蒐集
static int pinCb(defrCallbackType_e, defiPin* /*p*/, defiUserData){
    return 0;
}
static int netCb(defrCallbackType_e, defiNet* n, defiUserData){
    Net N; N.name = n->name();
    for (int i=0;i<n->numConnections();++i){
        const char* inst = n->instance(i);
        const char* pin  = n->pin(i);
        if (std::strcmp(inst, "PIN")==0){
            NetPinRef R; R.isIO=true; R.ioName=pin; R.ioLoc=PointI{0,0};
            N.pins.push_back(R);
        } else {
            auto it = D->instName2Id.find(inst);
            if (it==D->instName2Id.end()) continue;
            int iid = it->second;
            int mid = D->insts[iid].macroId;
            int lidx = -1;
            if (mid>=0){
                auto jt = D->macros[mid].pinName2Idx.find(pin);
                if (jt!=D->macros[mid].pinName2Idx.end()) lidx = jt->second;
            }
            NetPinRef R; R.instId=iid; R.libPinIdx=lidx; R.isIO=false;
            N.pins.push_back(R);
        }
    }
    D->netName2Id[N.name] = (int)D->nets.size();
    D->nets.push_back(std::move(N));
    return 0;
}

/* ---------- Public APIs ---------- */
bool loadDEF(const std::string& path, Design& d){
    FILE* f = fopen(path.c_str(),"r");
    if(!f){ std::cerr<<"❌ Cannot open DEF "<<path<<"\n"; return false; }
    D = &d;
    d.layout = Layout{};
    d.insts.clear(); d.instName2Id.clear();
    d.nets.clear();  d.netName2Id.clear();

    defrInit();
    defrSetUnitsCbk(unitsCb);      // double callback
    defrSetDieAreaCbk(dieAreaCb);
    defrSetRowCbk(rowCb);
    defrSetComponentCbk(compCb);
    defrSetPinCbk(pinCb);
    defrSetNetCbk(netCb);

    defrRead(f, path.c_str(), nullptr, 1);
    fclose(f);
    defrClear();

    d.buildIndices();
    std::cout<<"✅ DEF: insts="<<d.insts.size()<<", nets="<<d.nets.size()
             <<", rows="<<d.layout.rows.size()<<"\n";
    return true;
}

bool writeDEF(const std::string& out, const Design& d){
    FILE* fo = fopen(out.c_str(),"w");
    if(!fo){ std::cerr<<"❌ Cannot open DEF output "<<out<<"\n"; return false; }

    defwInitCbk(fo);
    defwVersion(5,8); defwNewLine();
    defwDividerChar("/"); defwNewLine();
    defwBusBitChars("[]"); defwNewLine();
    fprintf(fo, "DESIGN public1 ;\n");
    fprintf(fo, "UNITS DISTANCE MICRONS %d ;\n", d.dbuPerMicron);

    if (d.layout.dieArea.x2>d.layout.dieArea.x1 && d.layout.dieArea.y2>d.layout.dieArea.y1){
        fprintf(fo, "DIEAREA ( %d %d ) ( %d %d ) ;\n",
            d.layout.dieArea.x1, d.layout.dieArea.y1, d.layout.dieArea.x2, d.layout.dieArea.y2);
    }

    defwStartComponents((int)d.insts.size());
    const char* Z=nullptr;
    for (const auto& c : d.insts){
        const char* status = c.fixed? "FIXED" : (c.placed? "PLACED":"UNPLACED");
        const char* genName=Z; const char* genParam=Z; const char* eeq=Z;
        const char* source="NETLIST"; int numNetName=0; const char** netNames=nullptr;
        int numForeign=0; const char** foreigns=nullptr; int *fx=nullptr,*fy=nullptr,*fo_=nullptr;
        const char* region=Z; int xl=0,yl=0,xh=0,yh=0;

        const char* mname = (c.macroId>=0)? d.macros[c.macroId].name.c_str() : "UNK";

        defwComponent(
            c.name.c_str(),          // instance
            mname,                   // master
            numNetName, netNames,
            eeq, genName, genParam,
            source,
            numForeign, foreigns,
            fx, fy, fo_,
            status,
            c.x, c.y,
            0 /*statusOrient*/,
            -1.0 /*weight*/,
            region, xl, yl, xh, yh
        );
    }
    defwEndComponents();

    fprintf(fo, "END DESIGN\n");
    fclose(fo);
    std::cout<<"✅ DEF written → "<<out<<"\n";
    return true;
}

static const char* orientToStr(Orient o){
    switch(o){
        case Orient::N: return "N";
        case Orient::S: return "S";
        case Orient::E: return "E";
        case Orient::W: return "W";
        case Orient::FN:return "FN";
        case Orient::FS:return "FS";
        case Orient::FE:return "FE";
        case Orient::FW:return "FW";
    }
    return "N";
}

// Preserve entire DEF file from input, only update coordinates/orientation in "+ PLACED" lines inside COMPONENTS
bool writeDEFPreserve(const std::string& inDefPath, const std::string& outDefPath, const Design& d){
    std::ifstream fin(inDefPath);
    if(!fin){ std::cerr<<"❌ Cannot open input DEF for preserve mode: "<<inDefPath<<"\n"; return false; }
    std::ofstream fout(outDefPath);
    if(!fout){ std::cerr<<"❌ Cannot open output DEF: "<<outDefPath<<"\n"; return false; }

    // Precompute row-orientation by Y for fast lookup
    std::unordered_map<int,const char*> rowOrientByY;
    rowOrientByY.reserve(d.layout.rows.size()*2);
    for (const auto& r : d.layout.rows) {
        rowOrientByY[r.originY] = r.flip ? "FS" : "N";
    }

    std::string line;
    bool inComponents=false;
    std::string curInst;
    while (std::getline(fin, line)){
        std::string outLine = line;
        // Detect COMPONENTS block boundaries
        if (!inComponents){
            // entering COMPONENTS
            if (line.find("COMPONENTS ") != std::string::npos) {
                inComponents = true;
            }
        } else {
            if (line.find("END COMPONENTS") != std::string::npos) {
                inComponents = false;
                curInst.clear();
            } else {
                // capture current instance name when a new component begins: "- instName <master>"
                std::string trimmed = line;
                // ltrim spaces
                trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                if (!trimmed.empty() && trimmed[0]=='-'){
                    std::istringstream iss(trimmed);
                    std::string dash; std::string instName;
                    iss >> dash >> instName;
                    if (!instName.empty()) curInst = instName;
                }

                // If "+ PLACED" line, replace coordinates and orientation using design data
                if (!curInst.empty()){
                    // Only modify "+ PLACED" status, leave FIXED/UNPLACED lines intact
                    if (line.find("+ PLACED") != std::string::npos) {
                        auto it = d.instName2Id.find(curInst);
                        if (it != d.instName2Id.end()){
                            const Inst& inst = d.insts[it->second];
                            // Orientation must match the row at this Y
                            const char* rowOri = "N";
                            auto ro = rowOrientByY.find(inst.y);
                            if (ro != rowOrientByY.end()) rowOri = ro->second;
                            // Build replacement "( x y ) ORIENT"
                            char buf[128];
                            std::snprintf(buf, sizeof(buf), "( %d %d ) %s", inst.x, inst.y, rowOri);

                            // Find parentheses region and following orient token, then replace
                            size_t lpar = line.find('(');
                            size_t rpar = (lpar!=std::string::npos)? line.find(')', lpar) : std::string::npos;
                            if (lpar != std::string::npos && rpar != std::string::npos) {
                                // Find start of orient token after rpar
                                size_t pos = rpar + 1;
                                while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
                                // Skip existing orientation token letters
                                size_t orientStart = pos;
                                while (pos < line.size() && std::isalpha(static_cast<unsigned char>(line[pos]))) ++pos;
                                size_t orientEnd = pos;

                                std::string prefix = line.substr(0, lpar);
                                std::string suffix = (orientEnd <= line.size()) ? line.substr(orientEnd) : std::string();
                                outLine = prefix + buf + suffix;
                            } else {
                                // Fallback: replace entire line's coordinates/orient section naïvely
                                // Keep leading whitespace and status
                                std::string leading = line.substr(0, line.find_first_not_of(" \t"));
                                outLine = leading + "+ PLACED " + buf;
                            }
                        }
                    }
                }
            }
        }
        fout << outLine << "\n";
    }
    std::cout<<"✅ DEF (preserve mode) written → "<<outDefPath<<" (copied from input, updated COMPONENTS:+ PLACED)\n";
    return true;
}
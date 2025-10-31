#include "design_io.hpp"
#include <iostream>
#include <cmath>
#include "lefrReader.hpp"

using std::string;

static Design* G = nullptr;

static int siteCb(lefrCallbackType_e, lefiSite* s, lefiUserData) {
    Site S; S.name = s->name();
    S.sizeX = (int)std::lround(s->sizeX());
    S.sizeY = (int)std::lround(s->sizeY());
    G->sites.push_back(S);
    return 0;
}

static int macroCb(lefrCallbackType_e, lefiMacro* m, lefiUserData) {
    Macro M; M.name = m->name();
    if (m->hasSize()) {
        M.width  = (int)std::lround(m->sizeX());
        M.height = (int)std::lround(m->sizeY());
    }
    G->macros.push_back(M);
    return 0;
}

static int pinCb(lefrCallbackType_e, lefiPin* p, lefiUserData) {
    if (G->macros.empty()) return 0;
    Macro& M = G->macros.back();
    LibPin lp; lp.name = p->name();

    if (p->numPorts() > 0) {
        // 在此版本 API，lefiPin::port(i) 直接回傳 lefiGeometries*
        lefiGeometries* geo = p->port(0);
        if (geo) {
            for (int i = 0; i < geo->numItems(); ++i) {
                if (geo->itemType(i) == lefiGeomRectE) {
                    auto r = geo->getRect(i);
                    int cx = (int)std::lround((r->xl + r->xh) / 2.0);
                    int cy = (int)std::lround((r->yl + r->yh) / 2.0);
                    lp.shapes.push_back(LibPinShape{ PointI{cx, cy} });
                    break;
                }
            }
        }
    }

    if (lp.shapes.empty())
        lp.shapes.push_back(LibPinShape{ PointI{ M.width / 2, M.height / 2 } });

    int idx = (int)M.pins.size();
    M.pinName2Idx[lp.name] = idx;
    M.pins.push_back(std::move(lp));
    return 0;
}

bool loadLEF(const std::string& lefPath, Design& d) {
    FILE* f = fopen(lefPath.c_str(), "r");
    if (!f) { std::cerr << "❌ Cannot open LEF " << lefPath << "\n"; return false; }

    G = &d;
    d.macros.clear(); d.sites.clear();

    lefrInit();
    lefrSetSiteCbk(siteCb);
    lefrSetMacroCbk(macroCb);
    lefrSetPinCbk(pinCb);

    lefrRead(f, lefPath.c_str(), nullptr);
    fclose(f);
    lefrClear();

    d.buildIndices();
    std::cout << "✅ LEF: macros=" << d.macros.size() << ", sites=" << d.sites.size() << "\n";
    return true;
}
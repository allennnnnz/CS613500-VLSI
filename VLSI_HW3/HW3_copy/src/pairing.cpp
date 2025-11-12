#include "pairing.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

template <typename T>
static inline T clamp_val(T v, T lo, T hi) { return std::max(lo, std::min(v, hi)); }

static int getRowHeight(const Design& d) {
    if (!d.layout.rows.empty()) return d.layout.rows.front().xStep;
    return 200;
}

// ---- inst→nets adjacency ----
static std::vector<std::vector<int>> buildInstToNets(const Design& d) {
    std::vector<std::vector<int>> res(d.insts.size());
    for (int nid = 0; nid < (int)d.nets.size(); ++nid)
        for (const auto& p : d.nets[nid].pins)
            if (!p.isIO && p.instId >= 0)
                res[p.instId].push_back(nid);
    for (auto& v : res) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    }
    return res;
}

// ---- C(u,v): connectivity ----
static double connectivity_uv(const Design& d,
                              const std::vector<std::vector<int>>& inst2nets,
                              int u, int v) {
    const auto& a = inst2nets[u];
    const auto& b = inst2nets[v];
    size_t i=0,j=0; double C=0;
    while(i<a.size()&&j<b.size()){
        if(a[i]==b[j]){
            int deg = (int)d.nets[a[i]].pins.size();
            if(deg>1) C += 1.0/(deg-1);
            ++i; ++j;
        } else if(a[i]<b[j]) ++i;
        else ++j;
    }
    return C;
}

// ---- Bin grid ----
static BinGrid buildBinGrid(const Design& d, int binW, int binH) {
    BinGrid g;
    g.x0 = d.layout.dieArea.x1;
    g.y0 = d.layout.dieArea.y1;
    g.binW = binW; g.binH = binH;
    g.nx = std::max(1,(d.layout.dieArea.x2 - d.layout.dieArea.x1 + binW-1)/binW);
    g.ny = std::max(1,(d.layout.dieArea.y2 - d.layout.dieArea.y1 + binH-1)/binH);
    g.bins.assign(g.nx,std::vector<std::vector<int>>(g.ny));

    for(int i=0;i<(int)d.insts.size();++i){
        const auto&I=d.insts[i];
        if(I.fixed) continue;
        const auto&M=d.macros[I.macroId];
        if(M.height<=getRowHeight(d)){ // 單高
            int bx = clamp_val((I.x-g.x0)/binW,0,g.nx-1);
            int by = clamp_val((I.y-g.y0)/binH,0,g.ny-1);
            g.bins[bx][by].push_back(i);
        }
    }
    return g;
}

static void appendNeighbors(const BinGrid& g,int bx,int by,std::vector<int>& out){
    for(int dx=-1;dx<=1;++dx)
        for(int dy=-1;dy<=1;++dy){
            int x=bx+dx,y=by+dy;
            if(x>=0&&x<g.nx&&y>=0&&y<g.ny)
                out.insert(out.end(),g.bins[x][y].begin(),g.bins[x][y].end());
        }
}

// ---- Build edges with density control ----
static std::vector<PairEdge> buildEdges(const Design& d,const BinGrid& g,
                                        const PairingParams& pm,
                                        const std::vector<std::vector<int>>& inst2nets,
                                        std::vector<std::vector<int>>& pairCount) {
    std::vector<PairEdge> edges;
    int H=getRowHeight(d);
    for(int bx=0;bx<g.nx;++bx)
        for(int by=0;by<g.ny;++by){
            const auto&cells=g.bins[bx][by];
            if(cells.empty()) continue;
            std::vector<int> pool;
            appendNeighbors(g,bx,by,pool);
            for(int qi:cells){
                const auto&Q=d.insts[qi];
                const auto&MQ=d.macros[Q.macroId];
                for(int vi:pool){
                    if(vi<=qi) continue;
                    const auto&V=d.insts[vi];
                    const auto&MV=d.macros[V.macroId];
                    int md=std::abs(Q.x-V.x)+std::abs(Q.y-V.y);
                    if(md>pm.r_target) continue;
                    double C=connectivity_uv(d,inst2nets,qi,vi);
                    if(C<=0) continue;
                    double PA=H*std::abs(MQ.width-MV.width);
                    double PD=md;
                    double B=C-pm.alpha1*PA-pm.alpha2*PD;
                    if(B<=0) continue;
                    edges.push_back({qi,vi,B,bx,by});
                }
            }
        }
    std::sort(edges.begin(),edges.end(),[](auto&a,auto&b){return a.w>b.w;});
    return edges;
}

// ---- Greedy matching with density check ----
PairingResult run_pairing_and_selection(const Design& d,const PairingParams& pm){
    auto grid=buildBinGrid(d,pm.binW,pm.binH);
    auto inst2nets=buildInstToNets(d);

    std::cout << "[DEBUG] inst2nets first 5 cells:\n";
    for (int i = 0; i < std::min(5, (int)inst2nets.size()); ++i) {
        std::cout << "  inst " << i << " nets=" << inst2nets[i].size() << "\n";
    }

    std::vector<std::vector<int>> pairCount(grid.nx,std::vector<int>(grid.ny,0));
    auto edges=buildEdges(d,grid,pm,inst2nets,pairCount);
    std::vector<char> used(d.insts.size(),0);
    PairingResult pr;

    for(auto&e:edges){
        if(used[e.u]||used[e.v]) continue;
        if((double)pairCount[e.bx][e.by]/std::max(1.0,(double)grid.bins[e.bx][e.by].size())>pm.D_target)
            continue;
        used[e.u]=used[e.v]=1;
        pr.selectedPairs.push_back({e.u,e.v});
        pairCount[e.bx][e.by]++;
    }
    for(int i=0;i<(int)d.insts.size();++i)
        if(!used[i]) pr.expandedSingles.push_back(i);

    double total=0,count=0;
    for(int bx=0;bx<grid.nx;++bx)
        for(int by=0;by<grid.ny;++by){
            int ncell = grid.bins[bx][by].size();
            if(ncell>0){ total += (double)pairCount[bx][by]/ncell; count++; }
        }
    pr.avgDensity = (count>0)? total/count : 0.0;
    return pr;
}

// ---- Transform & Unpair ----
Design transform_to_all_double_height(const Design& d,const PairingResult& pr,TransformMaps& maps){
    Design out=d;
    out.insts.clear();
    maps.newInstInfo.clear();
    maps.oldToNew.assign(d.insts.size(),-1);
    std::unordered_map<int,int> mate;
    for(auto&uv:pr.selectedPairs){mate[uv.u]=uv.v;mate[uv.v]=uv.u;}
    auto evenY=[&](int y){
        int base=d.layout.rows.front().originY;
        int step=getRowHeight(d);
        int idx=(y-base)/(2*step);
        return base+idx*2*step;
    };
    for(int i=0;i<(int)d.insts.size();++i){
        const auto&I=d.insts[i];
        if(I.fixed){out.insts.push_back(I);continue;}
        if(mate.count(i)&&i<mate[i]){
            const auto&J=d.insts[mate[i]];
            Inst P;
            P.name="PAIR_"+I.name+"__"+J.name;
            P.macroId=I.macroId;
            P.x=(I.x+J.x)/2;
            P.y=evenY((I.y+J.y)/2);
            P.fixed=false;P.placed=true;
            out.insts.push_back(P);
            maps.newInstInfo.push_back({true,i,mate[i],false});
        }else if(!mate.count(i)){
            Inst S=I;S.y=evenY(I.y);out.insts.push_back(S);
            maps.newInstInfo.push_back({false,-1,-1,true});
        }
    }
    out.buildIndices();
    return out;
}

void unpair_and_refine(Design& dAfterDP,const TransformMaps& maps,
                       void (*runStagePlacement)(Design&)){
    runStagePlacement(dAfterDP);
}
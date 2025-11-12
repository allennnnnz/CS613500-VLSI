#include "dp_stage.hpp"
#include <algorithm>
#include <vector>
#include <unordered_set>
#include <limits>

static inline int cellWidth(const Design& d, int id){ return d.macros[d.insts[id].macroId].width; }

// Compute per-net HPWL (cell-LL + IO abs)
static long long netHPWL_cellLL(const Design& d, int nid){
    const Net& net = d.nets[nid]; if (net.isSpecial || net.pins.empty()) return 0;
    int xmin=std::numeric_limits<int>::max(), ymin=std::numeric_limits<int>::max();
    int xmax=std::numeric_limits<int>::min(), ymax=std::numeric_limits<int>::min();
    for (const auto& p: net.pins){
        int x=0,y=0;
        if (p.isIO || p.instId<0){ auto it=d.ioPinLoc.find(p.ioName); if(it!=d.ioPinLoc.end()){ x=it->second.x; y=it->second.y;} else { x=p.ioLoc.x; y=p.ioLoc.y; } }
        else { x=d.insts[p.instId].x; y=d.insts[p.instId].y; }
        if (x<xmin) xmin=x; if (x>xmax) xmax=x; if (y<ymin) ymin=y; if (y>ymax) ymax=y;
    }
    if (xmin>=xmax || ymin>=ymax) return 0; return (xmax-xmin) + (ymax-ymin);
}

// desired X = median of connected pins' x (excluding self)
static int desiredX(const Design& d, int id){
    std::vector<int> xs; xs.reserve(16);
    for(int nid : d.inst2nets[id]){
        const Net& net=d.nets[nid];
        for(const auto& p: net.pins){ if (!p.isIO && p.instId==id) continue; int x=0; if(p.isIO||p.instId<0){ auto it=d.ioPinLoc.find(p.ioName); x=(it!=d.ioPinLoc.end())? it->second.x: p.ioLoc.x; } else { x=d.insts[p.instId].x; } xs.push_back(x);} }
    if(xs.empty()) return d.insts[id].x; size_t m=xs.size()/2; std::nth_element(xs.begin(), xs.begin()+m, xs.end()); return xs[m];
}

// pack a list of ids tightly inside [L,R)
static void packRange(Design& d, const Row& row, int L, int R, const std::vector<int>& ids){ int cursor=L; for(int id: ids){ int w=cellWidth(d,id); int rb=R-w; int x=std::min(std::max(cursor, d.insts[id].x), rb); int site = row.originX + ((x - row.originX)/row.xStep)*row.xStep; if(site<L) site=L; if(site>rb) site=rb; d.insts[id].x=site; d.insts[id].y=row.originY; cursor=site+w; } }

bool performNetDrivenCompaction(Design& d, int topN, int neighborK){
    // 1) rank nets by HPWL
    std::vector<int> netIds; netIds.reserve(d.nets.size()); for (int i=0;i<(int)d.nets.size();++i) if(!d.nets[i].isSpecial) netIds.push_back(i);
    std::vector<std::pair<long long,int>> rank; rank.reserve(netIds.size());
    for (int nid: netIds){ long long hp = netHPWL_cellLL(d, nid); if (hp>0) rank.emplace_back(hp, nid); }
    if (rank.empty()) return false; std::sort(rank.begin(), rank.end(), std::greater<>());
    if (topN > (int)rank.size()) topN = (int)rank.size();

    // collect target movable insts
    std::vector<char> isTarget(d.insts.size(), 0);
    for (int i=0;i<topN;++i){ int nid=rank[i].second; for (const auto& p: d.nets[nid].pins){ if (!p.isIO && p.instId>=0 && p.instId<(int)d.insts.size() && !d.insts[p.instId].fixed) isTarget[p.instId]=1; } }

    bool any=false;
    // per row process
    for (const auto& row : d.layout.rows){
        // build fixeds and movables
        struct Item{int id,x,w; bool fixed;}; std::vector<Item> fixeds, movs;
        for (int i=0;i<(int)d.insts.size();++i){ const auto&I=d.insts[i]; if(I.y!=row.originY) continue; int w=d.macros[I.macroId].width; if(I.fixed) fixeds.push_back({i,I.x,w,true}); else movs.push_back({i,I.x,w,false}); }
        if (movs.empty()) continue; std::sort(fixeds.begin(), fixeds.end(), [](auto&a,auto&b){return a.x<b.x;}); std::sort(movs.begin(), movs.end(), [](auto&a,auto&b){return a.x<b.x;});
        int rowL=row.originX, rowR=row.originX+row.xStep*row.numSites; std::vector<std::pair<int,int>> segs; int cur=rowL; for(auto&f:fixeds){segs.emplace_back(cur,f.x); cur=f.x+f.w;} segs.emplace_back(cur,rowR);

        // for each segment, identify contiguous window that covers target ids
        for (auto [L,R] : segs){ if (R<=L) continue; // collect movables intersecting seg
            std::vector<int> ids; for (auto&m: movs){ int w = d.macros[d.insts[m.id].macroId].width; int x=d.insts[m.id].x; if (x>=R) break; if (x+w>L) ids.push_back(m.id); }
            if (ids.size()<2) continue;
            // mark target indices
            std::vector<int> idxs; idxs.reserve(ids.size()); for (int i=0;i<(int)ids.size();++i){ if (isTarget[ids[i]]) idxs.push_back(i);} if (idxs.size()<2) continue;
            int iL = idxs.front(); int iR = idxs.back();
            std::vector<int> window; window.reserve(iR - iL + 1); for (int i=iL;i<=iR;++i) window.push_back(ids[i]);
            // backup
            std::vector<int> saveX; saveX.reserve(window.size()); for (int id: window) saveX.push_back(d.insts[id].x);
            int64_t base = d.totalHPWL();
            // build reordered list: targets sorted by desiredX, non-target keep relative order but positions preserved in sequence
            std::vector<int> targets; targets.reserve(window.size()); for(int id: window) if(isTarget[id]) targets.push_back(id); std::sort(targets.begin(), targets.end(), [&](int a,int b){return desiredX(d,a) < desiredX(d,b);} );
            int tIdx=0; std::vector<int> newOrder; newOrder.reserve(window.size());
            for (int id: window){ if (isTarget[id]) newOrder.push_back(targets[tIdx++]); else newOrder.push_back(id); }
            // pack inside [L,R)
            packRange(d, row, L, R, newOrder);
            int64_t after = d.totalHPWL();
            if (after < base){ any=true; }
            else { // revert
                for (size_t k=0;k<window.size();++k) d.insts[window[k]].x = saveX[k];
            }
        }
    }
    return any;
}

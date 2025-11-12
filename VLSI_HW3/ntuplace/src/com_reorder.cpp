#include "dp_stage.hpp"
#include <algorithm>
#include <vector>
#include <limits>
#include <numeric>

static int cellWidth(const Design& d, int id){ return d.macros[d.insts[id].macroId].width; }

// Compute desired x for a cell as median of all connected pins' x (IO absolute)
static int desiredX(const Design& d, int id){
    std::vector<int> xs; xs.reserve(16);
    for (int nid : d.inst2nets[id]){
        const Net& net = d.nets[nid];
        for (const auto& p : net.pins){
            int x=0; if (p.isIO || p.instId<0){
                auto it = d.ioPinLoc.find(p.ioName);
                x = (it!=d.ioPinLoc.end())? it->second.x : p.ioLoc.x;
            } else if (p.instId!=id){ x = d.insts[p.instId].x; }
        xs.push_back(x);
        }
    }
    if (xs.empty()) return d.insts[id].x;
    size_t mid = xs.size()/2; std::nth_element(xs.begin(), xs.begin()+mid, xs.end());
    return xs[mid];
}

// Pack cells in [L,R) according to current order, snapping to site
static void packRange(Design& d, const Row& row, int L, int R, const std::vector<int>& ids){
    int cursor=L;
    for(int id: ids){ int w=cellWidth(d,id); int rb=R-w; int x=std::min(std::max(cursor, d.insts[id].x), rb);
        int site = row.originX + ((x - row.originX)/row.xStep)*row.xStep; if (site< L) site=L; if (site>rb) site=rb;
        d.insts[id].x=site; d.insts[id].y=row.originY; cursor=site+w; }
}

bool performCOMReorder(Design& d){
    bool any=false;
    for (const auto& row : d.layout.rows){
        // split segments by fixed
        struct Fix{int x,w;}; std::vector<Fix> fx;
        for (int i=0;i<(int)d.insts.size();++i){ const auto&I=d.insts[i]; if (I.y!=row.originY||!I.fixed) continue; fx.push_back({I.x, d.macros[I.macroId].width});}
        std::sort(fx.begin(),fx.end(),[](auto&a,auto&b){return a.x<b.x;});
        int rowL=row.originX, rowR=row.originX+row.xStep*row.numSites; std::vector<std::pair<int,int>> segs; int cur=rowL; for(auto&f:fx){segs.emplace_back(cur,f.x); cur=f.x+f.w;} segs.emplace_back(cur,rowR);
        // movables in row
        std::vector<int> mov; for (int i=0;i<(int)d.insts.size();++i){ const auto&I=d.insts[i]; if (I.y==row.originY && !I.fixed) mov.push_back(i);} std::sort(mov.begin(),mov.end(),[&](int a,int b){return d.insts[a].x<d.insts[b].x;});
        for (auto [L,R]: segs){ if (R<=L) continue; // collect movables intersecting seg
            std::vector<int> ids; for (int id: mov){ int w=cellWidth(d,id); int x=d.insts[id].x; if (x>=R) break; if (x+w>L) ids.push_back(id);} if(ids.size()<2) continue;
            // backup
            std::vector<int> saveX; saveX.reserve(ids.size()); for(int id: ids) saveX.push_back(d.insts[id].x);
            int64_t base = 0; // use global HPWL for acceptance
            // compute once per segment to avoid over-accept
            base = d.totalHPWL();
            // sort by desired x and pack
            std::vector<int> ord = ids; std::sort(ord.begin(), ord.end(), [&](int a,int b){return desiredX(d,a) < desiredX(d,b);} );
            packRange(d, row, L, R, ord);
            int64_t after = d.totalHPWL();
            if (after < base){ any=true; }
            else { // revert
                for (size_t k=0;k<ids.size();++k) d.insts[ids[k]].x = saveX[k];
            }
        }
    }
    return any;
}

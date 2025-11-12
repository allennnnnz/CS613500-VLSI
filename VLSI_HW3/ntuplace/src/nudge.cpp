#include "dp_stage.hpp"
#include <algorithm>
#include <vector>
#include <limits>

static int desiredX(const Design& d, int id){
    std::vector<int> xs; xs.reserve(16);
    for(int nid : d.inst2nets[id]){
        const Net& net = d.nets[nid];
        for(const auto& p: net.pins){ if(!p.isIO && p.instId==id) continue; int x=0; if(p.isIO||p.instId<0){ auto it=d.ioPinLoc.find(p.ioName); x=(it!=d.ioPinLoc.end())? it->second.x: p.ioLoc.x; } else x=d.insts[p.instId].x; xs.push_back(x);} }
    if(xs.empty()) return d.insts[id].x; size_t m=xs.size()/2; std::nth_element(xs.begin(), xs.begin()+m, xs.end()); return xs[m];
}

bool performNudgeTowardsDesiredX(Design& d, int maxSites){
    bool any=false; if (maxSites<=0) return false;
    for(const auto& row : d.layout.rows){
        int siteStep = row.xStep>0? row.xStep:1;
        for(int i=0;i<(int)d.insts.size();++i){ auto &I=d.insts[i]; if(I.fixed || I.y!=row.originY) continue; int target = desiredX(d,i); int cur=I.x; int delta=target - cur; if(delta==0) continue; int absSites = std::abs(delta)/siteStep; if(absSites==0) continue; int moveSites = std::min(absSites, maxSites); int step = (delta>0? 1:-1)*moveSites*siteStep; int newX = cur + step; // boundary clamp
            int rowL=row.originX; int rowR=row.originX + row.xStep*row.numSites; int w=d.macros[I.macroId].width; if(newX < rowL) newX=rowL; if(newX + w > rowR) newX = rowR - w;
            // evaluate partial nets
            int64_t base = partialHPWL(d, {i}); int oldX=I.x; I.x=newX; int64_t after=partialHPWL(d,{i}); if(after < base && d.isRoughLegal(i)){ any=true; } else { I.x=oldX; }
        }
    }
    return any;
}

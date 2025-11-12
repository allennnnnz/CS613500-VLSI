// fastDP integrated pipeline implementation
#include "dp_stage.hpp"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <numeric>
#include <unordered_set>
#include <climits>
#include <cmath>

// ================================================================
// HPWL utilities using cell lower-left metric (consistent with main)
// ================================================================
struct NetBBox {
    int xmin, xmax, ymin, ymax;
};

std::vector<NetBBox> netBBoxes; // 建立在 Design 裡

void buildNetBBox(Design& d) {
    netBBoxes.resize(d.nets.size());
    for (int nid = 0; nid < d.nets.size(); ++nid) {
        const auto& net = d.nets[nid];
        if (net.pins.empty()) continue;
        int xmin=INT_MAX, xmax=INT_MIN, ymin=INT_MAX, ymax=INT_MIN;
        for (auto& p : net.pins) {
            int x = p.isIO ? p.ioLoc.x : d.insts[p.instId].x;
            int y = p.isIO ? p.ioLoc.y : d.insts[p.instId].y;
            xmin = std::min(xmin, x); xmax = std::max(xmax, x);
            ymin = std::min(ymin, y); ymax = std::max(ymax, y);
        }
        netBBoxes[nid] = {xmin, xmax, ymin, ymax};
    }
}

static int64_t hpwlCellLL(const Design& d) {
    long long sum = 0;
    for (const Net& net : d.nets) {
        if (net.isSpecial || net.pins.empty()) continue;
        int xmin = INT_MAX, xmax = INT_MIN, ymin = INT_MAX, ymax = INT_MIN;
        for (const auto &p : net.pins) {
            int x = 0, y = 0;
            if (p.isIO || p.instId < 0) {
                auto it = d.ioPinLoc.find(p.ioName);
                if (it != d.ioPinLoc.end()) { x = it->second.x; y = it->second.y; }
                else { x = p.ioLoc.x; y = p.ioLoc.y; }
            } else {
                x = d.insts[p.instId].x; y = d.insts[p.instId].y;
            }
            xmin = std::min(xmin, x); xmax = std::max(xmax, x);
            ymin = std::min(ymin, y); ymax = std::max(ymax, y);
        }
        if (xmin < xmax && ymin < ymax)
            sum += (xmax - xmin) + (ymax - ymin);
    }
    return (int64_t)sum;
}

// ================================================================
// Compatibility wrapper for old partialHPWL API
// ================================================================
int64_t partialHPWL(const Design& d, const std::vector<int>& insts) {
    std::unordered_set<int> nets;
    for (int id : insts)
        if (id >= 0 && id < (int)d.inst2nets.size())
            for (int nid : d.inst2nets[id])
                nets.insert(nid);

    int64_t hpwl = 0;
    for (int nid : nets) {
        const auto& net = d.nets[nid];
        if (net.isSpecial || net.pins.empty()) continue;
        int xmin = INT_MAX, ymin = INT_MAX;
        int xmax = INT_MIN, ymax = INT_MIN;
        for (auto& p : net.pins) {
            int x=0,y=0;
            if (p.isIO || p.instId<0){
                auto it = d.ioPinLoc.find(p.ioName);
                if (it!=d.ioPinLoc.end()){ x=it->second.x; y=it->second.y; }
                else { x=p.ioLoc.x; y=p.ioLoc.y; }
            } else { x=d.insts[p.instId].x; y=d.insts[p.instId].y; }
            xmin = std::min(xmin, x); xmax = std::max(xmax, x);
            ymin = std::min(ymin, y); ymax = std::max(ymax, y);
        }
        hpwl += (xmax - xmin) + (ymax - ymin);
    }
    return hpwl;
}

// ================================================================
// Lightweight legality (row-wise minimal right push)
// ================================================================
void legalizeAndAlign(Design& d) {
    for (const auto &row : d.layout.rows) {
        // collect movable + fixed for ordering
        struct Item { int id; int x; int w; bool fixed; };
        std::vector<Item> fixeds, movs;
        for (int i=0;i<(int)d.insts.size();++i) {
            const Inst &I=d.insts[i]; if (I.y!=row.originY) continue;
            int w=d.macros[I.macroId].width;
            if (I.fixed) fixeds.push_back({i,I.x,w,true}); else movs.push_back({i,I.x,w,false});
        }
        std::sort(fixeds.begin(), fixeds.end(), [](auto&a,auto&b){return a.x<b.x;});
        std::sort(movs.begin(), movs.end(),   [](auto&a,auto&b){return a.x<b.x;});
        int rowL=row.originX; int rowR=row.originX + row.xStep*row.numSites;
        // build segments between fixeds
        std::vector<std::pair<int,int>> segs; int cur=rowL; for (auto&F:fixeds){segs.emplace_back(cur,F.x); cur=F.x+F.w;} segs.emplace_back(cur,rowR);
        size_t mvIdx=0;
        for (auto [L,R] : segs) {
            if (R<=L) continue;
            std::vector<int> ids; while (mvIdx<movs.size() && movs[mvIdx].x < R){ if (movs[mvIdx].x>=L) ids.push_back(movs[mvIdx].id); ++mvIdx; }
            if (ids.empty()) continue;
            std::sort(ids.begin(), ids.end(), [&](int a,int b){return d.insts[a].x < d.insts[b].x;});
            int cursor=L;
            for (int id : ids) {
                int w=d.macros[d.insts[id].macroId].width; int rightBound=R-w;
                int snapped = row.xStep? row.originX + ((d.insts[id].x-row.originX)/row.xStep)*row.xStep : d.insts[id].x;
                int target = std::max(cursor, snapped); if (target>rightBound) target=rightBound;
                d.insts[id].x = std::clamp(target, L, rightBound); d.insts[id].y=row.originY; cursor=d.insts[id].x + w;
            }
        }
    }
}

// Final legalization: minimal-move sweep (no extra compression)
void finalLegalize(Design& d) {
    // Run a couple of minimal right-push passes to clear residual overlaps
    legalizeAndAlign(d);
    legalizeAndAlign(d);
}

// ================================================================
// performDetailPlacement: integrate Pan-style passes already split out
// ================================================================
void performDetailPlacement(Design& d) {
    auto hpwl = [&](){ return hpwlCellLL(d); };
    int64_t hpwl_init = hpwl();
    std::cout << "==== fastDP Begin ====\n";
    std::cout << "[Init] HPWL="<< hpwl_init << "\n";

    // Snapshot for guard
    struct XY{int x,y;}; std::vector<XY> base(d.insts.size()); for(size_t i=0;i<d.insts.size();++i) base[i]={d.insts[i].x,d.insts[i].y};

    // 1. Skip initial clustering (it often worsens). Start from current placement.
    int64_t best = hpwl_init;

    // 2. Iterative optimization outer loop
    for(int outer=0; outer<20; ++outer){
        std::cout << "-- Outer Iter "<<outer<<" --\n";
        bool any=false; int64_t prev=hpwl();
        for(int inner=0; inner<6; ++inner){
            std::cout << "   Inner "<<inner<<"\n";
            bool changed=false;
            // Inner-iteration snapshot
            std::vector<XY> innerSnap(d.insts.size()); for(size_t i=0;i<d.insts.size();++i) innerSnap[i]={d.insts[i].x,d.insts[i].y};

            // Prefer safe local reorder first
            bool lr = false;
            // Phase A: gentle nudges towards desired X (larger step)
            lr |= performNudgeTowardsDesiredX(d, 4);
            // Phase B: median-of-pins reorder (coarse alignment)
            lr |= performCOMReorder(d);
            // Phase C: net-driven compaction on top-N nets (repeat)
            lr |= performNetDrivenCompaction(d, 200, 3);
            lr |= performNetDrivenCompaction(d, 140, 2);
            // sliding window refinements
            lr |= performLocalReorder(d, 7);
            lr |= performLocalReorder(d, 5);
            lr |= performLocalReorder(d, 3);
            if (lr) changed = true;
            legalizeAndAlign(d);
            int64_t cur = hpwl(); double delta = 100.0*(prev - cur)/ std::max<int64_t>(1, prev);
            if (cur >= prev) { // revert if no gain
                for(size_t i=0;i<d.insts.size();++i){ d.insts[i].x=innerSnap[i].x; d.insts[i].y=innerSnap[i].y; }
                legalizeAndAlign(d);
                cur = hpwl();
                changed = false;
            } else {
                prev = cur; if(cur < best){ best=cur; any=true; }
            }

            // Vertical swap with accept/reject
            std::vector<XY> snapVS(d.insts.size()); for(size_t i=0;i<d.insts.size();++i) snapVS[i]={d.insts[i].x,d.insts[i].y};
            int64_t beforeVS = hpwl();
            if (performVerticalSwap(d)) {
                legalizeAndAlign(d);
                int64_t afterVS = hpwl();
                if (afterVS < beforeVS) { changed = true; prev = afterVS; if(afterVS < best){ best=afterVS; any=true; } }
                else { for(size_t i=0;i<d.insts.size();++i){ d.insts[i].x=snapVS[i].x; d.insts[i].y=snapVS[i].y; } }
            }

            // Small-region global swap with accept/reject (wider search)
            std::vector<XY> snapGS(d.insts.size()); for(size_t i=0;i<d.insts.size();++i) snapGS[i]={d.insts[i].x,d.insts[i].y};
            int64_t beforeGS = hpwl();
            if (performGlobalSwap_OptRegion(d, 240, 3)) {
                legalizeAndAlign(d);
                int64_t afterGS = hpwl();
                if (afterGS < beforeGS) { changed = true; prev = afterGS; if(afterGS < best){ best=afterGS; any=true; } }
                else { for(size_t i=0;i<d.insts.size();++i){ d.insts[i].x=snapGS[i].x; d.insts[i].y=snapGS[i].y; } }
            }

            cur = hpwl();
            delta = 100.0 * (hpwl_init - cur) / std::max<int64_t>(1, hpwl_init);
            std::cout << "      Inner " << inner 
                      << " done. Current HPWL=" << cur 
                      << " (" << std::fixed << std::setprecision(2) << delta << "% improved)\n";

            if(!changed) break; // diminishing or no gain
        }

        // ✅ 每個 outer 結束都印一次
        int64_t outerHPWL = hpwl();
        double outerDelta = 100.0 * (hpwl_init - outerHPWL) / std::max<int64_t>(1, hpwl_init);
        std::cout << "   [After Outer " << outer 
                  << "] HPWL=" << outerHPWL 
                  << " (" << std::fixed << std::setprecision(2) << outerDelta 
                  << "% improved)\n";

        if(!any) break;
    }

    finalLegalize(d); int64_t hpwl_final = hpwl();
    double improvePct = 100.0*(hpwl_init - hpwl_final)/hpwl_init;
    std::cout << "[Final] HPWL="<<hpwl_final<<" (Improvement="<<std::fixed<<std::setprecision(2)<<improvePct<<"%)\n";

    // Guard disabled: keep the best obtained placement without reverting to base
    std::cout << "==== fastDP End ====\n";
}

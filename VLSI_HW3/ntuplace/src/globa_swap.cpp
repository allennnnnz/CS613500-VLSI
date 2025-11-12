// ================================================================
// Global Swap (Pan et al. 2005)
// ================================================================
#include "dp_stage.hpp"
#include <algorithm>
#include <unordered_set>
#include <functional>
#include <iostream>
#include <climits>
#include <cmath>

// ------------------------------------------------------------
// Helper implementations required by dp_stage.hpp for global swap
// ------------------------------------------------------------

std::vector<Seg> buildRowSegments(const Design& d, const Row& row) {
    std::vector<Seg> segs;
    // collect fixed instances in this row
    struct Item{int x,w;};
    std::vector<Item> fixeds;
    for (int i=0;i<(int)d.insts.size();++i){
        const auto& I = d.insts[i];
        if (!I.fixed || I.y != row.originY) continue;
        int w = d.macros[I.macroId].width;
        fixeds.push_back({I.x, w});
    }
    std::sort(fixeds.begin(), fixeds.end(), [](const Item&a, const Item&b){return a.x < b.x;});
    int L = row.originX;
    int R = row.originX + row.xStep * row.numSites;
    int cur = L;
    for (auto &fx : fixeds){
        if (fx.x > cur) segs.push_back({cur, std::min(fx.x, R)});
        cur = std::max(cur, fx.x + fx.w);
        if (cur >= R) break;
    }
    if (cur < R) segs.push_back({cur, R});
    // remove invalid
    segs.erase(std::remove_if(segs.begin(), segs.end(), [](const Seg& s){return s.R <= s.L;}), segs.end());
    return segs;
}

std::vector<int> collectMovablesInRow(const Design& d, const Row& row) {
    std::vector<int> ids;
    ids.reserve(d.insts.size());
    #pragma omp parallel for
    for (int i=0;i<(int)d.insts.size();++i){
        if (d.insts[i].y==row.originY && !d.insts[i].fixed)
            ids.push_back(i);
    }
    std::sort(ids.begin(), ids.end(), [&](int a,int b){return d.insts[a].x < d.insts[b].x;});
    return ids;
}

void packRangeInRow(Design& d, const Row& row, int segL, int segR,
                    const std::vector<int>& idsInRange) {
    if (idsInRange.empty()) return;
    std::vector<int> ids = idsInRange;
    std::sort(ids.begin(), ids.end(), [&](int a,int b){return d.insts[a].x < d.insts[b].x;});
    int step = std::max(1, row.xStep);
    int cursor = segL;
    std::vector<int> w(ids.size());
    for (size_t i=0;i<ids.size();++i) w[i] = d.macros[d.insts[ids[i]].macroId].width;
    for (size_t i=0;i<ids.size();++i){
        int id = ids[i];
        int rightMax = segR - w[i];
        int snapped = (step>1)? row.originX + ((d.insts[id].x - row.originX)/step)*step : d.insts[id].x;
        int target = std::max(cursor, snapped);
        if (target > rightMax) target = rightMax;
        if (step>1) target = row.originX + ((target - row.originX + step - 1)/step)*step; // ceil
        d.insts[id].x = std::clamp(target, segL, rightMax);
        cursor = d.insts[id].x + w[i];
    }
    // Backward adjust if overflow
    if (cursor > segR){
        int overflow = cursor - segR;
        for (int i=(int)ids.size()-1; i>=0 && overflow>0; --i){
            int id = ids[i];
            int leftBound = (i==0) ? segL : (d.insts[ids[i-1]].x + d.macros[d.insts[ids[i-1]].macroId].width);
            int movable = d.insts[id].x - leftBound;
            int take = std::min(movable, overflow);
            if (take>0){
                int newX = d.insts[id].x - take;
                if (step>1) newX = row.originX + ((newX - row.originX)/step)*step; // floor
                take = d.insts[id].x - newX;
                d.insts[id].x = std::max(leftBound, newX);
                overflow -= take;
            }
        }
    }
}

std::vector<int> collectLocalWindow(const Design& d, const Row& row,
                                    const std::vector<int>& movByX,
                                    int xL, int xR, int K) {
    if (movByX.empty()) return {};
    // movByX is sorted by x
    std::vector<int> idxMap(d.insts.size(), -1);
    for (size_t i=0;i<movByX.size();++i) idxMap[movByX[i]] = (int)i;
    auto inWindow = [&](int id){
        int x = d.insts[id].x; int w = d.macros[d.insts[id].macroId].width;
        return (x < xR) && (x + w > xL);
    };
    int first=-1, last=-1;
    for (size_t i=0;i<movByX.size();++i){
        int id = movByX[i];
        if (inWindow(id)) { if (first==-1) first=(int)i; last=(int)i; }
        else if (last!=-1 && d.insts[id].x >= xR) break;
    }
    if (first==-1) return {};
    int iL = std::max(0, first - K);
    int iR = std::min((int)movByX.size()-1, last + K);
    std::vector<int> out;
    out.reserve(iR - iL + 1);
    for (int i=iL; i<=iR; ++i) out.push_back(movByX[i]);
    return out;
}

std::vector<Gap> buildGapsInSegment(const Design& d,
                                    const Row& row,
                                    const Seg& seg,
                                    const std::vector<int>& movSortedByX) {
    std::vector<Gap> gaps;
    int cursor = seg.L;
    for (int id : movSortedByX){
        const auto& I = d.insts[id];
        if (I.y != row.originY || I.fixed) continue;
        int w = d.macros[I.macroId].width;
        if (I.x >= seg.R) break;
        if (I.x + w <= seg.L) continue;
        int x = std::max(I.x, seg.L);
        if (x > cursor) gaps.push_back({cursor, std::min(x, seg.R)});
        cursor = std::max(cursor, std::min(seg.R, I.x + w));
    }
    if (cursor < seg.R) gaps.push_back({cursor, seg.R});
    // remove invalid
    gaps.erase(std::remove_if(gaps.begin(), gaps.end(), [](const Gap& g){return g.R <= g.L;}), gaps.end());
    return gaps;
}

static inline const Row* findRowByY(const Design& d, int y) {
    for (const auto& r : d.layout.rows) if (r.originY == y) return &r;
    return nullptr;
}
static inline int cellWidth(const Design& d, int id) {
    return d.macros[d.insts[id].macroId].width;
}
static inline int snapToSiteX(const Row& row, int x) {
    if (row.xStep <= 0) return x;
    int dx = x - row.originX;
    int q  = (dx >= 0) ? (dx + row.xStep/2) / row.xStep
                       : (dx - row.xStep/2) / row.xStep;
    return row.originX + q * row.xStep;
}

// ----------------------------------------------------------------
// Optimal Region by median-of-bounding-box edges
// ----------------------------------------------------------------
static std::pair<int,int> optimalRegionX(const Design& d, int A) {
    std::vector<int> xs;
    for (int nid : d.inst2nets[A]) {
        const Net& net = d.nets[nid];
        int nMin = INT_MAX, nMax = INT_MIN;
        for (auto& p : net.pins) {
            if (!p.isIO && p.instId == A) continue;
            int px = p.isIO ? p.ioLoc.x : d.insts[p.instId].x;
            nMin = std::min(nMin, px);
            nMax = std::max(nMax, px);
        }
        if (nMin <= nMax) {
            xs.push_back(nMin);
            xs.push_back(nMax);
        }
    }
    if (xs.empty()) return {d.insts[A].x, d.insts[A].x};
    std::sort(xs.begin(), xs.end());
    int mid = xs.size() / 2;
    if (xs.size() % 2 == 1) return {xs[mid], xs[mid]};
    return {xs[mid-1], xs[mid]};
}

// ----------------------------------------------------------------
// Overlap penalty model (P1, P2)  [Pan et al. Eq.(1)]
// ----------------------------------------------------------------
static double computeOverlapPenalty(const Design& d,
                                    const Row& row,
                                    int idA, int idB)
{
    int wA = cellWidth(d, idA);
    int wB = (idB >= 0) ? cellWidth(d, idB) : 0;
    int delta = wA - wB;
    if (delta <= 0) return 0.0;

    // collect nearest spaces s1..s4
    double s[4] = {0,0,0,0};
    int foundL=0, foundR=0;
    std::vector<int> movs;
    for (int i=0;i<(int)d.insts.size();++i)
        if (!d.insts[i].fixed && d.insts[i].y==row.originY)
            movs.push_back(i);
    std::sort(movs.begin(), movs.end(),
              [&](int a,int b){return d.insts[a].x < d.insts[b].x;});

    int xB = (idB >= 0)? d.insts[idB].x : d.insts[idA].x;
    for (int id : movs) {
        int right = d.insts[id].x + cellWidth(d,id);
        if (right <= xB && foundL < 2) s[1-foundL++] = xB - right;
        else if (d.insts[id].x >= xB+wB && foundR < 2)
            s[2+foundR++] = d.insts[id].x - (xB+wB);
        if (foundL>=2 && foundR>=2) break;
    }

    double s2s3 = s[1] + s[2];
    double S1 = s[0]+s[1]+s[2]+s[3];
    const double wt1=1.0, wt2=5.0;  // wt2 ≫ wt1
    double P1 = std::max(0.0, (delta - s2s3)) * wt1;
    double P2 = std::max(0.0, (delta - S1)) * wt2;
    return P1 + P2;
}

// ----------------------------------------------------------------
// Benefit function: B = (W1 - W2) - (P1 + P2)
// ----------------------------------------------------------------
static double computeBenefit(Design& d, const Row& row,
                             const Seg& seg, int A, int B,
                             const std::vector<int>& localIDs,
                             std::function<void()> mutate)
{
    int64_t W1 = partialHPWL(d, localIDs);

    // backup x
    std::vector<int> saveX; saveX.reserve(localIDs.size());
    for (int id : localIDs) saveX.push_back(d.insts[id].x);

    mutate();  // apply swap or move
    int64_t W2 = partialHPWL(d, localIDs);

    // restore
    for (size_t i=0;i<localIDs.size();++i)
        d.insts[localIDs[i]].x = saveX[i];

    double penalty = computeOverlapPenalty(d, row, A, B);
    return (double)(W1 - W2) - penalty;
}

// ----------------------------------------------------------------
// Main: performGlobalSwap_OptRegion
// ----------------------------------------------------------------
bool performGlobalSwap_OptRegion(Design& d,
                                 int windowSites,
                                 int neighborK)
{
    struct Op { enum {Swap, MoveIntoGap} type; int A=-1,B=-1; int newX=0,rowY=0; double ben=0; };
    Op best;
    std::unordered_set<int> dirtyRows;  // 延後合法化用

    for (const Row& row : d.layout.rows) {
        auto segs = buildRowSegments(d, row);
        auto movs = collectMovablesInRow(d, row);
        if (movs.empty() || segs.empty()) continue;

        for (int A : movs) {
            if (d.insts[A].fixed) continue;
            auto [optL,optR] = optimalRegionX(d,A);
            if (optL > optR) std::swap(optL,optR);

            int W = windowSites * row.xStep;
            int wL = std::min(d.insts[A].x,optL) - W;
            int wR = std::max(d.insts[A].x,optR) + W;

            for (const auto& seg : segs) {
                if (seg.R<=wL || seg.L>=wR) continue;

                // (1) Swap with cell B
                for (int B : movs) {
                    if (B==A) continue;
                    const auto& IB = d.insts[B];
                    if (IB.x<seg.L||IB.x>=seg.R) continue;
                    if (IB.x<optL||IB.x>optR) continue;

                    int xL = std::min(d.insts[A].x,d.insts[B].x);
                    int xR = std::max(d.insts[A].x+cellWidth(d,A),
                                      d.insts[B].x+cellWidth(d,B));
                    auto ids = collectLocalWindow(d,row,movs,xL,xR,neighborK);
                    if (ids.size()<2) continue;

                    double ben = computeBenefit(d,row,seg,A,B,ids,[&](){
                        std::swap(d.insts[A].x,d.insts[B].x);
                    });
                    if (ben > best.ben) {
                        best = {Op::Swap, A, B, 0, row.originY, ben};
                        dirtyRows.insert(row.originY);
                    }
                }

                // (2) MoveIntoGap
                auto gaps = buildGapsInSegment(d,row,seg,movs);
                int wA = cellWidth(d,A);
                for (auto& g : gaps) {
                    int GL = std::max({g.L,optL,seg.L});
                    int GR = std::min({g.R,optR,seg.R});
                    if (GR - GL < wA) continue;
                    int target = snapToSiteX(row,(GL+GR-wA)/2);
                    target = std::clamp(target,GL,GR-wA);

                    int xL = std::min(d.insts[A].x,target);
                    int xR = std::max(d.insts[A].x+wA,target+wA);
                    auto ids = collectLocalWindow(d,row,movs,xL,xR,neighborK);
                    if (ids.empty()) ids.push_back(A);

                    double ben = computeBenefit(d,row,seg,A,-1,ids,[&](){
                        d.insts[A].x = target;
                    });
                    if (ben > best.ben) {
                        best = {Op::MoveIntoGap, A, -1, target, row.originY, ben};
                        dirtyRows.insert(row.originY);
                    }
                }
            }
        }
    }

    // 套用最佳操作
    if (best.ben > 0) {
        if (best.type == Op::Swap)
            std::swap(d.insts[best.A].x, d.insts[best.B].x);
        else
            d.insts[best.A].x = best.newX;
        dirtyRows.insert(best.rowY);

        std::cout << "  [GlobalSwap-OptRegion] applied op, benefit=" << best.ben << "\n";
    } else {
        std::cout << "  [GlobalSwap-OptRegion] no beneficial op\n";
        return false;
    }

    // ------------------------------------------------------------
    // 延後合法化：對所有 dirtyRows 做一次完整 repack
    // ------------------------------------------------------------
    for (int y : dirtyRows) {
        const Row* row = findRowByY(d, y);
        if (!row) continue;
        auto segs = buildRowSegments(d, *row);
        auto movs = collectMovablesInRow(d, *row);
        for (const auto& seg : segs) {
            auto ids = collectLocalWindow(d, *row, movs, seg.L, seg.R, 0);
            if (!ids.empty())
                packRangeInRow(d, *row, seg.L, seg.R, ids);
        }
    }

    return true;
}
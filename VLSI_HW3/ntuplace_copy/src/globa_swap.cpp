#include "design.hpp"
#include <algorithm>
#include <vector>
#include <limits>
#include <iostream>

// =============================================================
// 1️⃣ Optimal Region (Section III-A.1)
// =============================================================
RectI computeOptimalRegion(const Design& d, int instId)
{
    std::vector<int> xs, ys;

    for (int nid : d.inst2nets[instId]) {
        const auto& net = d.nets[nid];
        if (net.isSpecial || net.pins.size() <= 1) continue;

        int xmin = std::numeric_limits<int>::max();
        int xmax = std::numeric_limits<int>::min();
        int ymin = std::numeric_limits<int>::max();
        int ymax = std::numeric_limits<int>::min();

        for (const auto& p : net.pins) {
            if (p.instId == instId) continue;
            int px, py;
            if (p.isIO) {
                auto it = d.ioPinLoc.find(p.ioName);
                if (it != d.ioPinLoc.end()) { px = it->second.x; py = it->second.y; }
                else { px = p.ioLoc.x; py = p.ioLoc.y; }
            } else {
                const auto& inst = d.insts[p.instId];
                px = inst.x; py = inst.y;
            }
            xmin = std::min(xmin, px);
            xmax = std::max(xmax, px);
            ymin = std::min(ymin, py);
            ymax = std::max(ymax, py);
        }

        if (xmin == std::numeric_limits<int>::max()) continue;
        xs.push_back(xmin); xs.push_back(xmax);
        ys.push_back(ymin); ys.push_back(ymax);
    }

    if (xs.empty() || ys.empty()) {
        const auto& inst = d.insts[instId];
        return RectI{inst.x, inst.y, inst.x, inst.y};
    }

    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    int nX = xs.size(), nY = ys.size();
    int xlow  = xs[(nX - 1) / 2];
    int xhigh = xs[nX / 2];
    int ylow  = ys[(nY - 1) / 2];
    int yhigh = ys[nY / 2];
    return RectI{xlow, ylow, xhigh, yhigh};
}

// =============================================================
// 2️⃣ Overlap Penalty (Section III-A.2)
// =============================================================
double computeOverlapPenalty(const Design& d, int i, int j,
                             double wt1 = 1.0, double wt2 = 5.0)
{
    const Inst& ci = d.insts[i];
    const Inst& cj = d.insts[j];
    if (ci.fixed || cj.fixed) return 1e9;

    const Macro& mi = d.macros[ci.macroId];
    const Macro& mj = d.macros[cj.macroId];
    int wi = mi.width;
    int wj = mj.width;
    int overlapW = std::max(0, wi - wj);
    if (overlapW <= 0) return 0.0;

    // 找 j 所在的 row
    const Row* row = nullptr;
    for (const auto& r : d.layout.rows)
        if (r.originY == cj.y) { row = &r; break; }
    if (!row) return 0.0;

    // 收集該 row 所有 cell 區間
    struct CellSeg { int x1, x2; };
    std::vector<CellSeg> segs;
    for (const auto& ins : d.insts)
        if (ins.y == row->originY)
            segs.push_back({ins.x, ins.x + d.macros[ins.macroId].width});
    std::sort(segs.begin(), segs.end(),
              [](auto& a, auto& b){ return a.x1 < b.x1; });

    // 找 s1~s4
    double s1=0, s2=0, s3=0, s4=0;
    for (int k=0; k<(int)segs.size(); ++k) {
        if (segs[k].x1 == cj.x && segs[k].x2 == cj.x + wj) {
            if (k>=1) s2 = segs[k].x1 - segs[k-1].x2;
            if (k>=2) s1 = segs[k-1].x1 - segs[k-2].x2;
            if (k+1<(int)segs.size()) s3 = segs[k+1].x1 - segs[k].x2;
            if (k+2<(int)segs.size()) s4 = segs[k+2].x1 - segs[k+1].x2;
            break;
        }
    }

    double S1 = std::max(0.0, s1+s2+s3+s4);
    double diff = (double)(wi - wj);
    double P1 = std::max(0.0, diff - (s2 + s3)) * wt1;
    double P2 = std::max(0.0, diff - S1) * wt2;
    return P1 + P2;
}

// =============================================================
// 3️⃣ Global Swap Based on Benefit (Section III-A.3)
// =============================================================
void performGlobalSwap(Design& d)
{
    const double wt1 = 1.0, wt2 = 5.0;
    int swapCount = 0;

    for (int i = 0; i < (int)d.insts.size(); ++i) {
        if (d.insts[i].fixed) continue;
        RectI optR = computeOptimalRegion(d, i);

        double bestB = 0.0;
        int bestJ = -1;

        for (int j = 0; j < (int)d.insts.size(); ++j) {
            if (i == j || d.insts[j].fixed) continue;

            const auto& J = d.insts[j];
            if (J.x < optR.x1 || J.x > optR.x2) continue;
            if (J.y < optR.y1 || J.y > optR.y2) continue;

            double W1 = d.totalHPWL();
            std::swap(d.insts[i].x, d.insts[j].x);
            std::swap(d.insts[i].y, d.insts[j].y);
            double W2 = d.totalHPWL();
            std::swap(d.insts[i].x, d.insts[j].x);
            std::swap(d.insts[i].y, d.insts[j].y);

            double penalty = computeOverlapPenalty(d, i, j, wt1, wt2);
            double B = (W1 - W2) - penalty;

            if (B > bestB) {
                bestB = B;
                bestJ = j;
                if (B > 0.0) break; // first good
            }
        }

        if (bestJ >= 0 && bestB > 0.0) {
            std::swap(d.insts[i].x, d.insts[bestJ].x);
            std::swap(d.insts[i].y, d.insts[bestJ].y);
            ++swapCount;
        }
    }

    std::cout << "[GlobalSwap] Swaps performed: " << swapCount << std::endl;
}
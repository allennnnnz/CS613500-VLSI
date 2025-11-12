#include "dp_stage.hpp"
#include <unordered_set>
#include <cmath>
#include <iostream>
#include <omp.h>

// 宣告（若未包含）
extern RectI computeOptimalRegion(const Design&, int);
extern double computeOverlapPenalty(const Design&, int, int, double, double);

// 局部 HPWL（僅重算 i,j 相關 net）
static inline double localHPWL(const Design& d, int i, int j) {
    std::unordered_set<int> nets;
    for (int nid : d.inst2nets[i]) nets.insert(nid);
    for (int nid : d.inst2nets[j]) nets.insert(nid);

    double hpwl = 0;
    for (int nid : nets) hpwl += d.netHPWL(nid);
    return hpwl;
}

bool performVerticalSwap(Design& d)
{
    bool improved = false;
    int swapCount = 0;
    const double wt1 = 1.0, wt2 = 5.0;
    auto t0 = omp_get_wtime();

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < (int)d.insts.size(); ++i) {
        auto& cell = d.insts[i];
        if (cell.fixed) continue;

        RectI optR = computeOptimalRegion(d, i);
        int optCenterY = (optR.y1 + optR.y2) / 2;
        int curY = cell.y;

        int dy = (optCenterY > curY) ? +1 : (optCenterY < curY ? -1 : 0);
        if (dy == 0) continue;

        int rowHeight = d.macros[cell.macroId].height;
        if (rowHeight <= 0 && !d.layout.rows.empty())
            rowHeight = d.layout.rows.front().xStep;
        int newY = curY + dy * rowHeight;

        const Row* targetRow = nullptr;
        for (const auto& r : d.layout.rows)
            if (r.originY == newY) { targetRow = &r; break; }
        if (!targetRow) continue;

        // 限制候選數量 (避免整 row 掃)
        std::vector<int> candidates;
        candidates.reserve(8);
        for (int j = 0; j < (int)d.insts.size(); ++j) {
            if (d.insts[j].fixed || d.insts[j].y != targetRow->originY) continue;
            if (std::abs(d.insts[j].x - cell.x) < 10 * d.layout.rows.front().xStep)
                candidates.push_back(j);
        }

        double bestB = 0.0;
        int bestJ = -1;
        for (int j : candidates) {
            double W1 = localHPWL(d, i, j);
            std::swap(d.insts[i].y, d.insts[j].y);
            double W2 = localHPWL(d, i, j);
            std::swap(d.insts[i].y, d.insts[j].y);

            double penalty = computeOverlapPenalty(d, i, j, wt1, wt2);
            double B = (W1 - W2) - penalty;

            if (B > bestB) {
                bestB = B;
                bestJ = j;
            }
        }

        if (bestJ >= 0 && bestB > 0.0) {
            #pragma omp critical
            {
                std::swap(d.insts[i].y, d.insts[bestJ].y);
                ++swapCount;
                improved = true;
            }
        }
    }

    auto t1 = omp_get_wtime();
    std::cout << "[VerticalSwap-OMP] Performed " << swapCount
              << " swaps, Time=" << (t1 - t0) << "s\n";
    return improved;
}
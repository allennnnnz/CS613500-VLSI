#include "dp_stage.hpp"
#include <algorithm>
#include <vector>
#include <unordered_set>
#include <iostream>
#include <omp.h>

// 局部 HPWL（只重算 window 內 cell 相關的 nets）
static inline double localHPWL(const Design& d, const std::vector<int>& cellIds) {
    std::unordered_set<int> nets;
    for (int id : cellIds)
        for (int nid : d.inst2nets[id])
            nets.insert(nid);

    double hpwl = 0;
    for (int nid : nets)
        hpwl += d.netHPWL(nid);
    return hpwl;
}

bool performLocalReorder(Design& d, int windowSize)
{
    if (d.segments.empty())
        buildSegments(d);

    bool improved = false;
    int reorderCount = 0;
    auto t0 = omp_get_wtime();

    // 每個 segment 可獨立 → OpenMP 平行
    #pragma omp parallel for schedule(dynamic)
    for (int s = 0; s < (int)d.segments.size(); ++s) {
        auto& seg = d.segments[s];
        auto& cellIds = seg.cellIds;
        if ((int)cellIds.size() < windowSize) continue;

        std::sort(cellIds.begin(), cellIds.end(),
                  [&](int a, int b){ return d.insts[a].x < d.insts[b].x; });

        for (int start = 0; start + windowSize <= (int)cellIds.size(); ++start) {
            std::vector<int> window(cellIds.begin() + start,
                                    cellIds.begin() + start + windowSize);

            int leftX = d.insts[window.front()].x;
            int rightX = d.insts[window.back()].x +
                         d.macros[d.insts[window.back()].macroId].width;
            int totalWidth = rightX - leftX;

            double bestHPWL = localHPWL(d, window);
            std::vector<int> bestOrder = window;

            // 用 index permutation，而不是直接改 inst id
            std::sort(window.begin(), window.end());
            do {
                int sumW = 0;
                for (int id : window)
                    sumW += d.macros[d.insts[id].macroId].width;
                int spacing = (windowSize > 1)
                              ? (totalWidth - sumW) / (windowSize - 1)
                              : 0;

                // 暫時更新位置
                int curX = leftX;
                for (int id : window) {
                    d.insts[id].x = curX;
                    curX += d.macros[d.insts[id].macroId].width + spacing;
                }

                double hpwl = localHPWL(d, window);
                if (hpwl < bestHPWL) {
                    bestHPWL = hpwl;
                    bestOrder = window;
                }

            } while (std::next_permutation(window.begin(), window.end()));

            // 應用最佳排列
            int curX = leftX;
            int sumW = 0;
            for (int id : bestOrder)
                sumW += d.macros[d.insts[id].macroId].width;
            int spacing = (windowSize > 1)
                          ? (totalWidth - sumW) / (windowSize - 1)
                          : 0;

            for (int id : bestOrder) {
                d.insts[id].x = curX;
                curX += d.macros[d.insts[id].macroId].width + spacing;
            }

            if (bestOrder != window) {
                #pragma omp atomic
                ++reorderCount;
                improved = true;
            }
        }
    }

    auto t1 = omp_get_wtime();
    std::cout << "[LocalReorder-OMP] Groups re-ordered: " << reorderCount
              << "  Time=" << (t1 - t0) << "s\n";
    return improved;
}
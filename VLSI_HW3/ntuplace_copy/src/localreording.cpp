#include "dp_stage.hpp"
#include "design.hpp"
#include <algorithm>
#include <vector>
#include <iostream>
#include <limits>

// ================================================================
// Build Segments (Pan et al., 2005 Section III-A.6)
// ==============================================================
// ================================================================
// Local Reordering (Pan et al., 2005 Section III-A.5)
// ================================================================
// For each segment, take n=3 consecutive cells, try all permutations,
// keep boundaries fixed, evenly distribute cells, pick min HPWL.
// ================================================================
bool performLocalReorder(Design& d, int windowSize )
{
    if (d.segments.empty())
        buildSegments(d);

    bool improved = false;
    int reorderCount = 0;

    // Step 1️⃣: process each segment independently
    for (auto& seg : d.segments) {
        auto& cellIds = seg.cellIds;
        if ((int)cellIds.size() < windowSize) continue;

        // sort cells in this segment by x
        std::sort(cellIds.begin(), cellIds.end(),
                  [&](int a, int b){ return d.insts[a].x < d.insts[b].x; });

        // Step 2️⃣: sliding window of size n=3
        for (int start = 0; start + windowSize <= (int)cellIds.size(); ++start) {
            std::vector<int> window(cellIds.begin() + start,
                                    cellIds.begin() + start + windowSize);

            int leftX = d.insts[window.front()].x;
            int rightX = d.insts[window.back()].x +
                         d.macros[d.insts[window.back()].macroId].width;
            int totalWidth = rightX - leftX;

            // store original coordinates
            std::vector<int> origX(windowSize);
            for (int k = 0; k < windowSize; ++k)
                origX[k] = d.insts[window[k]].x;

            // Step 3️⃣: try all permutations (3! = 6)
            std::vector<int> bestOrder = window;
            double bestHPWL = d.totalHPWL();

            std::sort(window.begin(), window.end());
            do {
                int sumW = 0;
                for (int id : window)
                    sumW += d.macros[d.insts[id].macroId].width;
                int spacing = (windowSize > 1)
                              ? (totalWidth - sumW) / (windowSize - 1)
                              : 0;

                int curX = leftX;
                for (int id : window) {
                    d.insts[id].x = curX;
                    curX += d.macros[d.insts[id].macroId].width + spacing;
                }

                double hpwl = d.totalHPWL();
                if (hpwl < bestHPWL) {
                    bestHPWL = hpwl;
                    bestOrder = window;
                }

            } while (std::next_permutation(window.begin(), window.end()));

            // Step 4️⃣: apply best ordering if improved
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
                ++reorderCount;
                improved = true;
            }
        }
    }

    std::cout << "[LocalReorder] Groups re-ordered: " << reorderCount << std::endl;
    return improved;
}
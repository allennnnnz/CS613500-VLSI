#include "dp_stage.hpp"
#include <algorithm>
#include <iostream>
#include <unordered_set>
#include <vector>
#include <climits>
#include <numeric>
#include <iomanip>

// ================================================================
// Local Reordering (Pan et al., DAC 2005)
// ================================================================
// For each row, a sliding window of W cells is evaluated.
// For each window, all permutations (W!) are tested.
// HPWL is recomputed only for nets connected to window cells.
// The best permutation (min HPWL) is applied.
// After all rows, the placement is re-legalized.
// ================================================================

// ---------- Compute local HPWL (only affected nets) ----------
static int64_t localHPWL(const Design& d, const std::vector<int>& cellIds)
{
    if (cellIds.empty()) return 0;
    std::unordered_set<int> nets;
    for (int id : cellIds)
        for (int nid : d.inst2nets[id]) nets.insert(nid);

    int64_t total = 0;
    for (int nid : nets) {
        const Net& net = d.nets[nid];
        if (net.isSpecial || net.pins.empty()) continue;
        int xmin = INT_MAX, xmax = INT_MIN;
        int ymin = INT_MAX, ymax = INT_MIN;
        for (const auto& p : net.pins) {
            int px = p.isIO ? p.ioLoc.x : d.insts[p.instId].x;
            int py = p.isIO ? p.ioLoc.y : d.insts[p.instId].y;
            xmin = std::min(xmin, px);
            xmax = std::max(xmax, px);
            ymin = std::min(ymin, py);
            ymax = std::max(ymax, py);
        }
        total += (xmax - xmin) + (ymax - ymin);
    }
    return total;
}

// ---------- Main Function ----------
bool performLocalReorder(Design& d, int windowSize)
{
    if (windowSize < 2) return false;
    bool anyImproved = false;

    for (auto& row : d.layout.rows)
    {
        // Collect movable cells
        std::vector<int> movs;
        for (int i = 0; i < (int)d.insts.size(); ++i)
            if (!d.insts[i].fixed && d.insts[i].y == row.originY)
                movs.push_back(i);

        if ((int)movs.size() < windowSize) continue;
        std::sort(movs.begin(), movs.end(),
                  [&](int a,int b){return d.insts[a].x < d.insts[b].x;});

        // Slide window
        for (int start = 0; start + windowSize <= (int)movs.size(); ++start)
        {
            std::vector<int> window(movs.begin() + start,
                                    movs.begin() + start + windowSize);

            // Determine legal region boundaries
            int leftBound = row.originX;
            if (start > 0) {
                int prev = movs[start - 1];
                leftBound = d.insts[prev].x + d.macros[d.insts[prev].macroId].width;
            }
            int rightBound = row.originX + row.xStep * row.numSites;
            if (start + windowSize < (int)movs.size()) {
                int next = movs[start + windowSize];
                rightBound = d.insts[next].x;
            }

            // Backup original x
            std::vector<int> oldX(windowSize);
            for (int i = 0; i < windowSize; ++i)
                oldX[i] = d.insts[window[i]].x;

            // Compute base HPWL
            int64_t baseHPWL = localHPWL(d, window);
            int64_t bestHPWL = baseHPWL;
            std::vector<int> bestOrder = window;

            // Try all permutations
            std::sort(window.begin(), window.end());
            do {
                // Place tightly from leftBound
                int curX = leftBound;
                for (int id : window) {
                    int w = d.macros[d.insts[id].macroId].width;
                    d.insts[id].x = curX;
                    curX += w;
                }

                int64_t hp = localHPWL(d, window);
                if (hp < bestHPWL) {
                    bestHPWL = hp;
                    bestOrder = window;
                }
            } while (std::next_permutation(window.begin(), window.end()));

            // Restore
            for (int i = 0; i < windowSize; ++i)
                d.insts[window[i]].x = oldX[i];

            // Apply best if improved
            if (bestHPWL < baseHPWL) {
                int curX = leftBound;
                for (int id : bestOrder) {
                    int w = d.macros[d.insts[id].macroId].width;
                    d.insts[id].x = curX;
                    curX += w;
                }
                anyImproved = true;
            }
        }

        // Repack row (legalize after each row)
        auto movRow = collectMovablesInRow(d, row);
        for (const auto& seg : buildRowSegments(d, row)) {
            auto ids = collectLocalWindow(d, row, movRow, seg.L, seg.R, 0);
            if (!ids.empty())
                packRangeInRow(d, row, seg.L, seg.R, ids);
        }
    }

    if (anyImproved)
        std::cout << "  [LocalReorder-Pan2005] HPWL improved\n";
    else
        std::cout << "  [LocalReorder-Pan2005] no improvement\n";

    return anyImproved;
}
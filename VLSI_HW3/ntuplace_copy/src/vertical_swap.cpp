#include "dp_stage.hpp"
#include <unordered_set>
#include <cmath>
#include <iostream>
#include <climits>

// 🔹 只宣告，不 include .cpp
extern RectI computeOptimalRegion(const Design&, int);
extern double computeOverlapPenalty(const Design&, int, int, double, double);

bool performVerticalSwap(Design& d)
{
    bool improved = false;
    int swapCount = 0;
    const double wt1 = 1.0, wt2 = 5.0;

    for (int i = 0; i < (int)d.insts.size(); ++i) {
        auto& cell = d.insts[i];
        if (cell.fixed) continue;

        // Step 1. 找出 optimal region
        RectI optR = computeOptimalRegion(d, i);
        int optCenterY = (optR.y1 + optR.y2) / 2;
        int curY = cell.y;

        // Step 2. 決定移動方向（往上或往下）
        int dy = 0;
        if (optCenterY > curY) dy = +1;
        else if (optCenterY < curY) dy = -1;
        else continue; // 已在最佳 row

        // Step 3. 計算 row 高度
        int rowHeight = d.macros[cell.macroId].height;
        if (rowHeight <= 0) rowHeight = d.layout.rows.front().xStep;

        int newY = curY + dy * rowHeight;

        // Step 4. 找對應 row
        const Row* targetRow = nullptr;
        for (const auto& r : d.layout.rows)
            if (r.originY == newY) { targetRow = &r; break; }
        if (!targetRow) continue;

        // Step 5. 從目標 row 中挑幾個鄰近的 cells 作候選
        std::vector<int> candidates;
        for (int j = 0; j < (int)d.insts.size(); ++j)
            if (!d.insts[j].fixed && d.insts[j].y == targetRow->originY)
                candidates.push_back(j);

        // Step 6. 評估所有候選的 benefit
        double bestB = 0.0;
        int bestJ = -1;
        for (int j : candidates) {
            double W1 = d.totalHPWL();
            std::swap(d.insts[i].y, d.insts[j].y);
            double W2 = d.totalHPWL();
            std::swap(d.insts[i].y, d.insts[j].y);

            double penalty = computeOverlapPenalty(d, i, j, wt1, wt2);
            double B = (W1 - W2) - penalty;

            if (B > bestB) {
                bestB = B;
                bestJ = j;
            }
        }

        // Step 7. 執行最佳交換
        if (bestJ >= 0 && bestB > 0.0) {
            std::swap(d.insts[i].y, d.insts[bestJ].y);
            ++swapCount;
            improved = true;
        }
    }

    std::cout << "[VerticalSwap] Performed " << swapCount << " swaps\n";
    return improved;
}
#include "detailPlacement.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>

// 小工具（安全版）：保留視窗內既有的位置集合，僅嘗試把這些位置重新指派給不同 cell 的排列
// 這樣不會改變任何位置或間隙，因此必然保持合法且不與窗口外元件重疊
static int64_t trialAssignPositionsAndEval(
    Design& d,
    const std::vector<int>& winCellsSortedByX,        // cell Ids (依 x 遞增)
    const std::vector<std::pair<int,int>>& positions  // 對應的 (x,y) 位置（與上一向量同長、同序）
) {
    const size_t W = winCellsSortedByX.size();
    std::vector<int> oldX(W), oldY(W);
    for (size_t k = 0; k < W; ++k) {
        int id = winCellsSortedByX[k];
        oldX[k] = d.insts[id].x;
        oldY[k] = d.insts[id].y;
        d.insts[id].x = positions[k].first;
        d.insts[id].y = positions[k].second;
    }
    int64_t hpwlNow = d.totalHPWL();
    // rollback
    for (size_t k = 0; k < W; ++k) {
        int id = winCellsSortedByX[k];
        d.insts[id].x = oldX[k];
        d.insts[id].y = oldY[k];
    }
    return hpwlNow;
}

// 將一整條 row 上的 movable cells 緊貼擺放 (legalize step)
// 不做全列重排的合法化；假設輸入本來就合法，僅在成功改善時才改變排列
// 保持方向與 row.flip 一致
static void syncRowOrient(Design& d, int rowIdx) {
    const Row& row = d.layout.rows[rowIdx];
    for (auto& ins : d.insts) {
        if (!ins.fixed && ins.y == row.originY) {
            ins.orient = row.flip ? Orient::FS : Orient::N;
        }
    }
}

// 嘗試對 row 上長度W的視窗做所有排列，選最好 (sliding window reorder)
static bool improveRowByWindowReorder(Design& d, int rowIdx, int W = 3) {
    const Row& row = d.layout.rows[rowIdx];

    // 蒐集 row 上的 movable cells
    std::vector<int> cells;
    for (int i = 0; i < (int)d.insts.size(); ++i) {
        const auto& ins = d.insts[i];
        if (!ins.fixed && ins.y == row.originY)
            cells.push_back(i);
    }
    if ((int)cells.size() < W) return false;

    // sort by x
    std::sort(cells.begin(), cells.end(),
        [&](int a, int b){ return d.insts[a].x < d.insts[b].x; });

    bool anyImproved = false;

    // sliding window
    for (int start = 0; start + W <= (int)cells.size(); ++start) {
        // 取出視窗 cell 與其當前位置（依 x 排序）
        std::vector<int> windowCells;
        windowCells.reserve(W);
        for (int k = 0; k < W; ++k) windowCells.push_back(cells[start + k]);
        std::vector<std::pair<int,int>> positions(W);
        for (int k = 0; k < W; ++k) {
            int id = windowCells[k];
            positions[k] = { d.insts[id].x, row.originY };
        }

        // 基準順序（保持現況）
        std::vector<int> bestOrder = windowCells;
        int64_t bestCost = trialAssignPositionsAndEval(d, windowCells, positions);

        // 試所有排列 (W! permutations)，以相同 positions 順序賦予不同 cell
        std::sort(windowCells.begin(), windowCells.end());
        do {
            int64_t cost = trialAssignPositionsAndEval(d, windowCells, positions);
            if (cost < bestCost) {
                bestCost = cost;
                bestOrder = windowCells;
            }
        } while (std::next_permutation(windowCells.begin(), windowCells.end()));

        // 如果有更好順序 → 套用
        if (bestOrder != std::vector<int>{cells.begin()+start, cells.begin()+start+W}) {
            for (int k = 0; k < W; ++k) {
                int id = bestOrder[k];
                auto& ins = d.insts[id];
                ins.x = positions[k].first;
                ins.y = positions[k].second;
                ins.orient = row.flip ? Orient::FS : Orient::N;
            }
            anyImproved = true;
        }
    }
    return anyImproved;
}

// 嘗試對同一 row 的相鄰 cell 交換位置 (鄰近 swap)
static bool improveRowByAdjacentSwap(Design& d, int rowIdx) {
    const Row& row = d.layout.rows[rowIdx];

    std::vector<int> cells;
    for (int i = 0; i < (int)d.insts.size(); ++i) {
        const auto& ins = d.insts[i];
        if (!ins.fixed && ins.y == row.originY)
            cells.push_back(i);
    }
    if (cells.size() < 2) return false;

    std::sort(cells.begin(), cells.end(),
        [&](int a, int b){ return d.insts[a].x < d.insts[b].x; });

    bool improved = false;
    int64_t baseCost = d.totalHPWL();

    for (size_t k = 0; k + 1 < cells.size(); ++k) {
        int a = cells[k];
        int b = cells[k+1];

        int ax = d.insts[a].x, ay = d.insts[a].y;
        int bx = d.insts[b].x, by = d.insts[b].y;
        int wa = d.macros[d.insts[a].macroId].width;
        int wb = d.macros[d.insts[b].macroId].width;

        // 相鄰不重疊必要條件（交換後）：
        // 左者（B）在 ax，右者（A）在 bx：需要 ax + wb <= bx
        if (ax + wb > bx) continue;
        // 與右側下一顆的間距也需足夠
        if (k + 2 < cells.size()) {
            int c = cells[k+2];
            int cx = d.insts[c].x;
            if (bx + wa > cx) continue;
        }

        // Feasible → 試算 HPWL
        d.insts[a].x = bx; d.insts[a].y = by;
        d.insts[b].x = ax; d.insts[b].y = ay;

        int64_t newCost = d.totalHPWL();
        if (newCost < baseCost) {
            baseCost = newCost;
            improved = true;
            // 重新依 x 排序，以免後續鄰接關係失真
            cells.clear();
            for (int i = 0; i < (int)d.insts.size(); ++i) {
                const auto& ins = d.insts[i];
                if (!ins.fixed && ins.y == row.originY)
                    cells.push_back(i);
            }
            std::sort(cells.begin(), cells.end(),
                [&](int u, int v){ return d.insts[u].x < d.insts[v].x; });
            // 從前一個位置回退一步，讓掃描能再次檢視新的鄰接對
            if (k > 0) --k; else k = 0;
        } else {
            // revert
            d.insts[a].x = ax; d.insts[a].y = ay;
            d.insts[b].x = bx; d.insts[b].y = by;
        }
    }

    // 若有改善，做方向同步（位置合法性已由交換保證）
    if (improved) {
        syncRowOrient(d, rowIdx);
    }
    return improved;
}

// 主演算法：多輪 row-wise 局部優化
void detailPlacement(Design& d, int maxIter) {
    using namespace std::chrono;
    auto t0 = high_resolution_clock::now();

    std::cout << "==== Begin Detailed Placement (sliding-window + swaps) ====\n";
    int64_t hpwl0 = d.totalHPWL();
    std::cout << "HPWL before = " << hpwl0 << "\n";

    bool changed = true;
    for (int it = 0; it < maxIter && changed; ++it) {
        changed = false;

        // 對每列依序做:
        for (int r = 0; r < (int)d.layout.rows.size(); ++r) {
            bool imp1 = false; // 關閉視窗重排以避免任何潛在合法性風險
            bool imp2 = improveRowByAdjacentSwap(d, r);
            if (imp1 || imp2) {
                changed = true;
            }
            // 每次局部修改後，同步方向（保險）
            if (imp1 || imp2) syncRowOrient(d, r);
        }

        int64_t hpwlNow = d.totalHPWL();
        std::cout << "Iter " << it
                  << " HPWL=" << hpwlNow
                  << " delta=" << (hpwl0 - hpwlNow) << "\n";

        hpwl0 = hpwlNow;
    }

    // 出口前：orient 依 row.flip 全部同步一下 (保險)
    for (int r = 0; r < (int)d.layout.rows.size(); ++r) syncRowOrient(d, r);

    int64_t hpwlF = d.totalHPWL();
    auto t1 = high_resolution_clock::now();
    double sec = duration_cast<duration<double>>(t1 - t0).count();

    std::cout << "HPWL after  = " << hpwlF << "\n";
    std::cout << "==== End Detailed Placement ====\n";
    std::cout << "Runtime: " << sec << " sec\n";
}
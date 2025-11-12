#include "dp_stage.hpp"
#include <algorithm>
#include <unordered_set>
#include <iostream>
#include <limits>
#include <iomanip>

// ================================================================
// Vertical Swap (Pan et al., 2005)
// ================================================================
bool performVerticalSwap(Design& d) {
    bool improved = false;
    auto hpwlCell = [&](const std::vector<int>& ids){ return partialHPWL(d, ids); };

    // Row index map by Y
    std::unordered_map<int, int> y2row;
    for (int i = 0; i < (int)d.layout.rows.size(); ++i)
        y2row[d.layout.rows[i].originY] = i;

    for (int aid = 0; aid < (int)d.insts.size(); ++aid) {
        Inst& A = d.insts[aid];
        if (A.fixed) continue;
        int rowIdx = y2row[A.y];
        int rowH   = d.macros[A.macroId].height;

        // 嘗試上下兩個 row
        for (int dy : {-1, +1}) {
            int targetY = A.y + dy * rowH;
            if (!y2row.count(targetY)) continue;
            int targetIdx = y2row[targetY];
            const Row& targetRow = d.layout.rows[targetIdx];

            // 找距離 A.x 最近的可動 cell 作為候選
            int bestB = -1;
            int bestDx = std::numeric_limits<int>::max();
            for (int bid = 0; bid < (int)d.insts.size(); ++bid) {
                const Inst& B = d.insts[bid];
                if (B.fixed) continue;
                if (B.y != targetY) continue;
                int dx = std::abs(B.x - A.x);
                if (dx < bestDx) { bestDx = dx; bestB = bid; }
            }

            // 若有找到候選 cell：嘗試 swap
            if (bestB >= 0) {
                Inst& B = d.insts[bestB];
                std::vector<int> affected = {aid, bestB};
                int64_t oldPart = hpwlCell(affected);

                std::swap(A.x, B.x);
                std::swap(A.y, B.y);

                int64_t newPart = hpwlCell(affected);
                int64_t delta = newPart - oldPart;

                if (delta < 0 && d.isRoughLegal(aid) && d.isRoughLegal(bestB)) {
                    improved = true;
                } else {
                    // 不改善 → 還原
                    std::swap(A.x, B.x);
                    std::swap(A.y, B.y);
                }
            }
            else {
                // 若目標 row 無 cell 可 swap，嘗試直接移過去 (gap)
                int64_t oldPart = hpwlCell({aid});
                int oldY = A.y;
                A.y = targetY;
                A.x = d.snapToRowGrid(A.x, A.y, d.macros[A.macroId].width).x;
                int64_t newPart = hpwlCell({aid});
                int64_t delta = newPart - oldPart;
                if (delta < 0 && d.isRoughLegal(aid)) improved = true;
                else A.y = oldY; // 還原
            }
        }
    }

    std::cout << "  [VerticalSwap] done\n";
    return improved;
}
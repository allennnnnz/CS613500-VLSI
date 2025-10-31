// src/detailPlacement.cpp
#include "detailPlacement.hpp"
#include <iostream>

void simpleDetailPlacement(Design& d, int maxIter, int deltaX) {
    std::cout << "==== Begin simple detail placement ====\n";
    int64_t before = d.totalHPWL();
    std::cout << "HPWL before = " << before << "\n";

    for (int iter = 0; iter < maxIter; ++iter) {
        for (auto& inst : d.insts) {
            if (inst.fixed) continue;

            int oldX = inst.x;
            int oldY = inst.y;

            // 嘗試往右移一點
            auto newPos = d.snapToRowGrid(oldX + deltaX, oldY,
                                          d.macros[inst.macroId].width);
            inst.x = newPos.x;
            inst.y = newPos.y;

            int64_t after = d.totalHPWL();
            if (after < before) {
                before = after; // 接受移動
            } else {
                inst.x = oldX;
                inst.y = oldY; // 回復
            }
        }
    }

    int64_t finalHPWL = d.totalHPWL();
    std::cout << "HPWL after  = " << finalHPWL << "\n";
    std::cout << "==== End simple detail placement ====\n";
}
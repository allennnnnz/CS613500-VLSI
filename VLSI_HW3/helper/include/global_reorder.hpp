#ifndef GLOBAL_REORDER_HPP
#define GLOBAL_REORDER_HPP

#include "design_io.hpp"  // 提供 Design/Row/Net/Inst/Pin/Orient 等型別

// Sliding-Window Global Reorder (SW-GR) 參數
struct GlobalReorderConfig {
    // 視窗大小與步長（以 site 為單位）
    int  windowSitesGlobal = 400;
    int  strideSites       = 120;

    // 每個視窗最多處理的 cell 數（控制計算量）
    int  maxCellsPerWindow = 7;

    // 每個視窗最多嘗試的候選排列（啟發式 + 隨機）
    int  permLimit         = 128;

    // DP 成本（以 site 尺度）
    double gammaInternal   = 1.0;   // 無外部 pin：左右端 -x/+x
    double gammaOneSide    = 1.0;   // 單側外部：只對另一側極端課距離
    double lambdaTether    = 0.10;  // 回原位牽引（site 距離）

    // pass 次數（建議至少 2：L->R, R->L 各一輪）
    int  gwPasses          = 2;

    // 隨機種子（控制隨機排列稳定重現）
    unsigned rngSeed       = 2025;

    // 顯示進度
    bool verbose           = false;
};

// 執行 Sliding-Window Global Reorder
void global_reorder_sliding_windows(Design& d, const GlobalReorderConfig& cfg);

#endif // GLOBAL_REORDER_HPP
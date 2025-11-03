#pragma once
#include "design_io.hpp"

// 詳細列放參數（優化後的最佳設置）
struct DetailPlaceConfig {
    int    passes        = 60;     // 60 次迭代達到良好收斂
    int    windowSites   = 1000;   // 非常大的搜索視窗允許充分探索
    double lambdaTether  = 0.0;    // 完全移除 tether，純 HPWL 優化
    double gammaInternal = 0.1;    // 極小的內部寬度懲罰
    double gammaOneSide  = 0.2;    // 極小的單邊懲罰
    bool   verbose       = false;  // 關閉 verbose 以加快執行
};

// 執行 order-preserving 的 row-by-row DP 詳細列放
void detailPlacement(Design& d, const DetailPlaceConfig& cfg = DetailPlaceConfig{});
#pragma once
#include "design.hpp"

struct DetailPlaceConfig {
    int passes = 2;
    int windowSites = 30;
    double lambdaTether = 0.1;
    double gammaInternal = 1.0;
    double gammaOneSide = 1.0;
    bool verbose = false;
};

// 主函式：執行 detailed placement
void detailPlacement(Design& d, const DetailPlaceConfig& cfg = DetailPlaceConfig());

// 符合作業規範的 HPWL 計算（不含 SPECIALNETS）
long long computeTotalHPWL(const Design& d);
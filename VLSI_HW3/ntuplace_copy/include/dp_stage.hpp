#pragma once
#include "design.hpp"
#include <vector>
#include <cstdint>

// 局部 HPWL 計算
int64_t partialHPWL(const Design& d, const std::vector<int>& instIds);

// 局部重排（滑動視窗）
bool performLocalReorder(Design& d, int windowSize = 3);

// 垂直交換（上下 row 嘗試 swap）
bool performVerticalSwap(Design& d);

// 單段聚類（Single-Segment Clustering）
bool performSingleSegClustering(Design& d);

// 全域交換（Optimal Region-based Global Swap）
bool performGlobalSwap_OptRegion(Design& d, int windowSites = 200, int neighborK = 2);

// 中心傾向排序重排（by median-of-pins X）
bool performCOMReorder(Design& d);

// Top-N nets driven compaction (accept-only)
bool performNetDrivenCompaction(Design& d, int topN = 100, int neighborK = 2);

// Small-step nudge towards desired X (median-of-pins), accept-only
bool performNudgeTowardsDesiredX(Design& d, int maxSites = 3);

// 合法化 + 對齊（確保無 overlap、對齊 site grid）
void legalizeAndAlign(Design& d);

// 最終合法化（收尾用）
void finalLegalize(Design& d);

// 詳細放置主流程
void performDetailPlacement(Design& d);
struct Seg { int L, R; };  // Row segment range [L, R)
struct Gap { int L, R; };  // Gap range [L, R)

std::vector<Seg> buildRowSegments(const Design& d, const Row& row);
std::vector<int> collectMovablesInRow(const Design& d, const Row& row);
void packRangeInRow(Design& d, const Row& row, int segL, int segR,
                    const std::vector<int>& idsInRange);
std::vector<int> collectLocalWindow(const Design& d, const Row& row,
                                    const std::vector<int>& movByX,
                                    int xL, int xR, int K=2);
std::vector<Gap> buildGapsInSegment(const Design& d,
                                    const Row& row,
                                    const Seg& seg,
                                    const std::vector<int>& movSortedByX);
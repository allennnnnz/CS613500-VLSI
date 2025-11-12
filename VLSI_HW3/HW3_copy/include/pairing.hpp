#pragma once
#include "design.hpp"
#include <vector>
#include <unordered_map>

struct PairEdge { int u, v; double w; int bx, by; };
struct PairUV  { int u, v; };

struct BinGrid {
    int nx, ny, binW, binH;
    int x0, y0;
    std::vector<std::vector<std::vector<int>>> bins;
};

struct PairingParams {
    int binW = 0, binH = 0;
    int r_target = 0;
    double alpha1 = 2e-3;
    double alpha2 = 2e-4;
    double D_target = 0.35;
};

struct PairingResult {
    std::vector<PairUV> selectedPairs;
    std::vector<int> expandedSingles;
    double avgDensity = 0.0;
};

struct TransformMaps {
    struct Info { bool isPair; int u, v; bool isExpanded; };
    std::vector<Info> newInstInfo;
    std::vector<int>  oldToNew;
};

PairingResult run_pairing_and_selection(const Design& d, const PairingParams& pm);
Design transform_to_all_double_height(const Design& d,
                                      const PairingResult& pr,
                                      TransformMaps& maps);
void unpair_and_refine(Design& dAfterDP, const TransformMaps& maps,
                       void (*runStagePlacement)(Design&));
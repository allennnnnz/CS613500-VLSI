#include "dp_stage.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>

// ================================================================
// Legacy Detailed Placement Pipeline (Pan et al. 2005)
// Note: superseded by performDetailPlacement in dp_stage.cpp
// Kept for reference; not used by main flow.
// ================================================================
// FastDP main pipeline (Pan et al. 2005)
void performDetailPlacement_Legacy(Design& d) {
    double prevHPWL = d.totalHPWL();
    double bestHPWL = prevHPWL;
    std::cout << "[FastDP] Start HPWL = " << prevHPWL << std::endl;

    // Step 1: initial Single-Segment Clustering
    performSingleSegClustering(d);
    double hpwl_after_cluster = d.totalHPWL();
    std::cout << "[SingleSegCluster] HPWL=" << hpwl_after_cluster << std::endl;

    // Step 2: main iteration
    for (int outer = 0; outer < 10; ++outer) {
        double prev_iter_hpwl = d.totalHPWL();
        std::cout << "-- Outer Iter " << outer << " --" << std::endl;

        performGlobalSwap_OptRegion(d);
        std::cout << "  [GlobalSwap] HPWL=" << d.totalHPWL() << std::endl;

        performVerticalSwap(d);
        std::cout << "  [VerticalSwap] HPWL=" << d.totalHPWL() << std::endl;

        performLocalReorder(d, 3);
        std::cout << "  [LocalReorder] HPWL=" << d.totalHPWL() << std::endl;

        if ((prev_iter_hpwl - d.totalHPWL()) / prev_iter_hpwl < 1e-3)
            break; // no significant improvement
    }

    // Step 3: re-clustering until convergence
    for (int i = 0; i < 5; ++i) {
        double prev = d.totalHPWL();
        performSingleSegClustering(d);
        if ((prev - d.totalHPWL()) / prev < 1e-3) break;
    }

    std::cout << "[FastDP] Done. HPWL = " << d.totalHPWL()
              << " (Δ=" << std::fixed << std::setprecision(2)
              << 100.0 * (prevHPWL - d.totalHPWL()) / prevHPWL << "%)"
              << std::endl;
}
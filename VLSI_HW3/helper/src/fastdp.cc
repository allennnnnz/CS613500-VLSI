#include <bits/stdc++.h>
#include "placer.hpp"
#include "fastdp.hpp"
#include "wl.hpp"

#include "segment_utils.hpp"
#include "global_swap.hpp"
#include "local_reorder.hpp"
// 預留：#include "vertical_swap.hpp"
// 預留：#include "single_cluster.hpp"

using namespace std;

namespace fastdp {

// ===============================================================
// FastDP::optimize — 主流程控制
// ===============================================================
//
// 現在已支援 Global Swap + Local Reorder，
// 並保留 Vertical / SingleSegCluster 擴充點。
// ===============================================================
void optimize(db::Database& db, int windowSize, int maxPass)
{
    buildAdjacency(db);
    wl::initAllNetBBox(db);

    windowSize = max(2, min(windowSize, 5));

    long long initHPWL = wl::totalHPWL(db);
    cerr << fixed << setprecision(0);
    cerr << "=============================\n";
    cerr << "[FastDP] Initial HPWL = " << initHPWL << "\n";
    cerr << "=============================\n";

    // =====================
    // Main iterative loop
    // =====================
    double targetImprove = 2.0; // 目標 >=2%
    for (int pass = 0; pass < maxPass; ++pass) {
        bool improved = false;
        cerr << "========== DP Iteration " << pass << " ==========\n";

        // -----------------------------------------------
        // (1) Global Swap
        // -----------------------------------------------
        cerr << "[GlobalSwap] Start...\n";
        bool gs = performGlobalSwap(db);
        if (gs) improved = true;

        // -----------------------------------------------
        // (2) Vertical Swap (預留)
        // -----------------------------------------------
        // bool vs = performVerticalSwap(db);
        // if (vs) improved = true;

        // -----------------------------------------------
        // (3) Local Reordering
        // -----------------------------------------------
        cerr << "[LocalReorder] Start...\n";
        bool lr = performLocalReorder(db, windowSize);
        if (lr) improved = true;

        // -----------------------------------------------
        // (4) Recompute HPWL
        // -----------------------------------------------
        wl::initAllNetBBox(db);
        long long nowHPWL = wl::totalHPWL(db);
        double delta = 100.0 * (nowHPWL - initHPWL) / initHPWL;

        cerr << "[FastDP] Pass " << pass
             << "  HPWL = " << nowHPWL
             << "  (Δ=" << std::setprecision(2) << delta << "%)\n";

        double cumulative = 100.0 * (initHPWL - nowHPWL) / initHPWL;
        if (cumulative >= targetImprove) {
            cerr << "[FastDP] Reached target improvement >= " << targetImprove << "%. Stopping.\n";
            break;
        }
        if (!improved) {
            cerr << "[FastDP] No further improvement and target not met. Stopping.\n";
            break;
        }
    }

    // =====================
    // Final refinement (預留 Single-Segment Clustering)
    // =====================
    // performSingleSegClustering(db);

    wl::initAllNetBBox(db);
    long long finalHPWL = wl::totalHPWL(db);
    cerr << "=============================\n";
    cerr << "[FastDP] Final HPWL = " << finalHPWL
         << "  (" << std::setprecision(2)
         << 100.0 * (initHPWL - finalHPWL) / initHPWL << "% improved)\n";
    cerr << "=============================\n";
}

} // namespace fastdp
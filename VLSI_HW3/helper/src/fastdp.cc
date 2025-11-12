#include <bits/stdc++.h>
#include "placer.hpp"
#include "fastdp.hpp"
#include "wl.hpp"

// 各模組 header（目前只用 localReorder）
#include "localReorder.hpp"
// 預留：#include "global_swap.hpp"
// 預留：#include "vertical_swap.hpp"
// 預留：#include "single_cluster.hpp"
#include "segment_utils.hpp"

using namespace std;

namespace fastdp {

// ===============================================================
// FastDP::optimize — 主流程控制
// ===============================================================
//
// 結構設計為模組化，可自由插拔：
//    1. buildAdjacency()
//    2. init HPWL
//    3. (之後可插入 SingleSegCluster)
//    4. main iterative loop:
//          performGlobalSwap()
//          performVerticalSwap()
//          performLocalReorder()
//    5. (最後 refinement clustering)
//
// 目前僅啟用 Local Reorder 模組。
// ===============================================================
void optimize(db::Database& db, int windowSize, int maxPass)
{
    buildAdjacency(db);
    wl::initAllNetBBox(db);

    windowSize = max(2, min(windowSize, 4));

    long long initHPWL = wl::totalHPWL(db);
    cerr << fixed << setprecision(0);
    cerr << "=============================\n";
    cerr << "[FastDP] Initial HPWL = " << initHPWL << "\n";
    cerr << "=============================\n";

    // =====================
    // Main iterative loop
    // =====================
    for (int pass = 0; pass < maxPass; ++pass) {
        bool improved = false;

        cerr << "========== DP Iteration " << pass << " ==========\n";

        // -----------------------------------------------
        // (1) Global Swap (預留)
        // improved |= performGlobalSwap(db);
        // -----------------------------------------------

        // -----------------------------------------------
        // (2) Vertical Swap (預留)
        // improved |= performVerticalSwap(db);
        // -----------------------------------------------

        // -----------------------------------------------
        // (3) Local Reordering (目前實際啟用)
        // -----------------------------------------------
        cerr << "[LocalReorder] Start...\n";
        bool lr = performLocalReorder(db, windowSize);
        if (lr) improved = true;

        // -----------------------------------------------
        // (4) Refinement / Legalization
        // -----------------------------------------------
        wl::initAllNetBBox(db);
        long long nowHPWL = wl::totalHPWL(db);
        double delta = 100.0 * (nowHPWL - initHPWL) / initHPWL;

        cerr << "[FastDP] Pass " << pass
             << "  HPWL = " << nowHPWL
             << "  (Δ=" << std::setprecision(2) << delta << "%)\n";

        if (!improved) {
            cerr << "[FastDP] No further improvement, stopping.\n";
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
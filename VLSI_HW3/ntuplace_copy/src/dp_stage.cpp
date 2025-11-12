#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include "design.hpp"
#include "design_io.hpp"

// 已完成模組的外部宣告
extern void performGlobalSwap(Design& d);
extern bool performVerticalSwap(Design& d);
extern bool performLocalReorder(Design& d, int windowSize);
extern bool performSingleSegmentClustering(Design& d);

// 若 buildSegments 為成員函式則不用 extern
// extern void buildSegments(Design& d);

// 印 HPWL 工具
static void reportHPWL(const Design& d, const char* tag) {
    std::cout << "[HPWL " << tag << "] " << d.totalHPWL() << "\n";
}

// Snapshot/restore helpers (global scope)
static inline std::vector<std::pair<int,int>> snapshotXY(const Design& d){
    std::vector<std::pair<int,int>> xy; xy.reserve(d.insts.size());
    for (const auto& ins : d.insts) xy.emplace_back(ins.x, ins.y);
    return xy;
}
static inline void restoreXY(Design& d, const std::vector<std::pair<int,int>>& xy){
    size_t n = std::min(xy.size(), d.insts.size());
    for (size_t i=0;i<n;++i){ d.insts[i].x = xy[i].first; d.insts[i].y = xy[i].second; }
}

void runDetailedPlacement(Design& d, const std::string& inDefPath, const std::string& outDefPath, int iters)
{
    // ========== Initialization ==========
    int64_t hpwl_before = d.totalHPWL();
    std::cout << "\n=============================\n";
    std::cout << "[DP] Initial HPWL = " << hpwl_before << "\n";
    std::cout << "=============================\n";

    int64_t lastHPWL = hpwl_before;

    // ========== Outer Iteration ==========
    // Keep iterating until cumulative improvement >= 2% (iters is minimum), with safety cap.
    const double targetImprovePct = 2.0;
    const int safetyCap = std::max(iters + 200, 1000);
    for (int iter = 0; ; ++iter) {
        std::cout << "\n========== DP Iteration " << iter << " ==========\n";
        const int64_t iterStartHPWL = lastHPWL;

        // 1️⃣ Global Swap — 全域最佳化階段
        std::cout << "[GlobalSwap] Start...\n";
        auto snap_gs = snapshotXY(d);
        performGlobalSwap(d);
        int64_t hpwl_gs = d.totalHPWL();
        if (hpwl_gs > lastHPWL) { restoreXY(d, snap_gs); hpwl_gs = lastHPWL; std::cout << "[GlobalSwap] Rejected (no improvement)\n"; }
        std::cout << "[GlobalSwap] HPWL = " << hpwl_gs 
                  << " (Δ=" << (lastHPWL - hpwl_gs) << ")\n";
        lastHPWL = hpwl_gs;

        // 2️⃣ Vertical Swap — 修正垂直錯位
        std::cout << "[VerticalSwap] Start...\n";
        auto snap_vs = snapshotXY(d);
        bool vs_improved = performVerticalSwap(d);
        int64_t hpwl_vs = d.totalHPWL();
        if (hpwl_vs > lastHPWL) { restoreXY(d, snap_vs); hpwl_vs = lastHPWL; vs_improved = false; std::cout << "[VerticalSwap] Rejected (no improvement)\n"; }
        std::cout << "[VerticalSwap] HPWL = " << hpwl_vs 
                  << " (Δ=" << (lastHPWL - hpwl_vs) << ")\n";
        lastHPWL = hpwl_vs;

        // 3️⃣ Segment 建構 — 固定區段以供後續操作
        std::cout << "[BuildSegments] Building segments...\n";
        d.buildSegments();

        // 4️⃣ Local Reorder — 橫向局部最佳化 (n=3)
        std::cout << "[LocalReorder] Start (n=3)...\n";
        auto snap_lr = snapshotXY(d);
        bool lr_improved = performLocalReorder(d, 3);
        int64_t hpwl_lr = d.totalHPWL();
        if (hpwl_lr > lastHPWL) { restoreXY(d, snap_lr); hpwl_lr = lastHPWL; lr_improved = false; std::cout << "[LocalReorder] Rejected (no improvement)\n"; }
        std::cout << "[LocalReorder] HPWL = " << hpwl_lr 
                  << " (Δ=" << (lastHPWL - hpwl_lr) << ")\n";
        lastHPWL = hpwl_lr;

        // 5️⃣ Single-Segment Clustering — 動態群聚合法化
        std::cout << "[SingleSegCluster] Start...\n";
        auto snap_sc = snapshotXY(d);
        bool sc_improved = performSingleSegmentClustering(d);
        int64_t hpwl_sc = d.totalHPWL();
        if (hpwl_sc > lastHPWL) { restoreXY(d, snap_sc); hpwl_sc = lastHPWL; sc_improved = false; std::cout << "[SingleSegCluster] Rejected (no improvement)\n"; }
        std::cout << "[SingleSegCluster] HPWL = " << hpwl_sc 
                  << " (Δ=" << (lastHPWL - hpwl_sc) << ")\n";

        // 早停與目標：每回合與累積改善
        double iterImprovePct = (double)(iterStartHPWL - lastHPWL) / (double)iterStartHPWL * 100.0;
        double cumulativePct   = (double)(hpwl_before - lastHPWL) / (double)hpwl_before * 100.0;
        std::cout << "Iteration " << iter 
                  << " done. Total improvement this round = " << iterImprovePct << "%\n";
        std::cout << "[DP] Cumulative improvement so far = " << cumulativePct << " %\n";

        if (cumulativePct >= targetImprovePct && iter + 1 >= iters) {
            std::cout << "[DP] Target improvement (" << targetImprovePct << "%) reached. Stop.\n";
            break;
        }
        if (iter + 1 >= safetyCap) {
            std::cout << "[DP] Safety cap reached. Stop.\n";
            break;
        }
    }

    // ========== Final Report ==========
    int64_t hpwl_after = d.totalHPWL();
    double totalImprovement = (double)(hpwl_before - hpwl_after) / (double)hpwl_before * 100.0;
    std::cout << "\n=============================\n";
    std::cout << "[DP] Final HPWL = " << hpwl_after << "\n";
    std::cout << "[DP] Total Improvement = " << totalImprovement << " %\n";
    std::cout << "=============================\n";

    // ========== Output DEF ==========
    std::cout << "[Output] Writing DEF (preserve mode)...\n";
    if (!writeDEFPreserve(inDefPath, outDefPath, d))
        std::cerr << "❌ Failed to write DEF!\n";
    else
        std::cout << "✅ DEF written → " << outDefPath << "\n";
}
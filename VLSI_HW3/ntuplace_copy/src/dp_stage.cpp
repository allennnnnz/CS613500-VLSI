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

void runDetailedPlacement(Design& d, const std::string& inDefPath, const std::string& outDefPath, int iters)
{
    // ========== Initialization ==========
    int64_t hpwl_before = d.totalHPWL();
    std::cout << "\n=============================\n";
    std::cout << "[DP] Initial HPWL = " << hpwl_before << "\n";
    std::cout << "=============================\n";

    int64_t lastHPWL = hpwl_before;

    // ========== Outer Iteration ==========
    for (int iter = 0; iter < iters; ++iter) {
        std::cout << "\n========== DP Iteration " << iter << " ==========\n";

        // 1️⃣ Global Swap — 全域最佳化階段
        std::cout << "[GlobalSwap] Start...\n";
        performGlobalSwap(d);
        int64_t hpwl_gs = d.totalHPWL();
        std::cout << "[GlobalSwap] HPWL = " << hpwl_gs 
                  << " (Δ=" << (lastHPWL - hpwl_gs) << ")\n";
        lastHPWL = hpwl_gs;

        // 2️⃣ Vertical Swap — 修正垂直錯位
        std::cout << "[VerticalSwap] Start...\n";
        bool vs_improved = performVerticalSwap(d);
        int64_t hpwl_vs = d.totalHPWL();
        std::cout << "[VerticalSwap] HPWL = " << hpwl_vs 
                  << " (Δ=" << (lastHPWL - hpwl_vs) << ")\n";
        lastHPWL = hpwl_vs;

        // 3️⃣ Segment 建構 — 固定區段以供後續操作
        std::cout << "[BuildSegments] Building segments...\n";
        d.buildSegments();

        // 4️⃣ Local Reorder — 橫向局部最佳化 (n=3)
        std::cout << "[LocalReorder] Start (n=3)...\n";
        bool lr_improved = performLocalReorder(d, 3);
        int64_t hpwl_lr = d.totalHPWL();
        std::cout << "[LocalReorder] HPWL = " << hpwl_lr 
                  << " (Δ=" << (lastHPWL - hpwl_lr) << ")\n";
        lastHPWL = hpwl_lr;

        // 5️⃣ Single-Segment Clustering — 動態群聚合法化
        std::cout << "[SingleSegCluster] Start...\n";
        bool sc_improved = performSingleSegmentClustering(d);
        int64_t hpwl_sc = d.totalHPWL();
        std::cout << "[SingleSegCluster] HPWL = " << hpwl_sc 
                  << " (Δ=" << (lastHPWL - hpwl_sc) << ")\n";

        // ⛔ 早停條件 — 若幾乎沒改善則結束
        int64_t improvement = lastHPWL - hpwl_sc;
        double ratio = (double)improvement / (double)lastHPWL * 100.0;
        lastHPWL = hpwl_sc;

        if (!vs_improved && !lr_improved && !sc_improved && improvement <= 0) {
            std::cout << "[DP] No further improvement, stop at iteration " << iter << ".\n";
            break;
        }

        std::cout << "Iteration " << iter 
                  << " done. Total improvement this round = " << ratio << "%\n";
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
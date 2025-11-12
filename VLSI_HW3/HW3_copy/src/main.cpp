#include "design_io.hpp"
#include "dp_stage.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <limits>

// HPWL 計算 (使用 module lower-left corner；排除 SPECIALNETS)
static int64_t totalHPWL_cellLL(const Design& d) {
    long long sum = 0;
    for (const Net& net : d.nets) {
        if (net.isSpecial) continue;
        if (net.pins.empty()) continue;
        int xmin =  std::numeric_limits<int>::max();
        int xmax = -std::numeric_limits<int>::max();
        int ymin =  std::numeric_limits<int>::max();
        int ymax = -std::numeric_limits<int>::max();
        for (const auto& p : net.pins) {
            int px, py;
            if (p.isIO || p.instId < 0) {
                // Prefer IO pin location parsed from PINS
                auto it = d.ioPinLoc.find(p.ioName);
                if (it != d.ioPinLoc.end()) { px = it->second.x; py = it->second.y; }
                else { px = p.ioLoc.x; py = p.ioLoc.y; }
            } else {
                const Inst& I = d.insts[p.instId];
                // bottom-left of module per spec
                px = I.x; py = I.y;
            }
            xmin = std::min(xmin, px);
            xmax = std::max(xmax, px);
            ymin = std::min(ymin, py);
            ymax = std::max(ymax, py);
        }
        sum += (xmax - xmin) + (ymax - ymin);
    }
    return (int64_t)sum;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage:\n  " << argv[0]
                  << " <input.lef> <input.def> <output.def>\n";
        return 1;
    }

    std::string lefPath  = argv[1];
    std::string defIn    = argv[2];
    std::string defOut   = argv[3];

    Design design;

    auto t_start = std::chrono::high_resolution_clock::now();

    // === 讀入 LEF/DEF ===
    if (!loadLEF(lefPath, design)) {
        std::cerr << "Failed to read LEF\n";
        return 1;
    }
    if (!loadDEF(defIn, design)) {
        std::cerr << "Failed to read DEF\n";
        return 1;
    }
    design.buildIndices();

    // === 計算初始 HPWL ===
    int64_t hpwl_before = totalHPWL_cellLL(design);
    std::cout << "HPWL(before) = " << hpwl_before << "\n";

    // === 執行 HPWL 優化 ===
    std::cout << "==== Begin HPWL Optimization ====\n";
    run_hpwl_optimization(design);

    // 最終合法化：移除任何殘留的重疊並夾在列界內
    final_legalize_rows(design);

    // === 優化後 HPWL ===
    int64_t hpwl_after = totalHPWL_cellLL(design);
    double improve = 100.0 * (hpwl_before - hpwl_after) / hpwl_before;
    std::cout << "HPWL(after) = " << hpwl_after
              << "  (" << std::fixed << std::setprecision(2)
              << improve << "% improvement)\n";

    // === 輸出 DEF ===
    if (!writeDEFPreserve(defIn, defOut, design)) {
        std::cerr << "Failed to write DEF\n";
        return 1;
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double secs = std::chrono::duration_cast<std::chrono::duration<double>>(t_end - t_start).count();
    std::cout << "Runtime: " << std::fixed << std::setprecision(3)
              << secs << " sec\n";

    std::cout << "=============================\n";
    std::cout << "HPWL(before): " << hpwl_before << "\n";
    std::cout << "HPWL(after) : " << hpwl_after  << "\n";
    std::cout << "Total Improvement: " << std::fixed << std::setprecision(2)
              << improve << "%\n";
    std::cout << "=============================\n";
    return 0;
}
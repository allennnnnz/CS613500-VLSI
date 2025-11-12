#include "design_io.hpp"
#include "dp_stage.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <limits>

// ------------------------------------------------------------
// 計算總 HPWL （使用 instance lower-left corner；排除 SPECIALNETS）
// ------------------------------------------------------------
static int64_t totalHPWL_cellLL(const Design& d) {
    long long sum = 0;
    for (const Net& net : d.nets) {
        if (net.isSpecial || net.pins.empty()) continue;

        int xmin = std::numeric_limits<int>::max();
        int xmax = std::numeric_limits<int>::min();
        int ymin = std::numeric_limits<int>::max();
        int ymax = std::numeric_limits<int>::min();

        for (const auto& p : net.pins) {
            int px = 0, py = 0;
            if (p.isIO || p.instId < 0) {
                // 優先使用 ioPinLoc
                auto it = d.ioPinLoc.find(p.ioName);
                if (it != d.ioPinLoc.end()) {
                    px = it->second.x; py = it->second.y;
                } else {
                    px = p.ioLoc.x; py = p.ioLoc.y;
                }
            } else if (p.instId < (int)d.insts.size()) {
                const Inst& I = d.insts[p.instId];
                px = I.x;
                py = I.y;
            }
            xmin = std::min(xmin, px);
            xmax = std::max(xmax, px);
            ymin = std::min(ymin, py);
            ymax = std::max(ymax, py);
        }

        if (xmin < xmax && ymin < ymax)
            sum += (xmax - xmin) + (ymax - ymin);
    }
    return (int64_t)sum;
}

// ------------------------------------------------------------
// 主程式
// ------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage:\n  " << argv[0]
                  << " <input.lef> <input.def> <output.def>\n";
        return 1;
    }

    std::string lefPath = argv[1];
    std::string defIn   = argv[2];
    std::string defOut  = argv[3];

    Design design;
    auto t_start = std::chrono::high_resolution_clock::now();

    // === 讀入 LEF/DEF ===
    if (!loadLEF(lefPath, design)) {
        std::cerr << "[Error] Failed to read LEF: " << lefPath << "\n";
        return 1;
    }
    if (!loadDEF(defIn, design)) {
        std::cerr << "[Error] Failed to read DEF: " << defIn << "\n";
        return 1;
    }

    // === 建立索引 (必要！否則 partialHPWL 會 crash)
    design.buildIndices();

    std::cout << "✅ LEF/DEF loaded successfully.\n";
    std::cout << "   Macros=" << design.macros.size()
              << ", Insts=" << design.insts.size()
              << ", Nets=" << design.nets.size()
              << ", Rows=" << design.layout.rows.size() << "\n";
    std::cout << "   inst2nets size=" << design.inst2nets.size() << "\n";

    // === 計算初始 HPWL ===
    int64_t hpwl_before = totalHPWL_cellLL(design);
    std::cout << "HPWL(before) = " << hpwl_before << "\n";

    // === 執行詳細放置 ===
    try {
        performDetailPlacement(design);
    } catch (const std::exception& e) {
        std::cerr << "[Exception] during placement: " << e.what() << "\n";
        return 1;
    }

    // === 優化後 HPWL ===
    int64_t hpwl_after = totalHPWL_cellLL(design);
    double improve = 100.0 * (hpwl_before - hpwl_after) / hpwl_before;

    std::cout << "HPWL(after) = " << hpwl_after
              << "  (" << std::fixed << std::setprecision(2)
              << improve << "% improvement)\n";

    // === 輸出 DEF ===
    if (!writeDEFPreserve(defIn, defOut, design)) {
        std::cerr << "[Error] Failed to write DEF: " << defOut << "\n";
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
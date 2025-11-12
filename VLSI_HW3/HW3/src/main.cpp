#include "design_io.hpp"
#include "detailPlacement.hpp"
#include "global_reorder.hpp"
#include <iostream>
#include <chrono>
#include <limits>

// ---------------------------------------------------------------------
// Compute HPWL per spec (Section 3.2)
//  - Sum over NETS only (SPECIALNETS excluded)
//  - Each module pin at its bottom-left (inst.x, inst.y)
//  - IO pins use absolute coordinates from PINS (d.ioPinLoc)
// ---------------------------------------------------------------------
static int64_t totalHPWL_cellLL(const Design& d) {
    long long sum = 0;
    for (const Net& net : d.nets) {
        if (net.isSpecial || net.pins.empty()) continue;

        int xmin =  std::numeric_limits<int>::max();
        int xmax = -std::numeric_limits<int>::max();
        int ymin =  std::numeric_limits<int>::max();
        int ymax = -std::numeric_limits<int>::max();

        for (const auto& p : net.pins) {
            int px = 0, py = 0;

            if (p.isIO || p.instId < 0) {
                // IO pin: use absolute coordinate from DEF (PINS section)
                auto it = d.ioPinLoc.find(p.ioName);
                if (it != d.ioPinLoc.end()) {
                    px = it->second.x;
                    py = it->second.y;
                } else {
                    px = p.ioLoc.x;
                    py = p.ioLoc.y;
                }
            } else {
                // Cell pin: bottom-left corner of the instance
                const Inst& I = d.insts[p.instId];
                px = I.x;
                py = I.y;
            }

            if (px < xmin) xmin = px;
            if (px > xmax) xmax = px;
            if (py < ymin) ymin = py;
            if (py > ymax) ymax = py;
        }

        sum += (long long)(xmax - xmin) + (long long)(ymax - ymin);
    }
    return (int64_t)sum;
}

// ---------------------------------------------------------------------
// main
// ---------------------------------------------------------------------
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

    // 1️⃣ Read LEF
    if (!loadLEF(lefPath, design)) {
        std::cerr << "Failed to read LEF\n";
        return 1;
    }

    // 2️⃣ Read DEF
    if (!loadDEF(defIn, design)) {
        std::cerr << "Failed to read DEF\n";
        return 1;
    }

    design.buildIndices();  // rebuild lookup maps

    // 3️⃣ Compute HPWL before placement (spec-compliant)
    int64_t hpwl_before = totalHPWL_cellLL(design);
    std::cout << "HPWL(before) = " << hpwl_before << "\n";

    // 4️⃣ Detailed placement (order-preserving DP)
    DetailPlaceConfig dpCfg;
    dpCfg.verbose = true;
    dpCfg.passes = 2;
    detailPlacement(design, dpCfg);

    // 5️⃣ HPWL after detailed placement
    int64_t hpwl_after_dp = totalHPWL_cellLL(design);
    std::cout << "HPWL(after DP) = " << hpwl_after_dp << "\n";

    // 6️⃣ Global reorder (window-based)
    GlobalReorderConfig grCfg;
    grCfg.windowSitesGlobal = 400;
    grCfg.strideSites       = 120;
    grCfg.maxCellsPerWindow = 7;
    grCfg.permLimit         = 128;
    grCfg.gammaInternal     = 1.0;
    grCfg.gammaOneSide      = 1.0;
    grCfg.lambdaTether      = 0.10;
    grCfg.gwPasses          = 2;
    grCfg.verbose           = false;

    global_reorder_sliding_windows(design, grCfg);

    // 7️⃣ Final HPWL after global reorder
    int64_t hpwl_after = totalHPWL_cellLL(design);
    std::cout << "HPWL(after GR) = " << hpwl_after << "\n";

    // 8️⃣ Write back DEF
    if (!writeDEFPreserve(defIn, defOut, design)) {
        std::cerr << "Failed to write DEF\n";
        return 1;
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double secs = std::chrono::duration_cast<std::chrono::duration<double>>(t_end - t_start).count();
    std::cout << "Runtime: " << secs << " sec\n";

    // Summary
    double impDP = 100.0 * (hpwl_before - hpwl_after_dp) / hpwl_before;
    double impGR = 100.0 * (hpwl_before - hpwl_after) / hpwl_before;
    std::cout << "Improvement after DP: " << impDP << "%\n";
    std::cout << "Total Improvement after GR: " << impGR << "%\n";

    return 0;
}
// main.cpp
#include "design_io.hpp"
#include "detailPlacement.hpp"
#include <iostream>
#include <chrono>

// Compute HPWL using assignment's definition: each module's pin is its lower-left corner (inst.x, inst.y),
// and IO pins use their absolute ioLoc. This should match the verifier's metric.
static int64_t totalHPWL_cellLL(const Design& d) {
    long long sum = 0;
    for (const Net& net : d.nets) {
        if (net.pins.empty()) continue;
        int xmin =  std::numeric_limits<int>::max();
        int xmax = -std::numeric_limits<int>::max();
        int ymin =  std::numeric_limits<int>::max();
        int ymax = -std::numeric_limits<int>::max();
        for (const auto& p : net.pins) {
            int px, py;
            if (p.isIO || p.instId < 0) {
                px = p.ioLoc.x; py = p.ioLoc.y;
            } else {
                const Inst& I = d.insts[p.instId];
                px = I.x; py = I.y; // lower-left corner per spec
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

    // 1. 讀 LEF (macro 尺寸 / site pitch / pin 代表點)
    if (!loadLEF(lefPath, design)) {
        std::cerr << "Failed to read LEF\n";
        return 1;
    }

    // 2. 讀 DEF (units, diearea, rows, components, nets)
    if (!loadDEF(defIn, design)) {
        std::cerr << "Failed to read DEF\n";
        return 1;
    }

    // （保險）重建各種 name -> id map
    design.buildIndices();

    // 3. 計算優化前 HPWL（作業規格：以 cell 左下角作為 pin）
    int64_t hpwl_before = totalHPWL_cellLL(design);
    std::cout << "HPWL(before) = " << hpwl_before << "\n";

    // 4. 執行 detailed placement 最佳化 (greedy row-based swap)
    detailPlacement(design);

    // 5. 再算一次 HPWL（同規格）
    int64_t hpwl_after = totalHPWL_cellLL(design);
    std::cout << "HPWL(after)  = " << hpwl_after << "\n";

    // 6. 輸出 DEF
    //    - 會把 COMPONENTS 裡 "+ PLACED ( x y ) ORIENT" 更新
    //    - 其他內容全部照 input defIn 原樣拷貝
    if (!writeDEFPreserve(defIn, defOut, design)) {
        std::cerr << "Failed to write DEF\n";
        return 1;
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double secs = std::chrono::duration_cast<std::chrono::duration<double>>(t_end - t_start).count();
    std::cout << "Runtime: " << secs << " sec\n";

    return 0;
}
#include <iostream>
#include <string>
#include "design.hpp"
#include "design_io.hpp"

// 由 dp_stage.cpp 提供的 pipeline 函式
extern void runDetailedPlacement(Design& d, const std::string& inDef, const std::string& outDef, int iters = 3);

static void printUsage(const char* prog) {
    std::cerr << "Usage:\n"
              << "  " << prog << " tech.lef cell.lef input.def output.def [iters]\n"
              << "or:\n"
              << "  " << prog << " all.lef input.def output.def [iters]\n";
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        printUsage(argv[0]);
        return 1;
    }

    // 計算 iteration 數
    int iters = 3;
    if (argc >= 5) {
        char* endp = nullptr;
        long v = std::strtol(argv[argc - 1], &endp, 10);
        if (endp && *endp == '\0') {
            iters = (int)std::max(1L, v);
            --argc;
        }
    }

    const std::string outDefPath = argv[argc - 1];
    const std::string inDefPath  = argv[argc - 2];

    std::vector<std::string> lefFiles;
    for (int i = 1; i < argc - 2; ++i)
        lefFiles.emplace_back(argv[i]);

    Design d;

    // ============================================================
    // 1. 讀取所有 LEF
    // ============================================================
    for (const auto& lf : lefFiles) {
        std::cout << "[I/O] Reading LEF: " << lf << "\n";
        if (!loadLEF(lf, d)) {
            std::cerr << "❌ Failed to load LEF: " << lf << "\n";
            return 1;
        }
    }

    // ============================================================
    // 2. 讀取 DEF
    // ============================================================
    std::cout << "[I/O] Reading DEF: " << inDefPath << "\n";
    if (!loadDEF(inDefPath, d)) {
        std::cerr << "❌ Failed to load DEF: " << inDefPath << "\n";
        return 1;
    }

    // ============================================================
    // 3. 執行 Detailed Placement Pipeline
    // ============================================================
    runDetailedPlacement(d, inDefPath, outDefPath, iters);

    std::cout << "\n✅ Done.\n";
    return 0;
}
#include "design_io.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <lef> <def>\n";
        return 1;
    }

    Design design;
    loadLEF(argv[1], design);
    loadDEF(argv[2], design);

    std::cout << "First 5 instances before placement:\n";
    for (int i = 0; i < std::min(5, (int)design.insts.size()); ++i) {
        std::cout << "  inst " << i << ": x=" << design.insts[i].x 
                  << " y=" << design.insts[i].y << "\n";
    }

    return 0;
}

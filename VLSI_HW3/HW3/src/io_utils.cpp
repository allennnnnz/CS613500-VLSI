#include "io_utils.hpp"
#include <iostream>
#include <fstream>
#include <unordered_set>

std::vector<IOPin> gatherIOPins(const Design& d) {
    std::vector<IOPin> ios;
    ios.reserve(256);
    for (const Net& net : d.nets) {
        for (const auto& p : net.pins) {
            if (!p.isIO) continue;
            IOPin info;
            info.netName = net.name;
            info.x = p.ioLoc.x;
            info.y = p.ioLoc.y;
            ios.push_back(info);
        }
    }
    return ios;
}

void printIOStats(const Design& d) {
    auto ios = gatherIOPins(d);
    int cnt = (int)ios.size();
    if (cnt == 0) {
        std::cout << "[IO] pins = 0\n";
        return;
    }

    int xmin = ios[0].x, xmax = ios[0].x, ymin = ios[0].y, ymax = ios[0].y;
    for (const auto& io : ios) {
        if (io.x < xmin) xmin = io.x;
        if (io.x > xmax) xmax = io.x;
        if (io.y < ymin) ymin = io.y;
        if (io.y > ymax) ymax = io.y;
    }
    std::cout << "[IO] pins = " << cnt
              << ", bbox = (" << xmin << "," << ymin << ")- ("
              << xmax << "," << ymax << ")\n";
}

bool dumpIOToCSV(const Design& d, const std::string& csvPath) {
    std::ofstream ofs(csvPath);
    if (!ofs) return false;
    ofs << "net_name,x,y\n";
    for (const auto& io : gatherIOPins(d)) {
        ofs << io.netName << "," << io.x << "," << io.y << "\n";
    }
    return true;
}

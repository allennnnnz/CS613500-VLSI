#pragma once
#include "placer.hpp"
#include <limits>

namespace wl {

// compute total HPWL from db.netBBox
inline long long totalHPWL(const db::Database& db) {
    long long s = 0;
    for (auto &b : db.netBBox) {
        if (b[0] > b[1] || b[2] > b[3]) continue;
        s += (long long)(b[1] - b[0]) + (long long)(b[3] - b[2]);
    }
    return s;
}

// recompute bbox of one net from current cell/pin coords
inline void recomputeNetBBox(const db::Database& db, int nid, std::array<int,4>& out) {
    int xmin = std::numeric_limits<int>::max();
    int ymin = std::numeric_limits<int>::max();
    int xmax = std::numeric_limits<int>::min();
    int ymax = std::numeric_limits<int>::min();

    // cells
    for (int cid : db.nets[nid].cellIds) {
        const auto &c = db.cells[cid];
        int x = c.x, y = c.y; // pin at LL as per spec
        xmin = std::min(xmin, x); xmax = std::max(xmax, x);
        ymin = std::min(ymin, y); ymax = std::max(ymax, y);
    }
    // IO pins
    for (int pid : db.nets[nid].ioIds) {
        const auto &p = db.pins[pid];
        int x = p.x, y = p.y;
        xmin = std::min(xmin, x); xmax = std::max(xmax, x);
        ymin = std::min(ymin, y); ymax = std::max(ymax, y);
    }
    if (xmin == std::numeric_limits<int>::max()) {
        // empty (shouldn't happen), make a degenerate box
        out = {0,0,0,0};
    } else {
        out = {xmin, xmax, ymin, ymax};
    }
}

// init all net bboxes
inline void initAllNetBBox(db::Database& db) {
    db.netBBox.assign(db.nets.size(), {0,0,0,0});
    for (int i = 0; i < (int)db.nets.size(); ++i) {
        recomputeNetBBox(db, i, db.netBBox[i]);
    }
}

} // namespace wl
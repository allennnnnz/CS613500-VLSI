#include <bits/stdc++.h>
#include "localReorder.hpp"
#include "wl.hpp"
#include "segment_utils.hpp"

using namespace std;
using fastdp::snapToSite; 

namespace {

// =====================
// Helper functions
// =====================
static void collectAffectedNets(const db::Database& db,
                                const vector<int>& cells,
                                unordered_set<int>& aff) {
    for (int cid : cells)
        for (int nid : db.cell2nets[cid])
            aff.insert(nid);
}

static long long netsHPWL(const db::Database& db,
                          const unordered_set<int>& nets) {
    long long s = 0;
    for (int nid : nets) {
        auto &b = db.netBBox[nid];
        if (b[0] > b[1] || b[2] > b[3]) continue;
        s += (long long)(b[1] - b[0]) + (long long)(b[3] - b[2]);
    }
    return s;
}

// =====================
// Evaluate and apply reorder
// =====================
static long long evalAndPlaceIfBetter(db::Database& db, Segment& sg, int start, int W,
                                      const vector<int>& perm) {
    vector<int> oldOrder(sg.cells.begin()+start, sg.cells.begin()+start+W);
    vector<pair<int,int>> oldPos;
    oldPos.reserve(W);
    for (int cid : oldOrder)
        oldPos.push_back({db.cells[cid].x, db.cells[cid].y});

    unordered_set<int> aff;
    collectAffectedNets(db, oldOrder, aff);
    long long before = netsHPWL(db, aff);

    const auto &row = db.rows[sg.rowId];
    int left_x  = (start == 0) ? sg.xL
                               : db.cells[sg.cells[start-1]].x + db.cells[sg.cells[start-1]].width;
    int right_x = (start + W == (int)sg.cells.size()) ? sg.xR
                               : db.cells[sg.cells[start+W]].x;
    left_x  = max(sg.xL, snapToSite(left_x, row));
    right_x = min(sg.xR, snapToSite(right_x, row));
    if (left_x >= right_x) return 0;

    vector<int> newOrder(W);
    for (int i=0;i<W;++i)
        newOrder[i] = sg.cells[start + perm[i]];

    int curx = left_x;
    vector<pair<int,int>> newPos(W);
    bool overflow = false;
    for (int i=0;i<W;++i) {
        int cid = newOrder[i];
        auto &c = db.cells[cid];
        int placeX = snapToSite(curx, row);
        if (placeX + c.width > right_x) { overflow = true; break; }
        newPos[i] = {placeX, row.y};
        curx = placeX + c.width;
    }
    if (overflow) return 0;

    for (int i=0;i<W;++i) {
        int cid = newOrder[i];
        db.cells[cid].x = newPos[i].first;
        db.cells[cid].y = newPos[i].second;
        db.cells[cid].orient = row.orient;
    }

    for (int nid : aff)
        wl::recomputeNetBBox(db, nid, db.netBBox[nid]);
    long long after = netsHPWL(db, aff);

    long long gain = after - before;
    if (gain < 0) {
        for (int i=0;i<W;++i)
            sg.cells[start+i] = newOrder[i];
        return gain;
    } else {
        for (int i=0;i<W;++i) {
            int cid = oldOrder[i];
            db.cells[cid].x = oldPos[i].first;
            db.cells[cid].y = oldPos[i].second;
        }
        for (int nid : aff)
            wl::recomputeNetBBox(db, nid, db.netBBox[nid]);
        return 0;
    }
}

// =====================
// Generate permutations (cache up to W=5)
// =====================
static const vector<vector<int>>& perms(int W) {
    static vector<vector<vector<int>>> cache(6);
    if (!cache[W].empty()) return cache[W];
    vector<int> p(W); iota(p.begin(), p.end(), 0);
    do cache[W].push_back(p); while (next_permutation(p.begin(), p.end()));
    return cache[W];
}

// =====================
// Improve one segment
// =====================
static bool improveSegment(db::Database& db, Segment& sg, int W) {
    int n = (int)sg.cells.size();
    if (n < 2 || W < 2) return false;
    W = min(W, n);

    bool improved = false;
    const auto &P = perms(W);

    for (int i=0; i+W <= n; ++i) {
        long long bestGain = 0;
        for (auto &perm : P) {
            long long g = evalAndPlaceIfBetter(db, sg, i, W, perm);
            if (g < bestGain) bestGain = g;
        }
        if (bestGain < 0) improved = true;
    }
    return improved;
}

} // anonymous namespace

// =====================
// Public API
// =====================
namespace fastdp {

bool performLocalReorder(db::Database& db, int windowSize) {
    windowSize = max(2, min(windowSize, 4));
    auto segs = buildSegments(db);

    bool any = false;
    for (auto &sg : segs)
        any |= improveSegment(db, sg, windowSize);

    for (auto &sg : segs)
        legalizeSegmentOrder(db, sg);

    return any;
}

} // namespace fastdp
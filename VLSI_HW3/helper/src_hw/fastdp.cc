#include <bits/stdc++.h>
#include "placer.hpp"
#include "fastdp.hpp"
#include "wl.hpp"
using namespace std;

namespace {

long long netsHPWL(const db::Database& db, const std::unordered_set<int>& nets);

struct Segment {
    int rowId;
    int xL, xR;
    vector<int> cells;
};

// ---------------------------------------------------------------------------
// build adjacency for nets
void buildAdjacency(db::Database& db) {
    db.cell2nets.assign(db.cells.size(), {});
    for (int nid = 0; nid < (int)db.nets.size(); ++nid)
        for (int cid : db.nets[nid].cellIds)
            db.cell2nets[cid].push_back(nid);
}

// ---------------------------------------------------------------------------
// build row segments: same as your version but projects fixed cells vertically
vector<Segment> buildSegments(db::Database& db) {
    unordered_map<int,int> y2row;
    for (int i=0;i<(int)db.rows.size();++i)
        y2row[db.rows[i].y] = i;

    vector<pair<int,int>> rowSpan(db.rows.size());
    for (int i=0;i<(int)db.rows.size();++i) {
        auto &r = db.rows[i];
        int x0 = r.x;
        int x1 = r.x + r.siteCount * r.stepX;
        rowSpan[i] = {x0, x1};
    }

    // --- collect movable / fixed ---
    vector<vector<int>> rowMov(db.rows.size()), rowFix(db.rows.size());
    for (int cid=0; cid<(int)db.cells.size(); ++cid) {
        const auto &c = db.cells[cid];
        if (c.fixed) {
            // Project to every row whose y lies within [c.y, c.y + height)
            for (int rid=0; rid<(int)db.rows.size(); ++rid) {
                const auto &r = db.rows[rid];
                if (r.y >= c.y && r.y < c.y + c.height)
                    rowFix[rid].push_back(cid);
            }
        } else {
            auto it = y2row.find(c.y);
            if (it != y2row.end())
                rowMov[it->second].push_back(cid);
        }
    }

    // --- build free segments per row ---
    vector<Segment> segs;
    for (int rid=0; rid < (int)db.rows.size(); ++rid) {
        auto [x0,x1] = rowSpan[rid];
        auto &fix = rowFix[rid];
        sort(fix.begin(), fix.end(), [&](int a,int b){ return db.cells[a].x < db.cells[b].x; });

        vector<pair<int,int>> blocks;
        for (int cid: fix) {
            const auto &c = db.cells[cid];
            blocks.push_back({c.x, c.x + c.width});
        }
        sort(blocks.begin(), blocks.end());
        vector<pair<int,int>> merged;
        for (auto &b: blocks) {
            if (merged.empty() || b.first > merged.back().second) merged.push_back(b);
            else merged.back().second = max(merged.back().second, b.second);
        }

        int cur = x0;
        for (auto &b: merged) {
            if (cur < b.first)
                segs.push_back({rid, cur, b.first, {}});
            cur = max(cur, b.second);
        }
        if (cur < x1) segs.push_back({rid, cur, x1, {}});

        // assign movables to segments
        for (int cid: rowMov[rid]) {
            int cx = db.cells[cid].x;
            for (auto &sg : segs)
                if (sg.rowId==rid && cx>=sg.xL && cx<sg.xR)
                    sg.cells.push_back(cid);
        }
        for (auto &sg : segs)
            if (sg.rowId==rid)
                sort(sg.cells.begin(), sg.cells.end(),
                     [&](int a,int b){ return db.cells[a].x < db.cells[b].x; });
    }
    return segs;
}

// ---------------------------------------------------------------------------
inline int snapToSite(int x, const db::Row& r) {
    int dx = x - r.x;
    if (dx < 0) return r.x;
    int k = dx / r.stepX;
    return r.x + k * r.stepX;
}

// ---------------------------------------------------------------------------
void legalizeSegmentOrder(db::Database& db, Segment& sg) {
    const auto &row = db.rows[sg.rowId];
    auto snap = [&](int x) {
        int dx = x - row.x;
        if (dx < 0) return row.x;
        return row.x + (dx / row.stepX) * row.stepX;
    };

    int N = sg.cells.size();
    if (N == 0) return;

    // ==========================
    // 1. baseline greedy placement (always legal)
    // ==========================
    vector<pair<int,int>> pos0(N);
    int cur = snap(sg.xL);

    for (int i=0; i<N; i++) {
        int cid = sg.cells[i];
        auto &c = db.cells[cid];
        int px = snap(max(cur, c.x));
        px = min(px, sg.xR - c.width);
        pos0[i] = {px, row.y};
        cur = px + c.width;
    }

    // Apply baseline first
    for (int i=0;i<N;i++){
        int cid = sg.cells[i];
        db.cells[cid].x = pos0[i].first;
        db.cells[cid].y = row.y;
        db.cells[cid].orient = row.orient;
    }

    // compute HPWL before refinement
    unordered_set<int> aff;
    for (int cid : sg.cells)
        for (int nid : db.cell2nets[cid])
            aff.insert(nid);
    long long before = netsHPWL(db, aff);

    // ==========================
    // 2. compute a "target center"
    // ==========================
    int usedL = pos0.front().first;
    int usedR = pos0.back().first + db.cells[sg.cells.back()].width;
    int clusterCenter = (usedL + usedR) / 2;

    // ==========================
    // 3. CENTER-BIASED GREEDY legalize
    // ==========================
    vector<pair<int,int>> pos1(N);

    // Step A: find starting X = targetCenter - halfWidth
    int totalW = 0;
    for (int cid : sg.cells) totalW += db.cells[cid].width;

    int idealLeft = clusterCenter - totalW / 2;

    // clamp and snap
    idealLeft = max(idealLeft, sg.xL);
    idealLeft = min(idealLeft, sg.xR - totalW);
    idealLeft = snap(idealLeft);

    // Step B: greedy legalize from this new starting X
    cur = idealLeft;
    bool fail = false;

    for (int i=0;i<N;i++){
        int cid = sg.cells[i];
        auto &c = db.cells[cid];
        int px = snap(cur);
        if (px + c.width > sg.xR) { fail = true; break; }
        pos1[i] = {px, row.y};
        cur = px + c.width;
    }

    if (!fail) {
        // Try apply
        for (int i=0;i<N;i++){
            int cid = sg.cells[i];
            db.cells[cid].x = pos1[i].first;
            db.cells[cid].y = row.y;
        }

        for (int nid : aff)
            wl::recomputeNetBBox(db, nid, db.netBBox[nid]);

        long long after = netsHPWL(db, aff);

        if (after <= before) {
            // accept center-biased legalization
            return;
        }
    }

    // ==========================
    // 4. rollback to baseline
    // ==========================
    for (int i=0;i<N;i++){
        int cid = sg.cells[i];
        db.cells[cid].x = pos0[i].first;
        db.cells[cid].y = row.y;
    }
    for (int nid : aff)
        wl::recomputeNetBBox(db, nid, db.netBBox[nid]);
}

// ---------------------------------------------------------------------------
inline void collectAffectedNets(const db::Database& db,
                                const vector<int>& cells,
                                unordered_set<int>& aff) {
    for (int cid : cells)
        for (int nid : db.cell2nets[cid])
            aff.insert(nid);
}

inline long long netsHPWL(const db::Database& db, const unordered_set<int>& nets) {
    long long s=0;
    for (int nid : nets) {
        auto &b = db.netBBox[nid];
        if (b[0]>b[1]||b[2]>b[3]) continue;
        s += (long long)(b[1]-b[0]) + (long long)(b[3]-b[2]);
    }
    return s;
}

// ---------------------------------------------------------------------------
long long evalAndPlaceIfBetter(db::Database& db, Segment& sg, int start, int W,
                               const vector<int>& perm) {
    vector<int> oldOrder(sg.cells.begin()+start, sg.cells.begin()+start+W);
    vector<pair<int,int>> oldPos; oldPos.reserve(W);
    for (int cid : oldOrder) oldPos.push_back({db.cells[cid].x, db.cells[cid].y});

    unordered_set<int> aff;
    for (int cid : oldOrder)
        for (int nid : db.cell2nets[cid]) aff.insert(nid);

    long long before = netsHPWL(db, aff);

    const auto &row = db.rows[sg.rowId];
    int left_x  = (start == 0) ? sg.xL
                               : db.cells[sg.cells[start-1]].x + db.cells[sg.cells[start-1]].width;
    int right_x = (start + W == (int)sg.cells.size()) ? sg.xR
                               : db.cells[sg.cells[start+W]].x;
    auto snap = [&](int x) { return snapToSite(x, row); };
    left_x  = max(sg.xL, snap(left_x));
    right_x = min(sg.xR, snap(right_x));
    if (left_x >= right_x) return 0;

    vector<int> newOrder(W);
    for (int i=0;i<W;++i) newOrder[i] = sg.cells[start + perm[i]];

    int curx = left_x;
    vector<pair<int,int>> newPos(W);
    bool overflow = false;
    for (int i=0;i<W;++i) {
        int cid = newOrder[i];
        auto &c = db.cells[cid];
        int placeX = snap(curx);
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

    for (int nid : aff) wl::recomputeNetBBox(db, nid, db.netBBox[nid]);
    long long after = netsHPWL(db, aff);

    long long gain = after - before;
    if (gain < 0) {
        for (int i=0;i<W;++i) sg.cells[start+i] = newOrder[i];
        return gain;
    } else {
        for (int i=0;i<W;++i) {
            int cid = oldOrder[i];
            db.cells[cid].x = oldPos[i].first;
            db.cells[cid].y = oldPos[i].second;
        }
        for (int nid : aff) wl::recomputeNetBBox(db, nid, db.netBBox[nid]);
        return 0;
    }
}

// ---------------------------------------------------------------------------
inline const vector<vector<int>>& perms(int W) {
    static vector<vector<vector<int>>> cache(6);
    if (!cache[W].empty()) return cache[W];
    vector<int> p(W); iota(p.begin(), p.end(), 0);
    do cache[W].push_back(p); while (next_permutation(p.begin(), p.end()));
    return cache[W];
}

// ---------------------------------------------------------------------------
bool improveSegment(db::Database& db, Segment& sg, int W) {
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

// ---------------------------------------------------------------------------
namespace fastdp {

void optimize(db::Database& db, int windowSize, int maxPass) {
    buildAdjacency(db);
    wl::initAllNetBBox(db);

    windowSize = max(2, min(windowSize, 4));

    long long init = wl::totalHPWL(db);
    cerr << fixed << setprecision(0);
    cerr << "[FastDP] Initial HPWL = " << init << endl;

    for (int pass=0; pass<maxPass; ++pass) {
        auto segs = buildSegments(db);

        bool any = false;
        for (auto &sg : segs)
            any |= improveSegment(db, sg, min(windowSize, (int)sg.cells.size()));

        for (auto &sg : segs)
            legalizeSegmentOrder(db, sg);

        wl::initAllNetBBox(db);
        long long now = wl::totalHPWL(db);
        cerr << "[FastDP] Pass " << pass
             << "  HPWL=" << now
             << "  Δ=" << (100.0*(now - init)/init) << "%\n";
        if (!any) break;
    }

    auto segs = buildSegments(db);
    for (auto &sg : segs)
        legalizeSegmentOrder(db, sg);

    wl::initAllNetBBox(db);
    long long fin = wl::totalHPWL(db);
    cerr << "[FastDP] Final internal HPWL = " << fin << endl;
}

} // namespace fastdp
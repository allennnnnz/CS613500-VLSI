#include <bits/stdc++.h>
#include "global_reorder.hpp"
#include "segment_utils.hpp"
#include "wl.hpp"

using namespace std;

namespace fastdp {

static inline double targetXFor(const db::Database& db, int cid) {
    if (db.cell2nets[cid].empty()) return db.cells[cid].x;
    long long s=0; int cnt=0;
    for (int nid: db.cell2nets[cid]) {
        auto &b = db.netBBox[nid]; if (b[0] > b[1]) continue;
        s += (long long)(b[0] + b[1]); ++cnt;
    }
    if (cnt==0) return db.cells[cid].x;
    return (double)s/(2.0*cnt);
}

static void collectAffected(const db::Database& db, const vector<int>& ids, unordered_set<int>& nets) {
    for (int cid: ids) for (int nid: db.cell2nets[cid]) nets.insert(nid);
}

static long long netsHPWL(const db::Database& db, const unordered_set<int>& nets) {
    long long s=0; for (int nid: nets) { auto &b=db.netBBox[nid]; if (b[0]>b[1]||b[2]>b[3]) continue; s += (long long)(b[1]-b[0]) + (long long)(b[3]-b[2]); } return s; }

// 嘗試對一個 segment 依 targetX 重新排序＆壓實，若改善則接受
static bool improveSegmentByTarget(db::Database& db, Segment& sg) {
    if (sg.cells.size() < 2) return false;
    const auto &row = db.rows[sg.rowId];

    // 原次序 & 位置
    vector<int> orig = sg.cells;
    vector<pair<int,int>> oldPos; oldPos.reserve(sg.cells.size());
    for (int cid: sg.cells) oldPos.push_back({db.cells[cid].x, db.cells[cid].y});

    // 目標排序
    vector<pair<double,int>> order; order.reserve(sg.cells.size());
    for (int cid: sg.cells) order.push_back({targetXFor(db, cid), cid});
    stable_sort(order.begin(), order.end(), [&](auto &a, auto &b){ return a.first < b.first; });

    // 擺放（從 sg.xL 起，snap 到 site grid）
    int curx = snapToSite(sg.xL, row); vector<int> newOrder; newOrder.reserve(sg.cells.size());
    vector<pair<int,int>> newPos(sg.cells.size()); bool overflow=false;
    for (size_t i=0;i<order.size();++i) {
        int cid = order[i].second; auto &c=db.cells[cid];
        int placeX = snapToSite(curx, row);
        if (placeX + c.width > sg.xR) { overflow=true; break; }
        newOrder.push_back(cid); newPos[i] = {placeX, row.y}; curx = placeX + c.width;
    }
    if (overflow) return false;

    // before
    unordered_set<int> aff; collectAffected(db, sg.cells, aff);
    long long before = netsHPWL(db, aff);

    // apply
    for (size_t i=0;i<newOrder.size();++i) {
        int cid=newOrder[i]; db.cells[cid].x=newPos[i].first; db.cells[cid].y=newPos[i].second; db.cells[cid].orient=row.orient;
    }
    for (int nid: aff) wl::recomputeNetBBox(db, nid, db.netBBox[nid]);
    long long after = netsHPWL(db, aff);

    if (after < before) {
        sg.cells.swap(newOrder);
        return true;
    } else {
        // revert
        for (size_t i=0;i<orig.size();++i) { int cid=orig[i]; db.cells[cid].x=oldPos[i].first; db.cells[cid].y=oldPos[i].second; }
        for (int nid: aff) wl::recomputeNetBBox(db, nid, db.netBBox[nid]);
        return false;
    }
}

bool performGlobalReorder(db::Database& db) {
    auto segs = buildSegments(db);
    bool any=false;
    for (auto &sg: segs) any |= improveSegmentByTarget(db, sg);
    for (auto &sg: segs) legalizeSegmentOrder(db, sg);
    return any;
}

}

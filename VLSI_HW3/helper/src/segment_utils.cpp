#include "segment_utils.hpp"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
using namespace std;

namespace fastdp {

void buildAdjacency(db::Database& db) {
    db.cell2nets.assign(db.cells.size(), {});
    for (int nid = 0; nid < (int)db.nets.size(); ++nid)
        for (int cid : db.nets[nid].cellIds)
            db.cell2nets[cid].push_back(nid);
}

int snapToSite(int x, const db::Row& r) {
    int dx = x - r.x;
    if (dx < 0) return r.x;
    int k = dx / r.stepX;
    return r.x + k * r.stepX;
}

vector<Segment> buildSegments(db::Database& db) {
    unordered_map<int,int> y2row;
    for (int i=0;i<(int)db.rows.size();++i)
        y2row[db.rows[i].y] = i;

    vector<pair<int,int>> rowSpan(db.rows.size());
    for (int i=0;i<(int)db.rows.size();++i) {
        auto &r = db.rows[i];
        rowSpan[i] = {r.x, r.x + r.siteCount * r.stepX};
    }

    vector<vector<int>> rowMov(db.rows.size()), rowFix(db.rows.size());
    for (int cid=0; cid<(int)db.cells.size(); ++cid) {
        const auto &c = db.cells[cid];
        if (c.fixed) {
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

    vector<Segment> segs;
    for (int rid=0; rid<(int)db.rows.size(); ++rid) {
        auto [x0,x1] = rowSpan[rid];
        auto &fix = rowFix[rid];
        sort(fix.begin(), fix.end(),
             [&](int a,int b){ return db.cells[a].x < db.cells[b].x; });

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
        if (cur < x1)
            segs.push_back({rid, cur, x1, {}});

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

void legalizeSegmentOrder(db::Database& db, Segment& sg) {
    const auto &row = db.rows[sg.rowId];
    int curx = snapToSite(sg.xL, row);
    for (int cid : sg.cells) {
        auto &c = db.cells[cid];
        int want = c.x;
        int placed = max(want, curx);
        placed = snapToSite(placed, row);
        int rightLimit = sg.xR - c.width;
        if (placed > rightLimit) placed = rightLimit;
        if (placed < curx) placed = curx;
        c.x = placed;
        c.y = row.y;
        c.orient = row.orient;
        curx = placed + c.width;
    }
}

} // namespace fastdp
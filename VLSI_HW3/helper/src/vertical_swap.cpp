#include <bits/stdc++.h>
#include "vertical_swap.hpp"
#include "segment_utils.hpp"
#include "wl.hpp"

using namespace std;

namespace fastdp {

// 收集交換影響的 nets
static inline void collectNets(const db::Database& db, int a, int b, unordered_set<int>& nets) {
    for (int nid : db.cell2nets[a]) nets.insert(nid);
    for (int nid : db.cell2nets[b]) nets.insert(nid);
}

// 局部 HPWL
static long long partialHPWL(const db::Database& db, const unordered_set<int>& nets) {
    long long s=0; for (int nid: nets) { auto &b = db.netBBox[nid]; if (b[0] > b[1]|| b[2]>b[3]) continue; s += (long long)(b[1]-b[0]) + (long long)(b[3]-b[2]); } return s; }

// 確認 cell 在其 row 邊界內（不考慮重疊，重疊由行後壓實處理）
static inline bool withinRow(const db::Row* r, const db::Cell& c) {
    if (!r) return false; int rx1 = r->x + r->siteCount * r->stepX; return c.x >= r->x && c.x + c.width <= rx1; }

// 嘗試交換垂直相鄰 row 的兩顆 cell（不改變 x），若 HPWL 改善則接受
static long long tryVerticalSwap(db::Database& db, int a, int b) {
    if (a==b) return 0; auto &A=db.cells[a]; auto &B=db.cells[b]; if (A.fixed || B.fixed) return 0; if (A.y==B.y) return 0;
    const db::Row* rA=nullptr; const db::Row* rB=nullptr; for (auto &r: db.rows){ if(r.y==A.y) rA=&r; else if(r.y==B.y) rB=&r; if(rA&&rB) break; }
    if (!rA||!rB) return 0; if (abs(A.y - B.y) > rA->stepY*2) return 0; // 僅考慮相鄰或隔一行
    if (A.width != B.width) return 0; // 保守：僅寬度相等避免重疊複雜性

    unordered_set<int> nets; collectNets(db,a,b,nets);
    long long before = partialHPWL(db, nets);
    int ax=A.x, ay=A.y, bx=B.x, by=B.y; A.y=by; B.y=ay; // 水平不動
    // 邊界檢查
    const db::Row* rAnew=nullptr; const db::Row* rBnew=nullptr; for (auto &r: db.rows){ if(r.y==A.y) rAnew=&r; else if(r.y==B.y) rBnew=&r; if(rAnew&&rBnew) break; }
    bool legal = withinRow(rAnew,A) && withinRow(rBnew,B);
    long long gain=0;
    if (legal) {
        vector<array<int,4>> old; old.reserve(nets.size()); vector<int> ids; ids.reserve(nets.size());
        for (int nid: nets){ old.push_back(db.netBBox[nid]); ids.push_back(nid);} for(int nid: ids) wl::recomputeNetBBox(db,nid,db.netBBox[nid]);
        long long after=partialHPWL(db,nets); gain = before - after; for(size_t i=0;i<ids.size();++i) db.netBBox[ids[i]]=old[i];
    }
    A.x=ax; A.y=ay; B.x=bx; B.y=by;
    return gain>0?gain:0;
}

bool performVerticalSwap(db::Database& db) {
    // 建立 row -> cell list
    unordered_map<int, vector<int>> rowCells; rowCells.reserve(db.rows.size()*2);
    for (int i=0;i<(int)db.cells.size();++i) if(!db.cells[i].fixed) rowCells[db.cells[i].y].push_back(i);
    for (auto &kv: rowCells) sort(kv.second.begin(), kv.second.end(), [&](int a, int b){return db.cells[a].x < db.cells[b].x;});
    vector<int> rowsY; rowsY.reserve(rowCells.size()); for (auto &kv: rowCells) rowsY.push_back(kv.first); sort(rowsY.begin(), rowsY.end());

    bool improved=false; int swapCnt=0;
    // 逐對相鄰 row 嘗試：對齊 x 上的 cell（利用雙指標）
    for (size_t i=0;i+1<rowsY.size();++i) {
        int y1=rowsY[i], y2=rowsY[i+1]; auto &v1=rowCells[y1]; auto &v2=rowCells[y2];
        size_t p1=0,p2=0; while(p1<v1.size() && p2<v2.size()) {
            auto &C1=db.cells[v1[p1]]; auto &C2=db.cells[v2[p2]]; int x1=C1.x, x2=C2.x;
            if (x1==x2 && C1.width==C2.width) {
                long long g = tryVerticalSwap(db, v1[p1], v2[p2]);
                if (g>0) {
                    // 實際套用
                    int ax=C1.x, ay=C1.y, bx=C2.x, by=C2.y; C1.y=by; C2.y=ay;
                    unordered_set<int> nets; collectNets(db,v1[p1],v2[p2],nets);
                    for(int nid: nets) wl::recomputeNetBBox(db,nid,db.netBBox[nid]);
                    ++swapCnt; improved=true;
                }
                ++p1; ++p2; // 無論是否交換都前進
            } else if (x1 < x2) {
                ++p1;
            } else {
                ++p2;
            }
        }
    }
    if (swapCnt>0) cerr << "[VerticalSwap] Swaps=" << swapCnt << "\n";
    return improved;
}

// ============ Vertical Relocate ============
static inline const db::Row* findRowForY(const db::Database& db, int y) {
    for (const auto &r: db.rows) if (r.y==y) return &r; return nullptr;
}

static inline double targetX(const db::Database& db, int cid) {
    if (db.cell2nets[cid].empty()) return db.cells[cid].x;
    long long s=0; int cnt=0; for(int nid: db.cell2nets[cid]){ auto &b=db.netBBox[nid]; if(b[0]>b[1]) continue; s += (long long)(b[0]+b[1]); ++cnt; }
    return cnt? (double)s/(2.0*cnt) : db.cells[cid].x;
}

static int snapToSiteX(int x, const db::Row& r){ return fastdp::snapToSite(x, r); }

// 在 row 的 cell 列表（已依 x 排序）中尋找最靠近 target 的可容納寬度的空隙
static bool findNearestLegalX(const db::Database& db, const db::Row& row, const vector<int>& rowVec,
                              int width, int target, int &outX){
    int left=row.x, right=row.x + row.siteCount*row.stepX;
    auto fits=[&](int L, int R){ return R-L >= width; };
    long long bestDist = LLONG_MAX; int bestX=-1;

    auto consider = [&](int L, int R){
        if (!fits(L,R)) return; int tx = std::min(max(L, target - width/2), R - width);
        tx = snapToSiteX(tx, row); tx = max(L, min(tx, R-width)); long long d=llabs((long long)tx - target);
        if (d < bestDist) { bestDist=d; bestX=tx; }
    };

    if (rowVec.empty()) { consider(left, right); if (bestX>=0){ outX=bestX; return true;} return false; }

    // gap before first
    const auto &C0=db.cells[rowVec.front()]; consider(left, C0.x);
    // gaps between
    for (size_t i=0;i+1<rowVec.size();++i){ const auto &A=db.cells[rowVec[i]]; const auto &B=db.cells[rowVec[i+1]]; consider(A.x + A.width, B.x); }
    // gap after last
    const auto &Cn=db.cells[rowVec.back()]; consider(Cn.x + Cn.width, right);

    if (bestX>=0){ outX=bestX; return true; } return false;
}

bool performVerticalRelocate(db::Database& db){
    // y -> cells sorted by x
    unordered_map<int, vector<int>> rowCells; rowCells.reserve(db.rows.size()*2);
    for (int i=0;i<(int)db.cells.size();++i) if(!db.cells[i].fixed) rowCells[db.cells[i].y].push_back(i);
    for (auto &kv: rowCells) sort(kv.second.begin(), kv.second.end(), [&](int a, int b){return db.cells[a].x < db.cells[b].x;});

    vector<int> movable; movable.reserve(db.cells.size());
    for (int i=0;i<(int)db.cells.size();++i) if(!db.cells[i].fixed) movable.push_back(i);

    bool improved=false; int moves=0;
    // 以度數高者優先
    sort(movable.begin(), movable.end(), [&](int a, int b){ return db.cell2nets[a].size() > db.cell2nets[b].size(); });

    for (int a: movable){
        auto &A=db.cells[a]; const db::Row* rA=findRowForY(db, A.y); if(!rA) continue;
        int stepY=rA->stepY; if(stepY<=0) continue; double txd = targetX(db, a); int tx = (int)std::round(txd);

        for (int dy: {stepY, -stepY}){
            int ty = A.y + dy; auto it=rowCells.find(ty); if(it==rowCells.end()) continue; const db::Row* rT=findRowForY(db, ty); if(!rT) continue;
            int placeX; if(!findNearestLegalX(db, *rT, it->second, A.width, tx, placeX)) continue;

            // evaluate
            unordered_set<int> nets; for(int nid: db.cell2nets[a]) nets.insert(nid);
            long long before=0, after=0; for(int nid: nets){ auto &b=db.netBBox[nid]; if(b[0]>b[1]||b[2]>b[3]) continue; before += (long long)(b[1]-b[0]) + (long long)(b[3]-b[2]); }
            int ox=A.x, oy=A.y; A.x=placeX; A.y=ty; for(int nid: nets) wl::recomputeNetBBox(db,nid,db.netBBox[nid]);
            for(int nid: nets){ auto &b=db.netBBox[nid]; if(b[0]>b[1]||b[2]>b[3]) continue; after += (long long)(b[1]-b[0]) + (long long)(b[3]-b[2]); }
            long long gain = before - after;
            if (gain > 0){
                // accept, update rowCells: remove from old row, insert into new row at correct position
                auto &vecOld = rowCells[oy]; auto &vecNew=rowCells[ty];
                vecOld.erase(remove(vecOld.begin(), vecOld.end(), a), vecOld.end());
                auto itIns = lower_bound(vecNew.begin(), vecNew.end(), placeX, [&](int cid, int X){ return db.cells[cid].x < X; });
                vecNew.insert(itIns, a);
                ++moves; improved=true; break; // move at most once per cell this pass
            } else {
                // revert
                A.x=ox; A.y=oy; for(int nid: nets) wl::recomputeNetBBox(db,nid,db.netBBox[nid]);
            }
        }
    }
    if (moves>0) { cerr << "[VerticalRelocate] Moves=" << moves << "\n"; }
    return improved;
}

} // namespace fastdp

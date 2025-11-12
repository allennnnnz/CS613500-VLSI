#include "global_swap.hpp"
#include "segment_utils.hpp"
#include "wl.hpp"
#include <bits/stdc++.h>
#ifdef _OPENMP
#include <omp.h>
#endif
using namespace std;

namespace fastdp {

static const db::Row* findRowForY(const db::Database& db, int y) {
    for (const auto& r : db.rows)
        if (r.y == y) return &r;
    return nullptr;
}

static long long partialHPWL(const db::Database& db, const unordered_set<int>& nets)
{
    long long s = 0;
    for (int nid : nets) {
        auto &b = db.netBBox[nid];
        if (b[0] > b[1] || b[2] > b[3]) continue;
        s += (long long)(b[1] - b[0]) + (long long)(b[3] - b[2]);
    }
    return s;
}

// 快速檢查：cid 在其 row 內是否與其他 cell 重疊（使用目前位置）
static bool noRowOverlapFor(const db::Database& db, int cid) {
    const auto &C = db.cells[cid];
    int L = C.x, R = C.x + C.width;
    for (int j = 0; j < (int)db.cells.size(); ++j) {
        if (j == cid) continue;
        const auto &D = db.cells[j];
        if (D.fixed) continue;
        if (D.y != C.y) continue;
        int L2 = D.x, R2 = D.x + D.width;
        if (max(L, L2) < min(R, R2)) return false; // overlap
    }
    return true;
}

// 嘗試將 cell a 與 cell b 交換位置（僅在 HPWL 嚴格改善且不違法才接受）
// 計算交換的加權增益：raw_gain - lambda * 移動距離
static long long trySwapGain(db::Database& db, int a, int b, double lambda)
{
    if (a == b) return 0;
    auto &A = db.cells[a];
    auto &B = db.cells[b];
    if (A.fixed || B.fixed) return 0;

    const db::Row* rowA = findRowForY(db, A.y);
    const db::Row* rowB = findRowForY(db, B.y);
    if (!rowA || !rowB) return 0;

    // 先蒐集受影響 nets
    unordered_set<int> nets;
    for (int nid : db.cell2nets[a]) nets.insert(nid);
    for (int nid : db.cell2nets[b]) nets.insert(nid);
    long long before = partialHPWL(db, nets);

    // 模擬交換
    int ax=A.x, ay=A.y;
    int bx=B.x, by=B.y;
    A.x=bx; A.y=by; B.x=ax; B.y=ay;

    const db::Row* rowAnew = findRowForY(db, A.y);
    const db::Row* rowBnew = findRowForY(db, B.y);
    bool inBounds = rowAnew && rowBnew;
    if (inBounds) {
        int rightA = A.x + A.width;
        int rightB = B.x + B.width;
        int rowAend = rowAnew->x + rowAnew->siteCount * rowAnew->stepX;
        int rowBend = rowBnew->x + rowBnew->siteCount * rowBnew->stepX;
        inBounds &= (A.x >= rowAnew->x && rightA <= rowAend && B.x >= rowBnew->x && rightB <= rowBend);
    }

    bool legal = inBounds && noRowOverlapFor(db, a) && noRowOverlapFor(db, b);
    long long gain = 0;
    if (legal) {
        // 更新受影響 net 的 bbox 以計算 after
        vector<array<int,4>> old;
        old.reserve(nets.size());
        vector<int> netIds; netIds.reserve(nets.size());
        for (int nid : nets) { old.push_back(db.netBBox[nid]); netIds.push_back(nid); }

        for (int nid : netIds) wl::recomputeNetBBox(db, nid, db.netBBox[nid]);
    long long after = partialHPWL(db, nets);
    long long raw_gain = before - after; // >0 表示改善
    long long move_dist = llabs(ax - bx); // 水平距離（row 內對齊）
    long long penalty = (long long)(lambda * move_dist);
    gain = raw_gain - penalty;

        // 還原 bbox（真正接受時再重算一次）
        for (size_t k=0;k<netIds.size();++k) db.netBBox[netIds[k]] = old[k];
    }

    // 還原座標
    A.x=ax; A.y=ay; B.x=bx; B.y=by;
    return max(0LL, gain);
}

// 輕量合法化：逐 row 壓實，避免重疊並對齊 site grid
static void performLegalization(db::Database& db)
{
    for (const auto& row : db.rows) {
        int y = row.y;
        int left = row.x;
        int right = row.x + row.siteCount * row.stepX;
        vector<int> idx;
        idx.reserve(db.cells.size());
        for (int i = 0; i < (int)db.cells.size(); ++i) {
            const auto &c = db.cells[i];
            if (c.fixed) continue;
            if (c.y != y) continue;
            idx.push_back(i);
        }
        sort(idx.begin(), idx.end(), [&](int a, int b){ return db.cells[a].x < db.cells[b].x; });

        int cursor = left;
        for (int cid : idx) {
            auto &C = db.cells[cid];
            int want = max(C.x, cursor);
            // 對齊 site grid（往右取不小於 want 的最近 site）
            int dx = want - row.x;
            if (dx < 0) dx = 0;
            int snaps = (dx + row.stepX - 1) / row.stepX;
            int nx = row.x + snaps * row.stepX;
            // 邊界保護
            nx = max(nx, left);
            nx = min(nx, right - C.width);
            C.x = nx; C.y = y; // 保持在該 row
            cursor = C.x + C.width;
            if (cursor > right) cursor = right; // 超界就卡在尾端
        }
    }
    // 重新計算所有 net bbox 確保一致
    wl::initAllNetBBox(db);
}

bool performGlobalSwap(db::Database& db)
{
    cerr << "[GlobalSwap] Start...\n";

    auto buildRowCells = [&](){
        unordered_map<int, vector<int>> rc;
        rc.reserve(db.rows.size()*2);
        for (int i = 0; i < (int)db.cells.size(); ++i) if (!db.cells[i].fixed) rc[db.cells[i].y].push_back(i);
        for (auto &kv : rc) {
            auto &v = kv.second;
            sort(v.begin(), v.end(), [&](int a, int b){ return db.cells[a].x < db.cells[b].x; });
        }
        return rc;
    };

    vector<int> movable;
    movable.reserve(db.cells.size());
    for (int i = 0; i < (int)db.cells.size(); ++i)
        if (!db.cells[i].fixed) movable.push_back(i);
    if (movable.empty()) { cerr << "[GlobalSwap] No movable cells.\n"; return false; }

    // 度數排序（高網路連接度優先）
    sort(movable.begin(), movable.end(), [&](int a, int b){
        return db.cell2nets[a].size() > db.cell2nets[b].size();
    });

    bool improved = false;
    int swapCount = 0;

    const int K = 512;             // 總候選上限(更大)
    const int maxRounds = 12;      // 內部多輪
    const int search_window = 160; // 空間限制（以 site 為單位，擴大）
    double lambda = 0.0;         // 暫停移動距離懲罰以放行有效交換

    // 如果可取得 siteX 用於尺度化（假設所有 row stepX 相同）
    int siteX = 1;
    if (!db.rows.empty()) siteX = db.rows[0].stepX;
    lambda *= siteX; // 將係數轉換成與距離相同的尺度

    for (int round = 0; round < maxRounds; ++round) {
        bool anyThisRound = false;
        auto rowCells = buildRowCells();
        long long hpwl_before_round = wl::totalHPWL(db);
        // 行為：按度數高→低逐一嘗試
        #pragma omp parallel for schedule(static)
        for (int idx = 0; idx < (int)movable.size(); ++idx) {
            int a = movable[idx];
        const auto &A = db.cells[a];
        auto itRow = rowCells.find(A.y);
        if (itRow == rowCells.end() || itRow->second.size() <= 1) continue;
        const db::Row* rowA = findRowForY(db, A.y);
        int stepY = rowA ? rowA->stepY : 0;
            int stepX = rowA ? rowA->stepX : 1;

        // 估計最佳 x 中心：取 a 所連接 nets 的 bbox union 中心
        int xmin = INT_MAX, xmax = INT_MIN;
        for (int nid : db.cell2nets[a]) {
            const auto &b = db.netBBox[nid];
            if (b[0] > b[1] || b[2] > b[3]) continue;
            xmin = min(xmin, b[0]);
            xmax = max(xmax, b[1]);
        }
        int xCenter = (xmin==INT_MAX)? A.x : (xmin + xmax)/2;

            // 收集候選：同 row + 相鄰 row（|Δy|<=stepY），距離限制 |x - xCenter| <= search_window*stepX
        vector<int> cand; cand.reserve(K);
        auto gatherKFromRow = [&](const vector<int>& vec, int k){
            if (vec.empty() || (int)cand.size() >= K) return;
            auto it = lower_bound(vec.begin(), vec.end(), xCenter,
                                  [&](int cid, int X){ return db.cells[cid].x < X; });
            int li = (int)(it - vec.begin()) - 1;
            int ri = (int)(it - vec.begin());
            while ((int)cand.size() < K && k > 0 && (li >= 0 || ri < (int)vec.size())) {
                int chooseLeft = -1;
                if (li >= 0) chooseLeft = abs(db.cells[vec[li]].x - xCenter);
                int chooseRight = -1;
                if (ri < (int)vec.size()) chooseRight = abs(db.cells[vec[ri]].x - xCenter);
                if (chooseRight >= 0 && (chooseLeft < 0 || chooseRight <= chooseLeft)) { cand.push_back(vec[ri++]); }
                else { cand.push_back(vec[li--]); }
                --k;
            }
        };

            // 同 row 先取較多（例如 K 的一半）
            const auto &vecSame = itRow->second;
            gatherKFromRow(vecSame, K/2);
            // 相鄰 row（上/下）各取少量，並擴展到 ±2*stepY
            if (stepY > 0) {
                auto itUp1 = rowCells.find(A.y + stepY);
                if (itUp1 != rowCells.end()) gatherKFromRow(itUp1->second, K/6);
                auto itDn1 = rowCells.find(A.y - stepY);
                if (itDn1 != rowCells.end()) gatherKFromRow(itDn1->second, K/6);
                auto itUp2 = rowCells.find(A.y + 2*stepY);
                if (itUp2 != rowCells.end()) gatherKFromRow(itUp2->second, K/6);
                auto itDn2 = rowCells.find(A.y - 2*stepY);
                if (itDn2 != rowCells.end()) gatherKFromRow(itDn2->second, K - (int)cand.size());
            }

            // 套用距離篩選
            cand.erase(remove_if(cand.begin(), cand.end(), [&](int cid){
                return abs(db.cells[cid].x - xCenter) > search_window * stepX;
            }), cand.end());

            long long bestGain = 0;
            int bestB = -1;
            for (int b : cand) {
                if (b == a) continue;
                long long g = trySwapGain(db, a, b, lambda);
                if (g > bestGain) { bestGain = g; bestB = b; }
            }

            if (bestB >= 0 && bestGain > 0) {
                // 實際交換並更新受影響 nets 的 bbox（需要序列化保護）
                #pragma omp critical
                {
                    unordered_set<int> nets;
                    for (int nid : db.cell2nets[a]) nets.insert(nid);
                    for (int nid : db.cell2nets[bestB]) nets.insert(nid);
                    auto &A2 = db.cells[a]; auto &B2 = db.cells[bestB];
                    int ax=A2.x, ay=A2.y, bx=B2.x, by=B2.y;
                    A2.x=bx; A2.y=by; B2.x=ax; B2.y=ay;
                    for (int nid : nets) wl::recomputeNetBBox(db, nid, db.netBBox[nid]);
                    ++swapCount; improved = true; anyThisRound = true;
                }
            }
        }
        long long hpwl_after_round = wl::totalHPWL(db);
        double rel_improve = 100.0 * (hpwl_before_round - hpwl_after_round) / hpwl_before_round;
        cerr << "[GlobalSwap] Round " << round << " ΔHPWL=" << fixed << setprecision(3) << rel_improve << "%\n";
        if (!anyThisRound) {
            cerr << "[GlobalSwap] No beneficial swap this round. Early stop.\n";
            break; // 提前結束
        }
        // 不再以固定門檻停止，僅當本輪無有效交換才提前結束
    }

    if (swapCount > 0) {
        // 交換後做一次輕量合法化，避免殘留重疊/未對齊
        performLegalization(db);
        
        // 訊息
        cerr << "[GlobalSwap] Swaps performed: " << swapCount << "\n";
    } else
        cerr << "[GlobalSwap] No beneficial swaps found.\n";

    return improved;
}

} // namespace fastdp
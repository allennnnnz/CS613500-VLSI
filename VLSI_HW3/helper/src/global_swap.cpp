#include "global_swap.hpp"
#include "segment_utils.hpp"
#include "wl.hpp"
#include <bits/stdc++.h>
using namespace std;

namespace fastdp {

// ============================================================
// 計算 cell 的 optimal region（由 nets 的 bbox 定義）
// ============================================================
static pair<int,int> getMedianXRange(const db::Database& db, int cid)
{
    vector<int> xs;
    for (int nid : db.cell2nets[cid]) {
        const auto& box = db.netBBox[nid];
        if (box[0] > box[1]) continue;
        xs.push_back(box[0]);
        xs.push_back(box[1]);
    }
    if (xs.empty()) return {db.cells[cid].x, db.cells[cid].x};
    sort(xs.begin(), xs.end());
    int mid1 = xs[xs.size()/2];
    int mid2 = xs[(xs.size()-1)/2];
    return {min(mid1,mid2), max(mid1,mid2)};
}

static pair<int,int> getMedianYRange(const db::Database& db, int cid)
{
    vector<int> ys;
    for (int nid : db.cell2nets[cid]) {
        const auto& box = db.netBBox[nid];
        if (box[2] > box[3]) continue;
        ys.push_back(box[2]);
        ys.push_back(box[3]);
    }
    if (ys.empty()) return {db.cells[cid].y, db.cells[cid].y};
    sort(ys.begin(), ys.end());
    int mid1 = ys[ys.size()/2];
    int mid2 = ys[(ys.size()-1)/2];
    return {min(mid1,mid2), max(mid1,mid2)};
}

// ============================================================
// overlap penalty：距離/重疊越大懲罰越高
// ============================================================
static double overlapPenalty(const db::Cell& A, const db::Cell& B)
{
    int dx = max(0, min(A.x + A.width, B.x + B.width) - max(A.x, B.x));
    int dy = max(0, min(A.y + A.height, B.y + B.height) - max(A.y, B.y));
    return 0.5 * dx * dy;  // penalty proportional to overlap area
}

// ============================================================
// 嘗試計算 swap “benefit”
// B = (W1 - W2) - P1 - P2
// ============================================================
static double computeBenefit(db::Database& db, int a, int b)
{
    if (a == b) return -1e9;
    auto& A = db.cells[a];
    auto& B = db.cells[b];
    if (A.fixed || B.fixed) return -1e9;

    // 取得相關 nets
    unordered_set<int> nets;
    for (int nid : db.cell2nets[a]) nets.insert(nid);
    for (int nid : db.cell2nets[b]) nets.insert(nid);
    double W1 = wl::partialHPWL(db, nets);

    // 暫時交換
    int ax=A.x, ay=A.y, bx=B.x, by=B.y;
    A.x=bx; A.y=by; B.x=ax; B.y=ay;
    for (int nid : nets) wl::recomputeNetBBox(db, nid, db.netBBox[nid]);
    double W2 = wl::partialHPWL(db, nets);

    // overlap penalty
    double P1 = overlapPenalty(A,B);
    double P2 = 0.0;

    // 恢復
    A.x=ax; A.y=ay; B.x=bx; B.y=by;
    for (int nid : nets) wl::recomputeNetBBox(db, nid, db.netBBox[nid]);

    double Bv = (W1 - W2) - P1 - P2;
    return Bv;
}

// ============================================================
// 主函式：performGlobalSwap（依據論文算法）
// ============================================================
bool performGlobalSwap(db::Database& db)
{
    cerr << "[GlobalSwap] Start (Optimal Region based)...\n";

    vector<int> movable;
    for (int i=0;i<(int)db.cells.size();++i)
        if (!db.cells[i].fixed) movable.push_back(i);
    if (movable.empty()) return false;

    int swapCount = 0;
    bool improved = false;
    double totalBenefit = 0.0;

    for (int cid : movable)
    {
        const auto& c = db.cells[cid];

        // Step 1. 求 optimal region
        auto [xL,xR] = getMedianXRange(db,cid);
        auto [yB,yT] = getMedianYRange(db,cid);
        int cx = (xL+xR)/2;
        int cy = (yB+yT)/2;

        // Step 2. 找 region 內候選 cells
        vector<int> candidates;
        for (int j=0;j<(int)db.cells.size();++j) {
            if (j==cid) continue;
            const auto& t = db.cells[j];
            if (t.fixed) continue;
            if (t.x >= xL && t.x <= xR && t.y >= yB && t.y <= yT)
                candidates.push_back(j);
        }

        // 若 region 內沒有 cell，略過
        if (candidates.empty()) continue;

        // Step 3. 找出最佳 benefit 的 candidate
        double bestB = -1e9;
        int bestJ = -1;
        for (int j : candidates) {
            double Bv = computeBenefit(db, cid, j);
            if (Bv > bestB) {
                bestB = Bv;
                bestJ = j;
            }
        }

        // Step 4. 若有 positive benefit → 接受交換
        if (bestB > 0 && bestJ >= 0) {
            auto &A = db.cells[cid];
            auto &B = db.cells[bestJ];
            swap(A.x,B.x);
            swap(A.y,B.y);
            ++swapCount;
            totalBenefit += bestB;
            improved = true;
        }
    }

    // Step 5. legalize once after all swaps
    auto segs = buildSegments(db);
    for (auto &sg : segs) legalizeSegmentOrder(db, sg);

    wl::initAllNetBBox(db);
    cerr << "[GlobalSwap] Done. swaps="<<swapCount<<" totalBenefit="<<totalBenefit<<"\n";
    return improved;
}

} // namespace fastdp
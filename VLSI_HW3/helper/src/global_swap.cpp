#include "global_swap.hpp"
#include "segment_utils.hpp"
#include "wl.hpp"
#include <bits/stdc++.h>
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

// 嘗試將 cell a 與 cell b 交換位置（保留合法性）
static bool trySwap(db::Database& db, int a, int b)
{
    if (a == b) return false;
    auto &A = db.cells[a];
    auto &B = db.cells[b];
    if (A.fixed || B.fixed) return false;

    const db::Row* rowA = findRowForY(db, A.y);
    const db::Row* rowB = findRowForY(db, B.y);
    if (!rowA || !rowB) return false;

    // 同 row overlap check
    if (A.y == B.y) {
        if (abs(A.x - B.x) < max(A.width, B.width))
            return false;
    }

    unordered_set<int> nets;
    for (int nid : db.cell2nets[a]) nets.insert(nid);
    for (int nid : db.cell2nets[b]) nets.insert(nid);
    long long before = partialHPWL(db, nets);

    // swap
    int tmpx=A.x, tmpy=A.y;
    A.x=B.x; A.y=B.y;
    B.x=tmpx; B.y=tmpy;

    const db::Row* rowAnew = findRowForY(db, A.y);
    const db::Row* rowBnew = findRowForY(db, B.y);
    if (!rowAnew || !rowBnew) {
        std::swap(A.x,B.x); std::swap(A.y,B.y);
        return false;
    }

    int rightA = A.x + A.width;
    int rightB = B.x + B.width;
    int rowAend = rowAnew->x + rowAnew->siteCount * rowAnew->stepX;
    int rowBend = rowBnew->x + rowBnew->siteCount * rowBnew->stepX;
    if (A.x < rowAnew->x || rightA > rowAend || B.x < rowBnew->x || rightB > rowBend) {
        std::swap(A.x,B.x); std::swap(A.y,B.y);
        return false;
    }

    for (int nid : nets)
        wl::recomputeNetBBox(db, nid, db.netBBox[nid]);
    long long after = partialHPWL(db, nets);

    // 改良：容忍少量 HPWL 增加（利於 escape local min）
    bool accept = (after < before * 1.001);

    if (!accept) {
        std::swap(A.x,B.x); std::swap(A.y,B.y);
        for (int nid : nets)
            wl::recomputeNetBBox(db, nid, db.netBBox[nid]);
    }

    return accept;
}

bool performGlobalSwap(db::Database& db)
{
    cerr << "[GlobalSwap] Start...\n";

    vector<int> movable;
    for (int i = 0; i < (int)db.cells.size(); ++i)
        if (!db.cells[i].fixed) movable.push_back(i);

    if (movable.empty()) {
        cerr << "[GlobalSwap] No movable cells.\n";
        return false;
    }

    std::mt19937 rng(42);
    std::shuffle(movable.begin(), movable.end(), rng);

    bool improved = false;
    int swapCount = 0;
    int maxTry = min((int)movable.size(), 1500);

    for (int i = 0; i < maxTry; ++i) {
        int a = movable[i];
        int b = movable[rng() % movable.size()];
        if (abs(db.cells[a].y - db.cells[b].y) > 4 * db.rows.front().stepY)
            continue;

        if (trySwap(db, a, b)) {
            ++swapCount;
            improved = true;
        }
    }

    if (swapCount > 0)
        cerr << "[GlobalSwap] Swaps performed: " << swapCount << "\n";
    else
        cerr << "[GlobalSwap] No beneficial swaps found.\n";

    return improved;
}

} // namespace fastdp
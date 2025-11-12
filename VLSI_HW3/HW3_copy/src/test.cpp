#include "dp_stage.hpp"
#include <algorithm>
#include <iostream>
#include <vector>
#include <unordered_set>
#include <limits>
#include <cmath>
#include <chrono>
#include <random>

// ========== 基礎輔助工具 ==========

// 取得 instance 寬度
static inline int instWidth(const Design& d, int id) {
    if (id < 0 || id >= (int)d.insts.size()) return 1;
    int mid = d.insts[id].macroId;
    if (mid >= 0 && mid < (int)d.macros.size()) return d.macros[mid].width;
    return 1;
}

// 根據 y 找對應 row
static inline const Row* findRowForY(const Design& d, int y) {
    for (const auto& r : d.layout.rows)
        if (r.originY == y) return &r;
    return nullptr;
}

// 將 x 對齊 site
static inline int snap_to_site(const Row* r, int x, int width) {
    if (!r) return x;
    int dx = x - r->originX;
    int step = r->xStep > 0 ? r->xStep : 1;
    int s = (dx >= 0) ? (dx + step / 2) / step : (dx - step / 2) / step;
    int xs = r->originX + s * step;
    // 邊界限制
    int rowL = r->originX;
    int rowR = r->originX + r->xStep * r->numSites;
    xs = std::max(rowL, std::min(xs, rowR - width));
    return xs;
}

// ========== HPWL 計算工具 ==========

static inline int64_t netHPWL(const Design& d, int nid) {
    const Net& net = d.nets[nid];
    if (net.isSpecial || net.pins.empty()) return 0;
    int xmin = INT32_MAX, xmax = INT32_MIN;
    int ymin = INT32_MAX, ymax = INT32_MIN;

    for (auto& p : net.pins) {
        int x, y;
        if (p.isIO) {
            auto it = d.ioPinLoc.find(p.ioName);
            if (it != d.ioPinLoc.end()) { x = it->second.x; y = it->second.y; }
            else { x = p.ioLoc.x; y = p.ioLoc.y; }
        } else if (p.instId >= 0 && p.instId < (int)d.insts.size()) {
            x = d.insts[p.instId].x;
            y = d.insts[p.instId].y;
        } else continue;
        xmin = std::min(xmin, x); xmax = std::max(xmax, x);
        ymin = std::min(ymin, y); ymax = std::max(ymax, y);
    }
    return (xmax - xmin) + (ymax - ymin);
}

static inline int64_t totalHPWL(const Design& d) {
    int64_t sum = 0;
    for (int i = 0; i < (int)d.nets.size(); ++i)
        sum += netHPWL(d, i);
    return sum;
}

// 收集一組 cell 所連接的 nets
static inline std::unordered_set<int> collectNets(
    const std::vector<std::vector<int>>& inst2nets,
    const std::vector<int>& ids) {

    std::unordered_set<int> s; s.reserve(ids.size() * 3);
    for (int id : ids)
        for (int nid : inst2nets[id])
            s.insert(nid);
    return s;
}

// 計算一組 nets 的 HPWL
static inline int64_t netsHPWL(const Design& d,
                               const std::unordered_set<int>& nets) {
    int64_t sum = 0;
    for (int nid : nets) sum += netHPWL(d, nid);
    return sum;
}

// 暫時修改若干 cell x 座標，計算 ΔHPWL
static inline int64_t evalDeltaOverrides(Design& d,
                                         const std::unordered_set<int>& nets,
                                         const std::vector<std::pair<int,int>>& overrides) {
    int64_t oldH = netsHPWL(d, nets);
    std::vector<std::pair<int,int>> backup; backup.reserve(overrides.size());
    for (auto& p : overrides) {
        backup.emplace_back(p.first, d.insts[p.first].x);
        d.insts[p.first].x = p.second;
    }
    int64_t newH = netsHPWL(d, nets);
    for (auto& q : backup) d.insts[q.first].x = q.second;
    return newH - oldH;
}

// 建立 instance→nets adjacency
static std::vector<std::vector<int>> buildInstToNets(const Design& d) {
    std::vector<std::vector<int>> res(d.insts.size());
    for (int nid = 0; nid < (int)d.nets.size(); ++nid) {
        if (d.nets[nid].isSpecial) continue;
        for (auto& p : d.nets[nid].pins)
            if (!p.isIO && p.instId >= 0)
                res[p.instId].push_back(nid);
    }
    return res;
}

// ========== 基本合法化 (Row pack) ==========

static void clamp_and_left_pack_row(Design& d, const Row& r) {
    std::vector<int> cells;
    for (int i = 0; i < (int)d.insts.size(); ++i)
        if (d.insts[i].y == r.originY)
            cells.push_back(i);

    if (cells.empty()) return;
    std::sort(cells.begin(), cells.end(),
              [&](int a, int b) { return d.insts[a].x < d.insts[b].x; });

    int rowL = r.originX;
    int rowR = r.originX + r.xStep * r.numSites;
    int Lb = rowL;

    for (int idx = 0; idx < (int)cells.size();) {
        if (d.insts[cells[idx]].fixed) {
            Lb = std::max(Lb, d.insts[cells[idx]].x + instWidth(d, cells[idx]));
            ++idx;
            continue;
        }
        int j = idx;
        while (j < (int)cells.size() && !d.insts[cells[j]].fixed) ++j;

        int segLb = Lb, segRb = rowR;
        if (j < (int)cells.size()) segRb = std::min(segRb, d.insts[cells[j]].x);

        int cur = segLb;
        for (int k = idx; k < j; ++k) {
            int id = cells[k], w = instWidth(d, id);
            int want = std::min(std::max(d.insts[id].x, segLb), segRb - w);
            int xs = snap_to_site(&r, want, w);
            xs = std::max(xs, cur);
            xs = std::min(xs, segRb - w);
            d.insts[id].x = xs;
            cur = xs + w;
        }
        Lb = cur;
        idx = j;
    }
}

void final_legalize_rows(Design& d) {
    for (int round = 0; round < 2; ++round)
        for (const auto& r : d.layout.rows)
            clamp_and_left_pack_row(d, r);
}
// ========== 計算目標 x（估計最佳區域中心） ==========
static int compute_target_x(const Design& d,
                            int instId,
                            const std::vector<std::vector<int>>& inst2nets) {
    double sumWX = 0.0, sumW = 0.0;
    for (int nid : inst2nets[instId]) {
        const Net& net = d.nets[nid];
        bool hasIO = false;
        for (const auto& p : net.pins)
            if (p.isIO) { hasIO = true; break; }

        double w = 1.0;
        if ((int)net.pins.size() == 2) w *= 1.6;
        if (hasIO) w *= 1.2;
        if (w > 3.0) w = 3.0;

        for (const auto& p : net.pins) {
            if (!p.isIO && p.instId == instId) continue;
            int px;
            if (p.isIO) px = p.ioLoc.x;
            else {
                const Inst& I = d.insts[p.instId];
                int mw = (I.macroId >= 0 && I.macroId < (int)d.macros.size())
                         ? d.macros[I.macroId].width : 0;
                px = I.x + mw / 2;
            }
            sumWX += w * px;
            sumW  += w;
        }
    }
    if (sumW == 0.0) return d.insts[instId].x;
    return (int)std::llround(sumWX / sumW);
}

// ========== 左到右合法 pack ==========
static bool pack_in_order(const Design& d,
                          const Row* rp,
                          const std::vector<int>& ord,
                          int segLb, int segRb,
                          std::vector<std::pair<int,int>>& outOV) {
    outOV.clear(); outOV.reserve(ord.size());
    if (!rp) return false;

    int cur = std::max(segLb, rp->originX);
    for (int id : ord) {
        int w = instWidth(d, id);
        if (cur > segRb - w) return false;
        int xs = snap_to_site(rp, cur, w);
        xs = std::max(xs, cur);
        xs = std::min(xs, segRb - w);
        outOV.emplace_back(id, xs);
        cur = xs + w;
    }
    return true;
}

// ========== 目標順序打包 (single-segment clustering) ==========
static bool target_order_pack_segment(Design& d,
                                      const Row& r,
                                      const std::vector<int>& segCells,
                                      const std::vector<std::vector<int>>& inst2nets,
                                      int segLb, int segRb) {
    if (segCells.size() < 2) return false;
    const Row* rp = &r;

    // 1. 以目標 x 排序
    std::vector<int> ord = segCells;
    std::sort(ord.begin(), ord.end(), [&](int a,int b){
        int ta = compute_target_x(d,a,inst2nets);
        int tb = compute_target_x(d,b,inst2nets);
        return ta < tb;
    });

    // 2. 嘗試 pack
    std::vector<std::pair<int,int>> ov;
    if (!pack_in_order(d, rp, ord, segLb, segRb, ov)) {
        std::vector<int> keep = segCells;
        if (!pack_in_order(d, rp, keep, segLb, segRb, ov)) return false;
    }

    // 3. 評估 HPWL 改善
    auto nets = collectNets(inst2nets, segCells);
    int64_t delta = evalDeltaOverrides(d, nets, ov);
    if (delta < 0) {
        for (auto& p : ov) d.insts[p.first].x = p.second;
        return true;
    }
    return false;
}

// ========== 相鄰交換 (local reordering) ==========
static bool single_pass_adjacent_swap(Design& d,
                                      const Row& r,
                                      std::vector<int> segCells,
                                      const std::vector<std::vector<int>>& inst2nets,
                                      int segLb, int segRb) {
    if (segCells.size() < 2) return false;
    const Row* rp = &r;
    bool improved = false;
    auto nets = collectNets(inst2nets, segCells);

    for (int k = 0; k + 1 < (int)segCells.size(); ++k) {
        std::swap(segCells[k], segCells[k+1]);
        std::vector<std::pair<int,int>> ov;
        if (!pack_in_order(d, rp, segCells, segLb, segRb, ov)) {
            std::swap(segCells[k], segCells[k+1]);
            continue;
        }
        int64_t delta = evalDeltaOverrides(d, nets, ov);
        if (delta < 0) {
            for (auto& p : ov) d.insts[p.first].x = p.second;
            improved = true;
        } else {
            std::swap(segCells[k], segCells[k+1]);
        }
    }
    return improved;
}
// ========== Global Swap（跨 row cell 交換） ==========
static bool perform_global_swap(Design& d,
                                const std::vector<std::vector<int>>& inst2nets,
                                int sampleN = 2000) {
    int n = (int)d.insts.size();
    if (n < 2) return false;
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, n - 1);
    bool improved = false;

    for (int t = 0; t < sampleN; ++t) {
        int i = dist(rng), j = dist(rng);
        if (i == j) continue;
        if (d.insts[i].fixed || d.insts[j].fixed) continue;
        if (d.insts[i].y == d.insts[j].y) continue; // same row → skip

        int xi = d.insts[i].x, yi = d.insts[i].y;
        int xj = d.insts[j].x, yj = d.insts[j].y;
        const Row* ri = findRowForY(d, yj);
        const Row* rj = findRowForY(d, yi);
        if (!ri || !rj) continue;

        std::unordered_set<int> nets = collectNets(inst2nets, {i, j});
        std::vector<std::pair<int,int>> ov = {
            {i, snap_to_site(ri, xj, instWidth(d, i))},
            {j, snap_to_site(rj, xi, instWidth(d, j))}
        };
        int64_t delta = evalDeltaOverrides(d, nets, ov);
        if (delta < 0) {
            d.insts[i].x = ov[0].second; d.insts[i].y = yj;
            d.insts[j].x = ov[1].second; d.insts[j].y = yi;
            improved = true;
        }
    }
    return improved;
}

// ========== Vertical Swap（上下 row 移動） ==========
static bool perform_vertical_swap(Design& d,
                                  const std::vector<std::vector<int>>& inst2nets) {
    bool improved = false;
    for (int id = 0; id < (int)d.insts.size(); ++id) {
        if (d.insts[id].fixed) continue;
        int curY = d.insts[id].y;
        const Row* curRow = findRowForY(d, curY);
        if (!curRow) continue;
        int rowH = (curRow->site.height > 0)? curRow->site.height : 1;

        int bestY = curY;
        int64_t bestDelta = 0;
        for (int dy : {-1, +1}) {
            int newY = curY + dy * rowH;
            const Row* target = findRowForY(d, newY);
            if (!target) continue;
            std::unordered_set<int> nets = collectNets(inst2nets, {id});
            std::vector<std::pair<int,int>> ov = {
                {id, snap_to_site(target, d.insts[id].x, instWidth(d, id))}
            };
            int64_t delta = evalDeltaOverrides(d, nets, ov);
            if (delta < bestDelta) { bestDelta = delta; bestY = newY; }
        }
        if (bestY != curY) {
            d.insts[id].y = bestY;
            improved = true;
        }
    }
    return improved;
}

// ========== 主流程：FastDP 完整迴圈 ==========
void run_hpwl_optimization(Design& d) {
    using clk = std::chrono::steady_clock;
    auto t0 = clk::now();
    auto sec = [&](){ return std::chrono::duration<double>(clk::now() - t0).count(); };
    const double TIME_CAP = 300.0; // 5 分鐘上限
    const double EPS = 0.001; // 0.1 % 收斂門檻

    final_legalize_rows(d);
    auto inst2nets = buildInstToNets(d);
    int64_t hpPrev = totalHPWL(d);
    std::cout << "[FastDP] Start HPWL = " << hpPrev << "\n";

    int iter = 0;
    while (true) {
        ++iter;
        bool any = false;

        // (1) Per-row Segment Optimization
        for (const auto& r : d.layout.rows) {
            std::vector<int> all;
            for (int i = 0; i < (int)d.insts.size(); ++i)
                if (d.insts[i].y == r.originY) all.push_back(i);
            if (all.empty()) continue;
            std::sort(all.begin(), all.end(),
                      [&](int a,int b){return d.insts[a].x < d.insts[b].x;});
            int rowL = r.originX;
            int rowR = r.originX + r.xStep * r.numSites;

            for (int s = 0; s < (int)all.size();) {
                while (s < (int)all.size() && d.insts[all[s]].fixed) ++s;
                if (s >= (int)all.size()) break;
                int e = s;
                while (e < (int)all.size() && !d.insts[all[e]].fixed) ++e;

                std::vector<int> segCells;
                for (int k = s; k < e; ++k) segCells.push_back(all[k]);
                if (segCells.size() >= 2) {
                    int segLb = rowL;
                    if (s - 1 >= 0)
                        segLb = std::max(segLb, d.insts[all[s-1]].x + instWidth(d, all[s-1]));
                    int segRb = rowR;
                    if (e < (int)all.size())
                        segRb = std::min(segRb, d.insts[all[e]].x);
                    std::sort(segCells.begin(), segCells.end(),
                              [&](int a,int b){return d.insts[a].x < d.insts[b].x;});
                    if (target_order_pack_segment(d, r, segCells, inst2nets, segLb, segRb)) any = true;
                    std::sort(segCells.begin(), segCells.end(),
                              [&](int a,int b){return d.insts[a].x < d.insts[b].x;});
                    if (single_pass_adjacent_swap(d, r, segCells, inst2nets, segLb, segRb)) any = true;
                }
                s = e;
            }
        }

        // (2) Global Swap + Vertical Swap
        if (perform_global_swap(d, inst2nets)) any = true;
        if (perform_vertical_swap(d, inst2nets)) any = true;

        final_legalize_rows(d);

        int64_t hpNow = totalHPWL(d);
        double impr = double(hpPrev - hpNow) / (double)hpPrev;
        std::cout << "[Iter " << iter << "] HPWL = " << hpNow
                  << " (Δ=" << (hpNow - hpPrev) << ", impr=" << impr*100 << "%)\n";
        if (impr < EPS || !any || sec() > TIME_CAP) break;
        hpPrev = hpNow;
    }

    std::cout << "[FastDP] Done. Final HPWL = " << hpPrev << "\n";
}
#include "detailPlacement.hpp"
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <unordered_set>
#include <numeric>   // ✅ for std::iota
static inline int ceil_div(int a, int b) { return (a + b - 1) / b; }
static const long long INFLL = (1LL << 60);

// ================================================================
// HPWL (for selected nets) helper
// ================================================================
static long long hpwl_on_nets(const Design& d, const std::vector<int>& nets) {
    long long sum = 0;
    for (int nid : nets) {
        const Net& net = d.nets[nid];
        if (net.isSpecial || net.pins.empty()) continue;
        int xmin =  std::numeric_limits<int>::max();
        int xmax = -std::numeric_limits<int>::max();
        int ymin =  std::numeric_limits<int>::max();
        int ymax = -std::numeric_limits<int>::max();
        for (const auto& p : net.pins) {
            int px, py;
            if (p.isIO || p.instId < 0) {
                px = p.ioLoc.x; py = p.ioLoc.y;
            } else {
                const Inst& I = d.insts[p.instId];
                px = I.x; py = I.y;
            }
            if (px < xmin) xmin = px;
            if (px > xmax) xmax = px;
            if (py < ymin) ymin = py;
            if (py > ymax) ymax = py;
        }
        sum += (xmax - xmin) + (ymax - ymin);
    }
    return sum;
}

// ================================================================
// Compute total HPWL (exclude SPECIALNETS)
// ================================================================
long long computeTotalHPWL(const Design& d) {
    std::vector<int> nets(d.nets.size());
    std::iota(nets.begin(), nets.end(), 0);
    return hpwl_on_nets(d, nets);
}

// ================================================================
// Local window optimization in one row
// ================================================================
static long long optimize_row_window(Design& d, int ri,
                                     const std::vector<std::vector<int>>& inst2nets,
                                     int winCells);

// ================================================================
// Apply row y/orient
// ================================================================
static void apply_row_orient_and_y(Inst& I, const Row& r) {
    I.y = r.originY;
    I.orient = r.flip ? Orient::FS : Orient::N;
    I.placed = true;
}

// ===== Sliding-window reorder helpers (keep x-slots, search all perms) =====
static inline int cell_width_dbu(const Design& d, int iid, int step) {
    if (iid < 0 || iid >= (int)d.insts.size()) return step;
    int mid = d.insts[iid].macroId;
    if (mid >= 0 && mid < (int)d.macros.size()) return d.macros[mid].width;
    return step; // fallback: one site
}

static inline long long eval_delta_overrides_hpwl(Design& d,
                                                  const std::unordered_set<int>& nets,
                                                  const std::vector<std::pair<int,int>>& ov) {
    // backup
    std::vector<std::pair<int,int>> bak; bak.reserve(ov.size());
    for (auto &p : ov) bak.emplace_back(p.first, d.insts[p.first].x);
    long long oldH=0; for (int nid : nets) oldH += d.netHPWL(nid);
    for (auto &p : ov) d.insts[p.first].x = p.second;
    long long newH=0; for (int nid : nets) newH += d.netHPWL(nid);
    for (auto &p : bak) d.insts[p.first].x = p.second;
    return newH - oldH;
}

static bool reorder_window_keep_slots(Design& d,
                                      const Row& row,
                                      const std::vector<int>& cells,
                                      int l, int r,
                                      const std::vector<std::vector<int>>& inst2nets) {
    if (l >= r) return false;
    const int step = row.xStep > 0 ? row.xStep : 1;
    // collect ids and sorted slot x positions in current order
    std::vector<int> ids; ids.reserve(r-l+1);
    for (int i=l;i<=r;++i) ids.push_back(cells[i]);
    std::vector<int> xs; xs.reserve(ids.size());
    for (int id : ids) xs.push_back(d.insts[id].x);
    std::sort(xs.begin(), xs.end());

    // neighbor bounds
    int rowL = row.originX;
    int rowR = row.originX + row.xStep * row.numSites;
    int Lb = rowL;
    if (l > 0) { int ln = cells[l-1]; Lb = std::max(Lb, d.insts[ln].x + cell_width_dbu(d, ln, step)); }
    int Rb = rowR;
    if (r+1 < (int)cells.size()) { int rn = cells[r+1]; Rb = std::min(Rb, d.insts[rn].x); }

    auto legalSlots = [&](const std::vector<int>& ord){
        for (size_t k=0;k<ord.size();++k) {
            int w = cell_width_dbu(d, ord[k], step);
            int xk = xs[k];
            if (k==0 && xk < Lb) return false;
            if (k+1 < ord.size()) {
                if (xk + w > xs[k+1]) return false;
            } else {
                if (xk + w > Rb) return false;
            }
        }
        return true;
    };

    // collect union nets for ids
    std::unordered_set<int> nets; nets.reserve(ids.size()*3);
    for (int id : ids) for (int nid : inst2nets[id]) nets.insert(nid);

    // try all permutations (k <= 6 practical)
    std::vector<int> perm = ids, best = ids;
    long long bestDelta = 0;
    std::sort(perm.begin(), perm.end());
    do {
        if (!legalSlots(perm)) continue;
        std::vector<std::pair<int,int>> ov; ov.reserve(perm.size());
        for (size_t k=0;k<perm.size();++k) ov.emplace_back(perm[k], xs[k]);
        long long dlt = eval_delta_overrides_hpwl(d, nets, ov);
        if (dlt < bestDelta) { bestDelta = dlt; best = perm; }
    } while (std::next_permutation(perm.begin(), perm.end()));

    if (bestDelta < 0) {
        for (size_t k=0;k<best.size();++k) d.insts[best[k]].x = xs[k];
        return true;
    }
    return false;
}

static bool reorder_small_windows_for_row(Design& d,
                                          const Row& row,
                                          const std::vector<std::vector<int>>& inst2nets) {
    // collect movable cells on this row
    std::vector<int> cells;
    for (int iid=0;iid<(int)d.insts.size();++iid){
        const Inst& I = d.insts[iid];
        if (I.fixed || !I.placed) continue;
        if (I.y != row.originY) continue;
        cells.push_back(iid);
    }
    if ((int)cells.size() < 3) return false;
    std::sort(cells.begin(), cells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });

    bool improved = false;
    auto sweep = [&](int W){
        bool any=false;
        if ((int)cells.size() >= W){
            for (int i=0;i+W-1<(int)cells.size(); ++i){
                // Keep indices consistent by re-sorting after each change
                any |= reorder_window_keep_slots(d, row, cells, i, i+W-1, inst2nets);
                if (any) std::sort(cells.begin(), cells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
            }
            for (int i=(int)cells.size()-W; i>=0; --i){
                any |= reorder_window_keep_slots(d, row, cells, i, i+W-1, inst2nets);
                if (any) std::sort(cells.begin(), cells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
            }
        }
        return any;
    };
    // try W=5,4,3
    improved |= sweep(5);
    improved |= sweep(4);
    improved |= sweep(3);
    return improved;
}

static long long optimize_row_window(Design& d, int ri,
                                     const std::vector<std::vector<int>>& inst2nets,
                                     int winCells)
{
    const Row& row = d.layout.rows[ri];
    bool any = reorder_small_windows_for_row(d, row, inst2nets);
    return any ? 1 : 0;
}

// ================================================================
// Build inst→nets adjacency
// ================================================================
static std::vector<std::vector<int>> buildInstToNets(const Design& d) {
    std::vector<std::vector<int>> inst2nets(d.insts.size());
    for (int nid = 0; nid < (int)d.nets.size(); ++nid) {
        const Net& net = d.nets[nid];
        if (net.isSpecial) continue;
        for (const auto& p : net.pins)
            if (!p.isIO && p.instId >= 0)
                inst2nets[p.instId].push_back(nid);
    }
    return inst2nets;
}

// ================================================================
// Main detailPlacement
// ================================================================
void detailPlacement(Design& d, const DetailPlaceConfig& cfg) {
    if (cfg.verbose)
        std::cout << "==== Begin detailPlacement ====\n";

    auto inst2nets = buildInstToNets(d);

    long long totalGain = 0;
    for (int pass = 1; pass <= cfg.passes; ++pass) {
        long long gainThisPass = 0;
        for (int ri = 0; ri < (int)d.layout.rows.size(); ++ri) {
            gainThisPass += optimize_row_window(d, ri, inst2nets, 5);
        }
        totalGain += gainThisPass;
        if (cfg.verbose)
            std::cout << "[Pass " << pass << "] HPWL gain = " << gainThisPass << "\n";
    }

    if (cfg.verbose)
        std::cout << "==== End detailPlacement (total gain=" << totalGain << ") ====\n";
}
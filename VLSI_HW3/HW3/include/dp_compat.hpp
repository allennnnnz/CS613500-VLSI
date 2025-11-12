#pragma once
#include "design.hpp"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <limits>
#include <cassert>
#include <cstdint>

namespace coloquinte {
namespace dp {

using index_t = int;
using int_t   = int;
using netlist = Design;

constexpr index_t null_ind = std::numeric_limits<index_t>::max();

static inline bool isR90Family(Orient o) {
    return o==Orient::E || o==Orient::W || o==Orient::FE || o==Orient::FW;
}
static inline int orientedW(const Macro& m, Orient o) {
    return isR90Family(o) ? m.height : m.width;
}
static inline int orientedH(const Macro& m, Orient o) {
    return isR90Family(o) ? m.width  : m.height;
}

struct detailed_placement {
    struct placement_t_compat {};
    placement_t_compat plt_;

    std::vector<index_t> cell_rows_;

    int_t min_x_{0}, max_x_{0};
    int_t y_origin_{0};
    int_t row_height_{0};

    std::vector<std::pair<index_t,index_t>> neighbours_;
    std::vector<index_t> neighbours_limits_;
    std::vector<index_t> row_first_cells_, row_last_cells_;

    Design& des_;
    std::vector<int> instRow_;
    std::vector<std::vector<int>> rows_;

    explicit detailed_placement(Design& d) : des_(d) {
        min_x_     = des_.layout.dieArea.x1;
        max_x_     = des_.layout.dieArea.x2;
        y_origin_  = des_.layout.rows.empty()? 0 : des_.layout.rows.front().originY;
        row_height_= 0;
        if (!des_.layout.rows.empty()) {
            auto itS = des_.siteName2Id.find(des_.layout.rows.front().siteName);
            if (itS != des_.siteName2Id.end()) row_height_ = des_.sites[itS->second].sizeY;
        }

        buildRowIndex_();
        buildRowCells_();

        row_first_cells_.assign((int)rows_.size(), null_ind);
        row_last_cells_ .assign((int)rows_.size(), null_ind);
        for (int r=0; r<(int)rows_.size(); ++r) {
            if (!rows_[r].empty()) {
                row_first_cells_[r] = rows_[r].front();
                row_last_cells_[r]  = rows_[r].back();
            }
        }

        neighbours_limits_.resize(des_.insts.size()+1);
        for (int i=0;i<(int)des_.insts.size();++i) neighbours_limits_[i] = i;
        neighbours_limits_.back() = (int)des_.insts.size();
        neighbours_.assign(des_.insts.size(), {null_ind, null_ind});

        selfcheck();
    }

    index_t cell_height(index_t /*c*/) const { return 1; }
    index_t cell_cnt()  const { return (index_t)des_.insts.size(); }
    index_t row_cnt()   const { return (index_t)rows_.size(); }
    index_t neighbour_index(index_t c, index_t /*r*/) const { return c; }

    void selfcheck() const {
        assert(row_first_cells_.size() == row_last_cells_.size());
        for (int r=0;r<(int)rows_.size();++r) {
            const auto& v = rows_[r];
            for (int i=1;i<(int)v.size();++i) {
                assert(des_.insts[v[i-1]].x <= des_.insts[v[i]].x);
            }
        }
    }

    void swap_standard_cell_topologies(index_t c1, index_t c2) {
        assert(isStdCell_(c1) && isStdCell_(c2));
        int r1 = instRow_[c1];
        int r2 = instRow_[c2];
        assert(r1 >= 0 && r2 >= 0);

        sortRowByX_(r1);
        if (r2 != r1) sortRowByX_(r2);

        auto& v1 = rows_[r1];
        auto& v2 = rows_[r2];
        auto it1 = std::find(v1.begin(), v1.end(), c1);
        auto it2 = std::find(v2.begin(), v2.end(), c2);
        assert(it1 != v1.end() && it2 != v2.end());

        if (r1 == r2) {
            std::iter_swap(it1, it2);
        } else {
            int pos1 = (int)std::distance(v1.begin(), it1);
            int pos2 = (int)std::distance(v2.begin(), it2);
            v1[pos1] = c2;
            v2[pos2] = c1;
            des_.insts[c1].y = des_.layout.rows[r2].originY;
            des_.insts[c2].y = des_.layout.rows[r1].originY;
            instRow_[c1] = r2;
            instRow_[c2] = r1;
        }

        for (int r : {r1, r2}) {
            if (r >= 0) {
                sortRowByX_(r);
                row_first_cells_[r] = rows_[r].empty()? null_ind : rows_[r].front();
                row_last_cells_[r]  = rows_[r].empty()? null_ind : rows_[r].back();
            }
        }
        selfcheck();
    }

    std::pair<int_t,int_t> get_limit_positions(netlist const& /*circuit*/, index_t c) const {
        int r = instRow_[c];
        if (r < 0) return {des_.layout.dieArea.x1, des_.layout.dieArea.x1};

        const Row& row = des_.layout.rows[r];
        const Inst& it = des_.insts[c];
        const Macro& mc = des_.macros[it.macroId];
        int W  = orientedW(mc, it.orient);

        int rowL = row.originX;
        int rowR = row.originX + row.xStep * row.numSites;
        int L = rowL, R = rowR - W;
        if (R < L) R = L;

        int prev = get_prev_cell_on_row(c, r);
        int next = get_next_cell_on_row(c, r);

        if (prev != null_ind) {
            const Inst& p  = des_.insts[prev];
            const Macro& mp = des_.macros[p.macroId];
            L = std::max(L, p.x + orientedW(mp, p.orient));
        }
        if (next != null_ind) {
            const Inst& n  = des_.insts[next];
            R = std::min(R, n.x);
        }
        if (R < L) R = L;
        return {L, R};
    }

    index_t get_first_cell_on_row(index_t r) const {
        if (r < 0 || r >= (index_t)rows_.size()) return null_ind;
        return rows_[r].empty() ? null_ind : rows_[r].front();
    }
    index_t get_next_cell_on_row(index_t c, index_t r) const {
        if (!inRow_(c, r)) return null_ind;
        const auto& v = rows_[r];
        auto it = std::find(v.begin(), v.end(), c);
        if (it == v.end()) return null_ind;
        ++it;
        return (it == v.end()) ? null_ind : *it;
    }
    index_t get_prev_cell_on_row(index_t c, index_t r) const {
        if (!inRow_(c, r)) return null_ind;
        const auto& v = rows_[r];
        auto it = std::find(v.begin(), v.end(), c);
        if (it == v.end() || it == v.begin()) return null_ind;
        --it;
        return *it;
    }

    index_t get_first_standard_cell_on_row(index_t r) { return get_first_cell_on_row(r); }
    index_t get_next_standard_cell_on_row(index_t c, index_t r) { return get_next_cell_on_row(c, r); }

    void reorder_standard_cells(std::vector<index_t> const old_order,
                                std::vector<index_t> const new_order) {
        assert(old_order.size() == new_order.size());
        assert(!old_order.empty());
        int r = instRow_[old_order.front()];
        for (auto id : old_order)  { assert(instRow_[id] == r && isStdCell_(id)); }
        for (auto id : new_order) { assert(instRow_[id] == r && isStdCell_(id)); }

        auto& v = rows_[r];
        sortRowByX_(r);

        std::vector<int> idxs; idxs.reserve(old_order.size());
        for (auto id : old_order) {
            auto it = std::find(v.begin(), v.end(), id);
            assert(it != v.end());
            idxs.push_back((int)std::distance(v.begin(), it));
        }
        std::sort(idxs.begin(), idxs.end());
        for (int i=1;i<(int)idxs.size();++i) assert(idxs[i] == idxs[i-1]+1);
        int first = idxs.front();

        for (int i=0;i<(int)new_order.size();++i) v[first+i] = new_order[i];

        row_first_cells_[r] = v.empty()? null_ind : v.front();
        row_last_cells_[r]  = v.empty()? null_ind : v.back();
        selfcheck();
    }

    void reorder_cells(std::vector<index_t> const old_order,
                       std::vector<index_t> const new_order,
                       index_t r) {
        assert(r >= 0 && r < (index_t)rows_.size());
        assert(old_order.size() == new_order.size());
        assert(!old_order.empty());
        for (auto id : old_order)  { assert(instRow_[id] == (int)r && isStdCell_(id)); }
        for (auto id : new_order) { assert(instRow_[id] == (int)r && isStdCell_(id)); }

        auto& v = rows_[(int)r];
        sortRowByX_((int)r);

        std::vector<int> idxs; idxs.reserve(old_order.size());
        for (auto id : old_order) {
            auto it = std::find(v.begin(), v.end(), id);
            assert(it != v.end());
            idxs.push_back((int)std::distance(v.begin(), it));
        }
        std::sort(idxs.begin(), idxs.end());
        for (int i=1;i<(int)idxs.size();++i) assert(idxs[i] == idxs[i-1]+1);
        int first = idxs.front();

        for (int i=0;i<(int)new_order.size();++i) v[first+i] = new_order[i];

        row_first_cells_[(int)r] = v.empty()? null_ind : v.front();
        row_last_cells_ [(int)r] = v.empty()? null_ind : v.back();
        selfcheck();
    }

    // 成員版：列方向相容（偶數列 N/FN、奇數列 S/FS；firstRowN 控制第 0 列）
    void row_compatible_orientation(bool firstRowN) {
        for (int c=0; c<(int)des_.insts.size(); ++c) {
            if (!isStdCell_(c)) continue;
            int r = instRow_[c];
            if (r < 0) continue;
            bool wantFlip = (r % 2 != 0) ^ (!firstRowN);
            Orient& o = des_.insts[c].orient;
            if (wantFlip) { // 目標 S/FS
                if (o==Orient::N)  o = Orient::S;
                if (o==Orient::FN) o = Orient::FS;
            } else {        // 目標 N/FN
                if (o==Orient::S)  o = Orient::N;
                if (o==Orient::FS) o = Orient::FN;
            }
        }
    }

private:
    void buildRowIndex_() {
        instRow_.assign(des_.insts.size(), -1);
        std::unordered_map<int,int> y2r;
        y2r.reserve(des_.layout.rows.size()*2);
        for (int r=0;r<(int)des_.layout.rows.size();++r) {
            y2r[des_.layout.rows[r].originY] = r;
        }
        for (int i=0;i<(int)des_.insts.size();++i) {
            auto it = y2r.find(des_.insts[i].y);
            if (it != y2r.end()) instRow_[i] = it->second;
        }
        cell_rows_ = instRow_;
    }

    void buildRowCells_() {
        rows_.assign(des_.layout.rows.size(), {});
        for (int i=0;i<(int)des_.insts.size();++i) {
            const auto& inst = des_.insts[i];
            if (inst.fixed) continue;
            int r = instRow_[i];
            if (r < 0) continue;

            const Macro& mc = des_.macros[inst.macroId];
            const Row& row  = des_.layout.rows[r];

            auto itSite = des_.siteName2Id.find(row.siteName);
            if (itSite == des_.siteName2Id.end()) continue;
            const Site& site = des_.sites[itSite->second];

            int H = orientedH(mc, inst.orient);
            if (H != site.sizeY) continue; // 單高度限定

            rows_[r].push_back(i);
        }
        for (int r=0;r<(int)rows_.size();++r) sortRowByX_(r);
    }

    bool inRow_(int instId, int r) const {
        if (r < 0 || r >= (int)rows_.size()) return false;
        const auto& v = rows_[r];
        return std::find(v.begin(), v.end(), instId) != v.end();
    }

    bool isStdCell_(int instId) const {
        int r = instRow_[instId];
        if (r < 0) return false;
        const auto& inst = des_.insts[instId];
        if (inst.fixed) return false;

        const auto& row  = des_.layout.rows[r];
        auto itSite = des_.siteName2Id.find(row.siteName);
        if (itSite == des_.siteName2Id.end()) return false;
        const Site& site = des_.sites[itSite->second];

        const Macro& mc = des_.macros[inst.macroId];
        return orientedH(mc, inst.orient) == site.sizeY;
    }

    void sortRowByX_(int r) {
        if (r < 0 || r >= (int)rows_.size()) return;
        auto& v = rows_[r];
        std::sort(v.begin(), v.end(), [&](int a, int b){
            return des_.insts[a].x < des_.insts[b].x;
        });
    }
};

// ---- 自由函式：與原 API 相容（內部呼叫成員版） ----
inline void row_compatible_orientation(netlist const& /*circuit*/, detailed_placement& pl, bool first_row_orient){
    pl.row_compatible_orientation(first_row_orient);
}

// 其餘 swap/OSRP/row swap 等策略可在此逐步實作；先給空殼以便編譯
inline void swaps_global_HPWL(netlist const&, detailed_placement&, index_t, index_t, bool) {}
inline void swaps_global_RSMT(netlist const&, detailed_placement&, index_t, index_t, bool) {}
inline void swaps_row_convex_HPWL(netlist const&, detailed_placement&, index_t) {}
inline void swaps_row_convex_RSMT(netlist const&, detailed_placement&, index_t) {}
inline void swaps_row_noncvx_HPWL(netlist const&, detailed_placement&, index_t) {}
inline void swaps_row_noncvx_RSMT(netlist const&, detailed_placement&, index_t) {}
inline void OSRP_convex_HPWL(netlist const&, detailed_placement&) {}
inline void OSRP_convex_RSMT(netlist const&, detailed_placement&) {}
inline void OSRP_noncvx_HPWL(netlist const&, detailed_placement&) {}
inline void OSRP_noncvx_RSMT(netlist const&, detailed_placement&) {}

} // namespace dp
} // namespace coloquinte
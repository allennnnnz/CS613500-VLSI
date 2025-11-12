#include "dp_stage.hpp"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <numeric>
#include <unordered_set>
#include <random>
#include <chrono>

// forward decl
static inline int64_t netHPWL(const Design& d, int nid);

// --- Helpers ---
static inline int instWidth(const Design& d, int id) {
    if (id < 0 || id >= (int)d.insts.size()) {
        return d.layout.rows.empty() ? 1 : std::max(1, d.layout.rows.front().xStep);
    }
    int mid = d.insts[id].macroId;
    if (mid >= 0 && mid < (int)d.macros.size()) {
        return d.macros[mid].width;
    }
    return d.layout.rows.empty() ? 1 : std::max(1, d.layout.rows.front().xStep);
}

static inline const Row* findRowForY(const Design& d, int y) {
    for (const auto& r : d.layout.rows) if (r.originY == y) return &r;
    return nullptr;
}

static inline int snap_to_site(const Row* r, int x, int width) {
    if (!r) return x;
    int dx = x - r->originX;
    int step = r->xStep > 0 ? r->xStep : 1;
    int s = (dx>=0)? (dx + step/2)/step : (dx - step/2)/step;
    int xs = r->originX + s*step;
    int L = r->originX;
    int R = r->originX + r->xStep*r->numSites - width;
    if (R < L) R = L;
    if (xs < L) xs = L; if (xs > R) xs = R;
    return xs;
}

static inline std::unordered_set<int> collectNets(const std::vector<std::vector<int>>& inst2nets,
                                                  const std::vector<int>& ids) {
    std::unordered_set<int> s; s.reserve(ids.size()*3);
    for (int id : ids) for (int nid : inst2nets[id]) s.insert(nid);
    return s;
}

static inline int64_t netsHPWL(const Design& d, const std::unordered_set<int>& nets) {
    int64_t sum=0; for (int nid : nets) sum += netHPWL(d, nid); return sum;
}

static inline int64_t evalDeltaOverrides(Design& d,
                                         const std::unordered_set<int>& nets,
                                         const std::vector<std::pair<int,int>>& overrides) {
    // old HPWL
    int64_t oldH = netsHPWL(d, nets);
    // apply
    std::vector<std::pair<int,int>> backup; backup.reserve(overrides.size());
    for (auto& p : overrides) { backup.emplace_back(p.first, d.insts[p.first].x); d.insts[p.first].x = p.second; }
    int64_t newH = netsHPWL(d, nets);
    // restore
    for (auto& q : backup) d.insts[q.first].x = q.second;
    return newH - oldH;
}

static bool reorder_window_keep_positions(Design& d,
                                          const std::vector<int>& rowCells,
                                          int l, int r,
                                          const std::vector<std::vector<int>>& inst2nets,
                                          int segLb, int segRb) {
    if (l>=r) return false;
    std::vector<int> ids; ids.reserve(r-l+1);
    for (int i=l;i<=r;++i) ids.push_back(rowCells[i]);
    auto nets = collectNets(inst2nets, ids);
    // capture current x-slots (sorted)
    std::vector<int> xs; xs.reserve(ids.size());
    for (int id : ids) xs.push_back(d.insts[id].x);
    std::sort(xs.begin(), xs.end());

    // generate candidates
    std::vector<int> perm = ids, best = ids;
    int64_t bestDelta = 0;
    // neighbor bounds for legality
    int y = d.insts[rowCells[l]].y; const Row* rp = findRowForY(d, y);
    int Lb = segLb; if (l>0) { int ln=rowCells[l-1]; Lb = std::max(Lb, d.insts[ln].x + instWidth(d, ln)); }
    int Rb = segRb; if (r+1<(int)rowCells.size()) { int rn=rowCells[r+1]; Rb = std::min(Rb, d.insts[rn].x); }

    auto legalSlots = [&](const std::vector<int>& cand){
        if (xs.empty()) return false;
        if (xs[0] < Lb) return false;
        for (size_t k=0;k<cand.size();++k) {
            int w = instWidth(d, cand[k]);
            if (k+1 < cand.size()) {
                if (xs[k] + w > xs[k+1]) return false;
            } else {
                if (xs[k] + w > Rb) return false;
            }
        }
        return true;
    };

    auto tryPerm = [&](const std::vector<int>& cand){
        if (!legalSlots(cand)) return (int64_t)1e15;
        std::vector<std::pair<int,int>> ov; ov.reserve(cand.size());
        for (size_t k=0;k<cand.size();++k) ov.emplace_back(cand[k], xs[k]);
        return evalDeltaOverrides(d, nets, ov);
    };

    if ((int)ids.size() <= 7) {
        std::sort(perm.begin(), perm.end()); // assume ids distinct
        do {
            int64_t dlt = tryPerm(perm);
            if (dlt < bestDelta) { bestDelta = dlt; best = perm; }
        } while (std::next_permutation(perm.begin(), perm.end()));
    } else {
        // heuristic: identity, reverse, random samples
        int64_t dId = tryPerm(perm); if (dId < bestDelta) { bestDelta = dId; best = perm; }
        std::vector<int> rev = perm; std::reverse(rev.begin(), rev.end());
        int64_t dRv = tryPerm(rev); if (dRv < bestDelta) { bestDelta = dRv; best = rev; }
        std::mt19937 rng{std::random_device{}()};
        for (int t=0;t<40;++t) { std::shuffle(perm.begin(), perm.end(), rng); int64_t dlt = tryPerm(perm); if (dlt < bestDelta) { bestDelta=dlt; best=perm; } }
    }

    if (bestDelta < 0) {
        std::vector<std::pair<int,int>> ov; ov.reserve(best.size());
        for (size_t k=0;k<best.size();++k) ov.emplace_back(best[k], xs[k]);
        // apply
        for (auto& p: ov) d.insts[p.first].x = p.second;
        return true;
    }
    return false;
}

static bool compact_window_pack(Design& d,
                                const std::vector<int>& rowCells,
                                int l, int r,
                                const std::vector<std::vector<int>>& inst2nets,
                                bool toLeft,
                                int segLb, int segRb) {
    if (l>=r) return false;
    int y = d.insts[rowCells[l]].y; for (int i=l+1;i<=r;++i) if (d.insts[rowCells[i]].y != y) return false;
    const Row* rp = findRowForY(d, y);
    // neighbor bounds
    int Lb = segLb;
    if (l>0) { int ln = rowCells[l-1]; Lb = std::max(Lb, d.insts[ln].x + instWidth(d, ln)); }
    int Rb = segRb;
    if (r+1<(int)rowCells.size()) { int rn = rowCells[r+1]; Rb = std::min(Rb, d.insts[rn].x); }
    // total width
    int totalW=0; for (int i=l;i<=r;++i) totalW += instWidth(d, rowCells[i]);
    if (Rb - Lb < totalW) return false;
    std::vector<std::pair<int,int>> ov; ov.reserve(r-l+1);
    if (toLeft) {
        int cur = snap_to_site(rp, Lb, instWidth(d, rowCells[l]));
        for (int i=l;i<=r;++i) { int id=rowCells[i]; int w=instWidth(d,id); if (cur > Rb - w) return false; ov.emplace_back(id, cur); cur += w; }
    } else {
        int start = std::max(Lb, Rb - totalW);
        int cur = snap_to_site(rp, start, instWidth(d, rowCells[l]));
        for (int i=l;i<=r;++i) { int id=rowCells[i]; int w=instWidth(d,id); if (cur > Rb - w) return false; ov.emplace_back(id, cur); cur += w; }
    }
    auto nets = collectNets(inst2nets, std::vector<int>(rowCells.begin()+l, rowCells.begin()+r+1));
    int64_t dlt = evalDeltaOverrides(d, nets, ov);
    if (dlt < 0) { for (auto& p: ov) d.insts[p.first].x = p.second; return true; }
    return false;
}

static inline int median_of(std::vector<int>& a) { size_t m=a.size()/2; std::nth_element(a.begin(), a.begin()+m, a.end()); return a[m]; }

static int compute_target_x(const Design& d, int instId, const std::vector<std::vector<int>>& inst2nets) {
    // weighted average of connected pins' x (exclude self pins), emphasize 2-pin and IO nets
    double sumWX = 0.0, sumW = 0.0;
    for (int nid : inst2nets[instId]) {
        const Net& net = d.nets[nid];
        bool hasIO=false; for (const auto& p : net.pins) if (p.isIO) { hasIO=true; break; }
        double w = 1.0; if ((int)net.pins.size() == 2) w *= 1.6; if (hasIO) w *= 1.2; if (w > 3.0) w = 3.0;
        for (const auto& p : net.pins) {
            if (!p.isIO && p.instId == instId) continue; // exclude self
            // Avoid relying on LEF pin geometry; approximate by cell center for robustness
            int px;
            if (p.isIO) {
                px = p.ioLoc.x;
            } else if (p.instId >= 0 && p.instId < (int)d.insts.size()) {
                const Inst& I = d.insts[p.instId];
                int mw = 0;
                if (I.macroId >= 0 && I.macroId < (int)d.macros.size()) mw = d.macros[I.macroId].width;
                else mw = d.layout.rows.empty() ? 0 : d.layout.rows.front().xStep;
                px = I.x + mw/2;
            } else {
                px = 0;
            }
            sumWX += w * (double)px; sumW += w;
        }
    }
    if (sumW > 0) return (int)std::llround(sumWX / sumW);
    // fallback: center x
    const Inst& I = d.insts[instId]; const Macro& mc = d.macros[I.macroId];
    return I.x + mc.width/2;
}

static bool block_slide_L1_window(Design& d,
                                  const std::vector<int>& rowCells,
                                  int l, int r,
                                  const std::vector<std::vector<int>>& inst2nets,
                                  int segLb, int segRb) {
    if (l>=r) return false;
    int y = d.insts[rowCells[l]].y; for (int i=l+1;i<=r;++i) if (d.insts[rowCells[i]].y != y) return false;
    const Row* rp = findRowForY(d, y);
    // neighbor bounds
    int Lb = segLb; if (l>0) { int ln = rowCells[l-1]; Lb = std::max(Lb, d.insts[ln].x + instWidth(d, ln)); }
    int Rb = segRb; if (r+1<(int)rowCells.size()) { int rn=rowCells[r+1]; Rb = std::min(Rb, d.insts[rn].x); }
    int m = r-l+1;
    std::vector<int> ids; ids.reserve(m); for (int i=l;i<=r;++i) ids.push_back(rowCells[i]);
    std::vector<int> w(m), off(m); for (int i=0;i<m;++i){ w[i]=instWidth(d,ids[i]); off[i]=(i? off[i-1]+w[i-1] : 0);} int totalW = off.back() + w.back();
    if (Rb - Lb < totalW) return false;
    std::vector<int> v; v.reserve(m);
    for (int i=0;i<m;++i) { int t = compute_target_x(d, ids[i], inst2nets); v.push_back(t - off[i]); }
    int Sstar = median_of(v);
    int Smin = Lb, Smax = Rb - totalW;
    if (Smin > Smax) return false;
    int Ssnap = snap_to_site(rp, std::max(Smin, std::min(Smax, Sstar)), w[0]);
    std::vector<std::pair<int,int>> ov; ov.reserve(m);
    for (int i=0;i<m;++i) ov.emplace_back(ids[i], Ssnap + off[i]);
    auto nets = collectNets(inst2nets, ids);
    int64_t dlt = evalDeltaOverrides(d, nets, ov);
    if (dlt < 0) { for (auto& p: ov) d.insts[p.first].x = p.second; return true; }
    return false;
}

static bool reorder_with_pack_window(Design& d,
                                     const std::vector<int>& rowCells,
                                     int l, int r,
                                     const std::vector<std::vector<int>>& inst2nets,
                                     int segLb, int segRb) {
    if (l>=r) return false;
    int y = d.insts[rowCells[l]].y; for (int i=l+1;i<=r;++i) if (d.insts[rowCells[i]].y != y) return false;
    const Row* rp = findRowForY(d, y);
    int Lb = segLb; if (l>0) { int ln=rowCells[l-1]; Lb = std::max(Lb, d.insts[ln].x + instWidth(d, ln)); }
    int Rb = segRb; if (r+1<(int)rowCells.size()) { int rn=rowCells[r+1]; Rb = std::min(Rb, d.insts[rn].x); }
    int m=r-l+1; std::vector<int> ids; ids.reserve(m); for (int i=l;i<=r;++i) ids.push_back(rowCells[i]);
    int totalW=0; for (int id: ids) totalW += instWidth(d, id); if (Rb - Lb < totalW) return false;
    auto nets = collectNets(inst2nets, ids);

    // generate candidate orders
    std::vector<std::vector<int>> cands;
    cands.push_back(ids);
    {
        auto rev = ids; std::reverse(rev.begin(), rev.end()); cands.push_back(rev);
    }
    // sort by target x asc/desc
    {
        auto tmp = ids;
        std::sort(tmp.begin(), tmp.end(), [&](int a,int b){ return compute_target_x(d,a,inst2nets) < compute_target_x(d,b,inst2nets); });
        cands.push_back(tmp);
        std::reverse(tmp.begin(), tmp.end()); cands.push_back(tmp);
    }
    // adjacent pair swap pattern
    {
        auto tmp = ids;
        for (int k=0;k+1<m;k+=2) std::swap(tmp[k], tmp[k+1]);
        cands.push_back(tmp);
    }

    int64_t bestDelta = 0; std::vector<std::pair<int,int>> bestOV;
    auto evalPack = [&](const std::vector<int>& ord, bool toLeft){
        std::vector<std::pair<int,int>> ov; ov.reserve(m);
        if (toLeft) {
            int cur = snap_to_site(rp, Lb, instWidth(d, ord[0]));
            for (int k=0;k<m;++k) {
                int id=ord[k]; int w=instWidth(d,id); if (cur > Rb - w) return (int64_t)1e15; ov.emplace_back(id,cur); cur+=w;
            }
        } else {
            int start = std::max(Lb, Rb - totalW);
            int cur = snap_to_site(rp, start, instWidth(d, ord[0]));
            for (int k=0;k<m;++k) {
                int id=ord[k]; int w=instWidth(d,id); if (cur > Rb - w) return (int64_t)1e15; ov.emplace_back(id,cur); cur+=w;
            }
        }
        return evalDeltaOverrides(d, nets, ov);
    };

    for (auto& cand : cands) {
    int64_t dL = evalPack(cand, /*toLeft=*/true);
        if (dL < bestDelta) { bestDelta = dL; bestOV.clear(); int cur= snap_to_site(rp, Lb, instWidth(d, cand[0])); for (int k=0;k<m;++k){ int id=cand[k]; int w=instWidth(d,id); bestOV.emplace_back(id,cur); cur+=w; } }
    int64_t dR = evalPack(cand, /*toLeft=*/false);
        if (dR < bestDelta) { bestDelta = dR; bestOV.clear(); int start= std::max(Lb, Rb - totalW); int cur= snap_to_site(rp, start, instWidth(d, cand[0])); for (int k=0;k<m;++k){ int id=cand[k]; int w=instWidth(d,id); bestOV.emplace_back(id,cur); cur+=w; } }
    }

    if (bestDelta < 0 && !bestOV.empty()) { for (auto& p: bestOV) d.insts[p.first].x = p.second; return true; }
    return false;
}

static bool shift_single_cell_best(Design& d,
                                   const std::vector<int>& rowCells,
                                   int idx,
                                   const std::vector<std::vector<int>>& inst2nets,
                                   int maxSites,
                                   int segLb, int segRb) {
    int n = (int)rowCells.size(); if (idx<0 || idx>=n) return false;
    int id = rowCells[idx]; if (d.insts[id].fixed) return false;
    int y = d.insts[id].y; const Row* rp = findRowForY(d, y);
    if (!rp) return false;
    int rowL = rp->originX; int rowR = rp->originX + rp->xStep*rp->numSites;
    int w = instWidth(d, id);
    int Lb = segLb; if (idx>0) { int ln=rowCells[idx-1]; Lb = std::max(Lb, d.insts[ln].x + instWidth(d, ln)); }
    int Rb = segRb; if (idx+1<n) { int rn=rowCells[idx+1]; Rb = std::min(Rb, d.insts[rn].x); }
    if (Rb - Lb < w) return false;
    int cur = d.insts[id].x; int step = std::max(1, rp->xStep);
    auto nets = collectNets(inst2nets, std::vector<int>{id});
    int64_t bestDelta = 0; int bestX = cur;
    // search around current within ±maxSites
    int sCur = (cur - rp->originX)/step; int sMin = (Lb - rp->originX + step - 1)/step; int sMax = (Rb - w - rp->originX)/step;
    for (int ds = -maxSites; ds <= maxSites; ++ds) {
        int s = sCur + ds; if (s < sMin || s > sMax) continue;
        int x = rp->originX + s*step;
        std::vector<std::pair<int,int>> ov = { {id, x} };
        int64_t dlt = evalDeltaOverrides(d, nets, ov);
        if (dlt < bestDelta) { bestDelta = dlt; bestX = x; }
    }
    if (bestDelta < 0) { d.insts[id].x = bestX; return true; }
    return false;
}

static bool insert_within_window_pack(Design& d,
                                      const std::vector<int>& rowCells,
                                      int i, int j,
                                      const std::vector<std::vector<int>>& inst2nets,
                                      int segLb, int segRb) {
    int n=(int)rowCells.size(); if (i<0||j<0||i>=n||j>=n||i==j) return false; if (i>j) std::swap(i,j);
    int y = d.insts[rowCells[i]].y; for (int k=i+1;k<=j;++k) if (d.insts[rowCells[k]].y != y) return false;
    const Row* rp = findRowForY(d, y);
    int Lb = segLb; if (i>0) { int ln=rowCells[i-1]; Lb = std::max(Lb, d.insts[ln].x + instWidth(d, ln)); }
    int Rb = segRb; if (j+1<n) { int rn=rowCells[j+1]; Rb = std::min(Rb, d.insts[rn].x); }
    // build order: move rowCells[i] to after index j
    std::vector<int> ids; ids.reserve(j-i+1);
    for (int k=i+1;k<=j;++k) ids.push_back(rowCells[k]);
    ids.push_back(rowCells[i]);
    int totalW=0; for (int id: ids) totalW += instWidth(d,id); if (Rb - Lb < totalW) return false;
    auto nets = collectNets(inst2nets, ids);
    auto evalPack = [&](bool toLeft){
        std::vector<std::pair<int,int>> ov; ov.reserve(ids.size());
        if (toLeft) {
            int cur = snap_to_site(rp, Lb, instWidth(d, ids[0]));
            for (int t=0;t<(int)ids.size();++t) { int id=ids[t]; int w=instWidth(d,id); if (cur > Rb - w) return (int64_t)1e15; ov.emplace_back(id, cur); cur += w; }
        } else {
            int start = std::max(Lb, Rb - totalW);
            int cur = snap_to_site(rp, start, instWidth(d, ids[0]));
            for (int t=0;t<(int)ids.size();++t) { int id=ids[t]; int w=instWidth(d,id); if (cur > Rb - w) return (int64_t)1e15; ov.emplace_back(id, cur); cur += w; }
        }
        return evalDeltaOverrides(d, nets, ov);
    };
    int64_t dL = evalPack(true), dR = evalPack(false);
    if (dL >= 0 && dR >= 0) return false;
    // apply best
    std::vector<std::pair<int,int>> ov;
    if (dL <= dR) {
        int cur = snap_to_site(rp, Lb, instWidth(d, ids[0]));
        for (int t=0;t<(int)ids.size();++t) { int id=ids[t]; int w=instWidth(d,id); ov.emplace_back(id, cur); cur += w; }
    } else {
        int start = std::max(Lb, Rb - totalW);
        int cur = snap_to_site(rp, start, instWidth(d, ids[0]));
        for (int t=0;t<(int)ids.size();++t) { int id=ids[t]; int w=instWidth(d,id); ov.emplace_back(id, cur); cur += w; }
    }
    for (auto& p: ov) d.insts[p.first].x = p.second;
    return true;
}

static bool random_shuffle_pack(Design& d,
                                const std::vector<int>& rowCells,
                                int l, int r,
                                const std::vector<std::vector<int>>& inst2nets,
                                std::mt19937& rng,
                                int segLb, int segRb) {
    if (l>=r) return false;
    int y = d.insts[rowCells[l]].y; for (int i=l+1;i<=r;++i) if (d.insts[rowCells[i]].y != y) return false;
    const Row* rp = findRowForY(d, y);
    int Lb = segLb; if (l>0) { int ln=rowCells[l-1]; Lb = std::max(Lb, d.insts[ln].x + instWidth(d, ln)); }
    int Rb = segRb; if (r+1<(int)rowCells.size()) { int rn=rowCells[r+1]; Rb = std::min(Rb, d.insts[rn].x); }
    std::vector<int> ids; ids.reserve(r-l+1); for (int i=l;i<=r;++i) ids.push_back(rowCells[i]);
    std::shuffle(ids.begin(), ids.end(), rng);
    int totalW=0; for (int id: ids) totalW += instWidth(d,id); if (Rb - Lb < totalW) return false;
    auto nets = collectNets(inst2nets, ids);
    auto evalPack = [&](bool toLeft){
        std::vector<std::pair<int,int>> ov; ov.reserve(ids.size());
        if (toLeft) {
            int cur = snap_to_site(rp, Lb, instWidth(d, ids[0]));
            for (int k=0;k<(int)ids.size();++k) { int id=ids[k]; int w=instWidth(d,id); if (cur > Rb - w) return (int64_t)1e15; ov.emplace_back(id,cur); cur+=w; }
        } else {
            int start = std::max(Lb, Rb - totalW);
            int cur = snap_to_site(rp, start, instWidth(d, ids[0]));
            for (int k=0;k<(int)ids.size();++k) { int id=ids[k]; int w=instWidth(d,id); if (cur > Rb - w) return (int64_t)1e15; ov.emplace_back(id,cur); cur+=w; }
        }
        return evalDeltaOverrides(d, nets, ov);
    };
    int64_t dL = evalPack(true), dR = evalPack(false);
    if (dL >= 0 && dR >= 0) return false;
    std::vector<std::pair<int,int>> ov;
    if (dL <= dR) {
        int cur = snap_to_site(rp, Lb, instWidth(d, ids[0]));
        for (int k=0;k<(int)ids.size();++k) { int id=ids[k]; int w=instWidth(d,id); ov.emplace_back(id,cur); cur+=w; }
    } else {
        int start = std::max(Lb, Rb - totalW);
        int cur = snap_to_site(rp, start, instWidth(d, ids[0]));
        for (int k=0;k<(int)ids.size();++k) { int id=ids[k]; int w=instWidth(d,id); ov.emplace_back(id,cur); cur+=w; }
    }
    for (auto& p: ov) d.insts[p.first].x = p.second;
    return true;
}

static inline int64_t netHPWL(const Design& d, int nid) {
    const Net& net = d.nets[nid];
    if (net.isSpecial || net.pins.empty()) return 0; // skip special nets
    int xmin=INT32_MAX,xmax=INT32_MIN,ymin=INT32_MAX,ymax=INT32_MIN;
    for (auto&p:net.pins){
        int x,y;
        if(p.isIO){
            auto it = d.ioPinLoc.find(p.ioName);
            if (it != d.ioPinLoc.end()) { x=it->second.x; y=it->second.y; }
            else { x=p.ioLoc.x; y=p.ioLoc.y; }
        } else if (p.instId >= 0 && p.instId < (int)d.insts.size()) {
            // bottom-left corner
            x=d.insts[p.instId].x; y=d.insts[p.instId].y;
        } else { continue; }
        xmin=std::min(xmin,x);xmax=std::max(xmax,x);
        ymin=std::min(ymin,y);ymax=std::max(ymax,y);
    }
    return (xmax-xmin)+(ymax-ymin);
}

static inline int64_t totalHPWL(const Design& d) {
    int64_t s=0;
    for(int i=0;i<(int)d.nets.size();++i) s+=netHPWL(d,i);
    return s;
}

// 簡單的 row 合法化：對非固定 cell 進行站點對齊與左向緊縮，維持現有順序
static void clamp_and_left_pack_row(Design& d, const Row& r) {
    std::vector<int> cells;
    cells.reserve(d.insts.size());
    for (int i=0;i<(int)d.insts.size();++i) if (d.insts[i].y == r.originY) cells.push_back(i);
    if (cells.empty()) return;
    std::sort(cells.begin(), cells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
    int rowL = r.originX;
    int rowR = r.originX + r.xStep * r.numSites;
    int n = (int)cells.size();
    int i = 0;
    int Lb = rowL; // moving left boundary within the row
    while (i < n) {
        // If current is fixed, advance and update Lb
        if (d.insts[cells[i]].fixed) {
            Lb = std::max(Lb, d.insts[cells[i]].x + instWidth(d, cells[i]));
            ++i;
            continue;
        }
        // Determine segment end j at next fixed or end
        int j = i;
        while (j < n && !d.insts[cells[j]].fixed) ++j;
        // Compute segment boundaries based on surrounding fixed cells
        int segLb = Lb;
        int segRb = rowR;
        if (j < n) segRb = std::min(segRb, d.insts[cells[j]].x);
        // Sweep minimal right shifts within [segLb, segRb]
        int cur = segLb;
        for (int k=i; k<j; ++k) {
            int cid = cells[k];
            int w = instWidth(d, cid);
            int want = d.insts[cid].x;
            // clamp desired into [segLb, segRb - w]
            if (want < segLb) want = segLb;
            if (want > segRb - w) want = segRb - w;
            // enforce non-overlap
            if (want < cur) want = cur;
            // snap to site
            int xs = snap_to_site(&r, want, w);
            if (xs < cur) xs = cur;
            if (xs > segRb - w) xs = segRb - w;
            d.insts[cid].x = xs;
            cur = xs + w;
        }
        // advance past this segment; update Lb to the end of this placed block
        Lb = cur;
        i = j;
    }
}

// 對所有 rows 做一到兩輪保守合法化，確保無重疊且在列範圍內
void final_legalize_rows(Design& d) {
    for (int round=0; round<2; ++round) {
        for (const auto& r : d.layout.rows) {
            clamp_and_left_pack_row(d, r);
        }
    }
}

// 建立 inst→nets adjacency
static std::vector<std::vector<int>> buildInstToNets(const Design& d) {
    std::vector<std::vector<int>> res(d.insts.size());
    for (int nid=0;nid<(int)d.nets.size();++nid){
        if (d.nets[nid].isSpecial) continue; // exclude special nets
        for (auto&p:d.nets[nid].pins)
            if(!p.isIO&&p.instId>=0)
                res[p.instId].push_back(nid);
    }
    return res;
}

// 計算 window 中 HPWL（加速）
static int64_t windowHPWL(const Design& d, const std::vector<int>& windowCells,
                          const std::vector<std::vector<int>>& inst2nets) {
    std::unordered_set<int> nets;
    for (int cid : windowCells)
        for (int nid : inst2nets[cid])
            nets.insert(nid);
    int64_t hp = 0;
    for (int nid : nets) hp += netHPWL(d, nid);
    return hp;
}

void run_hpwl_optimization(Design& d) {
    using clk = std::chrono::steady_clock;
    const double TIME_BUDGET_SEC = 300.0; // enforce 300s cap per testcase
    auto t0 = clk::now();
    auto elapsed_sec = [&](){ return std::chrono::duration<double>(clk::now()-t0).count(); };
    int64_t hp0 = totalHPWL(d);
    std::cout << "[HPWL Optimization] Start HPWL = " << hp0 << "\n";
    auto inst2nets = buildInstToNets(d);

    bool improved = true; int outer=0;
    while (improved && outer < 1) {
        improved = false; ++outer;
        // 逐 row 取出 cells（包含固定），切成 segment（固定為障礙）
        for (const auto& r : d.layout.rows) {
            // collect all cells on this row (fixed + unfixed)
            std::vector<int> allCells;
            for (int i=0;i<(int)d.insts.size();++i) if (d.insts[i].y == r.originY) allCells.push_back(i);
            if (allCells.empty()) continue;
            std::sort(allCells.begin(), allCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });

            const Row* rp = findRowForY(d, r.originY);
            int rowL = rp ? rp->originX : std::numeric_limits<int>::min()/4;
            int rowR = rp ? (rp->originX + rp->xStep*rp->numSites) : std::numeric_limits<int>::max()/4;

            bool rowImp=false;

            // iterate segments of consecutive unfixed cells between fixed obstacles
            int m = (int)allCells.size();
            int s = 0;
            while (s < m) {
                // skip fixed
                while (s < m && d.insts[allCells[s]].fixed) ++s;
                if (s >= m) break;
                int e = s;
                while (e < m && !d.insts[allCells[e]].fixed) ++e;

                // segment [s, e-1] are unfixed
                std::vector<int> segCells;
                for (int k=s; k<e; ++k) segCells.push_back(allCells[k]);
                if (segCells.size() < 2) { s = e; continue; }

                int segLb = rowL;
                if (s-1 >= 0) {
                    int lf = allCells[s-1]; segLb = std::max(segLb, d.insts[lf].x + instWidth(d, lf));
                }
                int segRb = rowR;
                if (e < m) {
                    int rf = allCells[e]; segRb = std::min(segRb, d.insts[rf].x);
                }
                if (segRb <= segLb) { s = e; continue; }

                // Sort segment by x just in case
                std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });

                // time check
                if (elapsed_sec() > TIME_BUDGET_SEC) break;

                // 0) 大視窗 reorder（保留 x-slots），W=20，步長10（粗調）
                const int W0=20; int step0=10;
                for (int i=0;i+W0-1<(int)segCells.size(); i+=step0) {
                    rowImp |= reorder_window_keep_positions(d, segCells, i, i+W0-1, inst2nets, segLb, segRb);
                    if (elapsed_sec() > TIME_BUDGET_SEC) break;
                }
                for (int i=(int)segCells.size()-W0; i>=0 && elapsed_sec() <= TIME_BUDGET_SEC; i-=step0)
                    rowImp |= reorder_window_keep_positions(d, segCells, i, i+W0-1, inst2nets, segLb, segRb);

                // 1) k-window reorder with pack（更強），W=16..6；每個 W 左右各一遍
                for (int W1=16; W1>=6 && elapsed_sec() <= TIME_BUDGET_SEC; --W1) {
                    int step1 = std::max(1, W1/2);
                    if ((int)segCells.size() >= W1) {
                        for (int i=0;i+W1-1<(int)segCells.size(); i+=step1) {
                            std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                            rowImp |= reorder_with_pack_window(d, segCells, i, i+W1-1, inst2nets, segLb, segRb);
                            if (elapsed_sec() > TIME_BUDGET_SEC) break;
                        }
                        for (int i=(int)segCells.size()-W1; i>=0 && elapsed_sec() <= TIME_BUDGET_SEC; i-=step1) {
                            std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                            rowImp |= reorder_with_pack_window(d, segCells, i, i+W1-1, inst2nets, segLb, segRb);
                        }
                    }
                }

                // 2) L1 block slide（剛體滑動），W=8，步長4，雙向，做兩輪（降負載）
                const int W2=8; int step2=4;
                if ((int)segCells.size() >= W2) {
                    for (int round=0; round<2 && elapsed_sec() <= TIME_BUDGET_SEC; ++round) {
                        for (int i=0;i+W2-1<(int)segCells.size(); i+=step2) {
                            std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                            rowImp |= block_slide_L1_window(d, segCells, i, i+W2-1, inst2nets, segLb, segRb);
                            if (elapsed_sec() > TIME_BUDGET_SEC) break;
                        }
                        for (int i=(int)segCells.size()-W2; i>=0 && elapsed_sec() <= TIME_BUDGET_SEC; i-=step2) {
                            std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                            rowImp |= block_slide_L1_window(d, segCells, i, i+W2-1, inst2nets, segLb, segRb);
                        }
                    }
                }

                // 3) 視窗緊縮（left/right pack），W=12，雙向，兩輪（降負載）
                const int W3=12; int step3=6;
                if ((int)segCells.size() >= 2) {
                    for (int round=0; round<2 && elapsed_sec() <= TIME_BUDGET_SEC; ++round) {
                        for (int i=0;i+1<(int)segCells.size(); i+=step3) {
                            std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                            int l=i, rgt=std::min((int)segCells.size()-1, i+W3-1);
                            rowImp |= compact_window_pack(d, segCells, l, rgt, inst2nets, /*toLeft=*/true, segLb, segRb);
                            rowImp |= compact_window_pack(d, segCells, l, rgt, inst2nets, /*toLeft=*/false, segLb, segRb);
                            if (elapsed_sec() > TIME_BUDGET_SEC) break;
                        }
                        for (int i=(int)segCells.size()-2; i>=0 && elapsed_sec() <= TIME_BUDGET_SEC; i-=step3) {
                            std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                            int rgt=i+1, l=std::max(0, i-W3+2);
                            rowImp |= compact_window_pack(d, segCells, l, rgt, inst2nets, /*toLeft=*/true, segLb, segRb);
                            rowImp |= compact_window_pack(d, segCells, l, rgt, inst2nets, /*toLeft=*/false, segLb, segRb);
                        }
                    }
                }

                // 4) 單顆小幅位移（±10 sites），雙向掃描一遍（略縮半徑加速）
                for (int i=0;i<(int)segCells.size() && elapsed_sec() <= TIME_BUDGET_SEC; ++i) {
                    std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                    rowImp |= shift_single_cell_best(d, segCells, i, inst2nets, /*maxSites=*/10, segLb, segRb);
                }
                for (int i=(int)segCells.size()-1;i>=0 && elapsed_sec() <= TIME_BUDGET_SEC; --i) {
                    std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                    rowImp |= shift_single_cell_best(d, segCells, i, inst2nets, /*maxSites=*/10, segLb, segRb);
                }

                // 5) 小半徑插入（i→j），R=8，雙向一遍（降半徑）
                const int R=8;
                if ((int)segCells.size() >= 3) {
                    for (int i=0;i<(int)segCells.size() && elapsed_sec() <= TIME_BUDGET_SEC; ++i) {
                        std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                        for (int j=i+1;j<=std::min((int)segCells.size()-1, i+R) && elapsed_sec() <= TIME_BUDGET_SEC; ++j)
                            rowImp |= insert_within_window_pack(d, segCells, i, j, inst2nets, segLb, segRb);
                    }
                    for (int i=(int)segCells.size()-1;i>=0 && elapsed_sec() <= TIME_BUDGET_SEC; --i) {
                        std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                        for (int j=i-1;j>=std::max(0, i-R) && elapsed_sec() <= TIME_BUDGET_SEC; --j)
                            rowImp |= insert_within_window_pack(d, segCells, j, i, inst2nets, segLb, segRb);
                    }
                }

                // 6) 全段重排（啟發式，with pack）：最後再嘗試一次
                if ((int)segCells.size() >= 20 && elapsed_sec() <= TIME_BUDGET_SEC) {
                    std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                    rowImp |= reorder_with_pack_window(d, segCells, 0, (int)segCells.size()-1, inst2nets, segLb, segRb);
                }

                // 7) 全段緊縮（維持順序）：測試左/右 pack（以固定為界）
                if ((int)segCells.size() >= 2 && elapsed_sec() <= TIME_BUDGET_SEC) {
                    std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                    auto nets = collectNets(inst2nets, segCells);
                    // left pack
                    std::vector<std::pair<int,int>> ovL; ovL.reserve(segCells.size());
                    int curL = snap_to_site(rp, segLb, instWidth(d, segCells[0]));
                    bool badL=false; for (int id : segCells) { int w = instWidth(d, id); if (curL > segRb - w) { badL=true; break; } ovL.emplace_back(id, curL); curL += w; }
                    int64_t dL = badL? (int64_t)1e15 : evalDeltaOverrides(d, nets, ovL);
                    // right pack
                    int totalW=0; for (int id: segCells) totalW += instWidth(d,id);
                    std::vector<std::pair<int,int>> ovR; ovR.reserve(segCells.size());
                    int start = std::max(segLb, segRb - totalW);
                    int curR = snap_to_site(rp, start, instWidth(d, segCells[0]));
                    bool badR=false; for (int id : segCells) { int w = instWidth(d, id); if (curR > segRb - w) { badR=true; break; } ovR.emplace_back(id, curR); curR += w; }
                    int64_t dR = badR? (int64_t)1e15 : evalDeltaOverrides(d, nets, ovR);
                    if (dL < 0 || dR < 0) {
                        auto& best = (dL <= dR) ? ovL : ovR;
                        for (auto& p : best) d.insts[p.first].x = p.second;
                        rowImp = true;
                    }
                }

                // 8) 隨機視窗重排+pack（爬山式）— 次數依 segment 大小與剩餘時間而定
                if (elapsed_sec() <= TIME_BUDGET_SEC) {
                    std::mt19937 rng{std::random_device{}()};
                    int base = std::max(1, (int)segCells.size());
                    int trials = std::min(2000, base*8);
                    for (int t=0;t<trials && elapsed_sec() <= TIME_BUDGET_SEC; ++t) {
                        std::sort(segCells.begin(), segCells.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                        if ((int)segCells.size() < 5) break;
                        int W = 5 + (rng()%8); // 5..12
                        int iMax = std::max(0, (int)segCells.size() - W);
                        int i = (iMax>0)? (rng()%iMax) : 0;
                        rowImp |= random_shuffle_pack(d, segCells, i, i+W-1, inst2nets, rng, segLb, segRb);
                    }
                }

                improved |= rowImp;

                if (elapsed_sec() > TIME_BUDGET_SEC) break;
                s = e;
            }

            if (elapsed_sec() > TIME_BUDGET_SEC) break;

            // 每個 row 完成本輪處理後做一次保守合法化，避免下一輪出現例外狀態
            clamp_and_left_pack_row(d, r);
        }

        int64_t hpNow = totalHPWL(d);
        std::cout << "  Iter " << outer << " HPWL=" << hpNow << "\n";
        if (hpNow < hp0) hp0 = hpNow; else { /* 若無進步，可考慮中止 */ }
        if (elapsed_sec() > TIME_BUDGET_SEC) break;
    }
    std::cout << "[HPWL Optimization] Done. HPWL=" << hp0 << "\n";
}
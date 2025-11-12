// ===============================================================
// global_reorder.cpp  (C++14)
// Sliding-Window Global Reorder (SW-GR) for detailed placement
// ===============================================================
#include "global_reorder.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ---------- 小工具 ----------
static inline int ceil_div_GR(int a, int b) { return (a + b - 1) / b; }

struct BBox_GR {
    int xmin = std::numeric_limits<int>::max();
    int xmax = std::numeric_limits<int>::min();
    int ymin = std::numeric_limits<int>::max();
    int ymax = std::numeric_limits<int>::min();
};
static inline void bbox_add_point_GR(BBox_GR& b, int x, int y) {
    if (x < b.xmin) b.xmin = x;
    if (x > b.xmax) b.xmax = x;
    if (y < b.ymin) b.ymin = y;
    if (y > b.ymax) b.ymax = y;
}

// 用「cell 中心」近似 pin 位置（若未導入 LEF pin 幾何）
static inline void approx_pin_xy_GR(const Design& d, const NetPinRef& p, int& px, int& py) {
    if (p.isIO) { px = p.ioLoc.x; py = p.ioLoc.y; return; }
    if (p.instId < 0 || p.instId >= (int)d.insts.size()) { px = py = 0; return; }
    const Inst& I = d.insts[p.instId];
    int w = (I.macroId >= 0) ? d.macros[I.macroId].width  :
            (d.layout.rows.empty() ? 0 : d.layout.rows.front().xStep);
    int h = (I.macroId >= 0) ? d.macros[I.macroId].height :
            (d.layout.rows.empty() ? 0 : d.layout.rows.front().xStep);
    px = I.x + w/2;
    py = I.y + h/2;
}

// inst -> nets adjacency
static std::vector<std::vector<int>> buildInstToNets_GR(const Design& d) {
    std::vector<std::vector<int>> inst2nets(d.insts.size());
    for (int nid = 0; nid < (int)d.nets.size(); ++nid) {
        const Net& net = d.nets[nid];
        for (const auto& p : net.pins) {
            if (!p.isIO && p.instId >= 0 && p.instId < (int)d.insts.size()) {
                inst2nets[p.instId].push_back(nid);
            }
        }
    }
    for (auto& v : inst2nets) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    }
    return inst2nets;
}

static inline int site_width_of_inst_GR(const Design& d, const Row& row, int iid) {
    const Inst& I = d.insts[iid];
    if (I.macroId < 0) return 1;
    return std::max(1, ceil_div_GR(d.macros[I.macroId].width, row.xStep));
}

// 收集 winCells 涉及的 nets（去重）
static std::vector<int> gather_affected_nets_GR(const std::vector<int>& winCells,
                                                const std::vector<std::vector<int>>& inst2nets)
{
    std::vector<int> nets;
    for (int iid : winCells) {
        const auto& v = inst2nets[iid];
        nets.insert(nets.end(), v.begin(), v.end());
    }
    std::sort(nets.begin(), nets.end());
    nets.erase(std::unique(nets.begin(), nets.end()), nets.end());
    return nets;
}

// 排除一組 inst（winCells）後計算外部邊界（供 DP 成本）
static void compute_external_bounds_excluding_set_GR(
    const Design& d,
    const std::unordered_set<int>& excludedInsts,
    std::vector<int>& netXmin, std::vector<int>& netXmax)
{
    const int N = (int)d.nets.size();
    netXmin.assign(N, std::numeric_limits<int>::max());
    netXmax.assign(N, std::numeric_limits<int>::min());
    for (int nid = 0; nid < N; ++nid) {
        const Net& net = d.nets[nid];
        for (const auto& p : net.pins) {
            int px, py; approx_pin_xy_GR(d, p, px, py);
            if (!p.isIO && p.instId >= 0) {
                if (excludedInsts.count(p.instId)) continue; // 排除
            }
            if (px < netXmin[nid]) netXmin[nid] = px;
            if (px > netXmax[nid]) netXmax[nid] = px;
        }
    }
}

// 在視窗排列序內，標記每條 net 的左右極端索引
static void compute_local_extreme_indices_GR(
    const std::vector<int>& orderedCells,
    const std::vector<std::vector<int>>& inst2nets,
    int netsN,
    std::vector<int>& leftIdx, std::vector<int>& rightIdx)
{
    leftIdx.assign(netsN, -1);
    rightIdx.assign(netsN, -1);
    for (int i = 0; i < (int)orderedCells.size(); ++i) {
        int iid = orderedCells[i];
        for (int nid : inst2nets[iid]) {
            if (leftIdx[nid] == -1) leftIdx[nid] = i;
            rightIdx[nid] = i;
        }
    }
}

// 在小視窗內用「重掃涉網 pins」計算 ΔHPWL（成本可接受）
static long long deltaHPWL_nets_scan_GR(const Design& d,
                                        const std::vector<int>& nets,
                                        const std::unordered_map<int,int>& xOverrideDBU)
{
    long long delta = 0;
    for (int nid : nets) {
        const Net& net = d.nets[nid];

        BBox_GR b0;
        for (const auto& p : net.pins) {
            int px, py; approx_pin_xy_GR(d, p, px, py);
            bbox_add_point_GR(b0, px, py);
        }
        long long hp0 = (long long)(b0.xmax - b0.xmin) + (long long)(b0.ymax - b0.ymin);

        BBox_GR b1;
        for (const auto& p : net.pins) {
            int px, py; approx_pin_xy_GR(d, p, px, py);
            if (!p.isIO && p.instId >= 0) {
                auto it = xOverrideDBU.find(p.instId);
                if (it != xOverrideDBU.end()) {
                    // 調整 x：把 pin 中心沿著 inst.x 的位移等量平移
                    int dx = it->second - d.insts[p.instId].x;
                    px += dx;
                }
            }
            bbox_add_point_GR(b1, px, py);
        }
        long long hp1 = (long long)(b1.xmax - b1.xmin) + (long long)(b1.ymax - b1.ymin);
        delta += (hp1 - hp0);
    }
    return delta;
}

// ---------------- DP：固定順序單列合法化（site 尺度成本） ----------------
struct DPResult_GR {
    bool ok = true;
    std::vector<int> startSites; // 相對 row 左端（site）
};

static DPResult_GR dp_order_preserving_sites_GR(
    const Design& d, const Row& row,
    const std::vector<int>& orderedCells,
    const std::vector<int>& wSite,               // site 寬度
    const std::vector<int>& origX_site,          // 原起點（site）
    const std::vector<int>& sLo, const std::vector<int>& sHi,
    const std::vector<int>& netXmin_dbu,         // 外部邊界（DBU）
    const std::vector<int>& netXmax_dbu,
    const std::vector<std::vector<int>>& inst2nets,
    const std::vector<int>& leftIdx, const std::vector<int>& rightIdx,
    int Lsite, int Rsite,
    double gammaInternal, double gammaOneSide, double lambdaTether)
{
    DPResult_GR res;
    const int n = (int)orderedCells.size();
    res.startSites.assign(n, 0);
    if (n == 0) return res;

    int totalW = 0; for (int x : wSite) totalW += x;
    if (totalW > (Rsite - Lsite)) { res.ok = false; return res; }

    std::vector<int> Lend(n+1), Rend(n+1);
    Lend[0] = Lsite;
    for (int i = 0; i < n; ++i) Lend[i+1] = Lend[i] + wSite[i];
    Rend[n] = Rsite;
    for (int i = n-1; i >= 0; --i) Rend[i] = Rend[i+1] - wSite[i];

    const int step = row.xStep;

    // 外部邊界轉 site（相對 row 左端）
    std::vector<int> netMin_site(netXmin_dbu.size(), std::numeric_limits<int>::max());
    std::vector<int> netMax_site(netXmax_dbu.size(), std::numeric_limits<int>::min());
    for (int nid = 0; nid < (int)netXmin_dbu.size(); ++nid) {
        if (netXmin_dbu[nid] != std::numeric_limits<int>::max())
            netMin_site[nid] = (netXmin_dbu[nid] - row.originX) / step;
        if (netXmax_dbu[nid] != std::numeric_limits<int>::min())
            netMax_site[nid] = (netXmax_dbu[nid] - row.originX) / step;
    }

    const long long INFLL = (1LL<<60);

    auto cost_at_start = [&](int i, int sSite)->long long {
        if (sSite < sLo[i] || sSite > sHi[i]) return INFLL/4;

        long long hp_cost = 0;
        int iid = orderedCells[i];
        int x_site = sSite; // 以起點 site 代表（可換中心 site）

        for (int nid : inst2nets[iid]) {
            bool hasMin = (netMin_site[nid] != std::numeric_limits<int>::max());
            bool hasMax = (netMax_site[nid] != std::numeric_limits<int>::min());
            bool isLeftExtreme  = (leftIdx[nid]  == i);
            bool isRightExtreme = (rightIdx[nid] == i);

            if (hasMin && hasMax) {
                if (x_site > netMax_site[nid])      hp_cost += (x_site - netMax_site[nid]);
                else if (x_site < netMin_site[nid]) hp_cost += (netMin_site[nid] - x_site);
            } else if (hasMin && !hasMax) {
                if (isRightExtreme) hp_cost += (long long)std::llround(gammaOneSide * std::max(0, x_site - netMin_site[nid]));
            } else if (!hasMin && hasMax) {
                if (isLeftExtreme)  hp_cost += (long long)std::llround(gammaOneSide * std::max(0, netMax_site[nid] - x_site));
            } else {
                if (isLeftExtreme)  hp_cost += (long long)std::llround(gammaInternal * (-x_site));
                if (isRightExtreme) hp_cost += (long long)std::llround(gammaInternal * ( x_site));
            }
        }

        long long tether = (long long)std::llround(lambdaTether * std::llabs(x_site - origX_site[i]));
        return hp_cost + tether;
    };

    std::vector<long long> dpPrev(Rend[0]-Lend[0]+1, INFLL);
    std::vector<long long> dpCur;
    std::vector<std::vector<int>> parent(n);
    dpPrev[0] = 0;

    for (int i = 0; i < n; ++i) {
        int prevL = Lend[i], prevR = Rend[i];
        int curL  = Lend[i+1], curR  = Rend[i+1];
        int prevLen = prevR - prevL + 1;
        int curLen  = curR  - curL  + 1;

        std::vector<long long> prefVal(prevLen);
        std::vector<int>       prefArg(prevLen);
        prefVal[0] = dpPrev[0];
        prefArg[0] = prevL;
        for (int t = 1; t < prevLen; ++t) {
            if (dpPrev[t] <= prefVal[t-1]) { prefVal[t] = dpPrev[t]; prefArg[t] = prevL + t; }
            else { prefVal[t] = prefVal[t-1];           prefArg[t] = prefArg[t-1]; }
        }

        dpCur.assign(curLen, INFLL);
        parent[i].assign(curLen, -1);

        for (int k = curL; k <= curR; ++k) {
            int startSite = k - wSite[i];
            int idxPrev   = startSite - prevL;
            if (idxPrev < 0 || idxPrev >= prevLen) continue;

            long long bestPrev = prefVal[idxPrev];
            if (bestPrev >= INFLL/2) continue;

            long long cst = cost_at_start(i, startSite);
            if (cst >= INFLL/3) continue;

            long long cand = bestPrev + cst;
            int idxCur = k - curL;
            if (cand < dpCur[idxCur]) {
                dpCur[idxCur]    = cand;
                parent[i][idxCur] = prefArg[idxPrev];
            }
        }
        dpPrev.swap(dpCur);
    }

    int lastL = Lend[n], lastR = Rend[n];
    int bestK = -1; long long best = (1LL<<60);
    for (int t = 0; t <= lastR-lastL; ++t) {
        if (dpPrev[t] < best) { best = dpPrev[t]; bestK = lastL + t; }
    }
    if (bestK < 0) { res.ok = false; return res; }

    int curK = bestK;
    for (int i = n-1; i >= 0; --i) {
        int curL = Lend[i+1];
        int startSite = curK - wSite[i];
        res.startSites[i] = startSite;

        int idxCur = curK - curL;
        int prevK = parent[i][idxCur];
        if (!(prevK >= Lend[i] && prevK <= Rend[i])) { res.ok = false; return res; }
        curK = prevK;
    }
    return res;
}

// 產生候選排列（啟發式 + 輕度隨機）
static void gen_candidate_permutations_GR(
    const Design& d, const Row& row,
    const std::vector<int>& cells,
    const std::vector<std::vector<int>>& inst2nets,
    int permLimit,
    std::vector<std::vector<int>>& outPerms,
    std::mt19937& rng)
{
    outPerms.clear();
    if (cells.empty()) return;

    // 基線：依目前 x
    std::vector<int> byX = cells;
    std::sort(byX.begin(), byX.end(),
              [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
    outPerms.push_back(byX);

    // 依 net 重心 x
    std::vector<std::pair<double,int>> cx;
    cx.reserve(cells.size());
    for (int iid : cells) {
        double sumx = 0.0; int cnt = 0;
        for (int nid : inst2nets[iid]) {
            const Net& net = d.nets[nid];
            for (const auto& p : net.pins) {
                int px, py; approx_pin_xy_GR(d, p, px, py);
                sumx += px; ++cnt;
            }
        }
        double c = (cnt>0) ? (sumx/cnt) : (double)d.insts[iid].x;
        cx.emplace_back(c, iid);
    }
    std::sort(cx.begin(), cx.end());
    std::vector<int> byCentroid; byCentroid.reserve(cx.size());
    for (auto& kv : cx) byCentroid.push_back(kv.second);
    outPerms.push_back(byCentroid);

    // 高度數靠兩端
    std::vector<std::pair<int,int>> deg; deg.reserve(cells.size());
    for (int iid : cells) deg.emplace_back((int)inst2nets[iid].size(), iid);
    std::sort(deg.begin(), deg.end(), std::greater<std::pair<int,int>>());
    std::vector<int> ends = byX;
    int L=0, R=(int)ends.size()-1;
    for (auto& pr : deg) {
        if (L<=R) {
            if ((L+R)%2==0) ends[L++] = pr.second;
            else            ends[R--] = pr.second;
        }
    }
    outPerms.push_back(ends);

    // 輕度隨機擾動
    std::uniform_int_distribution<int> dist(0, (int)byX.size()-1);
    while ((int)outPerms.size() < permLimit) {
        std::vector<int> shuf = byX;
        int swaps = std::max(1, (int)shuf.size()/3);
        for (int s=0; s<swaps; ++s) {
            int i = dist(rng), j = dist(rng);
            if (i!=j) std::swap(shuf[i], shuf[j]);
        }
        outPerms.push_back(std::move(shuf));
    }

    std::sort(outPerms.begin(), outPerms.end());
    outPerms.erase(std::unique(outPerms.begin(), outPerms.end()), outPerms.end());
    if ((int)outPerms.size() > permLimit) outPerms.resize(permLimit);
}

// 嘗試單一視窗的 global reorder；若改善則套用
static bool try_window_global_reorder_GR(
    Design& d, int ri, int Lsite, int Rsite,
    const std::vector<int>& winCells_in,
    const std::vector<std::vector<int>>& inst2nets,
    const GlobalReorderConfig& cfg,
    std::mt19937& rng)
{
    if ((int)winCells_in.size() < 2) return false;
    const Row& row = d.layout.rows[ri];
    const int step = row.xStep;

    // 視窗內 cells（依 x 排序）
    std::vector<int> winCells = winCells_in;
    std::sort(winCells.begin(), winCells.end(),
              [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });

    // 若太多，只挑中心 K 顆，並收縮視窗覆蓋區（避免碰到非候選 cell）
    if ((int)winCells.size() > cfg.maxCellsPerWindow) {
        int Lx = row.originX + Lsite*step;
        int Rx = row.originX + Rsite*step;
        int cx = (Lx + Rx)/2;

        std::nth_element(winCells.begin(),
                         winCells.begin()+cfg.maxCellsPerWindow,
                         winCells.end(),
                         [&](int a,int b){
                             return std::abs(d.insts[a].x - cx) < std::abs(d.insts[b].x - cx);
                         });
        winCells.resize(cfg.maxCellsPerWindow);
        std::sort(winCells.begin(), winCells.end(),
                  [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });

        int minX = std::numeric_limits<int>::max();
        int maxX = std::numeric_limits<int>::min();
        for (int iid : winCells) {
            int wdbu = (d.insts[iid].macroId>=0)? d.macros[d.insts[iid].macroId].width : step;
            minX = std::min(minX, d.insts[iid].x);
            maxX = std::max(maxX, d.insts[iid].x + wdbu);
        }
        int Ls2 = (minX - row.originX)/step;
        int Rs2 = (maxX - row.originX + step-1)/step;
        Lsite = std::max(Lsite, Ls2);
        Rsite = std::min(Rsite, Rs2);
        if (Rsite - Lsite < 2) return false;

        // 確認沒有非候選可動 cell 混在收縮後視窗範圍
        for (int iid = 0; iid < (int)d.insts.size(); ++iid) {
            const Inst& I = d.insts[iid];
            if (I.fixed || !I.placed) continue;
            if (I.y != row.originY) continue;
            if (std::find(winCells.begin(), winCells.end(), iid) != winCells.end()) continue;
            int iw = (I.macroId>=0)? d.macros[I.macroId].width : step;
            int Lx2 = row.originX + Lsite*step, Rx2 = row.originX + Rsite*step;
            if (I.x < Rx2 && I.x + iw > Lx2) return false; // 視窗遭佔用，保守跳過
        }
    }

    // 產生候選排列
    std::vector<std::vector<int>> perms;
    gen_candidate_permutations_GR(d, row, winCells, inst2nets, cfg.permLimit, perms, rng);
    if (perms.empty()) return false;

    // 預處理：寬度/原位/視窗限制（皆用 site）
    const int m = (int)winCells.size();
    std::vector<int> wSite(m), orig_site(m), sLo(m), sHi(m);
    for (int i = 0; i < m; ++i) {
        wSite[i]     = site_width_of_inst_GR(d, row, winCells[i]);
        orig_site[i] = (d.insts[winCells[i]].x - row.originX) / step;
        int lo = std::max(Lsite, orig_site[i] - cfg.windowSitesGlobal);
        int hi = std::min(Rsite - wSite[i], orig_site[i] + cfg.windowSitesGlobal);
        if (hi < lo) { lo = std::max(Lsite, std::min(orig_site[i], Rsite - wSite[i])); hi = lo; }
        sLo[i] = lo; sHi[i] = hi;
    }

    // 外部邊界（排除 winCells）
    std::unordered_set<int> excl(winCells.begin(), winCells.end());
    std::vector<int> netXmin_dbu, netXmax_dbu;
    compute_external_bounds_excluding_set_GR(d, excl, netXmin_dbu, netXmax_dbu);

    // 涉網
    std::vector<int> affectedNets = gather_affected_nets_GR(winCells, inst2nets);

    long long bestDelta = 0;
    std::unordered_map<int,int> bestOverride; // iid -> newX(DBU)
    bool improved = false;

    // 建立 iid->index 映射，供排列重映射
    std::unordered_map<int,int> idxMap; idxMap.reserve(m*2);
    for (int i = 0; i < m; ++i) idxMap[winCells[i]] = i;

    // 逐排列嘗試
    for (const auto& permCells : perms) {
        // 依排列取出對應的向量
        std::vector<int> wSiteP, origP, sLoP, sHiP;
        wSiteP.reserve(m); origP.reserve(m); sLoP.reserve(m); sHiP.reserve(m);
        for (int iid : permCells) {
            int k = idxMap[iid];
            wSiteP.push_back(wSite[k]);
            origP .push_back(orig_site[k]);
            sLoP  .push_back(sLo[k]);
            sHiP  .push_back(sHi[k]);
        }

        // 局部極端
        std::vector<int> leftIdx, rightIdx;
        compute_local_extreme_indices_GR(permCells, inst2nets, (int)d.nets.size(), leftIdx, rightIdx);

        // 固定順序 DP（site 尺度成本）
        DPResult_GR dp = dp_order_preserving_sites_GR(
            d, row, permCells,
            wSiteP, origP, sLoP, sHiP,
            netXmin_dbu, netXmax_dbu,
            inst2nets, leftIdx, rightIdx,
            Lsite, Rsite,
            cfg.gammaInternal, cfg.gammaOneSide, cfg.lambdaTether
        );
        if (!dp.ok) continue;

        // 形成 overrides（DBU）
        std::unordered_map<int,int> xOverride;
        for (int i = 0; i < (int)permCells.size(); ++i) {
            int iid = permCells[i];
            int x = row.originX + dp.startSites[i] * step;
            xOverride[iid] = x;
        }

        // ΔHPWL（掃涉網）
        long long dH = deltaHPWL_nets_scan_GR(d, affectedNets, xOverride);
        if (dH < bestDelta) {
            bestDelta = dH;
            bestOverride = std::move(xOverride);
            improved = true;
        }
    }

    if (!improved) return false;

    // 套用最佳解
    for (auto& kv : bestOverride) {
        int iid = kv.first;
        d.insts[iid].x = kv.second;
        d.insts[iid].y = row.originY;
        d.insts[iid].orient = row.flip ? Orient::FS : Orient::N;
        d.insts[iid].placed = true;
    }
    return true;
}

// ---------------- 對全設計跑多 pass 的 SW-GR ----------------
void global_reorder_sliding_windows(Design& d, const GlobalReorderConfig& cfg)
{
    if (d.layout.rows.empty()) return;

    auto inst2nets = buildInstToNets_GR(d);

    // y -> row
    std::unordered_map<int,int> y2row;
    y2row.reserve(d.layout.rows.size()*2);
    for (int ri=0; ri<(int)d.layout.rows.size(); ++ri) {
        y2row[d.layout.rows[ri].originY] = ri;
    }

    // 每 row 可動 cells（初始化一次，之後每次改善重排）
    std::vector<std::vector<int>> rowCells(d.layout.rows.size());
    for (int iid=0; iid<(int)d.insts.size(); ++iid) {
        const Inst& I = d.insts[iid];
        if (I.fixed || !I.placed) continue;
        auto it = y2row.find(I.y);
        if (it == y2row.end()) continue;
        rowCells[it->second].push_back(iid);
    }
    for (int ri=0; ri<(int)rowCells.size(); ++ri) {
        auto& rc = rowCells[ri];
        std::sort(rc.begin(), rc.end(), [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
    }

    std::mt19937 rng(cfg.rngSeed);

    auto one_pass = [&](bool leftToRight) {
        long long improvedWindows = 0;

        for (int ri=0; ri<(int)d.layout.rows.size(); ++ri) {
            const Row& row = d.layout.rows[ri];
            if (row.numSites <= 0) continue;

            const int step = row.xStep;
            auto& rc = rowCells[ri];
            if (rc.size() < 2) continue;

            int start = 0, end = row.numSites;
            int L = leftToRight ? start : (end - cfg.windowSitesGlobal);
            if (!leftToRight) L = std::max(0, L);

            for (; (leftToRight ? (L < end) : (L >= 0));
                   (leftToRight ? L += cfg.strideSites : L -= cfg.strideSites))
            {
                int R = std::min(row.numSites, L + cfg.windowSitesGlobal);
                if (R - L < 2) continue;

                int Lx = row.originX + L*step;
                int Rx = row.originX + R*step;

                // 收集視窗 cells
                std::vector<int> winCells;
                winCells.reserve(32);
                for (int iid : rc) {
                    const Inst& I = d.insts[iid];
                    int wdbu = (I.macroId>=0)? d.macros[I.macroId].width : step;
                    int x2 = I.x + wdbu;
                    if (I.x < Rx && x2 > Lx) winCells.push_back(iid);
                }
                if ((int)winCells.size() < 2) continue;

                bool ok = try_window_global_reorder_GR(
                    d, ri, L, R, winCells, inst2nets, cfg, rng);

                if (ok) {
                    ++improvedWindows;
                    // x 變了，更新 row cell 排序
                    std::sort(rc.begin(), rc.end(),
                              [&](int a,int b){ return d.insts[a].x < d.insts[b].x; });
                }
            }
        }

        if (cfg.verbose) {
            std::cout << "[SW-GR] one_pass(" << (leftToRight?"L->R":"R->L")
                      << ") improved windows = " << improvedWindows << "\n";
        }
        return improvedWindows;
    };

    for (int p = 0; p < cfg.gwPasses; ++p) {
        one_pass(true);
        one_pass(false);
    }

    if (cfg.verbose) std::cout << "[SW-GR] Done.\n";
}
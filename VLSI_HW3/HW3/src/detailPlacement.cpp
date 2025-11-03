#include "detailPlacement.hpp"
#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

static inline int ceil_div(int a, int b) { return (a + b - 1) / b; }
static const long long INFLL = (1LL << 60);

// inst -> nets adjacency
static std::vector<std::vector<int>> buildInstToNets(const Design& d) {
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

static void apply_row_orient_and_y(Inst& I, const Row& r) {
    I.y = r.originY;
    I.orient = r.flip ? Orient::FS : Orient::N;
    I.placed = true;
}

struct RowDPResult {
    std::vector<int> startSites; // 每顆 cell 的起始 site（相對 row 左端）
    bool ok = true;
};

// 使用固定順序的 DP
// 成本 = 外部 bbox 出界懲罰（兩邊都有外部 pin 才啟用）
//      + 單邊外部 pin 的線性懲罰（只對另一側極端）
//      + 無外部 pin 的內部寬度線性分解（只對左右極端）
//      + 原位牽引（所有 cell）
static RowDPResult place_row_dp_order_preserving(
    const Design& d,
    const Row& row,
    const std::vector<int>& cellIds,              // inst ids (已依 x 排序，固定順序)
    const std::vector<int>& w,                    // 每顆 cell 的寬度（site 數）
    const std::vector<int>& origX,                // 每顆 cell 的原始 x（DBU，用於 tether）
    const std::vector<int>& sLo,                  // 視窗限制：start site 下界
    const std::vector<int>& sHi,                  // 視窗限制：start site 上界
    const std::vector<int>& netXmin,              // 每條 net 的 external xmin（若無 external 則為 +inf）
    const std::vector<int>& netXmax,              // 每條 net 的 external xmax（若無 external 則為 -inf）
    const std::vector<std::vector<int>>& inst2nets,
    const std::vector<int>& rowLeftIdxPerNet,     // 這個 row 中，每條 net 的最左 cell 局部索引（i），無則 -1
    const std::vector<int>& rowRightIdxPerNet,    // 這個 row 中，每條 net 的最右 cell 局部索引（i），無則 -1
    int Lsite, int Rsite, int stepDBU,
    double lambdaTether, double gammaInternal, double gammaOneSide,
    bool verbose = false)
{
    RowDPResult res;
    const int n = (int)cellIds.size();
    res.startSites.assign(n, 0);
    if (n == 0) return res;

    int totalW = 0; for (int x : w) totalW += x;
    if (totalW > (Rsite - Lsite)) {
        if (verbose) std::cout << "    [DP] width overflow (totalW=" << totalW
                               << " > capacity=" << (Rsite - Lsite) << ")\n";
        res.ok = false;
        return res;
    }

    std::vector<int> Lend(n+1), Rend(n+1);
    Lend[0] = Lsite;
    for (int i = 0; i < n; ++i) Lend[i+1] = Lend[i] + w[i];
    Rend[n] = Rsite;
    for (int i = n-1; i >= 0; --i) Rend[i] = Rend[i+1] - w[i];

    std::vector<long long> dpPrev(Rend[0] - Lend[0] + 1, INFLL);
    std::vector<long long> dpCur;
    std::vector<std::vector<int>> parent(n);
    dpPrev[0] = 0;

    auto cost_at_start = [&](int i, int sSite)->long long {
        if (sSite < sLo[i] || sSite > sHi[i]) return INFLL/4;

        long long x = (long long)row.originX + 1LL * sSite * stepDBU;

        long long hpwl_cost = 0;
        const int instId = cellIds[i];
        for (int nid : inst2nets[instId]) {
            const bool hasMin = (netXmin[nid] != std::numeric_limits<int>::max());
            const bool hasMax = (netXmax[nid] != std::numeric_limits<int>::min());
            const bool isLeftExtreme  = (rowLeftIdxPerNet[nid]  == i);
            const bool isRightExtreme = (rowRightIdxPerNet[nid] == i);

            if (hasMin && hasMax) {
                // 兩邊都有外部：只有出界才收費
                if (x > netXmax[nid])      hpwl_cost += (x - netXmax[nid]);
                else if (x < netXmin[nid]) hpwl_cost += (netXmin[nid] - x);
            } else if (hasMin && !hasMax) {
                // 只有左外部：HPWL ≈ x_right - Xmin_out → 只對右極端加 +x
                if (isRightExtreme) hpwl_cost += (long long)std::llround(gammaOneSide * x);
            } else if (!hasMin && hasMax) {
                // 只有右外部：HPWL ≈ Xmax_out - x_left → 只對左極端加 -x
                if (isLeftExtreme)  hpwl_cost += (long long)std::llround(gammaOneSide * (-x));
            } else {
                // 無外部：HPWL ≈ x_right - x_left → 左加 -x，右加 +x
                if (isLeftExtreme)  hpwl_cost += (long long)std::llround(gammaInternal * (-x));
                if (isRightExtreme) hpwl_cost += (long long)std::llround(gammaInternal * ( x));
            }
        }

        long long tether = (long long)std::llround(lambdaTether * std::llabs(x - (long long)origX[i]));
        return hpwl_cost + tether;
    };

    for (int i = 0; i < n; ++i) {
        const int prevL = Lend[i],  prevR = Rend[i];
        const int curL  = Lend[i+1], curR  = Rend[i+1];
        const int prevLen = prevR - prevL + 1;
        const int curLen  = curR - curL + 1;

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
            int startSite = k - w[i];
            int idxInPrev = startSite - prevL;
            if (idxInPrev < 0 || idxInPrev >= prevLen) continue;

            long long bestPrev = prefVal[idxInPrev];
            if (bestPrev >= INFLL/2) continue;

            long long cst = cost_at_start(i, startSite);
            if (cst >= INFLL/3) continue;

            long long cand = bestPrev + cst;
            int idxCur = k - curL;
            if (cand < dpCur[idxCur]) {
                dpCur[idxCur]   = cand;
                parent[i][idxCur] = prefArg[idxInPrev];
            }
        }
        dpPrev.swap(dpCur);
    }

    int lastL = Lend[n], lastR = Rend[n];
    int lastLen = lastR - lastL + 1;
    long long best = INFLL; int bestK = -1;
    for (int t = 0; t < lastLen; ++t) {
        if (dpPrev[t] < best) { best = dpPrev[t]; bestK = lastL + t; }
    }
    if (bestK < 0) { res.ok = false; return res; }

    int currK = bestK;
    for (int i = n-1; i >= 0; --i) {
        int curL = Lend[i+1];
        int startSite = currK - w[i];
        res.startSites[i] = startSite;

        int idxCur = currK - curL;
        int prevK = parent[i][idxCur];
        if (!(prevK >= Lend[i] && prevK <= Rend[i])) { res.ok = false; return res; }
        currK = prevK;
    }
    return res;
}

void detailPlacement(Design& d, const DetailPlaceConfig& cfg) {
    if (cfg.verbose) {
        std::cout << "==== Begin detailPlacement (order-preserving DP, "
                  << cfg.passes << " passes) ====\n";
        std::cout << "  lambdaTether=" << cfg.lambdaTether
                  << ", windowSites="   << cfg.windowSites
                  << ", gammaInternal=" << cfg.gammaInternal
                  << ", gammaOneSide="  << cfg.gammaOneSide << "\n";
    }

    auto inst2nets = buildInstToNets(d);

    std::unordered_map<int,int> y2row;
    y2row.reserve(d.layout.rows.size()*2);
    for (int ri = 0; ri < (int)d.layout.rows.size(); ++ri) {
        y2row[d.layout.rows[ri].originY] = ri;
    }

    for (int pass = 1; pass <= cfg.passes; ++pass) {
        long long movedCells = 0;
        if (cfg.verbose) std::cout << "[Pass " << pass << "]\n";

        for (int ri = 0; ri < (int)d.layout.rows.size(); ++ri) {
            const Row& row = d.layout.rows[ri];
            const int step  = row.xStep;
            const int Lsite = 0;
            const int Rsite = row.numSites;

            // 收集此 row 的可動 cells（保持原有 cell 順序為 x 升序）
            std::vector<int> cells;
            cells.reserve(512);
            for (int iid = 0; iid < (int)d.insts.size(); ++iid) {
                const Inst& I = d.insts[iid];
                if (I.fixed)  continue;
                if (!I.placed) continue;
                auto it = y2row.find(I.y);
                if (it == y2row.end() || it->second != ri) continue;
                cells.push_back(iid);
            }
            if (cells.empty()) continue;

            std::sort(cells.begin(), cells.end(),
                      [&](int a, int b){ return d.insts[a].x < d.insts[b].x; });

            // cell 寬度（site）
            std::vector<int> w(cells.size(), 1);
            for (int i = 0; i < (int)cells.size(); ++i) {
                const Inst& I = d.insts[cells[i]];
                if (I.macroId >= 0) {
                    int widthDBU = d.macros[I.macroId].width;
                    w[i] = std::max(1, ceil_div(widthDBU, step));
                } else {
                    w[i] = 1;
                }
            }

            // 原始 x（tether 參考）
            std::vector<int> origX(cells.size());
            for (int i = 0; i < (int)cells.size(); ++i)
                origX[i] = d.insts[cells[i]].x;

            // 本 row 的可動 cell 標記
            std::vector<char> inRowFlag(d.insts.size(), 0);
            for (int iid : cells) inRowFlag[iid] = 1;

            // 建 external bbox：排除本 row 的可動 cell
            std::vector<int> netXmin(d.nets.size(), std::numeric_limits<int>::max());
            std::vector<int> netXmax(d.nets.size(), std::numeric_limits<int>::min());
            for (int nid = 0; nid < (int)d.nets.size(); ++nid) {
                const Net& net = d.nets[nid];
                for (const auto& p : net.pins) {
                    int px;
                    if (p.isIO) {
                        px = p.ioLoc.x;
                    } else if (p.instId >= 0 && !inRowFlag[p.instId]) {
                        px = d.insts[p.instId].x; // pin = cell LL
                    } else continue;
                    if (px < netXmin[nid]) netXmin[nid] = px;
                    if (px > netXmax[nid]) netXmax[nid] = px;
                }
            }

            // 找出此 row 對每條 net 的左右極端（用 cells 的局部 index i 表示）
            std::vector<int> rowLeftIdxPerNet(d.nets.size(), -1);
            std::vector<int> rowRightIdxPerNet(d.nets.size(), -1);
            for (int i = 0; i < (int)cells.size(); ++i) {
                int iid = cells[i];
                for (int nid : inst2nets[iid]) {
                    if (rowLeftIdxPerNet[nid] == -1) rowLeftIdxPerNet[nid] = i;
                    rowRightIdxPerNet[nid] = i; // 最終即為最右
                }
            }

            // 視窗限制（以 site）
            std::vector<int> sLo(cells.size()), sHi(cells.size());
            for (int i = 0; i < (int)cells.size(); ++i) {
                int s0 = (origX[i] - row.originX) / step; // 原始起點（site）
                int lo = std::max(Lsite, s0 - cfg.windowSites);
                int hi = std::min(Rsite - w[i], s0 + cfg.windowSites);
                if (hi < lo) { lo = std::max(Lsite, std::min(s0, Rsite - w[i])); hi = lo; }
                sLo[i] = lo; sHi[i] = hi;
            }

            // 執行 DP
            RowDPResult ans = place_row_dp_order_preserving(
                d, row, cells, w, origX, sLo, sHi, netXmin, netXmax,
                inst2nets, rowLeftIdxPerNet, rowRightIdxPerNet,
                Lsite, Rsite, step,
                cfg.lambdaTether, cfg.gammaInternal, cfg.gammaOneSide,
                cfg.verbose && pass==1
            );

            if (!ans.ok) {
                if (cfg.verbose) std::cout << "  Row#" << ri << " DP infeasible. Keep original.\n";
                continue;
            }

            // 套用結果
            for (int i = 0; i < (int)cells.size(); ++i) {
                const int iid = cells[i];
                Inst& I = d.insts[iid];
                int startSite = ans.startSites[i];
                int newX = row.originX + startSite * step;
                if (I.x != newX || I.y != row.originY) ++movedCells;
                I.x = newX;
                apply_row_orient_and_y(I, row);
            }
        }

        if (cfg.verbose) std::cout << "  moved cells in pass " << pass << " = " << movedCells << "\n";
        
        // Post-processing: 嘗試相鄰 cell 的兩兩交換來進一步優化 HPWL
        if (pass % 3 == 0) {  // 每 3 次迭代做一次 swap pass
            long long swappedPairs = 0;
            for (int ri = 0; ri < (int)d.layout.rows.size(); ++ri) {
                const Row& row = d.layout.rows[ri];
                std::vector<int> rowCells;
                for (int iid = 0; iid < (int)d.insts.size(); ++iid) {
                    const Inst& I = d.insts[iid];
                    if (I.fixed || !I.placed) continue;
                    auto it = y2row.find(I.y);
                    if (it == y2row.end() || it->second != ri) continue;
                    rowCells.push_back(iid);
                }
                if (rowCells.size() < 2) continue;
                
                std::sort(rowCells.begin(), rowCells.end(),
                          [&](int a, int b){ return d.insts[a].x < d.insts[b].x; });
                
                // 嘗試小範圍內的任意兩個 cell 交換（不只是相鄰）
                for (int i = 0; i < (int)rowCells.size(); ++i) {
                    for (int j = i + 1; j < std::min(i + 8, (int)rowCells.size()); ++j) {
                        int id1 = rowCells[i];
                        int id2 = rowCells[j];
                        
                        // 只有相同寬度才能安全交換（避免 overlap）
                        int w1 = d.macros[d.insts[id1].macroId].width;
                        int w2 = d.macros[d.insts[id2].macroId].width;
                        if (w1 != w2) continue;
                        
                        // 計算交換前後的 HPWL 變化
                        // 簡化版：只計算這兩個 cell 相關的 nets
                        auto calcLocalHPWL = [&](int iid1, int x1, int iid2, int x2)->long long {
                        long long sum = 0;
                        std::vector<int> netsToCheck = inst2nets[iid1];
                        netsToCheck.insert(netsToCheck.end(), inst2nets[iid2].begin(), inst2nets[iid2].end());
                        std::sort(netsToCheck.begin(), netsToCheck.end());
                        netsToCheck.erase(std::unique(netsToCheck.begin(), netsToCheck.end()), netsToCheck.end());
                        
                        for (int nid : netsToCheck) {
                            const Net& net = d.nets[nid];
                            int xmin = std::numeric_limits<int>::max();
                            int xmax = std::numeric_limits<int>::min();
                            for (const auto& p : net.pins) {
                                int px;
                                if (p.isIO) {
                                    px = p.ioLoc.x;
                                } else if (p.instId == iid1) {
                                    px = x1;
                                } else if (p.instId == iid2) {
                                    px = x2;
                                } else if (p.instId >= 0) {
                                    px = d.insts[p.instId].x;
                                } else continue;
                                xmin = std::min(xmin, px);
                                xmax = std::max(xmax, px);
                            }
                            sum += (xmax - xmin);
                        }
                            return sum;
                        };
                        
                        int x1 = d.insts[id1].x;
                        int x2 = d.insts[id2].x;
                        long long hpwl_before = calcLocalHPWL(id1, x1, id2, x2);
                        long long hpwl_after  = calcLocalHPWL(id1, x2, id2, x1);
                        
                        if (hpwl_after < hpwl_before) {
                            // 交換 x 座標
                            std::swap(d.insts[id1].x, d.insts[id2].x);
                            ++swappedPairs;
                        }
                    }
                }
            }
            if (cfg.verbose && swappedPairs > 0) {
                std::cout << "  [Swap pass] swapped " << swappedPairs << " pairs\n";
            }
        }
    }

    if (cfg.verbose) std::cout << "==== End detailPlacement ====\n";
}
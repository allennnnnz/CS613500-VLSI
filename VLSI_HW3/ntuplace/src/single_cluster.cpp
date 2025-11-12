#include "dp_stage.hpp"
#include <algorithm>
#include <vector>
#include <limits>
#include <numeric>
#include <iostream>
#include <iomanip>

// ================================================================
// Single-Segment Clustering  (Pan et al., 2005)
// ================================================================
bool performSingleSegClustering(Design& d)
{
    int64_t hpwl_before = d.totalHPWL();
    std::vector<PointI> backup_xy(d.insts.size());
    for (size_t i = 0; i < d.insts.size(); ++i)
        backup_xy[i] = { d.insts[i].x, d.insts[i].y };

    for (const auto& row : d.layout.rows)
    {
        // --- collect fixed & movable ---
        struct Item { int id, x, w; bool fixed; };
        std::vector<Item> fixeds, movs;
        for (int i = 0; i < (int)d.insts.size(); ++i)
        {
            const auto& I = d.insts[i];
            if (I.y != row.originY) continue;
            int w = d.macros[I.macroId].width;
            if (I.fixed) fixeds.push_back({i, I.x, w, true});
            else          movs.push_back({i, I.x, w, false});
        }
        if (movs.empty()) continue;

        std::sort(fixeds.begin(), fixeds.end(), [](auto&a,auto&b){return a.x<b.x;});
        std::sort(movs.begin(), movs.end(),   [](auto&a,auto&b){return a.x<b.x;});

        int rowL = row.originX;
        int rowR = row.originX + row.xStep * row.numSites;

        // --- build segments between fixed cells ---
        std::vector<std::pair<int,int>> segs;
        int curL = rowL;
        for (auto& f : fixeds) {
            segs.emplace_back(curL, f.x);
            curL = f.x + f.w;
        }
        segs.emplace_back(curL, rowR);

        // --- for each segment ---
        for (auto [L, R] : segs) {
            if (R <= L) continue;

            // collect movables in this segment (keep order!)
            std::vector<int> ids;
            for (auto& m : movs)
                if (d.insts[m.id].x + d.macros[d.insts[m.id].macroId].width > L &&
                    d.insts[m.id].x < R)
                    ids.push_back(m.id);
            if (ids.empty()) continue;

            // --- Step 1: compute total width ---
            int totalW = 0;
            for (int id : ids)
                totalW += d.macros[d.insts[id].macroId].width;

            // --- Step 2: linear compaction ---
            int freeSpace = (R - L) - totalW;
            if (freeSpace < 0) freeSpace = 0;

            double avgSpace = (ids.size() > 1)
                                ? (double)freeSpace / (ids.size() - 1)
                                : (double)freeSpace;

            // --- Step 3: place sequentially (keep order, align to site) ---
            int curX = L;
            for (int id : ids) {
                const int w = d.macros[d.insts[id].macroId].width;
                int site = row.originX + ((curX - row.originX)/row.xStep)*row.xStep;
                site = std::max(L, std::min(site, R - w));
                d.insts[id].x = site;
                d.insts[id].y = row.originY;
                curX = site + w + (int)avgSpace;
                if (curX > R) break;
            }
        }
    }

    // --- evaluate ---
    int64_t hpwl_after = d.totalHPWL();
    double ratio = 100.0 * (hpwl_before - hpwl_after) / hpwl_before;

    // --- guard: revert if HPWL worsened ---
    if (hpwl_after > hpwl_before * 1.1) {
        for (size_t i = 0; i < d.insts.size(); ++i) {
            d.insts[i].x = backup_xy[i].x;
            d.insts[i].y = backup_xy[i].y;
        }
        // silent revert to avoid noisy logs
        return false;
    }

    std::cout << "  [SingleSegCluster] HPWL=" << hpwl_after
              << " (Δ=" << std::fixed << std::setprecision(2)
              << ratio << "%)\n";
    return (hpwl_after < hpwl_before);
}
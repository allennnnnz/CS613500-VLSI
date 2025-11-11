#include "design.hpp"
#include <vector>
#include <algorithm>
#include <iostream>
#include <limits>

// ================================================================
// Advanced Single-Segment Clustering (Pan et al., 2005, Fig. 4)
// ================================================================
bool performSingleSegmentClusteringWeighted(Design& d)
{
    if (d.segments.empty()) {
        std::cerr << "[SingleSegCluster] No segments.\n";
        return false;
    }

    bool improved = false;
    int clusterCount = 0;

    // for every segment
    for (auto& seg : d.segments) {
        auto& ids = seg.cellIds;
        if (ids.empty()) continue;

        std::sort(ids.begin(), ids.end(),
                  [&](int a, int b){ return d.insts[a].x < d.insts[b].x; });

        struct Cluster {
            std::vector<int> cells;
            double optCenter;  // Optimal Region Center
            int width;
            int leftBound, rightBound;
        };
        std::vector<Cluster> clusters;

        // 1️⃣ initial clusters
        for (int cid : ids) {
            const auto& inst = d.insts[cid];
            const auto& mc = d.macros[inst.macroId];
            int width = mc.width;

            // ─ get optimal region center: average of nets’ bbox centers ─
            double xmin = std::numeric_limits<double>::max();
            double xmax = -std::numeric_limits<double>::max();
            for (int nid : d.inst2nets[cid]) {
                const auto& net = d.nets[nid];
                if (net.isSpecial || net.pins.empty()) continue;
                int nxmin = std::numeric_limits<int>::max();
                int nxmax = std::numeric_limits<int>::min();
                for (const auto& p : net.pins) {
                    if (p.instId < 0) continue;
                    if (p.instId == cid) continue;
                    const auto& I = d.insts[p.instId];
                    nxmin = std::min(nxmin, I.x);
                    nxmax = std::max(nxmax, I.x);
                }
                if (nxmin <= nxmax) {
                    xmin = std::min(xmin, (double)nxmin);
                    xmax = std::max(xmax, (double)nxmax);
                }
            }
            if (xmin == std::numeric_limits<double>::max()) {
                xmin = seg.x1; xmax = seg.x2;
            }
            double opt = (xmin + xmax) / 2.0;
            opt = std::clamp(opt, (double)seg.x1, (double)(seg.x2 - width));

            clusters.push_back({{cid}, opt, width, (int)xmin, (int)xmax});
        }

        // 2️⃣ merge overlapping clusters
        for (size_t i = 1; i < clusters.size(); ++i) {
            auto& cur = clusters[i];
            auto& prev = clusters[i - 1];
            if (cur.optCenter < prev.optCenter + prev.width) {
                // overlap → merge
                Cluster merged;
                merged.cells = prev.cells;
                merged.cells.insert(merged.cells.end(),
                                    cur.cells.begin(), cur.cells.end());
                merged.width = prev.width + cur.width;
                merged.optCenter =
                    (prev.optCenter * prev.width + cur.optCenter * cur.width)
                    / merged.width;
                merged.optCenter = std::clamp(
                    merged.optCenter,
                    (double)seg.x1, (double)(seg.x2 - merged.width));
                clusters[i - 1] = merged;
                clusters.erase(clusters.begin() + i);
                i = std::max<size_t>(1, i) - 1; // recheck with previous
            }
        }

        clusterCount += clusters.size();

        // 3️⃣ place clusters (legalize)
        int curX = seg.x1;
        for (auto& cl : clusters) {
            int startX = (int)std::clamp(cl.optCenter,
                                         (double)curX,
                                         (double)(seg.x2 - cl.width));
            for (int cid : cl.cells) {
                auto& inst = d.insts[cid];
                const auto& mc = d.macros[inst.macroId];
                inst.x = startX;
                startX += mc.width;
            }
            curX = startX;
        }

        improved = true;
    }

    std::cout << "[SingleSegCluster-W] clusters=" << clusterCount << std::endl;
    return improved;
}

bool performSingleSegmentClustering(Design& d) {
    return performSingleSegmentClusteringWeighted(d);
}
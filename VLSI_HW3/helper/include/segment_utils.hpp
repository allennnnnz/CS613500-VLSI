#pragma once
#include "placer.hpp"
#include <vector>

struct Segment {
    int rowId;
    int xL, xR;
    std::vector<int> cells;
};

namespace fastdp {
void buildAdjacency(db::Database& db);
std::vector<Segment> buildSegments(db::Database& db);
void legalizeSegmentOrder(db::Database& db, Segment& sg);
int snapToSite(int x, const db::Row& r);
}
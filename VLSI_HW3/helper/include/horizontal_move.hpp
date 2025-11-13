#pragma once
#include "placer.hpp"

namespace fastdp {
// 單細胞水平移動：嘗試將每顆 cell 移到其目標 x 附近的最近合法空隙，若僅針對受影響網路 HPWL 有改善則接受。
bool performHorizontalMove(db::Database& db);
}

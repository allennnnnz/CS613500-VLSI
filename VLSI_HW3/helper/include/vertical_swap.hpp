#pragma once
#include "placer.hpp"

namespace fastdp {

// 嘗試跨相鄰 row 的交換以降低 y 向 HPWL；僅接受嚴格改善且保持合法
bool performVerticalSwap(db::Database& db);

// 嘗試將單顆 cell 搬移到相鄰 row 的最近合法 x 位置（最靠近其目標 x 中心），若嚴格改善則接受
bool performVerticalRelocate(db::Database& db);

}

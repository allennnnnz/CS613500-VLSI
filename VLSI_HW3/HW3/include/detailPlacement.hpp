#pragma once
#include "design.hpp"
#include <cstdint>

// 簡易詳細擺放主流程（含合法化與局部優化）
void detailPlacement(Design& d, int maxIter);

// 向後相容的封裝（忽略 deltaX）
inline void simpleDetailPlacement(Design& d, int maxIter, int /*deltaX_micron*/) {
	detailPlacement(d, maxIter);
}
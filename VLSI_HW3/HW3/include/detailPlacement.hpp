#pragma once
#include "design.hpp"
#include <cstdint>

// 我們的 detail placement 主流程
void simpleDetailPlacement(Design& d, int maxIter, int deltaX_micron);
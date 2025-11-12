#pragma once
#include "design.hpp"

// 針對 HPWL 優化的 detail placement
void run_hpwl_optimization(Design& d);
void final_legalize_rows(Design& d);
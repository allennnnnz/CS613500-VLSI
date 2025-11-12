#pragma once
#include "design.hpp"

// Pan et al. 2005 style global swap (opt-region based)
bool performGlobalSwap_OptRegion(Design& d,
                                 int windowSites = 200,
                                 int neighborK  = 2);
#pragma once
#include "placer.hpp"

namespace fastdp {
void optimize(db::Database& db, int windowSize = 3, int maxPass = 3);
}
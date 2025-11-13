#pragma once
#include "placer.hpp"

namespace fastdp {
void optimize(db::Database& db, int windowSize = 6, int maxPass = 5);
}
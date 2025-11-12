#pragma once
#include "placer.hpp"

namespace fastdp {
void optimize(db::Database& db, int windowSize = 4, int maxPass = 10);
}
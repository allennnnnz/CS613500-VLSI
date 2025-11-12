#pragma once
#include "placer.hpp"

namespace fastdp {
bool performLocalReorder(db::Database& db, int windowSize = 3);
}
#pragma once
#include "placer.hpp"

namespace fastdp {

// 以目標位置排序（net bbox 中心）對 segment 進行重排，僅接受嚴格改善
bool performGlobalReorder(db::Database& db);

}

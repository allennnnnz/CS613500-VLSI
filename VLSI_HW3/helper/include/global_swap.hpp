#pragma once
#include "placer.hpp"

namespace fastdp {

// 執行全域交換（嘗試不同 row 與區間的 cell 互換）
bool performGlobalSwap(db::Database& db);

}
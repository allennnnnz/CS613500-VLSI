#pragma once
#include "design.hpp"
#include <string>
#include <vector>

struct IOPin {
    std::string netName; // 所屬 net 名稱
    int x;               // 位置（DBU）
    int y;               // 位置（DBU）
};

// 收集所有 IO pins（不重複）
std::vector<IOPin> gatherIOPins(const Design& d);

// 列印 IO pins 的統計資訊
void printIOStats(const Design& d);

// 將 IO pins 輸出為 CSV，格式：net_name,x,y（DBU）
bool dumpIOToCSV(const Design& d, const std::string& csvPath);

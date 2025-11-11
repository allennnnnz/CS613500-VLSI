#pragma once
#include <string>
#include "design.hpp"

// LEF：讀取 Macro 尺寸、Site 尺寸、Pin 代表點
bool loadLEF(const std::string& lefPath, Design& d);

// DEF：讀 Units、DieArea、Rows、Components、Pins(IO座標)、Nets
bool loadDEF(const std::string& defPath, Design& d);

// DEF Writer：至少寫出 HEADER + DIEAREA + COMPONENTS（供 detail placement 回寫）
bool writeDEF(const std::string& defPath, const Design& d);

// DEF Writer（保留原檔除 COMPONENTS:+ PLACED 外皆不變）
bool writeDEFPreserve(const std::string& inDefPath, const std::string& outDefPath, const Design& d);
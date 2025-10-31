#include "design_io.hpp"
#include "detailPlacement.hpp"
#include <iostream>

int main(){
    Design d;
    const std::string lef = "../testcase/public1.lef";
    const std::string def = "../testcase/public1.def";
    const std::string out = "../output/public1.out";

    if(!loadLEF(lef, d)) return 1;
    if(!loadDEF(def, d)) return 1;

    if(!d.macros.empty())
        std::cout<<"macro[0] "<<d.macros[0].name<<" size=("<<d.macros[0].width<<","<<d.macros[0].height<<")\n";
    if(!d.sites.empty())
        std::cout<<"site[0] "<<d.sites[0].name<<" size=("<<d.sites[0].sizeX<<","<<d.sites[0].sizeY<<")\n";

    // 執行詳細擺放（合法化 + 簡易局部優化）
    detailPlacement(d, /*maxIter*/2);

    // 以「保留模式」回寫 DEF：完全複製 input DEF，僅更新 COMPONENTS 的 + PLACED 座標與方向
    if(!writeDEFPreserve(def, out, d)) return 1;
    return 0;
}
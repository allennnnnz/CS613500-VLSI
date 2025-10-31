#include "design_io.hpp"
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

    auto hpwl0 = d.totalHPWL();
    std::cout<<"HPWL before = "<<hpwl0<<"\n";

    // demo：移動第一顆可放置的 cell + 吸附 row grid
    for (auto& inst : d.insts){
        if (!inst.fixed) {
            const int w = (inst.macroId>=0)? d.macros[inst.macroId].width : 0;
            auto snapped = d.snapToRowGrid(inst.x + 200, inst.y, w);
            inst.x = snapped.x; inst.y = snapped.y;
            break;
        }
    }

    auto hpwl1 = d.totalHPWL();
    std::cout<<"HPWL after  = "<<hpwl1<<"\n";

    if(!writeDEF(out, d)) return 1;
    return 0;
}
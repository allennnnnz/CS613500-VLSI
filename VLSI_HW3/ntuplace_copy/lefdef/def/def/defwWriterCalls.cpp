#include <iostream>
#include <string>
#include <vector>
#include "defrReader.hpp"
#include "defwWriter.hpp"

// --------------------- 資料結構 ---------------------
struct Component {
    std::string instName;
    std::string macroName;
    std::string orient;
    std::string status;
    int x, y;
};
std::vector<Component> g_components;

// --------------------- Reader Callbacks ---------------------
int componentCallback(defrCallbackType_e, defiComponent* comp, defiUserData) {
    Component c;
    c.instName = comp->id();
    c.macroName = comp->name();
    c.orient = "N";
    c.x = comp->placementX();
    c.y = comp->placementY();

    if (comp->isFixed()) c.status = "FIXED";
    else if (comp->isPlaced()) c.status = "PLACED";
    else c.status = "UNPLACED";

    g_components.push_back(c);
    return 0;
}

// --------------------- Main ---------------------
int main() {
    const char* in_def  = "../testcase/public1.def";
    const char* out_def = "../output/public1.out";

    // ===== 1. Parse DEF =====
    FILE* defFile = fopen(in_def, "r");
    if (!defFile) { std::cerr << "❌ Cannot open input DEF.\n"; return 1; }

    defrInit();
    defrSetComponentCbk(componentCallback);
    defrRead(defFile, in_def, NULL, 1);
    fclose(defFile);
    defrClear();

    std::cout << "✅ DEF parsed successfully, components read: "
              << g_components.size() << std::endl;

    // ===== 2. Write DEF via API =====
    FILE* fout = fopen(out_def, "w");
    if (!fout) { std::cerr << "❌ Cannot open output DEF.\n"; return 1; }

    defwInitCbk(fout);

    defwVersion(5, 8);          defwNewLine();
    defwDividerChar("/");       defwNewLine();
    defwBusBitChars("[]");      defwNewLine();
    defwDesign("public1");      defwNewLine();
    defwUnits(2000);            defwNewLine();

    // === COMPONENTS ===
    defwStartComponents((int)g_components.size());
    for (auto& c : g_components) {
        defwComponent(c.instName.c_str(), c.macroName.c_str());
        if (c.status == "FIXED" || c.status == "PLACED")
            defwComponentPlacement(c.status.c_str(), c.x, c.y, c.orient.c_str());
        defwEndComponent();
    }
    defwEndComponents();
    // === END COMPONENTS ===

    defwEndDesign();
    fclose(fout);

    std::cout << "✅ DEF written successfully to: " << out_def << std::endl;
    return 0;
}
#include "design_io.hpp"
#include <iostream>
#include <cmath>
#include "lefrReader.hpp"

using std::string;

static Design* G = nullptr;
static int LEF_DBU = 1; // LEF UNITS DATABASE MICRONS, default 1 if not specified

static int unitsCb(lefrCallbackType_e, lefiUnits* u, lefiUserData) {
    if (u && u->hasDatabase()) {
        // In LEF, units are typically specified as DATABASE MICRONS <dbu>;
        // Scale all geometry by this factor to store in DBU (int).
        LEF_DBU = u->databaseNumber();
        if (G) G->dbuPerMicron = LEF_DBU; // keep in sync if DEF not loaded yet; DEF will overwrite later
    }
    return 0;
}

static int siteCb(lefrCallbackType_e, lefiSite* s, lefiUserData) {
    Site S; S.name = s->name();
    // LEF provides microns; scale by LEF_DBU to DBU ints
    S.sizeX = (int)std::lround(s->sizeX() * LEF_DBU);
    S.sizeY = (int)std::lround(s->sizeY() * LEF_DBU);
    G->sites.push_back(S);
    return 0;
}

static int macroCb(lefrCallbackType_e, lefiMacro* m, lefiUserData) {
    Macro M; M.name = m->name();
    if (m->hasSize()) {
        M.width  = (int)std::lround(m->sizeX() * LEF_DBU);
        M.height = (int)std::lround(m->sizeY() * LEF_DBU);
    }
    G->macros.push_back(M);
    return 0;
}

static int pinCb(lefrCallbackType_e, lefiPin* p, lefiUserData) {
    if (G->macros.empty()) return 0;
    Macro& M = G->macros.back();
    LibPin lp; lp.name = p->name();

    if (p->numPorts() > 0) {
        // 在此版本 API，lefiPin::port(i) 直接回傳 lefiGeometries*
        lefiGeometries* geo = p->port(0);
        if (geo) {
            for (int i = 0; i < geo->numItems(); ++i) {
                if (geo->itemType(i) == lefiGeomRectE) {
                    auto r = geo->getRect(i);
                    int cx = (int)std::lround(((r->xl + r->xh) / 2.0) * LEF_DBU);
                    int cy = (int)std::lround(((r->yl + r->yh) / 2.0) * LEF_DBU);
                    lp.shapes.push_back(LibPinShape{ PointI{cx, cy} });
                    break;
                }
            }
        }
    }

    if (lp.shapes.empty())
        lp.shapes.push_back(LibPinShape{ PointI{ M.width / 2, M.height / 2 } });

    int idx = (int)M.pins.size();
    M.pinName2Idx[lp.name] = idx;
    M.pins.push_back(std::move(lp));
    return 0;
}

bool loadLEF(const std::string& lefPath, Design& d) {
    FILE* f = fopen(lefPath.c_str(), "r");
    if (!f) { std::cerr << "❌ Cannot open LEF " << lefPath << "\n"; return false; }

    G = &d;
    LEF_DBU = 1;
    d.macros.clear(); d.sites.clear();

    lefrInit();
    lefrSetUnitsCbk(unitsCb);
    lefrSetSiteCbk(siteCb);
    lefrSetMacroCbk(macroCb);
    lefrSetPinCbk(pinCb);

    lefrRead(f, lefPath.c_str(), nullptr);
    fclose(f);
    lefrClear();

    d.buildIndices();
    std::cout << "✅ LEF: macros=" << d.macros.size() << ", sites=" << d.sites.size() << "\n";
    return true;
}
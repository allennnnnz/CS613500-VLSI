#pragma once
#include <bits/stdc++.h>
using namespace std;

namespace db {

struct MacroInfo {
    double width, height; // in micron
};

struct SiteInfo {
    double width, height;
};

struct Row {
    string name, siteName;
    int x, y;
    int siteCount;
    int stepX, stepY;
    string orient;
};

struct Cell {
    string instName, macroName;
    int x, y;
    int width = 0, height = 0;
    string orient;
    bool fixed;
};

struct Net {
    string name;
    vector<string> pins; // instance.pin or PIN pinName
    vector<int> cellIds;
    vector<int> ioIds;
};

struct Pin {
    string name;
    int x, y;
};


struct Database {
    double lefDBMicron = 1.0;
    double defDBMicron = 1.0;
    string inputDEF;
    SiteInfo coreSite;
    unordered_map<string, MacroInfo> macros;
    vector<Row> rows;
    vector<Cell> cells;
    vector<Net> nets;
    vector<Pin> pins;
    vector<vector<int>> cell2nets;
    vector<array<int,4>> netBBox; // xmin,xmax,ymin,ymax
};

} // namespace db
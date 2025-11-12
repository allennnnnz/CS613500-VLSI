#include "placer.hpp"
#include <cstddef>
using namespace std;

namespace parser {
void readLEF(const string &path, db::Database &db) {
    ifstream fin(path);
    string line, tok;
    string curMacro;
    bool inMacro = false;

    while (getline(fin, line)) {
        // 去除註解
        size_t pos = line.find('#');
        if (pos != string::npos) line = line.substr(0, pos);
        if (line.empty()) continue;

        stringstream ss(line);
        ss >> tok;

        if (tok == "UNITS") {
            while (getline(fin, line)) {
                if (line.find("DATABASE MICRONS") != string::npos) {
                    double val = 0;
                    if (sscanf(line.c_str(), " DATABASE MICRONS %lf", &val) == 1)
                        db.lefDBMicron = val;
                }
                if (line.find("END UNITS") != string::npos)
                    break;
            }
        }
        else if (tok == "SITE") {
            // e.g., SITE CoreSite
            string siteName; ss >> siteName;
            // 忽略 CLASS 與其他行，只在 SIZE 行更新寬高
            while (getline(fin, line)) {
                if (line.find("SIZE") != string::npos) {
                    double w, h;
                    // char by[4];
                    sscanf(line.c_str(), " SIZE %lf BY %lf", &w, &h);
                    db.coreSite.width = w;
                    db.coreSite.height = h;
                }
                if (line.find("END") != string::npos) break;
            }
        }
        else if (tok == "MACRO") {
            // 開始一個 macro 區塊
            ss >> curMacro;
            db.macros[curMacro] = {};
            inMacro = true;
        }
        else if (tok == "END" && inMacro) {
            // MACRO 區塊結束
            string name; ss >> name;
            if (name == curMacro) {
                curMacro.clear();
                inMacro = false;
            }
        }
        else if (tok == "SIZE" && inMacro) {
            double w, h;
            // char by[4];
            sscanf(line.c_str(), " SIZE %lf BY %lf", &w, &h);
            db.macros[curMacro].width = w;
            db.macros[curMacro].height = h;
        }
    }

    fin.close();
    // cerr << "[LEF] parsed " << db.macros.size() 
    //      << " macros, unit=" << db.lefDBMicron 
    //      << " DBU/μm" << endl;
}
void readDEF(const string &path, db::Database &db) {
    db.inputDEF = path;
    ifstream fin(path, ios::in);
    if (!fin.is_open()) {
        cerr << "Cannot open DEF file: " << path << endl;
        exit(1);
    }

    string line;
    enum State { NONE, COMPONENTS, NETS, PINS } state = NONE;

    db.cells.reserve(800000);
    db.nets.reserve(400000);
    db.pins.reserve(100000);

    db::Cell cell;
    db::Net net;
    // size_t lineCount = 0;

    string pinBlock;

    while (getline(fin, line)) {
        if (line.empty() || line[0] == '#') continue;
        // if (++lineCount % 500000 == 0)
        //     cerr << "[DEF] parsed " << lineCount/1000 << "k lines...\r" << flush;
        // --- UNITS ---
        if (line.find("UNITS DISTANCE MICRONS") != string::npos) {
            sscanf(line.c_str(), " UNITS DISTANCE MICRONS %lf", &db.defDBMicron);
        }

        // --- ROW ---
        else if (line.rfind("ROW", 0) == 0) {  // row starts with ROW
            char rowName[256], site[64], orient[8];
            db::Row row;
            if (sscanf(line.c_str(),
                    "ROW %255s %63s %d %d %7s DO %d BY %*d STEP %d %d",
                    rowName, site, &row.x, &row.y, orient,
                    &row.siteCount, &row.stepX, &row.stepY) == 8)
            {
                row.name = rowName;
                row.siteName = site;
                row.orient = orient;
                db.rows.push_back(row);
            }
        }

        // --- COMPONENTS ---
        else if (line.rfind("COMPONENTS", 0) == 0) {
            state = COMPONENTS;
            continue;
        } 
        else if (state == COMPONENTS) {
            if (line.rfind("END COMPONENTS", 0) == 0) {
                state = NONE;
                continue;
            }

            // 找開頭: "- instName MacroName"
            if (line.find("- ") != string::npos) {
                cell = {};
                char inst[256] = "", macro[256] = "";
                int matched = sscanf(line.c_str(), " - %255s %255s", inst, macro);
                if (matched == 2) {
                    cell.instName = inst;
                    cell.macroName = macro;
                } else {
                    cerr << "[WARN] Unrecognized component header: " << line << endl;
                    continue;
                }

                // 看這行是否同時有 "+ PLACED" 或 "+ FIXED"
                if (line.find("+ PLACED") != string::npos || line.find("+ FIXED") != string::npos) {
                    int x = 0, y = 0;
                    char orient[8] = "";
                    if (line.find("FIXED") != string::npos) cell.fixed = true;

                    // 嘗試直接抓座標
                    if (sscanf(line.c_str(), "%*[^()] ( %d %d ) %7s", &x, &y, orient) >= 3) {
                        cell.x = x;
                        cell.y = y;
                        cell.orient = orient;
                        db.cells.push_back(cell);
                    } else {
                        cerr << "[WARN] Failed to parse inline PLACED/FIXED: " << line << endl;
                    }
                }
            }
            // 如果不是新 cell 開頭（多行的第二行）
            else if (line.find("+ PLACED") != string::npos || line.find("+ FIXED") != string::npos) {
                if (cell.instName.empty() || cell.macroName.empty()) continue;
                int x = 0, y = 0;
                char orient[8] = "";
                if (line.find("FIXED") != string::npos) cell.fixed = true;
                if (sscanf(line.c_str(), "%*[^()] ( %d %d ) %7s", &x, &y, orient) >= 3) {
                    cell.x = x;
                    cell.y = y;
                    cell.orient = orient;
                    db.cells.push_back(cell);
                } else {
                    cerr << "[WARN] Failed to parse PLACED line: " << line << endl;
                }
            }
        }


        if (line.rfind("PINS", 0) == 0) {
            state = PINS;
            pinBlock.clear(); // 初始化
            continue;
        }
        else if (state == PINS) {
            pinBlock += " " + line;
            if (line.find(';') != string::npos) {
                int x = 0, y = 0; char name[128] = "";
                if (sscanf(pinBlock.c_str(), " - %127s", name) == 1) {
                    size_t p2 = pinBlock.rfind(')');
                    size_t p1 = pinBlock.rfind('(', p2);
                    if (p1 != string::npos && p2 != string::npos && p2 > p1) {
                        string inside = pinBlock.substr(p1 + 1, p2 - p1 - 1);
                        stringstream ss(inside);
                        ss >> x >> y;
                        db.pins.push_back({name, x, y});
                    }
                }
                pinBlock.clear(); // 清空當前 pin
            }
            if (line.rfind("END PINS", 0) == 0) {
                state = NONE;
                pinBlock.clear(); // 清空整區
                continue;
            }
        }


        // --- NETS ---
        else if (line.rfind("NETS", 0) == 0) {
            state = NETS;
            continue;
        } 
        else if (state == NETS) {
            if (line.rfind("END NETS", 0) == 0) {
                state = NONE;
                continue;
            }

            // 每遇到新的 net 開頭
            if (line[0] == '-') {
                net = {};
                char name[256];
                sscanf(line.c_str(), " - %255s", name);
                net.name = name;
                // 可能當行就有 pin，例如 "- net123 ( inst1 Y ) ( inst2 A )"
                size_t firstParen = line.find('(');
                if (firstParen != string::npos) {
                    size_t pos = firstParen;
                    while (true) {
                        size_t p1 = line.find('(', pos);
                        if (p1 == string::npos) break;
                        size_t p2 = line.find(')', p1);
                        if (p2 == string::npos) break;
                        string inside = line.substr(p1 + 1, p2 - p1 - 1);
                        stringstream ss(inside);
                        string inst, pin;
                        ss >> inst >> pin;
                        cout << inst << endl;
                        if (inst == "PIN") {
                            // 外部 I/O pin → 用 pin 名稱儲存
                            // if (!pin.empty())
                            net.pins.push_back("PIN:" + pin);
                        } else if (!inst.empty()) {
                            // 一般 cell → 儲存 instance 名稱
                            net.pins.push_back(inst);
                        }
                        pos = p2 + 1;
                    }
                }
            }
            // 如果不是新 net 開頭，也不是 END NETS，就繼續累積 pin
            else if (line.find('(') != string::npos) {
                size_t pos = 0;
                while (true) {
                    size_t p1 = line.find('(', pos);
                    if (p1 == string::npos) break;
                    size_t p2 = line.find(')', p1);
                    if (p2 == string::npos) break;
                    string inside = line.substr(p1 + 1, p2 - p1 - 1);
                    stringstream ss(inside);
                    string inst, pin;
                    ss >> inst >> pin;
                    // if (inst == "PIN") {
                    //     pos = p2 + 1;
                    //     continue; // 忽略外部 pins
                    // }
                    if (inst == "PIN") {
                        // 外部 I/O pin → 用 pin 名稱儲存
                        // if (!pin.empty())
                        net.pins.push_back("PIN:" + pin);
                    } else if (!inst.empty()) {
                        // 一般 cell → 儲存 instance 名稱
                        net.pins.push_back(inst);
                    }
                    // if (!inst.empty()) net.pins.push_back(inst);
                    pos = p2 + 1;
                }
            }

            // 檢查 net 是否結束
            if (line.find(';') != string::npos) {
                db.nets.push_back(std::move(net));
            }
        }
    }
    fin.close();

    for (auto &cell : db.cells) {
        auto it = db.macros.find(cell.macroName);
        if (it != db.macros.end()) {
            cell.width = it->second.width * db.lefDBMicron; // convert to DBU
            cell.height = it->second.height * db.lefDBMicron;
        }
    }

    // cerr << "[DEF] rows=" << db.rows.size()
    //      << " cells=" << db.cells.size()
    //      << " nets=" << db.nets.size()
    //      << "  (unit=" << db.defDBMicron << " DBU/μm)" << endl;
    unordered_map<string, int> instToIdx;
    unordered_map<string, int> pinToIdx;
    for (int i = 0; i < db.cells.size(); ++i)
        instToIdx[db.cells[i].instName] = i;
    for (int i = 0; i < db.pins.size(); ++i)
        pinToIdx[db.pins[i].name] = i;


    for (auto &net : db.nets) {
        net.cellIds.clear();
        net.ioIds.clear();

        for (const string &p : net.pins) {
            if (p.rfind("PIN:", 0) == 0) {
                string pinName = p.substr(4);         // ← 去掉 "PIN:"
                auto it = pinToIdx.find(pinName);
                if (it != pinToIdx.end()) net.ioIds.push_back(it->second);
            }
            else {
                auto it = instToIdx.find(p);
                if (it != instToIdx.end()) net.cellIds.push_back(it->second);
            }
        }
        net.pins.clear();
    }
}

} // namespace parser
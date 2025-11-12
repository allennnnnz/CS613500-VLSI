#include <bits/stdc++.h>
#include "placer.hpp"   // 包含你的 db::Database 結構
#include "fastdp.hpp"
#include "wl.hpp"
using namespace std;

// 宣告 parser namespace (如果分開實作)
namespace parser {
    void readLEF(const string &path, db::Database &db);
    void readDEF(const string &path, db::Database &db);
}

void writeDEF(const string &outPath, const db::Database &db) {
    ifstream fin(db.inputDEF);
    if (!fin.is_open()) {
        cerr << "[ERROR] Cannot open original DEF: " << db.inputDEF << endl;
        return;
    }
    ofstream fout(outPath);
    if (!fout.is_open()) {
        cerr << "[ERROR] Cannot open output DEF: " << outPath << endl;
        return;
    }

    string line;
    bool inComponents = false;
    int compCount = (int)db.cells.size();
    fout << std::fixed;

    while (getline(fin, line)) {
        string s = line;
        // remove leading/trailing spaces
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        if (s.empty()) { fout << line << "\n"; continue; }

        if (!inComponents) {
            if (s.rfind("COMPONENTS", 0) == 0) {
                inComponents = true;
                fout << "COMPONENTS " << compCount << " ;\n";
                // write all cells
                for (auto &c : db.cells) {
                    fout << "  - " << c.instName << " " << c.macroName << "\n";
                    string placeType = c.fixed ? "FIXED" : "PLACED";
                    fout << "    + " << placeType << " ( "
                         << c.x << " " << c.y << " ) "
                         << c.orient << " ;\n";
                }
                fout << "END COMPONENTS\n";
                // skip old components in input
                while (getline(fin, line)) {
                    string t = line;
                    t.erase(0, t.find_first_not_of(" \t\r\n"));
                    if (t.rfind("END COMPONENTS", 0) == 0)
                        break;
                }
            } else {
                fout << line << "\n";
            }
        } else {
            // we already output components; continue copying remaining sections
            fout << line << "\n";
        }
    }

    fin.close();
    fout.close();
}

int main(int argc, char** argv) {
    // if (argc != 3) {
    //     cerr << "Usage: ./hw3 <input.lef> <input.def>\n";
    //     return 1;
    // }
    string lefPath = argv[1];
    string defPath = argv[2];
    string outpath = argv[3];

    db::Database db;
    parser::readLEF(lefPath, db);
    parser::readDEF(defPath, db);

    wl::initAllNetBBox(db);
    double initWL = wl::totalHPWL(db);
    cerr << "[Init HPWL] " << initWL << endl;

    fastdp::optimize(db);

    // 重新同步 bbox
    for (int nid = 0; nid < (int)db.nets.size(); ++nid)
        wl::recomputeNetBBox(db, nid, db.netBBox[nid]);

    double finalWL = wl::totalHPWL(db);
    cerr << "[Final HPWL] " << fixed << setprecision(0)
        << finalWL << "  Δ=" << (finalWL - initWL) / initWL * 100 << "%" << endl;

    writeDEF(outpath, db);
    return 0;
}
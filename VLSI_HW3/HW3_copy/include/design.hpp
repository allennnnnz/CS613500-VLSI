#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <cassert>

enum class Orient : uint8_t { N=0, S=1, E=2, W=3, FN=4, FS=5, FE=6, FW=7 };

struct PointI {
    int x, y;
    constexpr PointI() : x(0), y(0) {}
    constexpr PointI(int xx, int yy) : x(xx), y(yy) {}
};

struct RectI {
    int x1, y1, x2, y2;
    constexpr RectI() : x1(0), y1(0), x2(0), y2(0) {}
    constexpr RectI(int a, int b, int c, int d) : x1(a), y1(b), x2(c), y2(d) {}
};

struct LibPinShape { PointI ref; }; // 代表點（HPWL 夠用）
struct LibPin {
    std::string name;
    int layerId = -1;
    std::vector<LibPinShape> shapes; // 至少 1 個 ref
};

struct Macro {
    std::string name;
    int width=0, height=0;                           // DBU
    std::unordered_map<std::string,int> pinName2Idx; // pin 名 -> index
    std::vector<LibPin> pins;
};

struct Site { std::string name; int sizeX=0, sizeY=0; };

struct Row {
    std::string name;
    std::string siteName;
    int originX=0, originY=0;
    bool flip=false;            // FS/…
    int xStep=0;
    int numSites=0;
};

struct Inst {
    std::string name;
    int macroId=-1;
    int x=0, y=0;
    Orient orient=Orient::N;
    bool fixed=false;
    bool placed=false;
};

struct NetPinRef {
    int instId = -1;           // -1 代表 IO
    int libPinIdx = -1;        // 宏內 pin 索引（instId>=0時用）
    bool isIO = false;
    std::string ioName;
    PointI ioLoc;
};

struct Net {
    std::string name;
    std::vector<NetPinRef> pins;
    bool isSpecial = false; // SPECIALNETS should be excluded from HPWL
};

struct Layout { RectI dieArea; std::vector<Row> rows; };

struct Design {
    int dbuPerMicron = 2000;
    // DEF/Design name captured from input (for consistent output headers)
    std::string designName;

    std::vector<Macro> macros;
    std::unordered_map<std::string,int> macroName2Id;

    std::vector<Site>  sites;
    std::unordered_map<std::string,int> siteName2Id;

    std::vector<Inst>  insts;
    std::unordered_map<std::string,int> instName2Id;

    std::vector<Net>   nets;
    std::unordered_map<std::string,int> netName2Id;

    // IO pin absolute locations from PINS section (by pin name)
    std::unordered_map<std::string, PointI> ioPinLoc;

    Layout layout;

    void buildIndices() {
        macroName2Id.clear(); for (int i=0;i<(int)macros.size();++i) macroName2Id[macros[i].name]=i;
        siteName2Id.clear();  for (int i=0;i<(int)sites.size(); ++i)  siteName2Id[sites[i].name]=i;
        instName2Id.clear();  for (int i=0;i<(int)insts.size(); ++i)  instName2Id[insts[i].name]=i;
        netName2Id.clear();   for (int i=0;i<(int)nets.size();  ++i)  netName2Id[nets[i].name]=i;
    }

    static PointI rotateFlip(const PointI& p, Orient o, int w, int h) {
        int rx=p.x, ry=p.y;
        switch(o){
            case Orient::N:  return PointI{ rx,       ry       };
            case Orient::S:  return PointI{ w - rx,   h - ry   };
            case Orient::E:  return PointI{ h - ry,   rx       };
            case Orient::W:  return PointI{ ry,       w - rx   };
            case Orient::FN: return PointI{ w - rx,   ry       };
            case Orient::FS: return PointI{ rx,       h - ry   };
            case Orient::FE: return PointI{ h - ry,   w - rx   }; // ← 修正：第二個用 w
            case Orient::FW: return PointI{ ry,       rx       };
        }
        return PointI{rx,ry};
    }

    PointI pinAbsXY(int instId, int libPinIdx) const {
        const Inst& ins = insts[instId];
        const Macro& mc = macros[ins.macroId];
        const PointI refRel = (libPinIdx>=0 && !mc.pins[libPinIdx].shapes.empty())
                            ? mc.pins[libPinIdx].shapes[0].ref
                            : PointI{mc.width/2, mc.height/2}; // fallback center
        PointI off = rotateFlip(refRel, ins.orient, mc.width, mc.height);
        return PointI{ ins.x + off.x, ins.y + off.y };
    }

    int64_t netHPWL(int netId) const {
        const Net& net = nets[netId];
        if (net.isSpecial) return 0; // exclude special nets
        if (net.pins.empty()) return 0;
        int xmin= std::numeric_limits<int>::max(), ymin= xmin;
        int xmax=-std::numeric_limits<int>::max(), ymax= xmax;
        for (const auto& p : net.pins) {
            PointI q;
            if (p.isIO) {
                auto it = ioPinLoc.find(p.ioName);
                if (it != ioPinLoc.end()) q = it->second; else q = p.ioLoc; // fallback
            } else {
                // bottom-left corner per spec (not pin geometry)
                if (p.instId >= 0 && p.instId < (int)insts.size()) {
                    const Inst& ins = insts[p.instId];
                    q = PointI{ ins.x, ins.y };
                } else {
                    q = PointI{0,0};
                }
            }
            xmin = std::min(xmin, q.x); xmax = std::max(xmax, q.x);
            ymin = std::min(ymin, q.y); ymax = std::max(ymax, q.y);
        }
        return (int64_t)(xmax-xmin) + (int64_t)(ymax-ymin);
    }

    int64_t totalHPWL() const {
        int64_t s=0;
        for (int i=0;i<(int)nets.size();++i) s+=netHPWL(i);
        return s;
    }

    PointI snapToRowGrid(int x, int y, int widthDBU) const {
        int best=-1, bestDy=std::numeric_limits<int>::max();
        for (int i=0;i<(int)layout.rows.size();++i){
            int dy = std::abs(y - layout.rows[i].originY);
            if (dy<bestDy) { bestDy=dy; best=i; }
        }
        if (best<0) return PointI{ x, y };
        const Row& r = layout.rows[best];
        int dx = x - r.originX;
        if (r.xStep>0){
            int s = (dx>=0)? (dx + r.xStep/2)/r.xStep : (dx - r.xStep/2)/r.xStep;
            int xs = r.originX + s*r.xStep;
            int L = r.originX, R = r.originX + r.xStep*r.numSites - widthDBU;
            if (R<L) R=L;
            xs = std::max(L, std::min(xs, R));
            return PointI{ xs, r.originY };
        }
        return PointI{ x, r.originY };
    }

    bool isRoughLegal(int instId) const {
        const Inst& it = insts[instId];
        const Macro& mc = macros[it.macroId];
        for (const auto& r : layout.rows){
            if (it.y != r.originY) continue;
            int L=r.originX, R=r.originX + r.xStep*r.numSites - mc.width;
            if (it.x>=L && it.x<=R) return true;
        }
        return false;
    }
};
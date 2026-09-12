// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/shoot.h"

#include "formats/map2d.h"

#include <cmath>
#include <cstring>

namespace omk {
namespace {

// The binary's own table at 0x004CFA30, kept here as well as in
// `tables/shoot_ai.json` because it is fourteen short strings and a replica
// that cannot name a type prints numbers. The JSON is the authority and
// `verify.py: exe tables` re-derives it; this must agree with it, which the
// sweep asserts rather than assumes.
const char* const kTypeNames[kCharTypeCount] = {
    "No one", "Man passer", "Woman passer", "Man enemy", "Woman enemy",
    "Mecagarde", "Mecadog", "X-Tech", "Z-Tech", "Incarnable",
    "Gandhar", "Zombie", "Spectre", "Astaroth",
};

// Astaroth's state graph, read from `sub_4800C0`'s switch on `+156`.
//
// The distances are his own literals, in world units (1 unit ~ 2.54 cm):
// 195 is where he closes to grapple, 273 the range of the throw in state 19,
// and 78 / 156 the two bands that pick its impulse. The two timers are float
// literals stored as bit patterns in the decompilation - 0x43160000 is 150.0
// and 0x42700000 is 60.0, five seconds and two at 30 Hz.
const std::vector<ShootEdge> kAstarothEdges = {
    {16, 17, "closed to within 195 units of the target"},
    {17, 18, "the grapple animation landed"},
    {18, 19, "the hold landed"},
    {19, 21, "the throw landed - and inside 273 units it also pushes the "
             "target by 3700 (<78), 2300 (<156) or 1200 units"},
    {20, 21, "recovered"},
    {20, 16, "the 150-frame timer ran out first"},
    {21, 16, "the recovery animation finished; the timer resets to 60 frames"},
    {29, 16, "and only when the global counter has reached 6"},
};
const std::vector<int> kAstarothStates = {16, 17, 18, 19, 20, 21, 27, 29};

// The generic shooter's state graph, read from `sub_424DE0`'s switch. The
// per-state geometry - the aiming, the projectile spawn, the cover search -
// is 1500 lines of decompiler output and is NOT ported; what is here is what
// the machine DECIDES. State 6 is the fight itself and carries a five-way
// sub-switch of its own; 9 and 28 share one arm.
const std::vector<ShootEdge> kGenericEdges = {
    { 1,  2, "the navigation node changed under it"},
    { 1,  6, "the node it was walking to is the one it wanted"},
    { 2,  6, "arrived"},
    { 4,  5, ""},
    { 5,  4, ""},
    { 6, 10, "out of the fight - also reached from sub-case 2"},
    {10, 11, ""},
    {11, 10, ""},
    {14,  4, ""},
};
const std::vector<int> kGenericStates = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,28};

}  // namespace

namespace {
// `sub_435810` (0x00435810): world -> cell on one floor, -1 (false) off it.
// The bounds carry half a cell of tolerance - `x < minX - (-0.5 * scale)` and
// `(dx - (-0.5 * scale)) / scale >= W` refuse - while the cell itself is the
// plain truncation of `dx / scale`.
bool wallCellOf(const Map2dFloor& f, double scale, double x, double z, int& cx, int& cz) {
    const double v8 = scale * -0.5;
    const double v4 = x - f.bound[0], v5 = z - f.bound[4];
    if (x < double(f.bound[0]) - v8) return false;
    if (static_cast<long long>((v4 - v8) / scale) >= static_cast<long long>(f.w)) return false;
    if (double(f.bound[4]) - v8 > z) return false;
    if (static_cast<long long>((v5 - v8) / scale) >= static_cast<long long>(f.h)) return false;
    cx = static_cast<int>(static_cast<long long>(v4 / scale));
    cz = static_cast<int>(static_cast<long long>(v5 / scale));
    return true;
}
// `sub_4357B0` (0x004357B0): the cell's CENTRE, `c * scale + min + scale/2`.
void wallCentre(const Map2dFloor& f, double scale, int cx, int cz, float out[2]) {
    const double v4 = scale * -0.5;
    out[0] = static_cast<float>(double(cx) * scale + f.bound[0] - v4);
    out[1] = static_cast<float>(double(cz) * scale + f.bound[4] - v4);
}
}  // namespace

bool shootThink(ShootRecord& r, const Map2d& map, const float pos[3],
                int characterType, int excludeFloor) {
    // `if (a3 == 10) v8 = 1; else v8 = sub_435020(x, y, z, a4)`
    const int f = characterType == 10
                      ? 1
                      : map.floorAt(pos[0], pos[1], pos[2], excludeFloor);
    r.node = static_cast<std::int8_t>(f);        // `u8(v5, 188) = v8`, a BYTE
    if (f < 0) return false;
    int cx = 0, cz = 0;
    if (!map.cellAt(f, pos[0], pos[2], cx, cz)) return false;
    if (r.state == 2) return false;              // `if (u32(v5, 156) == 2) return 0`
    if (map.blocked(f, cx, cz)) return false;    // `sub_4353E0`
    r.destX = cx;
    r.destZ = cz;
    return true;
}

int shootWallTest(ShootRecord& r, const Map2d& map, float x0, float z0, float dx, float dz,
                  int steps, float snap[2]) {
    // `if (u32(rec, 156) == 2) return 0;` - the edge walk is not tested
    if (r.state == 2) return 0;
    const int fl = static_cast<int>(static_cast<std::int8_t>(r.node & 0xFF));
    // (off every floor the engine would index its floor table at -1; there
    // is nothing to test against, so the move is let through)
    if (!map.valid() || fl < 0 || fl >= static_cast<int>(map.floors().size()) || steps <= 0)
        return 0;
    const Map2dFloor& f = map.floors()[static_cast<std::size_t>(fl)];
    const double scale = map.scale();
    // `v11 = v11 + f32(v4, 8)` - the walk accumulates in floats
    float x = x0, z = z0;
    int cx = 0, cz = 0;
    for (int count = 0;;) {
        x = x + dx;
        z = z + dz;
        if (!wallCellOf(f, scale, x, z, cx, cz)) return -1;
        const auto v8 = static_cast<signed char>(f.cell(cx, cz));
        if (v8 == static_cast<signed char>(0x80) || v8 == 0 || v8 == 2 || v8 == 3) {
            wallCentre(f, scale, r.destX, r.destZ, snap);
            return static_cast<int>(v8) + 1;
        }
        if (++count >= steps) break;
    }
    // LABEL_12: the landing is his cell now
    if (r.destX == cx && r.destZ == cz) {
        r.flags &= ~0x20000u;
    } else {
        r.destX = cx;
        r.destZ = cz;
        r.flags |= 0x20000u;
    }
    wallCentre(f, scale, cx, cz, snap);
    return 0;
}

void ShootField::seed(const Map2d& map, int floor, int cx, int cz) {
    // `if (a1 != -1)`
    if (floor < 0 || floor >= static_cast<int>(map.floors().size())) return;
    const Map2dFloor& f = map.floors()[static_cast<std::size_t>(floor)];
    // `LOBYTE(dword_52BA48) = u8(v5, 24); LOBYTE(dword_52BA38) = u8(v5, 28)`
    w_ = static_cast<int>(f.w & 0xFFu);
    h_ = static_cast<int>(f.h & 0xFFu);
    const std::size_t n = static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_);
    for (auto& g : grid_)
        if (g.size() < n) g.resize(n, 0xFF);
    // `v6 = dword_52BA40[v4]; memset(v6, 0xFF, w*h)` - the grid to build
    build_ = next_;
    std::fill(grid_[build_].begin(), grid_[build_].begin() + static_cast<std::ptrdiff_t>(n),
              std::uint8_t{0xFF});
    gridFloor_[build_] = floor;                 // `byte_52B968[v4] = a1`
    next_ = (build_ - 1) & 1;                   // `dword_52BA58 = (v4 - 1) & 1`
    read_ = next_;                              // `dword_52C3F0 = dword_52BA40[dword_52BA58]`
    // the queue: the seed as its first pair, read at 0, written at 2
    queue_.assign(2048, 0);
    queue_[0] = static_cast<std::uint8_t>(cx);
    queue_[1] = static_cast<std::uint8_t>(cz);
    rd_ = 0;
    wr_ = 2;
    wrapped_ = false;
    const std::size_t c = static_cast<std::size_t>(cx) + static_cast<std::size_t>(w_) *
                          static_cast<std::size_t>(cz);
    if (c < n) grid_[build_][c] = 0;
    floor_ = floor;
    seeded_ = true;
}

bool ShootField::expand(const Map2d& map) {
    if (!seeded_ || floor_ < 0 || floor_ >= static_cast<int>(map.floors().size())) return false;
    // `dword_52BA50 = dword_907DE0[a1]` - the floor's LIVE cells, stamps and all
    const Map2dFloor& f = map.floors()[static_cast<std::size_t>(floor_)];
    std::vector<std::uint8_t>& g = grid_[build_];
    const int w = w_;
    const std::size_t qEnd = queue_.size();
    auto refused = [&](int idx) {
        if (idx < 0 || static_cast<std::size_t>(idx) >= f.cells.size()) return true;
        const auto b = static_cast<signed char>(f.cells[static_cast<std::size_t>(idx)]);
        return b == 0 || b == 2 || b == 3 || b == -128;
    };
    std::size_t wr = wr_;
    auto push = [&](int nx, int nz) {
        queue_[wr] = static_cast<std::uint8_t>(nx);
        queue_[wr + 1] = static_cast<std::uint8_t>(nz);
        wr += 2;
        if (wr == qEnd) { wr = 0; wrapped_ = true; }
    };
    int v27 = 0;
    for (;;) {
        const int x = queue_[rd_], z = queue_[rd_ + 1];
        rd_ += 2;
        if (rd_ == qEnd) { rd_ = 0; wrapped_ = false; }
        const int c = w * z + x;
        const std::uint8_t val = (c >= 0 && static_cast<std::size_t>(c) < g.size())
                               ? g[static_cast<std::size_t>(c)] : std::uint8_t{0xFE};
        const std::uint8_t v6 = static_cast<std::uint8_t>(val + 1);
        if (val == 0xFE) break;                 // saturated: the search stops
        auto enter = [&](int nx, int nz) {
            const int idx = w * nz + nx;
            if (refused(idx)) return;
            if (static_cast<std::size_t>(idx) < g.size() && g[static_cast<std::size_t>(idx)] > v6) {
                g[static_cast<std::size_t>(idx)] = v6;
                push(nx, nz);
            }
        };
        // the four in the engine's order: x-1, x+1 (z inside), then z+1, z-1
        if (x - 1 >= 0 && x - 1 < w && z < h_) enter(x - 1, z);
        if (x + 1 >= 0 && x + 1 < w && z < h_) enter(x + 1, z);
        if (x < w && z + 1 >= 0 && z + 1 < h_) enter(x, z + 1);
        if (x < w && z - 1 >= 0 && z - 1 < h_) enter(x, z - 1);
        // the queue ran dry
        if (wrapped_) { if (wr >= rd_) break; }
        else if (wr <= rd_) break;
        if (++v27 >= 100) break;
    }
    wr_ = wr;
    return v27 != 100;
}

std::uint8_t ShootField::distance(int x, int z) const {
    if (read_ < 0 || x < 0 || z < 0 || x >= w_ || z >= h_) return 0xFF;
    const std::size_t i = static_cast<std::size_t>(x) + static_cast<std::size_t>(w_) *
                          static_cast<std::size_t>(z);
    const auto& g = grid_[read_];
    return i < g.size() ? g[i] : std::uint8_t{0xFF};
}

int ShootField::heading(const Map2d& map, int floor, int x, int z,
                        const std::function<int()>& rnd) const {
    if (read_ < 0) return -1;                   // `if (!dword_52C3F0) return -1`
    // (the engine indexes the floor table with whatever it is handed; off
    // every floor there is nothing to index, so the port refuses)
    if (floor < 0 || floor >= static_cast<int>(map.floors().size())) return -1;
    const Map2dFloor& f = map.floors()[static_cast<std::size_t>(floor)];
    // THE ROBBER'S FLOOR's dimensions against the read grid, whichever floor
    // that was built for - the engine does not check, and neither does this
    const int W = static_cast<int>(f.w), H = static_cast<int>(f.h);
    const int S = static_cast<int>(f.w & 0xFFu);   // `v23 = u8(v4, 24)`, the stride
    const auto& g = grid_[read_];
    auto at = [&](int xx, int zz) -> std::uint8_t {
        const long long i = static_cast<long long>(xx) + static_cast<long long>(zz) * S;
        return (i >= 0 && static_cast<std::size_t>(i) < g.size()) ? g[static_cast<std::size_t>(i)]
                                                                  : std::uint8_t{0xFF};
    };
    std::uint8_t v3 = 0xFF;
    int dir = -1;
    if (x - 1 >= 0 && x - 1 < W && z >= 0 && z < H && at(x - 1, z) != 0xFF) {
        v3 = at(x - 1, z);
        dir = 270;
    }
    if (x + 1 >= 0 && x + 1 < W && z >= 0 && z < H) {
        const std::uint8_t v = at(x + 1, z);
        // `(v == v3 && rand() & 1) || v < v3` - the draw only on a tie
        if (v <= v3 && ((v == v3 && rnd && (rnd() & 1) != 0) || v < v3)) {
            v3 = v;
            dir = 90;
        }
    }
    if (x >= 0 && x < W && z - 1 >= 0 && z - 1 < H && at(x, z - 1) < v3) { v3 = at(x, z - 1); dir = 0; }
    if (x >= 0 && x < W && z + 1 >= 0 && z + 1 < H && at(x, z + 1) < v3) { v3 = at(x, z + 1); dir = 180; }
    if (x - 1 >= 0 && x - 1 < W && z - 1 >= 0 && z - 1 < H && at(x - 1, z - 1) < v3) {
        v3 = at(x - 1, z - 1);
        dir = 315;
    }
    if (x + 1 >= 0) {
        if (x + 1 < W && z - 1 >= 0 && z - 1 < H && at(x + 1, z - 1) < v3) {
            v3 = at(x + 1, z - 1);
            dir = 45;
        }
        if (x + 1 < W && z + 1 >= 0 && z + 1 < H && at(x + 1, z + 1) < v3) {
            v3 = at(x + 1, z + 1);
            dir = 135;
        }
    }
    if (x - 1 >= 0 && x - 1 < W && z + 1 >= 0 && z + 1 < H && at(x - 1, z + 1) < v3) return 225;
    return dir;
}

bool shootGridTurn(ShootRecord& r, int heading, float& eulerY, float dt,
                   const std::function<int()>& rnd, ShootStep& out) {
    if (heading == -1) return false;
    r.flags |= 0x200u;                          // `BYTE1(v6) |= 2u`
    const float v20 = static_cast<float>(heading);
    double v7 = static_cast<double>(heading) - static_cast<double>(eulerY);
    if (v7 < 180.0) { if (v7 <= -180.0) v7 = v7 - -360.0; }
    else v7 = v7 - 360.0;
    auto clip = [&](int type, float total, float rate) {
        out.clipType = type;
        out.turnClip = type;
        out.turnTotal = total;
        out.turnRate = rate;
    };
    if (v7 < 0.0) {
        if (v7 > -5.0) { eulerY = v20; return true; }
        if (v7 <= -90.0) {
            if (v7 > -160.0) { clip(30, -90.0f, -5.0f); return true; }
            if (rnd) rnd();                     // drawn and discarded
            clip(32, -180.0f, 10.0f);
            return true;
        }
        eulerY = static_cast<float>(v7 * 0.16666667 * dt + eulerY);   // LABEL_23
        return true;
    }
    if (v7 <= 0.0) return true;
    if (v7 < 5.0) { eulerY = v20; return true; }
    if (v7 < 90.0) { eulerY = static_cast<float>(v7 * 0.16666667 * dt + eulerY); return true; }
    if (v7 >= 160.0) {
        if (rnd) rnd();                         // drawn and discarded
        clip(32, -180.0f, 10.0f);
        return true;
    }
    clip(31, 90.0f, 5.0f);
    return true;
}

ShootActionOut shootActorAction(ShootRecord& r, int action, int a3,
                                const std::function<bool(int)>& hasClip,
                                const std::function<int()>& rnd,
                                const std::function<ShootRouteChoice(int)>& acquire) {
    ShootActionOut o;
    // a picked clip playing: the request is PARKED for when it ends
    if (r.flags & 8u) {
        r.pendingAction = action;
        r.flags |= 0x8000000u;
        r.pendingArg = a3;
        o.parked = true;
        return o;
    }
    // `if (+144 == 1) sub_435650(+24)`: replacing a PATROL gives its route
    // back. The release itself is the caller's (it owns the map); this only
    // says so by forgetting the route.
    if (r.scriptStep == 1 && r.route >= 0) { o.releasedRoute = r.route; r.route = -1; }
    int v9 = action;
    if (a3) {
        r.routeArg = a3;
        r.hitAction = action;
        if (a3 > 0) v9 = 1;
        if (a3 == -1) v9 = 0;
    } else {
        r.hitAction = -1;
        if (action == -1) v9 = 0;
    }
    const std::uint32_t v10 = r.flags & 0xFFDFFFFFu;
    r.flags = v10;
    auto has = [&](int t) { return hasClip && hasClip(t); };
    auto walkClip = [&]() { return has(10) ? 10 : (has(9) ? 9 : -1); };
    switch (v9) {
    case 0:
    case 5: {
        // `+88 - 1`, and at 0 (or below) a coin picks 11 or 25 and the count
        // restarts at 10; otherwise flags 0x19 choose 25 over 11
        int t;
        r.actionCounter -= 1;
        if (r.actionCounter <= 0) {
            if (rnd && (rnd() & 1) != 0) { t = 11; r.flags &= 0xFF9FFFFFu; }
            else                         { t = 25; r.flags |= 0x600000u; }
            r.actionCounter = 10;
        } else if (v10 & 0x19u) {
            t = 25; r.flags |= 0x600000u;
        } else {
            t = 11; r.flags &= 0xFF9FFFFFu;
        }
        if (!has(t)) {
            t = 11;
            r.flags &= 0xFF9FFFFFu;
            if (!has(t)) return o;              // "anim non existante dans le .ANI"
        }
        r.state = 3;
        if (v9 == 0) { r.scriptStep = 0; r.flags &= 0xFFFFFFDDu; }
        else         { r.scriptStep = 5; r.flags |= 2u; }
        o.clipType = t;
        return o;
    }
    case 1: {
        // THE PATROL (`05_sys.c` 4079). Type 9 only - no fallback to 10, unlike
        // every walking action below - and nothing is written if there is none.
        if (!has(9)) return o;                  // "anim non existante dans le .ANI"
        r.flags &= ~2u;
        r.scriptStep = 1;
        r.state = 4;
        // `sub_4354E0(+188, +136, +140, (uint8_t)a3)` then `sub_4356B0(route,
        // +188, 0, out)`. The engine reads `+188` TWICE, once for each call,
        // which matters not at all - nothing between them writes it.
        const ShootRouteChoice c = acquire ? acquire(a3 & 0xFF) : ShootRouteChoice{};
        r.route = c.route;
        r.repeats = 0;                          // `+100 = 0`, the point index
        if (c.route >= 0) { r.goalX = c.x; r.goalZ = c.z; }
        // (with no route the engine leaves +44/+48 holding whatever its
        // uninitialised stack slot held; state 4 never reads them without one)
        o.clipType = 9;
        return o;
    }
    case 2:
    case 3: {
        const int t = walkClip();
        if (t < 0) return o;
        r.state = 6;
        r.flags = (r.flags & 0xFFFFFFDDu) | 0x20u;
        r.scriptStep = v9;
        o.clipType = t;
        o.timerProperty = 31;                   // `Game_RaiseEvent(44, {31, him})`
        return o;
    }
    case 4: {
        const int t = walkClip();
        if (t < 0) return o;
        r.flags &= ~2u;
        r.scriptStep = 4;
        r.state = 7;
        r.edgeMark = 0;
        o.clipType = t;
        o.timerProperty = 23;
        return o;
    }
    case 6: {
        const int t = walkClip();
        if (t < 0) return o;
        r.scriptStep = 6;
        r.flags |= 2u;
        o.clipType = t;
        return o;
    }
    case 7: {
        const int t = walkClip();
        if (t < 0) return o;
        r.state = 14;
        r.scriptStep = 7;
        r.flags &= ~2u;
        o.clipType = t;
        return o;
    }
    case 8: {
        if (!has(11)) return o;
        r.state = 15;
        r.flags &= ~2u;
        r.scriptStep = 8;
        r.flags |= 0x2000u;
        o.clipType = 11;
        return o;
    }
    case 9: {
        const int t = walkClip();
        if (t < 0) return o;
        r.flags &= ~2u;
        r.scriptStep = 9;
        r.state = 7;
        r.edgeMark = 0;
        o.clipType = t;
        o.timerProperty = 25;
        o.turnAround = true;                    // `+420 - -180.0`
        return o;
    }
    case 10: {
        if (!has(25)) return o;
        r.state = 15;
        r.scriptStep = 8;
        r.flags = (r.flags & 0xFFDFDFFDu) | 0x202000u;
        o.clipType = 25;
        return o;
    }
    default:
        return o;                               // 1, the patrol - NOT PORTED
    }
}

const char* charTypeName(int type) {
    return (type >= 0 && type < kCharTypeCount) ? kTypeNames[type] : "";
}

ShootBrain shootBrainFor(std::uint32_t characterType) {
    // `Shoot_ActorEnter`'s switch, and `default:` is load-bearing: 330 of the
    // 1032 shipped character records carry 0xFFFFFFFF here, and every one of
    // them lands on the generic shooter through it.
    switch (characterType) {
        case 7:  return ShootBrain::Inert;
        case 10: return ShootBrain::Gandhar;
        case 13: return ShootBrain::Astaroth;
        default: return ShootBrain::Generic;
    }
}

const char* shootBrainName(ShootBrain b) {
    switch (b) {
        case ShootBrain::Inert:    return "nullsub_9";
        case ShootBrain::Gandhar:  return "sub_47F6F0";
        case ShootBrain::Astaroth: return "sub_4800C0";
        default:                   return "sub_424DE0";
    }
}

const ShootAction* ShootAi::Tables::byCode(int code) const {
    for (const auto& a : actions) if (a.code == code) return &a;
    return nullptr;
}

// `sub_47FB40` (0x0047FB40): play the current step `repeats` times, then move
// on; a `{0, n}` step rewinds. The healthy path in `sub_47F6F0` is this
// function inlined, which is how the two were confirmed to be one walk.
int shootScriptAdvance(ShootRecord& r, const std::vector<ShootScriptStep>& s) {
    if (s.empty()) return 0;
    if (r.scriptStep < 0 || r.scriptStep >= static_cast<int>(s.size()))
        r.scriptStep = 0;
    const auto& cur = s[static_cast<std::size_t>(r.scriptStep)];
    if (r.repeats < cur.repeats) { ++r.repeats; return cur.action; }
    const int next = r.scriptStep + 1;
    if (next < static_cast<int>(s.size()) && !s[static_cast<std::size_t>(next)].rewind) {
        r.scriptStep = next;
        r.repeats = 1;
        return s[static_cast<std::size_t>(next)].action;
    }
    r.scriptStep = 0;
    r.repeats = 1;
    return s[0].action;
}

ShootAi::ShootAi(const Tables& t, std::uint32_t characterType) : t_(&t) {
    rec_.type  = characterType;
    rec_.brain = shootBrainFor(characterType);
    // Every arm starts where its own entry leaves it: Gandhar on the first
    // step of his script, Astaroth in 16, the generic shooter in 1.
    switch (rec_.brain) {
        case ShootBrain::Gandhar:  rec_.state = 0;  break;
        case ShootBrain::Astaroth: rec_.state = 16; break;
        case ShootBrain::Generic:  rec_.state = 1;  break;
        default: break;
    }
}

const std::vector<ShootScriptStep>& ShootAi::scriptForHealth() {
    // Re-read EVERY frame, and a change of band resets the walk. Note the
    // comparisons: Gandhar's are `<=` where Astaroth's speed bands are `<`.
    // That asymmetry is in the two functions and is not a transcription slip.
    if (rec_.health <= kCriticalAt) {
        if (rec_.band != 2) { rec_.band = 2; rec_.scriptStep = 0; rec_.repeats = 0; }
        return t_->critical;
    }
    if (rec_.health <= kWoundedAt) {
        if (rec_.band != 1) { rec_.band = 1; rec_.scriptStep = 0; rec_.repeats = 0; }
        return t_->wounded;
    }
    rec_.band = 0;
    return t_->healthy;
}

void ShootAi::tickGandhar(float dt) {
    (void)dt;
    if (rec_.health <= 0) {
        log_.push_back({Decision::Kind::Died, rec_.state, rec_.state, -1, 0.0f,
                        "hp reached 0 - event 43 arg 3"});
        return;
    }
    if (!actionDone_) return;           // dword_657A28 has not pulsed
    actionDone_ = false;

    const auto& script = scriptForHealth();
    const int code = shootScriptAdvance(rec_, script);
    const ShootAction* a = t_->byCode(code);
    const int from = rec_.state;
    if (!a) {                            // a code with no row: impossible in
        log_.push_back({Decision::Kind::Idle, from, code, -1, 0.0f,
                        "action code with no handler row"});
        return;                          // the shipped scripts, asserted
    }
    // the ENTER handler: set +156, set or clear channel flag 0x800, and pick
    // an animation by TYPE from this character's own list
    rec_.state = a->code;
    if (a->setsFlag) rec_.flags |= 0x800u; else rec_.flags &= ~0x800u;
    if (a->code == 16) {
        // the only action with no animation: it waits (rand() & 0x1F) + 30
        // frames. The replica takes the midpoint rather than a random draw,
        // because a sweep that has to be reproducible cannot roll dice - the
        // RANGE is the fact, and it is asserted.
        rec_.timer = 45.0f;
        log_.push_back({Decision::Kind::Wait, from, a->code, -1, rec_.timer,
                        "wait 30..61 frames"});
    } else {
        log_.push_back({Decision::Kind::Action, from, a->code, a->clipType,
                        0.0f, a->note});
    }
    rec_.flags &= 0xFFFFDF7Fu;           // the mask the original clears
}

void ShootAi::tickAstaroth(float dt) {
    rec_.timer -= dt;
    // the health bands, which scale his speed and his turn rate. `<`, not
    // `<=` - see scriptForHealth.
    float speed = 1.0f, turn = 60.0f;
    if (rec_.health < 100) { speed = 1.5f; turn = 40.0f; }
    if (rec_.health < 50)  { speed = 2.0f; turn = 30.0f; }
    (void)speed; (void)turn;

    if (rec_.health <= 0) {
        log_.push_back({Decision::Kind::Died, rec_.state, rec_.state, -1, 0.0f,
                        "hp reached 0 - event 43 arg 3"});
        return;
    }
    if (!actionDone_) return;
    actionDone_ = false;

    const int from = rec_.state;
    int to = from;
    const char* why = "";
    switch (from) {
        case 16: to = 17; why = "closed to within 195 units"; break;
        case 17: to = 18; why = "the grapple landed";         break;
        case 18: to = 19; why = "the hold landed";            break;
        case 19: to = 21; why = "the throw landed";
                 rec_.timer = 150.0f;                          break;
        case 20: if (rec_.timer <= 0.0f) { to = 16; why = "the 150-frame timer ran out"; }
                 else { to = 21; why = "recovered"; }          break;
        case 21: to = 16; why = "recovery finished"; rec_.timer = 60.0f; break;
        case 27: why = "a terminal state - the arm does nothing"; break;
        case 29: to = 16; why = "the global counter reached 6";  break;
        default: to = 16; why = "the default arm resets him";     break;
    }
    rec_.state = to;
    log_.push_back({from == to ? Decision::Kind::Idle : Decision::Kind::StateChange,
                    from, to, -1, rec_.timer, why});
}

void ShootAi::tickGeneric(float dt) {
    (void)dt;
    if (rec_.health <= 0) {
        log_.push_back({Decision::Kind::Died, rec_.state, rec_.state, -1, 0.0f,
                        "hp reached 0"});
        return;
    }
    if (!actionDone_) return;
    actionDone_ = false;
    const int from = rec_.state;
    // The graph, and only the graph. Which of a state's outgoing edges is
    // taken depends on geometry this tree does not have - the navigation node,
    // the line of sight, the weapon's range - so the replica takes the first
    // edge and RECORDS the choice rather than pretending to compute it.
    for (const auto& e : kGenericEdges)
        if (e.from == from) {
            rec_.state = e.to;
            log_.push_back({Decision::Kind::StateChange, from, e.to, -1, 0.0f, e.why});
            return;
        }
    log_.push_back({Decision::Kind::Idle, from, from, -1, 0.0f,
                    "a state with no outgoing edge in the switch"});
}

const char* ShootAi::tick(float dt) {
    rec_.flags |= 1u;                    // Shoot_TickNpc sets bit 0 around it
    switch (rec_.brain) {
        case ShootBrain::Inert:    break;             // nullsub_9
        case ShootBrain::Gandhar:  tickGandhar(dt);  break;
        case ShootBrain::Astaroth: tickAstaroth(dt); break;
        case ShootBrain::Generic:  tickGeneric(dt);  break;
    }
    rec_.flags &= ~1u;
    return shootBrainName(rec_.brain);
}

const std::vector<int>& astarothStates() { return kAstarothStates; }
const std::vector<int>& genericStates()  { return kGenericStates; }
const std::vector<ShootEdge>& astarothEdges() { return kAstarothEdges; }
const std::vector<ShootEdge>& genericEdges()  { return kGenericEdges; }

// ---------------------------------------------------------------------
// THE GEOMETRY (`todo/shoot-mode.md` 5c, 7a)
// ---------------------------------------------------------------------

// `sub_422540` (0x00422540), called once from `Shoot_ActorEnter`. Six reads
// of event 44 (`Actor_GetProperty`) chained so each gates the next - a
// character that fails to answer one gets none of the rest, which is why the
// defaults here are zero rather than something plausible.
//
// `39` is the engine's own inch-per-metre factor, the same constant the
// pedestrian spawn (`39 * (5 - density) * h[3]`) and the projectile speed
// (`property.hi * 3.9`) use. The ranges are therefore authored in METRES and
// the cone in DEGREES, per character - and NOT in the weapon table, whose two
// floats are the fire rate and one nothing reads.
void initShootRecord(ShootRecord& r, const ShootProperties& p) {
    r.health = p.health ? p.health : 10;          // the engine's own default
    r.rangeAcquire = static_cast<float>(39 * p.rangeAcquireM);
    r.rangeInner   = static_cast<float>(39 * p.rangeInnerM);
    r.rangeThird   = static_cast<float>(39 * p.rangeThirdM);
    r.coneCos      = static_cast<float>(std::cos(p.coneDegrees * 3.14159265358979 / 180.0));
    // property 37's five bits, fanned into `+160` exactly as the engine fans
    // them - the values are not a contiguous field and the order is its own.
    r.flags = 64;                                  // `u32(rec,160) = 64`
    if (p.behaviourBits & 0x10) r.flags |= 0x4000000u;
    if (p.behaviourBits & 0x04) r.flags |= 0x0800000u;
    if (p.behaviourBits & 0x08) r.flags |= 0x2000000u;
    if (p.behaviourBits & 0x02) r.flags |= 0x1000000u;
    if (p.behaviourBits & 0x20) r.flags |= 0x0100000u;
}

// `sub_420C70` (0x00420C70) and its wider twin `sub_420D90`.
//
// `self` is the end carrying a yaw, so it is the SHOOTER. The engine forms
// `self - targetPos` and dots it against `(0,0,1)` rotated by that yaw; the
// two sign conventions cancel, because a character faces -Z at yaw 0. See the
// header - the first version of this had the roles the other way round and
// would have made every gunman shoot at whatever stood behind him.
//
// `dot > cos(half) * dist` is the cone with both sides scaled by the distance
// instead of normalising, and the second clause is the range.
bool shootAcquires(const ShootRecord& r, const float self[4], const float targetPos[3],
                   AcquireOut& out, bool doubleRange) {
    const double dx = double(self[0]) - targetPos[0];
    const double dy = double(self[1]) - targetPos[1];
    const double dz = double(self[2]) - targetPos[2];
    out.dist2d2 = static_cast<float>(dx * dx + dz * dz);
    out.dist3d  = static_cast<float>(std::sqrt(dy * dy + out.dist2d2));

    const double yaw = self[3] * 3.14159265358979 / 180.0;
    // `sub_442160(0, yaw, 0)` then `Matrix3x3_RotateVector(0,0,1, m)`, in the
    // ROW-VECTOR convention this repo uses everywhere - so the x component is
    // `-sin`, not `+sin`.
    //
    // That sign is worth its paragraph, because it is invisible in every
    // still frame: the two conventions AGREE on the cardinal axes, so the
    // acquisition tests at yaw 0 and 180 pass either way. What separates them
    // is turning. With `+sin` the turn converges on the heading that points
    // the gunman exactly AWAY from his target and sits there - `shoot_range`
    // reported 400 frames from 135 degrees and never aimed - because the
    // direction rule and the aim test then disagree by construction. With
    // `-sin` it converges in 24. This is CLAUDE.md 1's "a value verified
    // standing still is not verified moving", and the convergence loop is
    // what caught it.
    const double fx = -std::sin(yaw), fy = 0.0, fz = std::cos(yaw);

    out.dotFlat = static_cast<float>(dz * fz + dx * fx);
    out.dot     = static_cast<float>(fy * dy + out.dotFlat);
    out.cross   = static_cast<float>(dx * fz - dz * fx);

    const double reach = doubleRange ? double(r.rangeAcquire) + r.rangeAcquire
                                     : double(r.rangeAcquire);
    return out.dot > double(r.coneCos) * out.dist3d && out.dist3d < reach;
}

// `sub_420EB0` (0x00420EB0). See the header for where each sign comes from -
// all three are `fcomp` against `flt_4BC224`, which the listing gives as 0.0,
// and the five constants are `flt_4BC228` 0.99, `flt_4BC22C` 0.80,
// `flt_4BC230` 0.2, `flt_4BC234` 10.0 and `flt_4BC238` 5.0.
int shootTurnToward(float& eulerY, const AcquireOut& a, bool allowSnap, float dt) {
    // `fld dotFlat; fcomp 0.0; fld dotFlat; fmul dotFlat; ... fchs` - the
    // square, negated when the dot itself is negative.
    const double signed2 = double(a.dotFlat) * std::fabs(double(a.dotFlat));
    const double near99  = double(a.dist2d2) * 0.99000001;
    const double near80  = double(a.dist2d2) * 0.80000001;

    if (signed2 > near99) return 0;               // already aimed: nothing to do

    // cross < 0 adds, cross >= 0 subtracts - the same split in all three arms
    const double dir = a.cross < 0.0f ? 1.0 : -1.0;

    if (signed2 > near80) {                        // the fine band: one delta
        eulerY = static_cast<float>(eulerY + dir * dt);
        return 0;
    }

    double rate;
    if (signed2 > double(a.dist3d) * 0.2) {
        rate = 5.0;                                // in front, but wide
    } else if (allowSnap) {
        // hard behind, or abeam - hand a turn ANIMATION back to the caller
        if (signed2 < -near80) return 180;
        return a.cross >= 0.0f ? -90 : 90;
    } else {
        rate = 10.0;                               // behind, and turning through
    }
    eulerY = static_cast<float>(eulerY + dir * rate * dt);
    return 0;
}

// ---------------------------------------------------------------------
// THE GENERIC BRAIN, `sub_424DE0` (0x00424DE0) - `todo/shoot-mode.md` 7c
// ---------------------------------------------------------------------
//
// ALL SIXTEEN of its states are transcribed here; nothing sets
// `unread` any more. The rule that got here is the one the port
// follows: an arm nobody has read is not a branch to guess, and a machine
// that invented the missing ten would be indistinguishable from one that had
// them right. `genericStatesRead()` is the list, and `verify.py: shoot
// generic` asserts it against `genericStates()` so the coverage cannot drift
// from the comment.
//
// THE PROLOGUE, shared by every arm (05_sys.c 5572-5580):
//   the TARGET is record `+96`, its position and facing are fetched once,
//   flag bit `0x200` is cleared, and the current clip pointer at `+16` is
//   dropped. A state that wants `0x200` sets it back.
namespace {

const std::vector<int> kGenericRead = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 28};

}  // namespace

const std::vector<int>& genericStatesRead() { return kGenericRead; }

ShootStep shootGenericStep(ShootRecord& r, const ShootFrameIn& in, float& eulerY) {
    ShootStep out;
    // ---- the prologue ------------------------------------------------
    r.flags &= ~0x200u;                       // `BYTE1(v18) &= ~2u`
    const int state = r.state;

    AcquireOut a;
    // every arm that turns or fires wants these, and the engine computes them
    // once into its globals for the same reason
    const bool acquired = shootAcquires(r, in.self, in.target, a, false);
    (void)acquired;

    // The code-to-CLIP-TYPE mapping, written out once: states 1, 4, 6, 8, 9,
    // 12, 13, 14 and 28 all do it, character for character.
    //
    // **WHAT TYPES 30, 31 AND 32 ANIMATE IS NOT ESTABLISHED.** Calling them
    // turn-left-90, turn-right-90 and turn-180 is a reading of the `+/-90` /
    // `180` codes that select them, and `docs/ASSETS.md` deliberately leaves
    // the behaviour types unnamed - 30 of the 34 have no clip at all.
    //
    // The reading is CONSISTENT with `sub_426C20`, which hands back 180 when
    // the destination is directly BEHIND and `+/-90` when it is behind and
    // off to one side (see `shootMoveDecision`) - but consistent is not
    // established, and nothing in the corpus says what a type-32 clip plays.
    //
    // So: the code selects a type, `turnTotal` is the degrees the chosen clip
    // is made to cover (`+184 = total / Anim_Frames`), and `turnRate` is the
    // fallback rotation when the library has no clip of that type. The 180
    // arm calls `rand()` and discards it - a quirk, recorded not copied.
    auto snapToClip = [&](int snap) {
        if (snap == 90)       { out.clipType = 31; out.turnTotal =   90.0f; out.turnRate =  5.0f; }
        else if (snap == 180) { out.clipType = 32; out.turnTotal = -180.0f; out.turnRate = 10.0f; }
        else if (snap == -90) { out.clipType = 30; out.turnTotal =  -90.0f; out.turnRate = -5.0f; }
        else if (snap != 0)   { out.clipType = in.defaultClipType; }
        if (snap == 90 || snap == 180 || snap == -90) out.turnClip = out.clipType;
    };

    switch (state) {
    case 1: {
        // NAVIGATE. Standing on the target's own node means contact, and the
        // hub state 6 takes over. Otherwise `sub_426C20` either hands back a
        // turn to play or says "take the step".
        r.flags &= ~0x10u;
        // NOTE THE ORDER, because it is load-bearing: the engine writes
        // `+156 = 6` here and the step branch below OVERWRITES it with 2. So
        // a gunman standing on his target's node who can also take a step
        // ends in 2, not 6 - the contact test loses. Transcribed as written.
        if (in.myNode == in.targetNode) out.nextState = 6;
        int code = in.moveCode;
        if (code == 1) {
            // The cell must be walkable, and the refusal set is the MOVEMENT
            // one - `{-128, 0, 2, 3}`, the signed form, occupancy included.
            const int c = in.stepCellValue;
            if (!(c == -128 || c == 0 || c == 2 || c == 3) && in.hasEdge) {
                const double dx = double(in.edgeTo[0]) - in.edgeFrom[0];
                const double dz = double(in.edgeTo[2]) - in.edgeFrom[2];
                out.takeStep = true;
                out.swapOccupancy = true;
                // `a1[105] = atan2(dz, dx) * 180/pi + 90` - the heading, in
                // degrees, with the engine's own quarter-turn offset.
                out.headingDeg = static_cast<float>(std::atan2(dz, dx) * 57.29577951308232 + 90.0);
                out.stepLength = static_cast<float>(std::sqrt(dx * dx + dz * dz));
                r.stepRemaining = out.stepLength;
                eulerY = out.headingDeg;
                out.nextState = 2;
            }
            code = 0;                       // `v20 = 0` - no turn this frame
        }
        snapToClip(code);
        break;
    }

    case 2: {
        // TRAVERSE the edge state 1 committed to, climbing its slope: the
        // vertical step is `dy / horizontalLength * distanceMovedThisFrame`,
        // and `+68` counts the remaining distance down. At zero the body is
        // snapped onto the edge's end and the hub takes over.
        r.flags &= ~0x10u;
        if (in.hasEdge) {
            const double dx = double(in.edgeTo[0]) - in.edgeFrom[0];
            const double dy = double(in.edgeTo[1]) - in.edgeFrom[1];
            const double dz = double(in.edgeTo[2]) - in.edgeFrom[2];
            const double flat = std::sqrt(dx * dx + dz * dz);
            if (flat > 0.0)
                out.climb = static_cast<float>(dy / flat * in.movedThisFrame);
        }
        r.groundY += out.climb;
        // `+68` counts the remaining distance down by however far the body
        // actually travelled this frame; at zero the arm snaps him onto the
        // edge's end, takes the new nav node from the edge, and hands over to
        // the hub. The occupancy of the cell he LEFT is restored there.
        r.stepRemaining -= in.movedThisFrame;
        if (r.stepRemaining <= 0.0f) {
            out.arrived = true;
            out.swapOccupancy = true;
            out.nextState = 6;
        }
        break;
    }

    case 4: {
        // PATROL a route. Without one there is nothing to do but the tail.
        if (in.hasRoute) {
            r.flags |= 0x200u;
            int code = in.moveCode;
            if (code == 1) {
                if (in.routeAdvanced) out.nextState = 5;
                code = 0;
            }
            snapToClip(code);
        }
        // The tail: if nothing changed the state the outcome is 4. If
        // something did, the engine takes the outcome from `sub_4272B0`,
        // which is NOT READ - so the port leaves it None and says so rather
        // than picking a plausible value.
        if (out.nextState < 0) out.outcome = ShootOutcome::Outcome4;
        // and a route is RELEASED whenever the state left 4 and 5
        if (out.nextState >= 0 && out.nextState != 4 && out.nextState != 5)
            out.releaseRoute = true;
        break;
    }

    case 6: {
        // THE HUB - where 1->2 and 4->5 both hand over. Three arms, and all
        // three turn WITH the snap and pick a turn clip from it; only the
        // first also fires.
        if (r.flags & 0x8000u) {
            // finishing: either drop flag 0x20, or ask for the default action
            if (in.scriptStep == 8) r.flags &= ~0x20u;
            else if (!(r.flags & 0x4000u)) out.actionRequest = 0;   // `Shoot_ActorAction(a2, 0, 0)`
            break;                       // straight to the epilogue - no timer
        }
        bool turned = false;
        if (in.targetPredicate && in.canFire) {
            out.outcome = ShootOutcome::Fire;
            turned = true;
        } else if (!(r.flags & 0x100u)) {
            // `sub_421CD0` says hold, and then nothing happens at all - it
            // has turned him along the path field itself (`shootGridTurn`)
            const bool hold = in.gridHeading
                ? shootGridTurn(r, in.gridHeading(), eulerY, in.dt, in.rand, out)
                : in.holdStill;
            if (!hold) turned = true;
        } else {
            r.flags |= 0x200u;
            turned = true;
        }
        if (turned) snapToClip(shootTurnToward(eulerY, a, true, in.dt));
        // the tail all three arms share: a timer at `+168`, and on expiry the
        // default action is asked for
        r.timer -= in.dt;
        // (05_sys.c 5997: `+168 -= dt; if (<= 0) Shoot_ActorAction(a2, 0, 0)` -
        // action 0, STAND AND FIRE, when his advance runs out)
        if (r.timer <= 0.0f) out.actionRequest = 0;
        break;
    }

    case 3:
        // `sub_426E00` decides; its bit 0 asks to FIRE. The turn is taken
        // either way, without the snap.
        r.flags |= 0x200u;
        if (in.targetPredicate) {
            if (in.targetPredicateBits & 1) out.outcome = ShootOutcome::Fire;
            shootTurnToward(eulerY, a, false, in.dt);
        }
        break;

    case 5:
        // play a clip out; when it ends, go to 4. Nothing else.
        r.flags |= 0x200u;
        if (in.clipFrame + in.dt >= in.clipFrames) out.nextState = 4;
        break;

    case 7: {
        // a TIMER at `+168` counting down, and a nav-node comparison: when
        // either says stop, ask for action 0 and - if flag `0x4000000` is
        // set, which is property 37's bit 0x10 - raise event 43 with 19.
        r.flags &= ~0x10u;
        r.timer -= in.dt;
        const bool done = r.timer <= 0.0f;
        if (done) { out.actionRequest = 0; out.turnRate = 0.0f; }
        break;
    }

    case 9:
    case 28:
    case 13:
    case 8: {
        // TURN, with the snap allowed - and the snap picks an ANIMATION.
        r.flags |= 0x200u;
        if (in.targetPredicate) out.outcome = ShootOutcome::Fire;
        snapToClip(shootTurnToward(eulerY, a, true, in.dt));
        // 9 and 28 are state 8's arm CHARACTER FOR CHARACTER, and 13 is the
        // same with one extra tail: when its clip has run out the outcome is
        // recomputed unconditionally rather than only on a state change.
        if (state == 13 && in.clipFrame + in.dt >= in.clipFrames)
            out.outcomeFromUnread = true;
        break;
    }

    case 12: {
        // The only arm that converts a world position into a GRID CELL
        // itself: `(self.x - bound[0]) / scale` and `(self.z - bound[4]) /
        // scale`, which is `Map2d::cellAt`'s arithmetic inline. If the step
        // is not available AND the cell is refused by the MOVEMENT test, it
        // asks for the script's own action; then it turns. It reaches the
        // epilogue directly, so its outcome is never recomputed.
        r.flags &= ~0x10u;
        int code = in.moveCode;
        bool act = true;
        if (code == 1) code = 0;
        else if (!(in.stepCellValue == -128 || in.stepCellValue == 0 ||
                   in.stepCellValue == 2 || in.stepCellValue == 3)) act = false;
        if (act) out.clipType = in.scriptStep;
        snapToClip(code);
        break;
    }

    case 14: {
        // Give up the route and fall back to patrolling from scratch.
        int code = in.moveCode;
        if (code == 1) { code = 0; out.nextState = 4; out.releaseRoute = true; }
        snapToClip(code);
        break;
    }

    case 15: {
        // ACQUIRE. The cone and range test AND the grid line of sight must
        // both hold - or flag 0x20, the latch that keeps a gunman engaged
        // once he has seen you, must already be set.
        if (r.flags & 0x8000u) break;
        const bool seen = acquired && in.gridLineOfSight;
        if (!(seen || (r.flags & 0x20u)) || !in.targetAlive) break;
        r.flags |= 0x20u;
        snapToClip(shootTurnToward(eulerY, a, true, in.dt));
        // and the FIRE test is the INNER range `+28`, not the acquisition
        // range `+32` - which is what the second of the three authored
        // distances is for.
        if (a.dist3d < r.rangeInner && seen) out.outcome = ShootOutcome::Fire;
        break;
    }

    case 10: {
        // aim while a clip runs; past halfway (or with flag 4) the outcome
        // changes from 3 to 2. When the clip ends, go to 11.
        r.flags |= 0x200u;
        shootTurnToward(eulerY, a, false, in.dt);
        if (in.clipFrame + in.dt < in.clipFrames) {
            out.outcome = ShootOutcome::Outcome3;
            if (in.clipFrames * 0.5f > in.clipFrame || (r.flags & 4u))
                out.outcome = ShootOutcome::Outcome2;
        } else {
            out.nextState = 11;
        }
        break;
    }

    case 11:
        // the mirror of 10: when the predicate holds and the clip has run
        // more than five frames, go back to 10 and clear the TARGET's 0x10.
        r.flags |= 0x200u;
        if (in.targetPredicate && in.clipFrame > 5.0f) out.nextState = 10;
        shootTurnToward(eulerY, a, false, in.dt);
        if (out.nextState < 0) out.outcome = ShootOutcome::Outcome2;
        break;

    default:
        out.unread = true;
        break;
    }

    // ---- the epilogue ------------------------------------------------
    //
    // The engine wraps the Euler HERE, after every arm, into (-360, 360):
    // `if (>= 360) -= 360; else if (< 0) += 360`. It is the one place the
    // angle is normalised, which matters because CLAUDE.md 1's wrap trap is
    // about exactly this value.
    if (eulerY >= 360.0f) eulerY -= 360.0f;
    else if (eulerY < 0.0f) eulerY += 360.0f;
    r.flags &= ~0x80u;

    // LABEL_222 / LABEL_181 / LABEL_58, the tail states 1, 3, 6, 8, 10 and 11
    // all reach: WHENEVER AN ARM CHANGED THE STATE the engine recomputes the
    // outcome through `sub_4272B0`, which is not read. So a transition always
    // leaves the outcome unknown rather than whatever the arm had set, and
    // saying so is the honest port of it.
    if (out.nextState >= 0) {
        out.outcomeFromUnread = true;
        r.state = out.nextState;
    }
    // and only once the transition has had its say: an outcome that came
    // from `sub_4272B0` is UNKNOWN, which is not the same as the default 0.
    if (out.outcomeFromUnread) out.outcome = ShootOutcome::None;
    return out;
}

// `sub_426C20` (0x00426C20) - the MOVE decision states 1, 4, 12 and 14 branch
// on, and the last of the four suppliers `shootGenericStep` used to take as a
// parameter.
//
// `dest` is the record's `+44`/`+48` - `goalX`/`goalZ`, which the PATROL
// writes from `sub_4356B0` and which are not the `+136/+140` cell.
//
// `arrived` is `sub_435900(self, goalX, goalZ)`, now read: `|goalX - self.x| <
// scale && |goalZ - self.z| < scale`, the map's own CELL SIZE either side, so
// "there" is one cell in each direction and never an exact point. It stays an
// argument because only the caller has the map.
//
// It confirms 7b's rotation convention from a second, independent site: the
// forward vector it builds is `(-sin(yaw), cos(yaw))`, the same `-sin` that
// only the convergence loop could establish for `sub_420C70`.
//
// Returns: 1 arrived / take the step, 0 "I turned in place this frame", and
// otherwise a clip-type code.
//
// MIND THE SENSE OF THE COMPONENT IT TESTS. It builds `b = -sin(yaw)*dx +
// cos(yaw)*dz` over `dest - self`, and since a character faces **-Z** at yaw
// 0 that is the BACKWARD component, not the forward one. So:
//
//     b <= 0                    the destination is ahead (or abeam): just
//                               steer, 10 degrees per delta, and return 0
//     0 < b <= 0.80 * distance  behind and off to one side: +/-90
//     b > 0.80 * distance       directly behind: 180
//
// Read as "forward" it says 180 for a destination straight ahead, which is
// how this was first written up and it was wrong; the probe's `move ahead 0
// behind 180` is what settled it.
int shootMoveDecision(const ShootRecord& r, const float self[4],
                      float& eulerY, float dt, bool arrived) {
    if (arrived) return 1;
    const double yaw = double(self[3]) * 3.14159265358979 / 180.0;
    const double sn = -std::sin(yaw), cs = std::cos(yaw);
    // +44/+48, the WORLD goal - NOT the +136/+140 cell. They shared one pair
    // of fields until 2026-09-12 (`actor/shoot.h`).
    const double dx = double(r.goalX) - self[0];
    const double dz = double(r.goalZ) - self[2];
    const double dist = std::sqrt(dx * dx + dz * dz);
    const double lateral = cs * dx - sn * dz;
    const double behind = sn * dx + cs * dz;   // +Z component: BACKWARD

    if (behind <= 0.0) {
        // ahead or abeam: steer 10 degrees per delta toward it, and CLAMP to
        // bearing the moment the step would overshoot - the same
        // `atan2(dz, dx) * 180/pi + 90` state 1 uses.
        const double step = 10.0 * dt;
        double y = self[3];
        const auto bearing = [&] {
            return std::atan2(dz, dx) * 57.29577951308232 + 90.0;
        };
        const auto lateralAt = [&](double deg) {
            const double rr = deg * 3.14159265358979 / 180.0;
            return std::cos(rr) * dx + std::sin(rr) * dz;
        };
        if (lateral > 0.0) {
            y += step;
            if (y >= 360.0) y -= 360.0;
            if (lateralAt(y) < 0.0) y = bearing();
        }
        if (lateral < 0.0) {
            y -= step;
            if (y < 0.0) y += 360.0;
            if (lateralAt(y) > 0.0) y = bearing();
        }
        eulerY = static_cast<float>(y);
        return 0;
    }
    if (behind <= dist * 0.80000001) return lateral > 0.0 ? 90 : -90;
    return 180;
}

// `sub_426E00` (0x00426E00). Transcribed in call order; the branches that
// only differ in which local they use are folded, since all four copies of
// the `sub_421020` block do the same three writes.
int shootEngage(ShootRecord& r, const AcquireOut& a, bool inCone, const EngageIn& in) {
    if (r.node == -1) return 0;
    if (!in.targetAlive || (r.flags & 0x8000u)) return 0;

    // `+108` and the 10/11 pair: whatever `sub_421020` finds, it latches 0x20
    // and puts him in 10 - or leaves him in 11 if he is already there.
    const auto goPair = [&] {
        r.flags |= 0x20u;
        r.state = (r.state == 11) ? 11 : 10;
        return 0;
    };

    const bool coneOk = in.sameNode && inCone;
    if (!in.sameNode) {
        if (in.found421020) return goPair();
        return 0;
    }
    if (!coneOk) {
        if (in.found421020) return goPair();
        return 0;
    }

    // the GRID line of sight, `sub_4359A0` - 1 clear, 0 blocked. (Its `2` can
    // never happen; see `todo/omk-play.md` 96's neighbour.)
    const int los = in.gridClear ? 1 : 0;
    const bool halfInner = double(r.rangeInner) * 0.5 > a.dist3d;

    bool engaged = false;
    if (a.dist3d < r.rangeAcquire && los) {
        r.flags |= 0x20u;
        r.state = 6;
        engaged = true;
    }

    if (los && halfInner) {
        r.flags |= 0x10u;
        if (in.found421020) { goPair(); }
        else if ((r.flags & 0x1000000u) || in.rayHits) {
            r.state = 6;
        } else if (double(r.rangeInner) * 0.25 <= a.dist3d ||
                   (r.flags & 0x100u) || a.dist3d >= 312.0f) {
            // 312 units is 8 metres at the engine's own 39 to the metre
            r.state = 13;
        } else {
            r.state = 8;
        }
    } else if (engaged) {
        r.state = 6;
    }

    // THE DISENGAGE. Past the third range and still in the hub, he gives up:
    // a coin flip sends him to 3 or back to patrolling in 4, and either way
    // the 0x20 latch is cleared - which is what lets him be re-acquired.
    if (a.dist3d >= r.rangeThird && r.state == 6) {
        r.flags &= ~0x20u;
        if (in.coinHeads) r.state = 3;
        else { r.state = 4; }
        return 0;
    }
    if (inCone && a.dist3d < r.rangeInner) return los;
    return 0;
}

NoiseHearing shootHearNoise(ShootRecord& r, const float self[3], const float at[3],
                            int hearingCells, float cellSize, int noiseFloor,
                            int selfFloor) {
    NoiseHearing h;
    // `test cl, 40h` / `test cl, 20h` / `cmp [esi-4], 0Eh`
    if (!(r.flags & 0x40u) || (r.flags & 0x20u) || r.state == 14) return h;
    const float dx = self[0] - at[0], dy = self[1] - at[1], dz = self[2] - at[2];
    const double reach = static_cast<double>(hearingCells) * static_cast<double>(cellSize);
    if (double(dx) * dx + double(dy) * dy + double(dz) * dz >= reach * reach) return h;
    h.heard = true;
    if (selfFloor != noiseFloor) {             // `movsx edx, byte [esi+1Ch]`
        h.act = true;
        h.action = r.hitAction;
        return h;
    }
    r.flags |= 0x20u;
    h.alerted = true;
    if (r.scriptStep != 8) {
        h.act = true;
        h.action = r.hitAction != -1 ? r.hitAction : 2;
    }
    return h;
}

}  // namespace omk

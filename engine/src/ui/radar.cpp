// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/radar.h"

#include "ui/hudbar.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace omk {

namespace {

std::uint32_t u32le(std::span<const std::byte> d, std::size_t o) {
    return static_cast<std::uint32_t>(d[o]) | static_cast<std::uint32_t>(d[o + 1]) << 8 |
           static_cast<std::uint32_t>(d[o + 2]) << 16 | static_cast<std::uint32_t>(d[o + 3]) << 24;
}

// `sub_442160` (0x00442160): a 3x3 from three angles, written in its own order
void euler(float a1, float a2, float a3, float m[9]) {
    const double v4 = std::sin(a3), v5 = std::cos(a1), v6 = std::cos(a2);
    const float v8 = static_cast<float>(v4), v10 = static_cast<float>(std::sin(a2));
    const float v14 = static_cast<float>(std::cos(a3)), v12 = static_cast<float>(std::sin(a1));
    const float v13 = static_cast<float>(v5), v11 = static_cast<float>(v4 * v10);
    const float v9 = static_cast<float>(v6);
    m[0] = v14 * v9;
    m[3] = v8 * v9;
    m[6] = -v10;
    m[1] = v10 * v12 * v14 - v8 * v13;
    m[4] = v12 * v11 + v14 * v13;
    m[7] = v12 * v9;
    m[2] = v10 * v14 * v13 + v8 * v12;
    m[5] = v13 * v11 - v12 * v14;
    m[8] = v9 * v13;
}

struct Prim {
    int layer = 0, seq = 0;
    bool line = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    std::uint32_t c0 = 0, c1 = 0;
};

// `sub_42EC80` (0x0042EC80): the segment clipped to the box, integer and
// with its own divisions. -> false when it is rejected.
bool clipSegment(int a1, int a2, int a3, int a4, int a7, int a8, int a9, int a10,
                 int& ox0, int& oy0, int& ox1, int& oy1) {
    int v12 = a1, v13 = a3;
    if (!((a1 >= a7 || a3 >= a7) && (a1 <= a9 || a3 <= a9))) return false;
    int v14 = a2, v15 = a4;
    if (!((a2 >= a8 || a4 >= a8) && (a2 <= a10 || a4 <= a10))) return false;
    if (a1 >= a7) {
        if (a3 < a7) {
            v13 = a7;
            v15 = (a2 - a4) * (a7 - a3) / (a1 - a3) + a4;
        }
    } else {
        v12 = a7;
        v14 = (a4 - a2) * (a7 - a1) / (a3 - a1) + a2;
    }
    if (v12 <= a9) {
        if (v13 > a9) {
            const int v17 = (v14 - v15) * (a9 - v13) / (v12 - v13);
            v13 = a9;
            v15 += v17;
        }
    } else {
        const int v16 = (v15 - v14) * (a9 - v12) / (v13 - v12);
        v12 = a9;
        v14 += v16;
    }
    if (!((v14 >= a8 || v15 >= a8) && (v14 <= a10 || v15 <= a10))) return false;
    if (v14 >= a8) {
        if (v15 < a8) {
            const int v19 = (v12 - v13) * (a8 - v15) / (v14 - v15);
            v15 = a8;
            v13 += v19;
        }
    } else {
        const int v18 = (v13 - v12) * (a8 - v14) / (v15 - v14);
        v14 = a8;
        v12 += v18;
    }
    if (v14 <= a10) {
        if (v15 > a10) {
            const int v21 = (v12 - v13) * (a10 - v15) / (v14 - v15);
            v15 = a10;
            v13 += v21;
        }
    } else {
        const int v20 = (v13 - v12) * (a10 - v14) / (v15 - v14);
        v14 = a10;
        v12 += v20;
    }
    ox0 = v12; oy0 = v14; ox1 = v13; oy1 = v15;
    return true;
}

// `sub_42EBA0` (0x0042EBA0): the rect ordered and clipped to the box.
bool clipRect(int a1, int a2, int a3, int a4, int a9, int a10, int a11, int a12,
              int& x0, int& y0, int& x1, int& y1) {
    int v14 = a1, v13 = a3;
    if (a1 > a3) { v14 = a3; v13 = a1; }
    int v16 = a2, v17 = a4;
    if (a2 > a4) { v16 = a4; v17 = a2; }
    if (!(v14 <= a11 && v16 <= a12)) return false;
    if (!(v13 >= a9 && v17 >= a10)) return false;
    x0 = std::max(v14, a9); y0 = std::max(v16, a10);
    x1 = std::min(v13, a11); y1 = std::min(v17, a12);
    return true;
}

// The opaque Gouraud line (flags 0x18: bit 3 shades, bits 0..2 clear so no
// blend): Bresenham, the last pixel left out, colour lerped per step.
void lineGouraud(Surface& fb, int x0, int y0, int x1, int y1,
                 std::uint32_t c0, std::uint32_t c1) {
    const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    const int n = std::max(dx, dy);
    if (n == 0) return;
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy, x = x0, y = y0;
    for (int i = 0; i < n; ++i) {
        if (x >= 0 && y >= 0 && x < fb.w && y < fb.h) {
            const auto ch = [&](int s) {
                const int a = static_cast<int>((c0 >> s) & 255u);
                const int b = static_cast<int>((c1 >> s) & 255u);
                return a + (b - a) * i / n;
            };
            fb.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.w) +
                  static_cast<std::size_t>(x)] = rgb565(ch(16), ch(8), ch(0));
        }
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx)  { err += dx; y += sy; }
    }
}

}  // namespace

RadarWire readRadarWire(std::span<const std::byte> d) {
    RadarWire w;
    if (d.size() < 8) return w;
    const std::uint32_t nv = u32le(d, 0), ne = u32le(d, 4);
    if (8ull + 12ull * nv + 4ull * ne != d.size()) return w;
    std::vector<float> v(static_cast<std::size_t>(nv) * 3);
    std::memcpy(v.data(), d.data() + 8, v.size() * sizeof(float));
    std::vector<std::uint16_t> e(static_cast<std::size_t>(ne) * 2);
    std::memcpy(e.data(), d.data() + 8 + 12ull * nv, e.size() * sizeof(std::uint16_t));
    for (const std::uint16_t i : e)
        if (i >= nv) return w;
    w.v = std::move(v);
    w.e = std::move(e);
    return w;
}

float radarHeightFor(const std::string& n) {
    // the nine literals at 0x4C4884, in the order 0x42EE70 tests them
    if (n == "SOUKT.WRE")    return 472.4409484863281f;
    if (n == "SMARKET1.WRE") return 492.1259765625f;
    if (n == "SOUKDOCK.WRE") return 492.1259765625f;
    if (n == "HAMES.WRE")    return 452.75592041015625f;
    if (n == "ARCHIV03.WRE" || n == "ARCHIV05.WRE") return 440.94488525390625f;
    if (n == "TETRADOU.WRE" || n == "TETRA2.WRE" || n == "TETRA3.WRE")
        return 492.1259765625f;
    return -1.0f;
}

bool Radar::load(const DataFs& fs, const std::string& mapFile) {
    // the file cleared first, whatever the name (the switch it also clears
    // is the Session's: `Session::fillSlotTables`)
    wire_ = RadarWire{};
    file_.clear();
    if (mapFile.size() < 3) return false;
    std::string n = mapFile;
    n.replace(n.size() - 3, 3, "WRE");
    const float h = radarHeightFor(n);
    if (h < 0.0f) return false;
    height_ = h;
    wire_ = readRadarWire(fs.read("RADAR/" + n));
    seen_.clear();
    if (wire_.valid()) file_ = n;
    return wire_.valid();
}

RadarFrame Radar::draw(Surface& fb, const RadarView& v, const std::vector<RadarActor>& actors) {
    RadarFrame out;
    // 0x42F000's own gate is the switch AND the file; the switch is the
    // caller's to test (the Session holds it)
    if (!wire_.valid()) return out;

    constexpr double kD2R = 0.017453292519943295;     // 0x4BC2E8
    const float yaw = static_cast<float>((double(v.facingDeg) + 180.0) * kD2R);
    float m[9];
    euler(static_cast<float>(90.0 * kD2R), yaw, 0.0f, m);

    // the local camera: the live one's focal lengths scaled to the box
    const int boxW = (v.right - v.left) & 0xFFFF, boxH = (v.bottom - v.top) & 0xFFFF;
    const float tanHalf = static_cast<float>(std::tan(double(v.fovDeg) * 0.5 * 0.0174532925199433));
    const float f228 = static_cast<float>(double(v.screenW >> 1) / tanHalf);
    const float f232 = static_cast<float>(double(v.screenH >> 1) * 1.3333334 / tanHalf);
    constexpr double k07 = 0.699999988079071;         // 0x4BC2F4
    const float fx = static_cast<float>(double(boxW) * f228 / double(v.screenW) * k07);
    const float fy = static_cast<float>(double(boxH) * f232 / double(v.screenH) * k07);
    const float cx = static_cast<float>((boxW >> 1) + v.left);
    const float cy = static_cast<float>((boxH >> 1) + v.top);

    const double theta = double(yaw) - 1.5707963267948966;   // 0x4BC2F8
    const float px = v.player[0], py = v.player[1], pz = v.player[2];
    const float TX = static_cast<float>(std::cos(theta) * distance_ - px);
    const float TY = py - height_;
    const float TZ = static_cast<float>(std::sin(theta) * distance_ - pz);

    // `sub_442F00` then `sub_441E50`; -> false behind the eye
    const auto project = [&](float x, float y, float z, int& sx, int& sy) {
        const float ox = static_cast<float>(double(m[0]) * x + double(m[6]) * z + double(m[3]) * y);
        const float oy = static_cast<float>(double(m[4]) * y + double(m[7]) * z + double(m[1]) * x);
        const float oz = static_cast<float>(double(m[8]) * z + double(m[5]) * y + double(m[2]) * x);
        if (!(oz >= 10.0f)) return false;
        const double inv = 1.0 / oz;
        sx = static_cast<int>(static_cast<float>(fx * ox * inv + cx));
        sy = static_cast<int>(static_cast<float>(fy * oy * inv + cy));
        return true;
    };
    // an end's grey, and whether it is above him
    const auto band = [&](float dh, bool& above) {
        constexpr float kReach = 118.11023712158203f;   // 0x4C4748, 3 m
        above = dh > 0.0f;
        if (dh > kReach) return 0xE0;
        if (dh > -kReach)
            return static_cast<int>(static_cast<std::uint8_t>(
                static_cast<int>(dh / kReach * 0x5F + 0x80)));
        return 0x20;
    };

    std::vector<Prim> prims;
    int seq = 0;
    for (int k = 0; k < wire_.edges(); ++k) {
        const int i = wire_.e[static_cast<std::size_t>(2 * k)];
        const int j = wire_.e[static_cast<std::size_t>(2 * k + 1)];
        const float* a = &wire_.v[static_cast<std::size_t>(3 * i)];
        const float* b = &wire_.v[static_cast<std::size_t>(3 * j)];
        int ax, ay, bx, by;
        if (!project(a[0] + TX, TY + a[1], -(TZ + a[2]), ax, ay)) continue;
        if (!project(b[0] + TX, TY + b[1], -(TZ + b[2]), bx, by)) continue;
        ++out.edgesInFront;
        bool aboveA, aboveB;
        const int ga = band(a[1] + py, aboveA);
        const int gb = band(b[1] + py, aboveB);
        Prim p;
        p.line = true;
        p.layer = 4 + int(aboveA) + int(aboveB);
        p.c0 = static_cast<std::uint32_t>(ga) * 0x010101u;
        p.c1 = static_cast<std::uint32_t>(gb) * 0x010101u;
        if (!clipSegment(ax, ay, bx, by, v.left, v.top, v.right, v.bottom,
                         p.x0, p.y0, p.x1, p.y1))
            continue;
        p.seq = seq++;
        prims.push_back(p);
        ++out.lines;
    }
    const auto square = [&](int sx, int sy, std::uint32_t c, int layer) {
        Prim p;
        p.layer = layer;
        p.c0 = c;
        if (!clipRect(sx - 4, sy - 4, sx + 4, sy + 4, v.left, v.top, v.right, v.bottom,
                      p.x0, p.y0, p.x1, p.y1))
            return false;
        p.seq = seq++;
        prims.push_back(p);
        return true;
    };
    {
        int sx, sy;
        if (project(px + TX, -(py - TY), -(pz + TZ), sx, sy)) {
            out.player = square(sx, sy, 0x0000FFu, 6);
            out.playerX = sx;
            out.playerY = sy;
        }
    }
    for (const RadarActor& ac : actors) {
        int red = 0;
        if (seen_[ac.id] && ac.state == 0) red = 0x10;
        if (ac.state == 3) {
            seen_[ac.id] = true;
            constexpr float kReach = 314.96063232421875f;   // 0x4C474C, 8 m
            const float dh = py - ac.pos[1];
            if (dh > kReach) red = 0xE0;
            else if (dh > -kReach) red = 0x80 + static_cast<int>(31.0f * (dh / kReach));
            else red = 0x20;
            red &= 0xFF;
        }
        if (red == 0) continue;
        int sx, sy;
        if (!project(ac.pos[0] + TX, -(ac.pos[1] - TY), -(ac.pos[2] + TZ), sx, sy)) continue;
        if (square(sx, sy, static_cast<std::uint32_t>(red) << 16, 5)) ++out.blips;
    }

    // the I2D walk: layers ascending, a layer's nodes in reverse submission
    std::sort(prims.begin(), prims.end(), [](const Prim& a, const Prim& b) {
        return a.layer != b.layer ? a.layer < b.layer : a.seq > b.seq;
    });
    for (const Prim& p : prims) {
        if (p.line) {
            lineGouraud(fb, p.x0, p.y0, p.x1, p.y1, p.c0, p.c1);
        } else {
            const int xs[4] = {p.x0, p.x1, p.x1, p.x0};
            const int ys[4] = {p.y0, p.y0, p.y1, p.y1};
            fillQuadD3d(fb, xs, ys, p.c0, 4u);
        }
    }
    return out;
}

}  // namespace omk

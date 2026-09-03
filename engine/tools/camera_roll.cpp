// SPDX-License-Identifier: GPL-3.0-or-later
// THE CAMERA ROLL - the rotation about the forward axis, which the renderer
// dropped until 2026-09-03.
//
//     camera_roll <gamedata> <out.bin>
//
// A camera record carries a roll and `RCamera` had no field for it, so every
// rolled shot in the game drew upright. It is invisible in any single still
// frame - which is exactly why it survived here for months after the web
// viewer was fixed (CLAUDE.md 1, "some errors are invisible at rest").
//
// The SENSE is derived, not chosen. `sub_442400` turns a direction and a roll
// into `sub_441FF0(pitch, yaw, roll)`, which `o3de/setpiece.cpp`'s
// `headingMatrix` transcribes verbatim; that matrix's COLUMNS are this basis -
// column 0 is `s`, column 1 is `-u` (the game's Y points down), column 2 is
// `f`. So rolling the basis and reading the engine's columns must agree, and
// this asserts they do over a spread of directions and rolls.
//
// Then the CORPUS: how many shipped cameras actually carry a roll, because a
// fix nothing exercises is a fix nobody can see.
#include "formats/scx.h"
#include "o3de/camedit.h"
#include "o3de/raster.h"
#include "o3de/setpiece.h"
#include "o3de/worldcam.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: camera_roll <gamedata> <out.bin>\n");
        return 2;
    }
    const std::string fr = argv[1];

    // ---- the basis agrees with the engine's own matrix, at every roll -----
    const float dirs[6][3] = {{0,0,1},{1,0,0},{0.3f,-0.5f,0.8f},
                              {-0.7f,0.2f,-0.6f},{0,0.4f,-0.9f},{0.6f,0.6f,0.5f}};
    const float rolls[5] = {0.0f, 30.0f, -47.5f, 120.0f, 179.0f};
    int agree = 0, tried = 0;
    double worst = 0.0;
    for (const auto& d : dirs)
        for (float roll : rolls) {
            ++tried;
            omk::RCamera c;
            for (int k = 0; k < 3; ++k) { c.eye[k] = 0; c.at[k] = d[k]; }
            c.rollDeg = roll;
            float s[3], u[3], f[3], th, tv;
            omk::cameraBasis(c, s, u, f, th, tv);
            omk::RCamera z = c; z.rollDeg = 0;
            float s0[3], u0[3], f0[3];
            omk::cameraBasis(z, s0, u0, f0, th, tv);
            float m[9];
            omk::headingMatrix(f0, roll, m);
            const float col0[3] = {m[0], m[3], m[6]};
            const float col1[3] = {-m[1], -m[4], -m[7]};
            double e = 0;
            for (int k = 0; k < 3; ++k) {
                e = std::max(e, std::fabs(double(s[k]) - col0[k]));
                e = std::max(e, std::fabs(double(u[k]) - col1[k]));
            }
            worst = std::max(worst, e);
            if (e < 1e-3) ++agree;
        }

    // ...and a roll must actually TURN the basis: at 90 degrees the right axis
    // must land on the old up axis, which a dropped roll cannot do.
    omk::RCamera a; a.at[2] = 1.0f; a.rollDeg = 90.0f;
    float s[3], u[3], f[3], th, tv;
    omk::cameraBasis(a, s, u, f, th, tv);
    omk::RCamera b0 = a; b0.rollDeg = 0.0f;
    float s0[3], u0[3], f0[3];
    omk::cameraBasis(b0, s0, u0, f0, th, tv);
    double dot = 0, turn = 0;
    for (int k = 0; k < 3; ++k) { dot += double(s[k]) * u0[k]; turn += double(s[k]) * s0[k]; }
    // ...and it lands on MINUS the old up, not plus: the basis carries the
    // game's Y-down convention (world up is (0,-1,0)), so a positive roll
    // takes the right axis onto -u0. Asserted with the sign rather than an
    // absolute value, because the sign is the half that can go wrong.
    const int landsOnUp = std::fabs(dot + 1.0) < 1e-3 ? 1 : 0;
    const int leftOldRight = std::fabs(turn) < 1e-3 ? 1 : 0;

    // ---- the corpus: cameras that carry a roll ---------------------------
    const omk::DataFs scp(fr + "/SCPTDATA");
    auto scenes = scp.list(".", ".SCX");
    std::sort(scenes.begin(), scenes.end());
    long editCams = 0, editRolled = 0;
    for (const auto& p : scenes) {
        const auto d = omk::DataFs::readPath(p);
        const auto st = omk::readScxStream(d);
        if (!st.valid || !st.camSize || d.size() < st.camOffset + st.camSize) continue;
        const auto cf = omk::readCamFile(
            std::span<const std::byte>(d).subspan(st.camOffset, st.camSize));
        for (const auto& c : cf.cameras) {
            ++editCams;
            if (std::fabs(c.roll) > 0.01f) ++editRolled;
        }
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u2 = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u2 >> (8 * k)));
    };
    put32(tried); put32(agree); put32(landsOnUp); put32(leftOldRight);
    put32(static_cast<std::int32_t>(editCams));
    put32(static_cast<std::int32_t>(editRolled));
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(o.data()),
              static_cast<std::streamsize>(o.size()));

    std::printf("basis vs the engine's own matrix: %d of %d agree (worst %.2e)\n",
                agree, tried, worst);
    std::printf("a 90-degree roll puts the right axis on MINUS the old up: %s, "
                "and off the old right: %s\n",
                landsOnUp ? "yes" : "NO", leftOldRight ? "yes" : "NO");
    std::printf("%ld editing cameras, %ld of them rolled\n", editCams, editRolled);
    return 0;
}

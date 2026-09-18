// SPDX-License-Identifier: GPL-3.0-or-later
// THE FIRST THING ON THE VITA'S GPU - `todo/vita-port.md` step G3.
//
// One set, through `GlesRenderer` on vitaGL, presented at 960x544, with the
// left stick flying the camera. It is the smallest program that answers the
// questions the Mac cannot:
//
//   * does vitaGL take this backend at all - its GLSL ES 1.00 through the
//     run-time translator, the 16-bit depth renderbuffer, the 565 texture
//     upload, `glBufferSubData` on a buffer in use;
//   * what a frame of a whole set costs on the device's GPU and in the
//     driver's CPU half, which the M1 profile could not see
//     (`handoff-vita.md` §1's caveat);
//   * whether the depth tie is needed on this depth buffer: SQUARE toggles
//     it, so the coincident-face panels can be LOOKED at both ways
//     (CLAUDE.md §6, the Anekbah signs - the port's own flicker).
//
//     controls  left stick  move            right stick  look
//               L / R       down / up       SQUARE       depth tie on/off
//               TRIANGLE    stretch/aspect  START        quit
//
// It writes `ux0:data/omk/smoke.txt`: the GL strings, then one line every 300
// frames with the mean frame time, the CPU time spent submitting, and the
// triangle counts. Data is read from `ux0:data/omk/gamedata` - the game's own
// tree, copied from the disc; nothing of it is in the VPK.
//
// **An instrument, not the port**: it plays no script and poses no one. Its
// only claim is about the renderer.
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <vitaGL.h>

#include "formats/tex3dt.h"
#include "o3de/geom3do.h"
#include "o3de/renderer.h"
#include "platform/datafs.h"

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

extern "C" { int _newlib_heap_size_user = 192 * 1024 * 1024; }

namespace omk {
Renderer* makeGlesRenderer();
bool glesPresentWorld(Renderer*, int vy, int vh, int frameW, int frameH, int winW, int winH);
void glesSetStretch(Renderer*, bool);
void glesSetDepthTie(Renderer*, bool);
}

namespace {
constexpr int kWinW = 960, kWinH = 544;
constexpr int kW = 640, kH = 480;   // the engine's frame
constexpr const char* kRoot = "ux0:data/omk/gamedata";
constexpr const char* kSet  = "MESHES/DECORS/Aapkayl.3DO";
// dialog 402's camera, the one the Vulkan backend's tier-4 claim rests on
constexpr float kEye[3] = {3526, 1015, -905}, kAt[3] = {3412, 1032, -882};
constexpr float kFov = 83.0f;

std::FILE* g_log = nullptr;
void say(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    if (g_log) { std::vfprintf(g_log, fmt, a); std::fflush(g_log); }
    va_end(a);
}
}  // namespace

int main() {
    scePowerSetArmClockFrequency(444);
    scePowerSetGpuClockFrequency(222);
    g_log = std::fopen("ux0:data/omk/smoke.txt", "w");
    vglInitExtended(0, kWinW, kWinH, 0x1800000, SCE_GXM_MULTISAMPLE_NONE);
    say("gl: %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
        reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);

    const omk::DataFs fs(kRoot);
    const auto path = fs.resolve(kSet);
    if (!path) { say("no %s under %s\n", kSet, kRoot); sceKernelExitProcess(1); return 1; }
    const auto d = omk::DataFs::readPath(*path);
    const auto t = fs.readSibling(kSet, ".3DT");
    const auto geo = omk::buildGeometry(d, omk::DrawFilter::Engine);
    const auto tex = omk::textures(d, t);
    std::vector<omk::Draw> draws;
    for (const auto& b : geo.batches) {
        omk::Draw dr;
        dr.bucketKey = static_cast<std::uint32_t>(b.material) & 0x3Fu;
        dr.geo = &geo; dr.start = b.start; dr.count = b.count;
        dr.blend = b.blend; dr.cutout = b.cutout;
        draws.push_back(dr);
    }
    say("set %s: %zu triangles, %zu textures, %zu batches\n", kSet,
        geo.corners.size() / 3, tex.size(), draws.size());

    omk::Renderer* r = omk::makeGlesRenderer();
    if (!r->init(kW, kH)) { say("gles renderer: init failed\n"); sceKernelExitProcess(1); return 1; }
    r->setTextures(tex);

    omk::View v;
    for (int k = 0; k < 3; ++k) { v.cam.eye[k] = kEye[k]; v.cam.at[k] = kAt[k]; }
    v.cam.hfovDeg = kFov;
    v.cam.w = kW; v.cam.h = kH;
    // heading and pitch from the start camera, so the stick flies from there
    float dir[3] = {kAt[0] - kEye[0], kAt[1] - kEye[1], kAt[2] - kEye[2]};
    float yaw = std::atan2(dir[0], dir[2]);
    float pitch = std::atan2(dir[1], std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]));

    bool tie = true, stretch = false;
    std::uint32_t prev = 0;
    using Clock = std::chrono::steady_clock;
    double sumFrame = 0, sumSubmit = 0;
    int n = 0;
    auto last = Clock::now();
    for (;;) {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0, &pad, 1);
        const std::uint32_t pressed = pad.buttons & ~prev;
        prev = pad.buttons;
        if (pad.buttons & SCE_CTRL_START) break;
        if (pressed & SCE_CTRL_SQUARE) { tie = !tie; omk::glesSetDepthTie(r, tie); say("depth tie %s\n", tie ? "ON" : "OFF"); }
        if (pressed & SCE_CTRL_TRIANGLE) { stretch = !stretch; omk::glesSetStretch(r, stretch); }

        const auto axis = [](unsigned char a) {
            const int c = static_cast<int>(a) - 128;
            return (c > -24 && c < 24) ? 0.0f : static_cast<float>(c) / 128.0f;
        };
        yaw   += axis(pad.rx) * 0.04f;
        pitch += axis(pad.ry) * 0.03f;   // Y points DOWN in the game's world
        const float fwd[3] = {std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                              std::cos(yaw) * std::cos(pitch)};
        const float side[3] = {std::cos(yaw), 0.0f, -std::sin(yaw)};
        const float speed = 12.0f;
        for (int k = 0; k < 3; ++k)
            v.cam.eye[k] += (-axis(pad.ly) * fwd[k] + axis(pad.lx) * side[k]) * speed;
        if (pad.buttons & SCE_CTRL_LTRIGGER) v.cam.eye[1] += speed;
        if (pad.buttons & SCE_CTRL_RTRIGGER) v.cam.eye[1] -= speed;
        for (int k = 0; k < 3; ++k) v.cam.at[k] = v.cam.eye[k] + fwd[k] * 100.0f;

        const auto t0 = Clock::now();
        r->begin(v);
        for (const auto& dr : draws) r->submit(dr);
        r->end();
        omk::glesPresentWorld(r, 0, kH, kW, kH, kWinW, kWinH);
        sumSubmit += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        vglSwapBuffers(GL_FALSE);

        const auto now = Clock::now();
        sumFrame += std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
        if (++n == 300) {
            const auto st = r->stats();
            say("300 frames: %.2f ms/frame, %.2f ms submitting (CPU), %ld tri offered, "
                "%ld drawn, tie %s\n", sumFrame / n, sumSubmit / n, st.triangles, st.drawn,
                tie ? "on" : "off");
            n = 0; sumFrame = sumSubmit = 0;
        }
    }
    delete r;
    // no vglEnd in this vitaGL (r1448): exiting the process releases the GPU
    if (g_log) std::fclose(g_log);
    sceKernelExitProcess(0);
    return 0;
}

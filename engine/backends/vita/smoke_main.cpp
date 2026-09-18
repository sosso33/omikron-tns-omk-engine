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
// With `ux0:data/omk/smoke.autoexit` present it quits by itself after 900
// frames - how `scripts/vita3k-run.sh` gets a flushed log out of the emulator.
// With `ux0:data/omk/smoke.bars` present it shows eight COLOUR BARS through
// the CPU-surface present path instead of the set (white, yellow, cyan,
// green, magenta, red, blue, black, top to bottom a 565 ramp) - the control
// that separates a colour fault in the 3D pass from one in the upload.
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
#include "ui/surface.h"

#include <algorithm>
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
bool glesPresentSurface(Renderer*, const Surface&, int winW, int winH);
void glesSetStretch(Renderer*, bool);
void glesSetDepthTie(Renderer*, bool);
void glesSetWindowTarget(Renderer*, unsigned fbo);
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

    // ---- PRESENT = READBACK, on the device (`gles_probe`'s exact check).
    // One frame of the set; the CPU's 888 -> 565 rule (`readback`, through
    // `quantise888DitherRow`) against the GPU present shader's, drawn into an
    // offscreen target of the frame's own size and read back - compared in
    // 565, dithered and not. A mismatch is logged with its pixel, the source
    // 888 value and both 565 answers, so a fault in how THIS GPU's compiler
    // runs the shader is located, not guessed.
    for (const int scaled : {0, 1}) {
        // at the frame's own size, and at the WINDOW's (960x544, pillarboxed
        // the way the screen shows it) - each window pixel compared against
        // the picture pixel the shader maps it to
        const int TW = scaled ? kWinW : kW, TH = scaled ? kWinH : kH;
        const int dw = scaled ? kWinH * kW / kH : kW;          // the fitted width
        const int dx0 = (TW - dw) / 2;
        GLuint tex = 0, fbo = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TW, TH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        std::vector<unsigned char> win(static_cast<std::size_t>(TW) * TH * 4);
        std::vector<unsigned char> raw(static_cast<std::size_t>(kW) * kH * 4);
        for (int dither = 0; dither <= 1; ++dither) {
            omk::View tv = v;
            tv.dither = dither != 0;
            r->begin(tv);
            for (const auto& dr : draws) r->submit(dr);
            r->end();
            const omk::Surface rb = r->readback();
            // the 888 the world target holds, for the log
            glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
            omk::glesSetWindowTarget(r, fbo);
            omk::glesPresentWorld(r, 0, kH, kW, kH, TW, TH);
            omk::glesSetWindowTarget(r, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glReadPixels(0, 0, TW, TH, GL_RGBA, GL_UNSIGNED_BYTE, win.data());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            long bad = 0, shown = 0, compared = 0;
            double sum[2][3] = {{0, 0, 0}, {0, 0, 0}};
            for (int wy = 0; wy < TH; ++wy)
                for (int wx = dx0; wx < dx0 + dw; ++wx) {
                    // the picture pixel under this window pixel's centre
                    const int x = std::min(kW - 1, (2 * (wx - dx0) + 1) * kW / (2 * dw));
                    const int y = std::min(kH - 1, (2 * wy + 1) * kH / (2 * TH));
                    // at the boundary between two picture pixels the float
                    // mapping may pick either; skip those rather than count them
                    const float fx = ((wx - dx0) + 0.5f) * kW / dw, fy = (wy + 0.5f) * kH / TH;
                    if (fx - static_cast<float>(static_cast<int>(fx)) < 0.02f ||
                        fy - static_cast<float>(static_cast<int>(fy)) < 0.02f) continue;
                    const std::size_t gl = (static_cast<std::size_t>(TH - 1 - wy) * TW + wx) * 4;
                    const std::uint16_t got = omk::rgb565(win[gl], win[gl + 1], win[gl + 2]);
                    const std::uint16_t want = rb.at(x, y);
                    for (int k = 0; k < 3; ++k) sum[0][k] += win[gl + k];
                    const int wr = ((want >> 11) & 31) << 3, wg = ((want >> 5) & 63) << 2,
                              wb = (want & 31) << 3;
                    sum[1][0] += wr; sum[1][1] += wg; sum[1][2] += wb;
                    ++compared;
                    if (got == want) continue;
                    const std::size_t src = (static_cast<std::size_t>(kH - 1 - y) * kW + x) * 4;
                    if (shown++ < 4)
                        say("  %s %s: win (%d,%d) pic (%d,%d) src %d %d %d  gpu %04x  cpu %04x\n",
                            scaled ? "scaled" : "1:1", dither ? "dithered" : "plain", wx, wy,
                            x, y, raw[src], raw[src + 1], raw[src + 2], got, want);
                    ++bad;
                }
            // A BLACK FRAME ANSWERS "EXACT" FOR NOTHING. In Vita3K the rendered
            // target never reaches emulated memory, so glReadPixels returns
            // zeros and both sides agree trivially - which is exactly how this
            // test first reported EXACT (2026-09-18). An empty input is its
            // own verdict, never a pass.
            long lit = 0;
            for (const auto px : rb.px) lit += px != 0;
            const char* verdict = lit == 0 ? "INPUT EMPTY (no lit pixel read back - not a result)"
                                : bad ? "DIFFERENT" : "EXACT";
            say("present = readback, %s %s: %s (%ld of %ld differ, %ld lit; mean gpu %.1f %.1f %.1f "
                "cpu %.1f %.1f %.1f)\n", scaled ? "scaled 960x544" : "1:1 640x480",
                dither ? "dithered" : "plain", verdict, bad, compared, lit,
                sum[0][0] / compared, sum[0][1] / compared, sum[0][2] / compared,
                sum[1][0] / compared, sum[1][1] / compared, sum[1][2] / compared);
        }
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
    }
    // heading and pitch from the start camera, so the stick flies from there
    float dir[3] = {kAt[0] - kEye[0], kAt[1] - kEye[1], kAt[2] - kEye[2]};
    float yaw = std::atan2(dir[0], dir[2]);
    float pitch = std::atan2(dir[1], std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]));

    bool tie = true, stretch = false;
    long total = 0;
    const auto fileExists = [](const char* p) {
        std::FILE* f = std::fopen(p, "r");
        if (f) std::fclose(f);
        return f != nullptr;
    };
    const bool bars = fileExists("ux0:data/omk/smoke.bars");
    // `smoke.worldbars`: the same bars CLEARED into the world target and shown
    // through the WORLD present (the 565 quantise + dither shader), no scene
    // shader; `smoke.nodither`: the set with the View's dither off.
    const bool worldBars = fileExists("ux0:data/omk/smoke.worldbars");
    if (fileExists("ux0:data/omk/smoke.nodither")) v.dither = false;
    say("modes: bars %d worldbars %d dither %d\n", bars ? 1 : 0, worldBars ? 1 : 0,
        v.dither ? 1 : 0);
    omk::Surface barPic(kW, kH, 0);
    if (bars) {
        const int col[8][3] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
                               {255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0}};
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                const int b = x * 8 / kW;
                // the bottom quarter is a grey ramp, for the channel depths
                const int g = x * 255 / (kW - 1);
                barPic.set(x, y, y < kH * 3 / 4 ? omk::rgb565(col[b][0], col[b][1], col[b][2])
                                                : omk::rgb565(g, g, g));
            }
        say("bars: the CPU-surface present path, no 3D\n");
    }
    const bool autoExit = [] {
        std::FILE* f = std::fopen("ux0:data/omk/smoke.autoexit", "r");
        if (f) std::fclose(f);
        return f != nullptr;
    }();
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
        if (autoExit && ++total > 900) break;
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
        if (bars) {
            omk::glesPresentSurface(r, barPic, kWinW, kWinH);
        } else if (worldBars) {
            r->begin(v);   // binds the world target
            const float col[8][3] = {{1, 1, 1}, {1, 1, 0}, {0, 1, 1}, {0, 1, 0},
                                     {1, 0, 1}, {1, 0, 0}, {0, 0, 1}, {0, 0, 0}};
            glEnable(GL_SCISSOR_TEST);
            for (int b = 0; b < 8; ++b) {
                glScissor(b * kW / 8, 0, kW / 8, kH);
                glClearColor(col[b][0], col[b][1], col[b][2], 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            glDisable(GL_SCISSOR_TEST);
            r->end();
            omk::glesPresentWorld(r, 0, kH, kW, kH, kWinW, kWinH);
        } else {
            r->begin(v);
            for (const auto& dr : draws) r->submit(dr);
            r->end();
            omk::glesPresentWorld(r, 0, kH, kW, kH, kWinW, kWinH);
        }
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

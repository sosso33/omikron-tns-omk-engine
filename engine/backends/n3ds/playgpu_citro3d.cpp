// SPDX-License-Identifier: GPL-3.0-or-later
// THE GPU WINDOW, CITRO3D - `PlayState::gpu...` for the Nintendo 3DS build
// (`todo/3ds-port.md` step 3). The world is drawn by the citro3d backend
// (`c3drender.h`) into its own offscreen target; a frame with anything over
// it is read back and composited on the CPU as the software reference's is
// (the SDL host's GL1 shape), and a frame with nothing over it goes straight
// to the top screen (3c, `gpuPresentWorld` below).
//
// `--software` keeps the software reference, for laying the two side by side
// (a 3DS has no environment to set a variable in); so does a console where
// `init()` says no.
#include "playframe.h"
#include "c3drender.h"
#include "n3dsfront.h"
#include "n3dshost.h"
#include "platform/profile.h"

#include <3ds.h>
#include <malloc.h>

// THE STRAIGHT PRESENT (3c): the build decides, frame by frame, whether
// anything is drawn over the world - the GLES window's "present pass", every
// gate in `playframe_world_draw.cpp` - and a frame nothing is drawn over goes
// to the top screen already halved by the GPU (`c3dPresentHalf`), with no
// readback, no composite and no CPU halving. The rest keep the CPU path.
bool PlayState::gpuWindowBuild() const { return true; }
void PlayState::gpuOpenWindow() {}
void PlayState::gpuOpenWorldHarness() {
    if (forceSoftware) {
        std::printf("renderer: --software - the software reference\n");
        return;
    }
    omk::Renderer* r = omk::makeC3dRenderer();
    if (texFilter > 0) r->setTextureFilter(texFilter);
    if (r->init(dispW, dispH)) {
        worldVk = r;
        std::printf("renderer: the world through %s (offscreen; the frame is presented on the CPU)\n",
                    r->name());
    } else {
        delete r;
        std::printf("renderer: no citro3d - the software reference\n");
    }
}
bool PlayState::gpuPresentSurface(const omk::Surface&) { return false; }
void PlayState::gpuPresentVerify(bool&) {}
// THE INTERFACE OVER THE WORLD ON THE GPU (`todo/3ds-port.md` 6.7): a frame
// with an interface on it (a soft gate - a conversation, a fade, a HUD, an
// open screen) is composed over the KEY as on the GLES and GL1 windows
// (`ui/overlay.h`), resolved here into C and M, and blended over the world in
// its own GPU frame (`c3dPresentOverlay`) - no readback, no CPU composite of
// the world, and the frame stays one behind like a plain one. A frame that
// cannot go that way is resolved on the CPU from a readback, so the key never
// reaches the screen. `OMK_NO_OVERLAY=1` or `sdmc:/omk/c3d-nooverlay` keep
// the readback path for every such frame, to lay the two side by side.
void PlayState::gpuPresentOverlay(bool& presentedWorld) {
    if (!overlayFrame || !worldVk || presentedWorld) return;
    // the key resolved into the two planes (`playgpu_gl1.cpp`'s loop): C
    // into `fb`, M beside it, on the rows a pass touched
    static std::vector<std::uint8_t> ovMask;
    if (ovMask.size() != fb.px.size()) ovMask.assign(fb.px.size(), std::uint8_t(0));
    for (int y = 0; y < fb.h; ++y) {
        const std::size_t o = static_cast<std::size_t>(y) * fb.w;
        if (!g_ov.rowInit[static_cast<std::size_t>(y)]) {
            std::fill(ovMask.begin() + static_cast<long>(o), ovMask.begin() + static_cast<long>(o + fb.w),
                      std::uint8_t(0));
            continue;
        }
        for (std::size_t i = o; i < o + static_cast<std::size_t>(fb.w); ++i) {
            if (fb.px[i] == omk::kOverlayKey) { fb.px[i] = g_ov.c[i]; ovMask[i] = g_ov.m[i]; }
            else ovMask[i] = 0;
        }
    }
    if (omk::N3dsFrontend* f = omk::liveN3dsFrontend()) {
        std::uint16_t* dst = f->topFramebuffer();
        if (dst && omk::c3dPresentOverlay(worldVk, fb, ovMask.data(), g_ov.rowInit.data(), ovFade, gpuVy, gpuVh,
                                          dst)) {
            f->presentWritten();
            presentedWorld = true;
            return;
        }
    }
    // THE FALLBACK: the world read back and the frame resolved on the CPU -
    // `C + world * M` per channel where the planes are, the world where the
    // key is - then the colour fade the shared code left to the GPU; the
    // caller presents `fb` as any composed frame
    const omk::Surface& pic = worldVk->readback();
    static long fellBack = 0;
    if (++fellBack == 1) std::printf("overlay: a frame resolved on the CPU (the GPU path refused it)\n");
    for (int y = 0; y < gpuVh && y < pic.h; ++y) {
        const int fy = gpuVy + y;
        if (fy < 0 || fy >= fb.h) continue;
        for (int x = 0; x < fb.w && x < pic.w; ++x) {
            const std::size_t i = static_cast<std::size_t>(fy) * fb.w + x;
            const std::uint16_t w = pic.px[static_cast<std::size_t>(y) * pic.w + x];
            if (fb.px[i] == omk::kOverlayKey) { fb.px[i] = w; continue; }
            const unsigned m = ovMask[i];
            if (!m) continue;
            const std::uint16_t c = fb.px[i];
            const unsigned r = std::min(31u, ((c >> 11) & 31u) + (((w >> 11) & 31u) * m + 127) / 255);
            const unsigned g = std::min(63u, ((c >> 5) & 63u) + (((w >> 5) & 63u) * m + 127) / 255);
            const unsigned b = std::min(31u, (c & 31u) + ((w & 31u) * m + 127) / 255);
            fb.px[i] = static_cast<std::uint16_t>(r << 11 | g << 5 | b);
        }
    }
    if (ovFade[3] > 0.0f) {
        const float k = ovFade[3];
        const int cr = static_cast<int>(ovFade[0] * 255.0f + 0.5f), cg = static_cast<int>(ovFade[1] * 255.0f + 0.5f),
                  cb = static_cast<int>(ovFade[2] * 255.0f + 0.5f);
        for (std::uint16_t& px : fb.px) {
            int r = ((px >> 11) & 31) << 3, g = ((px >> 5) & 63) << 2, b = (px & 31) << 3;
            r += static_cast<int>((cr - r) * k);
            g += static_cast<int>((cg - g) * k);
            b += static_cast<int>((cb - b) * k);
            px = static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}
void PlayState::gpuPresentWorld(bool& presentedWorld) {
    if (presentedWorld || !worldVk) return;
    // THE DIRECT PRESENT (6.4): dithered straight into the top screen's
    // framebuffer in one pass; the two passes below when it cannot
    if (omk::N3dsFrontend* f = omk::liveN3dsFrontend()) {
        std::uint16_t* dst = f->topFramebuffer();
        if (dst && omk::c3dPresentHalfDirect(worldVk, gpuVy, gpuVh, dst)) {
            f->presentWritten();
            presentedWorld = true;
            return;
        }
    }
    static omk::Surface screen(400, 240, 0);
    if (!omk::c3dPresentHalf(worldVk, gpuVy, gpuVh, screen)) return;
    front.present(screen);          // 400x240: the frontend copies it 1:1, its stats and capture as ever
    presentedWorld = true;
}
bool PlayState::gpuWorldOnWindow() { return worldVk && world_ == worldVk; }
void PlayState::gpuOverlayDecision(const char*& keep) {
    static const bool noOverlay = omk::envSet("OMK_NO_OVERLAY");
    omk::N3dsFrontend* f = omk::liveN3dsFrontend();
    overlayFrame = softGate && !noOverlay && worldVk && world_ == worldVk && f && f->topFramebuffer() &&
                   omk::c3dOverlayReady(worldVk);
    if (overlayFrame) {
        static std::string overlayWhy;
        overlayWhy = std::string("overlay (") + keep + ")";
        keep = overlayWhy.c_str();
    }
}
void PlayState::gpuResize(int, int, bool&) {}
bool PlayState::gpuDriverRow(std::vector<std::string>&) { return false; }
// every 60 frames, beside the phases line: the backend's own counts
// every 60 frames, beside the phases line: the backend's own counts, and
// THE MEMORY (`todo/3ds-port.md` step 4) - the heap's high-water mark
// (`mallinfo().arena`: newlib's heap grows and never gives back, so it is the
// footprint the run needed) and what is in use, the profiler's counted peak,
// linear memory and VRAM free now and at the lowest seen
void PlayState::gpuReportTimings() {
    if (worldVk) omk::c3dReport(worldVk, n);
    const struct mallinfo mi = mallinfo();
    static u32 linMin = ~0u, vramMin = ~0u;
    const u32 lin = linearSpaceFree(), vram = vramSpaceFree();
    linMin = std::min(linMin, lin);
    vramMin = std::min(vramMin, vram);
    const omk::prof::MemTotals mt = omk::prof::memTotals();
    std::printf("frame %ld memory: heap %.1f MB in use, %.1f MB high water, of %.1f; counted peak %.1f MB; "
                "linear %.1f MB free (lowest %.1f); VRAM %.2f MB free (lowest %.2f)\n", n,
                mi.uordblks / 1048576.0, mi.arena / 1048576.0, omk::n3ds::heapBytes() / 1048576.0,
                mt.peak / 1048576.0, lin / 1048576.0, linMin / 1048576.0, vram / 1048576.0,
                vramMin / 1048576.0);
}
void PlayState::gpuSlowFrameReport() {}
void PlayState::gpuFinishReport() {}
void PlayState::gpuVerifyWorldPicture() {}

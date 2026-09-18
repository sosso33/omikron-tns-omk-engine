// SPDX-License-Identifier: GPL-3.0-or-later
// THE GLES2 BACKEND AGAINST THE REFERENCE - `run_vulkan`'s differential, for
// the backend the PS Vita will draw with, run on the machine the port is
// developed on (`todo/vita-port.md` G2).
//
//     gles_probe <gamedata> <model.3DO> <eye> <at> <hfov> [WxH]
//
// Draws one set through `SoftwareRenderer` (the reference `verify.py` checks)
// and through `GlesRenderer` (backends/gles/glesrender.cpp), same camera, and
// prints four things:
//
//   1. COVERAGE AGREEMENT - both lit / either lit, the number `run_vulkan`
//      quotes (0.995 for Vulkan on the same set). Exact per-pixel equality is
//      NOT a criterion for a GPU backend; it is printed and not judged.
//   2. the same, DITHERED, since the dither is on by default in the game.
//   3. PRESENT = READBACK, which IS exact and is judged: the present pass
//      quantises 888 -> 565 in a GLSL ES 1.00 float shader, the readback
//      through `quantise888DitherRow` in C++. Every pixel must agree, dithered
//      and not, or the Vita's screen and the port's checks disagree about the
//      picture. `present: EXACT` or `present: N DIFFERENT`.
//   4. THE LETTERBOX - the same check with a 640x352 picture in a 640x480
//      frame, which is the case the present pass's row arithmetic can get
//      wrong.
//
// HEADLESS on macOS: a CGL context with no drawable, and the present pass
// pointed at an offscreen FBO (`glesSetWindowTarget`), so nothing opens a
// window and nothing takes the keyboard (CLAUDE.md §5's `SDL_VIDEODRIVER`
// lesson does not arise). The OpenGL framework is deprecated on macOS and
// still ships the 2.1 legacy profile, which is all this needs.
//
// Build (from engine/, after `make` has built the objects):
//
//     c++ -std=c++20 -O2 -Isrc -Ithird_party -o build/gles_probe \
//         backends/gles/gles_probe.cpp backends/gles/glesrender.cpp \
//         build/obj/src/*/*.o -framework OpenGL
//
// Exit status: 0 when the present checks are exact and coverage is at least
// 0.99, 1 otherwise - so a script can use it before `verify.py` knows of it.
// Prints only; writes nothing.
#if !defined(__APPLE__)
#  error "gles_probe makes its context with CGL; on another host, bring one up with EGL"
#endif
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>

#include "formats/tex3dt.h"
#include "o3de/geom3do.h"
#include "o3de/renderer.h"
#include "platform/datafs.h"
#include "ui/surface.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace omk {
Renderer* makeGlesRenderer();
bool glesPresentWorld(Renderer*, int vy, int vh, int frameW, int frameH, int winW, int winH);
bool glesPresentSurface(Renderer*, const Surface&, int winW, int winH);
void glesSetWindowTarget(Renderer*, unsigned fbo);
}

namespace {

void triple(const char* s, float o[3]) { std::sscanf(s, "%f,%f,%f", &o[0], &o[1], &o[2]); }


struct Coverage { long sw = 0, gl = 0, both = 0, differ = 0; double agree() const {
    const long e = sw + gl - both; return e ? double(both) / double(e) : 1.0; } };

Coverage compare(const omk::Surface& a, const omk::Surface& b) {
    Coverage c;
    for (std::size_t i = 0; i < a.px.size() && i < b.px.size(); ++i) {
        const bool la = a.px[i] != 0, lb = b.px[i] != 0;
        c.sw += la; c.gl += lb; c.both += la && lb;
        c.differ += a.px[i] != b.px[i];
    }
    return c;
}

// An offscreen "window" to present into and read back.
struct Window {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    bool make(int ww, int hh) {
        w = ww; h = hh;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return ok;
    }
    // top-left origin RGBA
    std::vector<unsigned char> read() const {
        std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4), out(px.size());
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        for (int y = 0; y < h; ++y)
            std::copy_n(px.data() + static_cast<std::size_t>(h - 1 - y) * w * 4, w * 4,
                        out.data() + static_cast<std::size_t>(y) * w * 4);
        return out;
    }
};

// The window, 1:1 with the engine's frame, against the readback: the picture's
// rows `vy .. vy+vh` must be exactly the readback's rows, and every other row
// black. COMPARED IN 565, `surface.h`'s rule: how a 565 value becomes 888 on
// the way to the screen is the HOST's (this Mac's driver expands a 565
// texture by rounding, so blue 3 shows as 25 where bit replication gives 24;
// the first version of this probe compared in 888 and reported 113303
// "differences" that were all that), and it is not what is under test.
// Truncating the window back to 565 inverts both expansions exactly.
// -> how many pixels disagree.
long presentMismatches(const Window& win, const omk::Surface& rb, int vy) {
    const auto px = win.read();
    long bad = 0;
    for (int y = 0; y < win.h; ++y)
        for (int x = 0; x < win.w; ++x) {
            const unsigned char* p = px.data() + (static_cast<std::size_t>(y) * win.w + x) * 4;
            const int py = y - vy;
            const std::uint16_t want =
                (py >= 0 && py < rb.h && x < rb.w) ? rb.at(x, py) : std::uint16_t{0};
            const std::uint16_t got = omk::rgb565(p[0], p[1], p[2]);
            const bool miss = got != want;
            static const bool dbg = std::getenv("GLES_PROBE_DEBUG") != nullptr;
            if (miss && dbg && bad < 8)
                std::printf("    miss (%d,%d) got %04x want %04x\n", x, y, got, want);
            bad += miss;
        }
    return bad;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: gles_probe <gamedata> <model.3DO> <eye> <at> <hfov> [WxH]\n");
        return 2;
    }
    // ---- the context: CGL, legacy profile, no drawable
    CGLPixelFormatAttribute attrs[] = {
        kCGLPFAAccelerated,
        kCGLPFAOpenGLProfile, static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_Legacy),
        static_cast<CGLPixelFormatAttribute>(0)};
    CGLPixelFormatObj pf = nullptr;
    GLint npf = 0;
    CGLContextObj ctx = nullptr;
    if (CGLChoosePixelFormat(attrs, &pf, &npf) != kCGLNoError || !pf ||
        CGLCreateContext(pf, nullptr, &ctx) != kCGLNoError) {
        std::fprintf(stderr, "no CGL context\n");
        return 1;
    }
    CGLDestroyPixelFormat(pf);
    CGLSetCurrentContext(ctx);
    std::printf("gl: %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    // ---- the set, as run_vulkan loads it
    const std::string model = argv[2];
    const auto d = omk::DataFs::readPath(model);
    if (d.empty()) { std::fprintf(stderr, "cannot read %s\n", model.c_str()); return 1; }
    const auto t = omk::DataFs::readPath(model.substr(0, model.size() - 4) + ".3DT");
    const auto geo = omk::buildGeometry(d, omk::DrawFilter::Engine);
    const auto tex = t.empty() ? std::vector<omk::Texture>{} : omk::textures(d, t);
    std::vector<omk::Draw> draws;
    for (const auto& b : geo.batches) {
        omk::Draw dr;
        dr.bucketKey = static_cast<std::uint32_t>(b.material) & 0x3Fu;
        dr.geo = &geo; dr.start = b.start; dr.count = b.count;
        dr.blend = b.blend; dr.cutout = b.cutout;
        draws.push_back(dr);
    }
    omk::RCamera cam;
    triple(argv[3], cam.eye); triple(argv[4], cam.at);
    cam.hfovDeg = static_cast<float>(std::atof(argv[5]));
    int W = 640, H = 480;
    if (argc > 6) std::sscanf(argv[6], "%dx%d", &W, &H);

    auto run = [&](omk::Renderer& r, const omk::View& v, int w, int h) -> const omk::Surface* {
        if (!r.init(w, h)) return nullptr;
        r.setTextures(tex);
        r.begin(v);
        for (const auto& dr : draws) r.submit(dr);
        r.end();
        return &r.readback();
    };

    omk::Renderer* gl = omk::makeGlesRenderer();
    int failures = 0;
    std::printf("%s  %zu triangles  %zu textures\n", model.c_str(),
                geo.corners.size() / 3, tex.size());

    for (int dither = 0; dither <= 1; ++dither) {
        omk::View v;
        v.cam = cam; v.cam.w = W; v.cam.h = H;
        v.dither = dither != 0;
        omk::SoftwareRenderer sw;
        const omk::Surface swF = *run(sw, v, W, H);
        const omk::Surface* g = run(*gl, v, W, H);
        if (!g) { std::fprintf(stderr, "gles renderer failed to come up\n"); return 1; }
        const Coverage c = compare(swF, *g);
        std::printf("%-9s %dx%d  software lit %ld  gles lit %ld  coverage %.4f  "
                    "differing %ld (%.1f%%, not a criterion)\n",
                    dither ? "dithered" : "plain", W, H, c.sw, c.gl, c.agree(), c.differ,
                    100.0 * double(c.differ) / double(swF.px.size()));
        if (c.agree() < 0.99) ++failures;

        // present = readback, 1:1
        Window win;
        if (!win.make(W, H)) { std::fprintf(stderr, "no window FBO\n"); return 1; }
        omk::glesSetWindowTarget(gl, win.fbo);
        const omk::Surface rb = *g;   // a copy: presenting must not need it
        omk::glesPresentWorld(gl, 0, H, W, H, W, H);
        const long bad = presentMismatches(win, rb, 0);
        std::printf("          present world: %s\n",
                    bad ? (std::to_string(bad) + " DIFFERENT").c_str() : "EXACT");
        failures += bad != 0;
        // and the CPU-composed path: the readback handed back as a surface
        omk::glesPresentSurface(gl, rb, W, H);
        const long badS = presentMismatches(win, rb, 0);
        std::printf("          present surface: %s\n",
                    badS ? (std::to_string(badS) + " DIFFERENT").c_str() : "EXACT");
        failures += badS != 0;
        omk::glesSetWindowTarget(gl, 0);
    }

    // ---- THE LETTERBOX: a 640x352 picture at row 64 of a 640x480 frame, the
    // dialogue camera mode's shape. The target is the full frame and the
    // picture its top rows, as `play.cpp` sets the view up.
    if (W == 640 && H == 480) {
        const int vh = 352, vy = (480 - vh) / 2;
        omk::View v;
        v.cam = cam; v.vx = 0; v.vy = vy; v.vw = W; v.vh = vh;
        const omk::Surface* g = run(*gl, v, W, H);
        Window win;
        win.make(W, H);
        omk::glesSetWindowTarget(gl, win.fbo);
        // the readback is the WHOLE target; the picture is its top `vh` rows
        omk::Surface pic(W, vh, 0);
        for (int y = 0; y < vh; ++y)
            for (int x = 0; x < W; ++x) pic.set(x, y, g->at(x, y));
        omk::glesPresentWorld(gl, vy, vh, W, H, W, H);
        const long bad = presentMismatches(win, pic, vy);
        long lit = 0;
        for (auto p : pic.px) lit += p != 0;
        std::printf("letterbox 640x%d at row %d  picture lit %ld  present: %s\n", vh, vy, lit,
                    bad ? (std::to_string(bad) + " DIFFERENT").c_str() : "EXACT");
        failures += bad != 0 || lit == 0;
        omk::glesSetWindowTarget(gl, 0);
    }

    delete gl;
    CGLSetCurrentContext(nullptr);
    CGLDestroyContext(ctx);
    std::printf("failures %d\n", failures);
    return failures ? 1 : 0;
}

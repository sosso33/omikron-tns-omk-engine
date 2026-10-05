// SPDX-License-Identifier: GPL-3.0-or-later
// The fixed-function OpenGL 1.x backend - see gl1render.h.
//
// WHAT IT TAKES FROM THE ORIGINAL rather than from the software reference:
// the shape of the pipeline. Direct3D's fixed function did the per-pixel work
// (texture x Gouraud colour, colour key, linear fog, the blend), the engine
// only CHANGED STATE between buckets and handed over vertex arrays. So here:
//   * state is set per draw only when it differs from the last one - blend,
//     depth write, alpha test, fog, cull, texture - the D3D render-state calls
//     the original made between buckets, and no more;
//   * textures are 16-bit (`GL_RGB5_A1`), as the original's D3D surfaces
//     were, with no mip chain (`sub_4638C0`: MIP NONE) - point-sampled by
//     default here, BILINEAR when `setTextureFilter(1)` asks, which is what
//     the original's hardware arm set (MAG/MIN LINEAR, `docs/ASSETS.md` 4);
//   * the colour key on black (`SetRenderState(27, 1)`, mesh flag 0x800) is
//     a texel of alpha 0 and the alpha test - the fixed-function equivalent.
//     Filtered, a kept edge fragment carries some of the key's black (the
//     Vulkan and GLES shaders divide it back out; fixed function cannot), so
//     a cutout's edge pixels are darkened by the key's share of their sample,
//     at most half (the alpha test keeps above 0.5) - which is also
//     what a card that turned the key into alpha before filtering drew. The
//     card's own key path is not reachable from this tree;
//   * the vertices are TRANSFORMED ON THE CPU, near-clipped and culled there,
//     and handed over already projected - the original's `D3DTLVERTEX`
//     (sx, sy, sz, rhw), here as clip-space (x w, y w, z w, w) so the
//     texturing stays perspective-correct;
//   * the fog is computed PER VERTEX on the CPU - the original's own path for
//     a card without table fog - with the bucket key's two exclusions
//     (`renderer.h`, View::fog).
// Both of the last two were forced as well as faithful: on Tiger's emulated
// Radeon 9700 the GPU's own transform let near-plane-crossing triangles
// through as huge garbage, and its fog darkened the near scene; done on the
// CPU, the frame matches the reference (2026-10-02).
//
// WHAT IT TAKES FROM THE SOFTWARE REFERENCE (`o3de/raster.cpp`), because a
// GPU frame is checked against it: the colour law (texel * colour,
// additive `src + dst`, multiply `dst * (1 - src)`, depth written by opaque
// draws only), the near cut (`kNearCut`), the single-sided cull and its
// winding, the shimmer added to the vertex colour before anything else, and
// the 4x4 ordered dither of the RGB565 framebuffer (applied at readback).
//
// THE CONTEXT. An offscreen CGL context rendering into a framebuffer object,
// so the window stays the frontend's: the frame comes back through
// `readback()` as RGB565 and is composited and presented exactly as the
// software reference's is (PlayState's world harness). CGL is OpenGL's own
// layer on Mac OS X (10.2 onwards); the framebuffer object is
// EXT_framebuffer_object (Tiger 10.4.3 onwards). EVERY entry point makes the
// context current and gives the caller's back - SDL keeps a GL context of its
// own for its window, and drawing into it is exactly the bug that guard is.
//
// ON CLASSIC MAC OS (`OMK_GL1_AGL`, the Carbon build, 2026-10-04) the context
// is AGL's and the WINDOW's: Mac OS 9's OpenGL 1.2.1 has neither pbuffers nor
// framebuffer objects, so the world is drawn into the window context's BACK
// buffer, read back from there before the frontend presents (`gl1host.h`),
// and the frontend then draws the composited frame over it and swaps. Every
// call on that path is OpenGL 1.2 or AGL, which OS 9's `OpenGLLibrary` and
// Tiger's CFM bridge both export under that one fragment name.
#if defined(OMK_GL1_AGL)
#  include <agl.h>
#  include <gl.h>
#elif defined(__APPLE__)
#  define GL_SILENCE_DEPRECATION 1
#  include <OpenGL/OpenGL.h>
#  include <OpenGL/gl.h>
#  include <OpenGL/glext.h>
#else
#  error "gl1render.cpp: CGL is Mac OS X only - another platform needs its own context"
#endif

#include "gl1render.h"
#include "gl1host.h"

#include "o3de/shimmer.h"
#include "platform/profile.h"
#include "ui/surface.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>

namespace omk {
namespace {

#if defined(OMK_GL1_AGL)
// The fog's colour sum is EXT_secondary_color on OpenGL 1.2 (core only from
// 1.4), and Mac OS 9's library exports the EXT names alone.
#  define OMK_GL_COLOR_SUM GL_COLOR_SUM_EXT
#  define OMK_GL_SECONDARY_ARRAY GL_SECONDARY_COLOR_ARRAY_EXT
#  define omkSecondaryColorPointer glSecondaryColorPointerEXT
using GlContext = AGLContext;
GlContext currentContext() { return aglGetCurrentContext(); }
void makeCurrent(GlContext c) { aglSetCurrentContext(c); }
#else
#  define OMK_GL_COLOR_SUM GL_COLOR_SUM
#  define OMK_GL_SECONDARY_ARRAY GL_SECONDARY_COLOR_ARRAY
#  define omkSecondaryColorPointer glSecondaryColorPointer
using GlContext = CGLContextObj;
GlContext currentContext() { return CGLGetCurrentContext(); }
void makeCurrent(GlContext c) { CGLSetCurrentContext(c); }
#endif

// The caller's context out, ours in - and back on scope exit.
struct Current {
    GlContext prev;
    explicit Current(GlContext ours) : prev(currentContext()) {
        if (prev != ours) makeCurrent(ours);
    }
    ~Current() {
        if (currentContext() != prev) makeCurrent(prev);
    }
};

// THE PROFILER'S GPU MEMORY (todo/debug-tools.md 5): what this backend
// specifies on the card, by object and size, and what it deletes. Nothing in
// a release build (`OMK_PROFILE=0`).
inline void gpuNote(const char* tag, omk::prof::GpuDomain d, GLuint id, long long bytes) {
    OMK_GPU_ALLOC(tag, omk::prof::gpuKey(d, id), bytes);
    (void)tag; (void)d; (void)id; (void)bytes;
}
inline void gpuForget(omk::prof::GpuDomain d, const GLuint* ids, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) OMK_GPU_FREE(omk::prof::gpuKey(d, ids[i]));
    (void)d; (void)ids; (void)n;
}

unsigned nextPow2(unsigned v) {
    unsigned p = 1;
    while (p < v) p <<= 1;
    return p;
}

class Gl1Renderer final : public Renderer {
public:
    ~Gl1Renderer() override {
        if (!ctx_) return;
#if defined(OMK_GL1_AGL)
        // the context is the frontend's, and it may have closed first
        if (gl1AglContext() != ctx_) return;
#endif
        {
            Current c(ctx_);
            if (!tex_.empty()) gpuForget(omk::prof::kGlTexture, tex_.data(), tex_.size());
            if (!tex_.empty()) glDeleteTextures(static_cast<GLsizei>(tex_.size()), tex_.data());
#if defined(OMK_GL1_AGL)
            if (readTex_) gpuForget(omk::prof::kGlTexture, &readTex_, 1);
            if (readTex_) glDeleteTextures(1, &readTex_);
#else
            if (fbo_) glDeleteFramebuffersEXT(1, &fbo_);
            if (colour_) gpuForget(omk::prof::kGlRenderbuffer, &colour_, 1);
            if (colour_) glDeleteRenderbuffersEXT(1, &colour_);
            if (depth_) gpuForget(omk::prof::kGlRenderbuffer, &depth_, 1);
            if (depth_) glDeleteRenderbuffersEXT(1, &depth_);
#endif
        }
#if !defined(OMK_GL1_AGL)                    // the AGL context is the frontend's
        CGLDestroyContext(ctx_);
        if (pbuf_) CGLDestroyPBuffer(pbuf_);
#endif
    }

    bool init(int w, int h) override {
        if (!ctx_ && !makeContext()) return false;
        Current c(ctx_);
        w_ = w; h_ = h;
#if defined(OMK_GL1_AGL)
        // the window's own back buffer, which is the window's size - the
        // frame the viewer asks for (the display size)
        glDrawBuffer(GL_BACK);
        glReadBuffer(GL_BACK);
#else
        if (!fbo_) glGenFramebuffersEXT(1, &fbo_);
        if (!colour_) glGenRenderbuffersEXT(1, &colour_);
        if (!depth_) glGenRenderbuffersEXT(1, &depth_);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo_);
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, colour_);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, w, h);
        gpuNote("render targets", omk::prof::kGlRenderbuffer, colour_, 4LL * w * h);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                     GL_RENDERBUFFER_EXT, colour_);
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, depth_);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, w, h);
        gpuNote("render targets", omk::prof::kGlRenderbuffer, depth_, 4LL * w * h);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
                                     GL_RENDERBUFFER_EXT, depth_);
        if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) {
            // a 24-bit depth buffer is not on every card of the period
            glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT16, w, h);
            gpuNote("render targets", omk::prof::kGlRenderbuffer, depth_, 2LL * w * h);
            if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) {
                std::fprintf(stderr, "gl1: the framebuffer object is incomplete\n");
                return false;
            }
        }
#endif
        fb_ = Surface(w, h, 0);
        rgba_.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        readDone_ = false;
        return true;
    }

    void setTextures(std::span<const Texture> t) override {
        Current c(ctx_);
        if (!tex_.empty()) gpuForget(omk::prof::kGlTexture, tex_.data(), tex_.size());
        if (!tex_.empty()) glDeleteTextures(static_cast<GLsizei>(tex_.size()), tex_.data());
        tex_.assign(t.size(), 0);
        texW_.assign(t.size(), 1.0f);
        texH_.assign(t.size(), 1.0f);
        if (t.empty()) return;
        glGenTextures(static_cast<GLsizei>(tex_.size()), tex_.data());
        std::vector<unsigned char> px;
        for (std::size_t i = 0; i < t.size(); ++i) {
            const Texture& x = t[i];
            if (!x.hasPixels()) { tex_[i] = 0; continue; }
            // OpenGL 1.x wants power-of-two sides; the UVs are in texels, so
            // a padded texture keeps them and only the texture matrix's scale
            // changes. Every shipped .3DT is already a power of two.
            const unsigned pw = nextPow2(static_cast<unsigned>(x.width));
            const unsigned ph = nextPow2(static_cast<unsigned>(x.height));
            px.assign(static_cast<std::size_t>(pw) * ph * 4, 0);
            for (int y = 0; y < x.height; ++y)
                for (int xx = 0; xx < x.width; ++xx) {
                    const std::size_t d = (static_cast<std::size_t>(y) * pw + xx) * 4;
                    int ri, gi, bi;
                    x.texel(static_cast<std::size_t>(y) * x.width + xx, ri, gi, bi);
                    const unsigned char r = static_cast<unsigned char>(ri),
                                        g = static_cast<unsigned char>(gi),
                                        b = static_cast<unsigned char>(bi);
                    px[d] = r; px[d + 1] = g; px[d + 2] = b;
                    // the colour key on black: alpha 0, which only a cutout
                    // draw's alpha test reads
                    px[d + 3] = (r | g | b) ? 255 : 0;
                }
            glBindTexture(GL_TEXTURE_2D, tex_[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_ ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_ ? GL_LINEAR : GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            // bytes in memory order: no packed type, so no byte order to get wrong
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, static_cast<GLsizei>(pw),
                         static_cast<GLsizei>(ph), 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            gpuNote("textures", omk::prof::kGlTexture, tex_[i], 2LL * pw * ph);   // 16 bits a texel
            texW_[i] = static_cast<float>(pw);
            texH_[i] = static_cast<float>(ph);
        }
        boundTex_ = ~0u;
    }

    void begin(const View& v) override {
        Current c(ctx_);
        view_ = v;
        RCamera cam = v.cam;
        cam.w = v.letterboxed() ? v.vw : w_;
        cam.h = v.letterboxed() ? v.vh : h_;
        flipX_ = cam.flipX;
        bindTarget();
        glViewport(0, 0, w_, h_);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClearDepth(1.0);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // the picture in the TOP-LEFT cam.w x cam.h, as the reference draws it;
        // GL's window origin is bottom-left. On classic Mac OS (AGL) it is
        // drawn at its LETTERBOX place in the window's back buffer instead -
        // `view.vy` rows down, where the composed frame puts it - so a frame
        // nothing is drawn over is presented as it stands (`presentWorld`,
        // todo/cpu-vs-original.md tier B); `readback` takes it from there.
#if defined(OMK_GL1_AGL)
        vy_ = v.letterboxed() ? std::max(0, std::min(v.vy, h_ - cam.h)) : 0;
#endif
        glViewport(0, h_ - vy_ - cam.h, cam.w, cam.h);

        // THE CAMERA is applied HERE, on the CPU, as the original did: it
        // transformed every vertex itself and handed D3D screen positions
        // (`D3DTLVERTEX`: sx, sy, sz, rhw), on cards that mostly had no T&L.
        // `cameraBasis` is the reference's own basis (mirror, roll and flip
        // folded in). Both GL matrices stay identity: what reaches GL is
        // already in clip space.
        cameraBasis(cam, bs_, bu_, bf_, th_, tv_);
        for (int k = 0; k < 3; ++k) eye_[k] = cam.eye[k];
        camW_ = cam.w; camH_ = cam.h;
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        // the D3D device's fixed states, set once (`sub_4638C0`)
        glShadeModel(GL_SMOOTH);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);                 // strict: the first of two equal faces wins
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glAlphaFunc(GL_GREATER, 0.5f);
        // THE FOG IS COMPUTED PER VERTEX, on the CPU (submit) - the
        // original's own fallback for a card without table fog, and what
        // keeps it off the device's fog unit altogether: under emulation that
        // unit darkened the near scene. colour * (1 - a) plus the fog colour
        // * a as a SECONDARY colour added after texturing is the reference's
        // lerp(texel * colour, fog, a), vertex by vertex.
        glDisable(GL_FOG);
        fogRGB_[0] = v.fogColour[0] / 255.0f;
        fogRGB_[1] = v.fogColour[1] / 255.0f;
        fogRGB_[2] = v.fogColour[2] / 255.0f;
        fogSum_ = fogRGB_[0] > 0 || fogRGB_[1] > 0 || fogRGB_[2] > 0;
        if (fogSum_ && !hasColourSum_) {
            // a renderer without EXT_secondary_color (Mac OS 9's software
            // one): the fog's colour is lost, its fade kept. The game's fog is
            // BLACK (`docs/ASSETS.md`), so the shipped scenes never get here.
            static bool told = false;
            if (!told) { told = true; std::printf("gl1: no EXT_secondary_color - a coloured fog draws black\n"); }
            fogSum_ = false;
        }
        if (fogSum_) { glEnable(OMK_GL_COLOR_SUM); glEnableClientState(OMK_GL_SECONDARY_ARRAY); }
        else if (hasColourSum_) { glDisable(OMK_GL_COLOR_SUM); glDisableClientState(OMK_GL_SECONDARY_ARRAY); }
        glDisable(GL_CULL_FACE);              // culled on the CPU, the reference's rule
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        // nothing cached survives a begin: the state is set on the first draw
        stateValid_ = false;
        boundTex_ = ~0u;
        st_ = RasterStats{};
        readDone_ = false;
    }

    void submit(const Draw& d) override {
        if (!d.geo || d.count < 3) return;
        Current c(ctx_);
        const Geometry& g = *d.geo;
        if (d.start + d.count > g.corners.size()) return;

        setState(d);

        // Per triangle, what the original's own vertex loop did before
        // DrawPrimitive: into view space, the near cut (Sutherland-Hodgman
        // against z > kNearCut, as `raster.cpp`), the single-sided cull on the
        // screen area with the reference's sign, then CLIP-SPACE positions -
        // (x w, y w, depth w, w) with w the view depth, which is what keeps
        // the texturing perspective-correct the way `rhw` did for D3D.
        const std::size_t nCorner = d.count - d.count % 3;
        const bool haveCull = g.cornerCull.size() == g.corners.size();
        pos_.clear(); col_.clear(); uv_.clear(); fogc_.clear();
        for (std::size_t t = 0; t < nCorner; t += 3) {
            ++st_.triangles;
            Vtx in[3];
            for (int k = 0; k < 3; ++k) {
                const Corner& c = g.corners[d.start + t + static_cast<std::size_t>(k)];
                const float dx = c.x - eye_[0], dy = c.y - eye_[1], dz = c.z - eye_[2];
                in[k].v[0] = dx * bs_[0] + dy * bs_[1] + dz * bs_[2];
                in[k].v[1] = dx * bu_[0] + dy * bu_[1] + dz * bu_[2];
                in[k].v[2] = dx * bf_[0] + dy * bf_[1] + dz * bf_[2];
                // the shimmer first, as the engine adds it into the bytes
                const float sh = shimmerOffset(c.phase, view_.shimmerClock);
                in[k].c[0] = c.r + sh; in[k].c[1] = c.g + sh; in[k].c[2] = c.b + sh;
                in[k].u = c.u; in[k].t = c.v;
            }
            Vtx poly[4];
            int np = 0;
            for (int k = 0; k < 3 && np < 4; ++k) {
                const Vtx& a = in[k];
                const Vtx& b = in[(k + 1) % 3];
                const bool ina = a.v[2] > kNearCut, inb = b.v[2] > kNearCut;
                if (ina) poly[np++] = a;
                if (ina != inb && np < 4) poly[np++] = lerp(a, b, (kNearCut - a.v[2]) / (b.v[2] - a.v[2]));
            }
            if (np < 3) { ++st_.behind; continue; }
            // the single-sided cull on the near-clipped triangle - the
            // reference's own test, before anything else is cut away
            if (haveCull && g.cornerCull[d.start + t] != 0 && !noCull()) {
                float cx[3], cy[3];
                for (int k = 0; k < 3; ++k) {
                    cx[k] = camW_ * 0.5f * (1.0f + (poly[k].v[0] / poly[k].v[2]) / th_);
                    cy[k] = camH_ * 0.5f * (1.0f - (poly[k].v[1] / poly[k].v[2]) / tv_);
                }
                const float area = (cx[1] - cx[0]) * (cy[2] - cy[0]) - (cx[2] - cx[0]) * (cy[1] - cy[0]);
                if ((flipX_ ? -area : area) > 0.0f) { ++st_.culled; continue; }
            }
            // ...then THE SCREEN EDGES, on the CPU too. A wall beside the eye
            // projects thousands of pixels off-screen once near-clipped, and
            // Tiger's emulated Radeon rasterised such a triangle unclipped -
            // its texture coordinates collapsed and the wall drew flat. The
            // original clipped its transformed triangles itself as well. A
            // hair of margin keeps the edge pixels covered.
            Vtx buf[2][8];
            int nb = np;
            for (int k = 0; k < np; ++k) buf[0][k] = poly[k];
            const float gx = th_ * 1.01f, gy = tv_ * 1.01f;
            int cur = 0;
            for (int plane = 0; plane < 4 && nb >= 3; ++plane) {
                const auto dist = [&](const Vtx& q) {
                    switch (plane) {
                    case 0:  return q.v[2] * gx - q.v[0];   // right:  x <= z tanh
                    case 1:  return q.v[2] * gx + q.v[0];   // left:   x >= -z tanh
                    case 2:  return q.v[2] * gy - q.v[1];   // top:    y <= z tanv
                    default: return q.v[2] * gy + q.v[1];   // bottom: y >= -z tanv
                    }
                };
                int no = 0;
                for (int k = 0; k < nb && no < 8; ++k) {
                    const Vtx& a = buf[cur][k];
                    const Vtx& b = buf[cur][(k + 1) % nb];
                    const float da = dist(a), db = dist(b);
                    if (da >= 0.0f) buf[cur ^ 1][no++] = a;
                    if ((da >= 0.0f) != (db >= 0.0f) && no < 8) buf[cur ^ 1][no++] = lerp(a, b, da / (da - db));
                }
                nb = no;
                cur ^= 1;
            }
            if (nb < 3) { ++st_.offscreen; continue; }
            np = nb;
            const Vtx* out = buf[cur];
            float sx[8], sy[8];
            for (int k = 0; k < np; ++k) {
                sx[k] = camW_ * 0.5f * (1.0f + (out[k].v[0] / out[k].v[2]) / th_);
                sy[k] = camH_ * 0.5f * (1.0f - (out[k].v[1] / out[k].v[2]) / tv_);
            }
            ++st_.drawn;
            for (int k = 1; k + 1 < np; ++k) {            // a fan: up to 6 triangles after the clips
                const int idx[3] = {0, k, k + 1};
                for (int j : idx) {
                    const Vtx& p = out[j];
                    const float w = p.v[2];
                    // NDC from the reference's own screen mapping; depth
                    // 1 - near/z, increasing away from the eye (the original
                    // kept rhw = 1/w and tested GREATER - the same order)
                    const float nx = 2.0f * sx[j] / camW_ - 1.0f;
                    const float ny = 1.0f - 2.0f * sy[j] / camH_;
                    const float nz = 2.0f * (1.0f - kNearCut / w) - 1.0f;
                    pos_.insert(pos_.end(), {nx * w, ny * w, nz * w, w});
                    // the reference's fog: f = (end - z) / (end - start) past
                    // the start, the colour lerped toward the fog's by 1 - f
                    float a = 0.0f;
                    if (fogKind_ != 0 && w > fogStart_) {
                        const float f = (fogEnd_ - w) / (fogEnd_ - fogStart_);
                        a = 1.0f - (f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f));
                    }
                    col_.insert(col_.end(), {toByte(p.c[0] * (1.0f - a)), toByte(p.c[1] * (1.0f - a)),
                                             toByte(p.c[2] * (1.0f - a)), 255});
                    if (fogSum_)
                        fogc_.insert(fogc_.end(), {fogRGB_[0] * a, fogRGB_[1] * a, fogRGB_[2] * a});
                    uv_.insert(uv_.end(), {p.u, p.t});
                }
            }
        }
        if (pos_.empty()) return;
        glVertexPointer(4, GL_FLOAT, 0, pos_.data());
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, col_.data());
        glTexCoordPointer(2, GL_FLOAT, 0, uv_.data());
        if (fogSum_) omkSecondaryColorPointer(3, GL_FLOAT, 0, fogc_.data());
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(pos_.size() / 4));
    }

    void end() override {
        Current c(ctx_);
        glFlush();
    }

    const Surface& readback() override {
        if (readDone_) return fb_;           // idempotent (renderer.h)
        Current c(ctx_);
        bindTarget();
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        // the first read over a known pattern: a read that writes nothing
        // then shows as 85 85 85, not as a black frame (the report below)
        if (!toldRead_) std::memset(rgba_.data(), 0x55, rgba_.size());
        // bytes, not a packed type: endian-free on any Mac
#if defined(OMK_GL1_AGL)
        readWindow();
#else
        glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, rgba_.data());
#endif
        // bottom-up rows into the top-down RGB565 frame, with the dither the
        // reference applies (`DITHERENABLE` 1 on both device arms)
        // (the world `vy_` rows down - 0 but on AGL - read back to the top)
        for (int y = 0; y < h_; ++y) {
            if (y + vy_ >= h_) {                 // below the picture: the clear
                for (int x = 0; x < w_; ++x) fb_.set(x, y, 0);
                continue;
            }
            const unsigned char* row = rgba_.data() + static_cast<std::size_t>(h_ - 1 - y - vy_) * w_ * 4;
            for (int x = 0; x < w_; ++x) {
                const unsigned char* p = row + static_cast<std::size_t>(x) * 4;
                fb_.set(x, y, view_.dither ? quantise888Dither(p[0], p[1], p[2], x, y)
                                           : quantise888(p[0], p[1], p[2]));
            }
        }
        readDone_ = true;
        if (!toldRead_) {
            // what the first read found, once: a black window is otherwise
            // indistinguishable from a renderer that drew nothing
            toldRead_ = true;
            const unsigned char* m = rgba_.data() +
                (static_cast<std::size_t>(h_ / 2) * w_ + static_cast<std::size_t>(w_ / 2)) * 4;
            std::printf("gl1: first readback %dx%d, %ld triangles, GL error 0x%x, centre "
                        "pixel %u %u %u\n", w_, h_, st_.triangles,
                        static_cast<unsigned>(glGetError()), m[0], m[1], m[2]);
        }
        return fb_;
    }

#if defined(OMK_GL1_AGL)
    // ---- THE WORLD PRESENTED STRAIGHT (todo/cpu-vs-original.md tier B) ----
    //
    // The world is in the window's back buffer at its letterbox place, the
    // bands cleared black: a frame nothing is drawn over needs no readback,
    // no composite and no `glDrawPixels` - it is swapped as it stands, at the
    // drawable's own depth with the DRIVER's dither (GL_DITHER, on by
    // default), as the original's 16-bit D3D device dithered (`DITHERENABLE`,
    // ASSETS 4) - the reader's choice of 2026-10-05. ~5 -> 17 fps on emulated
    // Tiger (`handoff-classic-mac.md`). Only where the window is the frame's
    // own size; anything else stays on the composite.
    bool presentWorld(int ww, int wh) {
        if (ww != w_ || wh != h_) return false;
        Current c(ctx_);
        aglSwapBuffers(ctx_);
        return true;
    }
    // ...and a frame with the interface over it: the composed frame `fb`,
    // whose world rows are the KEY (0xF81F), and `mask` - how much of the
    // world shows through a pixel - drawn OVER the world as the GLES
    // overlay does: `C + world * M`, then the colour fade (`fade` rgb + its
    // weight). Fixed function: the CPU folds key, mask and fade into one RGBA
    // texture, a quad blends it with (GL_ONE, GL_SRC_ALPHA). Only the band of
    // rows the overlay touches is uploaded and drawn.
    bool presentOverlay(const Surface& fb, const std::uint8_t* mask, const float fade[4],
                        int ww, int wh) {
        if (ww != w_ || wh != h_ || fb.w != w_ || fb.h != h_) return false;
        int lo = fb.h, hi = -1;
        const bool fading = fade[3] > 0.0f;
        for (int y = 0; y < fb.h; ++y) {
            const std::uint16_t* row = fb.px.data() + static_cast<std::size_t>(y) * fb.w;
            bool any = fading;
            for (int x = 0; x < fb.w && !any; ++x) any = row[x] != 0xF81F;
            if (any) { if (lo > y) lo = y; hi = y; }
        }
        Current c(ctx_);
        if (hi >= lo) {
            const int bh = hi - lo + 1;
            const GLsizei tw = static_cast<GLsizei>(nextPow2(static_cast<unsigned>(fb.w)));
            const GLsizei th = static_cast<GLsizei>(nextPow2(static_cast<unsigned>(fb.h)));
            if (!ovTex_ || ovTexW_ != tw || ovTexH_ != th) {
                if (!ovTex_) glGenTextures(1, &ovTex_);
                glBindTexture(GL_TEXTURE_2D, ovTex_);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                gpuNote("overlay", omk::prof::kGlTexture, ovTex_, 4LL * tw * th);
                ovTexW_ = tw; ovTexH_ = th;
                ovRowKey_.assign(static_cast<std::size_t>(th), 0);   // nothing sent yet
            }
            // ONLY THE ROWS THAT CHANGED (2026-10-05, run on Tiger): a row
            // keeps its place in the texture, and is converted and sent only
            // when its source - the 565 row, its mask row, the fade - differs
            // from what was last sent there. A subtitle is the same rows for
            // hundreds of frames; re-sending the band every frame cost the
            // emulated Tiger ~45 ms an overlay frame. The key a row is
            // compared by is a hash, as the GLES overlay's (four chains).
            glBindTexture(GL_TEXTURE_2D, ovTex_);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            ovRgba_.resize(static_cast<std::size_t>(fb.w) * 4);
            const float f = fade[3], g = 1.0f - f;
            const float fr = fade[0] * 255.0f * f, fg = fade[1] * 255.0f * f, fbl = fade[2] * 255.0f * f;
            std::uint32_t fadeKey = 2166136261u;
            for (int q = 0; q < 4; ++q) {
                std::uint32_t bits;
                std::memcpy(&bits, &fade[q], 4);
                fadeKey = (fadeKey ^ bits) * 16777619u;
            }
            for (int y = lo; y <= hi; ++y) {
                const std::uint16_t* src = fb.px.data() + static_cast<std::size_t>(y) * fb.w;
                const std::uint8_t* msk = mask ? mask + static_cast<std::size_t>(y) * fb.w : nullptr;
                std::uint32_t h0 = fadeKey, h1 = 0x9E3779B9u, h2 = 0x7F4A7C15u, h3 = 0x94D049BBu;
                int x = 0;
                for (; x + 4 <= fb.w; x += 4) {
                    h0 = (h0 ^ src[x])     * 16777619u;
                    h1 = (h1 ^ src[x + 1]) * 16777619u;
                    h2 = (h2 ^ src[x + 2]) * 16777619u;
                    h3 = (h3 ^ src[x + 3]) * 16777619u;
                }
                for (; x < fb.w; ++x) h0 = (h0 ^ src[x]) * 16777619u;
                if (msk)
                    for (int m = 0; m < fb.w; ++m) h1 = (h1 ^ msk[m]) * 16777619u;
                const std::uint32_t key = (h0 ^ (h1 * 3u) ^ (h2 * 5u) ^ (h3 * 7u)) | 1u;   // 0 = never sent
                if (ovRowKey_[static_cast<std::size_t>(y)] == key) continue;
                ovRowKey_[static_cast<std::size_t>(y)] = key;
                for (int xx = 0; xx < fb.w; ++xx) {
                    unsigned char* o = &ovRgba_[static_cast<std::size_t>(xx) * 4];
                    const std::uint16_t p = src[xx];
                    float r = 0, gg = 0, b = 0, a = 255;           // the KEY: the world as it is
                    if (p != 0xF81F) {
                        const unsigned r5 = (p >> 11) & 31, g6 = (p >> 5) & 63, b5 = p & 31;
                        r = static_cast<float>(r5 << 3 | r5 >> 2);
                        gg = static_cast<float>(g6 << 2 | g6 >> 4);
                        b = static_cast<float>(b5 << 3 | b5 >> 2);
                        a = msk ? msk[xx] : 0;
                    }
                    o[0] = static_cast<unsigned char>(r * g + fr + 0.5f);
                    o[1] = static_cast<unsigned char>(gg * g + fg + 0.5f);
                    o[2] = static_cast<unsigned char>(b * g + fbl + 0.5f);
                    o[3] = static_cast<unsigned char>(a * g + 0.5f);
                }
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, fb.w, 1, GL_RGBA, GL_UNSIGNED_BYTE, ovRgba_.data());
            }
            // the 2D state for one quad; `begin` sets all of its own again
            glViewport(0, 0, ww, wh);
            glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, ww, 0, wh, -1, 1);
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();
            glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
            glDisable(GL_DEPTH_TEST); glDisable(GL_FOG); glDisable(GL_CULL_FACE);
            glDisable(GL_ALPHA_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_LIGHTING);
            glEnable(GL_TEXTURE_2D);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_SRC_ALPHA);
            glColor4f(1, 1, 1, 1);
            // the band: fb rows lo..hi, top-down, onto window rows wh-lo..wh-hi-1
            const float t0 = static_cast<float>(lo) / th, t1 = static_cast<float>(hi + 1) / th,
                        s1 = static_cast<float>(fb.w) / tw;
            (void)bh;
            const float yTop = static_cast<float>(wh - lo), yBot = static_cast<float>(wh - hi - 1);
            glBegin(GL_QUADS);
            glTexCoord2f(0, t0);  glVertex2f(0, yTop);
            glTexCoord2f(s1, t0); glVertex2f(static_cast<float>(ww), yTop);
            glTexCoord2f(s1, t1); glVertex2f(static_cast<float>(ww), yBot);
            glTexCoord2f(0, t1);  glVertex2f(0, yBot);
            glEnd();
            glDisable(GL_BLEND);
            stateValid_ = false;             // the draw state the cache assumed is gone
            boundTex_ = ~0u;
        }
        aglSwapBuffers(ctx_);
        return true;
    }
#endif

    RasterStats stats() const override { return st_; }   // only `triangles` is known
    const char* name() const override { return "OpenGL 1.x fixed-function"; }
    // TEXTURE FILTERING (renderer.h): 0 nearest, 1 bilinear. No mip chain -
    // the original set MIP NONE on both arms - so trilinear is drawn bilinear
    // and said. Recorded here; applied to every texture `setTextures` made
    // and every one it makes after, so the order of the two calls is free.
    bool setTextureFilter(int mode) override {
        filter_ = mode >= 1 ? 1 : 0;
        if (mode >= 2)
            std::printf("gl1: trilinear asked - no mip chain in this backend, drawn bilinear\n");
        if (!tex_.empty()) {
            Current c(ctx_);
            for (GLuint id : tex_) {
                if (!id) continue;
                glBindTexture(GL_TEXTURE_2D, id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_ ? GL_LINEAR : GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_ ? GL_LINEAR : GL_NEAREST);
            }
            boundTex_ = ~0u;
        }
        return mode <= 1;
    }

private:
#if defined(OMK_GL1_AGL)
    // THE WINDOW'S BACK BUFFER, READ THROUGH A TEXTURE. `glReadPixels` from a
    // window surface returns zeros on Tiger's emulated Radeon 9700 while the
    // same frame draws correctly when swapped (2026-10-04, probed: a cleared
    // red read back black, and with the composite skipped the street showed
    // in the window); the copy into a texture and `glGetTexImage` go through
    // texture memory instead. Both calls are OpenGL 1.1, so Mac OS 9 has them.
    void readWindow() {
        const GLsizei tw = static_cast<GLsizei>(nextPow2(static_cast<unsigned>(w_)));
        const GLsizei th = static_cast<GLsizei>(nextPow2(static_cast<unsigned>(h_)));
        if (!readTex_ || readTexW_ != tw || readTexH_ != th) {
            if (!readTex_) glGenTextures(1, &readTex_);
            glBindTexture(GL_TEXTURE_2D, readTex_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            gpuNote("readback", omk::prof::kGlTexture, readTex_, 4LL * tw * th);
            readTexW_ = tw; readTexH_ = th;
            readBig_.assign(static_cast<std::size_t>(tw) * static_cast<std::size_t>(th) * 4, 0);
        }
        glBindTexture(GL_TEXTURE_2D, readTex_);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w_, h_);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, readBig_.data());
        boundTex_ = ~0u;                     // the next draw binds its own
        // the texture's rows are the back buffer's, bottom-up, at its stride
        for (int y = 0; y < h_; ++y)
            std::memcpy(rgba_.data() + static_cast<std::size_t>(y) * w_ * 4,
                        readBig_.data() + static_cast<std::size_t>(y) * tw * 4,
                        static_cast<std::size_t>(w_) * 4);
    }
    GLuint readTex_ = 0;
    GLsizei readTexW_ = 0, readTexH_ = 0;
    std::vector<unsigned char> readBig_;
    GLuint ovTex_ = 0;                   // the overlay (`presentOverlay`)
    GLsizei ovTexW_ = 0, ovTexH_ = 0;
    std::vector<unsigned char> ovRgba_;  // one row, converted
    std::vector<std::uint32_t> ovRowKey_;   // what each texture row was last sent from
#endif

    // Where the world is drawn and read from: the framebuffer object, or on
    // classic Mac OS the window's back buffer (the frontend presents after).
    void bindTarget() {
#if defined(OMK_GL1_AGL)
        glDrawBuffer(GL_BACK);
        glReadBuffer(GL_BACK);
#else
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo_);
#endif
    }

#if defined(OMK_GL1_AGL)
    // The window's context, made by the frontend (`gl1host.h`); nothing to
    // attach here, and no extension needed - the back buffer is the target.
    bool makeContext() {
        ctx_ = static_cast<GlContext>(gl1AglContext());
        if (!ctx_) {
            std::fprintf(stderr, "gl1: no AGL context on the window\n");
            return false;
        }
        Current c(ctx_);
        const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        hasColourSum_ = ext && std::strstr(ext, "GL_EXT_secondary_color");
        GLint vp[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, vp);      // the default: the drawable's size
        std::printf("gl1: the window's drawable is %dx%d\n", static_cast<int>(vp[2]),
                    static_cast<int>(vp[3]));
        std::printf("gl1: %s, OpenGL %s, on the window's back buffer (AGL)\n",
                    reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                    reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        return true;
    }
#else
    // THE CONTEXT NEEDS A DRAWABLE ON TIGER. With none attached, Mac OS X
    // 10.4 draws nothing and `glReadPixels` writes nothing - not even a clear,
    // not even into a framebuffer object, on the Radeon and on Apple's own
    // software renderer alike (probed 2026-10-02: the read-back bytes stayed
    // as the probe left them). A current Mac renders into the FBO without
    // one. So a small PBUFFER is attached - the period's offscreen drawable,
    // 10.3 onwards - and the FBO is drawn into as before; where pbuffers are
    // gone or refused (deprecated since 10.7) the context goes on without.
    bool makeContext() {
        for (int withPBuffer = 1; withPBuffer >= 0 && !ctx_; --withPBuffer) {
            CGLPixelFormatAttribute attrs[8];
            int k = 0;
            attrs[k++] = kCGLPFAAccelerated;
            if (withPBuffer) attrs[k++] = kCGLPFAPBuffer;
            attrs[k++] = kCGLPFAColorSize; attrs[k++] = static_cast<CGLPixelFormatAttribute>(24);
            attrs[k++] = kCGLPFADepthSize; attrs[k++] = static_cast<CGLPixelFormatAttribute>(16);
            attrs[k] = static_cast<CGLPixelFormatAttribute>(0);
            CGLPixelFormatObj pf = nullptr;
            GLint n = 0;
            if (CGLChoosePixelFormat(attrs, &pf, &n) != kCGLNoError || !pf) continue;
            const CGLError e = CGLCreateContext(pf, nullptr, &ctx_);
            CGLDestroyPixelFormat(pf);
            if (e != kCGLNoError || !ctx_) { ctx_ = nullptr; continue; }
            if (withPBuffer) {
                GLint screen = 0;
                if (CGLCreatePBuffer(16, 16, GL_TEXTURE_2D, GL_RGBA, 0, &pbuf_) != kCGLNoError ||
                    CGLGetVirtualScreen(ctx_, &screen) != kCGLNoError ||
                    CGLSetPBuffer(ctx_, pbuf_, 0, 0, screen) != kCGLNoError) {
                    if (pbuf_) { CGLDestroyPBuffer(pbuf_); pbuf_ = nullptr; }
                    CGLDestroyContext(ctx_); ctx_ = nullptr;
                    continue;
                }
            }
        }
        if (!ctx_) {
            std::fprintf(stderr, "gl1: no accelerated OpenGL context\n");
            return false;
        }
        Current c(ctx_);
        const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (!ext || !std::strstr(ext, "GL_EXT_framebuffer_object")) {
            std::fprintf(stderr, "gl1: no EXT_framebuffer_object (%s)\n",
                         reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
            return false;
        }
        std::printf("gl1: %s, OpenGL %s%s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                    reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                    pbuf_ ? ", on a pbuffer" : "");
        return true;
    }
#endif

    struct Vtx { float v[3]; float c[3]; float u, t; };
    static Vtx lerp(const Vtx& a, const Vtx& b, float t) {
        Vtx o;
        for (int k = 0; k < 3; ++k) { o.v[k] = a.v[k] + (b.v[k] - a.v[k]) * t; o.c[k] = a.c[k] + (b.c[k] - a.c[k]) * t; }
        o.u = a.u + (b.u - a.u) * t; o.t = a.t + (b.t - a.t) * t;
        return o;
    }
    // `OMK_NO_CULL=1` draws both sides, as it does for the reference
    static bool noCull() { static const bool n = std::getenv("OMK_NO_CULL") != nullptr; return n; }

    static unsigned char toByte(float v) {
        // the corner colour is 0..1; the reference clamps after the multiply,
        // fixed function clamps the vertex colour first - equal for 0..1
        const float x = v * 255.0f + 0.5f;
        return static_cast<unsigned char>(x <= 0.0f ? 0.0f : (x >= 255.0f ? 255.0f : x));
    }

    // The D3D render states the original changed between buckets, changed
    // here only when they differ from the last draw's.
    void setState(const Draw& d) {
        const unsigned slot = d.bucketKey & 0x3F;   // ASSETS 4b: the key's low six bits
        const bool hasTex = slot < tex_.size() && tex_[slot] != 0;
        if (!stateValid_ || d.blend != blend_) {
            switch (d.blend) {
            case Blend::Opaque: glDisable(GL_BLEND); glDepthMask(GL_TRUE); break;
            case Blend::Add:                                  // src + dst, saturating
                glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); glDepthMask(GL_FALSE); break;
            case Blend::Mul:                                  // dst * (1 - src)
                glEnable(GL_BLEND); glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
                glDepthMask(GL_FALSE); break;
            }
            blend_ = d.blend;
        }
        if (!stateValid_ || d.cutout != cutout_) {
            if (d.cutout) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
            cutout_ = d.cutout;
        }
        // the fog's two exclusions (View::fog): 0x2080 none, 0x800 doubled
        const int fogKind = !view_.fog || view_.fogEnd <= view_.fogStart ? 0
                          : (d.bucketKey & 0x2080) ? 0 : (d.bucketKey & 0x800) ? 2 : 1;
        fogKind_ = fogKind;
        const float fk = fogKind == 2 ? 2.0f : 1.0f;
        fogStart_ = view_.fogStart * fk;
        fogEnd_ = view_.fogEnd * fk;
        const unsigned want = hasTex ? slot : ~1u;
        if (want != boundTex_) {
            if (hasTex) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, tex_[slot]);
                // texel coordinates through the texture matrix, as a D3D port
                // divided them on the CPU
                glMatrixMode(GL_TEXTURE);
                glLoadIdentity();
                glScalef(1.0f / texW_[slot], 1.0f / texH_[slot], 1.0f);
                glMatrixMode(GL_MODELVIEW);
            } else {
                glDisable(GL_TEXTURE_2D);              // no texture: the colour alone
            }
            boundTex_ = want;
        }
        stateValid_ = true;
    }

    GlContext ctx_ = nullptr;
#if !defined(OMK_GL1_AGL)
    CGLPBufferObj pbuf_ = nullptr;
#endif
    GLuint fbo_ = 0, colour_ = 0, depth_ = 0;
    int w_ = 0, h_ = 0;
    int vy_ = 0;             // the picture's first row in the target (AGL: its letterbox place)
    std::vector<GLuint> tex_;
    std::vector<float> texW_, texH_;
    View view_;
    bool flipX_ = false;
    Surface fb_{1, 1, 0};
    std::vector<unsigned char> rgba_;
    bool readDone_ = false, toldRead_ = false;
    RasterStats st_;
    // the state cache
    bool stateValid_ = false, cutout_ = false;
    int  filter_ = 0;        // 0 nearest (the software devices), 1 bilinear (a 3D card)
    Blend blend_ = Blend::Opaque;
    int fogKind_ = -1;
    float fogStart_ = 0, fogEnd_ = 0, fogRGB_[3]{};
    bool fogSum_ = false;
    bool hasColourSum_ = true;   // EXT_secondary_color, asked on AGL (makeContext)
    unsigned boundTex_ = ~0u;
    // the scratch arrays, kept between draws (no allocation per frame)
    std::vector<float> pos_, uv_, fogc_;
    // the camera, applied on the CPU (begin)
    float bs_[3]{}, bu_[3]{}, bf_[3]{}, th_ = 1, tv_ = 1, eye_[3]{};
    int camW_ = 1, camH_ = 1;
    std::vector<unsigned char> col_;
};

}  // namespace

Renderer* makeGl1Renderer() { return new Gl1Renderer(); }

#if defined(OMK_GL1_AGL)
// `r` is the renderer `makeGl1Renderer` made (the glue holds no other)
bool gl1PresentWorld(Renderer* r, int ww, int wh) {
    return r && static_cast<Gl1Renderer*>(r)->presentWorld(ww, wh);
}
bool gl1PresentOverlay(Renderer* r, const Surface& fb, const std::uint8_t* mask,
                       const float fade[4], int ww, int wh) {
    return r && static_cast<Gl1Renderer*>(r)->presentOverlay(fb, mask, fade, ww, wh);
}
#endif

}  // namespace omk

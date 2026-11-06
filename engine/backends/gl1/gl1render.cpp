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
//     texturing stays perspective-correct; and in the original's SHAPE
//     (`submitShape`): a geometry's distinct vertices transformed once and
//     in order, its faces indexing them;
//   * the fog is computed PER VERTEX on the CPU - the original's own path for
//     a card without table fog - with the bucket key's two exclusions
//     (`renderer.h`, View::fog).
// Both of the last two are forced as well as faithful: on Tiger's emulated
// Radeon 9700 the GPU's own transform lets near-plane-crossing triangles
// through as huge garbage, and its fog darkens the near scene; done on the
// CPU, the frame matches the reference.
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
// ON CLASSIC MAC OS (`OMK_GL1_AGL`, the Carbon build) the context
// is AGL's and the WINDOW's: Mac OS 9's OpenGL 1.2.1 has neither pbuffers nor
// framebuffer objects, so the world is drawn into the window context's BACK
// buffer, read back from there before the frontend presents (`gl1host.h`),
// and the frontend then draws the composited frame over it and swaps. Every
// call on that path is OpenGL 1.2 or AGL, which OS 9's `OpenGLLibrary` and
// Tiger's CFM bridge both export under that one fragment name.
//
// THE SDL BUILD ON MAC OS X DRAWS ON ITS WINDOW TOO (`OMK_GL1_SDL_WINDOW`,
// `ppc-darwin.mk`): the context is the SDL window's (`backends/gl1/
// gl1sdlhost.cpp`) and every AGL path above - the straight present, the
// overlay, the read through a texture - is its path, the swap through the
// host. Offscreen, a 640x480 street frame cost an iBook G4 (1.42 GHz, Radeon
// 9550) 135 of its 183 ms in the readback alone (`todo/handoff-classic-mac.md`
// 3g). Where the host has no GL window (`SDL_VIDEODRIVER=dummy`, which is how
// `verify.py` runs it) the framebuffer object below is kept - decided in
// `makeContext`, `onWindow_`.
#if defined(OMK_GL1_AGL)
#  include <CodeFragments.h>
#  include <agl.h>
#  include <gl.h>
// Apple's upload extensions by value (gl.h has none); `gl1OverlayPath` says
// whether the driver offered them
#  ifndef GL_UNPACK_CLIENT_STORAGE_APPLE
#    define GL_UNPACK_CLIENT_STORAGE_APPLE 0x85B2
#  endif
#  ifndef GL_TEXTURE_RECTANGLE_EXT
#    define GL_TEXTURE_RECTANGLE_EXT 0x84F5
#  endif
#  ifndef GL_TEXTURE_STORAGE_HINT_APPLE
#    define GL_TEXTURE_STORAGE_HINT_APPLE 0x85BC
#  endif
#  ifndef GL_STORAGE_SHARED_APPLE
#    define GL_STORAGE_SHARED_APPLE 0x85BF
#  endif
#elif defined(__APPLE__)
#  define GL_SILENCE_DEPRECATION 1
#  include <OpenGL/OpenGL.h>
#  include <OpenGL/gl.h>
#  include <OpenGL/glext.h>
#  include <dlfcn.h>
#else
#  error "gl1render.cpp: CGL is Mac OS X only - another platform needs its own context"
#endif

#include "gl1render.h"
#include "gl1host.h"

#include "o3de/depthtie.h"
#include "o3de/drawcore.h"
#include "o3de/greybank.h"
#include "o3de/shimmer.h"
#include "o3de/texscale.h"
#include "o3de/vertexlight.h"
#include "platform/profile.h"
#include "ui/overlay.h"
#include "ui/surface.h"
#include "platform/env.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>

namespace omk {
namespace {

// `blendRule`'s factors (`o3de/drawcore.h`) as GL's
GLenum glFactor(BlendFactor f) {
    return f == BlendFactor::Zero ? GL_ZERO : f == BlendFactor::One ? GL_ONE : GL_ONE_MINUS_SRC_COLOR;
}

#if defined(OMK_GL1_AGL)
// The fog's colour sum is EXT_secondary_color on OpenGL 1.2 (core only from
// 1.4), and Mac OS 9's library exports the EXT names alone.
#  define OMK_GL_COLOR_SUM GL_COLOR_SUM_EXT
#  define OMK_GL_SECONDARY_ARRAY GL_SECONDARY_COLOR_ARRAY_EXT
// ...and only from OpenGL 1.2.1 on: an older OS 9 `OpenGLLibrary` (the
// reader's iMac G3, 2026-10-30) has no `glSecondaryColorPointerEXT`, and an
// import of it makes CFM refuse to launch the application at all
// ("OpenGLLibrary--glSecondaryColorPointerEXT est introuvable"). So it is
// looked up by name; not found, `hasColourSum_` is false and the fog loses
// its colour, as on a renderer without the extension.
// An OpenGLLibrary export by name (a Pascal string), or nullptr.
Ptr findOpenGLSymbol(ConstStr255Param name) {
    CFragConnectionID conn = 0;
    Ptr main = nullptr;
    Str255 err;
    if (GetSharedLibrary("\pOpenGLLibrary", kPowerPCCFragArch, kReferenceCFrag, &conn, &main, err) != noErr)
        return nullptr;
    Ptr sym = nullptr;
    CFragSymbolClass cls = 0;
    if (FindSymbol(conn, name, &sym, &cls) != noErr) return nullptr;
    return sym;                                  // a TVector: a function pointer here
}
using SecondaryColorPointerFn = void (*)(GLint, GLenum, GLsizei, const GLvoid*);
SecondaryColorPointerFn omkSecondaryColorPointer = nullptr;
SecondaryColorPointerFn findSecondaryColorPointer() {
    return reinterpret_cast<SecondaryColorPointerFn>(findOpenGLSymbol("\pglSecondaryColorPointerEXT"));
}
// ARB_multitexture's two selectors, for the fog in ONE pass (`fogMT_`) - by
// name too, by the rule above
using ActiveTextureFn = void (*)(GLenum);
ActiveTextureFn findActiveTexture(bool client) {
    return reinterpret_cast<ActiveTextureFn>(
        findOpenGLSymbol(client ? "\pglClientActiveTextureARB" : "\pglActiveTextureARB"));
}
// EXT_compiled_vertex_array's pair (`drawLocked`) - by name too
using LockArraysFn = void (*)(GLint, GLsizei);
using UnlockArraysFn = void (*)();
LockArraysFn findLockArrays() { return reinterpret_cast<LockArraysFn>(findOpenGLSymbol("\pglLockArraysEXT")); }
UnlockArraysFn findUnlockArrays() {
    return reinterpret_cast<UnlockArraysFn>(findOpenGLSymbol("\pglUnlockArraysEXT"));
}
using GlContext = AGLContext;
GlContext currentContext() { return aglGetCurrentContext(); }
void makeCurrent(GlContext c) { aglSetCurrentContext(c); }
#else
#  define OMK_GL_COLOR_SUM GL_COLOR_SUM
#  define OMK_GL_SECONDARY_ARRAY GL_SECONDARY_COLOR_ARRAY
#  define omkSecondaryColorPointer glSecondaryColorPointer
using ActiveTextureFn = void (*)(GLenum);
ActiveTextureFn findActiveTexture(bool client) { return client ? glClientActiveTexture : glActiveTexture; }
using LockArraysFn = void (*)(GLint, GLsizei);
using UnlockArraysFn = void (*)();
// by name: a current SDK declares the pair no more (`GL_EXT_compiled_vertex_array
// 0` in its glext.h), while the framework still exports it
LockArraysFn findLockArrays() { return reinterpret_cast<LockArraysFn>(dlsym(RTLD_DEFAULT, "glLockArraysEXT")); }
UnlockArraysFn findUnlockArrays() {
    return reinterpret_cast<UnlockArraysFn>(dlsym(RTLD_DEFAULT, "glUnlockArraysEXT"));
}
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
    // the caches keyed by a geometry's ADDRESS (the vertex index, the runs'
    // spheres) are dropped when it is destroyed (`geom3do.h` GpuResidency)
    Gl1Renderer() {
        addGeometryListener(&Gl1Renderer::geometryGone, this);
        // the profiler's A/B (`--profile-ab lock|cardfog`): each flipped
        // between frames, against the run's own setting - `begin` reads both
        // afresh every frame
        prof::abSwitch("lock", [](void* r, bool f) { auto* g = static_cast<Gl1Renderer*>(r); g->abFlip(f, g->abLock_, g->noLock_); }, this);
        prof::abSwitch("cardfog", [](void* r, bool f) { auto* g = static_cast<Gl1Renderer*>(r); g->abFlip(f, g->abFog_, g->cardFog_); }, this);
        // ...and `shape`: the submit in the original's shape against the
        // corner-by-corner one (`--gl1-per-corner`)
        prof::abSwitch("shape", [](void* r, bool f) { auto* g = static_cast<Gl1Renderer*>(r); g->abFlip(f, g->abShape_, g->perCorner_); }, this);
    }
    void abFlip(bool flipped, bool& now, bool& flag) {
        if (flipped != now) { now = flipped; flag = !flag; }
    }
    static void geometryGone(void* self, const Geometry* g) {
        auto* r = static_cast<Gl1Renderer*>(self);
        // a set's state said once as it goes (the slots' release, `releaseResidency`)
        if (const auto it = r->shapes_.find(g); it != r->shapes_.end() && it->second.n >= 20000)
            std::printf("gl1: a geometry of %zu corners gone - its shape released\n", it->second.n);
        r->vindex_.erase(g);
        r->shapes_.erase(g);
        r->clusters_.erase(g);
        r->tieBaked_.erase(g);
    }
    ~Gl1Renderer() override {
        removeGeometryListener(&Gl1Renderer::geometryGone, this);
        prof::abSwitchGone(this);
        if (!ctx_) return;
#if defined(OMK_GL1_ON_WINDOW)
        // the context is the frontend's, and it may have closed first
        if (onWindow_ && gl1WindowContext() != ctx_) return;
#endif
        {
            Current current(ctx_);
            for (auto& [key, u] : uploaded_) {
                gpuForget(omk::prof::kGlTexture, &u.name, 1);
                glDeleteTextures(1, &u.name);
            }
            uploaded_.clear();
#if defined(OMK_GL1_ON_WINDOW)
            if (readTex_) gpuForget(omk::prof::kGlTexture, &readTex_, 1);
            if (readTex_) glDeleteTextures(1, &readTex_);
            freeOverlayTiles();
            if (probeTex_) glDeleteTextures(1, &probeTex_);
#endif
#if !defined(OMK_GL1_AGL)
            if (fbo_) glDeleteFramebuffersEXT(1, &fbo_);
            if (colour_) gpuForget(omk::prof::kGlRenderbuffer, &colour_, 1);
            if (colour_) glDeleteRenderbuffersEXT(1, &colour_);
            if (depth_) gpuForget(omk::prof::kGlRenderbuffer, &depth_, 1);
            if (depth_) glDeleteRenderbuffersEXT(1, &depth_);
#endif
        }
#if !defined(OMK_GL1_AGL)                    // the AGL context is the frontend's
        if (!onWindow_) CGLDestroyContext(ctx_);  // ...and so is the SDL window's
        if (pbuf_) CGLDestroyPBuffer(pbuf_);
#endif
    }

    bool init(int w, int h) override {
        if (!ctx_ && !makeContext()) return false;
        Current current(ctx_);
        w_ = w; h_ = h;
#if defined(OMK_GL1_ON_WINDOW)
        // the window's own back buffer, which is the window's size - the
        // frame the viewer asks for (the display size)
        if (onWindow_) {
            glDrawBuffer(GL_BACK);
            glReadBuffer(GL_BACK);
        }
#endif
#if !defined(OMK_GL1_AGL)
        if (!onWindow_) {
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
        }   // the offscreen target
#endif
        fb_ = Surface(w, h, 0);
        rgba_.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        readDone_ = false;
        return true;
    }

    // THE TEXTURE POOL, UPLOADED ONLY WHERE IT IS NEW. The frame hands the
    // pool over again whenever it changes - a particle's sprite arriving or
    // leaving, a character coming into view - and re-sending EVERY texture on
    // the card, set and bodies, each texel through a call, costs the iBook G3
    // the likeliest part of `props, guns` at 12-65 ms a frame
    // (`todo/handoff-classic-mac.md` 3f). The original binds a texture into its 58-slot cache once
    // (`Tex3DT_BindMaterials`) and keeps it. As the GLES and 3DS backends do:
    // a texture already on the card from an earlier pool - the same pixel
    // storage, palette and size - is kept, and the entry holds that storage,
    // so its address cannot come back as another texture's while cached;
    // what no slot of the new pool uses leaves the card.
    //
    // A new one goes up as 16-bit ARGB 1555 words - its internal format
    // (`GL_RGB5_A1`) - built through the palette (256 entries, made once a
    // texture) instead of RGBA bytes for the driver to convert: the channels
    // TRUNCATED to five bits, as the engine's own 16-bit conversion does
    // (`rgb565`), and the colour key (alpha 0) on a black palette entry.
    void setTextures(std::span<const Texture> t) override {
        Current current(ctx_);
        flush();
        for (auto& [key, u] : uploaded_) u.used = false;
        tex_.assign(t.size(), 0);
        texW_.assign(t.size(), 1.0f);
        texH_.assign(t.size(), 1.0f);
        const auto keyOf = [](const Texture& x) {
            return std::make_tuple(x.idx.data(), x.pal.data(), x.width, x.height);
        };
        for (const Texture& x : t)
            if (x.hasPixels())
                if (auto hit = uploaded_.find(keyOf(x)); hit != uploaded_.end()) hit->second.used = true;
        int kept = 0, fresh = 0, dropped = 0;
        for (auto it = uploaded_.begin(); it != uploaded_.end(); ) {
            if (it->second.used) { ++it; continue; }
            gpuForget(omk::prof::kGlTexture, &it->second.name, 1);
            glDeleteTextures(1, &it->second.name);
            it = uploaded_.erase(it);
            ++dropped;
        }
        std::vector<std::uint16_t> px, small;
        decideTextureScale();
        for (std::size_t i = 0; i < t.size(); ++i) {
            const Texture& x = t[i];
            if (!x.hasPixels()) continue;
            auto hit = uploaded_.find(keyOf(x));
            if (hit == uploaded_.end()) {
                // OpenGL 1.x wants power-of-two sides; the UVs are in texels, so
                // a padded texture keeps them and only the texture matrix's scale
                // changes. Every shipped .3DT is already a power of two.
                const unsigned pw = nextPow2(static_cast<unsigned>(x.width));
                const unsigned ph = nextPow2(static_cast<unsigned>(x.height));
                std::uint16_t lut[256];
                for (int k = 0; k < 256; ++k) {
                    const std::uint8_t* q = x.pal.data() + 3 * k;
                    lut[k] = static_cast<std::uint16_t>(((q[0] | q[1] | q[2]) ? 0x8000u : 0u) |
                                                        (q[0] >> 3) << 10 | (q[1] >> 3) << 5 | (q[2] >> 3));
                }
                px.assign(static_cast<std::size_t>(pw) * ph, 0);
                const std::uint8_t* idx = x.idx.data();
                for (int y = 0; y < x.height; ++y) {
                    std::uint16_t* d = px.data() + static_cast<std::size_t>(y) * pw;
                    const std::uint8_t* sr = idx + static_cast<std::size_t>(y) * x.width;
                    for (int xx = 0; xx < x.width; ++xx) d[xx] = lut[sr[xx]];
                }
                GLuint name = 0;
                glGenTextures(1, &name);
                glBindTexture(GL_TEXTURE_2D, name);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_ ? GL_LINEAR : GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_ ? GL_LINEAR : GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                // packed 16-bit words in the host's own order: the packed type
                // is read as a host word on either byte order
                glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                // ...AT THE CARD'S SIZE (`decideTextureScale`): the original
                // makes a texture `256 * factor` a side and stretch-blits the
                // page into it (`sub_463420`). The blit's sampling was the
                // card's own; a plain DirectDraw stretch takes the nearest
                // source texel, `src = dst * srcSize / dstSize` - that, here,
                // a RECONSTRUCTION. The texture matrix keeps the logical size
                // (`texW_`), so the texel UVs land on the same picture.
                const unsigned sw = std::max(1u, static_cast<unsigned>(pw * texScaleW_));
                const unsigned sh = std::max(1u, static_cast<unsigned>(ph * texScaleH_));
                const std::uint16_t* upload = px.data();
                if (sw != pw || sh != ph) {
                    small.assign(static_cast<std::size_t>(sw) * sh, 0);
                    for (unsigned yy = 0; yy < sh; ++yy) {
                        const std::uint16_t* srow = px.data() + static_cast<std::size_t>(yy * ph / sh) * pw;
                        std::uint16_t* drow = small.data() + static_cast<std::size_t>(yy) * sw;
                        for (unsigned xx = 0; xx < sw; ++xx) drow[xx] = srow[xx * pw / sw];
                    }
                    upload = small.data();
                }
                // GL_UNSIGNED_SHORT_1_5_5_5_REV and GL_BGRA are OpenGL 1.2: an
                // OpenGL 1.1 driver (the reader's iMac G3, "OpenGL 1.1.ATI-5.69",
                // 2026-10-30) refuses them and the texture is left empty - the
                // world drawn white. The first upload says which: refused, every
                // texture from then on goes as RGBA bytes, the same 1555 texels
                // widened, still stored at 16 bits by the card.
                const GLsizei gw = static_cast<GLsizei>(sw), gh = static_cast<GLsizei>(sh);
                if (packed1555_) {
                    if (!packedKnown_) while (glGetError() != GL_NO_ERROR) {}
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, gw, gh, 0, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV,
                                 upload);
                    if (!packedKnown_) {
                        packedKnown_ = true;
                        packed1555_ = glGetError() == GL_NO_ERROR;
                        if (!packed1555_)
                            std::printf("gl1: the driver refuses OpenGL 1.2's packed 1555 texels - "
                                        "the textures are sent as RGBA bytes\n");
                    }
                }
                if (!packed1555_) {
                    bytes_.resize(static_cast<std::size_t>(sw) * sh * 4);
                    for (std::size_t k = 0, n = static_cast<std::size_t>(sw) * sh; k < n; ++k) {
                        const unsigned p = upload[k];
                        const unsigned r5 = (p >> 10) & 31, g5 = (p >> 5) & 31, b5 = p & 31;
                        bytes_[4 * k + 0] = static_cast<std::uint8_t>(r5 << 3 | r5 >> 2);
                        bytes_[4 * k + 1] = static_cast<std::uint8_t>(g5 << 3 | g5 >> 2);
                        bytes_[4 * k + 2] = static_cast<std::uint8_t>(b5 << 3 | b5 >> 2);
                        bytes_[4 * k + 3] = (p & 0x8000u) ? 0xFF : 0x00;
                    }
                    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, gw, gh, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                                 bytes_.data());
                }
                gpuNote("textures", omk::prof::kGlTexture, name, 2LL * sw * sh);   // 16 bits a texel
                hit = uploaded_.emplace(keyOf(x), Uploaded{name, x.idx, x.pal, static_cast<float>(pw),
                                                           static_cast<float>(ph), true}).first;
                ++fresh;
            } else {
                ++kept;
            }
            tex_[i] = hit->second.name;
            texW_[i] = hit->second.w;
            texH_[i] = hit->second.h;
        }
        boundTex_ = ~0u;
        // what stayed, what went up, what left - the first ten pools, then one
        // line in fifty, since a sprite coming and going changes the pool
        static long pools = 0;
        static long keptAll = 0, freshAll = 0;
        ++pools; keptAll += kept; freshAll += fresh;
        if (pools <= 10 || pools % 50 == 0)
            std::printf("gl1: texture pool %ld, %zu slots - %d kept on the card, %d uploaded, "
                        "%d dropped (all pools so far: %ld kept, %ld uploaded)\n",
                        pools, t.size(), kept, fresh, dropped, keptAll, freshAll);
    }

    void begin(const View& v) override {
        OMK_ZONE("gl1: begin");   // the profiler; `begin`, `submit`, `end` split the frame's
                                  // CPU share from the driver's (`todo/handoff-classic-mac.md` 3f)
        Current current(ctx_);
        flush();
        inFrame_ = false;
        setCamera(v);
        RCamera cam = v.cam;
        cam.w = camW_;
        cam.h = camH_;
        bindTarget();
        glViewport(0, 0, w_, h_);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(v.clearColour[0] / 255.0f, v.clearColour[1] / 255.0f,
                     v.clearColour[2] / 255.0f, 1.0f);   // the scene's fog colour
        glClearDepth(1.0);
        glDepthMask(GL_TRUE);
        {
            // the frame's first call that needs the back buffer: where the
            // CPU WAITS for the card to finish the last frame, if it does -
            // the iBook's fixed 7 ms in `world begin, set` (`todo/handoff-classic-mac.md`)
            OMK_ZONE("gl1: clear");
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
#if defined(OMK_GL1_ON_WINDOW)
        // THE LETTERBOX BARS BLACK. On AGL the picture is drawn at its
        // letterbox place in the window's own back buffer and presented as it
        // stands, so the rows above and below it are the window's - and the
        // clear above leaves them the FOG colour, so without this a
        // cutscene's bars come out sky-tinted. The composed frame's bars are
        // black, as the original's are; only the two bands are cleared again,
        // scissored.
        if (onWindow_ && v.letterboxed()) {
            const int top = std::max(0, std::min(v.vy, h_ - cam.h));
            const int below = h_ - top - cam.h;
            glEnable(GL_SCISSOR_TEST);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            if (top > 0) { glScissor(0, h_ - top, w_, top); glClear(GL_COLOR_BUFFER_BIT); }
            if (below > 0) { glScissor(0, 0, w_, below); glClear(GL_COLOR_BUFFER_BIT); }
            glDisable(GL_SCISSOR_TEST);
        }
#endif
        // the picture in the TOP-LEFT cam.w x cam.h, as the reference draws it;
        // GL's window origin is bottom-left. On classic Mac OS (AGL) it is
        // drawn at its LETTERBOX place in the window's back buffer instead -
        // `view.vy` rows down, where the composed frame puts it - so a frame
        // nothing is drawn over is presented as it stands (`presentWorld`,
        // todo/cpu-vs-original.md tier B); `readback` takes it from there.
#if defined(OMK_GL1_ON_WINDOW)
        vy_ = onWindow_ && v.letterboxed() ? std::max(0, std::min(v.vy, h_ - cam.h)) : 0;
#endif
        glViewport(0, h_ - vy_ - cam.h, cam.w, cam.h);

        // THE CAMERA is applied on the CPU (`setCamera`, in submit). With the
        // fog on the CPU both GL matrices stay identity: what reaches GL is
        // already in clip space. With the fog ON THE CARD (`cardFog_`) the
        // vertices are VIEW-space and the projection is a matrix
        // (`loadProjection`) - the same clip coordinates, and the eye depth
        // the card's fog needs.
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        loadProjection();
        inFrame_ = true;

        // the D3D device's fixed states, set once (`sub_4638C0`)
        glShadeModel(GL_SMOOTH);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);                 // strict: the first of two equal faces wins
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glAlphaFunc(GL_GREATER, 0.5f);
        // THE FOG ON THE CARD, as the original's: Direct3D TABLE fog
        // (`Raster_DrawTriangles`: FOGTABLEMODE 3, linear, per bucket - off
        // for the near and transparent keys, start and end doubled for 0x800;
        // `docs/ASSETS.md`), which the card computes from the depth. OpenGL
        // 1.1's own linear `GL_FOG` is its equal: the colour fogged after
        // texturing, `lerp(texel * colour, fog, 1 - f)` with f = (end - z) /
        // (end - start) - the reference's - and on a card without a colour
        // sum (OS 9's OpenGL 1.1, the iMac G3) no second pass: that pass was
        // 23-26 of a 100 m street frame's 46 draw calls there (2026-11-02,
        // `todo/handoff-classic-mac.md` 3i item 12). The state goes per batch
        // (`fogKindFor`, `applyFog`). `--gl1-cpu-fog` keeps the fog on the CPU
        // below, the A/B.
        //
        // ...THE FOG ON THE CPU: computed per vertex (`emit`). colour * (1 - a)
        // plus the fog colour * a as a SECONDARY colour added after texturing
        // is the reference's lerp(texel * colour, fog, a), vertex by vertex.
        // It was the default until 2026-11-02 for a note from Tiger, "under
        // emulation that unit darkened the near scene" - with the GPU's own
        // transform, whose clip-space z the card's fog then read.
        glDisable(GL_FOG);
        glFogState_ = -1;
        fogRGB_[0] = v.fogColour[0] / 255.0f;
        fogRGB_[1] = v.fogColour[1] / 255.0f;
        fogRGB_[2] = v.fogColour[2] / 255.0f;
        if (cardFog_) {
            const GLfloat c[4] = {fogRGB_[0], fogRGB_[1], fogRGB_[2], 1.0f};
            glFogi(GL_FOG_MODE, GL_LINEAR);
            glFogfv(GL_FOG_COLOR, c);
            static bool told = false;
            if (!told) {
                told = true;
                std::printf("gl1: the fog ON THE CARD - OpenGL's linear fog from the eye depth, "
                            "the original's table fog (--gl1-cpu-fog: per vertex on the CPU)\n");
            }
        }
        fogSum_ = !cardFog_ && (fogRGB_[0] > 0 || fogRGB_[1] > 0 || fogRGB_[2] > 0);
        fogPass_ = false;
        const bool fogMTWas = fogMT_;
        fogMT_ = false;
        static const bool noSum = envFlag("OMK_GL1_NO_COLOUR_SUM");   // the fallback, on a host that has the sum
        static const bool unitEnv = envFlag("OMK_GL1_FOG_UNIT");      // the second unit, not the second pass
        if (fogSum_ && (!hasColourSum_ || noSum) && (fogUnit_ || unitEnv) && multitexture()) {
            // THE FOG IN ONE PASS, ON THE SECOND TEXTURE UNIT - OPT-IN
            // (`--gl1-fog-unit`): unit 1 ADDS an RGB ramp holding fog * a,
            // sampled at the vertex's `a`, under GL_ADD (EXT_texture_env_add,
            // offered by the iMac's driver; core in 1.3); the vertex colour
            // darkened by (1 - a) as for the second pass. That is the colour
            // sum's texel * colour * (1 - a) + fog * a exactly, for every batch
            // - opaque, cutout, translucent and additive alike - with none of
            // the second pass's extra draw calls (19 of the iMac's 43 a frame).
            // NOT the default: its first form (GL_BLEND with the fog as the env
            // colour on a luminance ramp, `324b4db`) drew the iMac G3's horizon
            // as a light-blue hole again (the reader, 2026-10-31), where the
            // second pass is CONFIRMED there - an ATI Rage 128 driver's second
            // unit is not trusted until the iMac says so.
            static bool told = false;
            if (!told) {
                told = true;
                std::printf("gl1: no EXT_secondary_color%s - a coloured fog is added on the second "
                            "texture unit (ARB_multitexture + texture_env_add, one pass)\n",
                            noSum ? " (OMK_GL1_NO_COLOUR_SUM)" : "");
            }
            fogSum_ = false;
            fogMT_ = true;
            fogMTUnitOn(true);
        } else if (fogSum_ && (!hasColourSum_ || noSum)) {
            // A RENDERER WITHOUT EXT_secondary_color (OpenGL 1.1: Mac OS 9's
            // software one, the reader's iMac G3's Rage 128): the fog's colour
            // goes on in a SECOND PASS (`flush`). The streets' day/night fog
            // IS coloured - Anekbah's 91 160 171 - and without it the far
            // scene faded to black against the fog-coloured clear: a flat
            // light-blue band along the iMac's horizon (2026-10-30).
            static bool told = false;
            if (!told) {
                told = true;
                std::printf("gl1: no EXT_secondary_color%s - a coloured fog is added in a second pass\n",
                            noSum ? " (OMK_GL1_NO_COLOUR_SUM)" : "");
            }
            fogSum_ = false;
            fogPass_ = true;
            fogByte_[0] = toByte(fogRGB_[0]); fogByte_[1] = toByte(fogRGB_[1]); fogByte_[2] = toByte(fogRGB_[2]);
        }
        if (fogMTWas && !fogMT_) fogMTUnitOn(false);
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
        submits_ = batches_ = 0;
        readDone_ = false;
    }

    void submit(const Draw& d) override {
        if (!d.geo || d.count < 3) return;
        OMK_ZONE("gl1: submit");  // the loop below; a batch's draw call nests inside it
        Current current(ctx_);
        const Geometry& g = *d.geo;
        if (d.start + d.count > g.corners.size()) return;

        // ONE DRAW CALL A RUN OF EQUAL STATE, as the original's: it filled a
        // bucket's vertices into one pool and made one `DrawPrimitive` a
        // non-empty bucket (`Render_FlushBuckets`, `Raster_DrawTriangles`).
        // The vertices below are already in clip space with the fog folded
        // into their colours, so only the DEVICE state - blend, alpha test,
        // texture - divides a batch; a draw that changes none of it appends
        // to the one pending. Each `glDrawArrays` costs the driver a fixed
        // set-up, which on Mac OS X 10.2's Rage 128 path is the CPU's.
        {
            const std::size_t slot = drawTextureSlot(d);
            const bool hasTex = slot < tex_.size() && tex_[slot] != 0;
            const unsigned want = hasTex ? static_cast<unsigned>(slot) : ~1u;
            if (pending() && (!stateValid_ || d.blend != blend_ || d.cutout != cutout_ ||
                                  want != boundTex_ || (cardFog_ && fogKindFor(d) != fogKind_)))
                flush();
        }
        setState(d);
        ++submits_;

        // IN THE ORIGINAL'S SHAPE (`submitShape`) wherever it applies: the
        // geometry's distinct vertices transformed once and in order, its
        // faces indexing them. A body posed here (its rest corners, an affine
        // a mesh, lit per corner) and a geometry too small or changing every
        // frame stay corner by corner below, as does every draw under
        // `--gl1-per-corner` - the A/B.
        const bool posedDraw = d.meshPose && d.meshPoses && g.cornerMesh.size() == g.corners.size();
        if (!perCorner_ && !posedDraw) {
            if (Shape* s = shapeFor(g)) {
                ++statShapeDraws_;
                if (!s->vert16.empty()) submitShape(d, *s, s->vert16.data());
                else submitShape(d, *s, s->vert.data());
                return;
            }
        }
        ++statCornerDraws_;

        // Per triangle, what the original's own vertex loop did before
        // DrawPrimitive: into view space, the near cut (Sutherland-Hodgman
        // against z > kNearCut, as `raster.cpp`), the single-sided cull with
        // the reference's sign, the screen edges, then CLIP-SPACE positions -
        // (x w, y w, depth w, w) with w the view depth, which is what keeps
        // the texturing perspective-correct the way `rhw` did for D3D.
        //
        // THE TRIANGLE WHOLLY INSIDE - most of a frame's - TAKES NO CLIP
        // (for the iBook G3): each corner's OUTCODE against the
        // five planes is the clip's own test (`z > kNearCut`, then each
        // edge's `dist >= 0` with the same expression), so a triangle with
        // all corners inside comes out of the clips exactly as it went in,
        // and one with all corners outside ONE plane comes out empty - both
        // are decided here without running them. And NO DIVISION: the cull
        // is the sign of the view-space determinant (the projected area's
        // sign for corners in front of the eye), and the clip-space position
        // is (x / th, y / tv, z - 2 near, z), the reference's screen mapping
        // with the w multiplied back in. The G3 divides in 17-31 cycles and
        // does not pipeline it; this was 6 divisions a triangle and 3 a
        // vertex. What is lost is rounding: the old form went through the
        // screen position and back, and a triangle edge-on to the eye (zero
        // area, no pixels) may now cull on the other side of zero.
        const std::size_t nCorner = d.count - d.count % 3;
        const bool cullOn = g.cornerCull.size() == g.corners.size() && !noCull();
        const float gx = gx_, gy = gy_;

        // A POSED DRAW (`posesBodies`): each mesh's affine with the camera
        // folded in, made the first time one of its corners comes up in this
        // draw - view = B (R rest + t - eye), B the camera's three rows - and
        // the lights that reach the body, applied per corner below
        const bool posed = d.meshPose && d.meshPoses && g.cornerMesh.size() == g.corners.size();
        CornerLight pl[kPoseLights];
        int nl = 0;
        bool relight = false;
        if (posed) {
            if (++poseDraw_ == 0) { std::fill(poseGen_.begin(), poseGen_.end(), 0u); poseDraw_ = 1; }
            if (poseGen_.size() < d.meshPoses) {
                poseGen_.resize(d.meshPoses, 0u);
                poseView_.resize(12 * d.meshPoses);
            }
            nl = prepareLights(d, kPoseLights, pl);    // o3de/drawcore.h
            relight = relit(d, nl);
        }
        const auto viewAffine = [&](std::size_t m) -> const float* {
            float* P = poseView_.data() + 12 * m;
            if (poseGen_[m] == poseDraw_) return P;
            poseGen_[m] = poseDraw_;
            const float* a = d.meshPose + 12 * m;
            const float t[3] = {a[3] - eye_[0], a[7] - eye_[1], a[11] - eye_[2]};
            const float* rows[3] = {bs_, bu_, bf_};
            for (int r = 0; r < 3; ++r) {
                const float* B = rows[r];
                for (int j = 0; j < 3; ++j) P[4 * r + j] = B[0] * a[j] + B[1] * a[4 + j] + B[2] * a[8 + j];
                P[4 * r + 3] = B[0] * t[0] + B[1] * t[1] + B[2] * t[2];
            }
            return P;
        };

        // A VERTEX ONCE, NOT ONCE A CORNER: `indexFor` says which
        // corners of the geometry are one vertex (equal bytes, one mesh), and
        // within this draw a corner whose vertex was already transformed takes
        // its view position and outcode - and, once sent in this batch, its
        // INDEX - instead of computing and sending its own. Taken only when
        // its bytes are still the computing corner's, so a stale index costs
        // sharing, never a picture. Anekbah's set is 139245 corners on 63079
        // vertices; Kay'l 1626 on 272.
        const VIndex* vi = indexFor(g);
        ++drawStamp_;
        const std::vector<std::uint8_t>* tieLose = nullptr;
        if (const auto bt = tieBaked_.find(&g); bt != tieBaked_.end()) tieLose = &bt->second;
        // RUNS OF 4 TRIANGLES WHOLLY OUTSIDE ONE PLANE, SKIPPED UNREAD (step
        // 3h): two in five of a street frame's triangles are off the screen,
        // and each was read (48 bytes a corner, on a bus that reads ~170 MB/s)
        // and transformed before its outcodes rejected it. A run's bounding
        // sphere (`clustersFor`, built once a geometry) beyond one plane - by
        // a margin - means every corner of it carries that plane's bit, so
        // every triangle would have been rejected: the same picture. Not for
        // a posed draw, whose corners are at rest.
        // `OMK_GL1_NO_SKIP=1` turns the skip off, to show it changes no pixel
        // (`verify.py: engine: gl1 backend`)
        static const bool noSkip = omk::envFlag("OMK_GL1_NO_SKIP");
        const Clusters* cl = posed || noSkip ? nullptr : clustersFor(g);
        std::size_t clusterChecked = ~std::size_t{0};
        for (std::size_t t = 0; t < nCorner; t += 3) {
            if (cl) {
                const std::size_t c = (d.start + t) / kClusterCorners;
                if (c != clusterChecked) {
                    clusterChecked = c;
                    const float* sp = cl->sph.data() + 4 * c;
                    if (sphereOutside(sp)) {
                        // to the end of the run (or of the draw)
                        const std::size_t runEnd = (c + 1) * kClusterCorners - d.start;
                        const std::size_t to = std::min(runEnd, nCorner);
                        const std::size_t tris = (to - t + 2) / 3;
                        st_.triangles += static_cast<long>(tris);
                        st_.offscreen += static_cast<long>(tris);
                        statSkipped_ += static_cast<long>(3 * tris);
                        t = to - 3;                 // the loop's += 3 lands on `to`
                        continue;
                    }
                }
            }
            ++st_.triangles;
            if (pending() >= kBatchMax) flush();        // a bounded pool, as the original's
            // THE TRIANGLE TWO AHEAD, ASKED FOR NOW: the G3 has no hardware
            // prefetch, so without it each corner's 48 bytes are a miss the
            // loop waits on (~170 MB/s, a street frame reads ~1 MB of them).
            // A hint - `dcbt` on a PowerPC - over its 144 bytes, a 32-byte
            // line at a time; it changes no value read. (Fetching only the
            // corners of vertices not yet seen - about half - was measured on
            // the iMac G3 on 2026-11-02 and gained nothing: `todo/handoff-
            // classic-mac.md` 3i item 10.)
            if (t + 9 <= nCorner) {
                const char* ahead = reinterpret_cast<const char*>(&g.corners[d.start + t + 6]);
                for (std::size_t b = 0; b < 3 * sizeof(Corner); b += 32) __builtin_prefetch(ahead + b);
            }
            Vtx in[3];
            unsigned oc[3];
            std::int32_t meshOf[3] = {-1, -1, -1};
            std::int32_t slot[3] = {-1, -1, -1};        // the corner's cached vertex, when it has one
            // the POSITIONS and the outcodes first: a triangle wholly outside
            // (two in five of a street frame's) or facing away leaves before
            // any of its colour work, which was done for every corner first
            for (int k = 0; k < 3; ++k) {
                const std::size_t ci = d.start + t + static_cast<std::size_t>(k);
                const Corner& c = g.corners[ci];
                float x, y, z;
                if (posed) {
                    const std::int32_t m = g.cornerMesh[ci];
                    if (m >= 0 && static_cast<std::size_t>(m) < d.meshPoses) meshOf[k] = m;
                }
                ++statCorners_;
                if (vi) {
                    const std::uint32_t v = vi->vert[ci];
                    const VSlot& vsl = vs_[v];
                    if (vsl.stamp == drawStamp_) {
                        const std::size_t r = vsl.rep;
                        const bool asBuilt = !vi->allDoubt && !vi->doubt[ci] && !vi->doubt[r];
                        if (asBuilt) ++statTrusted_;
                        if (r == ci || asBuilt || sameCorner(g, r, ci)) {
                            in[k].v[0] = vsl.x; in[k].v[1] = vsl.y; in[k].v[2] = vsl.z;
                            oc[k] = vsl.oc;
                            slot[k] = static_cast<std::int32_t>(v);
                            continue;
                        }
                        // the same vertex by the index, other bytes now: its own
                    } else {
                        slot[k] = static_cast<std::int32_t>(v);   // computed below, kept
                    }
                }
                ++statTransformed_;
                if (meshOf[k] >= 0) {
                    const float* P = viewAffine(static_cast<std::size_t>(meshOf[k]));
                    x = P[0] * c.x + P[1] * c.y + P[2] * c.z + P[3];
                    y = P[4] * c.x + P[5] * c.y + P[6] * c.z + P[7];
                    z = P[8] * c.x + P[9] * c.y + P[10] * c.z + P[11];
                } else {
                    const float dx = c.x - eye_[0], dy = c.y - eye_[1], dz = c.z - eye_[2];
                    x = dx * bs_[0] + dy * bs_[1] + dz * bs_[2];
                    y = dx * bu_[0] + dy * bu_[1] + dz * bu_[2];
                    z = dx * bf_[0] + dy * bf_[1] + dz * bf_[2];
                }
                in[k].v[0] = x; in[k].v[1] = y; in[k].v[2] = z;
                unsigned o = z > kNearCut ? 0u : 1u;
                if (z * gx - x < 0.0f) o |= 2u;
                if (z * gx + x < 0.0f) o |= 4u;
                if (z * gy - y < 0.0f) o |= 8u;
                if (z * gy + y < 0.0f) o |= 16u;
                oc[k] = o;
                if (slot[k] >= 0) {
                    VSlot& vsl = vs_[static_cast<std::size_t>(slot[k])];
                    vsl.stamp = drawStamp_;
                    vsl.rep = static_cast<std::uint32_t>(ci);
                    vsl.x = x; vsl.y = y; vsl.z = z;
                    vsl.oc = static_cast<std::uint8_t>(o);
                    vsl.outStamp = 0;                     // not sent yet
                }
            }
            const unsigned outAll = oc[0] & oc[1] & oc[2];
            if (outAll) {
                if (outAll & 1u) ++st_.behind; else ++st_.offscreen;
                continue;
            }
            const bool cullThis = cullOn && g.cornerCull[d.start + t] != 0;
            const bool inside = (oc[0] | oc[1] | oc[2]) == 0;
            if (inside && cullThis && backFacing(in[0], in[1], in[2])) { ++st_.culled; continue; }
            // ...then the COLOURS, of a corner that is sent (`colourOf`)
            const auto colourOf = [&](int k) {
                const Corner& c = g.corners[d.start + t + static_cast<std::size_t>(k)];
                float rgb[3] = {c.r, c.g, c.b};
                if (relight) {
                    // from the scene's grey when the body starts black, else
                    // from the corner's colour, each reaching light added and
                    // clamped in turn on the POSED normal (`o3de/drawcore.h`)
                    lightStart(d, c, rgb);
                    if (nl > 0) {
                        float n[3] = {c.nx, c.ny, c.nz};
                        if (meshOf[k] >= 0)
                            poseNormal(d.meshPose + 12 * static_cast<std::size_t>(meshOf[k]), c.nx, c.ny, c.nz, n);
                        lightCorner(n, pl, nl, rgb);
                    }
                }
                // the shimmer first, as the engine adds it into the bytes
                const float sh = shimmerOffset(c.phase, view_.shimmerClock);
                in[k].c[0] = rgb[0] + sh; in[k].c[1] = rgb[1] + sh; in[k].c[2] = rgb[2] + sh;
                // ...then the greyscale bank (ops 150/151, `o3de/greybank.h`)
                if (view_.grey)
                    in[k].c[0] = in[k].c[1] = in[k].c[2] =
                        lumaGreyUnit(in[k].c[0], in[k].c[1], in[k].c[2]);
                in[k].u = c.u; in[k].t = c.v;
            };
            // A TIE LOSER (`bakeDepthTie`) is drawn a hair BACK and sends its
            // own vertices - a shared one is at the plain depth
            const bool lose = tieLose && tieLose->size() > (d.start + t) / 3 && (*tieLose)[(d.start + t) / 3];
            tieBack_ = lose;
            if (lose) ++statTieBack_;
            if (inside && lose) {
                ++st_.drawn;
                for (int k = 0; k < 3; ++k) { colourOf(k); idx_.push_back(emit(in[k])); }
                tieBack_ = false;
                continue;
            }
            if (inside) {
                ++st_.drawn;
                for (int k = 0; k < 3; ++k) {
                    const std::int32_t sl = slot[k];
                    if (sl >= 0 && vs_[static_cast<std::size_t>(sl)].outStamp == batchStamp_) {
                        idx_.push_back(vs_[static_cast<std::size_t>(sl)].out);   // sent: its index
                        continue;
                    }
                    colourOf(k);
                    const std::uint16_t o = emit(in[k]);
                    idx_.push_back(o);
                    if (sl >= 0) {
                        vs_[static_cast<std::size_t>(sl)].out = o;
                        vs_[static_cast<std::size_t>(sl)].outStamp = batchStamp_;
                    }
                }
                continue;
            }
            for (int k = 0; k < 3; ++k) colourOf(k);
            emitClipped(in, cullThis);
        }
    }

    void end() override {
        OMK_ZONE("gl1: end");     // the last batch, and the `glFlush` the driver may work in
        Current current(ctx_);
        flush();
        // THE FOG'S COLOUR SUM ENDS WITH THE WORLD. Left on, everything drawn
        // after it in the frame - the interface overlay, the fade quad, the
        // frontend's menus and films - gets the CURRENT secondary colour added
        // after texturing, and after a vertex array that value is undefined:
        // the iBook's Rage 128 keeps the last fog colour, so with a coloured
        // fog (the Impasse, the tutorial, the streets - not Kay'l's
        // black-fogged room) the overlay's transparent texels come out as
        // that colour over the world and the blend saturates the window
        // WHITE (`todo/handoff-classic-mac.md` 3f step 2k); the emulated
        // Tiger's Radeon leaves it zero, so it never shows there.
        // `begin` sets it again for the next frame.
        if (hasColourSum_) { glDisable(OMK_GL_COLOR_SUM); glDisableClientState(OMK_GL_SECONDARY_ARRAY); }
        // ...and so does the second unit's ramp, for the same reason, and the
        // card's fog: the overlay is drawn unfogged
        if (fogMT_) { fogMTUnitOn(false); fogMT_ = false; }
        if (glFogState_ > 0) glDisable(GL_FOG);
        glFogState_ = -1;
        inFrame_ = false;
        stateValid_ = false;
        glFlush();
        // what the batching saved: passes 61-120 summed (a street frame is
        // past its setup by then; a frame may be several passes), and then
        // the last 60 of every 600 (so a run that reaches the street minutes
        // later reports it too) - the draws handed over, the draw calls they
        // became, the triangles
        sum_[0] += submits_; sum_[1] += batches_; sum_[2] += st_.triangles;
        sum_[3] += st_.drawn; sum_[4] += st_.culled;
        ++passes_;
        if (passes_ == 60 || passes_ % 600 == 540) {
            sum_[0] = sum_[1] = sum_[2] = sum_[3] = sum_[4] = 0;
            statCorners_ = statTransformed_ = statSent_ = statSkipped_ = statTieBack_ = statTrusted_ = 0;
            statShapeDraws_ = statCornerDraws_ = statPatched_ = statRebuilt_ = 0;
            fogPasses_ = 0;
            lockedDraws_ = 0;
            fogChanges_ = 0;
        }
        if (passes_ == 120 || passes_ % 600 == 0)
            std::printf("gl1: 60 passes drew %ld draws in %ld draw calls, %ld of %ld triangles "
                        "(%ld culled); %ld corners, %ld transformed, %ld vertices sent, %ld "
                        "corners skipped unread in runs off the screen, %ld tie losers drawn back, "
                        "%ld shared corners taken without a compare (to pass %ld)\n",
                        sum_[0], sum_[1], sum_[3], sum_[2], sum_[4],
                        statCorners_, statTransformed_, statSent_, statSkipped_, statTieBack_,
                        statTrusted_, passes_);
        if (passes_ == 120 || passes_ % 600 == 0)
            std::printf("gl1: ...%ld of those draws submitted in the original's shape (vertices transformed "
                        "once, faces indexing them), %ld corner by corner%s; %ld vertices re-taken from dirty "
                        "lists, %ld shapes built again\n",
                        statShapeDraws_, statCornerDraws_, perCorner_ ? " (--gl1-per-corner)" : "",
                        statPatched_, statRebuilt_);
        if ((passes_ == 120 || passes_ % 600 == 0) && fogPass_)
            std::printf("gl1: ...and %ld fog second passes (no colour sum) in those draw calls\n", fogPasses_);
        if ((passes_ == 120 || passes_ % 600 == 0) && cardFog_)
            std::printf("gl1: ...%ld fog state changes on the card (the original's table fog)\n", fogChanges_);
        if ((passes_ == 120 || passes_ % 600 == 0) && hasLock_)
            std::printf("gl1: ...%ld of those draw calls with their vertices locked (EXT_compiled_vertex_array)\n",
                        lockedDraws_);
    }

    // THE MIRROR ON THE CARD, as the original draws it (`docs/ASSETS.md` 4c)
    // and as the GLES backend does: the reflected scene first, then the room
    // and the mirror over it with NO clear between - draw order and depth
    // confine the reflection. Without this the frame takes `drawWithMirror`'s
    // CPU fallback: three passes, three readbacks and a composite, and the
    // frame then goes to the CPU present ("cpu mirror") - on the iBook, every
    // frame Kay'l's bathroom mirror is in view.
    // `OMK_CPU_MIRROR=1` keeps the fallback, as on GLES.
    bool drawMirrorScene(const View& v, const View& refl, std::span<const Draw> scene,
                         std::span<const Draw> sceneClipped, std::span<const Draw> mirror) override {
        static const bool cpuMirror = omk::envFlag("OMK_CPU_MIRROR");
        if (cpuMirror || mirror.empty()) return false;
        begin(v);
        setCamera(refl);                     // the camera alone: no clear, no state
        for (const auto& d : sceneClipped) submit(d);
        setCamera(v);
        for (const auto& d : scene) submit(d);
        for (const auto& d : mirror) submit(d);
        end();
        return true;
    }

    const Surface& readback() override {
        if (readDone_) return fb_;           // idempotent (renderer.h)
        Current current(ctx_);
        flush();
        bindTarget();
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        // the first read over a known pattern: a read that writes nothing
        // then shows as 85 85 85, not as a black frame (the report below)
        if (!toldRead_) std::memset(rgba_.data(), 0x55, rgba_.size());
        // bytes, not a packed type: endian-free on any Mac
#if defined(OMK_GL1_ON_WINDOW)
        if (onWindow_) readWindow();
#endif
#if !defined(OMK_GL1_AGL)
        if (!onWindow_) glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, rgba_.data());
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

#if defined(OMK_GL1_ON_WINDOW)
    // ---- THE WORLD PRESENTED STRAIGHT (todo/cpu-vs-original.md tier B) ----
    //
    // The world is in the window's back buffer at its letterbox place, the
    // bands cleared black: a frame nothing is drawn over needs no readback,
    // no composite and no `glDrawPixels` - it is swapped as it stands, at the
    // drawable's own depth with the DRIVER's dither (GL_DITHER, on by
    // default), as the original's 16-bit D3D device dithered (`DITHERENABLE`,
    // ASSETS 4). ~5 -> 17 fps on emulated Tiger against the composite
    // (`handoff-classic-mac.md`). Where the window is LARGER than the frame
    // (the SDL build's fullscreen, a dragged edge) the frame is still drawn
    // at its own size, in the back buffer's bottom-left corner, and
    // `fitToWindow` scales that corner up on the card; a window smaller than
    // the frame cannot hold it and stays on the composite.
    bool holdsFrame(int ww, int wh) const { return onWindow_ && ww >= w_ && wh >= h_; }
    bool presentWorld(int ww, int wh) {
        if (!holdsFrame(ww, wh)) return false;
        Current current(ctx_);
        flush();
        fitToWindow(ww, wh);
        OMK_ZONE("gl1: swap");
        swapWindow();
        return true;
    }
    // THE FRAME SCALED TO THE WINDOW, on the card (`todo/handoff-classic-mac.md`
    // 3h): the corner the frame was drawn in copied into a texture
    // (`glCopyTexSubImage2D`, OpenGL 1.1 - no CPU read) and drawn back over
    // the whole window, fitted at its own aspect and centred, the bands
    // black - as the composed frame is fitted (`sdlGl1PresentFrame`), and
    // NEAREST as it is, so a frame changing path does not change its look.
    void fitToWindow(int ww, int wh) {
        if (ww == w_ && wh == h_) return;
        OMK_ZONE("gl1: fit to the window");
        // the probe's view of the corner, BEFORE the fit covers it
        const bool probing = fitProbeDue();
        if (probing) readPixelsTo(fitCorner_, 0, 0, w_, h_);
        readTexture();                       // the readback's texture, shared
        glBindTexture(GL_TEXTURE_2D, readTex_);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w_, h_);
        glViewport(0, 0, ww, wh);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, ww, 0, wh, -1, 1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
        glDisable(GL_DEPTH_TEST); glDisable(GL_FOG); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_LIGHTING);
        if (hasColourSum_) glDisable(OMK_GL_COLOR_SUM);   // the fog's (see `end`)
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);   // `begin` sets MODULATE again
        glColor4f(1, 1, 1, 1);
        const float sc = std::min(static_cast<float>(ww) / w_, static_cast<float>(wh) / h_);
        const float dw = w_ * sc, dh = h_ * sc;
        const float x0 = (ww - dw) * 0.5f, y0 = (wh - dh) * 0.5f;
        const float s1 = static_cast<float>(w_) / readTexW_, t1 = static_cast<float>(h_) / readTexH_;
        glBegin(GL_QUADS);                   // texture row 0 is the back buffer's BOTTOM row
        glTexCoord2f(0, 0);   glVertex2f(x0, y0);
        glTexCoord2f(s1, 0);  glVertex2f(x0 + dw, y0);
        glTexCoord2f(s1, t1); glVertex2f(x0 + dw, y0 + dh);
        glTexCoord2f(0, t1);  glVertex2f(x0, y0 + dh);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        boundTex_ = ~0u;                     // the next draw binds its own
        if (!toldFit_) {
            toldFit_ = true;
            std::printf("gl1: the %dx%d frame scaled to the %dx%d window on the card (x%.2f)\n",
                        w_, h_, ww, wh, static_cast<double>(sc));
        }
        if (probing) fitProbe(ww, wh, x0, y0, sc);
    }
    bool toldFit_ = false;
    // THE FIT PROBE, `OMK_GL1_FIT_PROBE=<file>` (an INSTRUMENT, `verify.py:
    // engine: gl1 window fit`): on the 60th fitted frame the corner is read
    // before the fit and the whole window after it, and every window pixel
    // inside the fitted rectangle is compared with the corner's pixel its
    // centre samples (NEAREST) - said as a count, from the window's own
    // pixels; the window is written to <file> (PPM, top row first) to look at
    long fitFrames_ = 0;
    std::vector<unsigned char> fitCorner_, fitWindow_;
    bool fitProbeDue() {
        static const char* path = omk::envStr("OMK_GL1_FIT_PROBE");
        return path && *path && ++fitFrames_ == 60;
    }
    void readPixelsTo(std::vector<unsigned char>& out, int x, int y, int w, int h) {
        out.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    }
    void fitProbe(int ww, int wh, float x0, float y0, float sc) {
        glFinish();
        readPixelsTo(fitWindow_, 0, 0, ww, wh);
        long inside = 0, match = 0, bandsBlack = 0, bands = 0;
        for (int Y = 0; Y < wh; ++Y)
            for (int X = 0; X < ww; ++X) {
                const unsigned char* p = fitWindow_.data() + (static_cast<std::size_t>(Y) * ww + X) * 4;
                const float fx = (X + 0.5f - x0) / sc, fy = (Y + 0.5f - y0) / sc;
                if (fx < 0 || fy < 0 || fx >= w_ || fy >= h_) {
                    ++bands;
                    bandsBlack += p[0] == 0 && p[1] == 0 && p[2] == 0;
                    continue;
                }
                const unsigned char* q = fitCorner_.data() +
                    (static_cast<std::size_t>(fy) * w_ + static_cast<std::size_t>(fx)) * 4;
                ++inside;
                match += p[0] == q[0] && p[1] == q[1] && p[2] == q[2];
            }
        std::printf("gl1 fit probe: %ld of %ld window pixels are the corner's nearest pixel; "
                    "%ld of %ld band pixels black\n", match, inside, bandsBlack, bands);
        if (FILE* f = std::fopen(omk::envStr("OMK_GL1_FIT_PROBE"), "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", ww, wh);
            for (int Y = wh - 1; Y >= 0; --Y)
                for (int X = 0; X < ww; ++X)
                    std::fwrite(fitWindow_.data() + (static_cast<std::size_t>(Y) * ww + X) * 4, 1, 3, f);
            std::fclose(f);
        }
    }
    // the swap: AGL's on its own context, the SDL window's through its host
    void swapWindow() {
#if defined(OMK_GL1_AGL)
        aglSwapBuffers(ctx_);
#else
        gl1WindowSwap();
#endif
    }
    bool onWindow() const { return onWindow_; }
    // ...and a frame with the interface over it: the composed frame `fb`,
    // whose world rows are the KEY (0xF81F), and `mask` - how much of the
    // world shows through a pixel - drawn OVER the world as the GLES
    // overlay does: `C + world * M`, then the colour fade (`fade` rgb + its
    // weight). Fixed function: the CPU folds key and mask into one RGBA
    // texture, a quad blends it with (GL_ONE, GL_SRC_ALPHA); only the band of
    // rows the overlay touches is uploaded and drawn. THE FADE IS A SECOND
    // QUAD: untextured over the whole window, its colour at its weight,
    // blended (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) - `dst*(1-f) + fade*f`,
    // the GLES shader's `rgb*(1-f) + fade*f` done by the blender. Folded into
    // the texture it would change every row every frame of a fade, and the
    // emulated Tiger re-sends the whole frame for 26-31 ms each.
    bool presentOverlay(const Surface& fb, const std::uint8_t* mask, const std::uint8_t* maskRow,
                        const float fade[4], int ww, int wh, bool probe = false) {
        if (!holdsFrame(ww, wh) || fb.w != w_ || fb.h != h_) return false;
        // the overlay goes over the world in the corner it was drawn in, at
        // the frame's size; the window's own size is the fit's, before the swap
        const int winW = ww, winH = wh;
        ww = w_; wh = h_;
        int lo = fb.h, hi = -1;
        const bool fading = fade[3] > 0.0f;
        // EACH ROW AGAINST THE COPY LAST SEEN: a row whose 565 pixels and
        // mask are word for word what they were costs one compare - its band
        // flag and its texture row stand. Hashing every row of the band each
        // frame, and scanning it for the key once more, costs ~14 ms an
        // overlay frame on the iBook with nothing on the interface changing
        // (a subtitle, a dialogue line; median, `todo/handoff-classic-mac.md`
        // 3f), and a hash is only nearly exact where the compare is exact. `maskRow` (the glue's, as GLES's): a row no blended
        // pass touched has an all-zero mask, which is then never read.
        {
            OMK_ZONE("gl1: overlay compare");
            const std::size_t W = static_cast<std::size_t>(fb.w);
            if (ovSeenRow_.size() != static_cast<std::size_t>(fb.h) || ovSumW_ != fb.w) {
                ovSumW_ = fb.w;
                ovSeenRow_.assign(static_cast<std::size_t>(fb.h), 0);   // nothing seen
                ovRowAny_.assign(static_cast<std::size_t>(fb.h), 0);
                ovRowBlack_.assign(static_cast<std::size_t>(fb.h), 0);
                ovRowDraw_.assign(static_cast<std::size_t>(fb.h), 0);
                // the column BLOCKS a row's changes are tracked in:
                // at most 32 a row, 32 pixels or wider
                ovBlockW_ = std::max<int>(32, (fb.w + 31) / 32);
                ovBlocks_ = (fb.w + ovBlockW_ - 1) / ovBlockW_;
                ovAllBlocks_ = ovBlocks_ >= 32 ? ~0u : (1u << ovBlocks_) - 1u;
                ovRowStale_.assign(static_cast<std::size_t>(fb.h), ovAllBlocks_);
                ovSum_.assign(static_cast<std::size_t>(fb.h) * static_cast<std::size_t>(ovBlocks_) * 2u, 0u);
            }
            // the rows the world's pass cleared BLACK itself: the letterbox
            // bars (`begin`, AGL) - a composed row there that is opaque black
            // and nothing else is already on the screen, and is not drawn
            const int viewTop = vy_, viewBot = vy_ + camH_;
            for (int y = 0; y < fb.h; ++y) {
                const std::size_t yy = static_cast<std::size_t>(y);
                const std::size_t o = yy * W;
                const std::uint16_t* row = fb.px.data() + o;
                const bool hasMask = mask && (!maskRow || maskRow[yy]);
                // 1 = seen without a mask, 2 = seen with one, 0 = never seen
                const std::uint8_t seenAs = hasMask ? 2 : 1;
                const bool seen = ovSeenRow_[yy] == seenAs;
                ovSeenRow_[yy] = seenAs;
                // WHICH BLOCKS of the row changed (steps 3c, 3e): each block's
                // two CHECKSUMS against the ones it had - the frame read once
                // and nothing else. Step 3c compared it with a KEPT COPY, and
                // the iBook's sixth run showed what that costs on its bus:
                // the copy read cold every frame, 1.2 MB in all, 8.7 ms a
                // street frame and 19.5 in the fight. Two sums of different
                // kinds (a*31 + w, and a rotate-xor) a 64-byte block: a change
                // they both miss is not a practical case.
                std::uint32_t* sum = ovSum_.data() + yy * static_cast<std::size_t>(ovBlocks_) * 2u;
                std::uint32_t blocks = 0;
                for (int bk = 0; bk < ovBlocks_; ++bk) {
                    const std::size_t x0 = static_cast<std::size_t>(bk * ovBlockW_);
                    const std::size_t n = std::min<std::size_t>(W - x0, static_cast<std::size_t>(ovBlockW_));
                    std::uint32_t h1 = 0x811C9DC5u, h2 = 0x9E3779B9u;
                    overlayBlockSums(row + x0, n * sizeof(std::uint16_t), h1, h2);
                    if (hasMask) overlayBlockSums(mask + o + x0, n, h1, h2);
                    if (!seen || sum[2 * bk] != h1 || sum[2 * bk + 1] != h2) {
                        blocks |= 1u << bk;
                        sum[2 * bk] = h1; sum[2 * bk + 1] = h2;
                    }
                }
                if (blocks) {
                    ovRowStale_[yy] |= blocks;
                    bool any = false, black = true;
                    for (std::size_t x = 0; x < W; ++x) {
                        // a non-key pixel's mask is 0 (the glue's), so a 0
                        // pixel is opaque black
                        if (row[x] != 0xF81F) any = true;
                        if (row[x] != 0) { black = false; if (any) break; }
                    }
                    ovRowAny_[yy] = any ? 1 : 0;
                    ovRowBlack_[yy] = black ? 1 : 0;
                }
                const bool barCleared = ovRowBlack_[yy] && (y < viewTop || y >= viewBot);
                ovRowDraw_[yy] = ovRowAny_[yy] && !barCleared ? 1 : 0;
                if (ovRowDraw_[yy]) { if (lo > y) lo = y; hi = y; }
            }
        }
        Current current(ctx_);
        flush();
        if (hi >= lo || fading) {
            // the 2D state for the quads; `begin` sets all of its own again
            glViewport(0, 0, ww, wh);
            glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, ww, 0, wh, -1, 1);
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();
            glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
            glDisable(GL_DEPTH_TEST); glDisable(GL_FOG); glDisable(GL_CULL_FACE);
            glDisable(GL_ALPHA_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_LIGHTING);
            if (hasColourSum_) glDisable(OMK_GL_COLOR_SUM);   // the fog's (see `end`)
            glEnable(GL_BLEND);
            stateValid_ = false;             // the draw state the cache assumed is gone
            boundTex_ = ~0u;
        }
        if (hi >= lo) {
            // IN TILES: the overlay is 64-row tiles two blocks wide (64
            // pixels at 640), each a texture of its own, made the first time a
            // tile holds something. As ONE 1024x512 RGBA8 texture, a draw of it
            // after any change costs the iBook ~43 ms - the driver sending the
            // whole 2 MB again - every frame of a fight, whose gauges' sparks
            // change it each frame (`gl1: overlay draw` 42.9 ms there, 0.75 in
            // the street where it rarely changes; `todo/handoff-classic-mac.md`
            // 3f). In tiles a change re-sends only the tiles it touches, and a
            // tile with nothing drawn is neither sent nor drawn.
            // The probe's path (`gl1OverlayPath`): client storage stores ARGB
            // 8888 words, which is what it was timed with.
            const Gl1UploadPath path = gl1OverlayPath();
            const int tileW = 2 * ovBlockW_, tileH = 64;
            const GLsizei texW = static_cast<GLsizei>(nextPow2(static_cast<unsigned>(tileW)));
            const int tilesX = (fb.w + tileW - 1) / tileW, tilesY = (fb.h + tileH - 1) / tileH;
            if (ovTiles_.size() != static_cast<std::size_t>(tilesX * tilesY) || ovTileW_ != tileW) {
                freeOverlayTiles();
                ovTiles_.assign(static_cast<std::size_t>(tilesX * tilesY), OvTile{});
                ovTileW_ = tileW;
                std::fill(ovRowStale_.begin(), ovRowStale_.end(), ovAllBlocks_);   // nothing sent yet
            }
            const bool argb = path.client || gl1OverlayArgb();
            const GLenum upFormat = argb ? GL_BGRA : GL_RGBA;
            const GLenum upType = argb ? GL_UNSIGNED_INT_8_8_8_8_REV : GL_UNSIGNED_BYTE;
            // one row's columns x0..x1 into `o32`, as the blend below reads them
            const auto convert = [&](int y, int x0, int x1, std::uint32_t* o32) {
                const std::size_t yy = static_cast<std::size_t>(y);
                const std::uint16_t* src = fb.px.data() + yy * fb.w;
                const std::uint8_t* msk = mask && (!maskRow || maskRow[yy]) ? mask + yy * fb.w : nullptr;
                for (int xx = x0; xx < x1; ++xx) {
                    const std::uint16_t p = src[xx];
                    unsigned r = 0, g = 0, bl = 0, al = 0;     // the KEY: the world as it is
                    if (p != 0xF81F) {
                        const unsigned r5 = (p >> 11) & 31, g6 = (p >> 5) & 63, b5 = p & 31;
                        r = r5 << 3 | r5 >> 2; g = g6 << 2 | g6 >> 4; bl = b5 << 3 | b5 >> 2;
                        al = 255u - (msk ? msk[xx] : 0u);   // alpha = 1 - M (the blend below)
                    }
                    if (argb) {
                        o32[xx - x0] = al << 24 | r << 16 | g << 8 | bl;
                    } else {
                        auto* o = reinterpret_cast<unsigned char*>(o32 + (xx - x0));
                        o[0] = static_cast<unsigned char>(r); o[1] = static_cast<unsigned char>(g);
                        o[2] = static_cast<unsigned char>(bl); o[3] = static_cast<unsigned char>(al);
                    }
                }
            };
            {
                OMK_ZONE("gl1: overlay rows");      // the profiler (todo/debug-tools.md)
                bool storeWaited = false;    // the card done with the client memory, once a frame
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                for (int ty = 0; ty < tilesY; ++ty) {
                    const int y0 = ty * tileH, y1 = std::min(fb.h, y0 + tileH) - 1;
                    if (y1 < lo || y0 > hi) continue;          // nothing drawn this frame in its rows
                    for (int tx = 0; tx < tilesX; ++tx) {
                        const int x0 = tx * tileW, x1 = std::min(fb.w, x0 + tileW);
                        const int b0 = x0 / ovBlockW_, b1 = (x1 - 1) / ovBlockW_;
                        const std::uint32_t bits = ((b1 - b0 + 1 >= 32 ? ~0u : ((1u << (b1 - b0 + 1)) - 1u)) << b0);
                        bool drawn = false, stale = false;
                        for (int r = y0; r <= y1; ++r) {
                            drawn |= ovRowDraw_[static_cast<std::size_t>(r)] != 0;
                            stale |= (ovRowStale_[static_cast<std::size_t>(r)] & bits) != 0;
                        }
                        if (!drawn) continue;                   // stays stale until it is
                        OvTile& T = ovTiles_[static_cast<std::size_t>(ty * tilesX + tx)];
                        const bool fresh = !T.tex;
                        if (fresh) {
                            glGenTextures(1, &T.tex);
                            glBindTexture(GL_TEXTURE_2D, T.tex);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                            if (path.hint) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_SHARED_APPLE);
                            if (path.client) {
                                T.store.assign(static_cast<std::size_t>(texW) * tileH, 0u);
                                glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
                                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texW, tileH, 0, GL_BGRA,
                                             GL_UNSIGNED_INT_8_8_8_8_REV, T.store.data());
                                glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
                            } else {
                                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texW, tileH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                            }
                            gpuNote("overlay", omk::prof::kGlTexture, T.tex, 4LL * texW * tileH);
                        }
                        if (!stale && !fresh) continue;
                        const std::size_t need = static_cast<std::size_t>(texW) * tileH;
                        if (path.client) {
                            // the card may still be reading last frame's texels
                            if (!storeWaited) { OMK_ZONE("gl1: overlay wait"); glFinish(); storeWaited = true; }
                            for (int r = y0; r <= y1; ++r) {
                                std::uint32_t* row = T.store.data() + static_cast<std::size_t>(r - y0) * texW;
                                const std::uint32_t st = fresh ? bits : (ovRowStale_[static_cast<std::size_t>(r)] & bits);
                                for (int bk = b0; bk <= b1; ++bk) {
                                    if (!(st >> bk & 1u)) continue;
                                    const int cx0 = bk * ovBlockW_, cx1 = std::min(x1, cx0 + ovBlockW_);
                                    convert(r, cx0, cx1, row + (cx0 - x0));
                                }
                                ovRowStale_[static_cast<std::size_t>(r)] &= ~bits;
                            }
                            glBindTexture(GL_TEXTURE_2D, T.tex);
                            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texW, y1 - y0 + 1, upFormat, upType, T.store.data());
                        } else {
                            if (ovBand_.size() < need) ovBand_.resize(need);
                            for (int r = y0; r <= y1; ++r) {
                                convert(r, x0, x1, ovBand_.data() + static_cast<std::size_t>(r - y0) * texW);
                                ovRowStale_[static_cast<std::size_t>(r)] &= ~bits;
                            }
                            glBindTexture(GL_TEXTURE_2D, T.tex);
                            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texW, y1 - y0 + 1, upFormat, upType, ovBand_.data());
                        }
                        ++ovStatCalls_;
                        ovStatSent_ += static_cast<long>(x1 - x0) * (y1 - y0 + 1);
                    }
                }
            }
            OMK_ZONE("gl1: overlay draw");      // the quads, apart from the uploads
            glEnable(GL_TEXTURE_2D);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            // `C + world * M` as the STANDARD premultiplied "over" - alpha
            // stored as 1 - M and (GL_ONE, GL_ONE_MINUS_SRC_ALPHA) - the same
            // sum to the bit. It was (GL_ONE, GL_SRC_ALPHA), M as alpha: a
            // destination factor of SOURCE alpha, which period hardware need
            // not have (the white screens were the fog's colour sum, `end`).
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(1, 1, 1, 1);
            // ...ONE QUAD A RUN OF DRAWN ROWS of each tile: the rows
            // holding only the key (the world through) and the letterbox's
            // black are not drawn
            const float invW = 1.0f / static_cast<float>(texW), invH = 1.0f / static_cast<float>(tileH);
            for (int ty = 0; ty < tilesY; ++ty) {
                const int y0 = ty * tileH, y1 = std::min(fb.h, y0 + tileH) - 1;
                if (y1 < lo || y0 > hi) continue;
                for (int tx = 0; tx < tilesX; ++tx) {
                    const OvTile& T = ovTiles_[static_cast<std::size_t>(ty * tilesX + tx)];
                    if (!T.tex) continue;
                    const int x0 = tx * tileW, x1 = std::min(fb.w, x0 + tileW);
                    bool bound = false;
                    for (int y = y0; y <= y1;) {
                        if (!ovRowDraw_[static_cast<std::size_t>(y)]) { ++y; continue; }
                        int e = y;
                        while (e + 1 <= y1 && ovRowDraw_[static_cast<std::size_t>(e + 1)]) ++e;
                        if (!bound) { glBindTexture(GL_TEXTURE_2D, T.tex); glBegin(GL_QUADS); bound = true; }
                        const float t0 = static_cast<float>(y - y0) * invH, t1 = static_cast<float>(e + 1 - y0) * invH;
                        const float s1 = static_cast<float>(x1 - x0) * invW;
                        const float yTop = static_cast<float>(wh - y), yBot = static_cast<float>(wh - e - 1);
                        const float xl = static_cast<float>(x0) * ww / fb.w, xr = static_cast<float>(x1) * ww / fb.w;
                        glTexCoord2f(0, t0);  glVertex2f(xl, yTop);
                        glTexCoord2f(s1, t0); glVertex2f(xr, yTop);
                        glTexCoord2f(s1, t1); glVertex2f(xr, yBot);
                        glTexCoord2f(0, t1);  glVertex2f(xl, yBot);
                        y = e + 1;
                    }
                    if (bound) glEnd();
                }
            }
            glDisable(GL_TEXTURE_2D);
            boundTex_ = ~0u;
        }
        if (fading) {
            // the fade over everything, interface included, as the GLES pass
            glDisable(GL_TEXTURE_2D);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(fade[0], fade[1], fade[2], fade[3] > 1.0f ? 1.0f : fade[3]);
            glBegin(GL_QUADS);
            glVertex2f(0, static_cast<float>(wh));
            glVertex2f(static_cast<float>(ww), static_cast<float>(wh));
            glVertex2f(static_cast<float>(ww), 0);
            glVertex2f(0, 0);
            glEnd();
            glColor4f(1, 1, 1, 1);
        }
        if (hi >= lo || fading) glDisable(GL_BLEND);
        if (probe) probeOverlay(fb, mask, lo, hi, ww, wh);
        // WHAT THE OVERLAY COSTS IN ROWS, every 300 overlay frames: how many
        // are drawn and how many had to be SENT again (changed), a frame -
        // sent row by row, the iBook's fight HUD costs 74 ms a frame in
        // `gl1: overlay rows` (`todo/handoff-classic-mac.md` 3f)
        {
            ++ovStatFrames_;
            for (int y = lo; y <= hi; ++y) ovStatDrawn_ += ovRowDraw_[static_cast<std::size_t>(y)];
            if (ovStatFrames_ == 300) {
                std::printf("gl1 overlay: over 300 frames %.1f rows drawn and %.1f rows' worth "
                            "of texels sent a frame, in %.1f calls\n",
                            ovStatDrawn_ / 300.0, ovStatSent_ / (300.0 * w_), ovStatCalls_ / 300.0);
                ovStatFrames_ = 0; ovStatDrawn_ = ovStatSent_ = ovStatCalls_ = 0;
            }
        }
        // ANY GL ERROR the overlay raised, said once each: the iBook's log is
        // all that machine gives back (a refused upload, out of memory)
        for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) {
            static unsigned told = 0;
            const unsigned bit = e >= GL_INVALID_ENUM && e <= GL_OUT_OF_MEMORY ? 1u << (e - GL_INVALID_ENUM) : 64u;
            if (told & bit) continue;
            told |= bit;
            std::printf("gl1: the overlay raised GL error 0x%04X (rows %d..%d, %s upload)\n",
                        static_cast<unsigned>(e), lo, hi, gl1OverlayArgb() ? "ARGB8888" : "RGBA bytes");
        }
        fitToWindow(winW, winH);
        OMK_ZONE("gl1: swap");
        swapWindow();
        return true;
    }
    // THE WHITE-SCREEN PROBE (steps 2j/2k; kept until a run confirms 2k): what the overlay frame holds and what
    // the card made of it, BEFORE the swap - the band and its texels by kind,
    // whether the overlay texture and the world's are resident in video
    // memory (`glAreTexturesResident`, OpenGL 1.1), and three pixels of the
    // back buffer: the window's centre, a text texel and a key texel of the
    // band. White there means the card drew white; the picture there and a
    // white window means the white is not drawn by this context.
    void probeOverlay(const Surface& fb, const std::uint8_t* mask, int lo, int hi,
                      int ww, int wh) {
        long nKey = 0, nOpaque = 0, nPartial = 0, nAdd = 0;
        int tx = -1, ty = -1, kx = -1, ky = -1;
        for (int y = lo; y <= hi; ++y)
            for (int x = 0; x < fb.w; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * fb.w + static_cast<std::size_t>(x);
                const unsigned m = mask ? mask[i] : 0u;
                if (fb.px[i] == 0xF81F) {
                    ++nKey;
                    if (kx < 0 && y >= (lo + hi) / 2) { kx = x; ky = y; }
                } else if (m == 0) {
                    ++nOpaque;
                    if (tx < 0) { tx = x; ty = y; }
                } else if (m == 255) ++nAdd;
                else ++nPartial;
            }
        GLuint ovName = 0;                       // the first tile made, for its residency
        for (const OvTile& t : ovTiles_) if (t.tex) { ovName = t.tex; break; }
        GLboolean ovRes = GL_FALSE;
        if (ovName) glAreTexturesResident(1, &ovName, &ovRes);
        std::vector<GLuint> names;
        for (const auto& [key, u] : uploaded_) names.push_back(u.name);
        std::vector<GLboolean> res(names.size(), GL_FALSE);
        int resident = 0;
        if (!names.empty()) {
            if (glAreTexturesResident(static_cast<GLsizei>(names.size()), names.data(), res.data()))
                resident = static_cast<int>(names.size());
            else
                for (GLboolean b : res) resident += b ? 1 : 0;
        }
        // each pixel COPIED into a small texture and read from there - the
        // way `readWindow` reads, since `glReadPixels` from the window gives
        // zeros on the emulated Radeon (it works on the Rage 128)
        unsigned char c[3][4];
        std::memset(c, 0x55, sizeof c);     // a read that writes nothing shows as 85s
        if (!probeTex_) {
            glGenTextures(1, &probeTex_);
            glBindTexture(GL_TEXTURE_2D, probeTex_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        glBindTexture(GL_TEXTURE_2D, probeTex_);
        boundTex_ = ~0u;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        unsigned char texel[16 * 16 * 4];
        const auto readAt = [&](int x, int yTop, unsigned char* out) {
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, wh - 1 - yTop, 1, 1);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
            std::memcpy(out, texel, 4);
        };
        readAt(ww / 2, wh / 2, c[0]);
        if (tx >= 0) readAt(tx, ty, c[1]);
        if (kx >= 0) readAt(kx, ky, c[2]);
        std::printf("gl1 overlay probe: band rows %d..%d - %ld key, %ld opaque, %ld partial, "
                    "%ld additive texels; overlay texture %s, world textures resident %d of %d; "
                    "the back buffer before the swap - centre %u %u %u, text (%d,%d) %u %u %u, "
                    "key (%d,%d) %u %u %u\n",
                    lo, hi, nKey, nOpaque, nPartial, nAdd,
                    !ovName ? "none" : ovRes ? "RESIDENT" : "NOT resident", resident,
                    static_cast<int>(names.size()), c[0][0], c[0][1], c[0][2], tx, ty,
                    c[1][0], c[1][1], c[1][2], kx, ky, c[2][0], c[2][1], c[2][2]);
    }
#endif

    RasterStats stats() const override { return st_; }   // only `triangles` is known
    // BODIES AND MOVING MESHES POSED HERE (`renderer.h` `Draw::meshPose`,
    // `todo/handoff-classic-mac.md` 3f step 3a): a posed draw's corners are
    // its REST corners and one affine a mesh, and `submit` folds the camera
    // into each mesh's affine once a draw - so a corner is transformed ONCE,
    // rest to view, instead of the frontend's CPU path placing it into the
    // world (and patching the moving set meshes into the set's buffer) for
    // `submit` to transform again. That placement and patch alone cost the
    // iBook's street ~20 ms a frame. The lights that reach
    // a body come as `Draw::vertexLights` and are applied per corner here by
    // `vertexlight.cpp`'s law, as the GLES shader applies them.
    bool posesBodies() const override { return true; }
    // THE DEPTH TIE, SETTLED ONCE (`renderer.h` bakeDepthTie, todo/optimization.md
    // step 29), as Vulkan and GLES do: of two coincident faces the engine's
    // strict test on its quantised depth shows the one drawn FIRST, and the
    // loser is drawn a hair back. Without a tie the street's coincident ground
    // faces fight in the depth buffer and flicker as the camera moves.
    bool bakeDepthTie(const Geometry* g, std::span<const Draw> order) override {
        if (tieBakeOff() || !g) return false;
        const std::vector<std::size_t> losers = bakeTieLosers(*g, order);   // o3de/drawcore.h
        std::vector<std::uint8_t> lose(g->corners.size() / 3 + 1, 0);
        for (const std::size_t tri : losers) if (tri < lose.size()) lose[tri] = 1;
        tieBaked_[g] = std::move(lose);
        g->resident.mark();
        static long told = 0;
        if (told++ < 4)
            std::printf("gl1: depth tie baked - %zu loser triangles over %zu draws\n", losers.size(), order.size());
        return true;
    }
    // `--full-textures`: every texture at its own size whatever the card's
    // memory - the port's own (an enhancement); before the first pool
    void setFullTextures(bool full) { fullTextures_ = full; }
    void setFogUnit(bool unit) { fogUnit_ = unit; }
    void setNoLock(bool off) { noLock_ = off; }
    void setPerCorner(bool on) { perCorner_ = on || envFlag("OMK_GL1_PER_CORNER"); }
    void setCpuFog(bool cpu) { cardFog_ = !(cpu || envFlag("OMK_GL1_CPU_FOG")); }
    int maxVertexLights() const override { return kPoseLights; }
    const char* name() const override { return "OpenGL 1.x fixed-function"; }
    // the texture filter alone (`Renderer::takes`)
    unsigned takes() const override { return omk::kTakesTextureFilter; }
    // TEXTURE FILTERING (renderer.h): 0 nearest, 1 bilinear. No mip chain -
    // the original set MIP NONE on both arms - so trilinear is drawn bilinear
    // and said. Recorded here; applied to every texture `setTextures` made
    // and every one it makes after, so the order of the two calls is free.
    bool setTextureFilter(int mode) override {
        filter_ = mode >= 1 ? 1 : 0;
        if (mode >= 2)
            std::printf("gl1: trilinear asked - no mip chain in this backend, drawn bilinear\n");
        if (!uploaded_.empty()) {
            Current current(ctx_);
            flush();
            for (auto& [key, u] : uploaded_) {
                const GLuint id = u.name;
                glBindTexture(GL_TEXTURE_2D, id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_ ? GL_LINEAR : GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_ ? GL_LINEAR : GL_NEAREST);
            }
            boundTex_ = ~0u;
        }
        return mode <= 1;
    }

private:
#if defined(OMK_GL1_ON_WINDOW)
    // THE WINDOW'S BACK BUFFER, READ THROUGH A TEXTURE. `glReadPixels` from a
    // window surface returns zeros on Tiger's emulated Radeon 9700 while the
    // same frame draws correctly when swapped (2026-10-04, probed: a cleared
    // red read back black, and with the composite skipped the street showed
    // in the window); the copy into a texture and `glGetTexImage` go through
    // texture memory instead. Both calls are OpenGL 1.1, so Mac OS 9 has them.
    void readWindow() {
        readTexture();
        glBindTexture(GL_TEXTURE_2D, readTex_);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w_, h_);
        // (the CPU's copy only where a frame is read: the fit needs none)
        readBig_.resize(static_cast<std::size_t>(readTexW_) * static_cast<std::size_t>(readTexH_) * 4);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, readBig_.data());
        boundTex_ = ~0u;                     // the next draw binds its own
        // the texture's rows are the back buffer's, bottom-up, at its stride
        for (int y = 0; y < h_; ++y)
            std::memcpy(rgba_.data() + static_cast<std::size_t>(y) * w_ * 4,
                        readBig_.data() + static_cast<std::size_t>(y) * readTexW_ * 4,
                        static_cast<std::size_t>(w_) * 4);
    }
    // the texture the frame's corner is copied into - for the read above
    // and the fit to a larger window (`fitToWindow`) - made at first use
    void readTexture() {
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
        }
    }
    GLuint readTex_ = 0;
    GLsizei readTexW_ = 0, readTexH_ = 0;
    std::vector<unsigned char> readBig_;
    // the overlay's TILES (`presentOverlay`): a texture each, made on
    // first use, and with client storage its own memory
    struct OvTile { GLuint tex = 0; std::vector<std::uint32_t> store; };
    std::vector<OvTile> ovTiles_;
    int ovTileW_ = 0;
    void freeOverlayTiles() {
        for (OvTile& t : ovTiles_)
            if (t.tex) { gpuForget(omk::prof::kGlTexture, &t.tex, 1); glDeleteTextures(1, &t.tex); t.tex = 0; }
        ovTiles_.clear();
    }
    std::vector<std::uint32_t> ovBand_;  // a run of changed rows, converted
    GLuint probeTex_ = 0;                // the white-screen probe's pixel reads
#endif
    // THE ORIGINAL'S TEXTURE SIZE RULE (`o3de/texscale.h`), decided once,
    // before the first texture goes up, from the card's free texture memory
    // (`gl1TextureMemory`, AGL); where the host does not say, whole.
    bool fullTextures_ = false, scaleDecided_ = false;
    float texScaleW_ = 1.0f, texScaleH_ = 1.0f;
    void decideTextureScale() {
        if (scaleDecided_) return;
        scaleDecided_ = true;
        long freeMem = -1;
#if defined(OMK_GL1_ON_WINDOW)
        if (onWindow_) freeMem = gl1TextureMemory();
#endif
        freeMem = omk::envInt("OMK_TEXTURE_MEMORY", freeMem);
        const TextureScale sc = fullTextures_ ? TextureScale{} : originalTextureScale(freeMem);
        texScaleW_ = sc.w; texScaleH_ = sc.h;
        if (freeMem > 0 || fullTextures_)
            std::printf("gl1: texture size - %ld KB free for textures, the original's rule needs "
                        "(58 + 5) x 128 KB = %ld KB: textures at %gx%g of their size (a 256x256 page "
                        "drawn %dx%d)%s\n", freeMem / 1024, originalTextureNeed() / 1024, sc.w, sc.h,
                        static_cast<int>(256 * sc.w), static_cast<int>(256 * sc.h),
                        fullTextures_ ? " - --full-textures: kept whole" : "");
    }
    // the posed draw's per-mesh affines with the camera folded in (`submit`),
    // made on first use in a draw: `poseGen_[m] == poseDraw_` when made
    static constexpr int kPoseLights = 32;
    std::vector<float> poseView_;          // 12 a mesh: view = P * rest
    std::vector<std::uint32_t> poseGen_;
    std::uint32_t poseDraw_ = 0;
#if defined(OMK_GL1_ON_WINDOW)
    // each row as last SEEN (its 565 pixels, its mask, 0/1/2 never /
    // without / with a mask), whether it holds anything but the key, and
    // whether the texture row holds it
    std::vector<std::uint8_t> ovSeenRow_, ovRowAny_;
    std::vector<std::uint32_t> ovSum_;   // two checksums a block of a row
    int ovSumW_ = 0;
    std::vector<std::uint32_t> ovRowStale_;   // a row's blocks the texture does not hold yet
    int ovBlockW_ = 32, ovBlocks_ = 1;
    std::uint32_t ovAllBlocks_ = 1;
    std::vector<std::uint8_t> ovRowBlack_, ovRowDraw_;   // all 0x0000; drawn this frame
    long ovStatFrames_ = 0, ovStatDrawn_ = 0, ovStatSent_ = 0, ovStatCalls_ = 0;
#endif

    // Where the world is drawn and read from: the framebuffer object, or on
    // classic Mac OS the window's back buffer (the frontend presents after).
    void bindTarget() {
#if defined(OMK_GL1_ON_WINDOW)
        if (onWindow_) {
            glDrawBuffer(GL_BACK);
            glReadBuffer(GL_BACK);
        }
#endif
#if !defined(OMK_GL1_AGL)
        if (!onWindow_) glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo_);
#endif
    }

#if defined(OMK_GL1_AGL)
    // The window's context, made by the frontend (`gl1host.h`); nothing to
    // attach here, and no extension needed - the back buffer is the target.
    bool makeContext() {
        ctx_ = static_cast<GlContext>(gl1WindowContext());
        if (!ctx_) {
            std::fprintf(stderr, "gl1: no AGL context on the window\n");
            return false;
        }
        Current current(ctx_);
        const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        hasColourSum_ = ext && std::strstr(ext, "GL_EXT_secondary_color");
        // the driver's extensions, whole: what a fallback may lean on is read
        // off a real machine's log, not guessed (the iMac G3's OpenGL 1.1)
        std::printf("gl1: extensions: %s\n", ext ? ext : "(none)");
        if (!omkSecondaryColorPointer) omkSecondaryColorPointer = findSecondaryColorPointer();
        std::printf("gl1: EXT_secondary_color %s by the renderer, glSecondaryColorPointerEXT %s in OpenGLLibrary\n",
                    hasColourSum_ ? "offered" : "not offered", omkSecondaryColorPointer ? "found" : "NOT found");
        hasColourSum_ = hasColourSum_ && omkSecondaryColorPointer;
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
    // 10.3 onwards - and the FBO is drawn into all the same; where pbuffers are
    // gone or refused (deprecated since 10.7) the context goes on without.
    bool makeContext() {
#if defined(OMK_GL1_SDL_WINDOW)
        // THE SDL WINDOW'S CONTEXT, where the host made one
        // (`backends/gl1/gl1sdlhost.cpp`): the world is drawn into the
        // window's back buffer and swapped, as on AGL - no framebuffer
        // object, no pbuffer, and no readback but for a frame composed on the
        // CPU. A host without one (no GL window) falls through to the
        // offscreen target below.
        if (void* win = gl1WindowContext()) {
            ctx_ = static_cast<GlContext>(win);
            onWindow_ = true;
            Current current(ctx_);
            GLint vp[4] = {0, 0, 0, 0};
            glGetIntegerv(GL_VIEWPORT, vp);
            std::printf("gl1: %s, OpenGL %s, on the window's back buffer (%dx%d drawable)\n",
                        reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                        reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                        static_cast<int>(vp[2]), static_cast<int>(vp[3]));
            return true;
        }
#endif
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
        Current current(ctx_);
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
    static bool noCull() { static const bool n = omk::envFlag("OMK_NO_CULL"); return n; }

    // THE CAMERA, applied on the CPU as the original did: it transformed
    // every vertex itself and handed D3D screen positions (`D3DTLVERTEX`:
    // sx, sy, sz, rhw), on cards that mostly had no T&L. `cameraBasis` is the
    // reference's own basis (mirror, roll and flip folded in). Apart from
    // `begin` so the mirror's reflected pass can change it without a clear.
    void setCamera(const View& v) {
        view_ = v;
        RCamera cam = v.cam;
        cam.w = v.letterboxed() ? v.vw : w_;
        cam.h = v.letterboxed() ? v.vh : h_;
        flipX_ = cam.flipX;
        cameraBasis(cam, bs_, bu_, bf_, th_, tv_);
        for (int k = 0; k < 3; ++k) eye_[k] = cam.eye[k];
        camW_ = cam.w; camH_ = cam.h;
        ith_ = 1.0f / th_; itv_ = 1.0f / tv_;
        gx_ = th_ * 1.01f; gy_ = tv_ * 1.01f;   // the screen edges' hair of margin
        gxLen_ = std::sqrt(1.0f + gx_ * gx_); gyLen_ = std::sqrt(1.0f + gy_ * gy_);   // `sphereOutside`
        // mid-frame (the mirror's reflected pass and back): the pending batch
        // was projected by the matrix of the camera before
        if (cardFog_ && inFrame_) {
            Current current(ctx_);
            flush();
            loadProjection();
        }
    }

    // The reference's cull (`raster.cpp`: the screen area's sign, flipped
    // under the mirror) without its divisions. For corners in front of the
    // eye the projected area is -det[v0; v1; v2] / (z0 z1 z2) times positive
    // scales, so its sign is the determinant's, negated.
    bool backFacing(const Vtx& a, const Vtx& b, const Vtx& c) const {
        const float det = a.v[0] * (b.v[1] * c.v[2] - b.v[2] * c.v[1]) -
                          a.v[1] * (b.v[0] * c.v[2] - b.v[2] * c.v[0]) +
                          a.v[2] * (b.v[0] * c.v[1] - b.v[1] * c.v[0]);
        return (flipX_ ? det : -det) > 0.0f;
    }

    // One clip-space vertex into the pending batch: (x / th, y / tv,
    // z - 2 near, z) is NDC (the reference's screen mapping; depth
    // 1 - near / z, increasing away from the eye, the order of the
    // original's rhw tested GREATER) times w = z. The reference's fog:
    // f = (end - z) / (end - start) past the start, the colour lerped toward
    // the fog's by 1 - f.
    std::uint16_t emit(const Vtx& p) {
        const auto at = static_cast<std::uint16_t>(pending());   // < kBatchMax + 18 < 65536
        ++statSent_;
        OutV o;
        const float w = p.v[2];
        if (cardFog_) {
            // VIEW space, projected by `loadProjection`; a tie loser's depth
            // pushed back through q: clip z = z - 2 near q, so q = 1 -
            // (4 / 65535) z / (2 near) is the same push as below, with x, y
            // and w untouched
            o.p[0] = p.v[0]; o.p[1] = p.v[1]; o.p[2] = w;
            o.p[3] = tieBack_ ? 1.0f - (4.0f / 65535.0f) * w / (2.0f * kNearCut) : 1.0f;
        } else {
            o.p[0] = p.v[0] * ith_; o.p[1] = p.v[1] * itv_;
            o.p[2] = w - 2.0f * kNearCut; o.p[3] = w;
            // a tie loser two 16-bit depth steps back (GL's NDC z spans 2, so a
            // window step of 1/65535 is 2/65535 of it), as the Vulkan and GLES
            // shaders draw theirs
            if (tieBack_) o.p[2] += (4.0f / 65535.0f) * w;
        }
        float a = 0.0f;
        if (!cardFog_ && fogKind_ != 0 && w > fogStart_) {
            const float f = (fogEnd_ - w) * fogInv_;
            a = 1.0f - (f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f));
        }
        const float k = 1.0f - a;   // the fog's share added after: the colour sum's (1 - a)
        o.c[0] = toByte(p.c[0] * k); o.c[1] = toByte(p.c[1] * k);
        o.c[2] = toByte(p.c[2] * k); o.c[3] = 255;
        o.uv[0] = p.u; o.uv[1] = p.t;
        if (fogSum_) {
            o.f[0] = fogRGB_[0] * a; o.f[1] = fogRGB_[1] * a; o.f[2] = fogRGB_[2] * a;
            out_.push_back(o);
        } else if (fogMT_) {
            // the ramp's coordinate: texel i holds fog * i / 255, sampled
            // linearly at s * 256 - 0.5, so s = (255 a + 0.5) / 256 reads back
            // fog * a; t the 256x1 ramp's one row (two coordinates: an
            // OpenGL 1.1 driver is not trusted with a one-coordinate array)
            o.f[0] = (255.0f * a + 0.5f) * (1.0f / 256.0f);
            o.f[1] = 0.5f;
            out_.push_back(o);
        } else {
            // no secondary colour this pass: the 28-byte vertex, not the
            // 40-byte one - a third less written and read a corner
            outN_.push_back(OutN{{o.p[0], o.p[1], o.p[2], o.p[3]}, {o.c[0], o.c[1], o.c[2], o.c[3]},
                                 {o.uv[0], o.uv[1]}});
            if (fogPass_) {
                // the second pass's colour: the fog, with `a` as its alpha
                const unsigned char ab = toByte(a);
                fogCol_.push_back({fogByte_[0], fogByte_[1], fogByte_[2], ab});
                fogAny_ = fogAny_ || ab != 0;
            }
        }
        return at;
    }
    std::size_t pending() const { return out_.size() + outN_.size(); }

    // The pending batch drawn, with the device state it was gathered under.
    void flush() {
        if (!out_.empty()) {
            const GLsizei stride = static_cast<GLsizei>(sizeof(OutV));
            glVertexPointer(4, GL_FLOAT, stride, out_[0].p);
            glColorPointer(4, GL_UNSIGNED_BYTE, stride, out_[0].c);
            glTexCoordPointer(2, GL_FLOAT, stride, out_[0].uv);
            if (fogSum_) omkSecondaryColorPointer(3, GL_FLOAT, stride, out_[0].f);
            if (fogMT_) {
                omkClientActiveTexture_(kTexUnit1);
                glTexCoordPointer(2, GL_FLOAT, stride, out_[0].f);
                omkClientActiveTexture_(kTexUnit0);
            }
            OMK_ZONE("gl1: draw call");          // the driver's share (todo/debug-tools.md)
            drawLocked(out_.size());
            ++batches_;
            out_.clear();
        }
        if (!outN_.empty()) {
            const GLsizei stride = static_cast<GLsizei>(sizeof(OutN));
            glVertexPointer(4, GL_FLOAT, stride, outN_[0].p);
            glColorPointer(4, GL_UNSIGNED_BYTE, stride, outN_[0].c);
            glTexCoordPointer(2, GL_FLOAT, stride, outN_[0].uv);
            {
                OMK_ZONE("gl1: draw call");
                drawLocked(outN_.size());
            }
            ++batches_;
            if (fogPass_ && fogAny_) fogSecondPass();
            outN_.clear();
        }
        fogCol_.clear();
        fogAny_ = false;
        // the batch's vertices are gone: an index into them is no longer one
        idx_.clear();
        ++batchStamp_;
    }

    // THE BATCH DRAWN WITH ITS VERTICES LOCKED (EXT_compiled_vertex_array,
    // offered by the iMac G3's OpenGL 1.1): the batch's arrays are locked
    // just before its `glDrawElements` and unlocked just after, so a driver
    // that transforms on the CPU - the Rage 128 has no hardware T&L - may
    // transform each vertex ONCE however many triangles share it (a street
    // frame sends ~1.9 corners a vertex), where an unlocked indexed draw lets
    // it transform each corner as it comes. Nothing stays locked past the
    // draw: the next batch, the fog's second pass with its own colours, a
    // mesh moving between frames (the fans, the doors, the walkers) all hand
    // over fresh arrays. `--gl1-no-lock` / `OMK_GL1_NO_LOCK` draws unlocked,
    // the A/B (2026-11-01, `todo/handoff-classic-mac.md` 3i item 9).
    void drawLocked(std::size_t verts) {
        const bool lock = lockArrays() && !noLock_ && verts > 0;
        if (!lock) {
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(idx_.size()), GL_UNSIGNED_SHORT, idx_.data());
            return;
        }
        // its own zone: the profile's A/B reads which draws were locked
        // (`engine: profiler windows`), and the iMac what they cost
        OMK_ZONE("gl1: draw locked");
        omkLockArrays_(0, static_cast<GLsizei>(verts));
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(idx_.size()), GL_UNSIGNED_SHORT, idx_.data());
        omkUnlockArrays_();
        ++lockedDraws_;
    }
    // EXT_compiled_vertex_array usable: offered, both entry points found,
    // not refused by `--gl1-no-lock`. Asked once, with the context current.
    // `OMK_GL1_LOCK_UNLISTED` takes it as offered: the M1's OpenGL exports
    // the pair but leaves the extension out of its list, and the check
    // (`engine: gl1 backend`) needs the locked draw on the host.
    bool lockArrays() {
        if (!lockKnown_) {
            lockKnown_ = true;
            static const bool offEnv = envFlag("OMK_GL1_NO_LOCK");
            static const bool unlisted = envFlag("OMK_GL1_LOCK_UNLISTED");
            const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
            const bool offered = (ext && std::strstr(ext, "GL_EXT_compiled_vertex_array")) || unlisted;
            if (offered) {
                omkLockArrays_ = findLockArrays();
                omkUnlockArrays_ = findUnlockArrays();
            }
            // `noLock_` is asked per draw (the A/B flips it); OMK_GL1_NO_LOCK is for the run
            hasLock_ = offered && omkLockArrays_ && omkUnlockArrays_ && !offEnv;
            std::printf("gl1: EXT_compiled_vertex_array %s%s\n", offered ? "offered" : "not offered",
                        hasLock_ && !noLock_ ? " - each batch's vertices locked around its draw call"
                        : !offered ? "" : (noLock_ || offEnv) ? " - NOT used (--gl1-no-lock)"
                                                              : " - but its entry points are not found");
        }
        return hasLock_;
    }

    // ARB_multitexture usable: offered, and both selectors found. Asked once,
    // with the context current.
    bool multitexture() {
        if (!mtKnown_) {
            mtKnown_ = true;
            const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
            const bool offered = ext && std::strstr(ext, "GL_ARB_multitexture");
            // GL_ADD: EXT_ or ARB_texture_env_add, or OpenGL 1.3's core
            const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            const bool add = (ext && std::strstr(ext, "_texture_env_add")) ||
                             (ver && (ver[0] > '1' || (ver[0] == '1' && ver[1] == '.' && ver[2] >= '3')));
            if (offered) {
                omkActiveTexture_ = findActiveTexture(false);
                omkClientActiveTexture_ = findActiveTexture(true);
            }
            GLint units = 0;
            if (offered) glGetIntegerv(kMaxTextureUnits, &units);
            hasMT_ = offered && add && omkActiveTexture_ && omkClientActiveTexture_ && units >= 2;
            std::printf("gl1: ARB_multitexture %s (%d units), texture_env_add %s%s\n",
                        offered ? "offered" : "not offered", static_cast<int>(units), add ? "yes" : "no",
                        offered && add && !hasMT_ ? " - but its selectors are not found or one unit only" : "");
        }
        return hasMT_;
    }
    // The second unit: the fog's ramp (fog * i / 255, RGB - rebuilt when the
    // day/night clock moves the fog's colour) under GL_ADD, its coordinate
    // array on - or all of it off again, for whatever is drawn after the
    // world. The first unit stays the selected one.
    void fogMTUnitOn(bool on) {
        omkActiveTexture_(kTexUnit1);
        if (on) {
            const unsigned char want[3] = {toByte(fogRGB_[0]), toByte(fogRGB_[1]), toByte(fogRGB_[2])};
            const bool fresh = !fogRamp_;
            if (fresh) {
                glGenTextures(1, &fogRamp_);
                glBindTexture(GL_TEXTURE_2D, fogRamp_);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            } else {
                glBindTexture(GL_TEXTURE_2D, fogRamp_);
            }
            if (fresh || std::memcmp(want, fogRampRGB_, 3) != 0) {
                unsigned char ramp[256 * 3];
                for (int i = 0; i < 256; ++i)
                    for (int c = 0; c < 3; ++c) ramp[i * 3 + c] = static_cast<unsigned char>((want[c] * i + 127) / 255);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                if (fresh) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, ramp);
                else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_BYTE, ramp);
                std::memcpy(fogRampRGB_, want, 3);
            }
            glEnable(GL_TEXTURE_2D);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, kTexEnvAdd);
            glMatrixMode(GL_TEXTURE);
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        omkActiveTexture_(kTexUnit0);
        omkClientActiveTexture_(kTexUnit1);
        if (on) glEnableClientState(GL_TEXTURE_COORD_ARRAY); else glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        omkClientActiveTexture_(kTexUnit0);
    }

    // THE FOG'S SECOND PASS, where there is no colour sum (`fogPass_`): the
    // batch drawn again over itself, adding fog * a. The first pass drew
    // texel * colour * (1 - a), so the sum is the reference's
    // lerp(texel * colour, fog, a) - all OpenGL 1.1:
    //   * depth GL_EQUAL, no depth write: the same vertices, so exactly the
    //     fragments the first pass left in front;
    //   * blended GL_SRC_ALPHA, GL_ONE with the vertex colour (fog, a);
    //   * a CUTOUT batch keeps its texture under GL_BLEND with the fog as the
    //     env colour - C = fog (1 - Ct) + fog Ct = fog, A = a * At - so a
    //     keyed-out texel (alpha 0) adds nothing, as it drew nothing; an
    //     opaque batch is drawn untextured (its black texels carry alpha 0 in
    //     the 1555 page and would refuse the fog), C = fog, A = a.
    // Only opaque and cutout batches: a translucent or additive one keeps
    // the black fog, its colour sum not reproducible in one blend.
    void fogSecondPass() {
        const BlendRule r = blendRule(blend_);
        if (r.blend || !r.depthWrite) return;
        const bool textured = cutout_ && boundTex_ != ~1u && boundTex_ != ~0u;
        glColorPointer(4, GL_UNSIGNED_BYTE, 4, fogCol_.data());
        glDepthFunc(GL_EQUAL);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        if (cutout_) glDisable(GL_ALPHA_TEST);
        if (textured) {
            const GLfloat env[4] = {fogRGB_[0], fogRGB_[1], fogRGB_[2], 1.0f};
            glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, env);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
        } else if (boundTex_ != ~1u) {
            glDisable(GL_TEXTURE_2D);
        }
        {
            OMK_ZONE("gl1: draw call");
            drawLocked(outN_.size());
        }
        ++batches_;
        ++fogPasses_;
        // back to the batch's state, as `setState` left it
        if (textured) glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        else if (boundTex_ != ~1u) glEnable(GL_TEXTURE_2D);
        if (cutout_) glEnable(GL_ALPHA_TEST);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }

    // the fog's two exclusions (View::fog), as `Raster_DrawTriangles` keys
    // them: 0x2080 (near, transparent) none, 0x800 start and end doubled
    int fogKindFor(const Draw& d) const {
        return !view_.fog || view_.fogEnd <= view_.fogStart ? 0
             : (d.bucketKey & 0x2080) ? 0 : (d.bucketKey & 0x800) ? 2 : 1;
    }
    // ...as the card's fog state, set only when it changes (the batch it
    // belongs to was flushed by `submit` first)
    void applyFog(int kind) {
        if (kind == glFogState_) return;
        if (kind == 0) {
            glDisable(GL_FOG);
        } else {
            glFogf(GL_FOG_START, fogStart_);
            glFogf(GL_FOG_END, fogEnd_);
            glEnable(GL_FOG);
        }
        glFogState_ = kind;
        ++fogChanges_;
    }
    // THE PROJECTION, with the fog on the card: view-space (x, y, z, q) to
    // the clip coordinates `emit` writes on the CPU otherwise - (x / th,
    // y / tv, z - 2 near q, z). q is 1 but for a tie loser's corner (`emit`).
    // Column-major for `glLoadMatrixf`. Identity with the fog on the CPU.
    void loadProjection() {
        glMatrixMode(GL_PROJECTION);
        if (!cardFog_) {
            glLoadIdentity();
        } else {
            GLfloat m[16] = {};
            m[0] = ith_;                  // row 0: x / th
            m[5] = itv_;                  // row 1: y / tv
            m[10] = 1.0f;                 // row 2: z ...
            m[14] = -2.0f * kNearCut;     //        ... - 2 near q
            m[11] = 1.0f;                 // row 3: w = z
            glLoadMatrixf(m);
        }
        glMatrixMode(GL_MODELVIEW);
    }

    static unsigned char toByte(float v) {
        // the corner colour is 0..1; the reference clamps after the multiply,
        // fixed function clamps the vertex colour first - equal for 0..1.
        // Converted FIRST and clamped as an integer (for the G3,
        // whose float compares and int compares are separate units): the
        // same byte, since truncation toward zero sends (-1, 0] to 0 as the
        // float clamp did and the rest clamps the same on either side
        const float x = v * 255.0f + 0.5f;
        if (!(x > -1.0f)) return 0;                    // (and a NaN: 0)
        if (x >= 255.0f) return 255;
        const int i = static_cast<int>(x);
        return static_cast<unsigned char>(i < 0 ? 0 : i);
    }

    // The D3D render states the original changed between buckets, changed
    // here only when they differ from the last draw's.
    void setState(const Draw& d) {
        const std::size_t slot = drawTextureSlot(d);   // ASSETS 4b; past 64 the pool's index
        const bool hasTex = slot < tex_.size() && tex_[slot] != 0;
        if (!stateValid_ || d.blend != blend_) {
            const BlendRule r = blendRule(d.blend);          // o3de/drawcore.h
            if (r.blend) {
                glEnable(GL_BLEND);
                glBlendFunc(glFactor(r.src), glFactor(r.dst));
            } else {
                glDisable(GL_BLEND);
            }
            glDepthMask(r.depthWrite ? GL_TRUE : GL_FALSE);
            blend_ = d.blend;
        }
        if (!stateValid_ || d.cutout != cutout_) {
            if (d.cutout) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
            cutout_ = d.cutout;
        }
        const int fogKind = fogKindFor(d);
        fogKind_ = fogKind;
        const float fk = fogKind == 2 ? 2.0f : 1.0f;
        fogStart_ = view_.fogStart * fk;
        fogEnd_ = view_.fogEnd * fk;
        fogInv_ = fogKind != 0 ? 1.0f / (fogEnd_ - fogStart_) : 0.0f;
        if (cardFog_) applyFog(fogKind);
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
#if defined(OMK_GL1_AGL)
    static constexpr bool onWindow_ = true;    // the AGL context is always the window's
#else
    bool onWindow_ = false;  // the SDL window's context (`OMK_GL1_SDL_WINDOW`), else offscreen
    CGLPBufferObj pbuf_ = nullptr;
#endif
    GLuint fbo_ = 0, colour_ = 0, depth_ = 0;
    int w_ = 0, h_ = 0;
    int vy_ = 0;             // the picture's first row in the target (on a window: its letterbox place)
    std::vector<GLuint> tex_;            // per pool slot: a name `uploaded_` owns
    std::vector<float> texW_, texH_;
    // the textures on the card, by their pixel storage (`setTextures`); the
    // entry holds that storage so its address is not reused while cached
    struct Uploaded { GLuint name; PixelBuffer idx, pal; float w, h; bool used; };
    std::map<std::tuple<const std::uint8_t*, const std::uint8_t*, int, int>, Uploaded> uploaded_;
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
    bool fogPass_ = false;       // no colour sum: the fog added in a second pass (`fogSecondPass`)
    bool fogMT_ = false;         // no colour sum: the fog on the second texture unit (`fogMTUnitOn`)
    bool cardFog_ = true;        // the fog on the card (`loadProjection`, `applyFog`); `--gl1-cpu-fog` false
    int glFogState_ = -1;
    bool inFrame_ = false;       // between `begin`'s setup and `end`: the projection is live        // the card's fog as last set: -1 unknown, 0 off, 1 / 2 the fog kind
    long fogChanges_ = 0;        // fog state changes, for the 60-pass line
    bool fogUnit_ = false;       // `--gl1-fog-unit`: the second unit, where it is there, not the second pass
    bool mtKnown_ = false, hasMT_ = false;
    bool lockKnown_ = false, hasLock_ = false, noLock_ = false;   // `drawLocked`
    bool abLock_ = false, abFog_ = false;   // the profiler's A/B: flipped now (`abFlip`)
    LockArraysFn omkLockArrays_ = nullptr;
    UnlockArraysFn omkUnlockArrays_ = nullptr;
    long lockedDraws_ = 0;
    ActiveTextureFn omkActiveTexture_ = nullptr, omkClientActiveTexture_ = nullptr;
    GLuint fogRamp_ = 0;
    unsigned char fogRampRGB_[3]{};                                   // the fog the ramp holds
    static constexpr GLenum kTexEnvAdd = 0x0104;                      // GL_ADD (EXT_texture_env_add, 1.3)
    static constexpr GLenum kTexUnit0 = 0x84C0, kTexUnit1 = 0x84C1;   // GL_TEXTURE0/1(_ARB)
    static constexpr GLenum kMaxTextureUnits = 0x84E2;                // GL_MAX_TEXTURE_UNITS(_ARB)
    unsigned char fogByte_[3]{};
    struct FogCol { unsigned char c[4]; };
    std::vector<FogCol> fogCol_;   // per pending vertex, beside `outN_`: (fog, a)
    bool fogAny_ = false;          // a vertex of the pending batch has a > 0
    long fogPasses_ = 0;
    bool hasColourSum_ = true;   // EXT_secondary_color, asked on AGL (makeContext)
    bool packed1555_ = true;     // the driver takes OpenGL 1.2's packed 1555 texels...
    bool packedKnown_ = false;   // ...as the first texture upload found
    std::vector<std::uint8_t> bytes_;   // a texture widened to RGBA bytes, when it does not
    unsigned boundTex_ = ~0u;
    // THE PENDING BATCH, interleaved, kept between frames for its capacity
    // (no allocation per frame); `f` is the fog's secondary colour, read only
    // when `fogSum_`. Bounded so a frame's largest run stays a few MB.
    // THE VERTEX INDEX: a geometry's corner -> its vertex, built
    // once per geometry and size (equal bytes within one mesh, as the 3DS
    // backend's `VertexIndex`); the per-vertex cache below is the current
    // draw's (`drawStamp_`) and, for the sent index, the current batch's
    // ...and WHICH CORNERS MAY HAVE CHANGED since it was built: a
    // byte a corner, set from the geometry's dirty lists as its revisions
    // pass (`Geometry::dirtyCorners`), or `allDoubt` when a revision came
    // without one. Two corners of one vertex that are both still as built
    // are equal by construction, so `submit` compares their 48 bytes only
    // when one of them is in doubt - comparing always would read every
    // shared corner's bytes on the G3, ~11k a street frame, for a word the
    // index already gives.
    struct VIndex {
        std::size_t n = 0; std::vector<std::uint32_t> vert; std::uint32_t nv = 0;
        std::uint64_t rev = 0; std::vector<std::uint8_t> doubt; bool allDoubt = false;
    };
    std::map<const Geometry*, VIndex> vindex_;
    // ONE RECORD A VERTEX: the six fields a lookup reads, in one 32-byte
    // line - as six arrays they are six cache misses a corner on the G3,
    // and the cache then costs what the transforms save
    struct VSlot {
        std::uint32_t stamp, rep, outStamp;
        float x, y, z;
        std::uint16_t out;
        std::uint8_t oc, pad[5];
    };
    std::vector<VSlot> vs_;
    std::vector<std::uint16_t> idx_;
    std::uint32_t drawStamp_ = 0, batchStamp_ = 1;
    long statCorners_ = 0, statTransformed_ = 0, statSent_ = 0, statSkipped_ = 0, statTieBack_ = 0;
    long statTrusted_ = 0;   // shared corners taken on the index's word, no compare
    // THE RUNS' BOUNDING SPHERES: a geometry's corners in runs of
    // `kClusterCorners` from corner 0 (4 triangles; a draw starts on a
    // triangle, so a run boundary is one too), a sphere (x, y, z, r) each,
    // kept per geometry and REVISION - the corners `dirtyCorners` lists
    // re-fit only their runs, anything else a rebuild. Only geometries large
    // enough to pay for it.
    // 4 triangles a run: measured over 60 street passes on the host, runs of
    // 32 triangles skipped 95040 corners, 16 185436, 8 248580, 4 298980 (a
    // fifth of the corners read, ~190 KB of spheres a set)
    static constexpr std::size_t kClusterCorners = 12, kClusterMinCorners = 4096;
    struct Clusters { std::size_t n = 0; std::uint64_t rev = 0; std::vector<float> sph; };
    std::map<const Geometry*, Clusters> clusters_;
    std::map<const Geometry*, std::vector<std::uint8_t>> tieBaked_;   // a geometry's tie losers, a byte a triangle
    bool tieBack_ = false;   // the triangle being emitted is a tie loser
    void fitCluster(const Geometry& g, std::size_t c, float* sp) const {
        const std::size_t a = c * kClusterCorners, b = std::min(g.corners.size(), a + kClusterCorners);
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (std::size_t i = a; i < b; ++i) {
            const Corner& k = g.corners[i];
            lo[0] = std::min(lo[0], k.x); hi[0] = std::max(hi[0], k.x);
            lo[1] = std::min(lo[1], k.y); hi[1] = std::max(hi[1], k.y);
            lo[2] = std::min(lo[2], k.z); hi[2] = std::max(hi[2], k.z);
        }
        const float cx = 0.5f * (lo[0] + hi[0]), cy = 0.5f * (lo[1] + hi[1]), cz = 0.5f * (lo[2] + hi[2]);
        float r2 = 0.0f;
        for (std::size_t i = a; i < b; ++i) {
            const Corner& k = g.corners[i];
            const float dx = k.x - cx, dy = k.y - cy, dz = k.z - cz;
            r2 = std::max(r2, dx * dx + dy * dy + dz * dz);
        }
        // a margin, so rounding in the per-corner transform can never put a
        // corner the sphere calls outside on the inside
        sp[0] = cx; sp[1] = cy; sp[2] = cz; sp[3] = std::sqrt(r2) * 1.001f + 1.0f;
    }
    const Clusters* clustersFor(const Geometry& g) {
        const std::size_t n = g.corners.size();
        if (n < kClusterMinCorners) return nullptr;
        Clusters& c = clusters_[&g];
        g.resident.mark();
        const std::size_t runs = (n + kClusterCorners - 1) / kClusterCorners;
        if (c.n == n && c.rev == g.revision) return &c;
        if (c.n == n && g.dirtyTo == g.revision && c.rev == g.dirtyFrom) {
            // only the runs the listed corners lie in (the list is not sorted)
            std::size_t last = ~std::size_t{0};
            for (const std::uint32_t k : g.dirtyCorners) {
                const std::size_t r = k / kClusterCorners;
                if (r == last || r >= runs) continue;
                last = r;
                fitCluster(g, r, c.sph.data() + 4 * r);
            }
        } else {
            c.n = n;
            c.sph.assign(4 * runs, 0.0f);
            for (std::size_t r = 0; r < runs; ++r) fitCluster(g, r, c.sph.data() + 4 * r);
        }
        c.rev = g.revision;
        return &c;
    }
    // the sphere wholly beyond the near plane or one screen edge, in the terms
    // the corners' outcodes use (`submit`): behind when z + r <= near, beyond
    // an edge when the edge's function plus r times its normal's length < 0
    bool sphereOutside(const float* sp) const {
        const float dx = sp[0] - eye_[0], dy = sp[1] - eye_[1], dz = sp[2] - eye_[2];
        const float x = dx * bs_[0] + dy * bs_[1] + dz * bs_[2];
        const float y = dx * bu_[0] + dy * bu_[1] + dz * bu_[2];
        const float z = dx * bf_[0] + dy * bf_[1] + dz * bf_[2];
        const float r = sp[3];
        if (z + r <= kNearCut) return true;
        const float lx = r * gxLen_, ly = r * gyLen_;
        return z * gx_ - x + lx < 0.0f || z * gx_ + x + lx < 0.0f ||
               z * gy_ - y + ly < 0.0f || z * gy_ + y + ly < 0.0f;
    }
    static constexpr std::size_t kIndexMinCorners = 64;
    bool sameCorner(const Geometry& g, std::size_t a, std::size_t b) const {
        if (std::memcmp(&g.corners[a], &g.corners[b], sizeof(Corner)) != 0) return false;
        return g.cornerMesh.size() != g.corners.size() || g.cornerMesh[a] == g.cornerMesh[b];
    }
    // A geometry's corners numbered by VERTEX: equal bytes within one mesh are
    // one vertex, numbered in order of first use - so a corner whose number is
    // new is that vertex's first. -> how many vertices. (`indexFor`,
    // `buildShape`)
    std::uint32_t vertexIds(const Geometry& g, std::vector<std::uint32_t>& vert) const {
        const std::size_t n = g.corners.size();
        vert.assign(n, 0);
        std::uint32_t nv = 0;
        const bool meshes = g.cornerMesh.size() == n;
        std::size_t cap = 64;
        while (cap < 2 * n) cap <<= 1;
        std::vector<std::int32_t> table(cap, -1);
        for (std::size_t k = 0; k < n; ++k) {
            std::uint32_t w[sizeof(Corner) / 4];
            std::memcpy(w, &g.corners[k], sizeof w);
            std::uint32_t h = 2166136261u ^ static_cast<std::uint32_t>(meshes ? g.cornerMesh[k] : 0);
            for (const std::uint32_t u : w) h = (h ^ u) * 16777619u;
            std::size_t sl = (h ^ (h >> 15)) & (cap - 1);
            for (;;) {
                const std::int32_t e = table[sl];
                if (e < 0) {
                    table[sl] = static_cast<std::int32_t>(k);
                    vert[k] = nv++;
                    break;
                }
                if (sameCorner(g, static_cast<std::size_t>(e), k)) {
                    vert[k] = vert[static_cast<std::size_t>(e)];
                    break;
                }
                sl = (sl + 1) & (cap - 1);
            }
        }
        return nv;
    }
    const VIndex* indexFor(const Geometry& g) {
        const std::size_t n = g.corners.size();
        if (n < kIndexMinCorners) return nullptr;
        auto it = vindex_.find(&g);
        if (it != vindex_.end() && it->second.n == n) {
            VIndex& x = it->second;
            if (x.rev != g.revision) {
                if (!x.allDoubt && g.dirtyTo == g.revision && g.dirtyFrom == x.rev) {
                    for (const std::uint32_t k : g.dirtyCorners)
                        if (k < n) x.doubt[k] = 1;
                } else {
                    // a change it cannot account for: compare always (a
                    // large geometry says so, once - the street's set has
                    // had none in a 600-frame walk)
                    if (!x.allDoubt && n >= 20000)
                        std::printf("gl1: a geometry of %zu corners changed without a dirty list - "
                                    "its shared corners compared from here on\n", n);
                    x.allDoubt = true;
                }
                x.rev = g.revision;
            }
            return &x;
        }
        if (it == vindex_.end() && vindex_.size() >= 256) vindex_.clear();   // bounded: transient geometries
        VIndex& x = vindex_[&g];
        g.resident.mark();
        x.n = n;
        x.rev = g.revision;
        x.doubt.assign(n, 0);
        x.allDoubt = false;
        x.nv = vertexIds(g, x.vert);
        if (vs_.size() < x.nv) vs_.resize(x.nv, VSlot{});
        if (n >= 20000)
            std::printf("gl1: a geometry of %zu corners indexed - %u vertices\n", n, x.nv);
        return &x;
    }
    // THE ORIGINAL'S SHAPE OF A GEOMETRY (`submitShape`): its distinct
    // vertices - the position, the texel and the baked colour as the bytes it
    // is sent as, 24 bytes, the original's own source vertex being 32
    // (`docs/ASSETS.md`) - and a vertex number a corner, 16 bits where the
    // geometry has fewer than 65536 vertices (`vert16`; Anekbah's set has
    // 63079), 32 otherwise (`vert`). `c[3]` is 255, or 0
    // for a vertex that SHIMMERS (`kFlicker`), whose colour is computed per
    // frame from its corner. `own[r]` is the first vertex the run of corners
    // r introduces (`kClusterCorners` a run), so run r's new vertices are
    // `own[r]` to `own[r + 1]`. `users`, how many corners a vertex has (255:
    // that many or more), checks a dirty list (`patchShape`).
    // Kept per geometry and revision; Anekbah's set (139245 corners, 63079
    // vertices): 1.8 MB here and 1.5 MB of transient records (`VX`, shared),
    // against the corner-by-corner submit's 0.7 MB index and 2.0 MB of cache
    // slots - 0.6 MB more, measured 2026-11-06.
    struct SVert { float x, y, z, u, v; unsigned char c[4]; };
    struct Shape {
        std::size_t n = 0; std::uint64_t rev = 0; std::uint32_t nv = 0;
        std::vector<SVert> vtx;
        std::vector<std::uint32_t> vert, own;
        std::vector<std::uint16_t> vert16;
        std::vector<std::uint8_t> users;
        long builtPass = 0;
        int quick = 0;            // rebuilt within a few passes of the last build, in a row
        bool perCorner = false;   // it changes every frame without a dirty list: corner by corner
    };
    std::map<const Geometry*, Shape> shapes_;
    // a vertex's TRANSFORMED record, this draw's (`stamp`): view space, the
    // outcode, and its index in the batch once sent (`sent` the batch's
    // stamp). One array for every shape - a draw is one geometry.
    struct VX {
        float x, y, z;
        std::uint32_t stamp, sent;
        std::uint16_t out;
        std::uint8_t oc, pad;
    };
    std::vector<VX> vx_;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> patchScratch_;   // (vertex, corner)
    bool perCorner_ = false, abShape_ = false;   // `--gl1-per-corner`; the profiler's A/B
    long statShapeDraws_ = 0, statCornerDraws_ = 0;
    long statPatched_ = 0, statRebuilt_ = 0;   // vertices a dirty list re-took; shapes built again

    static SVert shapeVertex(const Corner& c) {
        SVert s;
        s.x = c.x; s.y = c.y; s.z = c.z; s.u = c.u; s.v = c.v;
        // the bytes `emit` makes of an unfogged, unshimmering colour
        s.c[0] = toByte(c.r); s.c[1] = toByte(c.g); s.c[2] = toByte(c.b);
        s.c[3] = c.phase < 0.0f ? 255 : 0;
        return s;
    }
    void buildShape(const Geometry& g, Shape& s) {
        const std::size_t n = g.corners.size();
        if (s.n != 0) s.quick = passes_ - s.builtPass < 3 ? s.quick + 1 : 0;
        s.n = n;
        s.rev = g.revision;
        s.builtPass = passes_;
        s.nv = vertexIds(g, s.vert);
        s.vert16.clear();
        s.vtx.resize(s.nv);
        s.users.assign(s.nv, 0);
        const std::size_t runs = (n + kClusterCorners - 1) / kClusterCorners;
        s.own.assign(runs + 1, 0);
        std::uint32_t next = 0;
        for (std::size_t k = 0; k < n; ++k) {
            if (k % kClusterCorners == 0) s.own[k / kClusterCorners] = next;
            const std::uint32_t v = s.vert[k];
            if (v == next) { s.vtx[v] = shapeVertex(g.corners[k]); ++next; }   // its first use
            if (s.users[v] < 255) ++s.users[v];
        }
        s.own[runs] = next;
        if (s.nv <= 0x10000u) {
            s.vert16.assign(s.vert.begin(), s.vert.end());
            s.vert = std::vector<std::uint32_t>();     // released, not only emptied
        }
        if (n >= 20000)
            std::printf("gl1: a geometry of %zu corners in the original's shape - %u vertices, %zu KB\n", n,
                        s.nv, (s.vtx.size() * sizeof(SVert) + s.vert.size() * 4 + s.vert16.size() * 2 +
                               s.own.size() * 4 + s.users.size()) / 1024);
    }
    // The corners a dirty list names (`Geometry::dirtyCorners`) into their
    // vertices' records: only when EVERY corner of such a vertex is listed
    // and they are still equal - a mesh moved whole, a colour re-floored by
    // the clock - and the record then takes their bytes. Anything else
    // (a vertex split by the change) -> false, and the shape is built again.
    bool patchShape(const Geometry& g, Shape& s) {
        auto& ks = patchScratch_;
        ks.clear();
        for (const std::uint32_t k : g.dirtyCorners)
            if (k < s.n) ks.emplace_back(s.vert16.empty() ? s.vert[k] : s.vert16[k], k);
        std::sort(ks.begin(), ks.end());
        ks.erase(std::unique(ks.begin(), ks.end()), ks.end());   // the list may repeat a corner
        for (std::size_t i = 0; i < ks.size();) {
            const std::uint32_t v = ks[i].first;
            std::size_t j = i;
            while (j < ks.size() && ks[j].first == v) ++j;
            if (s.users[v] == 255 || j - i != s.users[v]) return false;
            const std::size_t k0 = ks[i].second;
            for (std::size_t m = i + 1; m < j; ++m)
                if (!sameCorner(g, k0, ks[m].second)) return false;
            s.vtx[v] = shapeVertex(g.corners[k0]);
            ++statPatched_;
            i = j;
        }
        return true;
    }
    // -> the geometry's shape, made or brought up to its revision; nullptr
    // for one drawn corner by corner: too small, or changing every frame
    // without a dirty list (three rebuilds in a row, each within three
    // passes of the last - a particle cloud), from then on
    Shape* shapeFor(const Geometry& g) {
        const std::size_t n = g.corners.size();
        if (n < kIndexMinCorners) return nullptr;
        auto it = shapes_.find(&g);
        if (it == shapes_.end()) {
            if (shapes_.size() >= 256) shapes_.clear();   // bounded: transient geometries
            Shape& s = shapes_[&g];
            g.resident.mark();
            buildShape(g, s);
            return &s;
        }
        Shape& s = it->second;
        if (s.perCorner) return nullptr;
        if (s.n == n && s.rev == g.revision) return &s;
        if (s.n == n && g.dirtyTo == g.revision && g.dirtyFrom == s.rev && patchShape(g, s)) {
            s.rev = g.revision;
            return &s;
        }
        if (n >= 20000)
            std::printf("gl1: a geometry of %zu corners changed beyond its dirty list - its shape built again\n", n);
        ++statRebuilt_;
        buildShape(g, s);
        if (s.quick >= 3) {
            s = Shape{};
            s.perCorner = true;
            return nullptr;
        }
        return &s;
    }

    // A TRIANGLE CROSSING THE NEAR PLANE OR A SCREEN EDGE, cut on the CPU and
    // sent as a fan - its corners' positions (view space) and colours given.
    // Shared by the corner-by-corner submit and `submitShape`.
    void emitClipped(const Vtx (&in)[3], bool cullThis) {
        const float gx = gx_, gy = gy_;
        Vtx poly[4];
        int np = 0;
        for (int k = 0; k < 3 && np < 4; ++k) {
            const Vtx& a = in[k];
            const Vtx& b = in[(k + 1) % 3];
            const bool ina = a.v[2] > kNearCut, inb = b.v[2] > kNearCut;
            if (ina) poly[np++] = a;
            if (ina != inb && np < 4) poly[np++] = lerp(a, b, (kNearCut - a.v[2]) / (b.v[2] - a.v[2]));
        }
        if (np < 3) { ++st_.behind; tieBack_ = false; return; }
        // the single-sided cull on the near-clipped triangle - the
        // reference's own test, before anything else is cut away
        if (cullThis && backFacing(poly[0], poly[1], poly[2])) { ++st_.culled; tieBack_ = false; return; }
        // ...then THE SCREEN EDGES, on the CPU too. A wall beside the eye
        // projects thousands of pixels off-screen once near-clipped, and
        // Tiger's emulated Radeon rasterised such a triangle unclipped -
        // its texture coordinates collapsed and the wall drew flat. The
        // original clipped its transformed triangles itself as well. A
        // hair of margin keeps the edge pixels covered.
        Vtx buf[2][8];
        int nb = np;
        for (int k = 0; k < np; ++k) buf[0][k] = poly[k];
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
        if (nb < 3) { ++st_.offscreen; tieBack_ = false; return; }
        const Vtx* out = buf[cur];
        ++st_.drawn;
        for (int k = 1; k + 1 < nb; ++k) {            // a fan: up to 6 triangles after the clips
            idx_.push_back(emit(out[0])); idx_.push_back(emit(out[k])); idx_.push_back(emit(out[k + 1]));
        }
        tieBack_ = false;
    }

    // THE SUBMIT IN THE ORIGINAL'S SHAPE (`todo/handoff-classic-mac.md` 3i
    // item 11, suggestion 2; done 2026-11-06). The original drew a node in two
    // passes (`docs/ASSETS.md`, "The two vertex records"): `sub_4947F0`
    // transformed the node's 32-byte source vertices ONCE, IN ORDER, into
    // the frame's pool, and `Render_SubmitMesh` then walked the faces, which
    // INDEX that pool, and tested each on the transformed values. The
    // corner-by-corner submit above reaches the same picture from the flat
    // corners: each corner's 48 bytes looked up through a hash index, a
    // cache slot with its stamps, and "doubt" bytes for corners that moved.
    // Here a geometry is held as `Shape` - its distinct vertices (24 bytes
    // each, the position, texel and baked colour) and a vertex number a
    // corner - and a draw goes:
    //   1. a RUN of `kClusterCorners` corners (4 triangles) not wholly off the
    //      screen (`sphereOutside`, as before) first TRANSFORMS THE VERTICES
    //      IT INTRODUCES: numbered in order of first use, a run's new
    //      vertices are one contiguous block, transformed in a plain loop -
    //      `sub_4947F0`'s pass, by run where the original went by node,
    //      because the port's set is one geometry and the runs are what its
    //      visibility test sees;
    //   2. its FACES then read three vertex numbers and the transformed
    //      records - a vertex introduced by an earlier run that this draw
    //      skipped or did not reach is transformed on first use;
    //   3. a face sent takes its vertices' records into the batch once each:
    //      the baked colour straight through as bytes, as the original copied
    //      its +44 dword - only a shimmering vertex, the greyscale bank or the
    //      fog on the CPU computes a colour, from the corner's own floats as
    //      before.
    // The face tests are the corner-by-corner submit's (`backFacing`'s sign is
    // the projected area's, without a divide) and the clip is the same
    // `emitClipped`, so the frame is the corner-by-corner one byte for byte
    // (`verify.py: engine: gl1 backend`). Not for a body posed here.
    template <class Ix>
    void submitShape(const Draw& d, Shape& S, const Ix* vert) {
        const Geometry& g = *d.geo;
        const std::size_t nCorner = d.count - d.count % 3;
        const bool cullOn = g.cornerCull.size() == g.corners.size() && !noCull();
        ++drawStamp_;
        if (vx_.size() < S.nv) vx_.resize(S.nv, VX{});
        const std::vector<std::uint8_t>* tieLose = nullptr;
        if (const auto bt = tieBaked_.find(&g); bt != tieBaked_.end()) tieLose = &bt->second;
        static const bool noSkip = omk::envFlag("OMK_GL1_NO_SKIP");
        const Clusters* cl = noSkip ? nullptr : clustersFor(g);
        // the colour as stored, straight through: the fog on the card (no
        // per-vertex fog term) and no greyscale; a shimmering vertex says so
        // in its own record
        const bool plain = cardFog_ && !view_.grey;
        // a vertex's colour the old way - the corner's floats (`colourOf`)
        const auto cornerColour = [&](std::size_t ci, Vtx& p) {
            const Corner& c = g.corners[ci];
            const float sh = shimmerOffset(c.phase, view_.shimmerClock);
            p.c[0] = c.r + sh; p.c[1] = c.g + sh; p.c[2] = c.b + sh;
            if (view_.grey) p.c[0] = p.c[1] = p.c[2] = lumaGreyUnit(p.c[0], p.c[1], p.c[2]);
            p.u = c.u; p.t = c.v;
        };
        const auto asVtx = [&](std::uint32_t v, std::size_t ci) {
            const VX& x = vx_[v];
            Vtx p;
            p.v[0] = x.x; p.v[1] = x.y; p.v[2] = x.z;
            cornerColour(ci, p);
            return p;
        };
        std::size_t run = ~std::size_t{0};
        for (std::size_t t = 0; t < nCorner; t += 3) {
            const std::size_t ci0 = d.start + t;
            const std::size_t r = ci0 / kClusterCorners;
            if (r != run) {
                run = r;
                if (cl && sphereOutside(cl->sph.data() + 4 * r)) {
                    // to the end of the run (or of the draw), unread
                    const std::size_t runEnd = (r + 1) * kClusterCorners - d.start;
                    const std::size_t to = std::min(runEnd, nCorner);
                    const std::size_t tris = (to - t + 2) / 3;
                    st_.triangles += static_cast<long>(tris);
                    st_.offscreen += static_cast<long>(tris);
                    statSkipped_ += static_cast<long>(3 * tris);
                    t = to - 3;                 // the loop's += 3 lands on `to`
                    continue;
                }
                transformVertices(S, S.own[r], S.own[r + 1]);      // step 1
            }
            ++st_.triangles;
            statCorners_ += 3;
            if (pending() >= kBatchMax) flush();        // a bounded pool, as the original's
            // the vertex numbers two triangles ahead, asked for now (no
            // hardware prefetch on the G3; a hint, it changes no value)
            if (ci0 + 6 < S.n) __builtin_prefetch(vert + ci0 + 6);
            const std::uint32_t v[3] = {vert[ci0], vert[ci0 + 1], vert[ci0 + 2]};
            for (int k = 0; k < 3; ++k)
                if (vx_[v[k]].stamp != drawStamp_) transformVertices(S, v[k], v[k] + 1);   // step 2
            const VX& a = vx_[v[0]];
            const VX& b = vx_[v[1]];
            const VX& c = vx_[v[2]];
            const unsigned outAll = a.oc & b.oc & c.oc;
            if (outAll) {
                if (outAll & 1u) ++st_.behind; else ++st_.offscreen;
                continue;
            }
            const bool cullThis = cullOn && g.cornerCull[ci0] != 0;
            const bool inside = (a.oc | b.oc | c.oc) == 0;
            if (inside && cullThis) {
                const Vtx p0{{a.x, a.y, a.z}, {}, 0, 0}, p1{{b.x, b.y, b.z}, {}, 0, 0}, p2{{c.x, c.y, c.z}, {}, 0, 0};
                if (backFacing(p0, p1, p2)) { ++st_.culled; continue; }
            }
            // A TIE LOSER (`bakeDepthTie`) is drawn a hair BACK and sends its
            // own vertices - a shared one is at the plain depth
            const bool lose = tieLose && tieLose->size() > ci0 / 3 && (*tieLose)[ci0 / 3];
            tieBack_ = lose;
            if (lose) ++statTieBack_;
            if (inside && lose) {
                ++st_.drawn;
                for (int k = 0; k < 3; ++k) idx_.push_back(emit(asVtx(v[k], ci0 + static_cast<std::size_t>(k))));
                tieBack_ = false;
                continue;
            }
            if (inside) {
                ++st_.drawn;
                for (int k = 0; k < 3; ++k) {                                       // step 3
                    VX& x = vx_[v[k]];
                    if (x.sent == batchStamp_) { idx_.push_back(x.out); continue; }
                    const SVert& s = S.vtx[v[k]];
                    std::uint16_t o;
                    if (plain && s.c[3] == 255) {
                        o = static_cast<std::uint16_t>(pending());
                        ++statSent_;
                        outN_.push_back(OutN{{x.x, x.y, x.z, 1.0f}, {s.c[0], s.c[1], s.c[2], 255}, {s.u, s.v}});
                    } else {
                        o = emit(asVtx(v[k], ci0 + static_cast<std::size_t>(k)));
                    }
                    x.out = o;
                    x.sent = batchStamp_;
                    idx_.push_back(o);
                }
                continue;
            }
            const Vtx in[3] = {asVtx(v[0], ci0), asVtx(v[1], ci0 + 1), asVtx(v[2], ci0 + 2)};
            emitClipped(in, cullThis);
        }
    }

    // `sub_4947F0`'s pass over a block of a shape's vertices: into view space
    // and their outcodes against the five planes (the corner-by-corner
    // submit's own expressions, so the same bits), stamped as this draw's
    void transformVertices(const Shape& S, std::uint32_t from, std::uint32_t to) {
        const float gx = gx_, gy = gy_;
        const float ex = eye_[0], ey = eye_[1], ez = eye_[2];
        for (std::uint32_t v = from; v < to; ++v) {
            if (v + 4 < to) __builtin_prefetch(&S.vtx[v + 4]);
            const SVert& s = S.vtx[v];
            VX& x = vx_[v];
            const float dx = s.x - ex, dy = s.y - ey, dz = s.z - ez;
            const float vx = dx * bs_[0] + dy * bs_[1] + dz * bs_[2];
            const float vy = dx * bu_[0] + dy * bu_[1] + dz * bu_[2];
            const float vz = dx * bf_[0] + dy * bf_[1] + dz * bf_[2];
            unsigned o = vz > kNearCut ? 0u : 1u;
            if (vz * gx - vx < 0.0f) o |= 2u;
            if (vz * gx + vx < 0.0f) o |= 4u;
            if (vz * gy - vy < 0.0f) o |= 8u;
            if (vz * gy + vy < 0.0f) o |= 16u;
            x.x = vx; x.y = vy; x.z = vz;
            x.oc = static_cast<std::uint8_t>(o);
            x.stamp = drawStamp_;
            x.sent = 0;                 // not sent yet
        }
        statTransformed_ += static_cast<long>(to - from);
    }

    struct OutV { float p[4]; unsigned char c[4]; float uv[2]; float f[3]; };
    struct OutN { float p[4]; unsigned char c[4]; float uv[2]; };   // a pass without the fog sum
    static constexpr std::size_t kBatchMax = 3 * 16384;
    std::vector<OutV> out_;
    std::vector<OutN> outN_;
    float fogInv_ = 0;
    long submits_ = 0, batches_ = 0, passes_ = 0, sum_[5]{};
    // the camera, applied on the CPU (setCamera)
    float bs_[3]{}, bu_[3]{}, bf_[3]{}, th_ = 1, tv_ = 1, eye_[3]{};
    float ith_ = 1, itv_ = 1, gx_ = 1.01f, gy_ = 1.01f, gxLen_ = 1.42f, gyLen_ = 1.42f;
    int camW_ = 1, camH_ = 1;
};

}  // namespace

Renderer* makeGl1Renderer() { return new Gl1Renderer(); }

void gl1SetFullTextures(Renderer* r, bool full) {
    if (r) static_cast<Gl1Renderer*>(r)->setFullTextures(full);
}
void gl1SetFogUnit(Renderer* r, bool unit) {
    if (r) static_cast<Gl1Renderer*>(r)->setFogUnit(unit);
}

void gl1SetPerCorner(Renderer* r, bool on) {
    if (r) static_cast<Gl1Renderer*>(r)->setPerCorner(on);
}
void gl1SetCpuFog(Renderer* r, bool cpu) {
    if (r) static_cast<Gl1Renderer*>(r)->setCpuFog(cpu);
}

void gl1SetNoLock(Renderer* r, bool off) {
    if (r) static_cast<Gl1Renderer*>(r)->setNoLock(off);
}

#if defined(OMK_GL1_ON_WINDOW)
// `r` is the renderer `makeGl1Renderer` made (the glue holds no other)
bool gl1PresentWorld(Renderer* r, int ww, int wh) {
    return r && static_cast<Gl1Renderer*>(r)->presentWorld(ww, wh);
}
bool gl1PresentOverlay(Renderer* r, const Surface& fb, const std::uint8_t* mask,
                       const std::uint8_t* maskRow, const float fade[4], int ww, int wh, bool probe) {
    return r && static_cast<Gl1Renderer*>(r)->presentOverlay(fb, mask, maskRow, fade, ww, wh, probe);
}
bool gl1OnWindow(Renderer* r) { return r && static_cast<Gl1Renderer*>(r)->onWindow(); }
#endif

}  // namespace omk

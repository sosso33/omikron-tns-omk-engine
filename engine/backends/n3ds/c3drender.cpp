// SPDX-License-Identifier: GPL-3.0-or-later
// The citro3d backend - see c3drender.h. Since 3b (`todo/3ds-port.md`) the
// GPU transforms: the geometry is RESIDENT in linear memory and the vertex
// shader (`scene.v.pica`) does the view-projection, the shimmer and the fog -
// the GLES backend's design (`backends/gles/glesrender.cpp`: buffers cached by
// geometry and revision, the dirty corners patched, the cull one draw per run
// of `cornerCull`), because the first console run spent 44-48 ms a street
// frame doing that work per vertex on the CPU, GL1's way.
#include "c3drender.h"

#include "o3de/shimmer.h"
#include "platform/profile.h"
#include "ui/surface.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>

// the vertex shader, assembled by picasso and embedded by bin2s (Makefile)
extern "C" {
extern const u8 scene_shbin[];
extern const u32 scene_shbin_size;
extern const u8 posed_shbin[];
extern const u32 posed_shbin_size;
}

namespace omk {
namespace {

// One corner as the shader reads it: the world position, texels, the baked
// colour as four bytes, the shimmer phase - the GLES `GpuVert` with the colour
// packed (28 bytes, not 36: linear memory is the 3DS's scarce one).
struct GpuVert {
    float x, y, z;
    float u, v;
    std::uint8_t r, g, b, a;
    float phase;
};
static_assert(sizeof(GpuVert) == 28, "the attribute stride below");

inline std::uint8_t toByte(float v) {
    const float x = v * 255.0f + 0.5f;
    return static_cast<std::uint8_t>(x <= 0.0f ? 0.0f : (x >= 255.0f ? 255.0f : x));
}
inline GpuVert gpuVert(const Corner& c) {
    return {c.x, c.y, c.z, c.u, c.v, toByte(c.r), toByte(c.g), toByte(c.b), 255, c.phase};
}

// A geometry changed AFTER it was drawn in the same pass cannot be rewritten
// in place - the GPU reads a buffer when the pass is submitted, not when the
// draw is recorded - so its corners go through this ring, one pass's worth.
constexpr std::size_t kRingCorners = 96 * 1024;        // 2.6 MB

// THE VERTEX INDEX (3ds-port.md step 6, 2026-10-07) - the original's own
// arrangement: `Scene_Load3DO` keeps the file's vertices and its faces INDEX
// them, and `sub_4947F0` transforms each unique vertex ONCE into a shared
// pool (todo/ram-vs-original.md, cpu-vs-original.md). The port's geometry is
// three corners a triangle - Anekbah's set 139245 corners on 63079 vertices,
// Kay'l 1626 on 272 - so a GPU fed the corners runs its vertex program once
// a corner. Here the corners stay the shared code's address and an index is
// built BENEATH them: corners share a vertex only within ONE MESH (the motion
// patch moves a mesh's corners by one transform, so equal corners stay equal)
// and only where every byte the GPU reads is equal. The PICA reuses a shaded
// vertex through its post-vertex cache (`GPUREG_POST_VERTEX_CACHE_NUM`;
// citro3d clears it after every `C3D_DrawElements`), so how much of the
// saving reaches the GPU is the console's to say. Indices are 16-bit (the
// PICA's widest), so a geometry over 65535 vertices stays unindexed.
template <class V>
struct VertexIndex {
    std::vector<V> verts;
    std::vector<std::uint16_t> idx;      // a corner's vertex
    std::vector<std::uint16_t> refs;     // the corners on a vertex
    // `value(k)` is corner k's vertex, `mesh` its mesh (or null: one mesh).
    // False when the vertices do not fit 16 bits.
    template <class F>
    bool build(std::size_t n, const std::int32_t* mesh, F&& value) {
        verts.clear(); refs.clear(); idx.assign(n, 0);
        std::vector<std::int32_t> vmesh;
        std::size_t cap = 64;
        while (cap < 2 * n) cap <<= 1;
        std::vector<std::int32_t> table(cap, -1);
        for (std::size_t k = 0; k < n; ++k) {
            const V v = value(k);
            const std::int32_t m = mesh ? mesh[k] : 0;
            std::uint32_t h = 2166136261u ^ static_cast<std::uint32_t>(m);
            const auto* b = reinterpret_cast<const unsigned char*>(&v);
            for (std::size_t i = 0; i < sizeof(V); ++i) h = (h ^ b[i]) * 16777619u;
            std::size_t slot = h & (cap - 1);
            for (;;) {
                const std::int32_t e = table[slot];
                if (e < 0) {
                    if (verts.size() >= 65536) return false;
                    table[slot] = static_cast<std::int32_t>(verts.size());
                    idx[k] = static_cast<std::uint16_t>(verts.size());
                    verts.push_back(v);
                    vmesh.push_back(m);
                    refs.push_back(1);
                    break;
                }
                if (vmesh[static_cast<std::size_t>(e)] == m &&
                    std::memcmp(&verts[static_cast<std::size_t>(e)], &v, sizeof(V)) == 0) {
                    idx[k] = static_cast<std::uint16_t>(e);
                    ++refs[static_cast<std::size_t>(e)];
                    break;
                }
                slot = (slot + 1) & (cap - 1);
            }
        }
        return true;
    }
};
// A scene geometry is indexed from this many corners: the sets and the large
// props. Smaller ones - the sky, the particles, the shadow quads, rewritten
// whole most frames - keep their corners (an index would be rebuilt as often).
constexpr std::size_t kIndexMinCorners = 4096;

// A REST corner of a body the GPU poses (3d): the scene's corner without the
// shimmer phase, with its mesh's SLOT and its rest normal (`posed.v.pica`).
struct GpuPoseVert {
    float x, y, z;
    float u, v;
    std::uint8_t r, g, b, a;
    float slot;
    float nx, ny, nz;
};
static_assert(sizeof(GpuPoseVert) == 40, "the posed attribute stride below");

// What `posed.v.pica` holds: 23 slots of three rows (its `pose[69]`), and
// eight lights. A body over either is posed and lit on the CPU instead.
constexpr std::size_t kPoseSlots = 23;
constexpr int kMaxLights = 8;

// WHICH WAY THE PICA CULLS the software rasterizer's back face under this
// projection - MEASURED, as GLES's `kGlesCullBack` is (3b, Azahar): the wrong
// value culls every front face, which no frame can hide.
constexpr GPU_CULLMODE kCullBack = GPU_CULL_BACK_CCW, kCullFront = GPU_CULL_FRONT_CCW;

class C3dRenderer final : public Renderer {
public:
    ~C3dRenderer() override {
        if (!ready_) return;
        closeFrame();
        freeTextures();
        removeGeometryListener(&C3dRenderer::geometryGone, this);
        for (auto& [g, r] : res_) if (r.buf) linearFree(r.buf);
        for (auto& [g, r] : res_) if (r.idx) linearFree(r.idx);
        for (void* b : grave_) linearFree(b);
        for (auto& [g, r] : pres_) { if (r.buf) linearFree(r.buf); if (r.idx) linearFree(r.idx); }
        for (void* b : poseGrave_) linearFree(b);
        if (target_) C3D_RenderTargetDelete(target_);
        if (ring_) linearFree(ring_);
        if (read_) linearFree(read_);
        for (std::uint32_t* b : small_) if (b) linearFree(b);
        if (dvlb_) { shaderProgramFree(&prog_); DVLB_Free(dvlb_); }
        if (pdvlb_) { shaderProgramFree(&pprog_); DVLB_Free(pdvlb_); }
        C3D_Fini();
    }

    bool init(int w, int h) override {
        if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return false;
        if (!ready_) {
            // a command buffer for a street frame's draws and their state
            if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4)) {
                std::printf("c3d: C3D_Init failed\n");
                return false;
            }
            dvlb_ = DVLB_ParseFile(const_cast<u32*>(reinterpret_cast<const u32*>(scene_shbin)),
                                   scene_shbin_size);
            if (!dvlb_) { std::printf("c3d: the vertex shader did not parse\n"); return false; }
            shaderProgramInit(&prog_);
            shaderProgramSetVsh(&prog_, &dvlb_->DVLE[0]);
            shaderInstance_s* vs = prog_.vertexShader;
            uMvp_ = shaderInstanceGetUniformLocation(vs, "mvp");
            uTexScale_ = shaderInstanceGetUniformLocation(vs, "texscale");
            uFog_ = shaderInstanceGetUniformLocation(vs, "fogp");
            uShim_ = shaderInstanceGetUniformLocation(vs, "shim");
            uWave_ = shaderInstanceGetUniformLocation(vs, "wave");
            uGrey_ = shaderInstanceGetUniformLocation(vs, "greyw");
            if (uMvp_ < 0 || uTexScale_ < 0 || uFog_ < 0 || uShim_ < 0 || uWave_ < 0 || uGrey_ < 0) {
                std::printf("c3d: the vertex shader lacks a uniform (mvp %d texscale %d fogp %d "
                            "shim %d wave %d greyw %d)\n", uMvp_, uTexScale_, uFog_, uShim_, uWave_,
                            uGrey_);
                return false;
            }
            // THE POSING PROGRAM (3d). Its first three uniforms must sit in the
            // scene program's registers - they carry across a switch - which
            // the declaration order gives and this checks; otherwise bodies
            // stay on the CPU, said.
            pdvlb_ = DVLB_ParseFile(const_cast<u32*>(reinterpret_cast<const u32*>(posed_shbin)),
                                    posed_shbin_size);
            if (pdvlb_) {
                shaderProgramInit(&pprog_);
                shaderProgramSetVsh(&pprog_, &pdvlb_->DVLE[0]);
                shaderInstance_s* ps = pprog_.vertexShader;
                pMisc_ = shaderInstanceGetUniformLocation(ps, "misc");
                pLights_ = shaderInstanceGetUniformLocation(ps, "lights");
                pPose_ = shaderInstanceGetUniformLocation(ps, "pose");
                pGrey_ = shaderInstanceGetUniformLocation(ps, "greyw");
                const bool shared = shaderInstanceGetUniformLocation(ps, "mvp") == uMvp_ &&
                                    shaderInstanceGetUniformLocation(ps, "texscale") == uTexScale_ &&
                                    shaderInstanceGetUniformLocation(ps, "fogp") == uFog_;
                posing_ = shared && pMisc_ >= 0 && pLights_ >= 0 && pPose_ >= 0 && pGrey_ >= 0;
                std::printf("c3d: bodies posed and lit %s\n", posing_
                            ? "by the GPU (posed.v.pica: 23 slots, 8 lights)"
                            : "on the CPU - the posing program's uniforms do not line up");
            }
            ring_ = static_cast<GpuVert*>(linearAlloc(kRingCorners * sizeof(GpuVert)));
            if (!ring_) { std::printf("c3d: no linear memory for the vertex ring\n"); return false; }
            addGeometryListener(&C3dRenderer::geometryGone, this);
            ready_ = true;
        }
        if (target_) { C3D_RenderTargetDelete(target_); target_ = nullptr; }
        if (read_) { linearFree(read_); read_ = nullptr; }
        for (std::uint32_t*& b : small_) if (b) { linearFree(b); b = nullptr; }
        lastHalfPass_ = -10;
        // THE PICA's SIDES ARE MULTIPLES OF 8 (its 8x8 tiles), and a frame
        // need not be (800x450, say): the target is rounded up and
        // the picture drawn in its top `h` rows - the top-left rectangle the
        // reference draws into anyway (`renderer.h`, View).
        tw_ = (w + 7) & ~7;
        th_ = (h + 7) & ~7;
        // THE 16-BIT EXPERIMENT (`sdmc:/omk/c3d-rgb565`, an instrument): the
        // target in RGB565, the original's framebuffer depth, and the
        // transfers in 565 too - no CPU conversion at all. What it is FOR is
        // the open question of `todo/3ds-port.md` 3: whether the PICA
        // dithers when it writes a 16-bit buffer (a capture of a dark
        // gradient shows it); Azahar cannot say. Off, the target is RGBA8
        // and the CPU dithers (the reference's 4x4 ordered).
        if (std::FILE* f = std::fopen("sdmc:/omk/c3d-rgb565", "r")) { std::fclose(f); rgb565_ = true; }
        // THE STRAIGHT PRESENT ONE FRAME BEHIND (`presentHalf`), unless
        // `sdmc:/omk/c3d-sync` is on the card - an instrument, to lay the
        // synchronous present beside the pipelined one on the console
        if (std::FILE* f = std::fopen("sdmc:/omk/c3d-sync", "r")) { std::fclose(f); syncPresent_ = true; }
        // THE VERTEX INDEX (step 6), unless `sdmc:/omk/c3d-arrays` is on the
        // card - an instrument, every draw from the corners as before
        if (std::FILE* f = std::fopen("sdmc:/omk/c3d-arrays", "r")) { std::fclose(f); noIndex_ = true; }
        target_ = C3D_RenderTargetCreate(tw_, th_, rgb565_ ? GPU_RB_RGB565 : GPU_RB_RGBA8,
                                         GPU_RB_DEPTH24_STENCIL8);
        if (!target_) {
            std::printf("c3d: no %dx%d render target (VRAM)\n", w, h);
            return false;
        }
        read_ = static_cast<std::uint32_t*>(linearAlloc(static_cast<std::size_t>(tw_) * th_ * 4));
        if (!read_) { std::printf("c3d: no linear memory for the readback\n"); return false; }
        OMK_GPU_ALLOC("render targets", omk::prof::gpuKey(omk::prof::kGlRenderbuffer, 1u), 8LL * w * h);
        w_ = w; h_ = h;
        fb_ = Surface(w, h, 0);
        readDone_ = false;
        std::printf("c3d: the straight present %s\n", syncPresent_
                    ? "SYNCHRONOUS (c3d-sync present): each frame waits for its own picture"
                    : "one frame behind: the GPU draws frame N while the CPU shows N-1 and builds N+1");
        if (rgb565_) std::printf("c3d: c3d-rgb565 present - the target is RGB565, the transfers 565, no CPU "
                                 "conversion (the 16-bit experiment)\n");
        std::printf("c3d: %s\n", noIndex_ ? "c3d-arrays present - every draw from the corners, unindexed"
                                          : "indexed draws - a vertex shaded once, as the original transforms it "
                                            "(the bodies, and scene geometry from 4096 corners)");
        std::printf("c3d: citro3d on the PICA200, a %dx%d %s target (the %dx%d frame in it) with "
                    "24-bit depth in VRAM, %u KB of VRAM left; the GPU transforms (resident geometry)\n",
                    tw_, th_, rgb565_ ? "RGB565" : "RGBA8", w, h, static_cast<unsigned>(vramSpaceFree() / 1024));
        return true;
    }

    // THE TEXTURE POOL, uploaded only where it is new (`todo/3ds-port.md`
    // 3a). The pool is handed over again at every change of the resident set
    // - the first console run logged it some twenty times walking out of the
    // restaurant into Anekbah, each re-tiling ~7 MB on the CPU. As the GLES
    // backend does (`glesrender.cpp` setTextures): a texture already on the
    // GPU from an earlier pool - the same pixel storage, the same size - is
    // kept, and the entry holds that storage, so its address cannot come back
    // as another texture's while cached; what no slot of the new pool uses
    // leaves the GPU.
    void setTextures(std::span<const Texture> t) override {
        closeFrame();                            // a texture in use is never freed under the GPU
        const u64 t0 = svcGetSystemTick();
        for (auto& [key, u] : uploaded_) u.used = false;
        tex_.assign(t.size(), Slot{});
        int reused = 0, fresh = 0, failed = 0;
        std::vector<std::uint16_t> tiled;
        for (std::size_t i = 0; i < t.size(); ++i) {
            const Texture& x = t[i];
            if (!x.hasPixels()) continue;
            // the PALETTE too: the greyscale bank (ops 150/151) hands the same
            // indices over with a grey palette, and the colour one comes back
            const auto key = std::make_tuple(x.idx.data(), x.pal.data(), x.width, x.height);
            auto hit = uploaded_.find(key);
            if (hit == uploaded_.end()) {
                auto tex = std::make_unique<C3D_Tex>();
                if (!C3D_TexInit(tex.get(), static_cast<u16>(x.width), static_cast<u16>(x.height),
                                 GPU_RGBA5551)) {
                    std::printf("c3d: texture %zu (%s, %dx%d) has no memory - drawn untextured\n",
                                i, x.name.c_str(), x.width, x.height);
                    ++failed;
                    continue;
                }
                // the palette as RGBA5551 once - the colour key on black as alpha 0
                std::uint16_t pal[256];
                for (int k = 0; k < 256; ++k) {
                    const std::uint8_t* p = x.pal.data() + 3 * k;
                    const unsigned r = p[0], g = p[1], b = p[2];
                    pal[k] = static_cast<std::uint16_t>((r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 |
                                                        ((r | g | b) ? 1u : 0u));
                }
                tile(x, pal, tiled);
                C3D_TexUpload(tex.get(), tiled.data());
                C3D_TexFlush(tex.get());
                C3D_TexSetWrap(tex.get(), GPU_REPEAT, GPU_REPEAT);
                const GPU_TEXTURE_FILTER_PARAM f = filter_ ? GPU_LINEAR : GPU_NEAREST;
                C3D_TexSetFilter(tex.get(), f, f);
                hit = uploaded_.emplace(key, Uploaded{std::move(tex), x.idx, x.pal, 2LL * x.width * x.height,
                                                      false}).first;
                ++fresh;
            } else {
                ++reused;
            }
            hit->second.used = true;
            tex_[i] = Slot{hit->second.tex.get(), static_cast<float>(x.width), static_cast<float>(x.height)};
        }
        int dropped = 0;
        long long bytes = 0;
        for (auto it = uploaded_.begin(); it != uploaded_.end(); ) {
            if (it->second.used) { bytes += it->second.bytes; ++it; continue; }
            C3D_TexDelete(it->second.tex.get());
            it = uploaded_.erase(it);
            ++dropped;
        }
        std::printf("c3d: texture pool of %zu - %d kept on the GPU, %d uploaded, %d dropped%s; "
                    "%lld KB as RGBA5551, %u KB of linear memory left, %.1f ms\n",
                    t.size(), reused, fresh, dropped, failed ? " (some failed)" : "", bytes / 1024,
                    static_cast<unsigned>(linearSpaceFree() / 1024),
                    (svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11);
        boundTex_ = ~0u;
    }

    void begin(const View& v) override {
        const u64 b0 = svcGetSystemTick();
        passStart_ = b0;
        closeFrame();
        view_ = v;
        RCamera cam = v.cam;
        cam.w = v.letterboxed() ? v.vw : w_;
        cam.h = v.letterboxed() ? v.vh : h_;
        flipX_ = cam.flipX;

        // WAITS FOR THE GPU'S LAST PASS (the whole queue: its draws and the
        // transfer that ended it), not for a VBlank - so THIS is where a GPU
        // slower than the CPU shows, and nowhere else (2026-10-06)
        const u64 fb0 = svcGetSystemTick();
        C3D_FrameBegin(0);
        rep_.frameWaitTicks += svcGetSystemTick() - fb0;
        inFrame_ = true;
        ++pass_;
        ++rep_.passes;
        // what a destroyed geometry left while the last pass could still read it
        for (void* b : grave_) linearFree(b);
        grave_.clear();
        for (void* b : poseGrave_) linearFree(b);
        poseGrave_.clear();
        // cleared to the view's clear colour - the fog's, so the horizon past
        // the clip distance is what the fog fades into (View::clearColour) -
        // in the TARGET's format: citro3d hands the value to GX_MemoryFill
        // unconverted and a 16-bit fill keeps the low half, so 0x000000FF on
        // the RGB565 target was 0x00FF, a saturated blue in every pixel
        // nothing draws (the reader's run E, 2026-10-07)
        const unsigned cr = v.clearColour[0], cg = v.clearColour[1], cb = v.clearColour[2];
        C3D_RenderTargetClear(target_, C3D_CLEAR_ALL,
                              rgb565_ ? ((cr >> 3) << 11 | (cg >> 2) << 5 | cb >> 3)
                                      : (cr << 24 | cg << 16 | cb << 8 | 0xFFu),
                              0x00FFFFFF);
        C3D_FrameDrawOn(target_);
        // the picture in the TOP-LEFT cam.w x cam.h, as the reference draws
        // it; the PICA's viewport origin is the bottom-left, as GL's
        C3D_SetViewport(0, static_cast<u32>(th_ - cam.h), static_cast<u32>(cam.w), static_cast<u32>(cam.h));
        shimClock_ = std::floor(std::floor(v.shimmerClock) / 4.0f);
        grey_ = v.grey ? 1.0f : 0.0f;
        // the posing program's register is its own (the scene's sit under
        // `pose` and are re-set with the shimmer on a switch back)
        if (pGrey_ >= 0) C3D_FVUnifSet(GPU_VERTEX_SHADER, pGrey_, 0.299f, 0.587f, 0.114f, grey_);
        boundProg_ = -1;
        useProgram(0);
        used_ = 0;

        // THE VIEW-PROJECTION from the software rasterizer's own basis, as
        // GLES builds it - rows 0 and 1 the screen axes over the two tangents
        // (y UP, as GL), row 3 f . (world - eye), the view depth the fog reads.
        // Row 2 is the PICA's: z = kNearCut - w, so z/w runs 0 at the near
        // cut toward -1 far away, the hardware clips z > 0 - the reference's
        // near cut - and the depth map makes 0..1 increasing away from the eye.
        float s[3], u[3], f[3], tanH = 0, tanV = 0;
        cameraBasis(cam, s, u, f, tanH, tanV);
        const float* e = cam.eye;
        const float se = s[0] * e[0] + s[1] * e[1] + s[2] * e[2];
        const float ue = u[0] * e[0] + u[1] * e[1] + u[2] * e[2];
        const float fe = f[0] * e[0] + f[1] * e[1] + f[2] * e[2];
        C3D_FVUnifSet(GPU_VERTEX_SHADER, uMvp_ + 0, s[0] / tanH, s[1] / tanH, s[2] / tanH, -se / tanH);
        C3D_FVUnifSet(GPU_VERTEX_SHADER, uMvp_ + 1, u[0] / tanV, u[1] / tanV, u[2] / tanV, -ue / tanV);
        C3D_FVUnifSet(GPU_VERTEX_SHADER, uMvp_ + 2, -f[0], -f[1], -f[2], fe + kNearCut);
        C3D_FVUnifSet(GPU_VERTEX_SHADER, uMvp_ + 3, f[0], f[1], f[2], -fe);
        C3D_DepthMap(true, -1.0f, 0.0f);
        setShimmer();
        rep_.beginTicks += svcGetSystemTick() - b0;

        cull_ = -1;
        stateValid_ = false;
        boundTex_ = ~0u;
        st_ = RasterStats{};
        readDone_ = false;
        // THE FOG'S COLOUR (View::fog): stage 1 blends stage 0's textured
        // colour toward it by the factor the vertex shader puts in the
        // primary colour's alpha - r * f + fog * (1 - f), as the reference
        // does after the texture. Alpha passes through (the colour key).
        {
            C3D_TexEnv* env = C3D_GetTexEnv(1);
            C3D_TexEnvInit(env);
            C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PRIMARY_COLOR);
            C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR,
                            GPU_TEVOP_RGB_SRC_ALPHA);
            C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
            C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS, GPU_PREVIOUS, GPU_PREVIOUS);
            C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
            C3D_TexEnvColor(env, 0xFF000000u | static_cast<u32>(v.fogColour[2]) << 16
                                 | static_cast<u32>(v.fogColour[1]) << 8 | v.fogColour[0]);
        }
        if ((v.fogColour[0] | v.fogColour[1] | v.fogColour[2]) && v.fog && !toldFogColour_) {
            toldFogColour_ = true;
            std::printf("c3d: a coloured fog (%u %u %u) - fading into it, the picture cleared to %u %u %u\n",
                        v.fogColour[0], v.fogColour[1], v.fogColour[2],
                        v.clearColour[0], v.clearColour[1], v.clearColour[2]);
        }
    }

    void submit(const Draw& d) override {
        const u64 s0 = svcGetSystemTick();
        submitInner(d);
        rep_.submitTicks += svcGetSystemTick() - s0;
    }

    void submitInner(const Draw& d) {
        if (!inFrame_ || !d.geo || d.count < 3) return;
        const Geometry& g = *d.geo;
        if (d.start + d.count > g.corners.size()) return;
        // A BODY POSED BY THE GPU (3d): the rest geometry and one affine a
        // mesh, through the posing program - or on the CPU when the program
        // cannot hold it (too many slots, a shimmering corner, too many
        // lights), into this pass's ring and through the scene program.
        std::size_t first = d.start;
        const void* buf = nullptr;
        const std::uint16_t* idx = nullptr;   // a corner's vertex, when the buffer is indexed
        std::size_t stride = sizeof(GpuVert);
        if (d.meshPose && d.meshPoses) {
            PoseRes* pr = posing_ ? poseResident(g) : nullptr;
            if (pr && pr->meshOfSlot.size() <= kPoseSlots && !pr->shimmer &&
                d.vertexLightCount <= kMaxLights) {
                useProgram(1);
                const u64 u0 = svcGetSystemTick();
                setPoseUniforms(d, *pr);
                rep_.uniformTicks += svcGetSystemTick() - u0;
                buf = pr->buf;
                idx = pr->idx;
                stride = sizeof(GpuPoseVert);
                ++posedGpu_;
            } else {
                const u64 c0 = svcGetSystemTick();
                buf = cpuPose(d);
                rep_.cpuPoseTicks += svcGetSystemTick() - c0;
                first = 0;
                if (!buf) return;
                useProgram(0);
                ++posedCpu_;
            }
        } else {
            // the corners on the GPU: the geometry's own buffer, or this
            // pass's ring when it changed after being drawn in it; `first` is
            // the buffer's index of the draw's first corner
            const u64 r0 = svcGetSystemTick();
            buf = resident(g, d.start, d.count, first, idx);
            rep_.residentTicks += svcGetSystemTick() - r0;
            if (!buf) return;
            useProgram(0);
        }
        setState(d);
        if (buf != boundBuf_) {
            C3D_BufInfo* bi = C3D_GetBufInfo();
            BufInfo_Init(bi);
            if (stride == sizeof(GpuPoseVert)) BufInfo_Add(bi, buf, stride, 5, 0x43210);
            else BufInfo_Add(bi, buf, stride, 4, 0x3210);
            boundBuf_ = buf;
        }
        st_.triangles += static_cast<long>(d.count / 3);
        ++rep_.draws;
        // THE BACK-FACE CULL, one draw per run of `cornerCull` (`geom3do.h`),
        // GLES's way: the engine culls in software (`Render_SubmitMesh`), here
        // the PICA culls the face the reference calls back - the mirror's
        // screen-X flip swapping it, as it swaps the area `raster.cpp` tests.
        const bool haveCull = g.cornerCull.size() == g.corners.size() && !noCull();
        const std::size_t e = d.start + d.count - d.count % 3;
        std::size_t i = d.start;
        while (i < e) {
            const std::uint8_t c = haveCull ? g.cornerCull[i] : 0u;
            std::size_t j = i;
            while (j < e && (haveCull ? g.cornerCull[j] : 0u) == c) ++j;
            const int mode = !c ? 0 : flipX_ ? 2 : 1;
            if (mode != cull_) {
                C3D_CullFace(mode == 0 ? GPU_CULL_NONE : mode == 1 ? kCullBack : kCullFront);
                cull_ = mode;
            }
            if (idx) {
                C3D_DrawElements(GPU_TRIANGLES, static_cast<int>(j - i), C3D_UNSIGNED_SHORT, idx + i);
                ++rep_.indexed;
            } else {
                C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(first + (i - d.start)), static_cast<int>(j - i));
            }
            ++rep_.calls;
            i = j;
        }
        st_.drawn += static_cast<long>(d.count / 3);
    }

    void end() override {
        if (passStart_) { rep_.passTicks += svcGetSystemTick() - passStart_; passStart_ = 0; }
        // the frame stays open: `readback` reads it inside the frame (see
        // there), and the next `begin` closes one nobody read
    }

    const Surface& readback() override {
        if (readDone_) return fb_;               // idempotent (renderer.h)
        if (inFrame_) {
            noteCmdBuf();
            // SYNCHRONOUS, and it has to be: an interface is composited over
            // this picture and the mirror differences two passes of ONE
            // frame (`drawWithMirror`). OUT of the frame, because INSIDE it
            // `C3D_SyncDisplayTransfer` only QUEUES the transfer and returns
            // (citro3d 1.7.1, renderqueue.c) - this read the buffer before
            // the GPU had written it, which Azahar, finishing every command
            // as it is queued, never showed (the seventh console run,
            // 2026-10-06). Out of it, the call waits for the queue - the
            // pass's draws - then for the transfer itself.
            closeFrame();
            const u64 w0 = svcGetSystemTick();
            C3D_SyncDisplayTransfer(static_cast<u32*>(target_->frameBuf.colorBuf), GX_BUFFER_DIM(tw_, th_),
                                    reinterpret_cast<u32*>(read_), GX_BUFFER_DIM(tw_, th_),
                                    GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                                    GX_TRANSFER_RAW_COPY(0) | transferFormats() |
                                    GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
            rep_.waitTicks += svcGetSystemTick() - w0;
        }
        GSPGPU_InvalidateDataCache(read_, static_cast<u32>(tw_) * th_ * 4);
        // A pixel is a word 0xRRGGBBAA, and the transfer hands the rows
        // TOP-DOWN already (the first Azahar frame, 2026-10-06, read them
        // bottom-up and came out upside down, everything else right). Into
        // the frame dithered as the reference is.
        if (rgb565_) {
            const auto* in = reinterpret_cast<const std::uint16_t*>(read_);
            for (int y = 0; y < h_; ++y)
                std::memcpy(fb_.px.data() + static_cast<std::size_t>(y) * w_,
                            in + static_cast<std::size_t>(y) * tw_, static_cast<std::size_t>(w_) * 2);
        } else {
            for (int y = 0; y < h_; ++y)
                toRgb565Row(read_ + static_cast<std::size_t>(y) * tw_, w_, 0, y,
                            fb_.px.data() + static_cast<std::size_t>(y) * w_);
        }
        readDone_ = true;
        if (!toldRead_) {
            toldRead_ = true;
            const std::uint32_t m = read_[static_cast<std::size_t>(h_ / 2) * tw_ + w_ / 2];
            std::printf("c3d: first readback %dx%d, %ld triangles, %zu geometries resident, %ld draws "
                        "posed by the GPU and %ld on the CPU, centre pixel %u %u %u\n", w_, h_,
                        st_.triangles, res_.size(), posedGpu_, posedCpu_,
                        static_cast<unsigned>(m >> 24), static_cast<unsigned>((m >> 16) & 0xFF),
                        static_cast<unsigned>((m >> 8) & 0xFF));
        }
        return fb_;
    }

    // 3c: see `c3dPresentHalf` (c3drender.h).
    bool presentHalf(int vy, int vh, Surface& screen) {
        // A FRAME THE SCREEN'S OWN SIZE (`--res 400x224` in args.txt, the
        // GPU-fill experiment of 2026-10-06: a quarter of the pixels to fill,
        // and no 2x2 average - so no anti-aliasing either) goes over 1:1;
        // anything larger is halved by the transfer, as the default 800x448.
        const bool one = w_ <= 400 && h_ <= 240;
        const int k = one ? 1 : 2;                      // the transfer's reduction
        const int hw = tw_ / k, hh = th_ / k;           // the target as transferred
        if (!inFrame_ || hw % 8 || hh % 8 || w_ / k > 400 || h_ / k > 240) return false;
        for (std::uint32_t*& b : small_)
            if (!b) {
                b = static_cast<std::uint32_t*>(linearAlloc(static_cast<std::size_t>(hw) * hh * 4));
                if (!b) return false;
            }
        noteCmdBuf();
        // ONE FRAME BEHIND (the reader's decision, 2026-10-06: "a 1 frame
        // delay is acceptable for this kind of game"). This pass's transfer
        // is QUEUED into one buffer and the CPU shows the OTHER, which the
        // last pass's transfer filled - finished, since this pass's
        // `C3D_FrameBegin` waited for the whole queue. So the GPU draws frame
        // N while the CPU presents N-1 and builds N+1, instead of each
        // waiting for the other. Only when the LAST PASS was a straight
        // present too: after a composited frame, or between the mirror's
        // passes, the other buffer holds an older picture or another pass's,
        // and this frame waits for its own (as with `sdmc:/omk/c3d-sync`).
        // Nothing is composited over a straight present, so the interface
        // cannot fall a frame out of step with the world.
        const int wr = halfCur_;
        const bool piped = !syncPresent_ && lastHalfPass_ == pass_ - 1;
        // the OUTPUT dimensions are given as the input's: with SCALE_XY the
        // transfer halves what it is told (Azahar, 2026-10-06 - told the
        // halved size, it wrote a quarter-size picture, 200x112)
        const u32 flags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                          transferFormats() | GX_TRANSFER_SCALING(one ? GX_TRANSFER_SCALE_NO : GX_TRANSFER_SCALE_XY);
        if (piped) {
            // inside the frame: queued behind the pass's draws, not waited for
            C3D_SyncDisplayTransfer(static_cast<u32*>(target_->frameBuf.colorBuf), GX_BUFFER_DIM(tw_, th_),
                                    reinterpret_cast<u32*>(small_[wr]), GX_BUFFER_DIM(tw_, th_), flags);
            closeFrame();
            ++rep_.piped;
        } else {
            // out of it: waits for the draws, then the transfer (`readback`)
            closeFrame();
            const u64 w0 = svcGetSystemTick();
            C3D_SyncDisplayTransfer(static_cast<u32*>(target_->frameBuf.colorBuf), GX_BUFFER_DIM(tw_, th_),
                                    reinterpret_cast<u32*>(small_[wr]), GX_BUFFER_DIM(tw_, th_), flags);
            rep_.waitTicks += svcGetSystemTick() - w0;
            ++rep_.synced;
        }
        const std::uint32_t* const shown = small_[piped ? wr ^ 1 : wr];
        lastHalfPass_ = pass_;
        halfCur_ = wr ^ 1;
        GSPGPU_InvalidateDataCache(const_cast<std::uint32_t*>(shown), static_cast<u32>(hw) * hh * (rgb565_ ? 2 : 4));
        const u64 p0 = svcGetSystemTick();
        if (screen.w != 400 || screen.h != 240) screen = Surface(400, 240, 0);
        std::fill(screen.px.begin(), screen.px.end(), std::uint16_t(0));
        // the frame halved and centred; the picture is its rows vy..vy+vh,
        // drawn in the target's top rows
        const int ox = (400 - w_ / k) / 2, oy = (240 - h_ / k) / 2;
        const int py0 = vy / k, ph = vh / k, pw = std::min(w_ / k, hw);
        for (int r = 0; r < ph && r < hh; ++r) {
            const int y = oy + py0 + r;
            if (y < 0 || y >= 240) continue;
            std::uint16_t* out = screen.px.data() + static_cast<std::size_t>(y) * 400 + ox;
            if (rgb565_)
                std::memcpy(out, reinterpret_cast<const std::uint16_t*>(shown) + static_cast<std::size_t>(r) * hw,
                            static_cast<std::size_t>(pw) * 2);
            else
                toRgb565Row(shown + static_cast<std::size_t>(r) * hw, pw, ox, y, out);
        }
        rep_.halfLoopTicks += svcGetSystemTick() - p0;
        ++straight_;
        if (straight_ == 1)
            std::printf("c3d: the first frame presented STRAIGHT - %s, %dx%d onto the top screen at %d,%d\n",
                        one ? "the screen's own size, 1:1" : "the transfer's 2x2 average", pw, ph, ox, oy + py0);
        return true;
    }

    RasterStats stats() const override { return st_; }
    const char* name() const override { return "citro3d (PICA200)"; }
    // the counts since the last report, as one line (`c3dReport`)
    void report(long frame) {
        const double ms = 1000.0 / SYSCLOCK_ARM11;
        const long f = rep_.passes ? rep_.passes : 1;
        std::printf("frame %ld c3d (mean a pass, %ld passes): %.0f draws, %.1f posed on the GPU and %.1f "
                    "on the CPU (%.2f ms), waiting on the GPU %.2f ms at begin and %.2f at a synchronous "
                    "transfer, straight presents %ld a frame behind and %ld synchronous, command buffer "
                    "%.0f%% at most; citro3d's last frame: drawing %.2f ms, processing %.2f ms\n",
                    frame, rep_.passes, static_cast<double>(rep_.draws) / f,
                    static_cast<double>(posedGpu_ - rep_.gpu0) / f, static_cast<double>(posedCpu_ - rep_.cpu0) / f,
                    rep_.cpuPoseTicks * ms / f, rep_.frameWaitTicks * ms / f, rep_.waitTicks * ms / f,
                    rep_.piped, rep_.synced, rep_.cmdMax * 100.0,
                    C3D_GetDrawingTime(), C3D_GetProcessingTime());
        std::printf("frame %ld c3d CPU (ms a pass): begin %.2f, begin..end %.2f, submit %.2f, of it "
                    "residency %.2f and pose uniforms %.2f; the straight present's dither loop %.2f; "
                    "%.0f GPU draw calls, %.0f indexed\n", frame,
                    rep_.beginTicks * ms / f, rep_.passTicks * ms / f,
                    rep_.submitTicks * ms / f, rep_.residentTicks * ms / f, rep_.uniformTicks * ms / f,
                    rep_.halfLoopTicks * ms / f, static_cast<double>(rep_.calls) / f,
                    static_cast<double>(rep_.indexed) / f);
        const long g = posedGpu_, c = posedCpu_;
        rep_ = Report{};
        rep_.gpu0 = g; rep_.cpu0 = c;
    }

    // 3d: a draw may carry a `meshPose` - the frontend hands rest geometry
    // and one affine a mesh - and up to eight lights with it.
    bool posesBodies() const override { return posing_; }
    int maxVertexLights() const override { return posing_ ? kMaxLights : 0; }

    // TEXTURE FILTERING (renderer.h): 0 nearest, 1 bilinear - the original's
    // hardware device (MAG/MIN LINEAR, MIP NONE). No mip chain yet, so
    // trilinear is drawn bilinear and said.
    bool setTextureFilter(int mode) override {
        filter_ = mode >= 1 ? 1 : 0;
        if (mode >= 2) std::printf("c3d: trilinear asked - no mip chain yet, drawn bilinear\n");
        const GPU_TEXTURE_FILTER_PARAM f = filter_ ? GPU_LINEAR : GPU_NEAREST;
        for (auto& [key, u] : uploaded_) C3D_TexSetFilter(u.tex.get(), f, f);
        return mode <= 1;
    }

private:
    // A pool slot: the texture on the GPU (an entry of `uploaded_`) and its size.
    struct Slot { C3D_Tex* tex = nullptr; float w = 1, h = 1; };
    // An upload, kept across pools: the storage it was made from is held, so
    // the key's address stays this texture's.
    struct Uploaded { std::unique_ptr<C3D_Tex> tex; PixelBuffer keep, keepPal; long long bytes = 0; bool used = false; };

    static bool noCull() { static const bool n = std::getenv("OMK_NO_CULL") != nullptr; return n; }

    u32 transferFormats() const {
        return rgb565_ ? (GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565))
                       : (GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8));
    }

    // A row of the transfer's words (0xRRGGBBAA) into 565, dithered as the
    // reference is - through `quantise888DitherRow`'s tables, bit for bit
    // `quantise888Dither` (`verify.py: engine: pixel tables`), and NOT that
    // function a pixel: `quantise888` divides by 255 per channel, and with no
    // divide instruction three per pixel cost the straight present ~15 ms a
    // frame (Azahar, 2026-10-06). The word byte-swapped is the RGBA byte
    // order the row function reads (`REV`, one instruction). `x0` is the
    // row's first pixel's column on the output, where the dither cell is.
    void toRgb565Row(const std::uint32_t* in, int n, int x0, int y, std::uint16_t* out) {
        rowRgba_.resize(static_cast<std::size_t>(n) + 4);
        for (int x = 0; x < n; ++x) rowRgba_[static_cast<std::size_t>(x)] = __builtin_bswap32(in[x]);
        if (!view_.dither) {
            const auto* b = reinterpret_cast<const unsigned char*>(rowRgba_.data());
            for (int x = 0; x < n; ++x) out[x] = quantise888(b[4 * x], b[4 * x + 1], b[4 * x + 2]);
            return;
        }
        // the dither's cell follows the OUTPUT column: start the row so that
        // its pixel 0 sits at x0 (the row function's own x runs from 0)
        if ((x0 & 3) == 0) {
            quantise888DitherRow(reinterpret_cast<const unsigned char*>(rowRgba_.data()), n, y, out);
        } else {
            const int pad = x0 & 3;
            rowPad_.assign(static_cast<std::size_t>(n + pad), 0);
            for (int x = 0; x < n; ++x) rowPad_[static_cast<std::size_t>(pad + x)] = rowRgba_[static_cast<std::size_t>(x)];
            row565_.resize(static_cast<std::size_t>(n + pad));
            quantise888DitherRow(reinterpret_cast<const unsigned char*>(rowPad_.data()), n + pad, y, row565_.data());
            std::memcpy(out, row565_.data() + pad, static_cast<std::size_t>(n) * sizeof(std::uint16_t));
        }
    }

    // ---- THE RESIDENT GEOMETRY (3b) ------------------------------------------
    // GLES's contract (`uploadGeometry`): cached by POINTER while the revision
    // holds; the same object with new corners refills in place, only the ones
    // `dirtyCorners` names when it names them (`geom3do.h`: valid when
    // `dirtyTo == revision` and this buffer holds `dirtyFrom`); a new size is
    // a new buffer. The geometry is marked resident, so its destruction
    // reaches `geometryGone` and its address can never meet this state again.
    struct Res {
        GpuVert* buf = nullptr;
        std::size_t n = 0;
        std::uint64_t rev = 0;
        long drawnPass = -1;          // the pass that last drew from it
        std::uint16_t* idx = nullptr; // a corner's vertex in `buf`, when indexed
        std::size_t nv = 0;           // ...and the vertices in `buf`
        std::vector<std::uint16_t> refs;
        int rebuilds = 0;             // whole re-indexes; past a few, left unindexed
        bool unindexed = false;
    };

    // Corners `buf`/`idx` for a geometry: indexed when it can be, else n
    // corners. The old blocks to the grave. False with no linear memory.
    bool fillResident(const Geometry& g, Res& r) {
        const std::size_t n = g.corners.size();
        VertexIndex<GpuVert> vi;
        const u64 t0 = svcGetSystemTick();
        const bool indexed = !noIndex_ && !r.unindexed && n >= kIndexMinCorners &&
                             g.cornerMesh.size() == n &&
                             vi.build(n, g.cornerMesh.data(), [&](std::size_t k) { return gpuVert(g.corners[k]); });
        const std::size_t nv = indexed ? vi.verts.size() : n;
        auto* buf = static_cast<GpuVert*>(linearAlloc(nv * sizeof(GpuVert)));
        auto* idx = indexed ? static_cast<std::uint16_t*>(linearAlloc(n * sizeof(std::uint16_t))) : nullptr;
        if (!buf || (indexed && !idx)) {
            if (buf) linearFree(buf);
            if (idx) linearFree(idx);
            if (!toldNoMem_) {
                toldNoMem_ = true;
                std::printf("c3d: no linear memory for a geometry of %zu corners (%u KB left) - not "
                            "drawn\n", n, static_cast<unsigned>(linearSpaceFree() / 1024));
            }
            return false;
        }
        if (indexed) {
            std::memcpy(buf, vi.verts.data(), nv * sizeof(GpuVert));
            std::memcpy(idx, vi.idx.data(), n * sizeof(std::uint16_t));
            GSPGPU_FlushDataCache(idx, n * sizeof(std::uint16_t));
            if (n >= 20000)
                std::printf("c3d: a geometry of %zu corners indexed - %zu vertices, %.1f ms\n", n, nv,
                            (svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11);
        } else {
            for (std::size_t k = 0; k < n; ++k) buf[k] = gpuVert(g.corners[k]);
        }
        GSPGPU_FlushDataCache(buf, nv * sizeof(GpuVert));
        retire(r);
        r.buf = buf; r.idx = idx; r.n = n; r.nv = nv;
        r.refs = indexed ? std::move(vi.refs) : std::vector<std::uint16_t>{};
        return true;
    }

    // A partial update of an indexed geometry: each dirty corner's vertex
    // rewritten, and refused (false: re-index whole) when two corners on one
    // vertex would disagree - two dirty ones with different values, or a
    // dirty one changing a vertex a clean corner still reads.
    bool patchIndexed(const Geometry& g, Res& r) {
        const auto& dc = g.dirtyCorners;
        if (markC_.size() < r.n) markC_.resize(r.n, 0);
        if (seenV_.size() < r.nv) seenV_.resize(r.nv, 0);
        touched_.clear();
        olds_.clear();
        bool conflict = false;
        for (const std::uint32_t k : dc) {
            if (k >= r.n || markC_[k]) continue;
            markC_[k] = 1;
            const std::uint16_t v = r.idx[k];
            const GpuVert val = gpuVert(g.corners[k]);
            if (seenV_[v] == 0) {
                touched_.push_back(v);
                olds_.push_back(r.buf[v]);
                r.buf[v] = val;
            } else if (std::memcmp(&r.buf[v], &val, sizeof val) != 0) {
                conflict = true;
            }
            ++seenV_[v];
        }
        for (std::size_t t = 0; t < touched_.size(); ++t) {
            const std::uint32_t v = touched_[t];
            if (seenV_[v] < r.refs[v] && std::memcmp(&r.buf[v], &olds_[t], sizeof(GpuVert)) != 0)
                conflict = true;
            seenV_[v] = 0;
        }
        for (const std::uint32_t k : dc) if (k < r.n) markC_[k] = 0;
        if (conflict) return false;
        // flushed in runs: the touched vertices sorted, a gap of 32 or less bridged
        std::sort(touched_.begin(), touched_.end());
        std::size_t i = 0;
        while (i < touched_.size()) {
            std::size_t j = i;
            while (j + 1 < touched_.size() && touched_[j + 1] - touched_[j] <= 32) ++j;
            GSPGPU_FlushDataCache(r.buf + touched_[i], (touched_[j] - touched_[i] + 1) * sizeof(GpuVert));
            i = j + 1;
        }
        return true;
    }

    const GpuVert* resident(const Geometry& g, std::size_t start, std::size_t count, std::size_t& first,
                            const std::uint16_t*& idx) {
        idx = nullptr;
        auto it = res_.find(&g);
        if (it != res_.end() && it->second.buf && it->second.rev == g.revision &&
            it->second.n == g.corners.size()) {
            it->second.drawnPass = pass_;
            idx = it->second.idx;
            return it->second.buf;
        }
        if (it != res_.end() && it->second.buf && it->second.drawnPass == pass_) {
            // changed after this pass drew it: the draw's corners through the ring
            if (used_ + count > kRingCorners) {
                if (!toldFull_) {
                    toldFull_ = true;
                    std::printf("c3d: the vertex ring is full (%zu corners) - a redrawn geometry is "
                                "skipped\n", kRingCorners);
                }
                return nullptr;
            }
            GpuVert* out = ring_ + used_;
            for (std::size_t k = 0; k < count; ++k) out[k] = gpuVert(g.corners[start + k]);
            GSPGPU_FlushDataCache(out, count * sizeof(GpuVert));
            used_ += count;
            first = 0;
            return out;
        }
        const std::size_t n = g.corners.size();
        if (it != res_.end() && it->second.buf && it->second.n == n && it->second.idx) {
            // INDEXED: the dirty corners' vertices, or the whole re-indexed
            Res& r = it->second;
            const bool partial = g.dirtyTo != 0 && g.dirtyTo == g.revision && r.rev == g.dirtyFrom;
            if (!(partial && patchIndexed(g, r))) {
                if (++r.rebuilds > 3 && !r.unindexed) {
                    r.unindexed = true;   // rewritten whole too often: corners from now on
                    std::printf("c3d: a geometry of %zu corners re-indexed whole %d times - left unindexed\n",
                                n, r.rebuilds);
                }
                if (!fillResident(g, r)) return nullptr;
            }
            r.rev = g.revision;
            r.drawnPass = pass_;
            idx = r.idx;
            return r.buf;
        }
        if (it != res_.end() && it->second.buf && it->second.n == n) {
            Res& r = it->second;
            const bool partial = g.dirtyTo != 0 && g.dirtyTo == g.revision && r.rev == g.dirtyFrom;
            if (partial) {
                // runs of consecutive corners, each written and flushed once;
                // the list is not sorted (CLAUDE.md 1, "the dirty list")
                const auto& dc = g.dirtyCorners;
                std::size_t i = 0;
                while (i < dc.size()) {
                    std::size_t j = i;
                    while (j + 1 < dc.size() && dc[j + 1] == dc[j] + 1) ++j;
                    const std::uint32_t lo = dc[i], hi = dc[j];
                    if (hi < n) {
                        for (std::uint32_t k = lo; k <= hi; ++k) r.buf[k] = gpuVert(g.corners[k]);
                        GSPGPU_FlushDataCache(r.buf + lo, (hi - lo + 1) * sizeof(GpuVert));
                    }
                    i = j + 1;
                }
            } else {
                for (std::size_t k = 0; k < n; ++k) r.buf[k] = gpuVert(g.corners[k]);
                GSPGPU_FlushDataCache(r.buf, n * sizeof(GpuVert));
            }
            r.rev = g.revision;
            r.drawnPass = pass_;
            return r.buf;
        }
        // new, or a new size
        Res& r = res_[&g];
        if (!fillResident(g, r)) return nullptr;
        r.rev = g.revision; r.drawnPass = pass_;
        g.resident.mark();
        idx = r.idx;
        return r.buf;
    }

    // A buffer let go: at once when no recorded draw of this pass reads it,
    // else when the next pass begins (`begin` waits for the GPU first).
    void retire(Res& r) {
        for (void* b : {static_cast<void*>(r.buf), static_cast<void*>(r.idx)}) {
            if (!b) continue;
            if (inFrame_ && r.drawnPass == pass_) grave_.push_back(b);
            else linearFree(b);
        }
        r.buf = nullptr;
        r.idx = nullptr;
    }

    static void geometryGone(void* ctx, const Geometry* g) {
        auto* self = static_cast<C3dRenderer*>(ctx);
        auto it = self->res_.find(g);
        if (it != self->res_.end()) {
            self->retire(it->second);
            self->res_.erase(it);
        }
        auto pt = self->pres_.find(g);
        if (pt != self->pres_.end()) {
            self->retirePose(pt->second);
            self->pres_.erase(pt);
        }
    }

    struct Report {
        long passes = 0, draws = 0;
        u64 cpuPoseTicks = 0, waitTicks = 0, frameWaitTicks = 0;
        long piped = 0, synced = 0;     // straight presents a frame behind / waited for
        u64 submitTicks = 0, residentTicks = 0, uniformTicks = 0, halfLoopTicks = 0;
        u64 beginTicks = 0, passTicks = 0;
        float cmdMax = 0.0f;
        long gpu0 = 0, cpu0 = 0;
        long calls = 0, indexed = 0;    // GPU draw calls, and of them indexed
    };
    void noteCmdBuf() { rep_.cmdMax = std::max(rep_.cmdMax, C3D_GetCmdBufUsage()); }

    // ---- THE POSED BODIES (3d) -------------------------------------------------
    // GLES's `uploadPosedGeometry`: a REST geometry changes only when the
    // model does, so its buffer is filled once per revision; each corner
    // carries its mesh's SLOT - the meshes it uses, numbered densely.
    struct PoseRes {
        GpuPoseVert* buf = nullptr;
        std::uint16_t* idx = nullptr;   // a corner's vertex in `buf`, when indexed
        std::size_t n = 0;
        std::uint64_t rev = 0;
        long drawnPass = -1;
        std::vector<std::int32_t> meshOfSlot;
        bool shimmer = false;         // a corner shimmers: the posing program cannot
    };

    PoseRes* poseResident(const Geometry& g) {
        if (g.corners.empty() || g.cornerMesh.size() != g.corners.size()) return nullptr;
        PoseRes& pr = pres_[&g];
        if (pr.buf && pr.rev == g.revision && pr.n == g.corners.size()) {
            pr.drawnPass = pass_;
            return &pr;
        }
        const std::size_t n = g.corners.size();
        retirePose(pr);
        pr.meshOfSlot.clear();
        pr.shimmer = false;
        std::unordered_map<std::int32_t, int> slotOf;
        std::vector<GpuPoseVert> rest(n);
        for (std::size_t k = 0; k < n; ++k) {
            const std::int32_t m = g.cornerMesh[k];
            auto it = slotOf.find(m);
            if (it == slotOf.end()) {
                it = slotOf.emplace(m, static_cast<int>(pr.meshOfSlot.size())).first;
                pr.meshOfSlot.push_back(m);
            }
            const Corner& c = g.corners[k];
            rest[k] = {c.x, c.y, c.z, c.u, c.v, toByte(c.r), toByte(c.g), toByte(c.b), 255,
                       static_cast<float>(it->second), c.nx, c.ny, c.nz};
            if (c.phase >= 0.0f) pr.shimmer = true;
        }
        // the slot is in the vertex, so equal bytes are one mesh's (VertexIndex)
        VertexIndex<GpuPoseVert> vi;
        const bool indexed = !noIndex_ && vi.build(n, nullptr, [&](std::size_t k) { return rest[k]; });
        const std::size_t nv = indexed ? vi.verts.size() : n;
        pr.buf = static_cast<GpuPoseVert*>(linearAlloc(nv * sizeof(GpuPoseVert)));
        pr.idx = indexed ? static_cast<std::uint16_t*>(linearAlloc(n * sizeof(std::uint16_t))) : nullptr;
        if (!pr.buf || (indexed && !pr.idx)) { retirePose(pr); return nullptr; }
        std::memcpy(pr.buf, indexed ? vi.verts.data() : rest.data(), nv * sizeof(GpuPoseVert));
        GSPGPU_FlushDataCache(pr.buf, nv * sizeof(GpuPoseVert));
        if (indexed) {
            std::memcpy(pr.idx, vi.idx.data(), n * sizeof(std::uint16_t));
            GSPGPU_FlushDataCache(pr.idx, n * sizeof(std::uint16_t));
        }
        pr.n = n;
        pr.rev = g.revision;
        pr.drawnPass = pass_;
        g.resident.mark();
        return &pr;
    }

    void retirePose(PoseRes& pr) {
        for (void* b : {static_cast<void*>(pr.buf), static_cast<void*>(pr.idx)}) {
            if (!b) continue;
            if (inFrame_ && pr.drawnPass == pass_) poseGrave_.push_back(b);
            else linearFree(b);
        }
        pr.buf = nullptr;
        pr.idx = nullptr;
    }

    // One affine a slot - the mesh's, or the identity for a corner no mesh
    // owns (`applyPose` leaves those at rest) - and the body's lights.
    void setPoseUniforms(const Draw& d, const PoseRes& pr) {
        for (std::size_t sl = 0; sl < pr.meshOfSlot.size(); ++sl) {
            const std::int32_t m = pr.meshOfSlot[sl];
            const int r = pPose_ + 3 * static_cast<int>(sl);
            if (m >= 0 && static_cast<std::size_t>(m) < d.meshPoses) {
                const float* a = d.meshPose + 12 * static_cast<std::size_t>(m);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, r + 0, a[0], a[1], a[2], a[3]);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, r + 1, a[4], a[5], a[6], a[7]);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, r + 2, a[8], a[9], a[10], a[11]);
            } else {
                C3D_FVUnifSet(GPU_VERTEX_SHADER, r + 0, 1, 0, 0, 0);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, r + 1, 0, 1, 0, 0);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, r + 2, 0, 0, 1, 0);
            }
        }
        const int nl = d.vertexLights ? std::min(d.vertexLightCount, kMaxLights) : 0;
        const bool black = d.lightsFromBlack;
        C3D_FVUnifSet(GPU_VERTEX_SHADER, pMisc_, black ? 1.0f : 0.0f, d.lightBase, 0.0f, 0.0f);
        for (int i = 0; i < kMaxLights; ++i) {
            if (i < nl) {
                const float* L = d.vertexLights + 8 * i;
                C3D_FVUnifSet(GPU_VERTEX_SHADER, pLights_ + 2 * i, L[0], L[1], L[2], 0.0f);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, pLights_ + 2 * i + 1, L[4], L[5], L[6], 0.0f);
            } else {
                C3D_FVUnifSet(GPU_VERTEX_SHADER, pLights_ + 2 * i, 0, 0, 0, 0);
                C3D_FVUnifSet(GPU_VERTEX_SHADER, pLights_ + 2 * i + 1, 0, 0, 0, 0);
            }
        }
    }

    // THE CPU FALLBACK: the draw's corners posed and lit here - the same law
    // as `posed.v.pica` (and `vertexlight.cpp`) - into this pass's ring as
    // scene corners, the shimmer phase kept for the scene program.
    const GpuVert* cpuPose(const Draw& d) {
        const Geometry& g = *d.geo;
        if (used_ + d.count > kRingCorners) {
            if (!toldFull_) {
                toldFull_ = true;
                std::printf("c3d: the vertex ring is full (%zu corners) - a body is skipped\n", kRingCorners);
            }
            return nullptr;
        }
        const bool haveMesh = g.cornerMesh.size() == g.corners.size();
        const int nl = d.vertexLights ? d.vertexLightCount : 0;
        const bool lit = nl > 0 || d.lightsFromBlack;
        GpuVert* out = ring_ + used_;
        for (std::size_t k = 0; k < d.count; ++k) {
            const Corner& c = g.corners[d.start + k];
            const std::int32_t m = haveMesh ? g.cornerMesh[d.start + k] : -1;
            float px = c.x, py = c.y, pz = c.z, nx = c.nx, ny = c.ny, nz = c.nz;
            if (m >= 0 && static_cast<std::size_t>(m) < d.meshPoses) {
                const float* a = d.meshPose + 12 * static_cast<std::size_t>(m);
                px = a[0] * c.x + a[1] * c.y + a[2] * c.z + a[3];
                py = a[4] * c.x + a[5] * c.y + a[6] * c.z + a[7];
                pz = a[8] * c.x + a[9] * c.y + a[10] * c.z + a[11];
                nx = a[0] * c.nx + a[1] * c.ny + a[2] * c.nz;
                ny = a[4] * c.nx + a[5] * c.ny + a[6] * c.nz;
                nz = a[8] * c.nx + a[9] * c.ny + a[10] * c.nz;
            }
            float col[3] = {c.r, c.g, c.b};
            if (lit) {
                if (d.lightsFromBlack) col[0] = col[1] = col[2] = d.lightBase;
                for (int i = 0; i < nl; ++i) {
                    const float* L = d.vertexLights + 8 * i;
                    const float t = -(nx * L[0] + ny * L[1] + nz * L[2]);
                    const float ti = std::clamp(t < 0.0f ? std::ceil(t) : std::floor(t), 0.0f, 255.0f);
                    for (int j = 0; j < 3; ++j)
                        col[j] = std::min(col[j] + std::floor(ti * L[4 + j] / 256.0f) / 255.0f, 1.0f);
                }
            }
            out[k] = {px, py, pz, c.u, c.v, toByte(col[0]), toByte(col[1]), toByte(col[2]), 255, c.phase};
        }
        GSPGPU_FlushDataCache(out, d.count * sizeof(GpuVert));
        used_ += d.count;
        return out;
    }

    // The program a draw needs, bound only when it changes. The two share
    // their first three uniforms (the view-projection, the texel scale, the
    // fog); what else each reads - the shimmer's wave here, the pose and the
    // lights there - lives in registers the other overwrites, so the scene's
    // is set again on the way back, and the attribute layout with it.
    void useProgram(int which) {
        if (which == boundProg_) return;
        C3D_AttrInfo* ai = C3D_GetAttrInfo();
        AttrInfo_Init(ai);
        AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);             // position
        AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);             // texels
        AttrInfo_AddLoader(ai, 2, GPU_UNSIGNED_BYTE, 4);     // the baked colour
        if (which == 1) {
            C3D_BindProgram(&pprog_);
            AttrInfo_AddLoader(ai, 3, GPU_FLOAT, 1);         // the mesh's slot
            AttrInfo_AddLoader(ai, 4, GPU_FLOAT, 3);         // the rest normal
        } else {
            C3D_BindProgram(&prog_);
            AttrInfo_AddLoader(ai, 3, GPU_FLOAT, 1);         // the shimmer phase
            if (boundProg_ == 1) setShimmer();
        }
        boundProg_ = which;
        boundBuf_ = nullptr;
    }

    // the shimmer's clock and wave (`o3de/shimmer.h`), into the scene program
    void setShimmer() {
        C3D_FVUnifSet(GPU_VERTEX_SHADER, uShim_, shimClock_, 0.0f, 0.0f, 0.0f);
        for (int i = 0; i < 32; ++i)
            C3D_FVUnifSet(GPU_VERTEX_SHADER, uWave_ + i, kShimmerWave[i] / 255.0f, 0.0f, 0.0f, 0.0f);
        // ...and the greyscale bank's word (`scene.v.pica`), which `pose`
        // overwrites too
        C3D_FVUnifSet(GPU_VERTEX_SHADER, uGrey_, 0.299f, 0.587f, 0.114f, grey_);
    }

    // THE PICA's TEXTURE LAYOUT: 8x8 tiles, the tiles row by row from the
    // BOTTOM of the image (t = 0 is the last row, as in GL), the 64 texels of
    // a tile in Morton (Z) order - x and y bits interleaved, x lowest.
    static void tile(const Texture& x, const std::uint16_t pal[256], std::vector<std::uint16_t>& out) {
        const int w = x.width, h = x.height;
        out.assign(static_cast<std::size_t>(w) * h, 0);
        const std::uint8_t* idx = x.idx.data();
        for (int y = 0; y < h; ++y) {
            const int ty = h - 1 - y;                       // the image's row y at t = ty
            for (int xx = 0; xx < w; ++xx) {
                const int tileIndex = (ty >> 3) * (w >> 3) + (xx >> 3);
                const int lx = xx & 7, ly = ty & 7;
                const int morton = (lx & 1) | ((ly & 1) << 1) | ((lx & 2) << 1) | ((ly & 2) << 2) |
                                   ((lx & 4) << 2) | ((ly & 4) << 3);
                out[static_cast<std::size_t>(tileIndex) * 64 + morton] =
                    pal[idx[static_cast<std::size_t>(y) * w + xx]];
            }
        }
    }

    void freeTextures() {
        for (auto& [key, u] : uploaded_) C3D_TexDelete(u.tex.get());
        uploaded_.clear();
        tex_.clear();
    }

    // The frame closed if open (every buffer the GPU reads was flushed out of
    // the CPU's cache where it was written).
    void closeFrame() {
        if (!inFrame_) return;
        C3D_FrameEnd(0);
        inFrame_ = false;
    }

    // The D3D render states the original changed between buckets, changed
    // here only when they differ from the last draw's.
    void setState(const Draw& d) {
        const unsigned slot = d.bucketKey & 0x3F;   // ASSETS 4b: the key's low six bits
        const bool hasTex = slot < tex_.size() && tex_[slot].tex;
        if (!stateValid_ || d.blend != blend_) {
            switch (d.blend) {
            case Blend::Opaque:
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
                C3D_DepthTest(true, GPU_LESS, GPU_WRITE_ALL);
                break;
            case Blend::Add:                                  // src + dst, saturating
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ONE, GPU_ONE);
                C3D_DepthTest(true, GPU_LESS, GPU_WRITE_COLOR);
                break;
            case Blend::Mul:                                  // dst * (1 - src)
                C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ZERO, GPU_ONE_MINUS_SRC_COLOR,
                               GPU_ZERO, GPU_ONE);
                C3D_DepthTest(true, GPU_LESS, GPU_WRITE_COLOR);
                break;
            }
            blend_ = d.blend;
        }
        if (!stateValid_ || d.cutout != cutout_) {
            C3D_AlphaTest(d.cutout, GPU_GREATER, 0x80);       // the colour key: alpha 0 dropped
            cutout_ = d.cutout;
        }
        // the fog's two exclusions (View::fog): 0x2080 none, 0x800 doubled
        const int fogKind = !view_.fog || view_.fogEnd <= view_.fogStart ? 0
                          : (d.bucketKey & 0x2080) ? 0 : (d.bucketKey & 0x800) ? 2 : 1;
        if (!stateValid_ || fogKind != fogKind_) {
            const float fk = fogKind == 2 ? 2.0f : 1.0f;
            const float fs = view_.fogStart * fk, fe = view_.fogEnd * fk;
            C3D_FVUnifSet(GPU_VERTEX_SHADER, uFog_, fe, fogKind ? 1.0f / (fe - fs) : 0.0f,
                          fogKind ? 1.0f : 0.0f, 0.0f);
            fogKind_ = fogKind;
        }
        const unsigned want = hasTex ? slot : ~1u;
        if (want != boundTex_) {
            // texels to the PICA's 0..1, by the bound texture's size
            C3D_FVUnifSet(GPU_VERTEX_SHADER, uTexScale_, hasTex ? 1.0f / tex_[slot].w : 0.0f,
                          hasTex ? 1.0f / tex_[slot].h : 0.0f, 0.0f, 0.0f);
            C3D_TexEnv* env = C3D_GetTexEnv(0);
            C3D_TexEnvInit(env);
            if (hasTex) {
                C3D_TexBind(0, tex_[slot].tex);
                C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);   // texel x colour
                C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);  // the key, for the alpha test
            } else {
                C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);    // no texture: the colour alone
                // ...and an opaque alpha (the primary's carries the fog factor)
                C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
                C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
            }
            boundTex_ = want;
        }
        stateValid_ = true;
    }

    bool ready_ = false, inFrame_ = false;
    DVLB_s* dvlb_ = nullptr;
    shaderProgram_s prog_{};
    int uMvp_ = -1, uTexScale_ = -1, uFog_ = -1, uShim_ = -1, uWave_ = -1, uGrey_ = -1;
    C3D_RenderTarget* target_ = nullptr;
    GpuVert* ring_ = nullptr;
    std::size_t used_ = 0;
    const void* boundBuf_ = nullptr;
    DVLB_s* pdvlb_ = nullptr;
    shaderProgram_s pprog_{};
    int pMisc_ = -1, pLights_ = -1, pPose_ = -1, pGrey_ = -1;
    float grey_ = 0.0f;   // View::grey, the greyscale bank (ops 150/151)
    bool posing_ = false;
    int boundProg_ = -1;
    float shimClock_ = 0.0f;
    std::unordered_map<const Geometry*, PoseRes> pres_;
    std::vector<void*> poseGrave_;
    long posedGpu_ = 0, posedCpu_ = 0;
    Report rep_;
    bool rgb565_ = false;
    u64 passStart_ = 0;             // the 16-bit experiment (`sdmc:/omk/c3d-rgb565`)
    std::vector<std::uint32_t> rowRgba_, rowPad_;     // a row in RGBA byte order (toRgb565Row)
    std::vector<std::uint16_t> row565_;
    std::unordered_map<const Geometry*, Res> res_;
    std::vector<void*> grave_;
    bool noIndex_ = false;              // `sdmc:/omk/c3d-arrays`: every draw from the corners
    std::vector<std::uint8_t> markC_;   // a partial update's scratch: corners seen
    std::vector<std::uint16_t> seenV_;  // ...and the corners seen on a vertex
    std::vector<std::uint32_t> touched_;
    std::vector<GpuVert> olds_;
    long pass_ = 0;
    std::uint32_t* read_ = nullptr;
    std::uint32_t* small_[2] = {nullptr, nullptr};   // the halved picture (3c), linear: two, the pipeline's
    int halfCur_ = 0;                   // the one the next straight present's transfer writes
    long lastHalfPass_ = -10;           // the pass the last straight present ended
    bool syncPresent_ = false;          // `sdmc:/omk/c3d-sync`: every straight present waited for
    long straight_ = 0;
    int w_ = 0, h_ = 0;          // the frame
    int tw_ = 0, th_ = 0;        // the target: the frame rounded up to multiples of 8
    std::vector<Slot> tex_;
    std::map<std::tuple<const std::uint8_t*, const std::uint8_t*, int, int>, Uploaded> uploaded_;
    View view_;
    bool flipX_ = false;
    Surface fb_{1, 1, 0};
    bool readDone_ = false, toldRead_ = false, toldFull_ = false, toldFogColour_ = false, toldNoMem_ = false;
    RasterStats st_;
    bool stateValid_ = false, cutout_ = false;
    int filter_ = 0;
    Blend blend_ = Blend::Opaque;
    int fogKind_ = -1;
    int cull_ = -1;
    unsigned boundTex_ = ~0u;
};

}  // namespace

Renderer* makeC3dRenderer() { return new C3dRenderer(); }

void c3dReport(Renderer* r, long frame) {
    if (r) static_cast<C3dRenderer*>(r)->report(frame);
}

bool c3dPresentHalf(Renderer* r, int vy, int vh, Surface& screen) {
    return r && static_cast<C3dRenderer*>(r)->presentHalf(vy, vh, screen);
}

}  // namespace omk

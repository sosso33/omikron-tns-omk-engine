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
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>

// the vertex shader, assembled by picasso and embedded by bin2s (Makefile)
extern "C" {
extern const u8 scene_shbin[];
extern const u32 scene_shbin_size;
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
        for (GpuVert* b : grave_) linearFree(b);
        if (target_) C3D_RenderTargetDelete(target_);
        if (ring_) linearFree(ring_);
        if (read_) linearFree(read_);
        if (dvlb_) { shaderProgramFree(&prog_); DVLB_Free(dvlb_); }
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
            if (uMvp_ < 0 || uTexScale_ < 0 || uFog_ < 0 || uShim_ < 0 || uWave_ < 0) {
                std::printf("c3d: the vertex shader lacks a uniform (mvp %d texscale %d fogp %d "
                            "shim %d wave %d)\n", uMvp_, uTexScale_, uFog_, uShim_, uWave_);
                return false;
            }
            ring_ = static_cast<GpuVert*>(linearAlloc(kRingCorners * sizeof(GpuVert)));
            if (!ring_) { std::printf("c3d: no linear memory for the vertex ring\n"); return false; }
            addGeometryListener(&C3dRenderer::geometryGone, this);
            ready_ = true;
        }
        if (target_) { C3D_RenderTargetDelete(target_); target_ = nullptr; }
        if (read_) { linearFree(read_); read_ = nullptr; }
        target_ = C3D_RenderTargetCreate(w, h, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
        if (!target_) {
            std::printf("c3d: no %dx%d render target (VRAM)\n", w, h);
            return false;
        }
        read_ = static_cast<std::uint32_t*>(linearAlloc(static_cast<std::size_t>(w) * h * 4));
        if (!read_) { std::printf("c3d: no linear memory for the readback\n"); return false; }
        OMK_GPU_ALLOC("render targets", omk::prof::gpuKey(omk::prof::kGlRenderbuffer, 1u), 8LL * w * h);
        w_ = w; h_ = h;
        fb_ = Surface(w, h, 0);
        readDone_ = false;
        std::printf("c3d: citro3d on the PICA200, a %dx%d RGBA8 target with 24-bit depth in VRAM, "
                    "%u KB of VRAM left; the GPU transforms (resident geometry)\n",
                    w, h, static_cast<unsigned>(vramSpaceFree() / 1024));
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
            const auto key = std::make_tuple(x.idx.data(), x.width, x.height);
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
                hit = uploaded_.emplace(key, Uploaded{std::move(tex), x.idx, 2LL * x.width * x.height,
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
        closeFrame();
        view_ = v;
        RCamera cam = v.cam;
        cam.w = v.letterboxed() ? v.vw : w_;
        cam.h = v.letterboxed() ? v.vh : h_;
        flipX_ = cam.flipX;

        C3D_FrameBegin(0);                       // waits for the GPU's last pass, not for a VBlank
        inFrame_ = true;
        ++pass_;
        // what a destroyed geometry left while the last pass could still read it
        for (GpuVert* b : grave_) linearFree(b);
        grave_.clear();
        C3D_RenderTargetClear(target_, C3D_CLEAR_ALL, 0x000000FF, 0x00FFFFFF);
        C3D_FrameDrawOn(target_);
        // the picture in the TOP-LEFT cam.w x cam.h, as the reference draws
        // it; the PICA's viewport origin is the bottom-left, as GL's
        C3D_SetViewport(0, static_cast<u32>(h_ - cam.h), static_cast<u32>(cam.w), static_cast<u32>(cam.h));
        C3D_BindProgram(&prog_);
        C3D_AttrInfo* ai = C3D_GetAttrInfo();
        AttrInfo_Init(ai);
        AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);             // position, world
        AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);             // texels
        AttrInfo_AddLoader(ai, 2, GPU_UNSIGNED_BYTE, 4);     // the baked colour
        AttrInfo_AddLoader(ai, 3, GPU_FLOAT, 1);             // the shimmer phase
        boundBuf_ = nullptr;
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
        // the shimmer's clock and wave (`o3de/shimmer.h`)
        C3D_FVUnifSet(GPU_VERTEX_SHADER, uShim_,
                      std::floor(std::floor(v.shimmerClock) / 4.0f), 0.0f, 0.0f, 0.0f);
        for (int i = 0; i < 32; ++i)
            C3D_FVUnifSet(GPU_VERTEX_SHADER, uWave_ + i, kShimmerWave[i] / 255.0f, 0.0f, 0.0f, 0.0f);

        cull_ = -1;
        stateValid_ = false;
        boundTex_ = ~0u;
        st_ = RasterStats{};
        readDone_ = false;
        if ((v.fogColour[0] | v.fogColour[1] | v.fogColour[2]) && v.fog && !toldFogColour_) {
            // the shipped game's fog is BLACK (renderer.h, View::fog); one
            // mode colours it, and this backend keeps its fade, not its colour
            toldFogColour_ = true;
            std::printf("c3d: a coloured fog - drawn as its fade toward black\n");
        }
    }

    void submit(const Draw& d) override {
        if (!inFrame_ || !d.geo || d.count < 3) return;
        const Geometry& g = *d.geo;
        if (d.start + d.count > g.corners.size()) return;
        // the corners on the GPU: the geometry's own buffer, or this pass's
        // ring when it changed after being drawn in it; `first` is the
        // buffer's index of the draw's first corner
        std::size_t first = d.start;
        const GpuVert* buf = resident(g, d.start, d.count, first);
        if (!buf) return;
        setState(d);
        if (buf != boundBuf_) {
            C3D_BufInfo* bi = C3D_GetBufInfo();
            BufInfo_Init(bi);
            BufInfo_Add(bi, buf, sizeof(GpuVert), 4, 0x3210);
            boundBuf_ = buf;
        }
        st_.triangles += static_cast<long>(d.count / 3);
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
            C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(first + (i - d.start)), static_cast<int>(j - i));
            i = j;
        }
        st_.drawn += static_cast<long>(d.count / 3);
    }

    void end() override {
        // the frame stays open: `readback` reads it inside the frame (see
        // there), and the next `begin` closes one nobody read
    }

    const Surface& readback() override {
        if (readDone_) return fb_;               // idempotent (renderer.h)
        if (inFrame_) {
            // INSIDE the frame: `C3D_SyncDisplayTransfer` then splits it,
            // waits for the GPU to finish what was queued, and transfers -
            // out of the frame it would wait on citro3d's frame pacer first
            C3D_SyncDisplayTransfer(static_cast<u32*>(target_->frameBuf.colorBuf), GX_BUFFER_DIM(w_, h_),
                                    reinterpret_cast<u32*>(read_), GX_BUFFER_DIM(w_, h_),
                                    GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                                    GX_TRANSFER_RAW_COPY(0) |
                                    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                    GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                                    GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
            closeFrame();
        }
        GSPGPU_InvalidateDataCache(read_, static_cast<u32>(w_) * h_ * 4);
        // A pixel is a word 0xRRGGBBAA, and the transfer hands the rows
        // TOP-DOWN already (the first Azahar frame, 2026-10-06, read them
        // bottom-up and came out upside down, everything else right). Into
        // the frame dithered as the reference is.
        for (int y = 0; y < h_; ++y) {
            const std::uint32_t* row = read_ + static_cast<std::size_t>(y) * w_;
            for (int x = 0; x < w_; ++x) {
                const std::uint32_t p = row[x];
                const int r = static_cast<int>(p >> 24), g = static_cast<int>((p >> 16) & 0xFF),
                          b = static_cast<int>((p >> 8) & 0xFF);
                fb_.set(x, y, view_.dither ? quantise888Dither(r, g, b, x, y) : quantise888(r, g, b));
            }
        }
        readDone_ = true;
        if (!toldRead_) {
            toldRead_ = true;
            const std::uint32_t m = read_[static_cast<std::size_t>(h_ / 2) * w_ + w_ / 2];
            std::printf("c3d: first readback %dx%d, %ld triangles, %zu geometries resident, centre "
                        "pixel %u %u %u\n", w_, h_, st_.triangles, res_.size(),
                        static_cast<unsigned>(m >> 24), static_cast<unsigned>((m >> 16) & 0xFF),
                        static_cast<unsigned>((m >> 8) & 0xFF));
        }
        return fb_;
    }

    RasterStats stats() const override { return st_; }
    const char* name() const override { return "citro3d (PICA200)"; }

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
    struct Uploaded { std::unique_ptr<C3D_Tex> tex; PixelBuffer keep; long long bytes = 0; bool used = false; };

    static bool noCull() { static const bool n = std::getenv("OMK_NO_CULL") != nullptr; return n; }

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
    };

    const GpuVert* resident(const Geometry& g, std::size_t start, std::size_t count, std::size_t& first) {
        auto it = res_.find(&g);
        if (it != res_.end() && it->second.buf && it->second.rev == g.revision &&
            it->second.n == g.corners.size()) {
            it->second.drawnPass = pass_;
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
        auto* buf = static_cast<GpuVert*>(linearAlloc(n * sizeof(GpuVert)));
        if (!buf) {
            if (!toldNoMem_) {
                toldNoMem_ = true;
                std::printf("c3d: no linear memory for a geometry of %zu corners (%u KB left) - not "
                            "drawn\n", n, static_cast<unsigned>(linearSpaceFree() / 1024));
            }
            return nullptr;
        }
        for (std::size_t k = 0; k < n; ++k) buf[k] = gpuVert(g.corners[k]);
        GSPGPU_FlushDataCache(buf, n * sizeof(GpuVert));
        if (it != res_.end()) retire(it->second);
        Res& r = res_[&g];
        r.buf = buf; r.n = n; r.rev = g.revision; r.drawnPass = pass_;
        g.resident.mark();
        return buf;
    }

    // A buffer let go: at once when no recorded draw of this pass reads it,
    // else when the next pass begins (`begin` waits for the GPU first).
    void retire(Res& r) {
        if (!r.buf) return;
        if (inFrame_ && r.drawnPass == pass_) grave_.push_back(r.buf);
        else linearFree(r.buf);
        r.buf = nullptr;
    }

    static void geometryGone(void* ctx, const Geometry* g) {
        auto* self = static_cast<C3dRenderer*>(ctx);
        auto it = self->res_.find(g);
        if (it == self->res_.end()) return;
        self->retire(it->second);
        self->res_.erase(it);
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
                C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);   // no texture: the colour alone
            }
            boundTex_ = want;
        }
        stateValid_ = true;
    }

    bool ready_ = false, inFrame_ = false;
    DVLB_s* dvlb_ = nullptr;
    shaderProgram_s prog_{};
    int uMvp_ = -1, uTexScale_ = -1, uFog_ = -1, uShim_ = -1, uWave_ = -1;
    C3D_RenderTarget* target_ = nullptr;
    GpuVert* ring_ = nullptr;
    std::size_t used_ = 0;
    const GpuVert* boundBuf_ = nullptr;
    std::unordered_map<const Geometry*, Res> res_;
    std::vector<GpuVert*> grave_;
    long pass_ = 0;
    std::uint32_t* read_ = nullptr;
    int w_ = 0, h_ = 0;
    std::vector<Slot> tex_;
    std::map<std::tuple<const std::uint8_t*, int, int>, Uploaded> uploaded_;
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

}  // namespace omk

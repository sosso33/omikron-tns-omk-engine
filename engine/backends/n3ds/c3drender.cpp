// SPDX-License-Identifier: GPL-3.0-or-later
// The citro3d backend - see c3drender.h. Its per-triangle work is the GL1
// backend's (`backends/gl1/gl1render.cpp`), kept line for line where it can
// be, so a difference between the two frames is the API's and not a second
// reading of the laws.
#include "c3drender.h"

#include "o3de/shimmer.h"
#include "platform/profile.h"
#include "ui/surface.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// the vertex shader, assembled by picasso and embedded by bin2s (Makefile)
extern "C" {
extern const u8 scene_shbin[];
extern const u32 scene_shbin_size;
}

namespace omk {
namespace {

// One vertex as the shader reads it: clip-space position, colour as four
// bytes, texture coordinates normalised.
struct GpuVertex {
    float x, y, z, w;
    std::uint8_t r, g, b, a;
    float u, v;
};
static_assert(sizeof(GpuVertex) == 28, "the attribute stride below");

// The vertices of a frame, in linear memory the GPU reads. A frame's draws
// are appended and drawn by offset; `C3D_FrameBegin` waits for the previous
// frame's GPU work, so one buffer is reused frame after frame.
constexpr std::size_t kRingVertices = 160 * 1024;      // 4.4 MB

class C3dRenderer final : public Renderer {
public:
    ~C3dRenderer() override {
        if (!ready_) return;
        closeFrame();
        freeTextures();
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
            ring_ = static_cast<GpuVertex*>(linearAlloc(kRingVertices * sizeof(GpuVertex)));
            if (!ring_) { std::printf("c3d: no linear memory for the vertex ring\n"); return false; }
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
                    "%u KB of VRAM left\n", w, h, static_cast<unsigned>(vramSpaceFree() / 1024));
        return true;
    }

    void setTextures(std::span<const Texture> t) override {
        closeFrame();                            // a texture in use is never freed under the GPU
        freeTextures();
        tex_.assign(t.size(), C3dTex{});
        long long bytes = 0;
        std::vector<std::uint16_t> tiled;
        for (std::size_t i = 0; i < t.size(); ++i) {
            const Texture& x = t[i];
            if (!x.hasPixels()) continue;
            if (!C3D_TexInit(&tex_[i].tex, static_cast<u16>(x.width), static_cast<u16>(x.height),
                             GPU_RGBA5551)) {
                std::printf("c3d: texture %zu (%s, %dx%d) has no memory - drawn untextured\n",
                            i, x.name.c_str(), x.width, x.height);
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
            C3D_TexUpload(&tex_[i].tex, tiled.data());
            C3D_TexFlush(&tex_[i].tex);
            C3D_TexSetWrap(&tex_[i].tex, GPU_REPEAT, GPU_REPEAT);
            const GPU_TEXTURE_FILTER_PARAM f = filter_ ? GPU_LINEAR : GPU_NEAREST;
            C3D_TexSetFilter(&tex_[i].tex, f, f);
            tex_[i].ok = true;
            tex_[i].w = static_cast<float>(x.width);
            tex_[i].h = static_cast<float>(x.height);
            bytes += 2LL * x.width * x.height;
        }
        std::printf("c3d: %zu textures as RGBA5551, %lld KB, %u KB of linear memory left\n",
                    t.size(), bytes / 1024, static_cast<unsigned>(linearSpaceFree() / 1024));
        boundTex_ = ~0u;
    }

    void begin(const View& v) override {
        closeFrame();
        view_ = v;
        RCamera cam = v.cam;
        cam.w = v.letterboxed() ? v.vw : w_;
        cam.h = v.letterboxed() ? v.vh : h_;
        flipX_ = cam.flipX;
        cameraBasis(cam, bs_, bu_, bf_, th_, tv_);
        for (int k = 0; k < 3; ++k) eye_[k] = cam.eye[k];
        camW_ = cam.w; camH_ = cam.h;

        C3D_FrameBegin(0);                       // waits for the GPU's last frame, not for a VBlank
        inFrame_ = true;
        C3D_RenderTargetClear(target_, C3D_CLEAR_ALL, 0x000000FF, 0x00FFFFFF);
        C3D_FrameDrawOn(target_);
        // the picture in the TOP-LEFT cam.w x cam.h, as the reference draws
        // it; the PICA's viewport origin is the bottom-left, as GL's
        C3D_SetViewport(0, static_cast<u32>(h_ - cam.h), static_cast<u32>(cam.w), static_cast<u32>(cam.h));
        C3D_BindProgram(&prog_);
        C3D_AttrInfo* ai = C3D_GetAttrInfo();
        AttrInfo_Init(ai);
        AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 4);             // position, clip space
        AttrInfo_AddLoader(ai, 1, GPU_UNSIGNED_BYTE, 4);     // colour
        AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 2);             // texture coordinates
        C3D_BufInfo* bi = C3D_GetBufInfo();
        BufInfo_Init(bi);
        BufInfo_Add(bi, ring_, sizeof(GpuVertex), 3, 0x210);
        used_ = 0;
        // the D3D device's fixed states (`sub_4638C0`): depth tested, strict
        // - the first of two equal faces wins - and culled on the CPU
        C3D_CullFace(GPU_CULL_NONE);
        // z/w runs 0 at the near cut to -1 far away (`submit`); the depth map
        // turns that into 0..1, increasing away from the eye
        C3D_DepthMap(true, -1.0f, 0.0f);
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
        setState(d);
        const unsigned slot = d.bucketKey & 0x3F;
        const bool hasTex = slot < tex_.size() && tex_[slot].ok;
        const float su = hasTex ? 1.0f / tex_[slot].w : 0.0f, sv = hasTex ? 1.0f / tex_[slot].h : 0.0f;

        // Per triangle, the GL1 backend's own loop: into view space, the near
        // cut, the single-sided cull, the screen edges, then CLIP-SPACE
        // positions - (x w, y w, z, w) with z = near - w, so z/w runs from 0
        // at the near cut toward -1, inside the PICA's [-w, 0] clip range.
        const std::size_t nCorner = d.count - d.count % 3;
        const bool haveCull = g.cornerCull.size() == g.corners.size();
        const std::size_t first = used_;
        for (std::size_t t = 0; t < nCorner; t += 3) {
            ++st_.triangles;
            Vtx in[3];
            for (int k = 0; k < 3; ++k) {
                const Corner& c = g.corners[d.start + t + static_cast<std::size_t>(k)];
                const float dx = c.x - eye_[0], dy = c.y - eye_[1], dz = c.z - eye_[2];
                in[k].v[0] = dx * bs_[0] + dy * bs_[1] + dz * bs_[2];
                in[k].v[1] = dx * bu_[0] + dy * bu_[1] + dz * bu_[2];
                in[k].v[2] = dx * bf_[0] + dy * bf_[1] + dz * bf_[2];
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
            if (haveCull && g.cornerCull[d.start + t] != 0 && !noCull()) {
                float cx[3], cy[3];
                for (int k = 0; k < 3; ++k) {
                    cx[k] = camW_ * 0.5f * (1.0f + (poly[k].v[0] / poly[k].v[2]) / th_);
                    cy[k] = camH_ * 0.5f * (1.0f - (poly[k].v[1] / poly[k].v[2]) / tv_);
                }
                const float area = (cx[1] - cx[0]) * (cy[2] - cy[0]) - (cx[2] - cx[0]) * (cy[1] - cy[0]);
                if ((flipX_ ? -area : area) > 0.0f) { ++st_.culled; continue; }
            }
            Vtx buf[2][8];
            int nb = np;
            for (int k = 0; k < np; ++k) buf[0][k] = poly[k];
            const float gx = th_ * 1.01f, gy = tv_ * 1.01f;
            int cur = 0;
            for (int plane = 0; plane < 4 && nb >= 3; ++plane) {
                const auto dist = [&](const Vtx& q) {
                    switch (plane) {
                    case 0:  return q.v[2] * gx - q.v[0];
                    case 1:  return q.v[2] * gx + q.v[0];
                    case 2:  return q.v[2] * gy - q.v[1];
                    default: return q.v[2] * gy + q.v[1];
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
            if (used_ + static_cast<std::size_t>(np - 2) * 3 > kRingVertices) {
                if (!toldFull_) {
                    toldFull_ = true;
                    std::printf("c3d: the vertex ring is full (%zu vertices) - the rest of the frame is not drawn\n",
                                kRingVertices);
                }
                break;
            }
            const Vtx* out = buf[cur];
            float sx[8], sy[8];
            for (int k = 0; k < np; ++k) {
                sx[k] = camW_ * 0.5f * (1.0f + (out[k].v[0] / out[k].v[2]) / th_);
                sy[k] = camH_ * 0.5f * (1.0f - (out[k].v[1] / out[k].v[2]) / tv_);
            }
            ++st_.drawn;
            for (int k = 1; k + 1 < np; ++k) {
                const int idx[3] = {0, k, k + 1};
                for (int j : idx) {
                    const Vtx& p = out[j];
                    const float w = p.v[2];
                    GpuVertex& o = ring_[used_++];
                    o.x = (2.0f * sx[j] / camW_ - 1.0f) * w;
                    o.y = (1.0f - 2.0f * sy[j] / camH_) * w;
                    o.z = kNearCut - w;
                    o.w = w;
                    float a = 0.0f;
                    if (fogKind_ != 0 && w > fogStart_) {
                        const float f = (fogEnd_ - w) / (fogEnd_ - fogStart_);
                        a = 1.0f - (f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f));
                    }
                    o.r = toByte(p.c[0] * (1.0f - a));
                    o.g = toByte(p.c[1] * (1.0f - a));
                    o.b = toByte(p.c[2] * (1.0f - a));
                    o.a = 255;
                    o.u = p.u * su;
                    o.v = p.t * sv;
                }
            }
        }
        if (used_ > first)
            C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(first), static_cast<int>(used_ - first));
    }

    void end() override {
        // the frame stays open: `readback` reads it inside the frame (see
        // there), and the next `begin` closes one nobody read
    }

    const Surface& readback() override {
        if (readDone_) return fb_;               // idempotent (renderer.h)
        if (inFrame_) {
            GSPGPU_FlushDataCache(ring_, used_ * sizeof(GpuVertex));
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
            std::printf("c3d: first readback %dx%d, %ld triangles (%ld drawn, %zu vertices), centre "
                        "pixel %u %u %u\n", w_, h_, st_.triangles, st_.drawn, used_,
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
        for (C3dTex& t : tex_) if (t.ok) C3D_TexSetFilter(&t.tex, f, f);
        return mode <= 1;
    }

private:
    struct C3dTex { C3D_Tex tex{}; bool ok = false; float w = 1, h = 1; };
    struct Vtx { float v[3]; float c[3]; float u, t; };
    static Vtx lerp(const Vtx& a, const Vtx& b, float t) {
        Vtx o;
        for (int k = 0; k < 3; ++k) { o.v[k] = a.v[k] + (b.v[k] - a.v[k]) * t; o.c[k] = a.c[k] + (b.c[k] - a.c[k]) * t; }
        o.u = a.u + (b.u - a.u) * t; o.t = a.t + (b.t - a.t) * t;
        return o;
    }
    static bool noCull() { static const bool n = std::getenv("OMK_NO_CULL") != nullptr; return n; }
    static std::uint8_t toByte(float v) {
        const float x = v * 255.0f + 0.5f;
        return static_cast<std::uint8_t>(x <= 0.0f ? 0.0f : (x >= 255.0f ? 255.0f : x));
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
        for (C3dTex& t : tex_) if (t.ok) C3D_TexDelete(&t.tex);
        tex_.clear();
    }

    // The frame closed if open: the vertices the GPU will read flushed out of
    // the CPU's cache first.
    void closeFrame() {
        if (!inFrame_) return;
        GSPGPU_FlushDataCache(ring_, used_ * sizeof(GpuVertex));
        C3D_FrameEnd(0);
        inFrame_ = false;
    }

    // The D3D render states the original changed between buckets, changed
    // here only when they differ from the last draw's.
    void setState(const Draw& d) {
        const unsigned slot = d.bucketKey & 0x3F;   // ASSETS 4b: the key's low six bits
        const bool hasTex = slot < tex_.size() && tex_[slot].ok;
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
        const int fogKind = !view_.fog || view_.fogEnd <= view_.fogStart ? 0
                          : (d.bucketKey & 0x2080) ? 0 : (d.bucketKey & 0x800) ? 2 : 1;
        fogKind_ = fogKind;
        const float fk = fogKind == 2 ? 2.0f : 1.0f;
        fogStart_ = view_.fogStart * fk;
        fogEnd_ = view_.fogEnd * fk;
        const unsigned want = hasTex ? slot : ~1u;
        if (want != boundTex_) {
            C3D_TexEnv* env = C3D_GetTexEnv(0);
            C3D_TexEnvInit(env);
            if (hasTex) {
                C3D_TexBind(0, &tex_[slot].tex);
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
    C3D_RenderTarget* target_ = nullptr;
    GpuVertex* ring_ = nullptr;
    std::size_t used_ = 0;
    std::uint32_t* read_ = nullptr;
    int w_ = 0, h_ = 0;
    std::vector<C3dTex> tex_;
    View view_;
    bool flipX_ = false;
    Surface fb_{1, 1, 0};
    bool readDone_ = false, toldRead_ = false, toldFull_ = false, toldFogColour_ = false;
    RasterStats st_;
    bool stateValid_ = false, cutout_ = false;
    int filter_ = 0;
    Blend blend_ = Blend::Opaque;
    int fogKind_ = -1;
    float fogStart_ = 0, fogEnd_ = 0;
    unsigned boundTex_ = ~0u;
    float bs_[3]{}, bu_[3]{}, bf_[3]{}, th_ = 1, tv_ = 1, eye_[3]{};
    int camW_ = 1, camH_ = 1;
};

}  // namespace

Renderer* makeC3dRenderer() { return new C3dRenderer(); }

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
// THE LIVE RENDERER - Vulkan, through MoltenVK on macOS.
//
// `PORTING` A1 puts two implementations behind one boundary: a software
// rasterizer that `verify.py` checks, and this, which makes the replica
// playable. `PORTING` A2 puts that boundary at the DECISION level, and the
// rule it states is the one this file is written against - **a backend
// receives decisions and turns them into API calls; it never makes one.**
//
// What that means concretely, and each of these is a decision arriving from
// `Draw` rather than a choice made here:
//
//   * the ORDER. Submissions are drawn in the order given, which is
//     `Render_FlushBuckets` walking 0x4000 buckets ascending - opaque, then
//     additive, then multiply, by material within each. Nothing here sorts by
//     depth, texture or state, however much a GPU would prefer it. The Anekbah
//     signs are the standing proof that this data has coincident faces whose
//     tie-break IS that order.
//   * the TEXTURE. `bucketKey & 0x3F` - the key's low six bits are the
//     material's runtime slot (ASSETS 4b). This binds what that says and does
//     not resolve a texture any other way.
//   * the BLEND. Three pipelines, one per `Blend`, built from the mesh flags
//     the loader decoded: `0x1000|0x2000` additive, `0x1000|0x4000` multiply.
//   * the CUTOUT. Flag `0x800`, a COLOUR KEY on black (`SetRenderState(27,1)`),
//     passed to the shader as a flag and discarded there. Not alpha.
//   * the BACK-FACE CULL. `Raster_DrawTriangles` sets D3DCULL_NONE, so the
//     rasterizer state says `VK_CULL_MODE_NONE` - but the engine has already
//     dropped every single-sided face seen from behind, in software, in
//     `Render_SubmitMesh` (`geom3do.h` `kTwoSided`). Here that is a push
//     constant per run of `cornerCull` and a discard on `gl_FrontFacing`, so
//     no pipeline is doubled.
//   * the CONVENTIONS. The view-projection is built from `cameraBasis`, the
//     software rasterizer's own - world up is (0,-1,0) because the game's Y
//     points DOWN, `hfovDeg` is HORIZONTAL with the vertical following from
//     the frame's shape. Those are the two errors that laying a wireframe over
//     a real screenshot corrected, and re-deriving them here would be a fresh
//     chance to get either wrong where the only symptom is a plausible
//     picture.
//
// **It renders OFFSCREEN and can read back.** No swapchain: the target is a
// VkImage, and `readback()` copies it to an RGB565 `Surface` - the same type
// the software renderer returns. That is what makes the pair differenceable
// rather than merely parallel, and this repo has spent two sessions learning
// what an unverifiable render costs. Presentation is the frontend's job; it
// already uploads a `Surface`.
//
// **What must NOT be claimed of a GPU frame.** It will never be pixel-equal to
// the software loop and is not meant to be: the fill rule, the interpolation
// precision and the texture filter are the driver's. That is `PORTING` B5's
// argument about a captured frame, and it applies here for the same reason -
// so the two are compared by SILHOUETTE and COVERAGE, with the same instrument
// `verify.py: engine silhouette` uses against the engine's own captures.
//
// A8: this is a system dependency and it is OPTIONAL. `make` and the whole
// suite must pass on a machine with no Vulkan SDK, so nothing under `src/`
// includes this and the Makefile builds it only when pkg-config finds vulkan.
// Every Vulkan create-info here is written `VkXxxInfo x{VK_STRUCTURE_TYPE_...}`:
// the type set, every other field value-initialised to zero - the API's own
// idiom, which `-Wmissing-field-initializers` reports once per struct.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include <vulkan/vulkan.h>
#include "platform/profile.h"

#include "o3de/renderer.h"
#include "o3de/depthtie.h"
#include "o3de/drawcore.h"
#include "o3de/vertexlight.h"
#include "o3de/texpack.h"
#include "o3de/colourgrade.h"
#include "o3de/lighttiles.h"
#include "o3de/fsrcon.h"
#include "ui/overlay.h"
#include "platform/events.h"
#include "platform/env.h"
#include "platform/threads.h"

#include <algorithm>
#include <chrono>
#include <array>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <map>
#include <tuple>
#include <span>
#include <string>
#include <vector>

namespace {

const uint32_t kVert[] =
#include "scene.vert.inc"
;
const uint32_t kFrag[] =
#include "scene.frag.inc"
;
// ...and with the TILED point lights (`o3de/lighttiles.h`): its own module,
// bound only on a frame whose tiles are on (`scene.frag`, -DOMK_TILED)
const uint32_t kFragTiled[] =
#include "scene_tiled.frag.inc"
;
// ...and both with the CUTOUT's late depth (`scene.frag` OMK_LATE_DEPTH): the
// opaque cutout draws' modules
const uint32_t kFragCut[] =
#include "scene_cut.frag.inc"
;
const uint32_t kFragTiledCut[] =
#include "scene_tiled_cut.frag.inc"
;
const uint32_t kFsqVert[] =
#include "fsq.vert.inc"
;
const uint32_t kFsqFrag[] =
#include "fsq.frag.inc"
;
// The present pass: the adventure frame's dither and 565 expansion on the GPU
// (todo/optimization.md step 4b).
const uint32_t kPresentFrag[] =
#include "present.frag.inc"
;
struct PresentPush { int32_t vy, vh, w, dither, ss, rcas; uint32_t rcasCon; int32_t raw; };
// The menus' cloud under the interface (`under.frag`)
const uint32_t kUnderFrag[] =
#include "under.frag.inc"
;
struct UnderPush { int32_t W, H, uw, uh, filter; };
// THE UPSCALE (`todo/remaster.md` A6): AMD FSR 1's EASU, vendored (MIT,
// `third_party/ffx_fsr1.h`); its RCAS is in the present pass above
const uint32_t kEasuFrag[] =
#include "easu.frag.inc"
;
// THE REMASTER'S POST-PASS (`todo/remaster.md` step 2): bloom and grade.
const uint32_t kPostFrag[] =
#include "post.frag.inc"
;
struct PostPush { float dstInv[2], srcInv[2], dir[2]; float strength; int32_t mode; int32_t lutSize; int32_t ao; float halo; };
// THE AMBIENT OCCLUSION (`todo/remaster.md` A4): from the frame's depth, half
// size, plain and multisampled
const uint32_t kAoFrag[] =
#include "ao.frag.inc"
;
const uint32_t kAoMsFrag[] =
#include "ao_ms.frag.inc"
;
constexpr float kAoRadius = 60.0f;   // the occlusion's reach, game units (~60 cm): contact, not shade
struct AoPush { float view[4], depth[4]; float radius, strength; int32_t mode, pad; float srcInv[2]; };
// The DEPTH-ONLY pass of the mapped shadow (`todo/enhancements.md` 6).
const uint32_t kShadowVert[] =
#include "shadow.vert.inc"
;

// The map's side. 1024 is a shadow of a handful of CHARACTERS over a slab a
// few metres across, so a texel is centimetres.
constexpr uint32_t kShadowSide = 1024;

// Set 1, binding 0 - per FRAME, and it must match `scene.frag`'s block.
// Set 1, binding 2 - the LIGHTS, per frame. std140: an array of structs
// aligns every member to 16 bytes, which three vec4s already are.
struct GpuLightStd140 {
    float posA[4];      // xyz position, w outer radius
    float dirB[4];      // xyz direction, w inner radius
    float colourI[4];   // rgb colour 0..1, a intensity
};
struct LightUbo {
    GpuLightStd140 l[omk::View::kMaxGpuLights];   // 8 the set's, the rest the remaster's points
    int32_t count;
    float   base;      // View::litBase, in the std140 padding that was here
    int32_t grey;      // View::grey, the greyscale bank (ops 150/151) - padding too
    int32_t tiles;     // 1: the TILED point lights (`o3de/lighttiles.h`) are bound - padding once
    // THE WATER's Fresnel (`todo/remaster.md` A3): the eye, at 3088. Written by
    // every `begin`, and the frame's own pass is the last one - the only one
    // that reads it (the reflection pass draws no water)
    float   eye[4];
    // THE RELIEF (`View::relief`), at 3104: read by the TILED module alone
    float   fx[4];
    // THE SAND'S DUST (`View::dust`, the restored weather's, the remaster's), at
    // 3120: strength, the drift's x and z, the distance scale; its colour at 3136
    float   dust[4];
    float   dustColour[4];
    // THE RAIN'S VEILS (`View::veil`), at 3152: strength, the fall's offset,
    // the distance scale; their colour at 3168, and the gust's lean at 3180 (`View::veilGust`)
    float   veil[4];
    float   veilColour[4];
    // THE LIGHTS' STREAKS on the wet floors (`View::streakLights`), at 3184:
    // x how many, y their strength (`View::streaks`); the lights at 3200
    float   streakN[4];
    GpuLightStd140 streak[omk::View::kMaxStreakLights];
};
// the offsets `scene.frag`'s block reads them at (std140: 64 lights of 48
// bytes, four scalars, then the vec4s)
static_assert(offsetof(LightUbo, eye) == 3088 && offsetof(LightUbo, fx) == 3104 &&
              offsetof(LightUbo, dust) == 3120 && offsetof(LightUbo, dustColour) == 3136 &&
              offsetof(LightUbo, veil) == 3152 && offsetof(LightUbo, veilColour) == 3168 &&
              offsetof(LightUbo, streakN) == 3184 && offsetof(LightUbo, streak) == 3200,
              "the Lights block's layout is scene.frag's");

struct ShadowUbo {
    float lightMvp[16];
    float strength;    // 0 = no shadow this frame, which is the default
    float texel;       // 1 / kShadowSide
    float bias;
    // THE SHIMMER's clock (`o3de/shimmer.h`), which is per FRAME like the rest
    // of this block and had a spare float sitting here for the std140
    // alignment. Not the shadow's business, but this is the frame's uniform.
    float shimmer;
};

#define VKCHECK(x, what)                                                     \
    do {                                                                     \
        const VkResult r_ = (x);                                             \
        if (r_ != VK_SUCCESS) {                                              \
            std::fprintf(stderr, "vulkan: %s -> %d\n", what, r_);            \
            return false;                                                    \
        }                                                                    \
    } while (0)

// how a baked tie loser's corner is marked; `scene.vert` decodes it
constexpr float kTieLoserPhase = 8192.0f;

// One vertex, matching `scene.vert`'s three inputs. The UVs stay in the
// material's own PIXEL units, as the shipped data stores them, and the shader
// divides by textureSize with the sampler set to REPEAT - which is the same
// wrap `raster.cpp`'s `sample()` does with `% t.width`.
struct GpuVert {
    float x, y, z;
    float u, v;
    float r, g, b;
    // THE NORMAL, for row 7's per-pixel lighting. It travels with the vertex
    // because a bone's rotation turns its normals with it - `applyPose` does
    // that already, for the crowd's per-vertex light.
    float nx, ny, nz;
    // THE SHIMMER's phase (`o3de/shimmer.h`), -1 on a mesh that does not.
    float phase;
    // THE CORNER'S MESH (`Geometry::cornerMesh`), -1 for none: which affine
    // a POSED draw places it by (`scene.vert`, `posesBodies`); every other
    // draw ignores it.
    float mesh;
};

// It MUST match `scene.frag`'s block byte for byte, and the ordering is what
// makes it match: a `vec3` in a push-constant block is 16-byte aligned, so
// `fogColour` sits at offset 80 and everything scalar has to come before it.
// Packed the obvious way - mvp, cutout, fogStart, fogEnd, fogColour - the
// colour arrived four bytes early and had done since the fog landed; it went
// unseen because the shipped fog colour is BLACK, so the misplaced bytes were
// zeros. See `scene.frag`.
struct Push {
    float mvp[16];     // 0
    int32_t cutout;    // 64  flag 0x800: a colour key on black, never alpha
    float fogStart;    // 68  0 = no fog for this batch; see renderer.h's View
    float fogEnd;      // 72
    int32_t caster;    // 76  this batch CASTS, so it must not RECEIVE
    float fogColour[3];// 80
    int32_t lit;       // 92  this batch is LIT per pixel (row 7)
    int32_t cull;      // 96  the BACK-FACE CULL: 0 both sides, +1 discard
                       //     where !gl_FrontFacing, -1 where gl_FrontFacing
    int32_t posed;     // 100 a body POSED HERE: set 2 holds its affines and lights
    // THE TEXTURE PACK (`o3de/texpack.h`, `todo/remaster.md` B4): the bound
    // texture is a replacement `texScaleU` x `texScaleV` the original's size,
    // and the UVs are still in the ORIGINAL's texels. 0 for every shipped
    // texture, and the shader then divides exactly as it did before.
    int32_t texScaled; // 104
    float texScaleU;   // 108
    float texScaleV;   // 112
    // THE BLOOM (`todo/remaster.md` step 2, `post.frag`): 0 off; 1 on - the
    // alpha written is the texel's LIGHT, from the pack's light map
    int32_t bloomMode; // 116
    // THE WATER REFLECTION (`todo/remaster.md` A3): a water draw adds the
    // reflection (binding 2) times the strength; 0 on every other draw. The
    // low four bits the mode; above them a WET floor's darkening, 0..255
    // (`Draw::wet`, `o3de/wetfloor.h`), and above that the rain's, 0..255 (its
    // ripples, `View::rain`)
    int32_t water;     // 120
    float waterStrength; // 124
};
// 128 is every device's guaranteed push-constant size: the block is full
static_assert(sizeof(Push) == 128, "the push block is 128 bytes");
// Which `gl_FrontFacing` is the software rasterizer's FRONT (a negative area in
// `raster.cpp`): this pipeline's `frontFace` and Vulkan's Y-down viewport.
// Measured, not derived - the wrong sign culls every front face and the room
// turns inside out, which `engine: back-face cull` and the renderer checks see.
constexpr int32_t kCullSign = 1;

class VulkanRenderer : public omk::Renderer {
public:
    bool init(int w, int h) override;
    void setTextures(std::span<const omk::Texture> t) override { poolTextures(t, false); }
    void setPreloadTextures(std::span<const omk::Texture> t) override;
    bool keepsPreload() const override { return true; }
    int preloadStep(int budget) override;
    // the pool's textures uploaded where new; `preload`: ONLY that, pinned -
    // nothing evicted, the frame's pool (`tex_`) untouched
    void poolTextures(std::span<const omk::Texture> t, bool preload);
    void begin(const omk::View& v) override;
    void shadowPass(const omk::View& v, std::span<const omk::Draw> casters) override;
    bool drawsPixelLights() const override { return true; }
    bool drawsShadowMap() const override { return true; }
    // BODIES POSED BY THE RENDERER (`renderer.h` `Draw::meshPose`), as GLES,
    // GL1 and the 3DS do, for consistency across the backends: a posed draw's rest corners, one affine a mesh and
    // the lights that reach the body go to set 2, and `scene.vert` places,
    // turns and lights each corner by the GLES shader's rule. A draw that will
    // not fit the frame's pose buffer, and every caster of the shadow map, is
    // posed on the CPU instead (`cpuPose`) - so no path draws it wrong.
    bool posesBodies() const override { return poseDs_ != VK_NULL_HANDLE; }
    int maxVertexLights() const override { return poseDs_ != VK_NULL_HANDLE ? kPoseLights : 0; }
    bool setWater(float) override { return true; }   // nothing to build ahead
    bool setBodyTint(bool) override { return true; }  // the pose block always carries it
    bool drawsBodyTint() const override { return true; }
    // scene.frag takes a point light always; ON says the frame will send the
    // emitters' lights, and only then are the tiled lights' buffers made (32 MB)
    bool setEmitterLights(bool on) override { emitters_ = on; return true; }
    bool drawsEmitterLights() const override { return true; }
    bool takesPack() const override { return true; }
    bool drawsRelief() const override { return true; }
    int maxPixelLights() const override { return omk::View::kMaxGpuLights; }
    bool tilesLights() const override {
        return pointsBuf_ != VK_NULL_HANDLE && tilesBuf_ != VK_NULL_HANDLE && pipeTiled_[0] != VK_NULL_HANDLE;
    }
    bool drawsWater() const override { return true; }
    bool drawWaterScene(const omk::View& v, const omk::View& refl, std::span<const omk::Draw> scene,
                        std::span<const omk::Draw> above, std::span<const omk::Draw> water,
                        float strength, float planeY) override;
    bool drawsTwoReflections() const override {
        return samples_ == VK_SAMPLE_COUNT_1_BIT ? reflPassKeep_ != VK_NULL_HANDLE : reflPass1Keep_ != VK_NULL_HANDLE;
    }
    bool drawTwoReflections(const omk::View& v, const omk::View& refl, std::span<const omk::Draw> scene,
                            std::span<const omk::Draw> above, float strength, float planeY,
                            const omk::View& refl2, std::span<const omk::Draw> above2, float planeY2) override;
    int secondLevelSeen() override { settle(); return levelSeen_; }
    bool drawMirrorScene(const omk::View& v, const omk::View& refl,
                         std::span<const omk::Draw> scene,
                         std::span<const omk::Draw> sceneClipped,
                         std::span<const omk::Draw> mirror) override;
    void pushView(const omk::View& v);   // recompute the mvp mid-frame
    void submit(const omk::Draw& d) override;
    void end() override;
    const omk::Surface& readback() override;
    omk::RasterStats stats() const override { return st_; }
    const char* name() const override { return "vulkan"; }
    // every request of the boundary (`Renderer::takes`)
    unsigned takes() const override {
        return omk::kTakesMultisample | omk::kTakesTextureFilter | omk::kTakesSupersample |
               omk::kTakesAnisotropy | omk::kTakesShadowMap | omk::kTakesPixelLights | omk::kTakesUpscale |
               omk::kTakesBloom | omk::kTakesGrade | omk::kTakesWater | omk::kTakesBodyTint |
               omk::kTakesEmitterLights | omk::kTakesAmbientOcclusion | omk::kTakesRelief | omk::kTakesMist |
               omk::kTakesTrilinear;
    }
    ~VulkanRenderer() override;
    // A geometry the renderer holds state for tells it when it is destroyed
    // (`omk::GpuResidency`, todo/optimization.md step 26): the maps are cleared
    // at once and the buffers destroyed at the next `begin`, when the previous
    // frame's fence has been waited on. `OMK_NO_GEOMETRY_RELEASE=1` keeps the
    // old behaviour, to compare.
    VulkanRenderer() {
        releaseOn_ = !omk::envFlag("OMK_NO_GEOMETRY_RELEASE");
        omk::addGeometryListener(&VulkanRenderer::geometryGone, this);
    }
    bool releaseOn_ = true;
    std::vector<std::pair<VkBuffer, VkDeviceMemory>> deadBufs_;
    long released_ = 0;
    static void geometryGone(void* self, const omk::Geometry* g);
    // The enhancement (renderer.h). Recorded here and honoured in makeTarget,
    // where the device's own limits get the last word.
    bool setMultisample(int samples) override {
        if (dev_ != VK_NULL_HANDLE) return false;   // too late: the target exists
        wantSamples_ = samples < 2 ? 1 : samples < 4 ? 2 : samples < 8 ? 4 : 8;
        return true;
    }
    int samples() const { return static_cast<int>(samples_); }
    bool setTextureFilter(int mode) override {
        if (dev_ != VK_NULL_HANDLE) return false;
        filter_ = mode < 1 ? 0 : mode < 2 ? 1 : 2;
        return true;
    }
    bool setAnisotropy(int n) override {
        if (dev_ != VK_NULL_HANDLE) return false;
        aniso_ = n < 1 ? 1 : n > 16 ? 16 : n;
        return true;
    }
    // THE BLOOM's strength, 0 off; any time - it changes no pipeline
    bool setBloom(float s) override { bloom_ = s < 0.0f ? 0.0f : s > 4.0f ? 4.0f : s; return true; }
    // THE AMBIENT OCCLUSION's strength, 0 off - turned ON before `init`: it
    // keeps the frame's depth readable, which the target is made with. Once
    // the target has it (`aoOk_`), the strength moves at any time - it is the
    // pass's push constant (OMK's options menu, `todo/graphics-menu.md` 3a).
    bool setAmbientOcclusion(float s) override {
        if (dev_ != VK_NULL_HANDLE && !aoOk_) return false;
        ao_ = s < 0.0f ? 0.0f : s > 4.0f ? 4.0f : s;
        return true;
    }
    // THE GRADE: `rgb` the LUT's N^3 entries, red fastest (a .cube's order),
    // 0..1; n 0 turns it off. Any time - uploaded before the next frame's pass.
    bool setGrade(const float* rgb, int n) override {
        if (!rgb || n < 2) { lutN_ = 0; return true; }
        omk::lutStrip(rgb, n, lutData_);
        lutN_ = n; lutDirty_ = true;
        return true;
    }
    bool setSupersample(int n) override {
        if (dev_ != VK_NULL_HANDLE) return false;   // too late: the target exists
        ss_ = n < 2 ? 1 : n < 4 ? 2 : 4;
        return true;
    }
    bool setUpscale(float f) override {
        if (dev_ != VK_NULL_HANDLE) return false;   // too late: the target exists
        up_ = f > 1.0f ? std::min(f, 3.0f) : 1.0f;
        return true;
    }
    int supersample() const { return ss_; }
    float upscale() const { return up_; }
    int textureFilter() const { return filter_; }
    int anisotropy() const { return anisoOn_ ? aniso_ : 1; }
    // The largest count the device's colour, depth and stencil limits all
    // allow - so a probe can tell "the device cannot" from "the backend did
    // not", which a check that skips on the former must be able to do.
    int maxSamples() const {
        const VkSampleCountFlags ok = props_.limits.framebufferColorSampleCounts
                                    & props_.limits.framebufferDepthSampleCounts
                                    & props_.limits.framebufferStencilSampleCounts;
        for (int n = 64; n > 1; n >>= 1)
            if (ok & static_cast<VkSampleCountFlags>(n)) return n;
        return 1;
    }

    const char* device() const { return props_.deviceName; }

private:
    bool pickDevice();
    bool makeTarget();
    bool makePipelines();
    void makeShadowSet();
    uint32_t memType(uint32_t bits, VkMemoryPropertyFlags want) const;
    // every device memory released here, so the profiler's GPU table
    // (todo/debug-tools.md 5) loses it too
    void freeDeviceMemory(VkDeviceMemory m) {
        OMK_GPU_FREE(omk::prof::gpuKey(omk::prof::kVkMemory, m));
        vkFreeMemory(dev_, m, nullptr);
    }
    bool makeBuffer(VkDeviceSize n, VkBufferUsageFlags use,
                    VkMemoryPropertyFlags props, VkBuffer& b, VkDeviceMemory& m);
    bool makeImage2D(const VkImageCreateInfo& ii, VkImageAspectFlags aspect, const char* what,
                     VkImage& img, VkDeviceMemory& mem, VkImageView* view);
    VkCommandBuffer oneShotBegin();
    void oneShotEnd(VkCommandBuffer cb);      // submitted and WAITED for
    void oneShotSubmit(VkCommandBuffer cb);   // submitted; freed once the queue is known done
    // THE FRAME'S ONE WAIT (`todo/perf-audit.md` C11, step 1). The scene's
    // submit (`end`), the present pass's and an uploaded screen's are not
    // waited for one by one: the queue runs them in order, their barriers
    // order them, and the present's fence - whose wait covers every earlier
    // submission - is the frame's wait. `settle` waits for anything still
    // out and reads what the scene left (its stamps, the level queries);
    // `settleIdle` does the reading when the queue is already known idle.
    // Everything that writes memory the GPU may still read, or reuses what
    // it may still run, settles first.
    void settle();
    void settleIdle();
    void readSceneResults();
    bool sceneInFlight_ = false;
    std::vector<VkCommandBuffer> pendingCbs_;
    // THE BATCHED UPLOAD (the remaster audit's performance item 2): a pool's
    // texture copies recorded into ONE command buffer (`uploadCb`), submitted
    // and waited on once (`flushUploads`), their bytes staged in one
    // persistent host-visible RING - not a staging buffer, an allocation, a
    // submit and a `vkQueueWaitIdle` a texture
    VkCommandBuffer uploadCb();
    bool stage(VkDeviceSize bytes, VkBuffer& buf, VkDeviceSize& off, unsigned char*& ptr);
    void flushUploads();
    static constexpr VkDeviceSize kRingBytes = VkDeviceSize{32} << 20;
    VkCommandBuffer batchCb_ = VK_NULL_HANDLE;
    std::vector<std::pair<VkBuffer, VkDeviceMemory>> batchOwn_;   // staging past the ring's size
    VkBuffer ring_ = VK_NULL_HANDLE; VkDeviceMemory ringMem_ = VK_NULL_HANDLE;
    unsigned char* ringPtr_ = nullptr;
    VkDeviceSize ringAt_ = 0;
    bool ringFailed_ = false;
    bool uploadGeometry(const omk::Geometry* g);

    VkInstance        inst_ = VK_NULL_HANDLE;
    VkPhysicalDevice  phys_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props_{};
    bool r8Ok_ = false;   // R8 blits and filters: the light maps one channel (`setTextures`)
    bool bcOk_ = false;   // BC7 and BC4 sampled and filtered: the pack's block files (`setTextures`)
    VkPhysicalDeviceMemoryProperties memProps_{};
    long long lazyBytes_ = 0; int lazyImages_ = 0;   // TRANSIENT attachments in lazily allocated memory
    VkDevice          dev_  = VK_NULL_HANDLE;
    VkQueue           queue_ = VK_NULL_HANDLE;
    uint32_t          qfam_ = 0;
    VkCommandPool     pool_ = VK_NULL_HANDLE;
    VkCommandBuffer   cb_   = VK_NULL_HANDLE;
    VkFence           fence_ = VK_NULL_HANDLE;

    int w_ = 0, h_ = 0;      // the OUTPUT size - what `readback()` answers with
    // ...and the RENDER size, `w_ * ss_` by `h_ * ss_`. Supersampling
    // (`todo/enhancements.md` 9) draws the frame larger and averages it down,
    // which is the only anti-aliasing that reaches a CUTOUT edge - MSAA looks
    // at triangle edges and a cutout's silhouette is a colour key inside one.
    int rw_ = 0, rh_ = 0;
    int ss_ = 1;             // 1 off, else 2 or 4
    // ...or THE UPSCALE (`todo/remaster.md` A6): the frame drawn `up_` times
    // SMALLER each way and brought up to the output by FSR 1 in the present
    // pass (`runPresentPass`); 1 off, and it takes the supersample's place
    float up_ = 1.0f;
    // a picture `v` output pixels long, in RENDER pixels - the one rule every
    // size the frame is drawn at goes through (EASU's viewport is the same)
    int rs(int v) const {
        return up_ > 1.0f ? std::max(1, static_cast<int>(std::lround(static_cast<float>(v) / up_))) : v * ss_;
    }
    int picH_ = 0;           // the last frame's picture height, output pixels (`readback`'s upscale)
    // Which region of `colour_` the present blit should take. `end()` fills
    // the whole render target; `presentSurface` uploads a finished picture at
    // the OUTPUT size into its top-left, and blitting the whole thing then
    // would stretch a quarter of the frame over the window.
    int srcW_ = 0, srcH_ = 0;
    // `colour_` is the single-sample image every consumer reads - the
    // readback, the swapchain blit, the 2D upload. With MSAA on it becomes
    // the RESOLVE target and the pipelines rasterise into `msColour_`.
    int                   wantSamples_ = 1;
    int                   filter_ = 0;        // 0 nearest (the original), 1 bilinear, 2 trilinear
    int                   aniso_ = 1;         // asked; 1 is off
    bool                  anisoOn_ = false;   // the device feature was enabled for it
    bool                  anisoAvail_ = false; // ...the device has it (enabled whatever was asked)
    std::vector<VkSampler> retiredSamplers_;  // `retexture`'s old ones: the 1x1 stand-ins' sets keep them
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
    VkImage        colour_ = VK_NULL_HANDLE;  VkDeviceMemory colourMem_ = VK_NULL_HANDLE;
    VkImageView    colourView_ = VK_NULL_HANDLE;
    VkImage        msColour_ = VK_NULL_HANDLE; VkDeviceMemory msColourMem_ = VK_NULL_HANDLE;
    VkImageView    msColourView_ = VK_NULL_HANDLE;
    VkImage        depth_ = VK_NULL_HANDLE;   VkDeviceMemory depthMem_ = VK_NULL_HANDLE;
    VkFormat       dsFmt_ = VK_FORMAT_UNDEFINED;
    VkImageView    depthView_ = VK_NULL_HANDLE;
    VkRenderPass   pass_ = VK_NULL_HANDLE;
    VkRenderPass   reflPass_ = VK_NULL_HANDLE;   // the water reflection's, under the AO (its depth not stored)
    // ...and, without MSAA, the SECOND floor level's (`drawTwoReflections`):
    // the same, its colour KEPT - it begins in COLOR_ATTACHMENT, so its clear
    // touches its own quarter (the render area) and the first's stays
    VkRenderPass   reflPassKeep_ = VK_NULL_HANDLE;
    VkFramebuffer  fbuf_ = VK_NULL_HANDLE;
    VkBuffer       readBuf_ = VK_NULL_HANDLE; VkDeviceMemory readMem_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout dsl_ = VK_NULL_HANDLE;
    VkDescriptorPool      dpool_ = VK_NULL_HANDLE;
    VkPipelineLayout      plo_ = VK_NULL_HANDLE;
    VkPipeline            pipe_[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkPipeline            pipeTiled_[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};   // the tiled module's
    // an OPAQUE CUTOUT draw's: its depth written by the shader, so a discarded
    // keyed texel writes none (`scene.frag` OMK_LATE_DEPTH) - plain and tiled
    VkPipeline            pipeCut_ = VK_NULL_HANDLE, pipeTiledCut_ = VK_NULL_HANDLE;
    // THE FLOORS' REFLECTION at ONE sample when the frame has MSAA: its own
    // render pass and the scene pipelines again at 1 sample (a pipeline's
    // count is fixed) - the reflection's per-fragment lights ran four times a
    // pixel for an image the relief blurs anyway. The water's keeps the MSAA.
    VkRenderPass          reflPass1_ = VK_NULL_HANDLE;
    VkRenderPass          reflPass1Keep_ = VK_NULL_HANDLE;   // ...the second level's, its colour kept
    VkPipeline            pipe1_[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkPipeline            pipeTiled1_[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    bool                  tilesNow_ = false;   // this frame's tiles are on: the tiled pipelines
    // The mirror pass's three extra states. `pipeStencil_` marks the mirror's
    // pixels, `pipeDepthReset_` clears depth inside that mark, and `pipeRefl_`
    // is `pipe_` again with the stencil TEST on so the reflection lands only
    // where the mirror is.
    VkPipeline            pipeStencil_ = VK_NULL_HANDLE;
    VkPipeline            pipeDepthReset_ = VK_NULL_HANDLE;
    VkPipeline            pipeRefl_[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkSampler             sampler_ = VK_NULL_HANDLE;

    // ---- the MAPPED shadow, `todo/enhancements.md` 6 (an ENHANCEMENT) -----
    VkImage         shImg_ = VK_NULL_HANDLE;  VkDeviceMemory shMem_ = VK_NULL_HANDLE;
    VkImageView     shView_ = VK_NULL_HANDLE; VkSampler      shSampler_ = VK_NULL_HANDLE;
    VkFormat        shFmt_ = VK_FORMAT_UNDEFINED;
    VkRenderPass    shPass_ = VK_NULL_HANDLE;  VkFramebuffer shFbuf_ = VK_NULL_HANDLE;
    VkPipeline      shPipe_ = VK_NULL_HANDLE;  VkPipelineLayout shPlo_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl1_ = VK_NULL_HANDLE;
    VkDescriptorSet       ds1_  = VK_NULL_HANDLE;
    VkDescriptorSet       ds1Refl_ = VK_NULL_HANDLE;   // ...the water reflection's: its own tiles
    omk::LightTiles       tilesRefl_;                  // ...binned for the reflected camera
    // ...and the SECOND floor level's: the upper half of the reflection's
    // half of the buffer - each reflection's lists then within a quarter
    // of the words (`reflTilesCap`), half-size frames binned in a quarter of the tiles
    VkDescriptorSet       ds1Refl2_ = VK_NULL_HANDLE;
    VkBuffer        shUbo_ = VK_NULL_HANDLE;   VkDeviceMemory shUboMem_ = VK_NULL_HANDLE;
    void*           shUboPtr_ = nullptr;
    VkBuffer        litUbo_ = VK_NULL_HANDLE;  VkDeviceMemory litUboMem_ = VK_NULL_HANDLE;
    void*           litUboPtr_ = nullptr;
    // THE TILED POINT LIGHTS (`o3de/lighttiles.h`), set 1's bindings 3 and 4:
    // every point light of the frame, then the tiles' lists - host-visible,
    // written each frame as the lights UBO is
    static constexpr VkDeviceSize kPointsBytes = omk::kMaxPoints * sizeof(GpuLightStd140);
    static constexpr VkDeviceSize kTileWordsMax = 1u << 22;   // 16 MB: 64 x 40-odd tiles x 16 slices x 48, and room
    VkBuffer        pointsBuf_ = VK_NULL_HANDLE; VkDeviceMemory pointsMem_ = VK_NULL_HANDLE;
    void*           pointsPtr_ = nullptr;
    VkBuffer        tilesBuf_ = VK_NULL_HANDLE;  VkDeviceMemory tilesMem_ = VK_NULL_HANDLE;
    void*           tilesPtr_ = nullptr;
    bool            emitters_ = false;   // `setEmitterLights`: the two buffers above made at init
    omk::LightTiles tiles_;
    bool            tilesTold_ = false, tilesOverTold_ = false;
    // The command buffer is opened by whichever of `shadowPass` and `begin`
    // runs first, and only once - the depth pass has to be recorded BEFORE the
    // frame's own render pass begins.
    bool            cbStarted_ = false;
    // THE GPU'S OWN TIME per pass (`OMK_GPU_TIMES=1`, an instrument - the
    // reader's 4K slowdown was GPU-bound and the CPU's phases could not say
    // where): a timestamp at each pass boundary of the frame's one command
    // buffer, OUTSIDE every render pass, read after the frame's fence (no
    // extra wait); the means per pass every 60 frames, as `gpu.time` too.
    VkQueryPool     tsPool_ = VK_NULL_HANDLE;
    // THE SECOND LEVEL'S OCCLUSION TEST (`Renderer::secondLevelSeen`): one
    // query round each of its floors' runs in the floors' frame (a query
    // cannot pause between the draws its runs sit among), read after the
    // frame's fence; past `kLevelQueries` runs, taken as seen
    static constexpr uint32_t kLevelQueries = 64;
    VkQueryPool     levelPool_ = VK_NULL_HANDLE;
    // ...queried AFTER the frame's scene, so a wall drawn later counts: its
    // floors drawn again through this - depth LESS_OR_EQUAL, written neither
    // depth nor colour - their own pixels passing only where nothing covers them
    VkPipeline      pipeProbe_ = VK_NULL_HANDLE;
    uint32_t        levelIssued_ = 0;
    bool            levelOver_ = false;
    int             levelSeen_ = -1;
    static constexpr uint32_t kStamps = 8;
    uint32_t        tsCount_ = 0;
    const char*     tsLabel_[kStamps] = {};
    double          tsSum_[kStamps] = {};      // ms, by the label's slot in `kStampNames`
    int             tsFrames_ = 0;
    void stamp(const char* label) {
        if (!tsPool_ || tsCount_ >= kStamps) return;
        if (tsCount_ == 0) vkCmdResetQueryPool(cb_, tsPool_, 0, kStamps);
        vkCmdWriteTimestamp(cb_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tsPool_, tsCount_);
        tsLabel_[tsCount_++] = label;
    }
    void readStamps();
    // ...and THE PRESENT PATH's, which the frame's one command buffer does not
    // hold: the upscale and the present pass (`runPresentPass`), a screen's
    // upload (`presentSurface`) are one-shot buffers, and the blit to the
    // swapchain (`presentImage`) another. Queries: 0 the path's start, 1 its
    // passes done, 2 the blit's start, 3 its end, 4 the upscale done (when
    // there is one); read after the present's own fence
    static constexpr uint32_t kPresStamps = 5;
    VkQueryPool     tsPresPool_ = VK_NULL_HANDLE;
    bool            tsPresOpen_ = false, tsPresUp_ = false, tsPresScreen_ = false;
    double          tsPresSum_[4] = {};        // upscale, the rest of the passes, blit, all (ms)
    int             tsPresFrames_ = 0, tsPresScreens_ = 0;
    void presStampStart(VkCommandBuffer cb, bool screen) {
        if (!tsPresPool_) return;
        vkCmdResetQueryPool(cb, tsPresPool_, 0, kPresStamps);
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, tsPresPool_, 0);
        tsPresOpen_ = true; tsPresUp_ = false; tsPresScreen_ = screen;
    }
    void presStamp(VkCommandBuffer cb, uint32_t q) {
        if (!tsPresPool_ || !tsPresOpen_) return;
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tsPresPool_, q);
        if (q == 4) tsPresUp_ = true;
    }
    void readPresStamps();
    bool            shadowLive_ = false;   // a depth pass was recorded this frame
    int             litCount_ = 0;         // lights uploaded for this frame
    float           shimmerClock_ = 0.0f;  // the View's, held for the uniform
    float           rain_ = 0.0f;          // the View's rain, for the wet floors' ripples
    float           halo_ = 0.0f;          // ...and its halos, the bloom spread (`View::halo`)
    bool            dither_ = true;        // the engine's DITHERENABLE, see surface.h

    struct Tex {
        VkImage img = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE; VkDescriptorSet ds = VK_NULL_HANDLE;
        // a TEXTURE PACK replacement's factor over the original (texpack.h)
        float su = 1.0f, sv = 1.0f;
        bool packed = false;
        // its LIGHT MAP, the pack's `<key>_e.png` (binding 1; `lightBlack_`
        // when there is none)
        VkImage emImg = VK_NULL_HANDLE; VkDeviceMemory emMem = VK_NULL_HANDLE;
        VkImageView emView = VK_NULL_HANDLE;
        // its NORMAL MAP, the pack's `<key>_n.png` (binding 3, the relief's;
        // `normalFlat_` when there is none)
        VkImage nmImg = VK_NULL_HANDLE; VkDeviceMemory nmMem = VK_NULL_HANDLE;
        VkImageView nmView = VK_NULL_HANDLE;
    };
    std::vector<Tex> tex_;                // per pool slot: handles `uploaded_` owns
    Tex              white_;              // a material with no texture
    Tex              lightBlack_;         // the light map of a texture that has none
    Tex              normalFlat_;         // the normal map of one that has none: flat, ALPHA 0 - "none"
    // the textures on the device, by their pixel storage (`setTextures`); the
    // entry holds that storage so its address is not reused while cached
    // `pinned`: a preloaded set's (`setPreloadTextures`) - kept while listed
    struct Uploaded { Tex t; omk::PixelBuffer idx, pal; bool used = false; bool pinned = false; };
    std::vector<omk::Texture> preQueue_;   // the preload's textures not yet on the device
    std::map<std::tuple<const std::uint8_t*, const std::uint8_t*, int, int>, Uploaded> uploaded_;

    std::map<const omk::Geometry*, std::pair<VkBuffer, VkDeviceMemory>> vbo_;
    // THE POSE BUFFER (set 2): host-visible, written from the start at every
    // `begin` (the frame before it was waited for), one block a posed body -
    // a header (lights, from-black, the base grey, the affines' count), the
    // lights (two vec4 each, `kPoseLights` places), then three vec4 an affine
    static constexpr int kPoseLights = 16, kPoseMaxMeshes = 256;
    // 2 MB: the Anekbah street walk's busiest frame writes 288 KB (25 staged
    // bodies and the crowd, measured 2026-10-20); a frame past it poses the
    // rest on the CPU (`writePose` refuses)
    static constexpr VkDeviceSize kPoseBytes = 2u << 20;
    static constexpr VkDeviceSize kPoseRange = (1 + 2 * kPoseLights + 3 * kPoseMaxMeshes + 1) * 16;
    VkDescriptorSetLayout dsl2_ = VK_NULL_HANDLE;
    VkBuffer poseBuf_ = VK_NULL_HANDLE;
    VkDeviceMemory poseMem_ = VK_NULL_HANDLE;
    void* posePtr_ = nullptr;
    VkDescriptorSet poseDs_ = VK_NULL_HANDLE;
    VkDeviceSize poseUsed_ = 0, poseAlign_ = 256;
    std::map<std::pair<const float*, const float*>, uint32_t> poseCache_;   // a body's block this frame
    std::map<const omk::Geometry*, omk::Geometry> cpuPosed_;   // `cpuPose`'s corners
    std::uint64_t cpuPoseRev_ = 0;
    bool writePose(const omk::Draw& d, uint32_t& off);
    omk::Draw cpuPose(const omk::Draw& d);
    void submitOne(const omk::Draw& d, bool posed, uint32_t poseOff);
    std::map<const omk::Geometry*, std::size_t> vboN_;
    // ...and how many corners its buffer HOLDS, which a geometry that changes
    // size every frame (the particles) keeps above what it uses, so it refills
    // in place instead of freeing and allocating device memory every frame
    // (the same cost the GLES ring answers on the Vita)
    std::map<const omk::Geometry*, std::size_t> vboCap_;
    std::map<const omk::Geometry*, std::uint64_t> vboRev_;
    // THE DEPTH TIE, resolved at SUBMIT (raster.cpp `kDepthTie` has the
    // reading). Two faces on the same four vertices in opposite winding - the
    // two SIDES of a shop sign, 18 pairs in Anekbah - and the engine's strict
    // test on a quantised z-buffer keeps the FIRST drawn on every pixel. The
    // engine's software back-face cull keeps only one side of such a pair,
    // and the tie's key carries the face's side - `depthtie.cpp` faceClass -
    // so what is left here is coincident faces of ONE winding. The software rasterizer rejects a
    // depth within 2^-16 of the buffer's; a GPU compare cannot read the
    // buffer, a quantised `gl_FragDepth` still straddles its grid on ~1% of
    // pixels, and a per-draw depth bias through the non-linear projection is
    // hundreds of inches at street distance. So the tie is settled where it
    // is decidable exactly: a face whose position set an earlier DEPTH-WRITING
    // face of the same geometry already claimed, in draw order, can never win
    // a pixel from it, and is DEGENERATED in the vertex buffer (its three
    // vertices collapsed to one) so it rasterises nothing. Faces are walked in
    // the order the draws arrive, once per geometry revision; a quad is the
    // consecutive pair `buildGeometry` emits, (0,1,2)(0,2,3).
    // The DECISION lives in `o3de/depthtie.h` (todo/optimization.md
    // step 3), so it can be tested without a GPU; this backend keeps the vertex
    // buffer half below.
    std::map<const omk::Geometry*, omk::DepthTie> tie_;
    void resolveTies(const omk::Draw& d);
    // THE TIE DECIDED ONCE (`bakeDepthTie`, todo/optimization.md step 29): per
    // geometry, one byte a corner, 1 on a loser's. A baked geometry skips
    // `resolveTies` and its losers are MARKED in every upload (the phase moved
    // down by `kTieLoserPhase`), which `scene.vert` draws two 16-bit depth
    // steps back instead of collapsing them.
    std::map<const omk::Geometry*, std::vector<std::uint8_t>> baked_;
    GpuVert vertOf(const omk::Geometry& g, std::size_t i, const std::vector<std::uint8_t>* lose) const {
        const auto& c = g.corners[i];
        const float mesh = g.cornerMesh.size() == g.corners.size() ? static_cast<float>(g.cornerMesh[i]) : -1.0f;
        GpuVert v{c.x, c.y, c.z, c.u, c.v, c.r, c.g, c.b, c.nx, c.ny, c.nz, c.phase, mesh};
        if (lose && i < lose->size() && (*lose)[i]) v.phase -= kTieLoserPhase;
        return v;
    }
    const std::vector<std::uint8_t>* bakedOf(const omk::Geometry* g) const {
        const auto it = baked_.find(g);
        return it == baked_.end() ? nullptr : &it->second;
    }
public:
    bool bakeDepthTie(const omk::Geometry* g, std::span<const omk::Draw> order) override;
private:

    Push             push_{};
    // the fog, held between begin() and each submit()
    bool             fog_ = false;
    float            fogStart_ = 0.0f, fogEnd_ = 0.0f;
    bool             mistOn_ = false;   // the frame has the mist (`View::mist`): the fogless batches marked for it
    float            fogColour_[3] = {0.0f, 0.0f, 0.0f};
    float            clearColour_[3] = {0.0f, 0.0f, 0.0f};   // View::clearColour
    omk::RasterStats st_;
    omk::Surface     fb_{1, 1, 0};
    bool             recording_ = false;
    bool             dirty_ = false;    // a new frame is waiting in readBuf_
    bool             copyOwed_ = false; // ...still to be copied there (`readback`)
    VkPipeline       forcePipeline_ = VK_NULL_HANDLE;  // the mirror pass's override
    bool             flipX_ = false;   // the current view's screen-X flip (mirror pass)
    bool             reflStencil_ = false;             // draw through pipeRefl_

    // ---- DIRECT PRESENTATION (optional; nothing here runs offscreen).
    //
    // The surface comes from the window layer, which is the only thing that
    // knows what a window is - `PORTING` A8 rule 2 keeps SDL in one file and
    // Vulkan in this one, so the two meet through opaque handles and a list of
    // extension names rather than by including each other's headers.
    std::vector<std::string> wantInstExts_;
    VkSurfaceKHR   surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swap_    = VK_NULL_HANDLE;
    VkFormat       swapFmt_ = VK_FORMAT_UNDEFINED;
    VkExtent2D     swapExt_{};
    std::vector<VkImage> swapImgs_;
    VkSemaphore    acquired_ = VK_NULL_HANDLE;
    // ONE PER SWAPCHAIN IMAGE: the present waits on it, and it may be
    // signalled again only once that image is acquired again - with one for
    // all, the next frame's submit re-signalled it while the last present
    // could still be waiting (the validation layer's
    // VUID-vkQueueSubmit-pSignalSemaphores-00067, seen once the frame
    // stopped idling the queue between presents, C11 step 1)
    std::vector<VkSemaphore> drawn_;
    VkBuffer       upBuf_ = VK_NULL_HANDLE; VkDeviceMemory upMem_ = VK_NULL_HANDLE;
    VkCommandBuffer pcb_ = VK_NULL_HANDLE;
    VkFence        pfence_ = VK_NULL_HANDLE;
    uint32_t       presentFam_ = 0;

    // ---- THE PRESENT PASS (todo/optimization.md step 4b) ----------------------
    // `colour_` dithered to 565 and expanded back to RGBA8 at the letterbox's
    // offset into `presImg_`, which the swapchain blit then takes - the bytes
    // `readback` + `presentSurface` produce for a frame nothing is drawn over,
    // without either. Made on first use.
    VkImage          presImg_ = VK_NULL_HANDLE;  VkDeviceMemory presMem_ = VK_NULL_HANDLE;
    VkImageView      presView_ = VK_NULL_HANDLE;
    VkRenderPass     presPass_ = VK_NULL_HANDLE; VkFramebuffer presFbuf_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout presDsl_ = VK_NULL_HANDLE;
    VkDescriptorPool presPool_ = VK_NULL_HANDLE; VkDescriptorSet presDs_ = VK_NULL_HANDLE;
    VkPipelineLayout presPlo_ = VK_NULL_HANDLE;  VkPipeline presPipe_ = VK_NULL_HANDLE;
    VkSampler        presSampler_ = VK_NULL_HANDLE;
    VkBuffer         presRead_ = VK_NULL_HANDLE; VkDeviceMemory presReadMem_ = VK_NULL_HANDLE;
    bool makePresentPass();
    bool runPresentPass(int vy, int vh, bool raw = false);

    // ---- THE UPSCALE (`todo/remaster.md` A6) -----------------------------
    // EASU's output at the OUTPUT size (`easu.frag`), which the present pass
    // reads through `presUpDs_` and sharpens (RCAS); made on first use
    VkImage          easuImg_ = VK_NULL_HANDLE;  VkDeviceMemory easuMem_ = VK_NULL_HANDLE;
    VkImageView      easuView_ = VK_NULL_HANDLE;
    VkRenderPass     easuPass_ = VK_NULL_HANDLE; VkFramebuffer easuFb_ = VK_NULL_HANDLE;
    VkDescriptorPool easuPool_ = VK_NULL_HANDLE;
    VkDescriptorSet  easuDs_ = VK_NULL_HANDLE;   // `colour_`, LINEAR: EASU's gathers
    VkDescriptorSet  presUpDs_ = VK_NULL_HANDLE; // `easuView_`: the present pass's input
    VkPipelineLayout easuPlo_ = VK_NULL_HANDLE;  VkPipeline easuPipe_ = VK_NULL_HANDLE;
    VkSampler        easuSampler_ = VK_NULL_HANDLE;
    bool makeUpscale();
    void killUpscale();

    // ---- THE REMASTER'S POST-PASS (`todo/remaster.md` step 2) -----------
    // The BLOOM from the texture pack's light maps and the city's colour
    // GRADE, after the scene's render pass and before the readback and the
    // present pass, so every way a frame leaves sees them. The scene is drawn
    // into `postSrc_` (`fbufPost_`); two quarter-size half-float images take
    // the glow (down, blur, blur); one pass writes the finished frame into
    // `colour_`. Made on first use for the target's size; `resize` drops it.
    float            bloom_ = 0.0f;
    VkImage          postSrc_ = VK_NULL_HANDLE;  VkDeviceMemory postSrcMem_ = VK_NULL_HANDLE;
    VkImageView      postSrcView_ = VK_NULL_HANDLE;
    VkImage          glowImg_[2] = {};  VkDeviceMemory glowMem_[2] = {};
    VkImageView      glowView_[2] = {};
    VkRenderPass     glowPass_ = VK_NULL_HANDLE, finalPass_ = VK_NULL_HANDLE;
    VkFramebuffer    glowFb_[2] = {};   VkFramebuffer finalFb_ = VK_NULL_HANDLE;
    VkFramebuffer    fbufPost_ = VK_NULL_HANDLE;   // `pass_` into `postSrc_`: the scene when the post runs
    bool             postNow_ = false;             // this frame's post, decided by `begin`
    bool             poolLit_ = false;             // a texture of the pool has a light map (`setTextures`)
    bool             lightMapNow_ = false;         // a draw of the frame's pass bound one: else the glow is black
    VkDescriptorSetLayout postDsl_ = VK_NULL_HANDLE;
    VkDescriptorPool postPool_ = VK_NULL_HANDLE;
    VkDescriptorSet  postDs_[8] = {};   // down, blur H, blur V, final; AO, AO blur H, AO blur V, AO depth
    VkSampler        postSampler_ = VK_NULL_HANDLE;
    VkPipelineLayout postPlo_ = VK_NULL_HANDLE;
    VkPipeline       glowPipe_ = VK_NULL_HANDLE, finalPipe_ = VK_NULL_HANDLE;
    int              glowW_ = 0, glowH_ = 0;
    // THE AMBIENT OCCLUSION: the depth's DEPTH-aspect view (`depthView_` has
    // both aspects, which a sampler may not read), two half-size R8 images
    // (AO, blur, blur), and the camera the frame's pass was drawn with
    float            ao_ = 0.0f;
    bool             aoOk_ = false;      // the depth format can be sampled
    VkImageView      depthRead_ = VK_NULL_HANDLE;
    VkImage          aoImg_[2] = {};  VkDeviceMemory aoMem_[2] = {};
    VkImageView      aoView_[2] = {};
    VkFramebuffer    aoFb_[2] = {};
    VkRenderPass     aoPass_ = VK_NULL_HANDLE;
    VkPipeline       aoPipe_ = VK_NULL_HANDLE;
    // ...and the half-size VIEW Z every occlusion pass reads (R32F, its own pass)
    VkImage          zImg_ = VK_NULL_HANDLE;  VkDeviceMemory zMem_ = VK_NULL_HANDLE;
    VkImageView      zView_ = VK_NULL_HANDLE;
    VkFramebuffer    zFb_ = VK_NULL_HANDLE;
    VkRenderPass     zPass_ = VK_NULL_HANDLE;
    VkPipeline       zPipe_ = VK_NULL_HANDLE;
    int              aoW_ = 0, aoH_ = 0;
    float            aoView4_[4] = {1, 1, 1, 1};   // tanH, tanV, picture w, h (render pixels)
    // the GRADE: an N*N x N RGBA8 strip, N 0 for none
    int              lutN_ = 0;
    std::vector<unsigned char> lutData_;
    bool             lutDirty_ = false;
    VkImage          lutImg_ = VK_NULL_HANDLE;  VkDeviceMemory lutMem_ = VK_NULL_HANDLE;
    VkImageView      lutView_ = VK_NULL_HANDLE;
    // ---- THE WATER REFLECTION (`todo/remaster.md` A3) ----------------------
    // A second scene target the size of the first, drawn through the camera
    // reflected in the water plane by the SAME render pass (so every pipeline
    // fits it) into its own framebuffer, then sampled by the water draws of
    // the main pass. Made on first use; `resize` drops it.
    VkImage          reflColour_ = VK_NULL_HANDLE, reflMs_ = VK_NULL_HANDLE, reflDepth_ = VK_NULL_HANDLE;
    VkDeviceMemory   reflColourMem_ = VK_NULL_HANDLE, reflMsMem_ = VK_NULL_HANDLE, reflDepthMem_ = VK_NULL_HANDLE;
    VkImageView      reflColourView_ = VK_NULL_HANDLE, reflMsView_ = VK_NULL_HANDLE, reflDepthView_ = VK_NULL_HANDLE;
    VkFramebuffer    reflFbuf_ = VK_NULL_HANDLE;
    // ...and the floors' at one sample (`reflPass1_`): the same colour image,
    // a depth of its own at half size
    VkFramebuffer    reflFbuf1_ = VK_NULL_HANDLE;
    VkImage          reflDepth1_ = VK_NULL_HANDLE;
    VkDeviceMemory   reflDepth1Mem_ = VK_NULL_HANDLE;
    VkImageView      reflDepth1View_ = VK_NULL_HANDLE;
    VkFramebuffer    passFb_ = VK_NULL_HANDLE;   // `begin`'s target when set, `fbuf_` otherwise
    bool             waterNow_ = false;
    bool             reflectingNow_ = false;   // the reflection pass: clip below `waterY_`
    // the REFLECTING FLOORS' frame (no water draws): its reflection drawn at
    // half size into the image's top-left quarter, the floors sampling it
    // there (`scene.frag` mode 3) - one image and one binding for both
    bool             reflHalf_ = false;
    bool reflHalfNow() const { return reflHalf_ && passFb_ != VK_NULL_HANDLE; }
    // ...and the SECOND level's (`drawTwoReflections`): `second_` while its
    // reflection is drawn, into the top-RIGHT quarter (x from `reflHalfW()`);
    // `twoRefl_` for the whole frame - the floors then sample each its own
    // quarter, and the reflections' tiles share their half of the buffer
    bool             second_ = false, twoRefl_ = false;
    const omk::View* secondView_ = nullptr;
    std::span<const omk::Draw> secondAbove_;
    float            secondY_ = 0.0f;
    int reflHalfW() const { return (rw_ + 1) / 2; }
    VkDeviceSize reflTilesAt() const { return kTileWordsMax * 4 + (second_ ? kTileWordsMax * 2 : 0); }
    std::size_t reflTilesCap() const { return twoRefl_ ? kTileWordsMax / 2 : kTileWordsMax; }
    float            waterStrength_ = 0.0f, waterY_ = 0.0f;
    bool makeReflection();
    void killReflection();
    void bindReflection(VkDescriptorSet ds);
    bool makePost();
    bool makePostAt();
    int  postFailW_ = -1, postFailH_ = -1;   // the size `makePostAt` last failed at: not retried every frame
    void killPost();
    void killLut();
    bool uploadLut();
    void bindPostSets();
    void recordPost(VkCommandBuffer cb);
    bool presentImage(VkImage src, int sw, int sh);

public:
    void needExtensions(const char* const* names, unsigned n) {
        wantInstExts_.assign(names, names + n);
    }
    void* createInstanceOnly();
    bool attachSurface(unsigned long long surf);
    bool makeSwapchain(int w, int h);
    // THE FRAME'S SIZE CHANGED (options row 2, `todo/options-menu.md`): the
    // frame-sized half of `makeTarget` is rebuilt - colour, depth, the pass
    // and framebuffer, the readback - with the present pass and the upload
    // buffer, which re-make themselves lazily. The pipelines stay: their
    // viewport is dynamic and the new pass is the same description, so it is
    // render-pass COMPATIBLE. The shadow map is a fixed size and is kept.
    bool resize(int w, int h);
    bool retarget(int samples, int ss, float up, float ao);   // ...at new enhancements (OMK's menu)
    bool makeSampler();                                        // the textures' sampler, by filter_/anisoOn_
    bool retexture(int filter, int aniso) override;            // ...again, the textures dropped
    // ...and the WINDOW's: fullscreen, or any resize, changes the surface's
    // extent and the swapchain is remade at it.
    bool remakeSwapchain();
    void killSwapchain();
    bool resizing_ = false;
    bool presentDirect();
    bool presentSurface(const omk::Surface& s);
    // THE CLOUD UNDER THE INTERFACE (`under.frag`): the frame uploaded with
    // the overlay's M in alpha, the 640x480 cloud beside it, one pass into
    // `presImg_`. Its own images, set and pipeline, made on first use and
    // dropped with the present pass's (`killUnder`).
    bool presentUnder(const omk::Surface& ui, const unsigned char* mask, const unsigned char* maskRow,
                      const omk::Surface& under, int filter, std::vector<unsigned char>* readback);
    bool makeUnder(int uw, int uh);
    void killUnder();
    VkImage          undUi_ = VK_NULL_HANDLE;    VkDeviceMemory undUiMem_ = VK_NULL_HANDLE;
    VkImageView      undUiView_ = VK_NULL_HANDLE;
    VkImage          undImg_ = VK_NULL_HANDLE;   VkDeviceMemory undImgMem_ = VK_NULL_HANDLE;
    VkImageView      undView_ = VK_NULL_HANDLE;
    VkImage          undMask_ = VK_NULL_HANDLE;  VkDeviceMemory undMaskMem_ = VK_NULL_HANDLE;
    VkImageView      undMaskView_ = VK_NULL_HANDLE;
    VkBuffer         undBuf_ = VK_NULL_HANDLE;   VkDeviceMemory undBufMem_ = VK_NULL_HANDLE;
    VkSampler        undSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout undDsl_ = VK_NULL_HANDLE;
    VkDescriptorPool undPool_ = VK_NULL_HANDLE;  VkDescriptorSet undDs_ = VK_NULL_HANDLE;
    VkPipelineLayout undPlo_ = VK_NULL_HANDLE;   VkPipeline undPipe_ = VK_NULL_HANDLE;
    int              undW_ = 0, undH_ = 0;
    void setViewport(const omk::View& view);
    // The present pass's three uses: present the frame (a window only), read
    // the pass's output back (the verification), and load `colour_` with a
    // picture of the caller's (the exhaustive probe). All take the letterbox
    // as the picture's first row `vy` and height `vh` in a `w_ x h_` window.
    bool presentWorld(int vy, int vh);
    bool worldPicture(int vy, int vh, std::vector<unsigned char>& rgba);
    bool probeUpload(const unsigned char* rgba);
    void setDitherForProbe(bool on) { dither_ = on; }
};

uint32_t VulkanRenderer::memType(uint32_t bits, VkMemoryPropertyFlags want) const {
    for (uint32_t i = 0; i < memProps_.memoryTypeCount; ++i)
        if ((bits & (1u << i)) &&
            (memProps_.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0;
}

bool VulkanRenderer::makeBuffer(VkDeviceSize n, VkBufferUsageFlags use,
                                VkMemoryPropertyFlags props, VkBuffer& b,
                                VkDeviceMemory& m) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = n; bi.usage = use; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VKCHECK(vkCreateBuffer(dev_, &bi, nullptr, &b), "vkCreateBuffer");
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev_, b, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memType(req.memoryTypeBits, props);
    VKCHECK(vkAllocateMemory(dev_, &ai, nullptr, &m), "vkAllocateMemory");
    // the profiler's GPU memory (todo/debug-tools.md 5): a buffer's category
    // from what it is for
    OMK_GPU_ALLOC((use & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ? "vertex buffers"
                  : (use & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) ? "uniforms"
                  : (use & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) ? "staging"
                  : (use & VK_BUFFER_USAGE_TRANSFER_DST_BIT) ? "readback" : "buffers",
                  omk::prof::gpuKey(omk::prof::kVkMemory, m), ai.allocationSize);
    VKCHECK(vkBindBufferMemory(dev_, b, m, 0), "vkBindBufferMemory");
    return true;
}

// ONE IMAGE, CHECKED (the remaster audit's bug 4, 2026-10-23): `ii`'s image,
// device-local memory allocated and bound, and - where `view` is given - a 2D
// view of all its levels with `aspect`. Or NONE of them: a step that fails
// destroys what the ones before it made, leaves every handle null and says
// so (once per `what`, which is also the GPU profiler's category). The image
// sequences were written out by hand ten times, and the two texture uploads
// checked nothing - out of device memory (the 4x pack with its light and
// normal maps) they bound memory to a null image and copied into it. A
// texture that fails now has no image and no set, and draws white.
bool VulkanRenderer::makeImage2D(const VkImageCreateInfo& ii, VkImageAspectFlags aspect, const char* what,
                                 VkImage& img, VkDeviceMemory& mem, VkImageView* view) {
    img = VK_NULL_HANDLE; mem = VK_NULL_HANDLE;
    if (view) *view = VK_NULL_HANDLE;
    const auto fail = [&](const char* step, VkResult r) {
        if (mem) freeDeviceMemory(mem);
        if (img) vkDestroyImage(dev_, img, nullptr);
        img = VK_NULL_HANDLE; mem = VK_NULL_HANDLE;
        static std::set<std::string> told;
        if (told.insert(what).second)
            std::fprintf(stderr, "vulkan: %s %ux%u - %s failed (%d), refused%s\n", what, ii.extent.width,
                         ii.extent.height, step, static_cast<int>(r),
                         r == VK_ERROR_OUT_OF_DEVICE_MEMORY ? ": out of device memory" : "");
        return false;
    };
    // `OMK_VK_IMAGE_FAIL=<what>`, an instrument: every image of that category
    // refused as if the device were out of memory (`verify.py: engine: vulkan
    // images`)
    static const char* refuse = omk::envStr("OMK_VK_IMAGE_FAIL");
    if (refuse && std::strcmp(refuse, what) == 0) return fail("the image", VK_ERROR_OUT_OF_DEVICE_MEMORY);
    VkResult r = vkCreateImage(dev_, &ii, nullptr, &img);
    if (r != VK_SUCCESS) { img = VK_NULL_HANDLE; return fail("the image", r); }
    VkMemoryRequirements req; vkGetImageMemoryRequirements(dev_, img, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    // a TRANSIENT attachment (never loaded nor stored: the MSAA samples, a
    // depth nothing reads) in LAZILY ALLOCATED memory where the device has
    // it - MEMORYLESS on Apple's GPUs, the samples living in tile memory
    // alone (the audit's 4K trims: 4x MSAA at 4K is ~133 MB of colour and
    // ~166 MB of depth and stencil that were real allocations)
    bool lazy = false;
    if (ii.usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)
        for (uint32_t i = 0; i < memProps_.memoryTypeCount; ++i)
            if ((req.memoryTypeBits & (1u << i)) &&
                (memProps_.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)) {
                ai.memoryTypeIndex = i;
                lazy = true;
                break;
            }
    r = vkAllocateMemory(dev_, &ai, nullptr, &mem);
    if (r != VK_SUCCESS) { mem = VK_NULL_HANDLE; return fail("its memory", r); }
    if (lazy) {
        lazyBytes_ += static_cast<long long>(ai.allocationSize);
        ++lazyImages_;
    }
    OMK_GPU_ALLOC(what, omk::prof::gpuKey(omk::prof::kVkMemory, mem), ai.allocationSize);
    r = vkBindImageMemory(dev_, img, mem, 0);
    if (r != VK_SUCCESS) return fail("binding its memory", r);
    if (!view) return true;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = ii.format;
    vi.subresourceRange = {aspect, 0, ii.mipLevels, 0, 1};
    r = vkCreateImageView(dev_, &vi, nullptr, view);
    if (r != VK_SUCCESS) { *view = VK_NULL_HANDLE; return fail("its view", r); }
    return true;
}

VkCommandBuffer VulkanRenderer::oneShotBegin() {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool_; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(dev_, &ai, &cb);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    return cb;
}

void VulkanRenderer::oneShotEnd(VkCommandBuffer cb) {
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(dev_, pool_, 1, &cb);
    settleIdle();   // the idle queue has run everything before it too
}

void VulkanRenderer::oneShotSubmit(VkCommandBuffer cb) {
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    pendingCbs_.push_back(cb);
}

void VulkanRenderer::settle() {
    if (sceneInFlight_) vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX);
    if (!pendingCbs_.empty()) vkQueueWaitIdle(queue_);
    settleIdle();
}

void VulkanRenderer::settleIdle() {
    if (!pendingCbs_.empty()) {
        vkFreeCommandBuffers(dev_, pool_, static_cast<uint32_t>(pendingCbs_.size()), pendingCbs_.data());
        pendingCbs_.clear();
    }
    if (sceneInFlight_) {
        sceneInFlight_ = false;
        readSceneResults();
    }
}

VkCommandBuffer VulkanRenderer::uploadCb() {
    if (batchCb_ == VK_NULL_HANDLE) batchCb_ = oneShotBegin();
    return batchCb_;
}

// `bytes` of staging, 16-aligned (a block format's texel block, and the
// copy's 4): from the ring - the batch flushed first when it is full - or,
// past the ring's size, a buffer of its own freed at the flush
bool VulkanRenderer::stage(VkDeviceSize bytes, VkBuffer& buf, VkDeviceSize& off, unsigned char*& ptr) {
    const VkDeviceSize need = (bytes + 15) & ~VkDeviceSize{15};
    if (need <= kRingBytes && !ringFailed_) {
        if (ring_ == VK_NULL_HANDLE) {
            void* p = nullptr;
            if (makeBuffer(kRingBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           ring_, ringMem_) &&
                vkMapMemory(dev_, ringMem_, 0, kRingBytes, 0, &p) == VK_SUCCESS) {
                ringPtr_ = static_cast<unsigned char*>(p);
            } else {
                ringFailed_ = true;   // a buffer a texture, as before
                std::fprintf(stderr, "vulkan: the upload ring refused - a staging buffer a texture\n");
            }
        }
        if (ringPtr_) {
            if (ringAt_ + need > kRingBytes) flushUploads();
            buf = ring_; off = ringAt_; ptr = ringPtr_ + ringAt_;
            ringAt_ += need;
            return true;
        }
    }
    VkBuffer b = VK_NULL_HANDLE; VkDeviceMemory m = VK_NULL_HANDLE;
    if (!makeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, b, m))
        return false;
    void* p = nullptr;
    if (vkMapMemory(dev_, m, 0, bytes, 0, &p) != VK_SUCCESS) {
        vkDestroyBuffer(dev_, b, nullptr);
        freeDeviceMemory(m);
        return false;
    }
    batchOwn_.push_back({b, m});
    buf = b; off = 0; ptr = static_cast<unsigned char*>(p);
    return true;
}

void VulkanRenderer::flushUploads() {
    if (batchCb_ != VK_NULL_HANDLE) {
        oneShotEnd(batchCb_);
        batchCb_ = VK_NULL_HANDLE;
    }
    for (auto& [b, m] : batchOwn_) {
        vkDestroyBuffer(dev_, b, nullptr);
        freeDeviceMemory(m);
    }
    batchOwn_.clear();
    ringAt_ = 0;
}

// The instance ALONE, so the window layer can make a surface from it before a
// device exists. Present support is a property of a (device, surface) pair, so
// the surface has to come first or the queue family cannot be chosen correctly
// - which is why this is split out rather than done inside `init`.
void* VulkanRenderer::createInstanceOnly() {
    if (inst_ != VK_NULL_HANDLE) return inst_;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "omk";
    app.apiVersion = VK_API_VERSION_1_1;
    // MoltenVK is a PORTABILITY driver: without this extension and flag the
    // loader reports zero devices on macOS and the failure looks like "no GPU"
    // rather than "not asked for".
    std::vector<const char*> iexts = {"VK_KHR_portability_enumeration"};
    for (const auto& e : wantInstExts_) iexts.push_back(e.c_str());
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(iexts.size());
    ci.ppEnabledExtensionNames = iexts.data();
    ci.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    if (vkCreateInstance(&ci, nullptr, &inst_) != VK_SUCCESS) {
        // Try again without portability: on a machine with a native driver the
        // extension may simply not be there, and that is not an error.
        iexts.erase(iexts.begin());
        ci.enabledExtensionCount = static_cast<uint32_t>(iexts.size());
        ci.ppEnabledExtensionNames = iexts.empty() ? nullptr : iexts.data();
        ci.flags = 0;
        if (vkCreateInstance(&ci, nullptr, &inst_) != VK_SUCCESS) {
            std::fprintf(stderr, "vulkan: vkCreateInstance failed\n");
            inst_ = VK_NULL_HANDLE;
        }
    }
    return inst_;
}

bool VulkanRenderer::attachSurface(unsigned long long surf) {
    surface_ = reinterpret_cast<VkSurfaceKHR>(surf);
    return surface_ != VK_NULL_HANDLE;
}

bool VulkanRenderer::pickDevice() {
    if (!createInstanceOnly()) return false;
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst_, &n, nullptr);
    if (!n) { std::fprintf(stderr, "vulkan: no physical device\n"); return false; }
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(inst_, &n, devs.data());
    phys_ = devs[0];
    vkGetPhysicalDeviceProperties(phys_, &props_);
    {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(phys_, VK_FORMAT_R8_UNORM, &fp);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                          VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        r8Ok_ = (fp.optimalTilingFeatures & need) == need;
    }
    vkGetPhysicalDeviceMemoryProperties(phys_, &memProps_);

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qn, qs.data());
    // With a surface the family must also be able to PRESENT to it. Choosing a
    // graphics family and assuming it can present is the usual way this breaks
    // on a machine where the two differ.
    bool found = false;
    for (uint32_t i = 0; i < qn; ++i) {
        if (!(qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        if (surface_ != VK_NULL_HANDLE) {
            VkBool32 ok = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(phys_, i, surface_, &ok);
            if (!ok) continue;
        }
        qfam_ = i; presentFam_ = i; found = true; break;
    }
    if (!found) {
        std::fprintf(stderr, "vulkan: no %squeue\n",
                     surface_ ? "graphics+present " : "graphics ");
        return false;
    }

    // VK_KHR_portability_subset is REQUIRED to be enabled when the device
    // exposes it - the spec says so - and MoltenVK does.
    uint32_t en = 0;
    vkEnumerateDeviceExtensionProperties(phys_, nullptr, &en, nullptr);
    std::vector<VkExtensionProperties> exts(en);
    vkEnumerateDeviceExtensionProperties(phys_, nullptr, &en, exts.data());
    std::vector<const char*> want;
    for (const auto& e : exts)
        if (std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0)
            want.push_back("VK_KHR_portability_subset");
    if (surface_ != VK_NULL_HANDLE) want.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    const float pri = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = qfam_; qi.queueCount = 1; qi.pQueuePriorities = &pri;
    // Anisotropic filtering is a device FEATURE, asked for at creation and
    // only when the enhancement wants it AND the device has it; the base
    // renderer enables no feature at all.
    VkPhysicalDeviceFeatures have{};
    vkGetPhysicalDeviceFeatures(phys_, &have);
    VkPhysicalDeviceFeatures enable{};
    // ...and enabled whenever the device has it, asked or not - it changes
    // nothing a sampler does not ask for, and OMK's options menu may ask later
    // (`retexture`)
    if (have.samplerAnisotropy) { enable.samplerAnisotropy = VK_TRUE; anisoAvail_ = true; }
    if (aniso_ > 1 && filter_ >= 2 && have.samplerAnisotropy) {
        anisoOn_ = true;
    } else if (aniso_ > 1) {
        std::fprintf(stderr, "vulkan: anisotropy %d ignored - %s\n", aniso_,
                     filter_ < 2 ? "it needs trilinear filtering (a mip chain)"
                                 : "the device has no samplerAnisotropy feature");
    }
    // THE PACK'S BLOCK FORMATS (`o3de/texpack.h`): BC7 for the colours and
    // the normal maps, BC4 for the light maps - read from the pack's KTX2
    // files when the device samples and filters both (Apple's GPUs through
    // MoltenVK do). `OMK_NO_BLOCK_FORMATS=1` keeps the PNGs, for comparison.
    if (have.textureCompressionBC && !omk::envFlag("OMK_NO_BLOCK_FORMATS")) {
        bool all = true;
        for (const VkFormat f : {VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_BC4_UNORM_BLOCK}) {
            VkFormatProperties fp{};
            vkGetPhysicalDeviceFormatProperties(phys_, f, &fp);
            const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
            all = all && (fp.optimalTilingFeatures & need) == need;
        }
        if (all) { enable.textureCompressionBC = VK_TRUE; bcOk_ = true; }
    }
    omk::texturePack().setCodecs(bcOk_ ? omk::kCodecBc : 0u);
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
    di.pEnabledFeatures = &enable;
    di.enabledExtensionCount = static_cast<uint32_t>(want.size());
    di.ppEnabledExtensionNames = want.empty() ? nullptr : want.data();
    VKCHECK(vkCreateDevice(phys_, &di, nullptr, &dev_), "vkCreateDevice");
    vkGetDeviceQueue(dev_, qfam_, 0, &queue_);

    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = qfam_;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VKCHECK(vkCreateCommandPool(dev_, &pi, nullptr, &pool_), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool_; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VKCHECK(vkAllocateCommandBuffers(dev_, &cai, &cb_), "vkAllocateCommandBuffers");
#if OMK_REMASTER
    {
        VkQueryPoolCreateInfo lqi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        lqi.queryType = VK_QUERY_TYPE_OCCLUSION; lqi.queryCount = kLevelQueries;
        if (vkCreateQueryPool(dev_, &lqi, nullptr, &levelPool_) != VK_SUCCESS) levelPool_ = VK_NULL_HANDLE;
    }
#endif
    if (omk::envFlag("OMK_GPU_TIMES")) {
        if (props_.limits.timestampComputeAndGraphics && props_.limits.timestampPeriod > 0.0f) {
            VkQueryPoolCreateInfo qpi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qpi.queryType = VK_QUERY_TYPE_TIMESTAMP; qpi.queryCount = kStamps;
            if (vkCreateQueryPool(dev_, &qpi, nullptr, &tsPool_) != VK_SUCCESS) tsPool_ = VK_NULL_HANDLE;
            qpi.queryCount = kPresStamps;
            if (vkCreateQueryPool(dev_, &qpi, nullptr, &tsPresPool_) != VK_SUCCESS) tsPresPool_ = VK_NULL_HANDLE;
        }
        std::printf("vulkan: the GPU's time per pass %s\n", tsPool_ ? "- every 60 frames (OMK_GPU_TIMES)"
                                                                 : "refused: the device has no timestamps");
    }
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VKCHECK(vkCreateFence(dev_, &fi, nullptr, &fence_), "vkCreateFence");
    return true;
}

bool VulkanRenderer::makeTarget() {
    // R8G8B8A8_UNORM rather than a 565 format: the readback quantises to 565
    // in software, which keeps the ONE 565 quantisation rule (`PORTING` A3 -
    // bring the other side into the framebuffer's space) in one place instead
    // of delegating it to a driver whose rounding is its own.
    // THE SAMPLE COUNT - the enhancement `setMultisample` asked for, cut to
    // what the device's colour, depth AND stencil limits all allow (the
    // mirror pass needs the stencil at the same count). 1 unless asked.
    samples_ = VK_SAMPLE_COUNT_1_BIT;
    if (wantSamples_ > 1) {
        const VkSampleCountFlags ok = props_.limits.framebufferColorSampleCounts
                                    & props_.limits.framebufferDepthSampleCounts
                                    & props_.limits.framebufferStencilSampleCounts;
        for (int n = wantSamples_; n > 1; n >>= 1)
            if (ok & static_cast<VkSampleCountFlags>(n)) {
                samples_ = static_cast<VkSampleCountFlagBits>(n); break;
            }
        if (static_cast<int>(samples_) != wantSamples_)
            std::fprintf(stderr, "vulkan: %dx MSAA is not supported here - using %dx\n",
                         wantSamples_, static_cast<int>(samples_));
    }
    const bool msaa = samples_ != VK_SAMPLE_COUNT_1_BIT;

    auto image = [&](VkFormat f, VkImageUsageFlags use, VkImageAspectFlags asp,
                     VkSampleCountFlagBits samples,
                     VkImage& img, VkDeviceMemory& mem, VkImageView& view) -> bool {
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = f;
        ii.extent = {static_cast<uint32_t>(rw_), static_cast<uint32_t>(rh_), 1};
        ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = samples; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = use; ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return makeImage2D(ii, asp, "render targets", img, mem, &view);
    };
    if (!image(VK_FORMAT_R8G8B8A8_UNORM,
               // SAMPLED for the present pass, which reads the frame where it is
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT,
               VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT,
               colour_, colourMem_, colourView_)) return false;
    // (TRANSIENT: resolved within the pass, never stored - `makeImage2D`)
    if (msaa && !image(VK_FORMAT_R8G8B8A8_UNORM,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT, samples_,
                       msColour_, msColourMem_, msColourView_)) return false;
    // A depth+STENCIL format, because the mirror pass confines its reflection
    // with a stencil. D32_SFLOAT_S8_UINT first, D24_UNORM_S8_UINT as the
    // fallback: which one a device supports is not a given, and MoltenVK's
    // answer differs from a desktop driver's.
    dsFmt_ = VK_FORMAT_UNDEFINED;
    for (VkFormat f : {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}) {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(phys_, f, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            dsFmt_ = f; break;
        }
    }
    if (dsFmt_ == VK_FORMAT_UNDEFINED) {
        std::fprintf(stderr, "vulkan: no depth+stencil format\n"); return false;
    }
    // ...SAMPLED and KEPT where the ambient occlusion reads it - and only there
    aoOk_ = false;
    if (ao_ > 0.0f) {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(phys_, dsFmt_, &fp);
        aoOk_ = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
        if (!aoOk_) std::fprintf(stderr, "vulkan: the depth format cannot be sampled - no ambient occlusion\n");
    }
    // (TRANSIENT where nothing reads it after the pass - the AO's absence)
    if (!image(dsFmt_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                           (aoOk_ ? VK_IMAGE_USAGE_SAMPLED_BIT : VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT),
               VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, samples_,
               depth_, depthMem_, depthView_)) return false;
    if (aoOk_) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = depth_; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = dsFmt_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        VKCHECK(vkCreateImageView(dev_, &vi, nullptr, &depthRead_), "vkCreateImageView(depth read)");
    }

    // Attachment 0 is what the pipelines rasterise into: `colour_` itself
    // without MSAA, `msColour_` with it - in which case it is not kept
    // (DONT_CARE) and attachment 2 is the resolve into `colour_`, which then
    // ends in the same TRANSFER_SRC layout the readback and the blit expect,
    // so nothing downstream knows which way the frame was made.
    VkAttachmentDescription att[3]{};
    att[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    att[0].samples = samples_;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                              : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    att[1].format = dsFmt_;
    att[1].samples = samples_;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = aoOk_ ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // the stencil lives within the pass (the mirror's, cleared at its start):
    // nothing loads it after, so it is not stored (the audit's 4K trims)
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    att[2].format = VK_FORMAT_R8G8B8A8_UNORM;
    att[2].samples = VK_SAMPLE_COUNT_1_BIT;
    att[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[2].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference cref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference rref{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1; sub.pColorAttachments = &cref;
    sub.pResolveAttachments = msaa ? &rref : nullptr;
    sub.pDepthStencilAttachment = &dref;
    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = msaa ? 3 : 2; rpi.pAttachments = att;
    rpi.subpassCount = 1; rpi.pSubpasses = &sub;
    VKCHECK(vkCreateRenderPass(dev_, &rpi, nullptr, &pass_), "vkCreateRenderPass");
    // ...and with the AO, the WATER REFLECTION's own: the same but for the
    // depth's store - nothing reads the reflection's depth, and storing it is
    // a full extra depth write (on a tiled GPU at 4K). Store ops do not touch
    // compatibility: its framebuffer and every pipeline stay `pass_`'s.
    if (aoOk_) {
        att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        if (vkCreateRenderPass(dev_, &rpi, nullptr, &reflPass_) != VK_SUCCESS) reflPass_ = VK_NULL_HANDLE;
        att[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    // ...and the second floor level's, one sample (with MSAA it is
    // `reflPass1Keep_`'s): layouts and ops do not touch compatibility either
    if (!msaa) {
        const VkAttachmentDescription keep0 = att[0], keep1 = att[1];
        att[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        if (vkCreateRenderPass(dev_, &rpi, nullptr, &reflPassKeep_) != VK_SUCCESS) reflPassKeep_ = VK_NULL_HANDLE;
        att[0] = keep0; att[1] = keep1;
    }

    VkImageView views[3] = {msaa ? msColourView_ : colourView_, depthView_, colourView_};
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = pass_; fi.attachmentCount = msaa ? 3 : 2; fi.pAttachments = views;
    fi.width = static_cast<uint32_t>(rw_); fi.height = static_cast<uint32_t>(rh_);
    fi.layers = 1;
    VKCHECK(vkCreateFramebuffer(dev_, &fi, nullptr, &fbuf_), "vkCreateFramebuffer");

    // `readBuf_` and `fb_` are made by the first `readback`: a GPU-presented
    // run never reads a frame back, and at 4K they are 33 + 16 MB
    fb_ = omk::Surface();
    if (lazyImages_ > 0) {
        static bool said = false;
        if (!said)
            std::printf("vulkan: %d render-target attachments TRANSIENT in lazily allocated memory, %.0f MB "
                        "(memoryless on Apple's GPUs)\n", lazyImages_, static_cast<double>(lazyBytes_) / 1048576.0);
        said = true;
        omk::event("vulkan.memoryless", {{"images", lazyImages_}, {"mb", static_cast<double>(lazyBytes_) / 1048576.0}});
    }
    // a RESIZE stops here: what follows is sized once and kept
    if (resizing_) return true;

    // ---- THE SHADOW MAP's own target (`todo/enhancements.md` 6) ----------
    //
    // One depth image, its own render pass, and a sampler. It is sized once
    // and reused: the light's slab is fitted to the CASTERS every frame, so
    // the resolution follows them rather than the world.
    shFmt_ = VK_FORMAT_UNDEFINED;
    for (VkFormat f : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(phys_, f, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            shFmt_ = f; break;
        }
    }
    if (shFmt_ != VK_FORMAT_UNDEFINED) {
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = shFmt_;
        ii.extent = {kShadowSide, kShadowSide, 1};
        ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (makeImage2D(ii, VK_IMAGE_ASPECT_DEPTH_BIT, "shadow map", shImg_, shMem_, &shView_)) {
            // SHADER_READ from the start: every frame's scene set binds it,
            // and a run whose shadow pass never draws (no mapped shadows)
            // read it UNDEFINED (the validation layer's VUID-vkCmdDraw-None-09600,
            // found 2026-10-24); its pass starts from UNDEFINED as before
            {
                VkCommandBuffer cb = oneShotBegin();
                VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.image = shImg_; b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
                b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &b);
                oneShotEnd(cb);
            }

            VkAttachmentDescription da{};
            da.format = shFmt_; da.samples = VK_SAMPLE_COUNT_1_BIT;
            da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            da.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            da.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            da.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            da.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkAttachmentReference dr{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
            VkSubpassDescription sp{};
            sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            sp.pDepthStencilAttachment = &dr;
            // The frame's own pass READS what this one wrote, so the
            // dependency out has to name the fragment stage.
            VkSubpassDependency dep[2]{};
            dep[0].srcSubpass = VK_SUBPASS_EXTERNAL; dep[0].dstSubpass = 0;
            dep[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dep[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dep[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            dep[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dep[1].srcSubpass = 0; dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            dep[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dep[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            VkRenderPassCreateInfo shadowRpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            shadowRpi.attachmentCount = 1; shadowRpi.pAttachments = &da;
            shadowRpi.subpassCount = 1; shadowRpi.pSubpasses = &sp;
            shadowRpi.dependencyCount = 2; shadowRpi.pDependencies = dep;
            vkCreateRenderPass(dev_, &shadowRpi, nullptr, &shPass_);

            VkFramebufferCreateInfo shadowFi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            shadowFi.renderPass = shPass_; shadowFi.attachmentCount = 1; shadowFi.pAttachments = &shView_;
            shadowFi.width = kShadowSide; shadowFi.height = kShadowSide; shadowFi.layers = 1;
            vkCreateFramebuffer(dev_, &shadowFi, nullptr, &shFbuf_);

            // NEAREST, because the fragment shader compares depths itself and
            // a linear filter would average DEPTHS rather than the comparison.
            VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            si.magFilter = si.minFilter = VK_FILTER_NEAREST;
            si.addressModeU = si.addressModeV = si.addressModeW =
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            si.maxLod = 0.0f;
            vkCreateSampler(dev_, &si, nullptr, &shSampler_);

            makeBuffer(sizeof(ShadowUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       shUbo_, shUboMem_);
            vkMapMemory(dev_, shUboMem_, 0, sizeof(ShadowUbo), 0, &shUboPtr_);
            if (shUboPtr_) std::memset(shUboPtr_, 0, sizeof(ShadowUbo));
            makeBuffer(sizeof(LightUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       litUbo_, litUboMem_);
            vkMapMemory(dev_, litUboMem_, 0, sizeof(LightUbo), 0, &litUboPtr_);
            if (litUboPtr_) std::memset(litUboPtr_, 0, sizeof(LightUbo));
            // the tiled point lights' buffers, only for a frame that sends
            // the emitters' lights: without them no tiled pipeline is bound,
            // and the 32 MB stayed mapped for nothing (`verify.py: engine:
            // gpu readback on demand`, 2026-10-21)
            if (emitters_) {
                makeBuffer(kPointsBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           pointsBuf_, pointsMem_);
                vkMapMemory(dev_, pointsMem_, 0, kPointsBytes, 0, &pointsPtr_);
                // TWO HALVES: the frame's camera's tiles, then the water
                // reflection's - binned for ITS camera (`ds1Refl_`)
                makeBuffer(kTileWordsMax * 4 * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           tilesBuf_, tilesMem_);
                vkMapMemory(dev_, tilesMem_, 0, kTileWordsMax * 4 * 2, 0, &tilesPtr_);
                if (tilesPtr_) {
                    std::memset(tilesPtr_, 0, 16);
                    std::memset(static_cast<char*>(tilesPtr_) + kTileWordsMax * 4, 0, 16);
                }
            }
        }
    }
    // NO SHADOW MAP, NO RENDERER: set 1 is made with it - the shadow map, the
    // lights' uniforms, the tiled lights' buffers - and every scene pipeline
    // reads that set, so a frame without it bound draws on nothing (MoltenVK
    // faults in its first draw: `OMK_VK_IMAGE_FAIL="shadow map"`, 2026-10-24).
    // Refused here like a render target, and the frontend falls back. (It
    // used to say the fitted blobs would stand in - they never could.)
    if (shFbuf_ == VK_NULL_HANDLE) {
        std::fprintf(stderr, "vulkan: no shadow map here - its set carries the scene's lights too, refused\n");
        return false;
    }
    return true;
}

// THE TEXTURES' SAMPLER, by `filter_` and `anisoOn_` - made at the
// pipelines', and again by `retexture`
bool VulkanRenderer::makeSampler() {
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    // NEAREST, and REPEAT. Nearest because the software rasterizer's `sample()`
    // takes one texel with no filtering and the two are being differenced;
    // repeat because `% t.width` is what lets one atlas tile across a wall.
    si.magFilter = VK_FILTER_NEAREST; si.minFilter = VK_FILTER_NEAREST;
    // ...unless the filtering ENHANCEMENT asked for bilinear (renderer.h).
    // The colour key survives it because `upload` puts the key in ALPHA and
    // the shader treats a filtered sample as premultiplied.
    if (filter_ >= 1) { si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR; }
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    // Trilinear: the mip chain `upload` generates, blended between levels.
    // With one level (every mode below 2) maxLod 0 samples level 0 exactly.
    if (filter_ >= 2) { si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; si.maxLod = VK_LOD_CLAMP_NONE; }
    if (anisoOn_) {
        si.anisotropyEnable = VK_TRUE;
        si.maxAnisotropy = std::min(static_cast<float>(aniso_), props_.limits.maxSamplerAnisotropy);
    }
    VKCHECK(vkCreateSampler(dev_, &si, nullptr, &sampler_), "vkCreateSampler");
    return true;
}

// THE TEXTURE FILTER and the ANISOTROPY asked again on a running renderer
// (OMK's options menu, `todo/graphics-menu.md` 3d). Both are baked: the
// sampler is written into every texture's set, and trilinear's mip chain is
// made at upload. So a new sampler (the old one RETIRED, not destroyed - the
// 1x1 stand-ins' sets keep it, a 1x1 texel filtering alike either way) and
// every upload dropped, the preloaded ones unpinned too; the caller hands the
// pool over again, and it uploads as a start with the values would.
bool VulkanRenderer::retexture(int filter, int aniso) {
    filter_ = filter < 1 ? 0 : filter < 2 ? 1 : 2;
    aniso_ = aniso < 1 ? 1 : aniso > 16 ? 16 : aniso;
    if (dev_ == VK_NULL_HANDLE) return true;
    vkDeviceWaitIdle(dev_); settleIdle();
    anisoOn_ = aniso_ > 1 && filter_ >= 2 && anisoAvail_;
    retiredSamplers_.push_back(sampler_);
    sampler_ = VK_NULL_HANDLE;
    if (!makeSampler()) return false;
    setPreloadTextures({});
    poolTextures({}, false);
    std::printf("vulkan: retextured - %s filtering, anisotropy %d; every texture dropped, uploaded again\n",
                filter_ == 0 ? "nearest" : filter_ == 1 ? "bilinear" : "trilinear", anisoOn_ ? aniso_ : 1);
    return true;
}

bool VulkanRenderer::makePipelines() {
    // SET 0 a texture's: binding 0 the texture, 1 its LIGHT MAP (the bloom's;
    // black without one), 2 the WATER pass's reflection (black without one),
    // 3 its NORMAL MAP (the relief's; flat, alpha 0, without one)
    VkDescriptorSetLayoutBinding b2tex[4]{};
    for (uint32_t k = 0; k < 4; ++k) {
        b2tex[k].binding = k; b2tex[k].descriptorCount = 1;
        b2tex[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b2tex[k].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 4; dli.pBindings = b2tex;
    VKCHECK(vkCreateDescriptorSetLayout(dev_, &dli, nullptr, &dsl_), "descriptorSetLayout");

    // SET 1 is per FRAME - the shadow map and the light's transform. It exists
    // whether or not a shadow is drawn, because a pipeline layout is fixed at
    // creation and `strength = 0` is how a frame says it has no shadow.
    VkDescriptorSetLayoutBinding b1[5]{};
    b1[0].binding = 0; b1[0].descriptorCount = 1;
    b1[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b1[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    b1[1].binding = 1; b1[1].descriptorCount = 1;
    b1[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b1[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    b1[2].binding = 2; b1[2].descriptorCount = 1;
    b1[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b1[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // ...3 the frame's point lights and 4 their TILES (`o3de/lighttiles.h`)
    for (uint32_t k = 3; k < 5; ++k) {
        b1[k].binding = k; b1[k].descriptorCount = 1;
        b1[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b1[k].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dli1{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli1.bindingCount = 5; dli1.pBindings = b1;
    VKCHECK(vkCreateDescriptorSetLayout(dev_, &dli1, nullptr, &dsl1_), "descriptorSetLayout1");

    // SET 2 is the POSE BUFFER (`posesBodies`): one dynamic storage buffer,
    // offset per posed body, read by the vertex stage
    VkDescriptorSetLayoutBinding b2{};
    b2.binding = 0; b2.descriptorCount = 1;
    b2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    b2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo dli2{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli2.bindingCount = 1; dli2.pBindings = &b2;
    VKCHECK(vkCreateDescriptorSetLayout(dev_, &dli2, nullptr, &dsl2_), "descriptorSetLayout2");

    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(Push)};
    const VkDescriptorSetLayout sets[3] = {dsl_, dsl1_, dsl2_};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 3; pli.pSetLayouts = sets;
    pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
    VKCHECK(vkCreatePipelineLayout(dev_, &pli, nullptr, &plo_), "pipelineLayout");

    auto module = [&](const uint32_t* code, size_t bytes) {
        VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        si.codeSize = bytes; si.pCode = code;
        VkShaderModule m = VK_NULL_HANDLE;
        vkCreateShaderModule(dev_, &si, nullptr, &m);
        return m;
    };
    VkShaderModule vs = module(kVert, sizeof(kVert));
    VkShaderModule fs = module(kFrag, sizeof(kFrag));
    VkShaderModule fsTiled = module(kFragTiled, sizeof(kFragTiled));
    VkShaderModule fsCut = module(kFragCut, sizeof(kFragCut));
    VkShaderModule fsTiledCut = module(kFragTiledCut, sizeof(kFragTiledCut));
    if (!vs || !fs) { std::fprintf(stderr, "vulkan: shader module\n"); return false; }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};

    VkVertexInputBindingDescription vb{0, sizeof(GpuVert), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[6] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVert, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(GpuVert, u)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVert, r)},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVert, nx)},
        {4, 0, VK_FORMAT_R32_SFLOAT,       offsetof(GpuVert, phase)},
        {5, 0, VK_FORMAT_R32_SFLOAT,       offsetof(GpuVert, mesh)},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 6; vi.pVertexAttributeDescriptions = va;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // DYNAMIC, because the letterbox is a viewport: camera mode draws into the
    // top-left 800x440 of a window-sized attachment and free roaming into all
    // of it, and baking either into the pipeline would need two of every one.
    VkViewport vp{0, 0, static_cast<float>(rw_), static_cast<float>(rh_), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {static_cast<uint32_t>(rw_), static_cast<uint32_t>(rh_)}};
    VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vps.viewportCount = 1; vps.pViewports = &vp;
    vps.scissorCount = 1; vps.pScissors = &sc;
    const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dsi{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dsi.dynamicStateCount = 2; dsi.pDynamicStates = dyn;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    // D3DCULL_NONE - `Raster_DrawTriangles` sets it. The engine's own cull is
    // in software, before the device: `push_.cull` and scene.frag's discard.
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = samples_;   // 1 unless the enhancement is on

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    // the three pipelines are the three blend modes, by `blendRule`
    // (`o3de/drawcore.h`): opaque, additive (0x1000|0x2000, 211 meshes) and
    // multiply (0x1000|0x4000, 6 meshes and the mode-6 sprites - `dst * (1 -
    // src)`, darkening where the source is bright; DST_COLOR / ZERO would be
    // `dst * src`, the wrong law). Only an OPAQUE pass owns the depth.
    const auto vkFactor = [](omk::BlendFactor f) {
        return f == omk::BlendFactor::Zero ? VK_BLEND_FACTOR_ZERO
             : f == omk::BlendFactor::One  ? VK_BLEND_FACTOR_ONE
                                           : VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    };
    const omk::Blend kModes[3] = {omk::Blend::Opaque, omk::Blend::Add, omk::Blend::Mul};
    const auto blendState = [&](int k) {
        const omk::BlendRule rule = omk::blendRule(kModes[k]);
        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = 0xF;
        cba.blendEnable = rule.blend ? VK_TRUE : VK_FALSE;
        if (rule.blend) {
            cba.srcColorBlendFactor = vkFactor(rule.src);
            cba.dstColorBlendFactor = vkFactor(rule.dst);
            cba.colorBlendOp = VK_BLEND_OP_ADD;
            // the alpha's factors are this backend's own (`BlendRule`)
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.dstAlphaBlendFactor = kModes[k] == omk::Blend::Add ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
            cba.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        return cba;
    };
    for (int k = 0; k < 3; ++k) {
        const VkPipelineColorBlendAttachmentState cba = blendState(k);
        ds.depthWriteEnable = omk::blendRule(kModes[k]).depthWrite ? VK_TRUE : VK_FALSE;

        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.stageCount = 2; gp.pStages = stages;
        gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vps; gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms; gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dsi; gp.layout = plo_; gp.renderPass = pass_;
        VKCHECK(vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipe_[k]),
                "vkCreateGraphicsPipelines");
        // ...and the same with the TILED fragment module
        if (fsTiled) {
            VkPipelineShaderStageCreateInfo st2[2] = {stages[0], stages[1]};
            st2[1].module = fsTiled;
            gp.pStages = st2;
            if (vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeTiled_[k]) != VK_SUCCESS)
                pipeTiled_[k] = VK_NULL_HANDLE;
            gp.pStages = stages;
        }
        // ...and the OPAQUE CUTOUT's, the depth the shader's (only an opaque
        // draw writes depth); without one a cutout draw keeps the plain
        if (k == 0) {
            VkPipelineShaderStageCreateInfo st3[2] = {stages[0], stages[1]};
            gp.pStages = st3;
            st3[1].module = fsCut;
            if (!fsCut || vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeCut_) != VK_SUCCESS)
                pipeCut_ = VK_NULL_HANDLE;
            st3[1].module = fsTiledCut;
            if (!fsTiled || !fsTiledCut ||
                vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeTiledCut_) != VK_SUCCESS)
                pipeTiledCut_ = VK_NULL_HANDLE;
            gp.pStages = stages;
        }
    }
#if OMK_REMASTER
    // ---- the second level's PROBE (`pipeProbe_`): the plain module, the frame's pass
    {
        VkPipelineColorBlendAttachmentState none{};   // no colour written
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1; cb.pAttachments = &none;
        ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        ds.depthWriteEnable = VK_FALSE;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.stageCount = 2; gp.pStages = stages;
        gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vps; gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms; gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dsi; gp.layout = plo_; gp.renderPass = pass_;
        if (vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeProbe_) != VK_SUCCESS)
            pipeProbe_ = VK_NULL_HANDLE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
    }
#endif
    // ---- THE FLOORS' REFLECTION at one sample (`reflPass1_`), with MSAA on
    if (samples_ != VK_SAMPLE_COUNT_1_BIT) {
        VkAttachmentDescription a1[2]{};
        a1[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        a1[0].samples = VK_SAMPLE_COUNT_1_BIT;
        a1[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a1[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a1[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a1[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a1[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a1[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;   // as the MSAA pass's resolve leaves it
        a1[1].format = dsFmt_;
        a1[1].samples = VK_SAMPLE_COUNT_1_BIT;
        a1[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a1[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a1[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a1[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a1[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a1[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference c1{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference d1{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub1{};
        sub1.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub1.colorAttachmentCount = 1; sub1.pColorAttachments = &c1;
        sub1.pDepthStencilAttachment = &d1;
        VkRenderPassCreateInfo rp1{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp1.attachmentCount = 2; rp1.pAttachments = a1;
        rp1.subpassCount = 1; rp1.pSubpasses = &sub1;
        if (vkCreateRenderPass(dev_, &rp1, nullptr, &reflPass1_) != VK_SUCCESS) reflPass1_ = VK_NULL_HANDLE;
        // ...and the second level's, its colour kept (`reflPassKeep_`'s rule)
        a1[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        if (reflPass1_ && vkCreateRenderPass(dev_, &rp1, nullptr, &reflPass1Keep_) != VK_SUCCESS)
            reflPass1Keep_ = VK_NULL_HANDLE;
        VkPipelineMultisampleStateCreateInfo ms1{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms1.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        for (int k = 0; k < 3 && reflPass1_; ++k) {
            const VkPipelineColorBlendAttachmentState cba = blendState(k);
            ds.depthWriteEnable = omk::blendRule(kModes[k]).depthWrite ? VK_TRUE : VK_FALSE;
            VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            cb.attachmentCount = 1; cb.pAttachments = &cba;
            VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            gp.stageCount = 2; gp.pStages = stages;
            gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
            gp.pViewportState = &vps; gp.pRasterizationState = &rs;
            gp.pMultisampleState = &ms1; gp.pDepthStencilState = &ds;
            gp.pColorBlendState = &cb;
            gp.pDynamicState = &dsi; gp.layout = plo_; gp.renderPass = reflPass1_;
            if (vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipe1_[k]) != VK_SUCCESS)
                pipe1_[k] = VK_NULL_HANDLE;
            if (fsTiled) {
                VkPipelineShaderStageCreateInfo st2[2] = {stages[0], stages[1]};
                st2[1].module = fsTiled;
                gp.pStages = st2;
                if (vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeTiled1_[k]) != VK_SUCCESS)
                    pipeTiled1_[k] = VK_NULL_HANDLE;
            }
        }
        // all or none: a missing one and the floors keep the MSAA pass
        for (int k = 0; k < 3; ++k)
            if (!pipe1_[k] || (fsTiled && !pipeTiled1_[k])) {
                for (auto& p : pipe1_) if (p) { vkDestroyPipeline(dev_, p, nullptr); p = VK_NULL_HANDLE; }
                for (auto& p : pipeTiled1_) if (p) { vkDestroyPipeline(dev_, p, nullptr); p = VK_NULL_HANDLE; }
                if (reflPass1_) { vkDestroyRenderPass(dev_, reflPass1_, nullptr); reflPass1_ = VK_NULL_HANDLE; }
                if (reflPass1Keep_) { vkDestroyRenderPass(dev_, reflPass1Keep_, nullptr); reflPass1Keep_ = VK_NULL_HANDLE; }
                break;
            }
    }
    // ---- the mirror pass's pipelines.
    //
    // A stencil mirror, the classic construction, and each state below is one
    // step of it:
    //
    //   1. `pipeStencil_` draws the mirror's own faces with the colour mask
    //      OFF and depth write off, replacing the stencil with 1 wherever they
    //      pass the depth test. So the mark is the mirror as actually SEEN -
    //      occluded parts of the plane do not get marked, which is the bug the
    //      CPU version had to be taught by hand.
    //   2. `pipeDepthReset_` puts depth back to the far plane inside the mark,
    //      so the room already drawn does not occlude the reflection.
    //   3. `pipeRefl_[blend]` is the ordinary scene state with the stencil TEST
    //      set to EQUAL 1, so the reflection can only land on the mirror.
    //
    // Then the mirror's own faces draw again with their real blend, over the
    // reflection - which is what the blend mode is for (ASSETS 4c).
    auto stencilOp = [](VkStencilOp pass, VkCompareOp cmp, uint32_t wmask) {
        VkStencilOpState o{};
        o.failOp = VK_STENCIL_OP_KEEP;
        o.depthFailOp = VK_STENCIL_OP_KEEP;
        o.passOp = pass;
        o.compareOp = cmp;
        o.compareMask = 0xFF;
        o.writeMask = wmask;
        o.reference = 1;
        return o;
    };
    {
        VkPipelineColorBlendAttachmentState none{};
        none.blendEnable = VK_FALSE;
        none.colorWriteMask = 0;              // marks the stencil, draws nothing
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1; cb.pAttachments = &none;

        VkPipelineDepthStencilStateCreateInfo sd{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        sd.depthTestEnable = VK_TRUE;
        sd.depthWriteEnable = VK_FALSE;
        sd.depthCompareOp = VK_COMPARE_OP_LESS;
        sd.stencilTestEnable = VK_TRUE;
        sd.front = sd.back = stencilOp(VK_STENCIL_OP_REPLACE, VK_COMPARE_OP_ALWAYS, 0xFF);

        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.stageCount = 2; gp.pStages = stages;
        gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vps; gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms; gp.pDepthStencilState = &sd;
        gp.pColorBlendState = &cb; gp.layout = plo_; gp.renderPass = pass_;
        VKCHECK(vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeStencil_),
                "stencil pipeline");

        // the depth reset: a full-screen triangle, no vertex input at all
        VkShaderModule qv = module(kFsqVert, sizeof(kFsqVert));
        VkShaderModule qf = module(kFsqFrag, sizeof(kFsqFrag));
        VkPipelineShaderStageCreateInfo qstages[2]{};
        qstages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_VERTEX_BIT, qv, "main", nullptr};
        qstages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_FRAGMENT_BIT, qf, "main", nullptr};
        VkPipelineVertexInputStateCreateInfo qvi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineDepthStencilStateCreateInfo qd{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        qd.depthTestEnable = VK_TRUE;
        qd.depthWriteEnable = VK_TRUE;
        qd.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        qd.stencilTestEnable = VK_TRUE;
        qd.front = qd.back = stencilOp(VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 0);
        gp.pStages = qstages;
        gp.pVertexInputState = &qvi;
        gp.pDepthStencilState = &qd;
        VKCHECK(vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeDepthReset_),
                "depth reset pipeline");
        vkDestroyShaderModule(dev_, qv, nullptr);
        vkDestroyShaderModule(dev_, qf, nullptr);
    }

    // and the reflection's three, the scene states with the stencil test on
    for (int k = 0; k < 3; ++k) {
        const VkPipelineColorBlendAttachmentState cba = blendState(k);
        VkPipelineDepthStencilStateCreateInfo rd{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        rd.depthTestEnable = VK_TRUE;
        rd.depthWriteEnable = omk::blendRule(kModes[k]).depthWrite ? VK_TRUE : VK_FALSE;
        rd.depthCompareOp = VK_COMPARE_OP_LESS;
        rd.stencilTestEnable = VK_TRUE;
        rd.front = rd.back = stencilOp(VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 0);
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1; cb.pAttachments = &cba;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.stageCount = 2; gp.pStages = stages;
        gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vps; gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms; gp.pDepthStencilState = &rd;
        gp.pColorBlendState = &cb; gp.layout = plo_; gp.renderPass = pass_;
        VKCHECK(vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeRefl_[k]),
                "reflection pipeline");
    }

    vkDestroyShaderModule(dev_, vs, nullptr);
    vkDestroyShaderModule(dev_, fs, nullptr);
    if (fsTiled) vkDestroyShaderModule(dev_, fsTiled, nullptr);
    if (fsCut) vkDestroyShaderModule(dev_, fsCut, nullptr);
    if (fsTiledCut) vkDestroyShaderModule(dev_, fsTiledCut, nullptr);

    if (!makeSampler()) return false;

    // 64 sets: the engine's texture pool is 58 slots, plus the white fallback.
    // Plus set 1, the per-frame shadow set, and its one uniform buffer.
    // two samplers a texture's set since the light map (binding 1)
    VkDescriptorPoolSize ps[4] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 531},   // four a texture's set
                                  {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8},
                                  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1},
                                  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6}};   // set 1's tiled lights
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    // (+2: the reflections' own sets 1, `ds1Refl_` and `ds1Refl2_`)
    dpi.maxSets = 133; dpi.poolSizeCount = 4; dpi.pPoolSizes = ps;
    // a texture's set is freed on its own when it leaves the pool (`setTextures`)
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    VKCHECK(vkCreateDescriptorPool(dev_, &dpi, nullptr, &dpool_), "descriptorPool");

    // ---- the POSE BUFFER and its set (set 2) ------------------------------
    // Without it every body is posed on the CPU (`posesBodies` no).
    poseAlign_ = std::max<VkDeviceSize>(16, props_.limits.minStorageBufferOffsetAlignment);
    if (makeBuffer(kPoseBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   poseBuf_, poseMem_) &&
        vkMapMemory(dev_, poseMem_, 0, kPoseBytes, 0, &posePtr_) == VK_SUCCESS) {
        VkDescriptorSetAllocateInfo sai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        sai.descriptorPool = dpool_; sai.descriptorSetCount = 1; sai.pSetLayouts = &dsl2_;
        if (vkAllocateDescriptorSets(dev_, &sai, &poseDs_) == VK_SUCCESS) {
            VkDescriptorBufferInfo bufi{poseBuf_, 0, kPoseRange};
            VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            wr.dstSet = poseDs_; wr.dstBinding = 0; wr.descriptorCount = 1;
            wr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
            wr.pBufferInfo = &bufi;
            vkUpdateDescriptorSets(dev_, 1, &wr, 0, nullptr);
        } else {
            poseDs_ = VK_NULL_HANDLE;
        }
    }
    if (poseDs_ == VK_NULL_HANDLE)
        std::fprintf(stderr, "vulkan: no pose buffer - every body posed on the CPU\n");

    // ---- the SHADOW pipeline and its one descriptor set -------------------
    if (shFbuf_ != VK_NULL_HANDLE) {
        makeShadowSet();

        // A DEPTH-ONLY pipeline: one vertex stage, no colour attachment, and
        // a depth bias, without which the ground shadows itself in stripes.
        VkShaderModule svs = module(kShadowVert, sizeof(kShadowVert));
        VkPushConstantRange spcr{VK_SHADER_STAGE_VERTEX_BIT, 0, 16 * sizeof(float)};
        VkPipelineLayoutCreateInfo spli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        spli.pushConstantRangeCount = 1; spli.pPushConstantRanges = &spcr;
        VKCHECK(vkCreatePipelineLayout(dev_, &spli, nullptr, &shPlo_), "shadow pipelineLayout");
        VkPipelineShaderStageCreateInfo sst{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
            VK_SHADER_STAGE_VERTEX_BIT, svs, "main", nullptr};
        VkVertexInputBindingDescription svb{0, sizeof(GpuVert), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription sva{0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                              offsetof(GpuVert, x)};
        VkPipelineVertexInputStateCreateInfo svi{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        svi.vertexBindingDescriptionCount = 1; svi.pVertexBindingDescriptions = &svb;
        svi.vertexAttributeDescriptionCount = 1; svi.pVertexAttributeDescriptions = &sva;
        VkPipelineInputAssemblyStateCreateInfo sia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        sia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport svp{0, 0, static_cast<float>(kShadowSide),
                       static_cast<float>(kShadowSide), 0.0f, 1.0f};
        VkRect2D ssc{{0, 0}, {kShadowSide, kShadowSide}};
        VkPipelineViewportStateCreateInfo svs2{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        svs2.viewportCount = 1; svs2.pViewports = &svp;
        svs2.scissorCount = 1; svs2.pScissors = &ssc;
        VkPipelineRasterizationStateCreateInfo srs{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        srs.polygonMode = VK_POLYGON_MODE_FILL;
        // NO culling: a character model is not a closed solid, and a shadow
        // MAP (an enhancement the engine never had) wants every face that can
        // block the light, whichever way it winds. The engine's own draw does
        // cull back faces, in software (ASSETS 4b, `geom3do.h` kTwoSided).
        srs.cullMode = VK_CULL_MODE_NONE;
        srs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        srs.lineWidth = 1.0f;
        srs.depthBiasEnable = VK_TRUE;
        srs.depthBiasConstantFactor = 1.5f;
        srs.depthBiasSlopeFactor = 2.5f;
        VkPipelineMultisampleStateCreateInfo sms{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        sms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo sds{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        sds.depthTestEnable = VK_TRUE; sds.depthWriteEnable = VK_TRUE;
        sds.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendStateCreateInfo scb{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        scb.attachmentCount = 0;
        VkGraphicsPipelineCreateInfo sgp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        sgp.stageCount = 1; sgp.pStages = &sst;
        sgp.pVertexInputState = &svi; sgp.pInputAssemblyState = &sia;
        sgp.pViewportState = &svs2; sgp.pRasterizationState = &srs;
        sgp.pMultisampleState = &sms; sgp.pDepthStencilState = &sds;
        sgp.pColorBlendState = &scb; sgp.layout = shPlo_; sgp.renderPass = shPass_;
        if (vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &sgp, nullptr, &shPipe_)
            != VK_SUCCESS) {
            std::fprintf(stderr, "vulkan: no shadow pipeline - mapped shadows off\n");
            shPipe_ = VK_NULL_HANDLE;
        }
        vkDestroyShaderModule(dev_, svs, nullptr);
    }
    return true;
}

// SET 1, made once by `makePipelines` and kept as long as the pool: a
// texture's set now leaves the pool on its own (`FREE_DESCRIPTOR_SET`), and
// nothing resets it. (It was a function because `setTextures` once RESET the
// pool, freeing every set - this one included - and was remade after each:
// the first version, made once, was quietly destroyed by the game's first
// `setTextures` and no shadow was ever drawn, while the synthetic probe's EMPTY
// texture list never reset and worked - the bug was in the difference between
// the two callers, not in the shadow.)
void VulkanRenderer::makeShadowSet() {
    if (shFbuf_ == VK_NULL_HANDLE || dpool_ == VK_NULL_HANDLE) return;
    VkDescriptorSetAllocateInfo sai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    sai.descriptorPool = dpool_; sai.descriptorSetCount = 1; sai.pSetLayouts = &dsl1_;
    ds1_ = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(dev_, &sai, &ds1_) != VK_SUCCESS) {
        std::fprintf(stderr, "vulkan: no shadow descriptor set\n");
        ds1_ = VK_NULL_HANDLE; return;
    }
    VkDescriptorBufferInfo bufi{shUbo_, 0, sizeof(ShadowUbo)};
    VkDescriptorImageInfo imgi{shSampler_, shView_,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo lbufi{litUbo_, 0, sizeof(LightUbo)};
    VkDescriptorBufferInfo pbufi{pointsBuf_, 0, kPointsBytes};
    VkDescriptorBufferInfo tbufi{tilesBuf_, 0, kTileWordsMax * 4};
    // ...and the water REFLECTION's twin, its tiles the buffer's second half
    if (pointsBuf_ && tilesBuf_) {
        if (ds1Refl_) { vkFreeDescriptorSets(dev_, dpool_, 1, &ds1Refl_); ds1Refl_ = VK_NULL_HANDLE; }
        if (vkAllocateDescriptorSets(dev_, &sai, &ds1Refl_) != VK_SUCCESS) ds1Refl_ = VK_NULL_HANDLE;
        if (ds1Refl2_) { vkFreeDescriptorSets(dev_, dpool_, 1, &ds1Refl2_); ds1Refl2_ = VK_NULL_HANDLE; }
        if (vkAllocateDescriptorSets(dev_, &sai, &ds1Refl2_) != VK_SUCCESS) ds1Refl2_ = VK_NULL_HANDLE;
    }
    VkWriteDescriptorSet wr[5]{};
    wr[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr[0].dstSet = ds1_; wr[0].dstBinding = 0; wr[0].descriptorCount = 1;
    wr[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wr[0].pBufferInfo = &bufi;
    wr[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr[1].dstSet = ds1_; wr[1].dstBinding = 1; wr[1].descriptorCount = 1;
    wr[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr[1].pImageInfo = &imgi;
    wr[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr[2].dstSet = ds1_; wr[2].dstBinding = 2; wr[2].descriptorCount = 1;
    wr[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wr[2].pBufferInfo = &lbufi;
    for (uint32_t k = 3; k < 5; ++k) {
        wr[k] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wr[k].dstSet = ds1_; wr[k].dstBinding = k; wr[k].descriptorCount = 1;
        wr[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        wr[k].pBufferInfo = k == 3 ? &pbufi : &tbufi;
    }
    vkUpdateDescriptorSets(dev_, pointsBuf_ && tilesBuf_ ? 5 : 3, wr, 0, nullptr);
    if (ds1Refl_) {
        VkDescriptorBufferInfo rbufi{tilesBuf_, kTileWordsMax * 4, kTileWordsMax * 4};
        for (auto& w : wr) w.dstSet = ds1Refl_;
        wr[4].pBufferInfo = &rbufi;
        vkUpdateDescriptorSets(dev_, 5, wr, 0, nullptr);
    }
    if (ds1Refl2_) {
        VkDescriptorBufferInfo rbufi2{tilesBuf_, kTileWordsMax * 4 + kTileWordsMax * 2, kTileWordsMax * 2};
        for (auto& w : wr) w.dstSet = ds1Refl2_;
        wr[4].pBufferInfo = &rbufi2;
        vkUpdateDescriptorSets(dev_, 5, wr, 0, nullptr);
    }
}

// The swapchain, and the presentation path.
//
// The frame is drawn into the SAME offscreen colour attachment the offscreen
// path uses and then BLITTED into the acquired swapchain image. Two reasons,
// and neither is laziness: `readback()` must keep working (it is what the
// checks and the mirror pass use), and the offscreen target is 640x352 while
// the window is 640x480 - the game's camera mode is letterboxed, so the blit
// lands at y=64 and the bands are the clear colour, which is what the engine's
// own frames show.
//
// `vkCmdBlitImage` rather than `vkCmdCopyImage` because the swapchain's format
// is the surface's choice and is usually B8G8R8A8 while the attachment is
// R8G8B8A8; a copy would reinterpret the bytes and swap red for blue, a blit
// converts.
bool VulkanRenderer::makeSwapchain(int w, int h) {
    VkSurfaceCapabilitiesKHR caps{};
    VKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, surface_, &caps),
            "surfaceCapabilities");
    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &nf, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(nf ? nf : 1);
    if (nf) vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &nf, fmts.data());
    if (!nf) { std::fprintf(stderr, "vulkan: no surface format\n"); return false; }
    VkSurfaceFormatKHR pick = fmts[0];
    for (const auto& f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
            pick = f; break;
        }
    swapFmt_ = pick.format;
    swapExt_ = caps.currentExtent.width != 0xFFFFFFFFu
             ? caps.currentExtent
             : VkExtent2D{static_cast<uint32_t>(w), static_cast<uint32_t>(h)};

    uint32_t want = caps.minImageCount + 1;
    if (caps.maxImageCount && want > caps.maxImageCount) want = caps.maxImageCount;
    VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sc.surface = surface_;
    sc.minImageCount = want;
    sc.imageFormat = swapFmt_;
    sc.imageColorSpace = pick.colorSpace;
    sc.imageExtent = swapExt_;
    sc.imageArrayLayers = 1;
    // TRANSFER_DST, not COLOR_ATTACHMENT: nothing renders into these, the
    // finished frame is blitted in.
    sc.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc.preTransform = caps.currentTransform;
    sc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // FIFO is the one mode every implementation must support, and it is
    // v-synced, which is what a 30 Hz game wants anyway.
    sc.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    // `OMK_PRESENT_IMMEDIATE=1`, an INSTRUMENT: no v-sync where the surface
    // offers IMMEDIATE, so an unpaced run's frame time is the frame's own
    // cost and not a multiple of the display's refresh (measuring the GPU)
    if (omk::envFlag("OMK_PRESENT_IMMEDIATE")) {
        uint32_t nm = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(phys_, surface_, &nm, nullptr);
        std::vector<VkPresentModeKHR> modes(nm);
        vkGetPhysicalDeviceSurfacePresentModesKHR(phys_, surface_, &nm, modes.data());
        for (const auto m : modes)
            if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) sc.presentMode = m;
        std::printf("vulkan: OMK_PRESENT_IMMEDIATE - present mode %s\n",
                    sc.presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE (no v-sync)"
                                                                     : "FIFO (IMMEDIATE not offered)");
    }
    sc.clipped = VK_TRUE;
    VKCHECK(vkCreateSwapchainKHR(dev_, &sc, nullptr, &swap_), "vkCreateSwapchainKHR");

    uint32_t ni = 0;
    vkGetSwapchainImagesKHR(dev_, swap_, &ni, nullptr);
    swapImgs_.resize(ni);
    vkGetSwapchainImagesKHR(dev_, swap_, &ni, swapImgs_.data());

    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VKCHECK(vkCreateSemaphore(dev_, &si, nullptr, &acquired_), "semaphore");
    drawn_.assign(ni, VK_NULL_HANDLE);
    for (auto& d : drawn_) VKCHECK(vkCreateSemaphore(dev_, &si, nullptr, &d), "semaphore");
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool_; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VKCHECK(vkAllocateCommandBuffers(dev_, &cai, &pcb_), "present cb");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VKCHECK(vkCreateFence(dev_, &fi, nullptr, &pfence_), "present fence");
    std::printf("vulkan: swapchain %ux%u, %u images, format %d\n",
                swapExt_.width, swapExt_.height, ni, static_cast<int>(swapFmt_));
    return true;
}

void VulkanRenderer::killSwapchain() {
    if (!dev_) return;
    if (pcb_) { vkFreeCommandBuffers(dev_, pool_, 1, &pcb_); pcb_ = VK_NULL_HANDLE; }
    if (pfence_) { vkDestroyFence(dev_, pfence_, nullptr); pfence_ = VK_NULL_HANDLE; }
    if (acquired_) { vkDestroySemaphore(dev_, acquired_, nullptr); acquired_ = VK_NULL_HANDLE; }
    for (auto& d : drawn_) if (d) vkDestroySemaphore(dev_, d, nullptr);
    drawn_.clear();
    if (swap_) { vkDestroySwapchainKHR(dev_, swap_, nullptr); swap_ = VK_NULL_HANDLE; }
    swapImgs_.clear();
}

bool VulkanRenderer::remakeSwapchain() {
    if (!dev_ || surface_ == VK_NULL_HANDLE) return false;
    vkDeviceWaitIdle(dev_); settleIdle();
    killSwapchain();
    return makeSwapchain(w_, h_);
}

bool VulkanRenderer::resize(int w, int h) {
    if (!dev_ || w <= 0 || h <= 0) return false;
    vkDeviceWaitIdle(dev_); settleIdle();
    const auto img = [&](VkImageView& v, VkImage& i, VkDeviceMemory& m) {
        if (v) vkDestroyImageView(dev_, v, nullptr);
        if (i) vkDestroyImage(dev_, i, nullptr);
        if (m) freeDeviceMemory(m);
        v = VK_NULL_HANDLE; i = VK_NULL_HANDLE; m = VK_NULL_HANDLE;
    };
    const auto buf = [&](VkBuffer& b, VkDeviceMemory& m) {
        if (b) vkDestroyBuffer(dev_, b, nullptr);
        if (m) freeDeviceMemory(m);
        b = VK_NULL_HANDLE; m = VK_NULL_HANDLE;
    };
    if (fbuf_) { vkDestroyFramebuffer(dev_, fbuf_, nullptr); fbuf_ = VK_NULL_HANDLE; }
    if (pass_) { vkDestroyRenderPass(dev_, pass_, nullptr); pass_ = VK_NULL_HANDLE; }
    if (reflPass_) { vkDestroyRenderPass(dev_, reflPass_, nullptr); reflPass_ = VK_NULL_HANDLE; }
    if (reflPassKeep_) { vkDestroyRenderPass(dev_, reflPassKeep_, nullptr); reflPassKeep_ = VK_NULL_HANDLE; }
    img(colourView_, colour_, colourMem_);
    img(msColourView_, msColour_, msColourMem_);
    if (depthRead_) { vkDestroyImageView(dev_, depthRead_, nullptr); depthRead_ = VK_NULL_HANDLE; }
    img(depthView_, depth_, depthMem_);
    buf(readBuf_, readMem_);
    buf(upBuf_, upMem_);
    killUpscale();   // before the present's pool and layout, which it borrows
    killUnder();     // it draws into the present pass's image
    // the present pass reads `colourView_`, so all of it goes; its guard is
    // `presPipe_`, and it is rebuilt at the new size on its next use
    if (presPipe_) { vkDestroyPipeline(dev_, presPipe_, nullptr); presPipe_ = VK_NULL_HANDLE; }
    if (presPlo_) { vkDestroyPipelineLayout(dev_, presPlo_, nullptr); presPlo_ = VK_NULL_HANDLE; }
    if (presSampler_) { vkDestroySampler(dev_, presSampler_, nullptr); presSampler_ = VK_NULL_HANDLE; }
    if (presPool_) { vkDestroyDescriptorPool(dev_, presPool_, nullptr); presPool_ = VK_NULL_HANDLE; }
    if (presDsl_) { vkDestroyDescriptorSetLayout(dev_, presDsl_, nullptr); presDsl_ = VK_NULL_HANDLE; }
    if (presFbuf_) { vkDestroyFramebuffer(dev_, presFbuf_, nullptr); presFbuf_ = VK_NULL_HANDLE; }
    if (presPass_) { vkDestroyRenderPass(dev_, presPass_, nullptr); presPass_ = VK_NULL_HANDLE; }
    img(presView_, presImg_, presMem_);
    buf(presRead_, presReadMem_);
    killPost();   // it reads `colour_` and is sized by the target
    killReflection();
    w_ = w; h_ = h;
    rw_ = rs(w_); rh_ = rs(h_);
    srcW_ = rw_; srcH_ = rh_;
    resizing_ = true;
    const bool ok = makeTarget();
    resizing_ = false;
    if (!ok) return false;
    std::printf("vulkan: frame target remade at %dx%d\n", w_, h_);
    return surface_ == VK_NULL_HANDLE || remakeSwapchain();
}

// A RETARGET (OMK's options menu, `todo/graphics-menu.md` 3c): the
// supersample, the upscale and the ambient occlusion asked again on a running
// renderer - the target remade by `resize` at the same size, which reads them
// (`makeTarget`: the render size, the readable depth). Viewport and scissor are
// dynamic state, so the pipelines stand. NOT MSAA: the scene pipelines are
// compiled for the sample count (`makePipelines`), so a different one is
// refused and waits for the next start.
bool VulkanRenderer::retarget(int samples, int ss, float up, float ao) {
    const int want = samples < 2 ? 1 : samples < 4 ? 2 : samples < 8 ? 4 : 8;
    if (dev_ != VK_NULL_HANDLE && want != wantSamples_) return false;
    wantSamples_ = want;
    ss_ = ss < 2 ? 1 : ss < 4 ? 2 : 4;
    up_ = up > 1.0f ? std::min(up, 3.0f) : 1.0f;
    ao_ = ao < 0.0f ? 0.0f : ao > 4.0f ? 4.0f : ao;
    if (up_ > 1.0f && ss_ > 1) {
        std::printf("vulkan: the upscale takes the supersample's place (--ssaa %d ignored)\n", ss_);
        ss_ = 1;
    }
    if (dev_ == VK_NULL_HANDLE) return true;
    const bool ok = resize(w_, h_);
    std::printf("vulkan: retargeted - %dx supersampling, upscale x%.2f, ambient occlusion %.2f%s\n", ss_,
                static_cast<double>(up_), static_cast<double>(ao_), aoOk_ || ao_ <= 0.0f ? "" : " (refused here)");
    return ok;
}

bool VulkanRenderer::presentDirect() {
    return presentImage(colour_, srcW_ ? srcW_ : rw_, srcH_ ? srcH_ : rh_);
}

// The swapchain half of every present: `src`'s top-left `sw x sh`, in
// TRANSFER_SRC layout, blitted into the window's middle rows.
bool VulkanRenderer::presentImage(VkImage src, int sw, int sh) {
    if (swap_ == VK_NULL_HANDLE) return false;
    // THE WINDOW MAY HAVE CHANGED SIZE - fullscreen, a resize - and a
    // swapchain at the old extent would present stretched or not at all.
    {
        VkSurfaceCapabilitiesKHR caps{};
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, surface_, &caps) == VK_SUCCESS &&
            caps.currentExtent.width != 0xFFFFFFFFu) {
            if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) return false;
            if (caps.currentExtent.width != swapExt_.width ||
                caps.currentExtent.height != swapExt_.height) {
                if (!remakeSwapchain()) return false;
            }
        }
    }
    uint32_t idx = 0;
    VkResult r = vkAcquireNextImageKHR(dev_, swap_, UINT64_MAX, acquired_,
                                       VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { remakeSwapchain(); return false; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(pcb_, 0);
    vkBeginCommandBuffer(pcb_, &bi);
    // a present with no pass of its own before it: the path starts here
    if (tsPresPool_ && !tsPresOpen_) { presStampStart(pcb_, false); presStamp(pcb_, 1); }
    presStamp(pcb_, 2);

    auto barrier = [&](VkImage img, VkImageLayout from, VkImageLayout to,
                       VkAccessFlags srcA, VkAccessFlags dstA,
                       VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = from; b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = srcA; b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(pcb_, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    // FROM THE TRANSFER STAGE, the one the submit waits for the acquire at:
    // from the top of the pipe the layout change was not ordered after the
    // acquire (the validation layer's SYNC-HAZARD-WRITE-AFTER-READ)
    barrier(swapImgs_[idx], VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // The letterbox bands. The engine clears to black and the picture occupies
    // the middle rows; clearing the whole image and blitting into the middle
    // reproduces that without a second attachment.
    VkClearColorValue black{{0.0f, 0.0f, 0.0f, 1.0f}};
    VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(pcb_, swapImgs_[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &black, 1, &all);
    // ...and the blit's write ordered after the clear's (WRITE_AFTER_WRITE)
    barrier(swapImgs_[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // The `w_ x h_` frame fitted into the window AT ITS OWN ASPECT, centred:
    // in a window of the frame's own width that is the old rule exactly (the
    // full width, `h_` rows in the middle), and fullscreen or a wider window
    // gets bars rather than a stretched picture.
    const int sW = static_cast<int>(swapExt_.width), sH = static_cast<int>(swapExt_.height);
    int dW = sW, dH = sW * h_ / w_;
    if (dH > sH) { dH = sH; dW = sH * w_ / h_; }
    if (sW == w_ && h_ <= sH) { dW = w_; dH = h_; }
    const int x0 = (sW - dW) / 2;
    const int y0 = (sH - dH) / 2;
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {sw, sh, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {x0, y0, 0};
    blit.dstOffsets[1] = {x0 + dW, y0 + dH, 1};
    vkCmdBlitImage(pcb_, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapImgs_[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   // LINEAR only when there is something to average: at ss 1
                   // this must stay NEAREST or every existing frame changes.
                   1, &blit, ss_ > 1 || up_ > 1.0f ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    presStamp(pcb_, 3);

    barrier(swapImgs_[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    vkEndCommandBuffer(pcb_);

    const VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1; si.pWaitSemaphores = &acquired_;
    si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1; si.pCommandBuffers = &pcb_;
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = &drawn_[idx];
    vkResetFences(dev_, 1, &pfence_);
    vkQueueSubmit(queue_, 1, &si, pfence_);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &drawn_[idx];
    pi.swapchainCount = 1; pi.pSwapchains = &swap_;
    pi.pImageIndices = &idx;
    vkQueuePresentKHR(queue_, &pi);
    vkWaitForFences(dev_, 1, &pfence_, VK_TRUE, UINT64_MAX);
    settleIdle();   // this fence covers the scene's and the passes' submits before it
    readPresStamps();
    return true;
}

// the present path's stamps, read after its fence (`tsPresPool_`)
void VulkanRenderer::readPresStamps() {
    if (!tsPresPool_ || !tsPresOpen_) return;
    tsPresOpen_ = false;
    uint64_t t[kPresStamps] = {};
    const uint32_t n = tsPresUp_ ? kPresStamps : 4;
    if (vkGetQueryPoolResults(dev_, tsPresPool_, 0, n, sizeof t, t, sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
    const double k = props_.limits.timestampPeriod / 1e6;
    const double up = tsPresUp_ ? static_cast<double>(t[4] - t[0]) * k : 0.0;
    tsPresSum_[0] += up;
    tsPresSum_[1] += static_cast<double>(t[1] - t[0]) * k - up;
    tsPresSum_[2] += static_cast<double>(t[3] - t[2]) * k;
    tsPresSum_[3] += static_cast<double>(t[3] - t[0]) * k;
    tsPresScreens_ += tsPresScreen_ ? 1 : 0;
    if (++tsPresFrames_ < 60) return;
    std::printf("vulkan: GPU ms a frame, the present path (mean of 60, %d of them a screen's) - upscale "
                "%.2f, the rest of its passes %.2f, the blit %.2f; from its start to the blit's end %.2f\n",
                tsPresScreens_, tsPresSum_[0] / 60.0, tsPresSum_[1] / 60.0, tsPresSum_[2] / 60.0,
                tsPresSum_[3] / 60.0);
    omk::event("gpu.present", {{"upscale", tsPresSum_[0] / 60.0}, {"passes", tsPresSum_[1] / 60.0},
                               {"blit", tsPresSum_[2] / 60.0}, {"span", tsPresSum_[3] / 60.0},
                               {"screens", tsPresScreens_}});
    for (auto& v : tsPresSum_) v = 0.0;
    tsPresFrames_ = tsPresScreens_ = 0;
}

// Present a CPU-composited frame - the mirror pass's output.
//
// `drawWithMirror` composites on the CPU because the pass lives on the
// boundary (`PORTING` A2) and must work for every backend, so on a frame where
// it ran the finished picture is in a `Surface` and not in the colour
// attachment. Uploading it costs one conversion and one transfer, which is
// what a direct-presented frame otherwise avoids entirely - so the mirror
// frame is the slow one, and a GPU composite (a stencil pass) is the named
// next step rather than something quietly pretended away.
// the fewest pixels a chunk of the expansion below is given: a quarter of a
// 640x480 frame, so a small window's stays on the caller's thread
constexpr std::size_t kExpandGrain = 640u * 480u / 4u;

bool VulkanRenderer::presentSurface(const omk::Surface& s) {
    if (swap_ == VK_NULL_HANDLE) return false;
    settle();   // `upBuf_` is written below
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w_) * h_ * 4;
    if (upBuf_ == VK_NULL_HANDLE &&
        !makeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    upBuf_, upMem_)) return false;
    void* p = nullptr;
    vkMapMemory(dev_, upMem_, 0, bytes, 0, &p);
    auto* dst = static_cast<unsigned char*>(p);
    const int n = std::min<int>(w_ * h_, static_cast<int>(s.px.size()));
    // 565 -> replicated RGBA8 through the 65536-entry table, the same bytes
    // the per-channel shifts gave (todo/optimization.md step 4) - SPLIT OVER
    // THE POOL, each chunk its own pixels: at 4K this was 8.3 million table
    // reads on one core, ~3 ms of every screen's frame (`todo/perf-audit.md`
    // 11). Not done on the GPU from a 565 texture: a UNORM read widens a
    // channel as round(c * 255 / 31), which is not the replication for 4 of
    // the 32 five-bit values and 10 of the 64 six-bit ones.
    const unsigned char* lut = omk::expand565Rgba();
    const std::uint16_t* src = s.px.data();
    omk::Threads::shared().parallelFor(0, static_cast<std::size_t>(std::max(n, 0)), kExpandGrain,
                                       [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i)
            std::memcpy(dst + 4 * i, lut + 4 * static_cast<std::size_t>(src[i]), 4);
    });
    vkUnmapMemory(dev_, upMem_);

    // UPSCALED, `colour_` is smaller than the picture: it goes into the
    // present pass's output-size image instead, and is presented from there
    const bool up = up_ > 1.0f;
    if (up && !makePresentPass()) return false;
    VkCommandBuffer cb = oneShotBegin();
    presStampStart(cb, true);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = up ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = up ? presImg_ : colour_;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
    vkCmdCopyBufferToImage(cb, upBuf_, up ? presImg_ : colour_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    if (!up) { srcW_ = w_; srcH_ = h_; }   // only the top-left holds the uploaded picture
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    presStamp(cb, 1);
    oneShotSubmit(cb);   // the present's fence is the wait
    return up ? presentImage(presImg_, w_, h_) : presentDirect();
}

// ---- THE CLOUD UNDER THE INTERFACE (`under.frag`) ------------------------------

void VulkanRenderer::killUnder() {
    if (undPipe_) { vkDestroyPipeline(dev_, undPipe_, nullptr); undPipe_ = VK_NULL_HANDLE; }
    if (undPlo_) { vkDestroyPipelineLayout(dev_, undPlo_, nullptr); undPlo_ = VK_NULL_HANDLE; }
    if (undPool_) { vkDestroyDescriptorPool(dev_, undPool_, nullptr); undPool_ = VK_NULL_HANDLE; }
    undDs_ = VK_NULL_HANDLE;
    if (undDsl_) { vkDestroyDescriptorSetLayout(dev_, undDsl_, nullptr); undDsl_ = VK_NULL_HANDLE; }
    if (undSampler_) { vkDestroySampler(dev_, undSampler_, nullptr); undSampler_ = VK_NULL_HANDLE; }
    const auto img = [&](VkImageView& v, VkImage& i, VkDeviceMemory& m) {
        if (v) vkDestroyImageView(dev_, v, nullptr);
        if (i) vkDestroyImage(dev_, i, nullptr);
        if (m) freeDeviceMemory(m);
        v = VK_NULL_HANDLE; i = VK_NULL_HANDLE; m = VK_NULL_HANDLE;
    };
    img(undUiView_, undUi_, undUiMem_);
    img(undView_, undImg_, undImgMem_);
    img(undMaskView_, undMask_, undMaskMem_);
    if (undBuf_) vkDestroyBuffer(dev_, undBuf_, nullptr);
    if (undBufMem_) freeDeviceMemory(undBufMem_);
    undBuf_ = VK_NULL_HANDLE; undBufMem_ = VK_NULL_HANDLE;
    undW_ = undH_ = 0;
}

bool VulkanRenderer::makeUnder(int uw, int uh) {
    if (undPipe_ != VK_NULL_HANDLE && undW_ == uw && undH_ == uh) return true;
    killUnder();
    if (!makePresentPass()) return false;
    const auto image = [&](int w, int h, VkFormat fmt, const char* what, VkImage& i, VkDeviceMemory& m,
                           VkImageView& v) {
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt;
        ii.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return makeImage2D(ii, VK_IMAGE_ASPECT_COLOR_BIT, what, i, m, &v);
    };
    // the frame as its own 565 words and the planes' M as bytes - both
    // copied as they are, the expansion the shader's
    if (!image(w_, h_, VK_FORMAT_R16_UINT, "under: interface", undUi_, undUiMem_, undUiView_) ||
        !image(w_, h_, VK_FORMAT_R8_UNORM, "under: mask", undMask_, undMaskMem_, undMaskView_) ||
        !image(uw, uh, VK_FORMAT_R16_UINT, "under: cloud", undImg_, undImgMem_, undView_) ||
        !makeBuffer(static_cast<VkDeviceSize>(uw) * uh * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    undBuf_, undBufMem_)) {
        killUnder();
        return false;
    }
    {
        // texelFetch only: the sampler's filter is never consulted
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = VK_FILTER_NEAREST; si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VKCHECK(vkCreateSampler(dev_, &si, nullptr, &undSampler_), "under sampler");
        VkDescriptorSetLayoutBinding b[3]{};
        for (uint32_t k = 0; k < 3; ++k) {
            b[k].binding = k; b[k].descriptorCount = 1;
            b[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b[k].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dli.bindingCount = 3; dli.pBindings = b;
        VKCHECK(vkCreateDescriptorSetLayout(dev_, &dli, nullptr, &undDsl_), "under set layout");
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 1; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
        VKCHECK(vkCreateDescriptorPool(dev_, &pi, nullptr, &undPool_), "under pool");
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = undPool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &undDsl_;
        VKCHECK(vkAllocateDescriptorSets(dev_, &ai, &undDs_), "under set");
        VkDescriptorImageInfo info[3] = {
            {undSampler_, undUiView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {undSampler_, undView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {undSampler_, undMaskView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet wr[3]{};
        for (uint32_t k = 0; k < 3; ++k) {
            wr[k] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            wr[k].dstSet = undDs_; wr[k].dstBinding = k; wr[k].descriptorCount = 1;
            wr[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr[k].pImageInfo = &info[k];
        }
        vkUpdateDescriptorSets(dev_, 3, wr, 0, nullptr);
    }
    {
        VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UnderPush)};
        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pli.setLayoutCount = 1; pli.pSetLayouts = &undDsl_;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
        VKCHECK(vkCreatePipelineLayout(dev_, &pli, nullptr, &undPlo_), "under pipeline layout");
        const auto module = [&](const uint32_t* code, size_t bytes) {
            VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            si.codeSize = bytes; si.pCode = code;
            VkShaderModule m = VK_NULL_HANDLE;
            vkCreateShaderModule(dev_, &si, nullptr, &m);
            return m;
        };
        VkShaderModule qv = module(kFsqVert, sizeof(kFsqVert));
        VkShaderModule qf = module(kUnderFrag, sizeof(kUnderFrag));
        if (!qv || !qf) {
            if (qv) vkDestroyShaderModule(dev_, qv, nullptr);
            if (qf) vkDestroyShaderModule(dev_, qf, nullptr);
            std::fprintf(stderr, "vulkan: under shader module\n");
            killUnder();
            return false;
        }
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, qv, "main", nullptr};
        st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, qf, "main", nullptr};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp{0, 0, static_cast<float>(w_), static_cast<float>(h_), 0.0f, 1.0f};
        VkRect2D sc{{0, 0}, {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_)}};
        VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cba{};
        cba.blendEnable = VK_FALSE; cba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1; cb.pAttachments = &cba;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.stageCount = 2; gp.pStages = st;
        gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vps; gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms; gp.pColorBlendState = &cb;
        gp.layout = undPlo_; gp.renderPass = presPass_;
        const VkResult pr = vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &undPipe_);
        vkDestroyShaderModule(dev_, qv, nullptr);
        vkDestroyShaderModule(dev_, qf, nullptr);
        if (pr != VK_SUCCESS) { std::fprintf(stderr, "vulkan: under pipeline\n"); killUnder(); return false; }
    }
    undW_ = uw; undH_ = uh;
    return true;
}

bool VulkanRenderer::presentUnder(const omk::Surface& s, const unsigned char* mask, const unsigned char* maskRow,
                                  const omk::Surface& under, int filter, std::vector<unsigned char>* readback) {
    if (swap_ == VK_NULL_HANDLE || s.w != w_ || s.h != h_ || !under.valid()) return false;
    settle();   // `upBuf_` and `undBuf_` are written below
    if (!makeUnder(under.w, under.h)) return false;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w_) * h_ * 4;
    if (upBuf_ == VK_NULL_HANDLE &&
        !makeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    upBuf_, upMem_)) return false;
    // THE INTERFACE: the 565 words as they are, then the planes' M - both
    // straight copies (a 565 -> RGBA8 expansion per pixel was a third of a
    // core at 3024x1964); `under.frag` expands, and reads the key itself
    const VkDeviceSize uiBytes = static_cast<VkDeviceSize>(w_) * h_ * 2;
    const VkDeviceSize maskBytes = static_cast<VkDeviceSize>(w_) * h_;
    {
        void* p = nullptr;
        vkMapMemory(dev_, upMem_, 0, uiBytes + maskBytes, 0, &p);
        auto* dst = static_cast<unsigned char*>(p);
        std::memcpy(dst, s.px.data(), static_cast<std::size_t>(uiBytes));
        if (mask) std::memcpy(dst + uiBytes, mask, static_cast<std::size_t>(maskBytes));
        else std::memset(dst + uiBytes, 0, static_cast<std::size_t>(maskBytes));
        (void)maskRow;   // M is 0 outside the rows the planes touched, `resolveOverlayPlanes`'s
        vkUnmapMemory(dev_, upMem_);
    }
    // THE CLOUD, 640x480, its 565 words too: the shader widens them as the
    // BLIT does, which is not the present's table (blue `>> 2` against `>> 3`)
    {
        void* p = nullptr;
        const VkDeviceSize ub = static_cast<VkDeviceSize>(under.w) * under.h * 2;
        vkMapMemory(dev_, undBufMem_, 0, ub, 0, &p);
        std::memcpy(p, under.px.data(), static_cast<std::size_t>(ub));
        vkUnmapMemory(dev_, undBufMem_);
    }
    VkCommandBuffer cb = oneShotBegin();
    const auto layout = [&](VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags srcA,
                            VkAccessFlags dstA, VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = from; b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = srcA; b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(cb, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };
    const auto upload = [&](VkBuffer buf, VkDeviceSize at, VkImage img, int w, int h) {
        // the last frame's pass read it; every texel is rewritten
        layout(img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy cp{};
        cp.bufferOffset = at;
        cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        cp.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vkCmdCopyBufferToImage(cb, buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
        layout(img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    };
    upload(upBuf_, 0, undUi_, w_, h_);
    upload(upBuf_, uiBytes, undMask_, w_, h_);
    upload(undBuf_, 0, undImg_, under.w, under.h);
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = presPass_; rp.framebuffer = presFbuf_;
    rp.renderArea = {{0, 0}, {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_)}};
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, undPipe_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, undPlo_, 0, 1, &undDs_, 0, nullptr);
    const UnderPush up{w_, h_, under.w, under.h, filter >= 1 ? 1 : 0};
    vkCmdPushConstants(cb, undPlo_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof up, &up);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRenderPass(cb);   // `presImg_` left TRANSFER_SRC by the pass
    // the verification's copy of what is presented (OMK_VERIFY_GPU_CLOUD)
    if (readback && (presRead_ != VK_NULL_HANDLE ||
                     makeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                presRead_, presReadMem_))) {
        VkBufferImageCopy cp{};
        cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        cp.imageExtent = {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
        vkCmdCopyImageToBuffer(cb, presImg_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, presRead_, 1, &cp);
    }
    if (readback) oneShotEnd(cb);    // the copy is read just below
    else oneShotSubmit(cb);          // the present's fence is the wait
    if (readback && presRead_ != VK_NULL_HANDLE) {
        readback->resize(static_cast<std::size_t>(bytes));
        void* p = nullptr;
        if (vkMapMemory(dev_, presReadMem_, 0, bytes, 0, &p) == VK_SUCCESS) {
            std::memcpy(readback->data(), p, static_cast<std::size_t>(bytes));
            vkUnmapMemory(dev_, presReadMem_);
        } else {
            readback->clear();
        }
    }
    return presentImage(presImg_, w_, h_);
}

// ---- THE PRESENT PASS (todo/optimization.md step 4b) --------------------------

bool VulkanRenderer::makePresentPass() {
    if (presPipe_ != VK_NULL_HANDLE) return true;
    const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
    {
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt;
        ii.extent = {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
        ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        // TRANSFER_DST: `presentSurface` uploads into it when the upscale
        // leaves `colour_` smaller than the output
        ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (!makeImage2D(ii, VK_IMAGE_ASPECT_COLOR_BIT, "present image", presImg_, presMem_, &presView_))
            return false;
    }
    {
        // every pixel is written, so nothing is loaded; it ends where the
        // swapchain blit and the verification copy both read from
        VkAttachmentDescription a{};
        a.format = fmt; a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
        // in: after the last frame's blit or copy read it and `presentSurface`
        // wrote it; out: before the next blit or copy reads it
        VkSubpassDependency dep[2]{};
        dep[0].srcSubpass = VK_SUBPASS_EXTERNAL; dep[0].dstSubpass = 0;
        dep[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep[1].srcSubpass = 0; dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dep[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpi.attachmentCount = 1; rpi.pAttachments = &a;
        rpi.subpassCount = 1; rpi.pSubpasses = &sub;
        rpi.dependencyCount = 2; rpi.pDependencies = dep;
        VKCHECK(vkCreateRenderPass(dev_, &rpi, nullptr, &presPass_), "present render pass");
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = presPass_; fi.attachmentCount = 1; fi.pAttachments = &presView_;
        fi.width = static_cast<uint32_t>(w_); fi.height = static_cast<uint32_t>(h_); fi.layers = 1;
        VKCHECK(vkCreateFramebuffer(dev_, &fi, nullptr, &presFbuf_), "present framebuffer");
    }
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0; b.descriptorCount = 1;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dli.bindingCount = 1; dli.pBindings = &b;
        VKCHECK(vkCreateDescriptorSetLayout(dev_, &dli, nullptr, &presDsl_), "present set layout");
        // its OWN pool: `setTextures` resets the scene's, and this set must
        // survive a texture reload
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 1; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
        VKCHECK(vkCreateDescriptorPool(dev_, &pi, nullptr, &presPool_), "present pool");
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = presPool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &presDsl_;
        VKCHECK(vkAllocateDescriptorSets(dev_, &ai, &presDs_), "present set");
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = VK_FILTER_NEAREST; si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VKCHECK(vkCreateSampler(dev_, &si, nullptr, &presSampler_), "present sampler");
        VkDescriptorImageInfo ii{presSampler_, colourView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wr.dstSet = presDs_; wr.dstBinding = 0; wr.descriptorCount = 1;
        wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr.pImageInfo = &ii;
        vkUpdateDescriptorSets(dev_, 1, &wr, 0, nullptr);
    }
    {
        VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PresentPush)};
        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pli.setLayoutCount = 1; pli.pSetLayouts = &presDsl_;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
        VKCHECK(vkCreatePipelineLayout(dev_, &pli, nullptr, &presPlo_), "present pipeline layout");
        const auto module = [&](const uint32_t* code, size_t bytes) {
            VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            si.codeSize = bytes; si.pCode = code;
            VkShaderModule m = VK_NULL_HANDLE;
            vkCreateShaderModule(dev_, &si, nullptr, &m);
            return m;
        };
        VkShaderModule qv = module(kFsqVert, sizeof(kFsqVert));
        VkShaderModule qf = module(kPresentFrag, sizeof(kPresentFrag));
        if (!qv || !qf) {
            // (the one that was made, not leaked)
            if (qv) vkDestroyShaderModule(dev_, qv, nullptr);
            if (qf) vkDestroyShaderModule(dev_, qf, nullptr);
            std::fprintf(stderr, "vulkan: present shader module\n");
            return false;
        }
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, qv, "main", nullptr};
        st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, qf, "main", nullptr};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp{0, 0, static_cast<float>(w_), static_cast<float>(h_), 0.0f, 1.0f};
        VkRect2D sc{{0, 0}, {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_)}};
        VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cba{};
        cba.blendEnable = VK_FALSE; cba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1; cb.pAttachments = &cba;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.stageCount = 2; gp.pStages = st;
        gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vps; gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms; gp.pColorBlendState = &cb;
        gp.layout = presPlo_; gp.renderPass = presPass_;
        const VkResult pr = vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &presPipe_);
        vkDestroyShaderModule(dev_, qv, nullptr);
        vkDestroyShaderModule(dev_, qf, nullptr);
        if (pr != VK_SUCCESS) { std::fprintf(stderr, "vulkan: present pipeline\n"); return false; }
    }
    return true;   // `presRead_` is `worldPicture`'s, made on its first call
}

// `colour_` (TRANSFER_SRC, as `end()` and `presentSurface` leave it) through the
// present shader into `presImg_`, and back to TRANSFER_SRC.
bool VulkanRenderer::runPresentPass(int vy, int vh, bool raw) {
    if (!makePresentPass()) return false;
    const bool up = up_ > 1.0f;
    if (up && !makeUpscale()) return false;
    vh = std::clamp(vh, 1, h_);
    VkCommandBuffer cb = oneShotBegin();
    presStampStart(cb, false);
    // Real scopes both ways (the audit's item 8): the top of the pipe and no
    // access ordered nothing, and only the host's wait on the frame's fence
    // kept it right
    const auto layout = [&](VkImageLayout from, VkImageLayout to, VkAccessFlags srcA, VkAccessFlags dstA,
                            VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = from; b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = colour_;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = srcA; b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(cb, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };
    // its last writer: the scene's or the post's pass, or `presentSurface`'s copy
    layout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    if (up) {
        // EASU: the drawn picture, `rs(w_) x rs(vh)` of `colour_`'s top-left,
        // up to the picture's `w_ x vh` in `easuImg_`'s
        VkRenderPassBeginInfo ep{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        ep.renderPass = easuPass_; ep.framebuffer = easuFb_;
        ep.renderArea = {{0, 0}, {static_cast<uint32_t>(w_), static_cast<uint32_t>(vh)}};
        vkCmdBeginRenderPass(cb, &ep, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, easuPipe_);
        const VkViewport vp{0, 0, static_cast<float>(w_), static_cast<float>(vh), 0.0f, 1.0f};
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &ep.renderArea);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, easuPlo_, 0, 1, &easuDs_, 0, nullptr);
        std::uint32_t con[16];
        omk::fsrEasuCon(con, static_cast<float>(rs(w_)), static_cast<float>(rs(vh)), static_cast<float>(rw_),
                        static_cast<float>(rh_), static_cast<float>(w_), static_cast<float>(vh));
        vkCmdPushConstants(cb, easuPlo_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof con, con);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRenderPass(cb);   // `easuImg_` left SHADER_READ_ONLY for the pass below
        presStamp(cb, 4);
    }
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = presPass_; rp.framebuffer = presFbuf_;
    rp.renderArea = {{0, 0}, {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_)}};
    vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, presPipe_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, presPlo_, 0, 1, up ? &presUpDs_ : &presDs_,
                            0, nullptr);
    const PresentPush pp{vy, vh, w_, dither_ ? 1 : 0, up ? 1 : ss_, up ? 1 : 0,
                         up ? omk::fsrRcasCon(omk::kFsrRcasStops) : 0u, raw ? 1 : 0};
    vkCmdPushConstants(cb, presPlo_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pp, &pp);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRenderPass(cb);
    // after the EASU's or this pass's reads, before the readback's copy or
    // `presentDirect`'s blit
    layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
           0, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
           VK_PIPELINE_STAGE_TRANSFER_BIT);
    presStamp(cb, 1);
    oneShotSubmit(cb);   // ordered by the queue; the present's fence (or a readback's wait) is the wait
    return true;
}

bool VulkanRenderer::presentWorld(int vy, int vh) {
    if (swap_ == VK_NULL_HANDLE) return false;
    if (!runPresentPass(vy, vh)) return false;
    return presentImage(presImg_, w_, h_);
}

bool VulkanRenderer::worldPicture(int vy, int vh, std::vector<unsigned char>& rgba) {
    if (!runPresentPass(vy, vh)) return false;
    if (presRead_ == VK_NULL_HANDLE &&
        !makeBuffer(static_cast<VkDeviceSize>(w_) * h_ * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    presRead_, presReadMem_))
        return false;
    VkCommandBuffer cb = oneShotBegin();
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
    vkCmdCopyImageToBuffer(cb, presImg_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, presRead_, 1, &cp);
    oneShotEnd(cb);
    const std::size_t bytes = static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_) * 4;
    rgba.resize(bytes);
    void* p = nullptr;
    if (vkMapMemory(dev_, presReadMem_, 0, bytes, 0, &p) != VK_SUCCESS) return false;
    std::memcpy(rgba.data(), p, bytes);
    vkUnmapMemory(dev_, presReadMem_);
    return true;
}

// Load `colour_` with a `w_ x h_` RGBA8 picture - the probe's way of asking the
// pass about colours no scene is guaranteed to contain.
bool VulkanRenderer::probeUpload(const unsigned char* rgba) {
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w_) * h_ * 4;
    if (upBuf_ == VK_NULL_HANDLE &&
        !makeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    upBuf_, upMem_)) return false;
    void* p = nullptr;
    if (vkMapMemory(dev_, upMem_, 0, bytes, 0, &p) != VK_SUCCESS) return false;
    std::memcpy(p, rgba, static_cast<std::size_t>(bytes));
    vkUnmapMemory(dev_, upMem_);
    VkCommandBuffer cb = oneShotBegin();
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = colour_;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
    vkCmdCopyBufferToImage(cb, upBuf_, colour_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    oneShotEnd(cb);
    return true;
}

bool VulkanRenderer::init(int w, int h) {
    w_ = w; h_ = h;
    if (up_ > 1.0f && ss_ > 1) {
        std::printf("vulkan: the upscale takes the supersample's place (--ssaa %d ignored)\n", ss_);
        ss_ = 1;
    }
    rw_ = rs(w_); rh_ = rs(h_);
    if (up_ > 1.0f) {
        std::printf("vulkan: the frame drawn at %dx%d and upscaled to %dx%d by FSR 1 (x%.2f) - "
                    "a REMASTER feature\n", rw_, rh_, w_, h_, static_cast<double>(up_));
        omk::event("vulkan.upscale", {{"factor", static_cast<double>(up_)}, {"drawW", static_cast<long>(rw_)},
                                      {"drawH", static_cast<long>(rh_)}, {"outW", static_cast<long>(w_)},
                                      {"outH", static_cast<long>(h_)}});
    }
    srcW_ = rw_; srcH_ = rh_;
    if (!pickDevice()) return false;
    if (!makeTarget()) return false;
    if (!makePipelines()) return false;
    // The window is 640x480 while the 3D target is the letterboxed 640x352.
    if (surface_ != VK_NULL_HANDLE && !makeSwapchain(w, 480)) return false;
    return true;
}

void VulkanRenderer::setPreloadTextures(std::span<const omk::Texture> t) {
    const auto keyOf = [](const omk::Texture& x) {
        return std::make_tuple(x.idx.data(), x.pal.data(), x.width, x.height);
    };
    for (auto& [key, u] : uploaded_) u.pinned = false;
    preQueue_.clear();
    for (const auto& x : t) {
        if (!x.hasPixels()) continue;
        if (auto hit = uploaded_.find(keyOf(x)); hit != uploaded_.end()) hit->second.pinned = true;
        else preQueue_.push_back(x);   // shares its pixel buffers: the key stays
    }
}

int VulkanRenderer::preloadStep(int budget) {
    if (preQueue_.empty() || budget <= 0) return static_cast<int>(preQueue_.size());
    const std::size_t n = std::min(preQueue_.size(), static_cast<std::size_t>(budget));
    std::vector<omk::Texture> now(preQueue_.end() - static_cast<std::ptrdiff_t>(n), preQueue_.end());
    preQueue_.resize(preQueue_.size() - n);
    poolTextures(now, true);
    return static_cast<int>(preQueue_.size());
}

void VulkanRenderer::poolTextures(std::span<const omk::Texture> t, bool preload) {
    // One VkImage and one descriptor set per material, plus a white 1x1 for a
    // material with no texture - which `raster.cpp` handles by leaving the
    // texel at 255 and letting the vertex colour stand alone.
    // `fill` writes tw*th RGBA texels, alpha the colour key
    // `withSet` false: the image alone - a light map, a normal map, a 1x1
    // stand-in, bound into ANOTHER texture's set (each had one allocated,
    // written and freed at once)
    // `one`: ONE channel (R8), `fill` writing a byte a texel - a light map,
    // which the shader reads `.r` of; a quarter of its memory as RGBA (each
    // 1024x1024 map 4 MB -> 1 MB). Only where the device blits and filters
    // R8 (`r8Ok_`), else RGBA as before.
    // an uploaded image's view, and its descriptor set unless `withSet` false
    auto viewAndSet = [&](Tex& out, VkFormat fmt, uint32_t levels, bool withSet) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = out.img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = fmt;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
        if (vkCreateImageView(dev_, &vi, nullptr, &out.view) != VK_SUCCESS) {
            // no view, no texture: it draws white (and a map stands aside)
            out.view = VK_NULL_HANDLE;
            freeDeviceMemory(out.mem);
            vkDestroyImage(dev_, out.img, nullptr);
            out.img = VK_NULL_HANDLE; out.mem = VK_NULL_HANDLE;
            return;
        }
        if (!withSet) return;

        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = dpool_; dai.descriptorSetCount = 1; dai.pSetLayouts = &dsl_;
        if (vkAllocateDescriptorSets(dev_, &dai, &out.ds) != VK_SUCCESS) {
            // said, never written into: the draw then takes the white set
            out.ds = VK_NULL_HANDLE;
            static bool told = false;
            if (!told) { told = true; std::fprintf(stderr, "vulkan: the texture descriptor pool is exhausted\n"); }
            return;
        }
        VkDescriptorImageInfo dii{sampler_, out.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wr.dstSet = out.ds; wr.dstBinding = 0; wr.descriptorCount = 1;
        wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr.pImageInfo = &dii;
        vkUpdateDescriptorSets(dev_, 1, &wr, 0, nullptr);
    };
    // `what`: the image's category - "textures", or the 1x1 stand-ins every
    // missing texture or map falls back to
    auto upload = [&](auto&& fill, int tw, int th, Tex& out, bool withSet = true, bool one = false,
                      const char* what = "textures") {
        const VkFormat fmt = one ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(tw) * th * (one ? 1 : 4);
        VkBuffer sb; VkDeviceSize so = 0;
        unsigned char* dst = nullptr;
        if (!stage(bytes, sb, so, dst)) return;
        // Alpha carries the COLOUR KEY: 0 where the texel is black, the key
        // the cutout path (flag 0x800) discards on, 255 elsewhere. With the
        // nearest sampler the shader's test on it is the same test on the
        // same texel as before; with a linear one the (0,0,0,0) key texels
        // make the sample PREMULTIPLIED, which is what lets the edge stay
        // clean. A non-cutout batch never reads alpha, so a black texel
        // still draws black.
        fill(dst);

        // THE MIP CHAIN - the trilinear enhancement's, generated here by a
        // ladder of half-size linear blits, because the .3DT ships ONE level
        // (MIPFILTER NONE in the original). Averaging RGBA with the key
        // texels at (0,0,0,0) keeps every level premultiplied, so the
        // shader's cutout rule holds down the chain; a non-cutout batch's
        // black texels average as black, which is what they are.
        uint32_t levels = 1;
        if (filter_ >= 2)
            for (int m = std::max(tw, th); m > 1; m >>= 1) ++levels;
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt;
        ii.extent = {static_cast<uint32_t>(tw), static_cast<uint32_t>(th), 1};
        ii.mipLevels = levels; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                 | (levels > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (!makeImage2D(ii, VK_IMAGE_ASPECT_COLOR_BIT, what, out.img, out.mem, nullptr))
            return;   // no image: the texture draws white (its staging left to the flush)

        VkCommandBuffer cb = uploadCb();
        VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.srcQueueFamilyIndex = bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = out.img;
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
        bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
        VkBufferImageCopy cp{};
        cp.bufferOffset = so;
        cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        cp.imageExtent = {static_cast<uint32_t>(tw), static_cast<uint32_t>(th), 1};
        vkCmdCopyBufferToImage(cb, sb, out.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
        // The ladder: each level becomes a blit SOURCE once written, and the
        // next is blitted from it at half size.
        int pw = tw, ph = th;
        for (uint32_t l = 1; l < levels; ++l) {
            bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, l - 1, 1, 0, 1};
            bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
            const int nw = std::max(pw / 2, 1), nh = std::max(ph / 2, 1);
            VkImageBlit bl{};
            bl.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l - 1, 0, 1};
            bl.srcOffsets[1] = {pw, ph, 1};
            bl.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1};
            bl.dstOffsets[1] = {nw, nh, 1};
            vkCmdBlitImage(cb, out.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           out.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
            pw = nw; ph = nh;
        }
        // Level 0..levels-2 are now TRANSFER_SRC, the last TRANSFER_DST; the
        // last level is moved to SRC too so ONE barrier can hand the whole
        // image to the shader. With one level there is no ladder: level 0 goes
        // from DST to the shader - the image has no TRANSFER_SRC usage then
        // (the validation layer's VUID-VkImageMemoryBarrier-oldLayout-01212,
        // found 2026-10-24).
        if (levels > 1) {
            bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, levels - 1, 1, 0, 1};
            bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
        }
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
        bar.oldLayout = levels > 1 ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.srcAccessMask = levels > 1 ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
        // (submitted with the pool's other copies - `flushUploads`)
        viewAndSet(out, fmt, levels, withSet);
    };
    // ...and a texture of the pack's BLOCK FORMATS (`o3de/texpack.h`
    // PackBlocks): the file's levels copied as they are - no blit ladder,
    // a block image cannot be blitted, and the KTX2 carries its own chain
    // made the ladder's way (`tools/tex_encode.cpp`). Only its level 0 when
    // the sampler takes no mips, as the RGBA path makes none.
    auto uploadZ = [&](const omk::PackBlocks& z, Tex& out, bool withSet = true) {
        const VkFormat fmt = static_cast<VkFormat>(z.vkFormat);
        const uint32_t levels = filter_ >= 2 ? static_cast<uint32_t>(z.levels.size()) : 1u;
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(z.bytes.size());
        VkBuffer sb; VkDeviceSize so = 0;
        unsigned char* dst = nullptr;
        if (!stage(bytes, sb, so, dst)) return;
        std::memcpy(dst, z.bytes.data(), z.bytes.size());
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt;
        ii.extent = {static_cast<uint32_t>(z.w), static_cast<uint32_t>(z.h), 1};
        ii.mipLevels = levels; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (!makeImage2D(ii, VK_IMAGE_ASPECT_COLOR_BIT, "textures", out.img, out.mem, nullptr))
            return;   // no image: the texture draws white (its staging left to the flush)
        VkCommandBuffer cb = uploadCb();
        VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.srcQueueFamilyIndex = bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = out.img;
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
        bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
        std::vector<VkBufferImageCopy> cps(levels);
        for (uint32_t l = 0; l < levels; ++l) {
            cps[l] = VkBufferImageCopy{};
            cps[l].bufferOffset = so + z.levels[l].first;
            cps[l].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1};
            cps[l].imageExtent = {std::max(1u, static_cast<uint32_t>(z.w) >> l),
                                  std::max(1u, static_cast<uint32_t>(z.h) >> l), 1};
        }
        vkCmdCopyBufferToImage(cb, sb, out.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels, cps.data());
        bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
        viewAndSet(out, fmt, levels, withSet);
    };

    // THE POOL, UPLOADED ONLY WHERE IT IS NEW, as the GLES, 3DS and GL1
    // backends keep it. The frame hands the pool over again whenever it
    // changes - a particle's sprite, a character coming into view - and
    // re-sending would mean waiting for the device to go idle, destroying
    // EVERY image and sending them all again. A texture already on the device from an earlier pool -
    // the same pixel storage, palette and size - is kept with its image and
    // its descriptor set, and the entry holds that storage, so its address
    // cannot come back as another texture's while cached; what no slot of the
    // new pool uses is destroyed, after a wait for the device only then.
    // A set is freed on its own and only the new ones are allocated -
    // reallocating them all on a per-frame call exhausts the descriptor pool
    // and crashes.
    // binding 1 of a texture's set: its light map, or the shared black one;
    // binding 3 its normal map, or the shared flat one
    const auto bindLight = [&](VkDescriptorSet ds, VkImageView view, VkImageView normal) {
        VkDescriptorImageInfo dii[2] = {{sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                        {sampler_, normal, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet wr[2]{};
        for (int k = 0; k < 2; ++k) {
            wr[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wr[k].dstSet = ds; wr[k].dstBinding = k == 0 ? 1 : 3; wr[k].descriptorCount = 1;
            wr[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wr[k].pImageInfo = &dii[k];
        }
        vkUpdateDescriptorSets(dev_, 2, wr, 0, nullptr);
        bindReflection(ds);
    };
    if (normalFlat_.img == VK_NULL_HANDLE) {
        upload([](unsigned char* d) { d[0] = 128; d[1] = 128; d[2] = 255; d[3] = 0; }, 1, 1, normalFlat_, false,
               false, "texture stand-ins");
    }
    if (lightBlack_.img == VK_NULL_HANDLE) {
        upload([](unsigned char* d) { d[0] = d[1] = d[2] = d[3] = 0; }, 1, 1, lightBlack_, false, false,
               "texture stand-ins");
        // an upload's set is written with binding 0 alone: the black one's
        // view stands in for the light map and the reflection until bound
    }
    if (white_.img == VK_NULL_HANDLE) {
        upload([](unsigned char* d) { d[0] = d[1] = d[2] = d[3] = 255; }, 1, 1, white_, true, false,
               "texture stand-ins");
        // (the stand-ins refused - 4 bytes each, so a device already out of
        // memory: no set to write into, and a draw with none is skipped)
        if (white_.ds && lightBlack_.view && normalFlat_.view)
            bindLight(white_.ds, lightBlack_.view, normalFlat_.view);
    }
    const auto keyOf = [](const omk::Texture& x) {
        return std::make_tuple(x.idx.data(), x.pal.data(), x.width, x.height);
    };
    if (!preload) {
    for (auto& [key, u] : uploaded_) u.used = false;
    for (const auto& x : t)
        if (x.hasPixels())
            if (auto hit = uploaded_.find(keyOf(x)); hit != uploaded_.end()) hit->second.used = true;
    }
    bool waited = false;
    for (auto it = uploaded_.begin(); !preload && it != uploaded_.end(); ) {
        if (it->second.used || it->second.pinned) { ++it; continue; }
        if (!waited) { vkDeviceWaitIdle(dev_); settleIdle(); waited = true; }
        Tex& old = it->second.t;
        if (old.ds)   vkFreeDescriptorSets(dev_, dpool_, 1, &old.ds);
        if (old.view) vkDestroyImageView(dev_, old.view, nullptr);
        if (old.img)  vkDestroyImage(dev_, old.img, nullptr);
        if (old.mem)  freeDeviceMemory(old.mem);
        if (old.emView) vkDestroyImageView(dev_, old.emView, nullptr);
        if (old.emImg)  vkDestroyImage(dev_, old.emImg, nullptr);
        if (old.emMem)  freeDeviceMemory(old.emMem);
        if (old.nmView) vkDestroyImageView(dev_, old.nmView, nullptr);
        if (old.nmImg)  vkDestroyImage(dev_, old.nmImg, nullptr);
        if (old.nmMem)  freeDeviceMemory(old.nmMem);
        it = uploaded_.erase(it);
    }
    if (!preload) tex_.assign(t.size(), Tex{});
    // THE TEXTURE PACK (`o3de/texpack.h`), consulted only when one was opened:
    // a texture it holds is uploaded from the pack's pixels at the pack's size
    const omk::TexturePack& pack = omk::texturePack();
    std::vector<omk::PackImage> reps;
    int lit = 0;
    // what a pool change COSTS the frame it lands on - the pack's decode and
    // every upload - said when it brings anything new (the preload's measure)
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    int fresh = 0, blocksUp = 0;
    long prepared = 0;
    if (pack.shown()) {
        // every texture this pool brings that the device does not hold yet,
        // decoded together on the thread pool
        std::vector<const omk::Texture*> toDecode(t.size(), nullptr);
        for (std::size_t i = 0; i < t.size(); ++i)
            if (t[i].hasPixels() && !uploaded_.count(keyOf(t[i]))) toDecode[i] = &t[i];
        const std::size_t taken0 = pack.parkedTaken();
        reps = pack.replacements(toDecode);
        prepared = static_cast<long>(pack.parkedTaken() - taken0);   // decoded ahead, on a load's thread
    }
    const auto t1 = clk::now();
    int packed = 0, normals = 0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const omk::Texture& x = t[i];
        if (!x.hasPixels()) continue;
        auto hit = uploaded_.find(keyOf(x));
        if (hit == uploaded_.end()) {
            Uploaded u;
            u.idx = x.idx; u.pal = x.pal; u.used = !preload; u.pinned = preload;
            if (pack.shown() && reps[i].ok && !reps[i].colourZ.empty()) {
                const omk::PackImage& rep = reps[i];
                uploadZ(rep.colourZ, u.t);
                u.t.su = static_cast<float>(rep.w) / static_cast<float>(x.width);
                u.t.sv = static_cast<float>(rep.h) / static_cast<float>(x.height);
                u.t.packed = true;
                ++blocksUp;
            } else if (pack.shown() && reps[i].ok) {
                const omk::PackImage& rep = reps[i];
                const int rw = rep.w, rh = rep.h;
                upload([&](unsigned char* d) { std::memcpy(d, rep.rgba.data(), rep.rgba.size()); }, rw, rh, u.t);
                u.t.su = static_cast<float>(rw) / static_cast<float>(x.width);
                u.t.sv = static_cast<float>(rh) / static_cast<float>(x.height);
                u.t.packed = true;
            } else {
                upload([&](unsigned char* d) { x.toRgbaKeyed(d); }, x.width, x.height, u.t);
            }
            // ...and its LIGHT MAP, when the pack has one: grey in RGBA, the
            // red channel what `scene.frag` reads
            if (pack.shown() && !reps[i].emissiveZ.empty()) {
                Tex em;
                uploadZ(reps[i].emissiveZ, em, false);
                u.t.emImg = em.img; u.t.emMem = em.mem; u.t.emView = em.view;
                ++lit;
            } else if (pack.shown() && !reps[i].emissive.empty()) {
                const omk::PackImage& rep = reps[i];
                Tex em;
                if (r8Ok_)
                    upload([&](unsigned char* d) { std::memcpy(d, rep.emissive.data(), rep.emissive.size()); },
                           rep.ew, rep.eh, em, false, true);
                else
                    upload([&](unsigned char* d) {
                        for (std::size_t q = 0; q < rep.emissive.size(); ++q)
                            d[4 * q] = d[4 * q + 1] = d[4 * q + 2] = rep.emissive[q], d[4 * q + 3] = 255;
                    }, rep.ew, rep.eh, em, false);
                u.t.emImg = em.img; u.t.emMem = em.mem; u.t.emView = em.view;
                ++lit;
            }
            // ...and its NORMAL MAP (the relief's), when the pack has one
            if (pack.shown() && !reps[i].normalZ.empty()) {
                Tex nm;
                uploadZ(reps[i].normalZ, nm, false);
                u.t.nmImg = nm.img; u.t.nmMem = nm.mem; u.t.nmView = nm.view;
                ++normals;
            } else if (pack.shown() && !reps[i].normal.empty()) {
                const omk::PackImage& rep = reps[i];
                Tex nm;
                upload([&](unsigned char* d) { std::memcpy(d, rep.normal.data(), rep.normal.size()); }, rep.nw, rep.nh, nm, false);
                u.t.nmImg = nm.img; u.t.nmMem = nm.mem; u.t.nmView = nm.view;
                ++normals;
            }
            if (u.t.ds && lightBlack_.view && normalFlat_.view)
                bindLight(u.t.ds, u.t.emView ? u.t.emView : lightBlack_.view,
                          u.t.nmView ? u.t.nmView : normalFlat_.view);
            hit = uploaded_.emplace(keyOf(x), std::move(u)).first;
            ++fresh;
        } else if (preload) {
            hit->second.pinned = true;
        }
        if (preload) continue;
        tex_[i] = hit->second.t;
        packed += tex_[i].packed ? 1 : 0;
    }
    flushUploads();   // the pool's copies, submitted and waited on once
    poolLit_ = std::any_of(tex_.begin(), tex_.end(), [](const Tex& t) { return t.emView != VK_NULL_HANDLE; });
    const double decodeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double uploadMs = std::chrono::duration<double, std::milli>(clk::now() - t1).count();
    if (preload) {
        if (fresh > 0)
            std::printf("vulkan: preload - %d textures uploaded ahead (%d from block files), read and decode %.1f ms, "
                        "uploads %.1f ms; %zu still to come\n", fresh, blocksUp, decodeMs, uploadMs, preQueue_.size());
        omk::event("texpack.preload", {{"fresh", fresh}, {"blocks", blocksUp}, {"prepared", prepared}, {"decodeMs", decodeMs},
                                       {"uploadMs", uploadMs}, {"waiting", static_cast<long>(preQueue_.size())}});
        return;
    }
    if (fresh > 0)
        std::printf("vulkan: texture pool - %d new of %zu (%d from the pack's block files), the pack's read and "
                    "decode %.1f ms, the uploads %.1f ms\n", fresh, t.size(), blocksUp, decodeMs, uploadMs);
    if (pack.shown())
        omk::event("texpack.pool", {{"backend", "vulkan"}, {"textures", static_cast<long>(t.size())},
                                    {"replaced", packed}, {"lights", lit}, {"normals", normals},
                                    {"lightChannels", r8Ok_ ? 1 : 4}, {"fresh", fresh}, {"blocks", blocksUp},
                                    {"prepared", prepared},
                                    {"decodeMs", decodeMs}, {"uploadMs", uploadMs}});
}

bool VulkanRenderer::uploadGeometry(const omk::Geometry* g) {
    // Cached by pointer, but only while the CONTENT has not moved: a posed
    // character is the same object with different vertices every frame
    // (`Geometry::revision`), and returning the cached buffer for it froze the
    // character at the first pose the GPU ever saw.
    const auto seen = vboRev_.find(g);
    if (vbo_.count(g) && seen != vboRev_.end() && seen->second == g->revision)
        return true;
    bool grow = false;
    if (vbo_.count(g)) {
        // Same object, new vertices. The previous frame has already been
        // waited on (present ends with `vkWaitForFences`), so the buffer is
        // not in flight and can be refilled in place when it still fits.
        const auto it = vboN_.find(g);
        const auto capIt = vboCap_.find(g);
        const std::size_t cap = capIt != vboCap_.end() ? capIt->second : (it != vboN_.end() ? it->second : 0);
        if (it != vboN_.end() && cap >= g->corners.size()) {
            // ...and when the geometry says which corners moved since the
            // revision this buffer holds, only those are written
            // (`Geometry::dirtyCorners`, todo/optimization.md step 8): a moving
            // crate is a few hundred of Anekbah's 139245 corners. The triangles
            // the depth tie degenerated stay degenerated; `resolveTies` puts
            // back the ones that are no longer losers.
            static const bool noDirty = omk::envFlag("OMK_NO_DIRTY");
            const bool partial = !noDirty && seen != vboRev_.end() && g->dirtyTo != 0 &&
                                 g->dirtyTo == g->revision && seen->second == g->dirtyFrom &&
                                 it->second == g->corners.size();
            void* p = nullptr;
            const VkDeviceSize bytes = g->corners.size() * sizeof(GpuVert);
            if (vkMapMemory(dev_, vbo_[g].second, 0, bytes, 0, &p) == VK_SUCCESS) {
                auto* dst = static_cast<GpuVert*>(p);
                const auto* lose = bakedOf(g);
                if (partial) {
                    for (const std::uint32_t i : g->dirtyCorners) {
                        if (i >= g->corners.size()) continue;
                        dst[i] = vertOf(*g, i, lose);
                    }
                } else {
                    for (std::size_t i = 0; i < g->corners.size(); ++i) dst[i] = vertOf(*g, i, lose);
                    tie_[g].vboReplaced();
                }
                vkUnmapMemory(dev_, vbo_[g].second);
                vboRev_[g] = g->revision;
                vboN_[g] = g->corners.size();
                return true;
            }
        }
        vkDestroyBuffer(dev_, vbo_[g].first, nullptr);
        freeDeviceMemory(vbo_[g].second);
        vbo_.erase(g); vboN_.erase(g); vboRev_.erase(g);
        grow = true;                   // it has changed size once: give it room
    }
    std::vector<GpuVert> v(g->corners.size());
    const auto* lose = bakedOf(g);
    for (std::size_t i = 0; i < g->corners.size(); ++i) v[i] = vertOf(*g, i, lose);
    const VkDeviceSize bytes = v.size() * sizeof(GpuVert);
    if (!bytes) return false;
    // a first buffer is exact - a set is never re-sized - and one that has
    // outgrown its buffer gets half as much again
    const std::size_t capCorners = grow ? v.size() + v.size() / 2 : v.size();
    VkBuffer b; VkDeviceMemory m;
    if (!makeBuffer(capCorners * sizeof(GpuVert), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    b, m)) return false;
    void* p = nullptr;
    vkMapMemory(dev_, m, 0, bytes, 0, &p);
    std::memcpy(p, v.data(), bytes);
    vkUnmapMemory(dev_, m);
    vbo_[g] = {b, m};
    g->resident.mark();
    vboN_[g] = v.size();
    vboCap_[g] = capCorners;
    vboRev_[g] = g->revision;
    tie_[g].vboReplaced();
    return true;
}

// The GLES backend's rule, the same `DepthTie` walked once over the set's
// whole draw order (`glesrender.cpp` has the reading): its losers draw a depth
// step back rather than degenerated, so a culled winner or a door leaf moved
// off its frame leaves the loser drawn, as the engine draws it.
bool VulkanRenderer::bakeDepthTie(const omk::Geometry* g, std::span<const omk::Draw> order) {
    if (omk::tieBakeOff() || !g) return false;
    const std::vector<std::size_t> tris = omk::bakeTieLosers(*g, order);
    std::vector<std::uint8_t> lose(g->corners.size(), 0);
    for (const std::size_t tri : tris)
        for (std::size_t k = 3 * tri; k < 3 * tri + 3 && k < lose.size(); ++k) lose[k] = 1;
    baked_[g] = std::move(lose);
    // the buffer holds the old decision - degenerated faces, or none marked:
    // forget both, so the next upload is whole and marked
    tie_.erase(g);
    vboRev_.erase(g);
    static const bool tieLog = omk::envFlag("OMK_TIE_LOG");
    if (tieLog)
        std::printf("[tie] vulkan: baked %zu losers over %zu draws on geometry %p\n",
                    tris.size(), order.size(), static_cast<const void*>(g));
    return true;
}

void VulkanRenderer::resolveTies(const omk::Draw& d) {
    if (omk::tieOff()) return;              // the fight left in, for a before/after
    const omk::Geometry* g = d.geo;
    if (baked_.count(g)) return;             // settled once, drawn back (`bakeDepthTie`)
    auto& t = tie_[g];
    std::vector<std::size_t> losers, restore;
    t.resolve(*g, d.start, d.count, d.blend == omk::Blend::Opaque, losers, restore);
    if (losers.empty() && restore.empty()) return;
    // `OMK_TIE_LOG=1`: every loser, with the mesh it came from
    static const bool tieLog = omk::envFlag("OMK_TIE_LOG");
    if (tieLog)
        for (const std::size_t tri : losers)
            std::printf("[tie] triangle %zu mesh %d at %.1f %.1f %.1f (draw start %zu count %zu)\n",
                        tri, 3 * tri < g->cornerMesh.size() ? g->cornerMesh[3 * tri] : -1,
                        g->corners[3 * tri].x, g->corners[3 * tri].y, g->corners[3 * tri].z,
                        d.start, d.count);
    const auto vb = vbo_.find(g);
    const auto vn = vboN_.find(g);
    if (vb == vbo_.end() || vn == vboN_.end()) return;
    void* p = nullptr;
    const VkDeviceSize bytes = vn->second * sizeof(GpuVert);
    if (vkMapMemory(dev_, vb->second.second, 0, bytes, 0, &p) != VK_SUCCESS) return;
    auto* v = static_cast<GpuVert*>(p);
    // first the triangles degenerated earlier that are losers no more - a
    // partial upload left them as they were - then this draw's losers
    for (const std::size_t tri : restore) {
        const std::size_t c = 3 * tri;
        if (c + 2 >= vn->second || c + 2 >= g->corners.size()) continue;
        for (std::size_t k = c; k < c + 3; ++k) {
            const auto& s = g->corners[k];
            v[k] = {s.x, s.y, s.z, s.u, s.v, s.r, s.g, s.b, s.nx, s.ny, s.nz, s.phase};
        }
    }
    for (const std::size_t tri : losers) {
        const std::size_t c = 3 * tri;
        if (c + 2 >= vn->second) continue;
        v[c + 1].x = v[c + 2].x = v[c].x;
        v[c + 1].y = v[c + 2].y = v[c].y;
        v[c + 1].z = v[c + 2].z = v[c].z;
    }
    vkUnmapMemory(dev_, vb->second.second);
    t.dropped += static_cast<long>(losers.size());
    // Reported at `end()`, once per geometry - a set's batches arrive over
    // many submits, and a running total after the first read as the whole
    // (it did: 30 for Anekbah's 248), while not every triangle is ever
    // submitted, so "all walked" never comes.
    t.touched = true;
}

void VulkanRenderer::setViewport(const omk::View& view) {
    // ...in RENDER pixels, so the letterbox's strip scales with the frame.
    picH_ = view.letterboxed() ? view.vh : h_;
    int vw = rs(view.letterboxed() ? view.vw : w_);
    int vh = rs(picH_);
    // the FLOORS' reflection: drawn at HALF size into the image's top-left
    // quarter (`reflHalf_`) - a quarter of the pixels; the water's, whole
    if (reflHalfNow()) { vw = (vw + 1) / 2; vh = (vh + 1) / 2; }
    // ...the second level's in the top-RIGHT one (`second_`)
    const int x0 = second_ && reflHalfNow() ? reflHalfW() : 0;
    const VkViewport vp{static_cast<float>(x0), 0, static_cast<float>(vw), static_cast<float>(vh),
                        0.0f, 1.0f};
    const VkRect2D sc{{x0, 0}, {static_cast<uint32_t>(std::min(vw, rw_ - x0)), static_cast<uint32_t>(vh)}};
    vkCmdSetViewport(cb_, 0, 1, &vp);
    vkCmdSetScissor(cb_, 0, 1, &sc);
}

void VulkanRenderer::pushView(const omk::View& view) {
    // The view-projection, built from the SOFTWARE rasterizer's own basis so
    // the two cannot disagree about a convention.
    omk::RCamera cam = view.cam;
    flipX_ = cam.flipX;
    // The letterbox is a VIEWPORT: the picture goes into the top-left
    // `vw x vh` of an attachment that stays window-sized, because the
    // swapchain present needs that size. The vertical fov follows from `vh`.
    cam.w = view.letterboxed() ? view.vw : w_;
    cam.h = view.letterboxed() ? view.vh : h_;
    float s[3], u[3], f[3], tanH = 0, tanV = 0;
    omk::cameraBasis(cam, s, u, f, tanH, tanV);

    // Vulkan clip space already has Y DOWN and Z in [0,1], which happens to
    // suit a game whose own Y points down. Screen x = w/2*(1 + (vx/vz)/tanH)
    // is x_ndc = (vx/vz)/tanH; screen y = h/2*(1 - (vy/vz)/tanV) is
    // y_ndc = -(vy/vz)/tanV. So the projection rows are 1/tanH and -1/tanV,
    // with w = vz.
    const float nearP = omk::kNearCut, farP = 200000.0f;
    const float A = farP / (farP - nearP), B = -farP * nearP / (farP - nearP);
    // view = R * (world - eye), R's rows are s, u, f. Folded into one matrix,
    // COLUMN-major as GLSL wants it.
    const float e[3] = {cam.eye[0], cam.eye[1], cam.eye[2]};
#if OMK_VR
    // A VR eye's off-axis frustum (`RCamera::lensX`/`lensY`), as GLES's: the
    // rows take `- lens * f`, and 0 leaves them bit-identical.
    const float lx = cam.lensX, ly = cam.lensY;
    const float fe0 = f[0] * e[0] + f[1] * e[1] + f[2] * e[2];
    const float r0[4] = {(s[0] - lx * f[0]) / tanH, (s[1] - lx * f[1]) / tanH,
                         (s[2] - lx * f[2]) / tanH,
                         (-(s[0] * e[0] + s[1] * e[1] + s[2] * e[2]) + lx * fe0) / tanH};
    const float r1[4] = {-(u[0] - ly * f[0]) / tanV, -(u[1] - ly * f[1]) / tanV,
                         -(u[2] - ly * f[2]) / tanV,
                         ((u[0] * e[0] + u[1] * e[1] + u[2] * e[2]) - ly * fe0) / tanV};
#else
    const float r0[4] = {s[0] / tanH, s[1] / tanH, s[2] / tanH,
                         -(s[0] * e[0] + s[1] * e[1] + s[2] * e[2]) / tanH};
    const float r1[4] = {-u[0] / tanV, -u[1] / tanV, -u[2] / tanV,
                         (u[0] * e[0] + u[1] * e[1] + u[2] * e[2]) / tanV};
#endif
    const float r2[4] = {A * f[0], A * f[1], A * f[2],
                         -A * (f[0] * e[0] + f[1] * e[1] + f[2] * e[2]) + B};
    const float r3[4] = {f[0], f[1], f[2],
                         -(f[0] * e[0] + f[1] * e[1] + f[2] * e[2])};
    const float* rows[4] = {r0, r1, r2, r3};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            push_.mvp[c * 4 + r] = rows[r][c];
}

// THE DEPTH PASS FROM THE LIGHT - `todo/enhancements.md` row 6.
//
// An ORTHOGRAPHIC slab fitted to the casters, not to the scene: a fragment
// outside it is lit by definition, which is what keeps a characters-only
// shadow from darkening the far end of a street. The same matrix writes the
// map and reads it, so whatever Vulkan's clip space does to the handedness
// cancels between the two.
void VulkanRenderer::shadowPass(const omk::View& v, std::span<const omk::Draw> casters) {
    if (shPipe_ == VK_NULL_HANDLE || !v.shadow.on || casters.empty()) return;
    const float R = v.shadow.radius > 1.0f ? v.shadow.radius : 1.0f;

    // the light's basis: forward along its direction, any stable up
    float f[3] = {v.shadow.dir[0], v.shadow.dir[1], v.shadow.dir[2]};
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl < 1e-6f) return;
    for (float& c : f) c /= fl;
    float up[3] = {0.0f, -1.0f, 0.0f};
    if (std::fabs(f[0] * up[0] + f[1] * up[1] + f[2] * up[2]) > 0.99f) {
        up[0] = 1.0f; up[1] = 0.0f; up[2] = 0.0f;
    }
    float r[3] = {up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2],
                  up[0] * f[1] - up[1] * f[0]};
    const float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (float& c : r) c /= rl;
    float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2],
                  f[0] * r[1] - f[1] * r[0]};
    // The eye sits a slab's depth behind the casters, so nothing is clipped
    // by the near plane however the light is angled.
    const float back = R * 2.0f;
    float eye[3];
    for (int k = 0; k < 3; ++k) eye[k] = v.shadow.centre[k] - f[k] * back;
    const float far = back + R * 2.0f;

    // column-major, the way `pushView` writes the scene's
    float m[16] = {};
    const float* ax[3] = {r, u, f};
    const float sc[3] = {1.0f / R, 1.0f / R, 1.0f / far};
    for (int row = 0; row < 3; ++row) {
        float d = 0.0f;
        for (int c = 0; c < 3; ++c) {
            m[c * 4 + row] = ax[row][c] * sc[row];
            d += ax[row][c] * eye[c];
        }
        m[12 + row] = -d * sc[row];
    }
    m[15] = 1.0f;

    if (!cbStarted_) {
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkResetCommandBuffer(cb_, 0);
        vkBeginCommandBuffer(cb_, &bi);
        cbStarted_ = true;
        tsCount_ = 0;
        stamp("start");
    }
    // a posed caster is placed on the CPU: the depth-only pipeline has no set 2
    std::vector<omk::Draw> placed;
    placed.reserve(casters.size());
    for (const auto& d : casters)
        placed.push_back(d.meshPose && d.meshPoses && d.geo &&
                                 d.geo->cornerMesh.size() == d.geo->corners.size()
                             ? cpuPose(d) : d);
    for (const auto& d : placed) if (d.geo) uploadGeometry(d.geo);

    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = shPass_; rp.framebuffer = shFbuf_;
    rp.renderArea = {{0, 0}, {kShadowSide, kShadowSide}};
    rp.clearValueCount = 1; rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cb_, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, shPipe_);
    vkCmdPushConstants(cb_, shPlo_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof m, m);
    for (const auto& d : placed) {
        if (!d.geo || !d.count) continue;
        const auto it = vbo_.find(d.geo);
        if (it == vbo_.end()) continue;
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb_, 0, 1, &it->second.first, &off);
        vkCmdDraw(cb_, static_cast<uint32_t>(d.count), 1,
                  static_cast<uint32_t>(d.start), 0);
    }
    vkCmdEndRenderPass(cb_);
    stamp("shadow");

    if (shUboPtr_) {
        ShadowUbo ub{};
        std::memcpy(ub.lightMvp, m, sizeof m);
        ub.strength = v.shadow.strength;
        ub.texel = 1.0f / static_cast<float>(kShadowSide);
        // In the slab's own 0..1 depth, one map texel of slope at the worst
        // angle - enough to stop the ground shadowing itself.
        ub.bias = 0.0015f;
        ub.shimmer = shimmerClock_;
        std::memcpy(shUboPtr_, &ub, sizeof ub);
    }
    shadowLive_ = true;
}

void VulkanRenderer::begin(const omk::View& view) {
    settle();   // the last frame's GPU work, before its buffers are reused
    st_ = omk::RasterStats{};
    // the pose buffer from its start: the frame that wrote it was waited for
    poseUsed_ = 0;
    poseCache_.clear();
    // the previous frame's fence has been waited on (the present ends with it)
    for (auto& bm : deadBufs_) {
        vkDestroyBuffer(dev_, bm.first, nullptr);
        freeDeviceMemory(bm.second);
    }
    deadBufs_.clear();
    fog_ = view.fog;
    fogStart_ = view.fogStart;
    mistOn_ = view.mist > 0.0f || view.dust > 0.0f || view.veil > 0.0f;   // the dust and veils take the marked batches
    fogEnd_ = view.fogEnd;
    for (int i = 0; i < 3; ++i)
        fogColour_[i] = static_cast<float>(view.fogColour[i]) / 255.0f;
    for (int i = 0; i < 3; ++i)
        clearColour_[i] = static_cast<float>(view.clearColour[i]) / 255.0f;
    pushView(view);
    // the ambient occlusion's camera: the last pass begun is the frame's own
    if (aoOk_) {
        omk::RCamera c = view.cam;
        c.w = view.letterboxed() ? view.vw : w_;
        c.h = view.letterboxed() ? view.vh : h_;
        float s3[3], u3[3], f3[3], tH = 1.0f, tV = 1.0f;
        omk::cameraBasis(c, s3, u3, f3, tH, tV);
        aoView4_[0] = tH; aoView4_[1] = tV;
        aoView4_[2] = static_cast<float>(rs(c.w)); aoView4_[3] = static_cast<float>(rs(c.h));
    }
    // ---- THE LIGHTS, per frame (`todo/enhancements.md` row 7) ------------
    shimmerClock_ = view.shimmerClock;
    rain_ = view.rain;
    halo_ = view.halo;
    dither_ = view.dither;
    litCount_ = 0;
    if (litUboPtr_) {
        LightUbo ub{};
        const int n = static_cast<int>(std::min<std::size_t>(
            view.lights.size(), static_cast<std::size_t>(omk::View::kMaxGpuLights)));
        for (int i = 0; i < n; ++i) {
            const auto& l = view.lights[static_cast<std::size_t>(i)];
            for (int k = 0; k < 3; ++k) {
                ub.l[i].posA[k] = l.pos[k];
                ub.l[i].dirB[k] = l.dir[k];
                ub.l[i].colourI[k] = l.colour[k];
            }
            ub.l[i].posA[3] = l.radiusA;
            ub.l[i].dirB[3] = l.radiusB;
            ub.l[i].colourI[3] = l.intensity;
        }
        ub.count = n;
        ub.base = view.litBase;
        ub.grey = view.grey ? 1 : 0;
        for (int k = 0; k < 3; ++k) ub.eye[k] = view.cam.eye[k];
        ub.eye[3] = view.setDim;   // the night (`View::setDim`), w of the eye's vec4
        ub.fx[0] = view.relief;
        // THE MIST (`View::mist`): its strength, its ground and its height
        ub.fx[1] = view.mist; ub.fx[2] = view.mistGround; ub.fx[3] = view.mistHeight;
        ub.dust[0] = view.dust; ub.dust[1] = view.dustDrift[0]; ub.dust[2] = view.dustDrift[1];
        ub.dust[3] = view.dustScale;
        for (int k = 0; k < 3; ++k) ub.dustColour[k] = view.dustColour[k];
        ub.dustColour[3] = 0.0f;
        ub.veil[0] = view.veil; ub.veil[1] = view.veilFall; ub.veil[2] = view.veilScale;
        ub.veil[3] = view.puddles;   // the PUDDLES' strength, in the veils' spare
        for (int k = 0; k < 3; ++k) ub.veilColour[k] = view.veilColour[k];
        ub.veilColour[3] = view.veilGust;   // w the gust's lean
        const int ns = static_cast<int>(std::min<std::size_t>(view.streakLights.size(),
                                                              static_cast<std::size_t>(omk::View::kMaxStreakLights)));
        for (int i = 0; i < ns; ++i) {
            const auto& l = view.streakLights[static_cast<std::size_t>(i)];
            for (int k = 0; k < 3; ++k) { ub.streak[i].posA[k] = l.pos[k]; ub.streak[i].colourI[k] = l.colour[k]; }
            ub.streak[i].posA[3] = l.radiusA;
            ub.streak[i].colourI[3] = l.intensity;
        }
        ub.streakN[0] = static_cast<float>(ns);
        ub.streakN[1] = view.streaks;
        // THE TILED POINT LIGHTS (`o3de/lighttiles.h`): every one the frame
        // keeps, and their tiles binned for THIS camera at the render size.
        // The reflection pass begins first and the frame's pass last - the
        // last binning is the one both read, and the reflection skips them.
        ub.tiles = 0;
        tilesNow_ = false;
        // `OMK_REFL_UNLIT=1`, an instrument: the reflection's tiles left empty,
        // as before - what `verify.py: engine: floors` compares against
        static const bool reflUnlit = omk::envFlag("OMK_REFL_UNLIT");
        if (passFb_ != VK_NULL_HANDLE && reflUnlit && !view.points.empty() && tilesPtr_ && pipeTiled_[0]) {
            // the tiled pipelines, as the reflection drew with, and NO tiles
            // (0 across): the module the same, its lights none
            std::memset(static_cast<char*>(tilesPtr_) + reflTilesAt(), 0, 16);
            ub.tiles = 1;
            tilesNow_ = true;
        } else if (passFb_ != VK_NULL_HANDLE && !view.points.empty() && pointsPtr_ && tilesPtr_ && pipeTiled_[0] &&
            (second_ ? ds1Refl2_ : ds1Refl_) != VK_NULL_HANDLE) {
            // THE WATER REFLECTION'S begin: its tiles binned for the REFLECTED
            // camera into the buffer's second half, which its own set 1 binds
            // (the points are the frame's - the world's lights light the world
            // wherever it is seen from; the frame's begin writes them again)
            const std::size_t np = std::min<std::size_t>(view.points.size(), static_cast<std::size_t>(omk::kMaxPoints));
            omk::RCamera bc = view.cam;
            bc.w = view.letterboxed() ? view.vw : w_;
            bc.h = view.letterboxed() ? view.vh : h_;
            const int hd = reflHalf_ ? 2 : 1;   // the floors' reflection is drawn at half size
            omk::binLights(bc, (rs(bc.w) + hd - 1) / hd, (rs(bc.h) + hd - 1) / hd,
                           std::span<const omk::View::GpuLight>(view.points.data(), np), tilesRefl_);
            // (the second level's: its x origin above the tile's size, `scene.frag`)
            if (second_ && tilesRefl_.words.size() > 3)
                tilesRefl_.words[3] |= static_cast<std::uint32_t>(reflHalfW()) << 16;
            if (tilesRefl_.words.size() <= reflTilesCap()) {
                std::memcpy(static_cast<char*>(tilesPtr_) + reflTilesAt(), tilesRefl_.words.data(),
                            tilesRefl_.words.size() * 4);
                ub.tiles = 1;
                tilesNow_ = true;
            }
        } else if (!view.points.empty() && pointsPtr_ && tilesPtr_ && pipeTiled_[0]) {
            const std::size_t np = std::min<std::size_t>(view.points.size(), static_cast<std::size_t>(omk::kMaxPoints));
            auto* gp = static_cast<GpuLightStd140*>(pointsPtr_);
            for (std::size_t i = 0; i < np; ++i) {
                const auto& l = view.points[i];
                for (int k = 0; k < 3; ++k) { gp[i].posA[k] = l.pos[k]; gp[i].dirB[k] = l.dir[k]; gp[i].colourI[k] = l.colour[k]; }
                gp[i].posA[3] = l.radiusA; gp[i].dirB[3] = l.radiusB; gp[i].colourI[3] = l.intensity;
            }
            omk::RCamera bc = view.cam;
            bc.w = view.letterboxed() ? view.vw : w_;
            bc.h = view.letterboxed() ? view.vh : h_;
            omk::binLights(bc, rs(bc.w), rs(bc.h), std::span<const omk::View::GpuLight>(view.points.data(), np), tiles_);
            if (tiles_.words.size() <= kTileWordsMax) {
                std::memcpy(tilesPtr_, tiles_.words.data(), tiles_.words.size() * 4);
                ub.tiles = 1;
                tilesNow_ = true;
            } else if (!tilesOverTold_) {
                tilesOverTold_ = true;   // said, never silent: every point light goes with it
                std::fprintf(stderr, "vulkan: the tiled lights' lists (%zu words) past the buffer's %llu - "
                             "the point lights off this frame\n", tiles_.words.size(),
                             static_cast<unsigned long long>(kTileWordsMax));
            }
            if (!tilesTold_) {
                tilesTold_ = true;
                omk::event("remaster.tiles", {{"lights", static_cast<long>(np)}, {"listed", tiles_.listed},
                                              {"full", tiles_.full}, {"tile", tiles_.tile}});
                std::printf("vulkan: the tiled point lights - %zu lights over %dx%d tiles of %d px, %ld listed, "
                            "%d tiles full (a REMASTER feature)\n", np, tiles_.across, tiles_.down, tiles_.tile,
                            tiles_.listed, tiles_.full);
            }
        }
        litCount_ = n;
        std::memcpy(litUboPtr_, &ub, sizeof ub);
    }

    // The shadow pass may already have opened it; it must be opened ONCE, and
    // the depth pass has to be recorded before this render pass begins.
    if (!cbStarted_) {
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkResetCommandBuffer(cb_, 0);
        vkBeginCommandBuffer(cb_, &bi);
        cbStarted_ = true;
        tsCount_ = 0;
        stamp("start");
    }
    // A frame with no shadow pass says so through the uniform, because the
    // descriptor set is bound either way.
    if (!shadowLive_ && shUboPtr_) {
        ShadowUbo ub{};
        ub.texel = 1.0f / static_cast<float>(kShadowSide);
        ub.shimmer = shimmerClock_;   // the shimmer runs with or without a shadow
        std::memcpy(shUboPtr_, &ub, sizeof ub);
    }
    VkClearValue clear[2]{};
    // the engine clears to the scene's fog colour (`View::clearColour`); with
    // the BLOOM its alpha is the light map's value, so what no surface covers
    // clears to 0 - not light (at 1 the fog past the clip distance glowed)
    clear[0].color = {{clearColour_[0], clearColour_[1], clearColour_[2], bloom_ > 0.0f ? 0.0f : 1.0f}};
    clear[1].depthStencil = {1.0f, 0};   // depth far, stencil 0
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = passFb_ && second_ ? (passFb_ == reflFbuf1_ ? reflPass1Keep_ : reflPassKeep_)
                  : passFb_ && passFb_ == reflFbuf1_ ? reflPass1_ : passFb_ && reflPass_ ? reflPass_ : pass_;
    // the post's this frame: decided here, where the scene's target is
    // chosen - with it the scene lands in `postSrc_`, and `end` must run it.
    // The bloom asks for it only with a light map in the pool: without one
    // every texel's light is 0 and the glow would move no pixel
    if (!passFb_) {
        lightMapNow_ = false;
        postNow_ = ((bloom_ > 0.0f && poolLit_) || lutN_ > 1 || aoOk_) && makePost();
    }
    rp.framebuffer = passFb_ ? passFb_ : postNow_ ? fbufPost_ : fbuf_;
    // the RENDER size, not the output one: a render area smaller than the
    // attachment leaves the rest of it undefined, and with supersampling on
    // the resolve then averages four blocks of uninitialised memory.
    rp.renderArea = {{0, 0}, {static_cast<uint32_t>(reflHalfNow() ? (rw_ + 1) / 2 : rw_),
                              static_cast<uint32_t>(reflHalfNow() ? (rh_ + 1) / 2 : rh_)}};
    // (the second level's: the top-right quarter alone - its clear too)
    if (second_ && reflHalfNow()) {
        rp.renderArea.offset.x = reflHalfW();
        rp.renderArea.extent.width = static_cast<uint32_t>(rw_ - reflHalfW());
    }
    rp.clearValueCount = 2; rp.pClearValues = clear;
    vkCmdBeginRenderPass(cb_, &rp, VK_SUBPASS_CONTENTS_INLINE);
    setViewport(view);
    recording_ = true;
}

// A POSED body's block in the pose buffer (`posesBodies`): written once a
// frame for each body (its draws share the `meshPose` and the lights), at an
// offset the device's storage alignment allows. -> false when it does not fit.
bool VulkanRenderer::writePose(const omk::Draw& d, uint32_t& off) {
    const auto key = std::make_pair(d.meshPose, d.vertexLights);
    if (const auto it = poseCache_.find(key); it != poseCache_.end()) { off = it->second; return true; }
    const VkDeviceSize at = (poseUsed_ + poseAlign_ - 1) / poseAlign_ * poseAlign_;
    if (!posePtr_ || at + kPoseRange > kPoseBytes || d.meshPoses > kPoseMaxMeshes) return false;
    float* f = reinterpret_cast<float*>(static_cast<unsigned char*>(posePtr_) + at);
    const int nl = d.vertexLights ? std::min(d.vertexLightCount, kPoseLights) : 0;
    f[0] = static_cast<float>(nl);
    f[1] = d.lightsFromBlack ? 1.0f : 0.0f;
    f[2] = d.lightBase;
    f[3] = static_cast<float>(d.meshPoses);
    for (int i = 0; i < nl; ++i) std::memcpy(f + 4 + 8 * i, d.vertexLights + 8 * i, 8 * sizeof(float));
    float* a = f + 4 + 8 * kPoseLights;
    std::memcpy(a, d.meshPose, d.meshPoses * 12 * sizeof(float));   // three vec4 an affine
    float* t = a + 12 * d.meshPoses;                                 // ...and the body light's tint
    t[0] = d.lightTint[0]; t[1] = d.lightTint[1]; t[2] = d.lightTint[2]; t[3] = 1.0f;
    poseUsed_ = at + (1 + 2 * kPoseLights + 3 * d.meshPoses + 1) * 16;
    off = static_cast<uint32_t>(at);
    poseCache_.emplace(key, off);
    return true;
}

// A posed draw's corners placed on the CPU instead - by the same affine and,
// for the colour, `applyLights`' law on the posed normal from the scene's grey
// where the body starts black (the shader's rule, `scene.vert`). For what the
// pose buffer does not reach: a body past its room, and every caster of the
// shadow map, whose depth-only pipeline has no set 2.
omk::Draw VulkanRenderer::cpuPose(const omk::Draw& d) {
    omk::Geometry& cp = cpuPosed_[d.geo];
    omk::poseGeometry(d, cp, omk::PoseParts{true, true, kPoseLights});   // o3de/drawcore.h
    cp.revision = d.geo->revision + (++cpuPoseRev_ << 32);
    omk::Draw out = d;
    out.geo = &cp;
    out.meshPose = nullptr;
    out.meshPoses = 0;
    out.vertexLights = nullptr;
    out.vertexLightCount = 0;
    out.lightsFromBlack = false;
    return out;
}

void VulkanRenderer::submit(const omk::Draw& d) {
    if (!recording_ || !d.geo || !d.count) return;
    if (d.meshPose && d.meshPoses && d.geo->cornerMesh.size() == d.geo->corners.size()) {
        uint32_t off = 0;
        if (poseDs_ != VK_NULL_HANDLE && writePose(d, off)) submitOne(d, true, off);
        else submitOne(cpuPose(d), false, 0);
        return;
    }
    submitOne(d, false, 0);
}

void VulkanRenderer::submitOne(const omk::Draw& d, bool posed, uint32_t poseOff) {
    if (!uploadGeometry(d.geo)) return;
    resolveTies(d);
    st_.triangles += static_cast<long>(d.count / 3);

    const int k = d.blend == omk::Blend::Opaque ? 0
                : d.blend == omk::Blend::Add    ? 1 : 2;
    // The mirror pass swaps the state under the same submissions: the stencil
    // marker while it is marking, the stencil-tested variants while it is
    // drawing the reflection. The DRAWS are unchanged, which is the point -
    // the backend is choosing how, not what.
    VkPipeline use = forcePipeline_ ? forcePipeline_
                   : reflStencil_   ? pipeRefl_[k]
                   : passFb_ && passFb_ == reflFbuf1_ ? (tilesNow_ && pipeTiled1_[k] ? pipeTiled1_[k] : pipe1_[k])
                   : tilesNow_ && pipeTiled_[k] ? pipeTiled_[k]
                                    : pipe_[k];
    // an opaque CUTOUT draw: the same pipeline with the shader's depth
    if (d.cutout && use == pipe_[0] && pipeCut_) use = pipeCut_;
    else if (d.cutout && use == pipeTiled_[0] && pipeTiledCut_) use = pipeTiledCut_;
    vkCmdBindPipeline(cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, use);

    // The texture is the key's LOW SIX BITS and nothing else (ASSETS 4b) -
    // or the pool's full index above them, past 64 (`drawTextureSlot`).
    const std::size_t slot = omk::drawTextureSlot(d);
    const VkDescriptorSet ds =
        (slot < tex_.size() && tex_[slot].ds != VK_NULL_HANDLE) ? tex_[slot].ds
                                                                : white_.ds;
    if (ds == VK_NULL_HANDLE) return;   // not even the white stand-in: nothing to draw with
    lightMapNow_ |= slot < tex_.size() && tex_[slot].emView != VK_NULL_HANDLE;
    vkCmdBindDescriptorSets(cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, plo_, 0, 1, &ds, 0, nullptr);
    // (the water reflection's pass binds its own: the tiles of ITS camera)
    const VkDescriptorSet dsR = second_ ? ds1Refl2_ : ds1Refl_;
    const VkDescriptorSet ds1 = passFb_ != VK_NULL_HANDLE && dsR != VK_NULL_HANDLE ? dsR : ds1_;
    if (ds1 != VK_NULL_HANDLE)
        vkCmdBindDescriptorSets(cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, plo_, 1, 1,
                                &ds1, 0, nullptr);
    // set 2, the pose buffer - bound for every draw (the shader declares it),
    // at the body's block for a posed one
    if (poseDs_ != VK_NULL_HANDLE)
        vkCmdBindDescriptorSets(cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, plo_, 2, 1,
                                &poseDs_, 1, &poseOff);
    push_.posed = posed ? 1 : 0;

    push_.cutout = d.cutout ? 1 : 0;
    push_.caster = d.castsShadow ? 1 : 0;
    push_.bloomMode = bloom_ > 0.0f ? 1 : 0;
    // ...and a REFLECTING FLOOR's run (`Draw::reflect`), the water's rule at
    // its own strength - in the frame's pass, never the reflection's
    const bool floorNow = !reflectingNow_ && d.reflect > 0.0f;
    push_.water = waterNow_ ? 1 : floorNow ? 3 : reflectingNow_ ? 2 : 0;
    // ...and a WET floor's darkening (`Draw::wet`), in the mode's high bits -
    // the block is full: 0..255 above the low four
    if (!reflectingNow_ && d.wet > 0.0f) {
        push_.water |= static_cast<int32_t>(std::lround(std::min(d.wet, 1.0f) * 255.0f)) << 4;
        // ...and the RAIN's strength for its ripples, the eight bits above
        push_.water |= static_cast<int32_t>(std::lround(std::clamp(rain_, 0.0f, 1.0f) * 255.0f)) << 12;
        // ...and bit 20: it is SNOW lying, not rain (`Draw::snow`)
        if (d.snow) push_.water |= 1 << 20;
        // ...and bit 21: a BODY in the rain (`Draw::wetBody`), not a floor
        if (d.wetBody) push_.water |= 1 << 21;
    }
    // ...and bit 22: a floor of the SECOND level (`Draw::reflectPlane`), its
    // reflection the image's top-right quarter
    if (floorNow && twoRefl_ && d.reflectPlane == 1) push_.water |= 1 << 22;
    push_.waterStrength = reflectingNow_ ? waterY_ : floorNow ? waterStrength_ * d.reflect : waterStrength_;
    {
        const Tex& bound = slot < tex_.size() && tex_[slot].ds != VK_NULL_HANDLE ? tex_[slot] : white_;
        push_.texScaled = bound.packed ? 1 : 0;
        push_.texScaleU = bound.su;
        push_.texScaleV = bound.sv;
    }
    // ...lit while the frame has lights: the uniform block's - or the TILED
    // point lights, which hold the emitters' when they are tiled: with the
    // block's count alone, a frame where none of the set's own lights reached
    // the player drew every draw unlit, the emitters' lights in the tiles
    // unread - the arcade and Kay'l went dark a step from a lit spot (the
    // reader, 2026-10-28; `OMK_NO_TILES=1`, slotting the emitters, hid it)
    push_.lit = litCount_ > 0 || tilesNow_ ? d.lit : 0;
    // THE FOG's two exclusions, applied here because this is where the bucket
    // key is - the same rule `renderer.cpp` applies for the software loop, and
    // it MUST be the same rule or the two backends draw different pictures.
    // Key bits 0x2080 (the near bucket, the transparent state) are not fogged;
    // key bit 0x800 (the cutout path, and the sky through mesh flag 0x10000)
    // doubles both ends.
    push_.fogStart = 0.0f;
    push_.fogEnd = 0.0f;
    if (fog_ && !(d.bucketKey & 0x2080u)) {
        const float fogScale = (d.bucketKey & 0x800u) ? 2.0f : 1.0f;
        push_.fogStart = fogStart_ * fogScale;
        push_.fogEnd   = fogEnd_ * fogScale;
        for (int i = 0; i < 3; ++i) push_.fogColour[i] = fogColour_[i];
    } else if (mistOn_ && !(d.bucketKey & 0x2080u)) {
        // THE MIST without the game's fog (an unlimited clip has none): the
        // same batches, marked -1 / -1 - no linear fog (`fogEnd > fogStart`
        // fails), the mist's to take (`scene.frag`)
        push_.fogStart = -1.0f;
        push_.fogEnd = -1.0f;
        for (int i = 0; i < 3; ++i) push_.fogColour[i] = fogColour_[i];
    }
    const auto it = vbo_.find(d.geo);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb_, 0, 1, &it->second.first, &off);
    // THE BACK-FACE CULL, one push per run of `cornerCull` (`geom3do.h`): a
    // batch groups by material, so single- and two-sided faces can share one.
    // `kCullSign` is this pipeline's sense of "front" for the software
    // rasterizer's winding; the mirror pass's screen-X flip reverses it, as it
    // reverses the area `raster.cpp` tests.
    const omk::Geometry* g = d.geo;
    const bool haveCull = g->cornerCull.size() == g->corners.size();
    std::size_t i = d.start;
    const std::size_t e = d.start + d.count;
    while (i < e) {
        const std::uint8_t c = haveCull ? g->cornerCull[i] : 0u;
        std::size_t j = i;
        while (j < e && (haveCull ? g->cornerCull[j] : 0u) == c) ++j;
        push_.cull = c ? (flipX_ ? -kCullSign : kCullSign) : 0;
        vkCmdPushConstants(cb_, plo_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(Push), &push_);
        vkCmdDraw(cb_, static_cast<uint32_t>(j - i), 1, static_cast<uint32_t>(i), 0);
        i = j;
    }
    st_.drawn += static_cast<long>(d.count / 3);
}

// The NATIVE mirror pass - one frame, one submit, nothing touching the CPU.
//
// The boundary hands down decisions and this turns them into API calls, which
// is A2's rule: the reflected view is `drawWithMirror`'s (`p -= 2*dist*n` and
// the screen-X flip the engine's own bucket walk performs), `sceneClipped` is
// the scene restricted to the mirror's front half-space, and `mirror` is the
// draws whose corners carry flag `0x100000`. What is chosen HERE is only the
// stencil - the answer to "where may the reflection land", which the CPU path
// answers by differencing two frames instead.
bool VulkanRenderer::drawMirrorScene(const omk::View& v, const omk::View& refl,
                                     std::span<const omk::Draw> scene,
                                     std::span<const omk::Draw> sceneClipped,
                                     std::span<const omk::Draw> mirror) {
    // THE ORIGINAL'S TWO PASSES (`sub_440D90`, 0x00440D90):
    // the scene through the camera reflected in the mirror's plane FIRST, then
    // through the real camera over it - `Render_Frame(reflected, 1)` then
    // `Render_Frame(camera, 0)`, and `Render_FlushBuckets` begins its scene
    // with ZENABLE on and clears nothing between them. The reflected pass
    // leaves the depths of a virtual room behind the plane, so the real room's
    // walls, nearer, cover it everywhere but the mirror's opening, and the
    // mirror's own faces blend over what shows through. The GLES backend does
    // the same, so the two draw a mirror alike. The draws in FRONT of the plane
    // only, as the CPU fallback takes them.
    if (mirror.empty()) return false;
    begin(v);
    pushView(refl);
    for (const auto& d : sceneClipped) submit(d);
    pushView(v);
    for (const auto& d : scene) submit(d);
    for (const auto& d : mirror) submit(d);
    end();
    return true;
}

// the frame's stamps, read after its fence: each pass's time is from the stamp
// before it to its own (the first, "start", none)
void VulkanRenderer::readStamps() {
    static const char* const kNames[kStamps] = {"shadow", "reflection", "scene", "post"};
    if (!tsPool_ || tsCount_ < 2) return;
    uint64_t t[kStamps] = {};
    if (vkGetQueryPoolResults(dev_, tsPool_, 0, tsCount_, sizeof t, t, sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
    for (uint32_t i = 1; i < tsCount_; ++i)
        for (uint32_t n = 0; n < kStamps && kNames[n]; ++n)
            if (std::strcmp(tsLabel_[i], kNames[n]) == 0)
                tsSum_[n] += static_cast<double>(t[i] - t[i - 1]) * props_.limits.timestampPeriod / 1e6;
    if (++tsFrames_ < 60) return;
    std::printf("vulkan: GPU ms a frame (mean of 60) - shadow %.2f, reflection %.2f, scene %.2f, post %.2f\n",
                tsSum_[0] / 60.0, tsSum_[1] / 60.0, tsSum_[2] / 60.0, tsSum_[3] / 60.0);
    omk::event("gpu.time", {{"shadow", tsSum_[0] / 60.0}, {"reflection", tsSum_[1] / 60.0},
                            {"scene", tsSum_[2] / 60.0}, {"post", tsSum_[3] / 60.0}});
    for (auto& v : tsSum_) v = 0.0;
    tsFrames_ = 0;
}

void VulkanRenderer::end() {
    // `OMK_TIE_STATS=1`: every 30 frames, how each large geometry's revisions
    // were answered - replayed from the dirty corners, walked, or a replay
    // abandoned part way (`engine: dirty corners`).
    static const bool tieStats = omk::envFlag("OMK_TIE_STATS");
    static long frameNo = 0;
    if (tieStats && ++frameNo % 30 == 0) {
        for (const auto& [g, t] : tie_)
            if (g->corners.size() > 30000)
                std::printf("tie stats: frame %ld, %zu triangles - replayed %ld walked %ld fallbacks %ld\n",
                            frameNo, g->corners.size() / 3, t.replays, t.walks, t.fallbacks);
        // ...and where the full walks went: every geometry, largest cost first
        std::vector<std::pair<long, std::size_t>> cost;
        long faces = 0;
        for (auto& [g, t] : tie_) {
            if (t.facesWalked > 0) cost.push_back({t.facesWalked, g->corners.size() / 3});
            faces += t.facesWalked;
            t.facesWalked = 0;
        }
        std::sort(cost.begin(), cost.end(), std::greater<>());
        std::printf("tie walk: frames %ld-%ld, %zu geometries walked %ld faces;", frameNo - 29, frameNo,
                    cost.size(), faces);
        for (std::size_t k = 0; k < cost.size() && k < 8; ++k)
            std::printf(" %ld over %zu tris%s", cost[k].first, cost[k].second, k + 1 < cost.size() && k < 7 ? "," : "");
        std::printf("\n");
    }
    // The depth-tie pass's report, once per geometry with losers.
    for (auto& [g, t] : tie_)
        if (t.touched && !t.logged && t.dropped > 0 && g->corners.size() > 3000) {
            t.logged = true;
            std::printf("vulkan: depth tie - %ld of %ld triangles coincide with an earlier face "
                        "and are degenerated so the first drawn wins, as the engine's strict test "
                        "decides\n", t.dropped, static_cast<long>(g->corners.size() / 3));
        }
    if (!recording_) return;
    vkCmdEndRenderPass(cb_);
    stamp("scene");
    if (postNow_) {
        recordPost(cb_);
        stamp("post");
    }
    // The colour attachment ends in TRANSFER_SRC_OPTIMAL (the render pass says
    // so). The READBACK COPY is not recorded here: a frame the GPU presents
    // (`presentWorld`) never reads it, so copying every frame is waste
    // (todo/cpu-vs-original.md tier A). `readback()` records it when a frame
    // is read - the image is in TRANSFER_SRC_OPTIMAL then too, the present
    // pass handing it back so.
    copyOwed_ = true;
    srcW_ = rw_; srcH_ = rh_;   // the whole render target is live
    vkEndCommandBuffer(cb_);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cb_;
    vkResetFences(dev_, 1, &fence_);
    vkQueueSubmit(queue_, 1, &si, fence_);
    // NOT WAITED HERE: the present's fence is the frame's wait (`settle`)
    sceneInFlight_ = true;
    recording_ = false;
    cbStarted_ = false;
    shadowLive_ = false;
    dirty_ = true;
}

// what the scene's submit leaves for the CPU, read once it is done
void VulkanRenderer::readSceneResults() {
    readStamps();
    // the second level's queries, this frame's: seen if any sample passed
    if (levelIssued_ > 0) {
        uint64_t q[kLevelQueries] = {};
        if (vkGetQueryPoolResults(dev_, levelPool_, 0, levelIssued_, sizeof q, q, sizeof(uint64_t),
                                  VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            levelSeen_ = levelOver_ ? 1 : 0;
            for (uint32_t i = 0; i < levelIssued_; ++i) if (q[i] > 0) levelSeen_ = 1;
        } else {
            levelSeen_ = -1;
        }
    } else {
        levelSeen_ = -1;
    }
    levelIssued_ = 0;
    levelOver_ = false;
}

const omk::Surface& VulkanRenderer::readback() {
    // IDEMPOTENT, and that is a contract not an optimisation. The software
    // renderer returns its stored framebuffer, so a caller may read it twice
    // and may WRITE to it between the two - which is exactly what the mirror
    // pass does when it composites the reflection in. Re-reading the GPU
    // buffer on every call silently threw that composite away and the frame
    // came back nearly black on Vulkan while software was correct. So a frame
    // is converted once, when `end()` says there is a new one.
    if (fb_.w != w_ || fb_.h != h_) fb_ = omk::Surface(w_, h_, 0);
    if (!dirty_) return fb_;
    dirty_ = false;
    settle();
    // UPSCALED, the picture is read at the OUTPUT size, through the same EASU
    // and RCAS as the present pass's (`raw`: undithered), and quantised below
    // as a frame drawn at that size would be
    const bool up = up_ > 1.0f;
    const int cw = up ? w_ : rw_, ch = up ? h_ : rh_;
    if (readBuf_ == VK_NULL_HANDLE &&
        !makeBuffer(static_cast<VkDeviceSize>(cw) * ch * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    readBuf_, readMem_))
        return fb_;
    if (copyOwed_) {                 // the copy `end()` does not record
        copyOwed_ = false;
        if (up && !runPresentPass(0, picH_ > 0 ? picH_ : h_, true)) return fb_;
        VkCommandBuffer cb = oneShotBegin();
        VkBufferImageCopy cp{};
        cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        cp.imageExtent = {static_cast<uint32_t>(cw), static_cast<uint32_t>(ch), 1};
        vkCmdCopyImageToBuffer(cb, up ? presImg_ : colour_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readBuf_, 1, &cp);
        oneShotEnd(cb);              // submitted and waited: the bytes are there
    }
    void* p = nullptr;
    vkMapMemory(dev_, readMem_, 0, VK_WHOLE_SIZE, 0, &p);
    const auto* src = static_cast<const unsigned char*>(p);
    // 888 -> 565 HERE, in software, and not by asking for a 565 attachment:
    // `PORTING` A3 keeps the one quantisation rule in one place rather than
    // delegating it to a driver whose rounding is its own.
    //
    // ...and the SUPERSAMPLE RESOLVE happens on the 8-bit side, BEFORE that
    // one quantisation: averaging four 565 values would quantise four times
    // and then average the error, which throws away most of the point.
    // `present.frag` does the same resolve on the GPU for a frame nothing is
    // drawn over, so this loop runs only on the CPU path (and for
    // `OMK_VERIFY_GPU_PRESENT`, which compares the two).
    if (ss_ <= 1 && dither_) {
        // Row by row through the channel tables - the same bits as
        // `quantise888Dither` a pixel, without a division and a modulo for
        // every one of them (todo/optimization.md step 4).
        for (int y = 0; y < h_; ++y)
            omk::quantise888DitherRow(src + 4 * static_cast<std::size_t>(y) * static_cast<std::size_t>(w_),
                                      w_, y, fb_.px.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w_));
    } else if (ss_ <= 1) {
        for (int i = 0; i < w_ * h_; ++i)
            fb_.px[static_cast<std::size_t>(i)] =
                omk::rgb565(src[4 * i], src[4 * i + 1], src[4 * i + 2]);
    } else {
        const int n = ss_ * ss_;
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x) {
                int acc[3] = {0, 0, 0};
                for (int sy = 0; sy < ss_; ++sy)
                    for (int sx = 0; sx < ss_; ++sx) {
                        const std::size_t o =
                            (static_cast<std::size_t>(y * ss_ + sy) *
                                 static_cast<std::size_t>(rw_) +
                             static_cast<std::size_t>(x * ss_ + sx)) * 4;
                        acc[0] += src[o]; acc[1] += src[o + 1]; acc[2] += src[o + 2];
                    }
                const int ar = (acc[0] + n / 2) / n, ag = (acc[1] + n / 2) / n,
                          ab = (acc[2] + n / 2) / n;
                fb_.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + x] =
                    dither_ ? omk::quantise888Dither(ar, ag, ab, x, y)
                            : omk::rgb565(static_cast<unsigned char>(ar),
                                          static_cast<unsigned char>(ag),
                                          static_cast<unsigned char>(ab));
            }
    }
    vkUnmapMemory(dev_, readMem_);
    return fb_;
}

void VulkanRenderer::geometryGone(void* self, const omk::Geometry* g) {
    auto* r = static_cast<VulkanRenderer*>(self);
    if (!r->releaseOn_) return;
    bool had = false;
    if (auto it = r->vbo_.find(g); it != r->vbo_.end()) {
        // a set's buffer said once as it goes (the slots' release, `releaseResidency`)
        if (const auto nIt = r->vboN_.find(g); nIt != r->vboN_.end() && nIt->second >= 20000)
            std::printf("vulkan: a geometry of %zu corners gone - its vertex buffer released\n", nIt->second);
        r->deadBufs_.push_back(it->second);
        r->vbo_.erase(it);
        had = true;
    }
    r->vboN_.erase(g);
    r->vboCap_.erase(g);
    r->vboRev_.erase(g);
    had |= r->tie_.erase(g) > 0;
    had |= r->baked_.erase(g) > 0;
    if (had) ++r->released_;
}

VulkanRenderer::~VulkanRenderer() {
    omk::removeGeometryListener(&VulkanRenderer::geometryGone, this);
    if (!dev_) return;
    vkDeviceWaitIdle(dev_); settleIdle();
    killPost();
    killLut();
    killReflection();
    for (auto& bm : deadBufs_) {
        vkDestroyBuffer(dev_, bm.first, nullptr);
        freeDeviceMemory(bm.second);
    }
    deadBufs_.clear();
    for (auto& [g, bm] : vbo_) {
        vkDestroyBuffer(dev_, bm.first, nullptr);
        freeDeviceMemory(bm.second);
    }
    auto killTex = [&](Tex& t) {
        if (t.view) vkDestroyImageView(dev_, t.view, nullptr);
        if (t.img)  vkDestroyImage(dev_, t.img, nullptr);
        if (t.mem)  freeDeviceMemory(t.mem);
        if (t.emView) vkDestroyImageView(dev_, t.emView, nullptr);
        if (t.emImg)  vkDestroyImage(dev_, t.emImg, nullptr);
        if (t.emMem)  freeDeviceMemory(t.emMem);
        if (t.nmView) vkDestroyImageView(dev_, t.nmView, nullptr);
        if (t.nmImg)  vkDestroyImage(dev_, t.nmImg, nullptr);
        if (t.nmMem)  freeDeviceMemory(t.nmMem);
    };
    for (auto& [key, u] : uploaded_) killTex(u.t);   // the slots' handles are theirs
    uploaded_.clear();
    tex_.clear();
    killTex(white_);
    killTex(lightBlack_);
    killTex(normalFlat_);
    if (sampler_) vkDestroySampler(dev_, sampler_, nullptr);
    for (VkSampler old : retiredSamplers_) vkDestroySampler(dev_, old, nullptr);
    if (dpool_) vkDestroyDescriptorPool(dev_, dpool_, nullptr);
    if (tsPool_) vkDestroyQueryPool(dev_, tsPool_, nullptr);
    if (tsPresPool_) vkDestroyQueryPool(dev_, tsPresPool_, nullptr);
    if (levelPool_) vkDestroyQueryPool(dev_, levelPool_, nullptr);
    for (auto& p : pipe_) if (p) vkDestroyPipeline(dev_, p, nullptr);
    for (auto& p : pipeTiled_) if (p) vkDestroyPipeline(dev_, p, nullptr);
    if (pipeCut_) vkDestroyPipeline(dev_, pipeCut_, nullptr);
    if (pipeProbe_) vkDestroyPipeline(dev_, pipeProbe_, nullptr);
    if (pipeTiledCut_) vkDestroyPipeline(dev_, pipeTiledCut_, nullptr);
    for (auto& p : pipe1_) if (p) vkDestroyPipeline(dev_, p, nullptr);
    for (auto& p : pipeTiled1_) if (p) vkDestroyPipeline(dev_, p, nullptr);
    if (reflPass1_) vkDestroyRenderPass(dev_, reflPass1_, nullptr);
    if (reflPass1Keep_) vkDestroyRenderPass(dev_, reflPass1Keep_, nullptr);
    if (plo_) vkDestroyPipelineLayout(dev_, plo_, nullptr);
    if (dsl_) vkDestroyDescriptorSetLayout(dev_, dsl_, nullptr);
    if (dsl2_) vkDestroyDescriptorSetLayout(dev_, dsl2_, nullptr);
    if (poseBuf_) vkDestroyBuffer(dev_, poseBuf_, nullptr);
    if (poseMem_) freeDeviceMemory(poseMem_);
    if (pointsBuf_) vkDestroyBuffer(dev_, pointsBuf_, nullptr);
    if (pointsMem_) freeDeviceMemory(pointsMem_);
    flushUploads();   // (nothing pending outside a pool; its own buffers, if any)
    if (ring_) vkDestroyBuffer(dev_, ring_, nullptr);
    if (ringMem_) freeDeviceMemory(ringMem_);
    if (tilesBuf_) vkDestroyBuffer(dev_, tilesBuf_, nullptr);
    if (tilesMem_) freeDeviceMemory(tilesMem_);
    if (readBuf_) vkDestroyBuffer(dev_, readBuf_, nullptr);
    if (readMem_) freeDeviceMemory(readMem_);
    if (fbuf_) vkDestroyFramebuffer(dev_, fbuf_, nullptr);
    if (pass_) vkDestroyRenderPass(dev_, pass_, nullptr);
    if (reflPass_) vkDestroyRenderPass(dev_, reflPass_, nullptr);
    if (reflPassKeep_) vkDestroyRenderPass(dev_, reflPassKeep_, nullptr);
    if (colourView_) vkDestroyImageView(dev_, colourView_, nullptr);
    if (colour_) vkDestroyImage(dev_, colour_, nullptr);
    if (colourMem_) freeDeviceMemory(colourMem_);
    if (msColourView_) vkDestroyImageView(dev_, msColourView_, nullptr);
    if (msColour_) vkDestroyImage(dev_, msColour_, nullptr);
    if (msColourMem_) freeDeviceMemory(msColourMem_);
    if (depthRead_) vkDestroyImageView(dev_, depthRead_, nullptr);
    if (depthView_) vkDestroyImageView(dev_, depthView_, nullptr);
    if (depth_) vkDestroyImage(dev_, depth_, nullptr);
    if (depthMem_) freeDeviceMemory(depthMem_);
    killUpscale();
    killUnder();
    if (presPipe_) vkDestroyPipeline(dev_, presPipe_, nullptr);
    if (presPlo_) vkDestroyPipelineLayout(dev_, presPlo_, nullptr);
    if (presSampler_) vkDestroySampler(dev_, presSampler_, nullptr);
    if (presPool_) vkDestroyDescriptorPool(dev_, presPool_, nullptr);
    if (presDsl_) vkDestroyDescriptorSetLayout(dev_, presDsl_, nullptr);
    if (presFbuf_) vkDestroyFramebuffer(dev_, presFbuf_, nullptr);
    if (presPass_) vkDestroyRenderPass(dev_, presPass_, nullptr);
    if (presView_) vkDestroyImageView(dev_, presView_, nullptr);
    if (presImg_) vkDestroyImage(dev_, presImg_, nullptr);
    if (presMem_) freeDeviceMemory(presMem_);
    if (presRead_) vkDestroyBuffer(dev_, presRead_, nullptr);
    if (presReadMem_) freeDeviceMemory(presReadMem_);
    if (upBuf_) vkDestroyBuffer(dev_, upBuf_, nullptr);
    if (upMem_) freeDeviceMemory(upMem_);
    if (pipeStencil_) vkDestroyPipeline(dev_, pipeStencil_, nullptr);
    if (pipeDepthReset_) vkDestroyPipeline(dev_, pipeDepthReset_, nullptr);
    for (auto& pp : pipeRefl_) if (pp) vkDestroyPipeline(dev_, pp, nullptr);
    // the mapped shadow and set 1's layout and buffers - older than the
    // remaster, and never destroyed until the audit's cleanliness (2026-10-26);
    // set 1 itself goes with the pool
    if (shPipe_) vkDestroyPipeline(dev_, shPipe_, nullptr);
    if (shPlo_) vkDestroyPipelineLayout(dev_, shPlo_, nullptr);
    if (shFbuf_) vkDestroyFramebuffer(dev_, shFbuf_, nullptr);
    if (shPass_) vkDestroyRenderPass(dev_, shPass_, nullptr);
    if (shSampler_) vkDestroySampler(dev_, shSampler_, nullptr);
    if (shView_) vkDestroyImageView(dev_, shView_, nullptr);
    if (shImg_) vkDestroyImage(dev_, shImg_, nullptr);
    if (shMem_) freeDeviceMemory(shMem_);
    if (dsl1_) vkDestroyDescriptorSetLayout(dev_, dsl1_, nullptr);
    if (shUbo_) vkDestroyBuffer(dev_, shUbo_, nullptr);
    if (shUboMem_) freeDeviceMemory(shUboMem_);   // unmapped by the free
    if (litUbo_) vkDestroyBuffer(dev_, litUbo_, nullptr);
    if (litUboMem_) freeDeviceMemory(litUboMem_);
    if (pfence_) vkDestroyFence(dev_, pfence_, nullptr);
    if (acquired_) vkDestroySemaphore(dev_, acquired_, nullptr);
    for (auto& d : drawn_) if (d) vkDestroySemaphore(dev_, d, nullptr);
    if (swap_) vkDestroySwapchainKHR(dev_, swap_, nullptr);
    if (fence_) vkDestroyFence(dev_, fence_, nullptr);
    if (pool_) vkDestroyCommandPool(dev_, pool_, nullptr);
    vkDestroyDevice(dev_, nullptr);
    if (surface_ && inst_) vkDestroySurfaceKHR(inst_, surface_, nullptr);
    if (inst_) vkDestroyInstance(inst_, nullptr);
}

// THE REMASTER'S HALF (`vkrender_remaster.inl`): the upscale, the post pass,
// the water reflection. Always compiled - this backend is built only with
// OMK_REMASTER (the Makefile drops it otherwise), so no stubs.
#if !OMK_REMASTER
#error "the Vulkan backend is built with OMK_REMASTER only"
#endif
#include "vkrender_remaster.inl"

}  // namespace

namespace omk {

// The factory. `src/` never names this - A8 rule 2 - so a caller that wants the
// live renderer links the backend and calls it, and a caller that does not gets
// a build with no Vulkan in it at all.
Renderer* makeVulkanRenderer() { return new VulkanRenderer(); }

// The presentation seam. `PORTING` A8 rule 2 keeps SDL in one file and Vulkan
// in this one, so these pass opaque handles and extension NAMES rather than
// letting either include the other's headers. The order is forced by Vulkan:
// the instance needs the window system's extensions, the surface needs the
// instance, and the device's queue choice needs the surface.
void vulkanNeedExtensions(Renderer* r, const char* const* names, unsigned n) {
    if (auto* v = dynamic_cast<VulkanRenderer*>(r)) v->needExtensions(names, n);
}
void* vulkanCreateInstance(Renderer* r) {
    auto* v = dynamic_cast<VulkanRenderer*>(r);
    return v ? v->createInstanceOnly() : nullptr;
}
bool vulkanAttachSurface(Renderer* r, unsigned long long surf) {
    auto* v = dynamic_cast<VulkanRenderer*>(r);
    return v && v->attachSurface(surf);
}
bool vulkanPresent(Renderer* r) {
    auto* v = dynamic_cast<VulkanRenderer*>(r);
    return v && v->presentDirect();
}
bool vulkanPresentUnder(Renderer* r, const Surface& ui, const unsigned char* mask,
                        const unsigned char* maskRow, const Surface& under, int filter,
                        std::vector<unsigned char>* readback) {
    auto* v = dynamic_cast<VulkanRenderer*>(r);
    return v && v->presentUnder(ui, mask, maskRow, under, filter, readback);
}
bool vulkanPresentSurface(Renderer* r, const Surface& s) {
    auto* v = dynamic_cast<VulkanRenderer*>(r);
    return v && v->presentSurface(s);
}
// The present pass (todo/optimization.md step 4b).
bool vulkanPresentWorld(Renderer* r, int vy, int vh) {
    auto* v = dynamic_cast<VulkanRenderer*>(r);
    return v && v->presentWorld(vy, vh);
}
// options row 2 on the Vulkan backend (`todo/options-menu.md`)
bool vulkanResize(Renderer* r, int w, int h) {
    auto* v = dynamic_cast<VulkanRenderer*>(r);
    return v && v->resize(w, h);
}
// ...and at new enhancements (OMK's options menu, `retarget`)
bool vulkanRetarget(Renderer* r, int samples, int ss, float up, float ao) {
    auto* v = dynamic_cast<VulkanRenderer*>(r);
    return v && v->retarget(samples, ss, up, ao);
}
bool vulkanWorldPicture(Renderer* r, int vy, int vh, std::vector<unsigned char>& rgba) {
    auto* v = dynamic_cast<VulkanRenderer*>(r);
    return v && v->worldPicture(vy, vh, rgba);
}
bool vulkanProbeUpload(Renderer* r, const unsigned char* rgba, bool dither) {
    auto* v = dynamic_cast<VulkanRenderer*>(r);
    if (!v) return false;
    v->setDitherForProbe(dither);
    return v->probeUpload(rgba);
}
int vulkanSamples(Renderer* r) {
    return static_cast<VulkanRenderer*>(r)->samples();
}
int vulkanMaxSamples(Renderer* r) {
    return static_cast<VulkanRenderer*>(r)->maxSamples();
}
int vulkanTextureFilter(Renderer* r) {
    return static_cast<VulkanRenderer*>(r)->textureFilter();
}
int vulkanAnisotropy(Renderer* r) {
    return static_cast<VulkanRenderer*>(r)->anisotropy();
}
const char* vulkanDeviceName(Renderer* r) {
    auto* v = dynamic_cast<VulkanRenderer*>(r);
    return v ? v->device() : "";
}

}  // namespace omk

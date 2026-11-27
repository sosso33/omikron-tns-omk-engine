// SPDX-License-Identifier: GPL-3.0-or-later
// THE DEKO3D BACKEND - the Nintendo Switch's own GPU API (`todo/deko3d.md`).
//
// Built from the VULKAN backend (`backends/vulkan/vkrender.cpp`), which it
// follows decision for decision - `PORTING` A2's rule is the same here: **a
// backend receives decisions and turns them into API calls; it never makes
// one.** The order is the submission order, the texture the bucket key's
// slot, the blend `omk::blendRule`, the cutout a colour key the shader
// discards on, the back-face cull a push field and a discard on
// `gl_FrontFacing` (the device's own cull stays OFF). It draws with Vulkan's
// SHADERS, translated for `uam` by `dkshaders.py`, and fills the same blocks
// (`backends/vulkan/shaderblocks.h`), so the two cannot disagree about a
// layout.
//
// **D1, first light (todo/deko3d.md):** the original's picture - the scene
// pass, the texture pool, the baked depth tie, `readback`, and the present of
// the world (`present.frag`: the dither and the 565 expansion, on the GPU)
// and of a CPU-composed `Surface`. Not yet: frames in flight (one frame, its
// fence waited for at the next `begin`), the overlay planes, the GPU poses,
// MSAA / supersample / upscale, the shadow map and every remaster pass - each
// answers "no" through the boundary (`takes`, `posesBodies`, ...), so the
// frontend does them the way it does for a backend without them.
//
// What deko3d changes against Vulkan:
//   * NO PIPELINES. The state (rasterizer, colour, blend, depth) is bound in
//     the command list, so a blend change is three small binds.
//   * NO PUSH CONSTANTS: `dkCmdBufPushConstants` writes a UNIFORM BUFFER
//     inline in the command list, ordered with the draws - Vulkan's push
//     block exactly. The frame's light and shadow blocks go the same way,
//     so no CPU-written uniform is ever in flight.
//   * NO SETS: per-stage bindings (`dkshaders.py` flattens `set S binding B`
//     to `S*6+B`), a texture a handle into ONE image-descriptor array.
//   * THE Y AXIS. The device is made with deko3d's defaults - depth 0..1 and
//     an UPPER-LEFT origin - where normalised Y points UP (the examples use
//     `glm::perspectiveRH_ZO` unflipped): Direct3D's convention, not
//     Vulkan's. So `pushView` negates the projection's second row, and the
//     frame lands on the same image rows as Vulkan's; both APIs then call a
//     triangle front when it turns counter-clockwise ON SCREEN, so
//     `kCullSign` is Vulkan's 1. Reasoned, not measured: `OMK_DEKO_CULL_SIGN=-1`
//     flips it on the console if the rooms turn inside out.
#include <deko3d.h>
#include <switch.h>

#include "o3de/renderer.h"
#include "o3de/drawcore.h"
#include "o3de/raster.h"
#include "o3de/geom3do.h"
#include "o3de/texpack.h"
#include "o3de/fsrcon.h"
#include "o3de/colourgrade.h"
#include "formats/ktx2.h"
#include "ui/surface.h"
#include "platform/env.h"
#include "../vulkan/shaderblocks.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace omk::shaderblocks;

// ---- the sizes -------------------------------------------------------------
constexpr uint32_t kDataBlock    = 32u << 20;   // CPU-written, GPU-read: vertices, staging, descriptors
constexpr uint32_t kCachedBlock  = 8u << 20;    // GPU-written, CPU-read: the readback
constexpr uint32_t kImageBlock   = 64u << 20;   // textures and targets
constexpr uint32_t kCodeBlock    = 1u << 20;    // the shaders' code
constexpr uint32_t kCmdBytes     = 4u << 20;    // a frame's command memory, more added on demand
constexpr uint32_t kCmdGrow      = 1u << 20;
constexpr uint32_t kUpCmdBytes   = 1u << 20;    // the texture uploads' command memory
constexpr uint32_t kStagingBytes = 16u << 20;   // the uploads' ring, flushed when full
constexpr uint32_t kMaxImages    = 4096;        // image descriptors
constexpr int      kSwapImages   = 3;
constexpr uint32_t kUboAlign     = DK_UNIFORM_BUF_ALIGNMENT;

// the bindings after `dkshaders.py`'s flattening (set * 6 + binding)
constexpr uint32_t kUboPush = 0, kUboShadow = 6, kUboLights = 8;     // scene.frag sets 1.0 / 1.2
constexpr uint32_t kTexTex = 0, kTexEmis = 1, kTexRefl = 2, kTexShadow = 7;
constexpr uint32_t kSsboPose = 12;                                   // scene.vert set 2.0

// the fixed image descriptors; a pool texture takes the next free one
enum : uint32_t { kImgWhite = 0, kImgBlack = 1, kImgFlat = 2, kImgColour = 3, kImgSurface = 4,
                  kImgOverC = 5, kImgOverM = 6, kImgEasu = 7, kImgPostSrc = 8, kImgGlow0 = 9, kImgGlow1 = 10,
                  kImgLut = 11, kImgShadow = 12, kImgFirst = 16 };
// the samplers: the pool's filter (0 nearest - the original's software
// devices - 1 bilinear, its 3D cards'), and the present's nearest clamp
enum : uint32_t { kSampNearest = 0, kSampLinear = 1, kSampClamp = 2, kSampTrilinear = 3, kSampLinearClamp = 4,
                  kSampCount = 5 };

constexpr uint32_t alignUp(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }

// THE FIRST PRESENTS, step by step (`OMK_DEKO_TRACE=N`, default 3): the first
// console run went black and silent at the first present, so each step says
// itself until the frames are known to flow
int& traceLeft() {
    static int n = omk::envInt("OMK_DEKO_TRACE", 3);
    return n;
}
#define DKTRACE(...)                                                          \
    do {                                                                      \
        if (traceLeft() > 0) { std::printf("deko3d trace: " __VA_ARGS__); std::fflush(stdout); } \
    } while (0)
constexpr int64_t kFenceTimeoutNs = 2000000000;   // a frame's fence: 2 s, then said, not hung

// Which `gl_FrontFacing` is the software rasterizer's FRONT - Vulkan's
// `kCullSign`, the same framebuffer and the same on-screen rule (above).
int32_t cullSign() {
    static const int32_t s = omk::envInt("OMK_DEKO_CULL_SIGN", 1) < 0 ? -1 : 1;
    return s;
}

// ---- THE HEAPS: a few large memory blocks, sub-allocated ------------------
// A deko3d memory block is a kernel mapping (4 KB aligned); a block per
// texture would be thousands of them. First fit over a sorted free list,
// coalesced on free; a request larger than the block size gets a block of
// its own.
struct Alloc {
    DkMemBlock mb = nullptr;
    uint32_t off = 0, size = 0;
    int block = -1;
    explicit operator bool() const { return mb != nullptr; }
    DkGpuAddr gpu() const { return dkMemBlockGetGpuAddr(mb) + off; }
    void* cpu() const { return static_cast<char*>(dkMemBlockGetCpuAddr(mb)) + off; }
};

class Heap {
public:
    void init(DkDevice d, uint32_t flags, uint32_t blockBytes, const char* name) {
        dev_ = d; flags_ = flags; blockBytes_ = blockBytes; name_ = name;
    }
    Alloc alloc(uint32_t size, uint32_t align) {
        if (!size) size = 1;
        align = std::max<uint32_t>(align, 4);
        for (std::size_t b = 0; b < blocks_.size(); ++b)
            if (Alloc a = carve(static_cast<int>(b), size, align)) return a;
        // a new block: the usual size, or the request's own
        uint32_t want = std::max(blockBytes_, alignUp(size + align, DK_MEMBLOCK_ALIGNMENT));
        if (flags_ & DkMemBlockFlags_Code) want += DK_SHADER_CODE_UNUSABLE_SIZE;
        want = alignUp(want, DK_MEMBLOCK_ALIGNMENT);
        DkMemBlockMaker m;
        dkMemBlockMakerDefaults(&m, dev_, want);
        m.flags = flags_;
        DkMemBlock mb = dkMemBlockCreate(&m);
        if (!mb) {
            std::printf("deko3d: the %s heap could not grow by %u KB\n", name_, want >> 10);
            return {};
        }
        Block nb;
        nb.mb = mb;
        nb.size = want;
        // the code heap's last bytes are the prefetcher's: never handed out
        nb.free[0] = (flags_ & DkMemBlockFlags_Code) ? want - DK_SHADER_CODE_UNUSABLE_SIZE : want;
        blocks_.push_back(std::move(nb));
        total_ += want;
        return carve(static_cast<int>(blocks_.size() - 1), size, align);
    }
    void free(Alloc& a) {
        if (!a || a.block < 0 || a.block >= static_cast<int>(blocks_.size())) { a = {}; return; }
        auto& fr = blocks_[static_cast<std::size_t>(a.block)].free;
        uint32_t o = a.off, s = a.size;
        auto next = fr.lower_bound(o);
        if (next != fr.end() && o + s == next->first) { s += next->second; next = fr.erase(next); }
        if (next != fr.begin()) {
            auto prev = std::prev(next);
            if (prev->first + prev->second == o) { prev->second += s; used_ -= a.size; a = {}; return; }
        }
        fr[o] = s;
        used_ -= a.size;
        a = {};
    }
    void destroy() {
        for (auto& b : blocks_) dkMemBlockDestroy(b.mb);
        blocks_.clear();
        used_ = total_ = 0;
    }
    uint64_t used() const { return used_; }
    uint64_t total() const { return total_; }
private:
    struct Block {
        DkMemBlock mb = nullptr;
        uint32_t size = 0;
        std::map<uint32_t, uint32_t> free;   // offset -> length
    };
    Alloc carve(int b, uint32_t size, uint32_t align) {
        Block& bl = blocks_[static_cast<std::size_t>(b)];
        for (auto it = bl.free.begin(); it != bl.free.end(); ++it) {
            const uint32_t o = it->first, s = it->second;
            const uint32_t a = alignUp(o, align);
            if (a < o || a + size > o + s) continue;
            bl.free.erase(it);
            if (a > o) bl.free[o] = a - o;
            if (a + size < o + s) bl.free[a + size] = o + s - (a + size);
            used_ += size;
            Alloc r;
            r.mb = bl.mb; r.off = a; r.size = size; r.block = b;
            return r;
        }
        return {};
    }
    DkDevice dev_ = nullptr;
    uint32_t flags_ = 0, blockBytes_ = 0;
    const char* name_ = "";
    std::vector<Block> blocks_;
    uint64_t used_ = 0, total_ = 0;
};

void debugOut(void*, const char* context, DkResult result, const char* message) {
    std::printf("deko3d: %s - result %d: %s\n", context ? context : "?", static_cast<int>(result),
                message ? message : "");
}

// a module from the NRO's romfs (`romfs:/shaders/<name>.dksh`, built by the
// Switch's CMake from `dkshaders.py` + `uam`)
struct DkshHeader {
    uint32_t magic, header_sz, control_sz, code_sz, programs_off, num_programs;
};

class DekoRenderer : public omk::Renderer {
public:
    DekoRenderer() { omk::addGeometryListener(&DekoRenderer::geometryGone, this); }
    ~DekoRenderer() override;

    bool init(int w, int h) override;
    void setTextures(std::span<const omk::Texture> t) override { poolTextures(t); }
    void begin(const omk::View& v) override;
    void submit(const omk::Draw& d) override;
    void end() override;
    // THE MAPPED SHADOW (D4d, Vulkan's `shadowPass`): the characters' depth
    // from the light into a 1024x1024 map, before the frame's own pass
    void shadowPass(const omk::View& v, std::span<const omk::Draw> casters) override;
    bool drawsShadowMap() const override { return true; }
    const omk::Surface& readback() override;
    omk::RasterStats stats() const override { return st_; }
    const char* name() const override { return "deko3d"; }
    // THE REMASTER (`todo/deko3d.md` D4a): the pack, the per-pixel and the
    // emitters' lights, the mist, the wet floors and the 3D rain (`setWater`),
    // the body light - what the scene shader computes from the frame's blocks
    // and each draw's fields. Not yet: the upscale, the bloom and grade, the
    // occlusion, the reflections, the relief (D4b-e) - their setters answer no.
    unsigned takes() const override {
        return omk::kTakesTextureFilter | omk::kTakesTrilinear | omk::kTakesPixelLights | omk::kTakesWater |
               omk::kTakesBodyTint | omk::kTakesEmitterLights | omk::kTakesMist | omk::kTakesUpscale |
               omk::kTakesBloom | omk::kTakesGrade | omk::kTakesAnisotropy | omk::kTakesShadowMap;
    }
    bool setTextureFilter(int mode) override {
        if (dev_) return false;   // the samplers exist - `retexture` moves it later
        filter_ = mode < 1 ? 0 : mode < 2 ? 1 : 2;
        return true;
    }
    // the mode switch (original <-> remaster) and OMK's menu: the filter
    // moved, every texture dropped - the frame hands the pool over again,
    // from the pack or as shipped (Vulkan's `retexture`)
    bool retexture(int filter, int aniso) override {
        filter_ = filter < 1 ? 0 : filter < 2 ? 1 : 2;
        aniso_ = aniso < 1 ? 1 : aniso > 16 ? 16 : aniso;
        if (!dev_) return true;
        poolTextures({});
        // the samplers rewritten with no frame in flight reading them
        dkQueueWaitIdle(queue_);
        markIdle();
        writeSamplers();
        std::printf("deko3d: retextured - %s, anisotropy %dx; every texture dropped, uploaded again\n",
                    filter_ == 0 ? "nearest" : filter_ == 1 ? "bilinear" : "trilinear", anisotropy());
        return true;
    }
    // THE ANISOTROPY (D4d, GLES's and Vulkan's rule): asked before `init`, and
    // only behind trilinear - the sampler's, Maxwell's own up to 16x
    bool setAnisotropy(int n) override {
        if (dev_) return false;
        aniso_ = n < 1 ? 1 : n > 16 ? 16 : n;
        return true;
    }
    int anisotropy() const { return filter_ >= 2 ? aniso_ : 1; }
    bool takesPack() const override { return true; }
    // THE BLOOM and THE GRADE (D4c, Vulkan's post-pass - `post.frag`): any time,
    // no target to remake; the glow only where a draw bound a light map
    bool setBloom(float s) override { bloom_ = s < 0.0f ? 0.0f : s > 4.0f ? 4.0f : s; return true; }
    bool setGrade(const float* rgb, int n) override {
        if (!rgb || n < 2) { lutN_ = 0; return true; }
        omk::lutStrip(rgb, n, lutData_);
        lutN_ = n;
        lutDirty_ = true;
        return true;
    }
    // THE UPSCALE (D4b): before `init` - the targets are made for it
    bool setUpscale(float f) override {
        if (dev_) return false;
        up_ = f > 1.0f ? std::min(f, 3.0f) : 1.0f;
        return true;
    }
    bool drawsPixelLights() const override { return true; }
    int maxPixelLights() const override { return omk::View::kMaxGpuLights; }
    bool setEmitterLights(bool) override { return true; }   // the points ride in the frame's light block
    bool drawsEmitterLights() const override { return true; }
    bool setBodyTint(bool) override { return true; }       // the pose block carries it
    bool drawsBodyTint() const override { return true; }
    bool setWater(float) override { return true; }         // the wet floors, the mist, the 3D rain: the shader's
    uint32_t sampler() const { return filter_ >= 2 ? kSampTrilinear : filter_ ? kSampLinear : kSampNearest; }
    bool bakeDepthTie(const omk::Geometry* g, std::span<const omk::Draw> order) override;
    // BODIES POSED BY THE RENDERER (`renderer.h` `Draw::meshPose`), as Vulkan,
    // GLES, GL1 and the 3DS: a posed draw's REST corners - static, uploaded
    // once - and its block in the slot's pose buffer, which `scene.vert`
    // places, turns and lights each corner by. A body that will not fit the
    // buffer is posed on the CPU (`cpuPose`), so no path draws it wrong.
    bool posesBodies() const override { return slots_[0].pose.mb != nullptr; }
    int maxVertexLights() const override { return posesBodies() ? kPoseLights : 0; }
    // THE GPU'S OWN TIME for the last frame (`renderer.h`), from four
    // timestamps (`stamp`): the scene's list and the present's, the queue's
    // wait for a swapchain image between them left out - as GLES's timer
    // ends before the swap
    double gpuFrameMs() const override { return gpuMs_; }

    bool presentWorld(int vy, int vh);
    bool presentSurface(const omk::Surface& s);
    // THE INTERFACE AS AN OVERLAY over the GPU's world (`overlay.frag`,
    // GLES's `presentOverlay`): `s` the CPU's frame with the world's pixels
    // left as the key, `mask` the planes' M (`resolveOverlayPlanes`), `fade`
    // the colour fade (rgb, weight). False: not taken (the frame composed on
    // the CPU instead).
    bool presentOverlay(const omk::Surface& s, const unsigned char* mask, const float fade[4], int vy, int vh);
    const char* device() const { return "Tegra X1 (deko3d)"; }
    void setSwapInterval(int n) { if (swap_) dkSwapchainSetSwapInterval(swap_, n < 0 ? 0 : static_cast<uint32_t>(n)); }

private:
    static void geometryGone(void* self, const omk::Geometry* g);
    static void addCmdMem(void* user, DkCmdBuf cb, size_t minReq);
    bool loadShader(const char* name, DkShader& sh);
    bool makeImage(DkImage& img, Alloc& mem, DkImageFormat fmt, int w, int h, uint32_t flags, uint32_t levels = 1);
    void writeDescriptor(uint32_t slot, const DkImage& img);
    uint32_t takeSlot();
    void waitFrame();
    void openCb();
    void bindFrameState(const DkImageView* target);
    // THE POST-PASS (D4c): the scene drawn into `postSrc_` when the frame has
    // a glow or a grade, then the glow's passes at a quarter of the size and
    // the final one into `colour_` - Vulkan's `recordPost`, without the
    // occlusion (GLES's Switch draws none)
    bool makePost();
    bool uploadLut();
    void recordPost();
    void postPass(const DkImageView* rt, int w, int h, uint32_t src, uint32_t glow, int srcW, int srcH, float dx,
                  float dy, int mode, float strength, float halo);
    float bloom_ = 0.0f, halo_ = 0.0f;
    int lutN_ = 0;
    bool lutDirty_ = false;
    std::vector<unsigned char> lutData_;
    bool postNow_ = false, lightMapNow_ = false, poolLit_ = false, postMade_ = false;
    DkImage postSrc_{}, glow_[2]{}, lut_{};
    Alloc postSrcMem_, glowMem_[2], lutMem_;
    DkImageView postSrcView_{}, glowView_[2]{};
    int gw_ = 0, gh_ = 0, lutMadeN_ = 0;
    DkShader shPost_{};
    // the mapped shadow: its depth map, its vertex module, this frame's block
    static constexpr int kShadowSide = 1024;   // Vulkan's
    DkImage shadow_{};
    Alloc shadowMem_;
    DkImageView shadowView_{};
    DkShader shShadow_{};
    bool shadowMade_ = false, shadowLive_ = false;
    float shadowMvp_[16]{};
    float shadowStrength_ = 0.0f;
    uint32_t boundEmis_ = kImgBlack;
    void pushView(const omk::View& v);
    // the buffer this frame draws `g` from, its contents current - or nullptr
    const Alloc* uploadGeometry(const omk::Geometry* g);
    void writeCorners(const omk::Geometry& g, Alloc& mem, const std::vector<std::uint32_t>* only);
    void poolTextures(std::span<const omk::Texture> t);
    bool stage(uint32_t bytes, Alloc& out);
    void flushUploads();
    // `over`: the overlay's fade - its planes already staged in the slot - to
    // lay over the present's picture; nullptr for none
    bool runPresent(uint32_t imgSlot, int vy, int vh, bool raw, const float* over = nullptr);
    void recordPresentDraw(const DkImageView* rt, uint32_t imgSlot, int vy, int vh, bool raw);
    static bool amdFsr() { static const bool a = omk::envOn("OMK_DEKO_FSR_AMD"); return a; }
    // a picture `v` output pixels long, in RENDER pixels (Vulkan's `rs`): the
    // upscale draws the frame `up_` times smaller
    int rs(int v) const {
        return up_ > 1.0f ? std::max(1, static_cast<int>(std::lround(static_cast<float>(v) / up_))) : v;
    }
    float up_ = 1.0f;
    DkShader shEasu_{}, shEasuFetch_{};   // AMD's EASU gathered, and with its texels fetched
    // THE LITE PAIR (the default): GLES's float transcriptions - EASU
    // (`easu_lite.frag`) and the present with RCAS (`present.frag`
    // OMK_RCAS_LITE); `OMK_DEKO_FSR_AMD=1` takes AMD's header through `uam`
    DkShader shEasuLite_{}, shPresentLite_{};
    DkImage easu_{}, readImg_{};   // EASU's output, and the upscaled readback's picture
    Alloc easuMem_, readImgMem_;
    DkImageView easuView_{}, readView_{};
    void stamp(int k);
    void readStamps();
    bool writePose(const omk::Draw& d, uint32_t& off);
    omk::Draw cpuPose(const omk::Draw& d);
    void submitOne(const omk::Draw& d, bool posed, uint32_t poseOff);
    // Vulkan's `kPoseLights` / `kPoseMaxMeshes` (`scene.vert`'s kLights must say the same)
    static constexpr int kPoseLights = 32, kPoseMaxMeshes = 256;
    // 2 MB a slot: Anekbah's busiest street frame writes ~288 KB (Vulkan's note)
    static constexpr uint32_t kPoseBytes = 2u << 20;
    static constexpr uint32_t kPoseRange = (1 + 2 * kPoseLights + 3 * kPoseMaxMeshes + 1) * 16;
    static constexpr uint32_t kPoseAlign = 256;
    std::map<const omk::Geometry*, omk::Geometry> cpuPosed_;   // `cpuPose`'s corners
    std::uint64_t cpuPoseRev_ = 0;
    DkGpuAddr boundPose_ = 0;   // the storage buffer bound at binding 12 now
    // THE LADDER (`OMK_DEKO_LADDER=1`, an instrument): a timestamp at each
    // rung of a frame - 0 the list opened, 1 the frame's state and blocks,
    // 2 the clears, 3 the scene sent, 4 the present's list begun, 5 its
    // barrier, 6 its draw - and each span's mean said every 60 frames
    void rung(int k);
public:
    std::string takeLadder();
private:
    bool ladder_ = false;
    double rungSum_[8] = {};
    double easuSum_ = 0.0;   // rung 5 -> 7, EASU's alone (a frame with the upscale)
    long easuFrames_ = 0;
    long rungFrames_ = 0;

    GpuVert vertOf(const omk::Geometry& g, std::size_t i, const std::vector<std::uint8_t>* lose) const {
        const auto& c = g.corners[i];
        const float mesh = g.cornerMesh.size() == g.corners.size() ? static_cast<float>(g.cornerMesh[i]) : -1.0f;
        GpuVert v{c.x, c.y, c.z, c.u, c.v, c.r, c.g, c.b, c.nx, c.ny, c.nz, c.phase, mesh};
        if (lose && i < lose->size() && (*lose)[i]) v.phase -= kTieLoserPhase;
        return v;
    }

    // ---- the device
    DkDevice dev_ = nullptr;
    DkQueue queue_ = nullptr;
    DkCmdBuf cb_ = nullptr, upCb_ = nullptr;
    Heap data_, cached_, images_, code_;
    Alloc upCmdMem_;
    // THE FRAMES IN FLIGHT (`todo/deko3d.md` D2): a frame works in a SLOT -
    // its command memory and what that outgrew into, its fence, its
    // timestamps, its surface's staging, the buffers retired while it was
    // recorded - and waits only for THAT slot's last frame (`openCb`), so the
    // CPU records frame N+1 while the GPU draws N. `OMK_DEKO_SLOTS` 1..3,
    // default 2; 1 is the one-frame-at-a-time of first light.
    static constexpr int kMaxSlots = 3;
    struct Slot {
        Alloc cmdMem;
        std::vector<Alloc> extra;   // what the frame's list outgrew into
        DkFence fence{};
        bool pending = false;       // its last frame's fence not waited for yet
        std::vector<Alloc> dead;    // retired while it was recorded: freed when it comes round
        Alloc stamps;               // the GPU timer's and the ladder's reports
        unsigned stamped = 0, rungs = 0;
        Alloc surfStage;
        Alloc overStage;            // the overlay's two planes: C (16 bits a pixel), then M
        // THE POSE BUFFER (`scene.vert` set 2 -> storage buffer 12): the
        // frame's posed bodies, one block each, written from the start at
        // every `begin`; the slot's own, as the CPU writes it each frame
        Alloc pose;
        uint32_t poseUsed = 0;
        std::map<std::pair<const float*, const float*>, uint32_t> poseCache;   // a body's block this frame
    };
    Slot slots_[kMaxSlots];
    int nSlots_ = 2, slot_ = 0;
    Slot& cur() { return slots_[slot_]; }
    void markIdle() { for (Slot& sl : slots_) sl.pending = false; }
    bool cbOpen_ = false;           // `cb_` holds this frame's commands
    bool recording_ = false;        // between `begin` and `end`
    bool sceneSubmitted_ = false;   // `end` sent a scene that no present has closed
    bool dirty_ = false;            // a scene ended that `readback` has not read

    // ---- the shaders (Vulkan's, `dkshaders.py`)
    DkShader shVert_{}, shFrag_{}, shFragCut_{}, shFsq_{}, shPresent_{}, shFsqFrag_{};
    // THE ORIGINAL'S MODULES (`scene.frag` -DOMK_ORIGINAL, and its cutout's):
    // the game's own shading alone, for a draw that asks nothing of the
    // remaster in a frame that asks nothing of it (`frameOrig_`, `submit`) -
    // the full shader's 49 registers were ~9.5 ms of the street's 13.3
    DkShader shOrig_{}, shOrigCut_{};
    DkShader shOverlay_{};
    DkImage overC_{}, overM_{};   // the overlay's planes: C as R16_UINT words, M as R8
    Alloc overCMem_, overMMem_;
    long overlays_ = 0;
    bool frameOrig_ = true;
    // `OMK_DEKO_PROBE=N`, an instrument read at each `begin` (so `--bench`'s
    // OMK_BENCH_VARY steps through it): what the scene's GPU time is made of,
    // one part taken away at a time - 1 a trivial fragment shader (fsq.frag),
    // 2 no switch to the cutout's shader, 3 a 1x1 scissor (no pixel drawn),
    // 4 one push a draw, not one a run of the cull, 5 each run's block written
    // in a RING of uniform buffers and bound there - no `pushConstants`, 6 no
    // draw at all (the frame's fixed cost), 7 probe 3 without the per-draw
    // texture bind and state changes. 0 the frame as it is.
    int probe_ = 0;
    Alloc pushRing_;                  // probe 5's ring, one 256-byte slot a run
    uint32_t pushRingAt_ = 0;
    static constexpr uint32_t kPushRingSlots = 8192;
    // what a frame issued, summed for `dekoTakeCounts` (the 60-frame report)
    long draws_ = 0, runs_ = 0, verts_ = 0, frames_ = 0, origDraws_ = 0, posedDraws_ = 0;
    // ...and what the CPU spent in `submit`: the vertex uploads (how many,
    // their bytes, their ms) and the whole of it
    long uploads_ = 0, upBytes_ = 0;
    double upMs_ = 0.0, submitMs_ = 0.0, waitMs_ = 0.0;   // waitMs_: `waitFrame`, the slot's fence
public:
    void takeCounts(long out[6]) {
        out[0] = draws_; out[1] = runs_; out[2] = verts_; out[3] = frames_; out[4] = origDraws_; out[5] = posedDraws_;
        draws_ = runs_ = verts_ = frames_ = origDraws_ = posedDraws_ = 0;
    }
    void takeCpu(double out[6]) {
        out[0] = static_cast<double>(uploads_); out[1] = static_cast<double>(upBytes_);
        out[2] = upMs_; out[3] = submitMs_; out[4] = waitMs_; out[5] = static_cast<double>(overlays_);
        uploads_ = upBytes_ = overlays_ = 0;
        upMs_ = submitMs_ = waitMs_ = 0.0;
    }
private:
    std::vector<Alloc> shaderMem_;

    // ---- the descriptors
    Alloc imgDesc_, sampDesc_;
    std::vector<uint32_t> freeSlots_;
    uint32_t nextSlot_ = kImgFirst;
    int filter_ = 0;
    int aniso_ = 1;   // asked; 1 is off
    void writeSamplers();

    // ---- the targets
    int w_ = 0, h_ = 0;
    DkImage colour_{}, depth_{}, surfImg_{};
    Alloc colourMem_, depthMem_, surfMem_;
    DkImageView colourView_{}, depthView_{};
    DkImage swapImg_[kSwapImages]{};
    Alloc swapMem_[kSwapImages];
    DkImageView swapView_[kSwapImages]{};
    DkSwapchain swap_ = nullptr;

    // ---- the frame's blocks
    Alloc pushUbo_, shadowUbo_, lightUbo_, poseBuf_, readBuf_;
    // the GPU timer: four reports of 16 bytes (a value, then the timestamp)
    // - 0 the frame's list opened, 1 the scene sent, 2 the present's list
    // begun, 3 the frame done; `stamped_` which were written this frame
    double gpuMs_ = -1.0;
    Push push_{};
    bool fog_ = false, mistOn_ = false;
    float fogStart_ = 0, fogEnd_ = 0, fogColour_[3] = {0, 0, 0};
    float rain_ = 0;
    bool dither_ = true;
    bool flipX_ = false;
    int litCount_ = 0;
    int picH_ = 0;
    int lastBlend_ = -1;
    const DkShader* lastFrag_ = nullptr;
    omk::RasterStats st_;
    omk::Surface fb_{1, 1, 0};

    // ---- the stand-ins and the pool
    DkImage white_{}, black_{}, flat_{};
    Alloc whiteMem_, blackMem_, flatMem_;
    struct DTex {
        DkImage img{};
        Alloc mem;
        uint32_t slot = kImgWhite;
        // THE PACK's replacement (`o3de/texpack.h`): `su` x `sv` the
        // original's size, the UVs still in the original's texels (`texScaled`)
        bool packed = false;
        float su = 1.0f, sv = 1.0f;
        // its LIGHT MAP (the pack's `_e`, the bloom's): one channel, read `.r`
        DkImage emImg{};
        Alloc emMem;
        uint32_t emSlot = kImgBlack;
    };
    std::vector<const DTex*> texOf_;   // the pool's index -> its texture (nullptr: none)
    bool uploadBlocks(const omk::PackBlocks& z, DTex& out);   // a pack KTX2's levels, as they are
    bool uploadLightMap(const omk::PackImage& rep, DTex& t);  // its `_e`: BC4 blocks, or R8 bytes
    struct Uploaded {
        omk::PixelBuffer idx, pal;   // the storage held, so its address cannot come back as another texture's
        bool used = false;
        std::unique_ptr<DTex> t;     // a DkImageView points at its DkImage: it must not move
    };
    using Key = std::tuple<const std::uint8_t*, const std::uint8_t*, int, int>;
    std::map<Key, Uploaded> uploaded_;
    std::vector<uint32_t> texSlot_;   // the pool's index -> its image descriptor
    Alloc ring_;
    uint32_t ringAt_ = 0;
    bool upOpen_ = false;
    std::vector<Alloc> upOwn_;        // staging past the ring's size, freed after the flush

    // ---- the geometry
    // A GEOMETRY'S VERTICES ON THE GPU. A static one has ONE buffer, every
    // frame's. One whose contents change (a set's moving meshes, a body posed
    // on the CPU) has one COPY A SLOT: a frame writes its own slot's copy
    // alone, which no frame in flight reads (its last frame was waited for),
    // and writes only the corners changed since that copy was last brought up
    // to date - each copy keeps the union of the `dirtyCorners` it missed
    // (`pending`), or `whole` when a revision came without them. The first
    // console reading: Anekbah's whole 7.5 MB re-sent each frame for its
    // moving meshes, 8.6 of a 9 ms submit (GLES sends ~72 KB - the same rule).
    struct Vbo {
        Alloc mem[kMaxSlots];
        int copies = 0;                       // 1 (static, or one slot) or nSlots_
        std::size_t n = 0;
        std::uint64_t seen = 0;               // the latest revision observed
        bool whole[kMaxSlots] = {};
        std::vector<std::uint32_t> pending[kMaxSlots];
    };
    std::map<const omk::Geometry*, Vbo> vbo_;
    std::map<const omk::Geometry*, std::vector<std::uint8_t>> baked_;
};

// ---------------------------------------------------------------------------

void DekoRenderer::addCmdMem(void* user, DkCmdBuf cb, size_t minReq) {
    auto* self = static_cast<DekoRenderer*>(user);
    Alloc a = self->data_.alloc(std::max<uint32_t>(kCmdGrow, alignUp(static_cast<uint32_t>(minReq), 0x1000)),
                                DK_CMDMEM_ALIGNMENT);
    if (!a) return;   // the device reports the overflow itself
    dkCmdBufAddMemory(cb, a.mb, a.off, a.size);
    self->cur().extra.push_back(a);
}

void DekoRenderer::writeSamplers() {
    auto* sd = static_cast<DkSamplerDescriptor*>(sampDesc_.cpu());
    DkSampler s;
    dkSamplerDefaults(&s);   // nearest, REPEAT - `raster.cpp`'s `% t.width`
    dkSamplerDescriptorInitialize(&sd[kSampNearest], &s);
    s.minFilter = s.magFilter = DkFilter_Linear;
    dkSamplerDescriptorInitialize(&sd[kSampLinear], &s);
    // TRILINEAR (the enhancement, `setTextureFilter(2)`): the pack's block
    // files carry their own mip chain; a texture with one level samples
    // as bilinear through it. Its ANISOTROPY, when asked (`anisotropy()`)
    s.mipFilter = DkMipFilter_Linear;
    s.maxAnisotropy = static_cast<float>(anisotropy());
    dkSamplerDescriptorInitialize(&sd[kSampTrilinear], &s);
    dkSamplerDefaults(&s);
    s.wrapMode[0] = s.wrapMode[1] = s.wrapMode[2] = DkWrapMode_ClampToEdge;
    dkSamplerDescriptorInitialize(&sd[kSampClamp], &s);
    s.minFilter = s.magFilter = DkFilter_Linear;   // EASU's gather
    dkSamplerDescriptorInitialize(&sd[kSampLinearClamp], &s);
}

bool DekoRenderer::loadShader(const char* name, DkShader& sh) {
    const std::string path = std::string("romfs:/shaders/") + name + ".dksh";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { std::printf("deko3d: no shader %s\n", path.c_str()); return false; }
    std::vector<unsigned char> file;
    unsigned char buf[16384];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) file.insert(file.end(), buf, buf + n);
    std::fclose(f);
    DkshHeader hdr{};
    if (file.size() < sizeof hdr) { std::printf("deko3d: %s is short\n", path.c_str()); return false; }
    std::memcpy(&hdr, file.data(), sizeof hdr);
    if (hdr.control_sz + hdr.code_sz > file.size()) {
        std::printf("deko3d: %s - control %u + code %u past the file's %zu bytes\n", path.c_str(),
                    hdr.control_sz, hdr.code_sz, file.size());
        return false;
    }
    Alloc code = code_.alloc(hdr.code_sz, DK_SHADER_CODE_ALIGNMENT);
    if (!code) return false;
    std::memcpy(code.cpu(), file.data() + hdr.control_sz, hdr.code_sz);
    DkShaderMaker sm;
    dkShaderMakerDefaults(&sm, code.mb, code.off);
    sm.control = file.data();
    sm.programId = 0;
    dkShaderInitialize(&sh, &sm);
    shaderMem_.push_back(code);
    if (!dkShaderIsValid(&sh)) { std::printf("deko3d: %s did not initialise\n", path.c_str()); return false; }
    return true;
}

bool DekoRenderer::makeImage(DkImage& img, Alloc& mem, DkImageFormat fmt, int w, int h, uint32_t flags,
                             uint32_t levels) {
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, dev_);
    lm.flags = flags;
    lm.format = fmt;
    lm.dimensions[0] = static_cast<uint32_t>(w);
    lm.dimensions[1] = static_cast<uint32_t>(h);
    lm.mipLevels = levels;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &lm);
    mem = images_.alloc(static_cast<uint32_t>(dkImageLayoutGetSize(&layout)), dkImageLayoutGetAlignment(&layout));
    if (!mem) return false;
    dkImageInitialize(&img, &layout, mem.mb, mem.off);
    return true;
}

void DekoRenderer::writeDescriptor(uint32_t slot, const DkImage& img) {
    DkImageView view;
    dkImageViewDefaults(&view, &img);
    auto* d = static_cast<DkImageDescriptor*>(imgDesc_.cpu());
    dkImageDescriptorInitialize(&d[slot], &view, false, false);
}

uint32_t DekoRenderer::takeSlot() {
    if (!freeSlots_.empty()) { const uint32_t s = freeSlots_.back(); freeSlots_.pop_back(); return s; }
    if (nextSlot_ < kMaxImages) return nextSlot_++;
    static bool told = false;
    if (!told) { told = true; std::printf("deko3d: the %u image descriptors are all taken\n", kMaxImages); }
    return kImgWhite;
}

bool DekoRenderer::init(int w, int h) {
    if (dev_) return w == w_ && h == h_;
    // the swapchain's images go to the compositor, which takes up to 1920x1080
    if (w < 1 || h < 1 || w > 1920 || h > 1080) {
        std::printf("deko3d: a %dx%d frame is past the console's 1920x1080\n", w, h);
        return false;
    }
    w_ = w; h_ = h;
    DkDeviceMaker dm;
    dkDeviceMakerDefaults(&dm);   // depth 0..1, upper-left origin (see the header)
    dm.cbDebug = debugOut;
    dev_ = dkDeviceCreate(&dm);
    if (!dev_) return false;
    DkQueueMaker qm;
    dkQueueMakerDefaults(&qm, dev_);
    qm.flags = DkQueueFlags_Graphics;
    queue_ = dkQueueCreate(&qm);
    if (!queue_) return false;

    data_.init(dev_, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, kDataBlock, "data");
    cached_.init(dev_, DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuCached, kCachedBlock, "readback");
    images_.init(dev_, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, kImageBlock, "image");
    code_.init(dev_, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, kCodeBlock, "code");

    DkCmdBufMaker cm;
    dkCmdBufMakerDefaults(&cm, dev_);
    cm.userData = this;
    cm.cbAddMem = addCmdMem;
    cb_ = dkCmdBufCreate(&cm);
    DkCmdBufMaker um;
    dkCmdBufMakerDefaults(&um, dev_);
    upCb_ = dkCmdBufCreate(&um);
    nSlots_ = static_cast<int>(std::clamp<long>(omk::envInt("OMK_DEKO_SLOTS", 2), 1, kMaxSlots));
    for (int k = 0; k < nSlots_; ++k) {
        slots_[k].cmdMem = data_.alloc(kCmdBytes, DK_CMDMEM_ALIGNMENT);
        if (!slots_[k].cmdMem) return false;
    }
    upCmdMem_ = data_.alloc(kUpCmdBytes, DK_CMDMEM_ALIGNMENT);
    ring_ = data_.alloc(kStagingBytes, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
    if (!cb_ || !upCb_ || !upCmdMem_ || !ring_) return false;

    if (!loadShader("scene.vert", shVert_) || !loadShader("scene.frag", shFrag_) ||
        !loadShader("scene_cut.frag", shFragCut_) || !loadShader("fsq.vert", shFsq_) ||
        !loadShader("present.frag", shPresent_) || !loadShader("fsq.frag", shFsqFrag_) ||
        !loadShader("scene_orig.frag", shOrig_) || !loadShader("scene_orig_cut.frag", shOrigCut_) ||
        !loadShader("overlay.frag", shOverlay_) || !loadShader("easu.frag", shEasu_) ||
        !loadShader("easu_nogather.frag", shEasuFetch_) || !loadShader("easu_lite.frag", shEasuLite_) ||
        !loadShader("present_lite.frag", shPresentLite_) || !loadShader("post.frag", shPost_) ||
        !loadShader("shadow.vert", shShadow_))
        return false;

    // the descriptors: one array of images, one of samplers
    imgDesc_ = data_.alloc(kMaxImages * sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT);
    sampDesc_ = data_.alloc(kSampCount * sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT);
    if (!imgDesc_ || !sampDesc_) return false;
    std::memset(imgDesc_.cpu(), 0, imgDesc_.size);
    writeSamplers();
    if (aniso_ > 1)
        std::printf(filter_ >= 2 ? "deko3d: anisotropy %dx\n" : "deko3d: anisotropy %d ignored - it needs "
                    "trilinear filtering (a mip chain)\n", aniso_);

    // the targets: the scene's colour (read by the present pass and the
    // readback's copy) and depth-stencil, the uploaded surface, the swapchain
    // (no `Usage2DEngine`: the readback's copy is the copy engine's, and works
    // without it - checked on the console. Suspected of the upscale's cost,
    // it was not: the present 9.92 ms with or without the flag)
    if (!makeImage(colour_, colourMem_, DkImageFormat_RGBA8_Unorm, w, h, DkImageFlags_UsageRender) ||
        !makeImage(depth_, depthMem_, DkImageFormat_Z24S8, w, h,
                   DkImageFlags_UsageRender | DkImageFlags_HwCompression) ||
        !makeImage(surfImg_, surfMem_, DkImageFormat_RGBA8_Unorm, w, h, DkImageFlags_Usage2DEngine))
        return false;
    dkImageViewDefaults(&colourView_, &colour_);
    dkImageViewDefaults(&depthView_, &depth_);
    writeDescriptor(kImgColour, colour_);
    writeDescriptor(kImgSurface, surfImg_);
    if (!makeImage(overC_, overCMem_, DkImageFormat_R16_Uint, w, h, DkImageFlags_Usage2DEngine) ||
        !makeImage(overM_, overMMem_, DkImageFormat_R8_Unorm, w, h, DkImageFlags_Usage2DEngine))
        return false;
    writeDescriptor(kImgOverC, overC_);
    writeDescriptor(kImgOverM, overM_);
    if (up_ > 1.0f) {
        if (!makeImage(easu_, easuMem_, DkImageFormat_RGBA8_Unorm, w, h, DkImageFlags_UsageRender) ||
            !makeImage(readImg_, readImgMem_, DkImageFormat_RGBA8_Unorm, w, h,
                       DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine))
            return false;
        dkImageViewDefaults(&easuView_, &easu_);
        dkImageViewDefaults(&readView_, &readImg_);
        writeDescriptor(kImgEasu, easu_);
        std::printf("deko3d: the upscale x%.2f - the scene drawn at %dx%d, EASU and RCAS to %dx%d\n",
                    static_cast<double>(up_), rs(w), rs(h), w, h);
    }
    const DkImage* swapPtrs[kSwapImages];
    for (int i = 0; i < kSwapImages; ++i) {
        if (!makeImage(swapImg_[i], swapMem_[i], DkImageFormat_RGBA8_Unorm, w, h,
                       DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression))
            return false;
        dkImageViewDefaults(&swapView_[i], &swapImg_[i]);
        swapPtrs[i] = &swapImg_[i];
    }
    DkSwapchainMaker sm;
    dkSwapchainMakerDefaults(&sm, dev_, nwindowGetDefault(), swapPtrs, kSwapImages);
    swap_ = dkSwapchainCreate(&sm);
    if (!swap_) return false;
    // v-sync, but not under the performance sweep: `OMK_PRESENT_IMMEDIATE`
    // (the GLES window's rule, `playgpu_gles.cpp`)
    dkSwapchainSetSwapInterval(swap_, omk::envFlag("OMK_PRESENT_IMMEDIATE") ? 0 : 1);

    // the frame's blocks: written through the command list (`pushConstants`)
    pushUbo_ = data_.alloc(alignUp(sizeof(Push) > sizeof(PresentPush) ? sizeof(Push) : sizeof(PresentPush), kUboAlign),
                           kUboAlign);
    shadowUbo_ = data_.alloc(alignUp(sizeof(ShadowUbo), kUboAlign), kUboAlign);
    lightUbo_ = data_.alloc(alignUp(sizeof(LightUbo), kUboAlign), kUboAlign);
    poseBuf_ = data_.alloc(256, kUboAlign);   // `scene.vert` declares it; a posed draw never comes (D3)
    readBuf_ = cached_.alloc(static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 4, 256);
    for (int k = 0; k < nSlots_; ++k) {
        slots_[k].surfStage = data_.alloc(static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 4,
                                          DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
        slots_[k].stamps = data_.alloc(16 * 16, 16);   // the timer's four, then the ladder's (`rung`)
        slots_[k].overStage = data_.alloc(static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 3,
                                          DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
        if (!slots_[k].overStage) return false;
        slots_[k].pose = data_.alloc(kPoseBytes, kPoseAlign);   // none: the bodies posed on the CPU
        if (!slots_[k].surfStage || !slots_[k].stamps) return false;
        std::memset(slots_[k].stamps.cpu(), 0, slots_[k].stamps.size);
    }
    if (!pushUbo_ || !shadowUbo_ || !lightUbo_ || !poseBuf_ || !readBuf_) return false;
    std::memset(poseBuf_.cpu(), 0, poseBuf_.size);

    // the stand-ins: white for a material with no texture (`raster.cpp`
    // leaves the texel at 255), black for a light map and a reflection, the
    // flat normal
    {
        const unsigned char white[4] = {255, 255, 255, 255}, black[4] = {0, 0, 0, 0}, flat[4] = {128, 128, 255, 0};
        struct S { DkImage* img; Alloc* mem; uint32_t slot; const unsigned char* px; };
        const S ss[3] = {{&white_, &whiteMem_, kImgWhite, white}, {&black_, &blackMem_, kImgBlack, black},
                         {&flat_, &flatMem_, kImgFlat, flat}};
        dkCmdBufClear(upCb_);
        dkCmdBufAddMemory(upCb_, upCmdMem_.mb, upCmdMem_.off, upCmdMem_.size);
        upOpen_ = true;
        for (const S& s : ss) {
            if (!makeImage(*s.img, *s.mem, DkImageFormat_RGBA8_Unorm, 1, 1, 0)) return false;
            Alloc st;
            if (!stage(4, st)) return false;
            std::memcpy(st.cpu(), s.px, 4);
            DkImageView v;
            dkImageViewDefaults(&v, s.img);
            const DkCopyBuf src{st.gpu(), 0, 0};
            const DkImageRect r{0, 0, 0, 1, 1, 1};
            dkCmdBufCopyBufferToImage(upCb_, &src, &v, &r, 0);
            writeDescriptor(s.slot, *s.img);
        }
        flushUploads();
    }
    fb_ = omk::Surface(w, h, 0);
    // the pack's BLOCK FILES this GPU samples: Maxwell takes BC and ASTC both
    omk::texturePack().setCodecs(omk::kCodecBc | omk::kCodecAstc);
    std::printf("deko3d: up - %dx%d, %d swapchain images, %d frame(s) in flight, filter %s\n", w, h,
                kSwapImages, nSlots_, filter_ ? "bilinear" : "nearest");
    return true;
}

DekoRenderer::~DekoRenderer() {
    omk::removeGeometryListener(&DekoRenderer::geometryGone, this);
    if (queue_) dkQueueWaitIdle(queue_);
    if (swap_) dkSwapchainDestroy(swap_);
    if (cb_) dkCmdBufDestroy(cb_);
    if (upCb_) dkCmdBufDestroy(upCb_);
    if (queue_) dkQueueDestroy(queue_);
    data_.destroy(); cached_.destroy(); images_.destroy(); code_.destroy();
    if (dev_) dkDeviceDestroy(dev_);
}

void DekoRenderer::geometryGone(void* self, const omk::Geometry* g) {
    auto* r = static_cast<DekoRenderer*>(self);
    if (auto it = r->vbo_.find(g); it != r->vbo_.end()) {
        for (int c = 0; c < it->second.copies; ++c)
            r->cur().dead.push_back(it->second.mem[c]);   // a frame in flight may still draw it
        r->vbo_.erase(it);
    }
    r->baked_.erase(g);
}

// ---- THE FRAME ------------------------------------------------------------

void DekoRenderer::waitFrame() {
    Slot& sl = cur();
    if (sl.pending) {
        const auto w0 = std::chrono::steady_clock::now();
        struct WaitTimer {
            double& ms; std::chrono::steady_clock::time_point t0;
            ~WaitTimer() { ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
        } wt{waitMs_, w0};
        DKTRACE("waiting for the frame's fence\n");
        const DkResult r = dkFenceWait(&sl.fence, kFenceTimeoutNs);
        if (r != DkResult_Success) {
            std::printf("deko3d: the frame's fence did not signal in 2 s (result %d, queue %s)\n",
                        static_cast<int>(r), dkQueueIsInErrorState(queue_) ? "IN ERROR" : "not in error");
            std::fflush(stdout);
            dkQueueWaitIdle(queue_);
            markIdle();
        }
        sl.pending = false;
    }
}

void DekoRenderer::rung(int k) {
    if (!ladder_) return;
    dkCmdBufReportCounter(cb_, DkCounter_Timestamp, cur().stamps.gpu() + static_cast<uint32_t>(16 * (4 + k)));
    cur().rungs |= 1u << k;
}

std::string DekoRenderer::takeLadder() {
    if (!ladder_ || rungFrames_ == 0) return {};
    static const char* names[6] = {"state+blocks", "clears", "scene", "(acquire)", "barrier", "present draw"};
    std::string out;
    char b[64];
    for (int k = 0; k < 6; ++k) {
        std::snprintf(b, sizeof b, "%s%s %.2f", k ? ", " : "", names[k], rungSum_[k] / static_cast<double>(rungFrames_));
        out += b;
    }
    if (easuFrames_ > 0) {
        std::snprintf(b, sizeof b, " (of it EASU %.2f)", easuSum_ / static_cast<double>(easuFrames_));
        out += b;
    }
    for (double& v : rungSum_) v = 0.0;
    rungFrames_ = 0;
    easuSum_ = 0.0;
    easuFrames_ = 0;
    return out;
}

void DekoRenderer::stamp(int k) {
    dkCmdBufReportCounter(cb_, DkCounter_Timestamp, cur().stamps.gpu() + static_cast<uint32_t>(16 * k));
    cur().stamped |= 1u << k;
}

// the last frame's GPU time, once its fence is known signalled: the scene's
// span plus the present's, or (no scene sent apart - a surface alone) the
// whole list's
void DekoRenderer::readStamps() {
    // the slot's LAST frame - done, its fence waited for (`openCb`)
    Slot& sl = cur();
    unsigned& stamped_ = sl.stamped;
    if (ladder_ && (sl.rungs & 0x7Fu) == 0x7Fu) {
        const auto* q = static_cast<const volatile std::uint64_t*>(sl.stamps.cpu());
        std::uint64_t t[7];
        for (int k = 0; k < 7; ++k) t[k] = q[2 * (4 + k) + 1];
        for (int k = 0; k < 6; ++k)
            rungSum_[k] += t[k + 1] >= t[k] ? static_cast<double>(dkTimestampToNs(t[k + 1] - t[k])) / 1e6 : 0.0;
        ++rungFrames_;
        if (sl.rungs & 0x80u) {
            const std::uint64_t te = q[2 * (4 + 7) + 1];
            if (te >= t[5]) { easuSum_ += static_cast<double>(dkTimestampToNs(te - t[5])) / 1e6; ++easuFrames_; }
        }
    }
    sl.rungs = 0;
    if ((stamped_ & 0x9u) != 0x9u) { stamped_ = 0; return; }
    const auto* r = static_cast<const volatile std::uint64_t*>(sl.stamps.cpu());
    const std::uint64_t t0 = r[1], t1 = r[3], t2 = r[5], t3 = r[7];   // each report's second word
    std::uint64_t ticks = 0;
    if ((stamped_ & 0x6u) == 0x6u && t1 >= t0 && t3 >= t2) ticks = (t1 - t0) + (t3 - t2);
    else if (t3 >= t0) ticks = t3 - t0;
    gpuMs_ = static_cast<double>(dkTimestampToNs(ticks)) / 1e6;
    static int told = 0;
    if (told < 3) {
        ++told;
        // every word, both of each report: which one is the timestamp is
        // read here, not assumed (each report's second word is taken)
        std::printf("deko3d: the GPU timer - frame %.2f ms, mask %x, words", gpuMs_, stamped_);
        for (int w = 0; w < 8; ++w) std::printf(" %llu", static_cast<unsigned long long>(r[w]));
        std::printf("\n");
    }
    stamped_ = 0;
}

// the frame's command list, from the start of its memory once the GPU is done with it
void DekoRenderer::openCb() {
    if (cbOpen_) return;
    slot_ = (slot_ + 1) % nSlots_;   // the next slot: its last frame is the one to wait for
    waitFrame();
    for (Alloc& a : cur().extra) data_.free(a);
    cur().extra.clear();
    for (Alloc& a : cur().dead) data_.free(a);
    cur().dead.clear();
    readStamps();   // the frame before is done: its timestamps are in
    dkCmdBufClear(cb_);
    dkCmdBufAddMemory(cb_, cur().cmdMem.mb, cur().cmdMem.off, cur().cmdMem.size);
    cbOpen_ = true;
    sceneSubmitted_ = false;
    stamp(0);
    ladder_ = omk::envFlag("OMK_DEKO_LADDER");
    rung(0);
    // the descriptors the CPU wrote since (a new pool's), and the images the
    // uploads filled, seen fresh
    dkCmdBufBindImageDescriptorSet(cb_, imgDesc_.gpu(), kMaxImages);
    dkCmdBufBindSamplerDescriptorSet(cb_, sampDesc_.gpu(), kSampCount);
    dkCmdBufBarrier(cb_, DkBarrier_None, DkInvalidateFlags_Descriptors | DkInvalidateFlags_Image);
}

void DekoRenderer::pushView(const omk::View& view) {
    // Vulkan's `pushView`, from the software rasterizer's own basis - with
    // the second row NEGATED: deko3d's normalised Y points up (the header)
    omk::RCamera cam = view.cam;
    flipX_ = cam.flipX;
    cam.w = view.letterboxed() ? view.vw : w_;
    cam.h = view.letterboxed() ? view.vh : h_;
    float s[3], u[3], f[3], tanH = 0, tanV = 0;
    omk::cameraBasis(cam, s, u, f, tanH, tanV);
    const float nearP = omk::kNearCut, farP = 200000.0f;
    const float A = farP / (farP - nearP), B = -farP * nearP / (farP - nearP);
    const float e[3] = {cam.eye[0], cam.eye[1], cam.eye[2]};
    const float r0[4] = {s[0] / tanH, s[1] / tanH, s[2] / tanH,
                         -(s[0] * e[0] + s[1] * e[1] + s[2] * e[2]) / tanH};
    // Vulkan's row is -u / tanV (its Y down); deko3d's is +u / tanV
    const float r1[4] = {u[0] / tanV, u[1] / tanV, u[2] / tanV,
                         -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]) / tanV};
    const float r2[4] = {A * f[0], A * f[1], A * f[2],
                         -A * (f[0] * e[0] + f[1] * e[1] + f[2] * e[2]) + B};
    const float r3[4] = {f[0], f[1], f[2], -(f[0] * e[0] + f[1] * e[1] + f[2] * e[2])};
    const float* rows[4] = {r0, r1, r2, r3};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) push_.mvp[c * 4 + r] = rows[r][c];
}

// the state every scene draw shares, bound once a frame
void DekoRenderer::bindFrameState(const DkImageView* target) {
    const DkImageView* rt = target;
    dkCmdBufBindRenderTargets(cb_, &rt, 1, &depthView_);
    const DkShader* sh[2] = {&shVert_, &shFrag_};
    dkCmdBufBindShaders(cb_, DkStageFlag_GraphicsMask, sh, 2);
    lastFrag_ = &shFrag_;
    DkRasterizerState rs;
    dkRasterizerStateDefaults(&rs);
    rs.cullMode = DkFace_None;   // `Raster_DrawTriangles`' D3DCULL_NONE; the engine culled in software
    dkCmdBufBindRasterizerState(cb_, &rs);
    DkColorWriteState cw;
    dkColorWriteStateDefaults(&cw);
    dkCmdBufBindColorWriteState(cb_, &cw);
    // scene.vert's six inputs, from one buffer of `GpuVert`
    DkVtxAttribState a[6];
    const struct { uint32_t off; DkVtxAttribSize size; } at[6] = {
        {offsetof(GpuVert, x), DkVtxAttribSize_3x32},  {offsetof(GpuVert, u), DkVtxAttribSize_2x32},
        {offsetof(GpuVert, r), DkVtxAttribSize_3x32},  {offsetof(GpuVert, nx), DkVtxAttribSize_3x32},
        {offsetof(GpuVert, phase), DkVtxAttribSize_1x32}, {offsetof(GpuVert, mesh), DkVtxAttribSize_1x32}};
    for (int i = 0; i < 6; ++i) {
        std::memset(&a[i], 0, sizeof a[i]);
        a[i].bufferId = 0;
        a[i].isFixed = 0;
        a[i].offset = at[i].off;
        a[i].size = at[i].size;
        a[i].type = DkVtxAttribType_Float;
        a[i].isBgra = 0;
    }
    dkCmdBufBindVtxAttribState(cb_, a, 6);
    const DkVtxBufferState vb{sizeof(GpuVert), 0};
    dkCmdBufBindVtxBufferState(cb_, &vb, 1);
    // the blocks: the push block in both stages, the frame's shadow and lights
    dkCmdBufBindUniformBuffer(cb_, DkStage_Vertex, kUboPush, pushUbo_.gpu(), pushUbo_.size);
    dkCmdBufBindUniformBuffer(cb_, DkStage_Fragment, kUboPush, pushUbo_.gpu(), pushUbo_.size);
    dkCmdBufBindUniformBuffer(cb_, DkStage_Fragment, kUboShadow, shadowUbo_.gpu(), shadowUbo_.size);
    dkCmdBufBindUniformBuffer(cb_, DkStage_Fragment, kUboLights, lightUbo_.gpu(), lightUbo_.size);
    dkCmdBufBindStorageBuffer(cb_, DkStage_Vertex, kSsboPose, poseBuf_.gpu(), poseBuf_.size);
    boundPose_ = poseBuf_.gpu();
    // the stand-ins under the light map, the reflection and the shadow map
    const uint32_t samp = sampler();
    dkCmdBufBindTexture(cb_, DkStage_Fragment, kTexEmis, dkMakeTextureHandle(kImgBlack, samp));
    boundEmis_ = kImgBlack;
    dkCmdBufBindTexture(cb_, DkStage_Fragment, kTexRefl, dkMakeTextureHandle(kImgBlack, samp));
    dkCmdBufBindTexture(cb_, DkStage_Fragment, kTexShadow,
                        dkMakeTextureHandle(shadowLive_ ? kImgShadow : kImgBlack, kSampClamp));
    lastBlend_ = -1;
}

void DekoRenderer::begin(const omk::View& view) {
    if (!dev_) return;
    // a scene the last frame sent and nothing presented (a readback alone, or
    // `OMK_NO_GPU_PRESENT`): done once the queue is, and its list started over
    if (cbOpen_ && sceneSubmitted_) {
        dkQueueWaitIdle(queue_);
        markIdle();
        cbOpen_ = false;
    }
    openCb();   // (the shadow pass may have opened it: then a no-op)
    st_ = omk::RasterStats{};
    fog_ = view.fog;
    fogStart_ = view.fogStart;
    fogEnd_ = view.fogEnd;
    mistOn_ = view.mist > 0.0f || view.dust > 0.0f || view.veil > 0.0f;
    for (int i = 0; i < 3; ++i) fogColour_[i] = static_cast<float>(view.fogColour[i]) / 255.0f;
    rain_ = view.rain;
    dither_ = view.dither;
    // the original's module may draw this frame: none of the frame-wide
    // remaster passes (the mapped shadow is not drawn here yet - D4) -
    // `OMK_DEKO_FULL=1` keeps the full shader throughout, to compare
    // read each frame (`OMK_BENCH_VARY` moves it), and by its VALUE: `envFlag`
    // is "set", and OMK_DEKO_FULL=0 is set (the console's A/B drew the full
    // shader on both sides)
    const bool forceFull = omk::envOn("OMK_DEKO_FULL");
    frameOrig_ = !forceFull && !shadowLive_ && view.mist <= 0.0f && view.dust <= 0.0f && view.veil <= 0.0f;
    cur().poseUsed = 0;   // the slot's last frame is done (`openCb`): its pose buffer from the start
    cur().poseCache.clear();
    probe_ = omk::envInt("OMK_DEKO_PROBE", 0);
    ++frames_;
    pushRingAt_ = 0;
    if (probe_ == 5 && !pushRing_) pushRing_ = data_.alloc(kPushRingSlots * kUboAlign, kUboAlign);
    pushView(view);

    // ---- THE FRAME'S BLOCKS, Vulkan's `begin` field for field (no tiles: D4)
    LightUbo ub{};
    const int n = static_cast<int>(std::min<std::size_t>(view.lights.size(),
                                                         static_cast<std::size_t>(omk::View::kMaxGpuLights)));
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
    ub.eye[3] = view.setDim;
    ub.fx[0] = view.relief;
    ub.fx[1] = view.mist; ub.fx[2] = view.mistGround; ub.fx[3] = view.mistHeight;
    ub.dust[0] = view.dust; ub.dust[1] = view.dustDrift[0]; ub.dust[2] = view.dustDrift[1];
    ub.dust[3] = view.dustScale;
    for (int k = 0; k < 3; ++k) ub.dustColour[k] = view.dustColour[k];
    ub.veil[0] = view.veil; ub.veil[1] = view.veilFall; ub.veil[2] = view.veilScale;
    ub.veil[3] = view.puddles;
    for (int k = 0; k < 3; ++k) ub.veilColour[k] = view.veilColour[k];
    ub.veilColour[3] = view.veilGust;
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
    ub.tiles = 0;
    litCount_ = n;
    ShadowUbo sb{};
    sb.texel = 1.0f / static_cast<float>(kShadowSide);
    sb.shimmer = view.shimmerClock;
    if (shadowLive_) {
        // the light's matrix as the fragment reads it - NOT the flipped one
        // the depth pass drew with (`shadowPass`)
        std::memcpy(sb.lightMvp, shadowMvp_, sizeof shadowMvp_);
        sb.strength = shadowStrength_;
        // in the slab's own 0..1 depth, one map texel of slope at the worst
        // angle - enough to stop the ground shadowing itself (Vulkan's)
        sb.bias = 0.0015f;
    }

    // THE POST-PASS's this frame: decided here, where the scene's target is
    // chosen - the glow only with a light map in the pool (without one every
    // texel's light is 0), the grade whenever one is set (Vulkan's rule)
    halo_ = view.halo;
    if (lutDirty_ && lutN_ > 1 && !uploadLut()) lutN_ = 0;
    postNow_ = ((bloom_ > 0.0f && poolLit_) || lutN_ > 1) && makePost();
    lightMapNow_ = false;
    bindFrameState(postNow_ ? &postSrcView_ : &colourView_);
    dkCmdBufPushConstants(cb_, lightUbo_.gpu(), lightUbo_.size, 0, sizeof ub, &ub);
    dkCmdBufPushConstants(cb_, shadowUbo_.gpu(), shadowUbo_.size, 0, sizeof sb, &sb);
    rung(1);

    // the clear: the scene's fog colour (`View::clearColour`), depth far, stencil 0
    const DkScissor all{0, 0, static_cast<uint32_t>(w_), static_cast<uint32_t>(h_)};
    dkCmdBufSetScissors(cb_, 0, &all, 1);
    // (with the BLOOM the alpha is the light map's value: what no surface
    // covers clears to 0 - not light; at 1 the fog past the clip glowed)
    dkCmdBufClearColorFloat(cb_, 0, DkColorMask_RGBA, view.clearColour[0] / 255.0f, view.clearColour[1] / 255.0f,
                            view.clearColour[2] / 255.0f, bloom_ > 0.0f ? 0.0f : 1.0f);
    dkCmdBufClearDepthStencil(cb_, true, 1.0f, 0xFF, 0);
    rung(2);
    // the letterbox is a VIEWPORT: the picture in the top-left `vw x vh`
    picH_ = view.letterboxed() ? view.vh : h_;
    // ...in RENDER pixels: the upscale draws it `up_` times smaller
    const int vw = rs(view.letterboxed() ? view.vw : w_), vhR = rs(picH_);
    const DkViewport vp{0.0f, 0.0f, static_cast<float>(vw), static_cast<float>(vhR), 0.0f, 1.0f};
    const DkScissor sc = probe_ == 3 || probe_ == 7 ? DkScissor{0, 0, 1, 1}
                                     : DkScissor{0, 0, static_cast<uint32_t>(std::min(vw, w_)), static_cast<uint32_t>(vhR)};
    dkCmdBufSetViewports(cb_, 0, &vp, 1);
    dkCmdBufSetScissors(cb_, 0, &sc, 1);
    recording_ = true;
}

void DekoRenderer::writeCorners(const omk::Geometry& g, Alloc& mem, const std::vector<std::uint32_t>* only) {
    const auto u0 = std::chrono::steady_clock::now();
    const auto bk = baked_.find(&g);
    const std::vector<std::uint8_t>* lose = bk == baked_.end() ? nullptr : &bk->second;
    auto* dst = static_cast<GpuVert*>(mem.cpu());
    const std::size_t n = g.corners.size();
    std::size_t wrote = 0;
    if (only) {
        for (const std::uint32_t i : *only)
            if (i < n) { dst[i] = vertOf(g, i, lose); ++wrote; }
    } else {
        for (std::size_t i = 0; i < n; ++i) dst[i] = vertOf(g, i, lose);
        wrote = n;
    }
    ++uploads_;
    upBytes_ += static_cast<long>(wrote * sizeof(GpuVert));
    upMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - u0).count();
}

const Alloc* DekoRenderer::uploadGeometry(const omk::Geometry* g) {
    const std::size_t n = g->corners.size();
    if (!n) return nullptr;
    const uint32_t bytes = static_cast<uint32_t>(n * sizeof(GpuVert));
    auto it = vbo_.find(g);
    // new, or of another size (or marked by `bakeDepthTie`): one buffer, written whole
    if (it == vbo_.end() || it->second.n != n) {
        if (it != vbo_.end()) {
            for (int c = 0; c < it->second.copies; ++c) cur().dead.push_back(it->second.mem[c]);
            vbo_.erase(it);
        }
        Vbo v;
        v.mem[0] = data_.alloc(bytes, 256);
        if (!v.mem[0]) return nullptr;
        v.copies = 1;
        v.n = n;
        v.seen = g->revision;
        it = vbo_.emplace(g, std::move(v)).first;
        g->resident.mark();
        writeCorners(*g, it->second.mem[0], nullptr);
        return &it->second.mem[0];
    }
    Vbo& v = it->second;
    if (g->revision != v.seen) {
        // the change observed: which corners, if the geometry says (`dirtyCorners`)
        static const bool noDirty = omk::envFlag("OMK_NO_DIRTY");
        const bool delta = !noDirty && g->dirtyTo != 0 && g->dirtyTo == g->revision && g->dirtyFrom == v.seen;
        if (v.copies == 1 && nSlots_ > 1) {
            // static until now: its one buffer may be in flight - a copy a slot from here on
            cur().dead.push_back(v.mem[0]);
            v.mem[0] = Alloc{};
            v.copies = nSlots_;
            for (int c = 0; c < v.copies; ++c) {
                v.mem[c] = data_.alloc(bytes, 256);
                if (!v.mem[c]) { for (int k = 0; k < c; ++k) data_.free(v.mem[k]); vbo_.erase(it); return nullptr; }
                v.whole[c] = true;
                v.pending[c].clear();
            }
        } else {
            for (int c = 0; c < v.copies; ++c) {
                if (v.whole[c]) continue;
                if (!delta || v.pending[c].size() + g->dirtyCorners.size() > n / 4) {
                    v.whole[c] = true;   // a revision without its corners, or most of them: all
                    v.pending[c].clear();
                } else {
                    v.pending[c].insert(v.pending[c].end(), g->dirtyCorners.begin(), g->dirtyCorners.end());
                }
            }
        }
        v.seen = g->revision;
    }
    // this frame's copy, brought up to date
    const int c = v.copies == 1 ? 0 : slot_;
    if (v.whole[c]) {
        writeCorners(*g, v.mem[c], nullptr);
        v.whole[c] = false;
        v.pending[c].clear();
    } else if (!v.pending[c].empty()) {
        writeCorners(*g, v.mem[c], &v.pending[c]);
        v.pending[c].clear();
    }
    return &v.mem[c];
}

bool DekoRenderer::bakeDepthTie(const omk::Geometry* g, std::span<const omk::Draw> order) {
    if (omk::tieBakeOff() || !g) return false;
    const std::vector<std::size_t> tris = omk::bakeTieLosers(*g, order);
    std::vector<std::uint8_t> lose(g->corners.size(), 0);
    for (const std::size_t tri : tris)
        for (std::size_t k = 3 * tri; k < 3 * tri + 3 && k < lose.size(); ++k) lose[k] = 1;
    baked_[g] = std::move(lose);
    // the buffers hold the old decision: rewritten whole, marked - each a NEW
    // buffer (an old one may be in flight)
    if (auto it = vbo_.find(g); it != vbo_.end()) it->second.n = 0;
    return true;
}

bool DekoRenderer::writePose(const omk::Draw& d, uint32_t& off) {
    // Vulkan's block, word for word: a header (lights, from black, the base
    // grey, the affines' count), the lights (two vec4 each), three vec4 an
    // affine, then the body light's tint
    Slot& sl = cur();
    const auto key = std::make_pair(d.meshPose, d.vertexLights);
    if (const auto it = sl.poseCache.find(key); it != sl.poseCache.end()) { off = it->second; return true; }
    const uint32_t at = alignUp(sl.poseUsed, kPoseAlign);
    if (!sl.pose || at + kPoseRange > sl.pose.size || d.meshPoses > static_cast<std::size_t>(kPoseMaxMeshes)) return false;
    float* f = reinterpret_cast<float*>(static_cast<unsigned char*>(sl.pose.cpu()) + at);
    const int nl = d.vertexLights ? std::min(d.vertexLightCount, kPoseLights) : 0;
    f[0] = static_cast<float>(nl);
    f[1] = d.lightsFromBlack ? 1.0f : 0.0f;
    f[2] = d.lightBase;
    f[3] = static_cast<float>(d.meshPoses);
    for (int i = 0; i < nl; ++i) std::memcpy(f + 4 + 8 * i, d.vertexLights + 8 * i, 8 * sizeof(float));
    float* a = f + 4 + 8 * kPoseLights;
    std::memcpy(a, d.meshPose, d.meshPoses * 12 * sizeof(float));
    float* t = a + 12 * d.meshPoses;
    t[0] = d.lightTint[0]; t[1] = d.lightTint[1]; t[2] = d.lightTint[2]; t[3] = 1.0f;
    sl.poseUsed = at + static_cast<uint32_t>((1 + 2 * kPoseLights + 3 * d.meshPoses + 1) * 16);
    off = at;
    sl.poseCache.emplace(key, off);
    return true;
}

// A posed draw's corners placed on the CPU instead (Vulkan's `cpuPose`): the
// same affine, and `applyLights`' law for the colour
omk::Draw DekoRenderer::cpuPose(const omk::Draw& d) {
    omk::Geometry& cp = cpuPosed_[d.geo];
    omk::poseGeometry(d, cp, omk::PoseParts{true, true, kPoseLights});
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

void DekoRenderer::submit(const omk::Draw& d) {
    if (!recording_ || !d.geo || !d.count) return;
    if (d.meshPose && d.meshPoses && d.geo->cornerMesh.size() == d.geo->corners.size()) {
        uint32_t off = 0;
        if (writePose(d, off)) submitOne(d, true, off);
        else submitOne(cpuPose(d), false, 0);
        ++posedDraws_;
        return;
    }
    submitOne(d, false, 0);
}

void DekoRenderer::submitOne(const omk::Draw& d, bool posed, uint32_t poseOff) {
    if (probe_ == 6) return;   // the probe: no draw
    using clk = std::chrono::steady_clock;
    const auto s0 = clk::now();
    struct SubmitTimer {   // the whole of `submit`, however it returns
        DekoRenderer* r; clk::time_point t0;
        ~SubmitTimer() { r->submitMs_ += std::chrono::duration<double, std::milli>(clk::now() - t0).count(); }
    } timer{this, s0};
    const Alloc* vmem = uploadGeometry(d.geo);
    if (!vmem) return;
    st_.triangles += static_cast<long>(d.count / 3);

    // THE BLEND - `omk::blendRule`, Vulkan's three pipelines as three states
    int k = d.blend == omk::Blend::Opaque ? 0 : d.blend == omk::Blend::Add ? 1 : 2;
    if (probe_ == 7 && lastBlend_ >= 0) k = lastBlend_;   // the probe: the first state kept
    if (k != lastBlend_) {
        const omk::Blend kModes[3] = {omk::Blend::Opaque, omk::Blend::Add, omk::Blend::Mul};
        const omk::BlendRule rule = omk::blendRule(kModes[k]);
        const auto factor = [](omk::BlendFactor f) {
            return f == omk::BlendFactor::Zero ? DkBlendFactor_Zero
                 : f == omk::BlendFactor::One  ? DkBlendFactor_One
                                               : DkBlendFactor_InvSrcColor;
        };
        DkColorState cs;
        dkColorStateDefaults(&cs);
        dkColorStateSetBlendEnable(&cs, 0, rule.blend);
        dkCmdBufBindColorState(cb_, &cs);
        if (rule.blend) {
            DkBlendState bs;
            dkBlendStateDefaults(&bs);
            dkBlendStateSetOps(&bs, DkBlendOp_Add, DkBlendOp_Add);
            // the alpha's factors are the backend's own (Vulkan's `BlendRule` note)
            dkBlendStateSetFactors(&bs, factor(rule.src), factor(rule.dst), DkBlendFactor_One,
                                   kModes[k] == omk::Blend::Add ? DkBlendFactor_One : DkBlendFactor_Zero);
            dkCmdBufBindBlendState(cb_, 0, &bs);
        }
        DkDepthStencilState ds;
        dkDepthStencilStateDefaults(&ds);
        ds.depthTestEnable = 1;
        ds.depthWriteEnable = rule.depthWrite ? 1 : 0;   // only an opaque pass owns the depth
        ds.depthCompareOp = DkCompareOp_Less;
        dkCmdBufBindDepthStencilState(cb_, &ds);
        lastBlend_ = k;
    }
    // THE TEXTURE: the key's low six bits, or the pool's index above them
    const std::size_t slot = omk::drawTextureSlot(d);
    const uint32_t img = slot < texSlot_.size() ? texSlot_[slot] : kImgWhite;
    if (probe_ != 7 || draws_ == 0)
        dkCmdBufBindTexture(cb_, DkStage_Fragment, kTexTex, dkMakeTextureHandle(img, sampler()));
    ++draws_;

    // the pose buffer: the body's block for a posed draw, the stand-in else
    push_.posed = posed ? 1 : 0;
    const DkGpuAddr poseAt = posed ? cur().pose.gpu() + poseOff : poseBuf_.gpu();
    if (poseAt != boundPose_) {
        dkCmdBufBindStorageBuffer(cb_, DkStage_Vertex, kSsboPose, poseAt, posed ? kPoseRange : poseBuf_.size);
        boundPose_ = poseAt;
    }
    push_.cutout = d.cutout ? 1 : 0;
    push_.caster = d.castsShadow ? 1 : 0;
    // THE BLOOM: the alpha written is the texel's LIGHT, its light map's
    push_.bloomMode = bloom_ > 0.0f ? 1 : 0;
    if (bloom_ > 0.0f) {
        const DTex* bt = slot < texOf_.size() ? texOf_[slot] : nullptr;
        const uint32_t em = bt ? bt->emSlot : kImgBlack;
        if (em != boundEmis_) {
            dkCmdBufBindTexture(cb_, DkStage_Fragment, kTexEmis, dkMakeTextureHandle(em, sampler()));
            boundEmis_ = em;
        }
        lightMapNow_ |= em != kImgBlack;
    }
    // no reflection pass and no water yet (D4): a WET floor's darkening and
    // the rain's ripples ride in the mode's high bits, as on Vulkan
    push_.water = 0;
    if (d.wet > 0.0f) {
        push_.water |= static_cast<int32_t>(std::lround(std::min(d.wet, 1.0f) * 255.0f)) << 4;
        push_.water |= static_cast<int32_t>(std::lround(std::clamp(rain_, 0.0f, 1.0f) * 255.0f)) << 12;
        if (d.snow) push_.water |= 1 << 20;
        if (d.wetBody) push_.water |= 1 << 21;
    }
    push_.waterStrength = 0.0f;
    {
        // THE PACK: a replacement `su` x `sv` the original's size, its UVs
        // still in the original's texels (`scene.frag` texScaled)
        const DTex* bound = slot < texOf_.size() ? texOf_[slot] : nullptr;
        push_.texScaled = bound && bound->packed ? 1 : 0;
        push_.texScaleU = bound ? bound->su : 1.0f;
        push_.texScaleV = bound ? bound->sv : 1.0f;
    }
    push_.lit = litCount_ > 0 ? d.lit : 0;
    // THE FOG's two exclusions - `renderer.cpp`'s rule, Vulkan's
    push_.fogStart = 0.0f;
    push_.fogEnd = 0.0f;
    if (fog_ && !(d.bucketKey & 0x2080u)) {
        const float fogScale = (d.bucketKey & 0x800u) ? 2.0f : 1.0f;
        push_.fogStart = fogStart_ * fogScale;
        push_.fogEnd = fogEnd_ * fogScale;
        for (int i = 0; i < 3; ++i) push_.fogColour[i] = fogColour_[i];
    } else if (mistOn_ && !(d.bucketKey & 0x2080u)) {
        push_.fogStart = -1.0f;
        push_.fogEnd = -1.0f;
        for (int i = 0; i < 3; ++i) push_.fogColour[i] = fogColour_[i];
    }
    // THE FRAGMENT MODULE: an opaque CUTOUT draw the late depth's (`scene.frag`
    // OMK_LATE_DEPTH); the original's (OMK_ORIGINAL) wherever the draw and the
    // frame ask nothing of the remaster - the full module everywhere else
    const bool orig = frameOrig_ && push_.lit == 0 && push_.water == 0 && push_.bloomMode == 0;
    const bool cut = d.cutout && k == 0 && probe_ != 2;
    const DkShader* frag = probe_ == 1 ? &shFsqFrag_
                         : orig ? (cut ? &shOrigCut_ : &shOrig_)
                                : (cut ? &shFragCut_ : &shFrag_);
    if (frag != lastFrag_ && probe_ != 7) {
        const DkShader* sh[1] = {frag};
        dkCmdBufBindShaders(cb_, DkStageFlag_Fragment, sh, 1);
        lastFrag_ = frag;
    }
    origDraws_ += orig ? 1 : 0;
    dkCmdBufBindVtxBuffer(cb_, 0, vmem->gpu(), vmem->size);
    // THE BACK-FACE CULL, one push per run of `cornerCull`
    const omk::Geometry* g = d.geo;
    const bool haveCull = g->cornerCull.size() == g->corners.size();
    std::size_t i = d.start;
    const std::size_t e = std::min(d.start + d.count, g->corners.size());
    while (i < e) {
        const std::uint8_t c = haveCull ? g->cornerCull[i] : 0u;
        std::size_t j = i;
        while (j < e && (haveCull ? g->cornerCull[j] : 0u) == c) ++j;
        if (probe_ == 4) j = e;   // the probe: the whole draw as one run
        push_.cull = c ? (flipX_ ? -cullSign() : cullSign()) : 0;
        if (probe_ == 5 && pushRing_ && pushRingAt_ < kPushRingSlots) {
            // the probe: the run's block in a slot of its own, bound in both stages
            const uint32_t off = pushRingAt_++ * kUboAlign;
            std::memcpy(static_cast<char*>(pushRing_.cpu()) + off, &push_, sizeof(Push));
            dkCmdBufBindUniformBuffer(cb_, DkStage_Vertex, kUboPush, pushRing_.gpu() + off, kUboAlign);
            dkCmdBufBindUniformBuffer(cb_, DkStage_Fragment, kUboPush, pushRing_.gpu() + off, kUboAlign);
        } else {
            dkCmdBufPushConstants(cb_, pushUbo_.gpu(), pushUbo_.size, 0, sizeof(Push), &push_);
        }
        ++runs_;
        verts_ += static_cast<long>(j - i);
        dkCmdBufDraw(cb_, DkPrimitive_Triangles, static_cast<uint32_t>(j - i), 1, static_cast<uint32_t>(i), 0);
        i = j;
    }
    st_.drawn += static_cast<long>(d.count / 3);
}

void DekoRenderer::end() {
    if (!recording_) return;
    recording_ = false;
    shadowLive_ = false;   // the next frame's shadow pass sets it again
    if (postNow_) recordPost();
    rung(3);
    stamp(1);
    dkQueueSubmitCommands(queue_, dkCmdBufFinishList(cb_));
    sceneSubmitted_ = true;
    dirty_ = true;
}

const omk::Surface& DekoRenderer::readback() {
    // IDEMPOTENT (`renderer.h`): converted once a frame, and a caller's edits kept
    if (fb_.w != w_ || fb_.h != h_) fb_ = omk::Surface(w_, h_, 0);
    if (!dirty_ || !dev_) return fb_;
    dirty_ = false;
    openCb();
    // the scene's writes done before the copy engine reads the target
    dkCmdBufBarrier(cb_, DkBarrier_Full, DkInvalidateFlags_Image);
    const DkImageRect r{0, 0, 0, static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
    const DkCopyBuf dst{readBuf_.gpu(), 0, 0};
    if (up_ > 1.0f) {
        // UPSCALED, the picture is read at the OUTPUT size through the same
        // EASU and RCAS as the present pass's, undithered (`raw`) - Vulkan's
        // rule - and quantised below as a frame drawn at that size would be
        dkCmdBufBindImageDescriptorSet(cb_, imgDesc_.gpu(), kMaxImages);
        dkCmdBufBindSamplerDescriptorSet(cb_, sampDesc_.gpu(), kSampCount);
        recordPresentDraw(&readView_, kImgColour, 0, picH_ > 0 ? picH_ : h_, true);
        dkCmdBufBarrier(cb_, DkBarrier_Full, 0);
        dkCmdBufCopyImageToBuffer(cb_, &readView_, &r, &dst, 0);
    } else {
        dkCmdBufCopyImageToBuffer(cb_, &colourView_, &r, &dst, 0);
    }
    dkQueueSubmitCommands(queue_, dkCmdBufFinishList(cb_));
    dkQueueWaitIdle(queue_);
    markIdle();   // the queue is idle
    // the CPU's cached copy of what the GPU wrote, dropped before it is read
    dkMemBlockFlushCpuCache(readBuf_.mb, readBuf_.off, readBuf_.size);
    const auto* src = static_cast<const unsigned char*>(readBuf_.cpu());
    // 888 -> 565 HERE, in software, as Vulkan does (`PORTING` A3: one
    // quantisation rule, in one place)
    if (dither_) {
        for (int y = 0; y < h_; ++y)
            omk::quantise888DitherRow(src + 4 * static_cast<std::size_t>(y) * static_cast<std::size_t>(w_), w_, y,
                                      fb_.px.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w_));
    } else {
        for (int i = 0; i < w_ * h_; ++i)
            fb_.px[static_cast<std::size_t>(i)] = omk::rgb565(src[4 * i], src[4 * i + 1], src[4 * i + 2]);
    }
    return fb_;
}

// THE PRESENT PASS (`present.frag`): `colour_` or the uploaded surface into
// the swapchain image - the dither and the 565 expansion, or (`raw`) the
// 8-bit values as they are
bool DekoRenderer::runPresent(uint32_t imgSlot, int vy, int vh, bool raw, const float* over) {
    DKTRACE("present: acquiring a swapchain image\n");
    const int slot = dkQueueAcquireImage(queue_, swap_);
    DKTRACE("present: image %d acquired\n", slot);
    if (slot < 0 || slot >= kSwapImages) return false;
    // (a list of its own after a scene sent apart: its span timed alone)
    if (sceneSubmitted_) stamp(2);
    rung(4);
    if (over) {
        // the overlay's planes from the slot's staging into their images -
        // after every earlier command, the last frame's overlay that read them
        // included (`DkBarrier_Full`)
        dkCmdBufBarrier(cb_, DkBarrier_Full, 0);
        const uint32_t cBytes = static_cast<uint32_t>(w_) * static_cast<uint32_t>(h_) * 2;
        const DkImageRect r{0, 0, 0, static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
        DkImageView vc, vm;
        dkImageViewDefaults(&vc, &overC_);
        dkImageViewDefaults(&vm, &overM_);
        const DkCopyBuf sc{cur().overStage.gpu(), 0, 0}, sm{cur().overStage.gpu() + cBytes, 0, 0};
        dkCmdBufCopyBufferToImage(cb_, &sc, &vc, &r, 0);
        dkCmdBufCopyBufferToImage(cb_, &sm, &vm, &r, 0);
    }
    // what wrote the source (the scene's pass, or the surface's copy) done
    // and its image cache dropped before the pass samples it
    dkCmdBufBarrier(cb_, DkBarrier_Full, DkInvalidateFlags_Image);
    rung(5);
    // the descriptor arrays again: this is a new command list
    dkCmdBufBindImageDescriptorSet(cb_, imgDesc_.gpu(), kMaxImages);
    dkCmdBufBindSamplerDescriptorSet(cb_, sampDesc_.gpu(), kSampCount);
    recordPresentDraw(&swapView_[slot], imgSlot, vy, vh, raw);
    if (over) {
        // ...and the interface over it: C + world * M, the fade over all
        // (the blend ONE, SRC_ALPHA - GLES's)
        const DkShader* osh[1] = {&shOverlay_};
        dkCmdBufBindShaders(cb_, DkStageFlag_Fragment, osh, 1);
        DkColorState ocs;
        dkColorStateDefaults(&ocs);
        dkColorStateSetBlendEnable(&ocs, 0, true);
        dkCmdBufBindColorState(cb_, &ocs);
        DkBlendState obs;
        dkBlendStateDefaults(&obs);
        dkBlendStateSetOps(&obs, DkBlendOp_Add, DkBlendOp_Add);
        dkBlendStateSetFactors(&obs, DkBlendFactor_One, DkBlendFactor_SrcAlpha, DkBlendFactor_Zero, DkBlendFactor_One);
        dkCmdBufBindBlendState(cb_, 0, &obs);
        dkCmdBufBindTexture(cb_, DkStage_Fragment, 0, dkMakeTextureHandle(kImgOverC, kSampClamp));
        dkCmdBufBindTexture(cb_, DkStage_Fragment, 1, dkMakeTextureHandle(kImgOverM, kSampClamp));
        dkCmdBufPushConstants(cb_, pushUbo_.gpu(), pushUbo_.size, 0, 4 * sizeof(float), over);
        dkCmdBufDraw(cb_, DkPrimitive_Triangles, 3, 1, 0, 0);
        ++overlays_;
    }
    // the frame's fence INSIDE its list, as deko3d's samples do
    // (`CCmdMemRing::end`): signalled on the queue after the present (the
    // first console run) it never signalled - every wait ran out its 2 s
    rung(6);
    stamp(3);
    dkCmdBufSignalFence(cb_, &cur().fence, true);
    dkQueueSubmitCommands(queue_, dkCmdBufFinishList(cb_));
    DKTRACE("present: submitted, presenting image %d\n", slot);
    dkQueuePresentImage(queue_, swap_, slot);
    if (traceLeft() > 0) {
        // the first frames waited for here, so a GPU fault is said at once
        const DkResult r = dkFenceWait(&cur().fence, kFenceTimeoutNs);
        std::printf("deko3d trace: present done - fence %d, queue %s\n", static_cast<int>(r),
                    dkQueueIsInErrorState(queue_) ? "IN ERROR" : "fine");
        std::fflush(stdout);
        --traceLeft();
    }
    cur().pending = true;
    cbOpen_ = false;   // the next frame starts its list from the top, once this one is done
    return true;
}

// THE PRESENT PASS's draw into `rt` (the swapchain's image, or `readbackImg_`):
// `imgSlot`'s picture - with the upscale, EASU's of it - dithered and
// expanded, or (`raw`) its 8-bit values
void DekoRenderer::recordPresentDraw(const DkImageView* rt, uint32_t imgSlot, int vy, int vh, bool raw) {
    dkCmdBufBindRenderTargets(cb_, &rt, 1, nullptr);
    const DkViewport vp{0.0f, 0.0f, static_cast<float>(w_), static_cast<float>(h_), 0.0f, 1.0f};
    const DkScissor sc{0, 0, static_cast<uint32_t>(w_), static_cast<uint32_t>(h_)};
    dkCmdBufSetViewports(cb_, 0, &vp, 1);
    dkCmdBufSetScissors(cb_, 0, &sc, 1);
    // `OMK_DEKO_CLEAR=1`, an instrument: the image MAGENTA first - seen
    // magenta, the swapchain reaches the screen and the pass draws nothing;
    // still black, the images never reach it
    static const bool magenta = omk::envFlag("OMK_DEKO_CLEAR");
    if (magenta) dkCmdBufClearColorFloat(cb_, 0, DkColorMask_RGBA, 1.0f, 0.0f, 1.0f, 1.0f);
    const DkShader* sh[2] = {&shFsq_, &shPresent_};
    dkCmdBufBindShaders(cb_, DkStageFlag_GraphicsMask, sh, 2);
    lastFrag_ = nullptr;
    DkRasterizerState rast;
    dkRasterizerStateDefaults(&rast);
    rast.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(cb_, &rast);
    DkColorState cs;
    dkColorStateDefaults(&cs);
    dkCmdBufBindColorState(cb_, &cs);
    DkColorWriteState cw;
    dkColorWriteStateDefaults(&cw);
    dkCmdBufBindColorWriteState(cb_, &cw);
    DkDepthStencilState ds;
    dkDepthStencilStateDefaults(&ds);
    ds.depthTestEnable = 0;
    ds.depthWriteEnable = 0;
    dkCmdBufBindDepthStencilState(cb_, &ds);
    lastBlend_ = -1;
    dkCmdBufBindVtxAttribState(cb_, nullptr, 0);
    dkCmdBufBindVtxBufferState(cb_, nullptr, 0);
    dkCmdBufBindUniformBuffer(cb_, DkStage_Fragment, kUboPush, pushUbo_.gpu(), pushUbo_.size);
    vh = std::clamp(vh, 1, h_);
    // THE UPSCALE (`todo/remaster.md` A6, Vulkan's): the scene drawn `up_`
    // times smaller in `colour_`'s top-left, brought up to the picture's
    // size by FSR 1's EASU (`easu.frag`, the four texels round a point by
    // textureGather - a linear, clamped sampler) into `easu_`, then
    // sharpened by RCAS in this pass (`present.frag`), before the dither
    const bool up = up_ > 1.0f && imgSlot == kImgColour;
    if (up) {
        const DkImageView* ert = &easuView_;
        dkCmdBufBindRenderTargets(cb_, &ert, 1, nullptr);
        const DkViewport evp{0.0f, 0.0f, static_cast<float>(w_), static_cast<float>(vh), 0.0f, 1.0f};
        const DkScissor esc{0, 0, static_cast<uint32_t>(w_), static_cast<uint32_t>(vh)};
        dkCmdBufSetViewports(cb_, 0, &evp, 1);
        dkCmdBufSetScissors(cb_, 0, &esc, 1);
        // `OMK_DEKO_EASU_GATHER=1`, an A/B: the gathered module (the cyan picture's)
        static const bool gather = omk::envOn("OMK_DEKO_EASU_GATHER");
        const DkShader* esh[2] = {&shFsq_, !amdFsr() ? &shEasuLite_ : gather ? &shEasu_ : &shEasuFetch_};
        dkCmdBufBindShaders(cb_, DkStageFlag_GraphicsMask, esh, 2);
        dkCmdBufBindTexture(cb_, DkStage_Fragment, 0, dkMakeTextureHandle(kImgColour, kSampLinearClamp));
        if (amdFsr()) {
            std::uint32_t con[16];
            omk::fsrEasuCon(con, static_cast<float>(rs(w_)), static_cast<float>(rs(vh)), static_cast<float>(w_),
                            static_cast<float>(h_), static_cast<float>(w_), static_cast<float>(vh));
            dkCmdBufPushConstants(cb_, pushUbo_.gpu(), pushUbo_.size, 0, sizeof con, con);
        } else {
            // the lite EASU's: the drawn picture, then the output's
            const float sizes[4] = {static_cast<float>(rs(w_)), static_cast<float>(rs(vh)), static_cast<float>(w_),
                                    static_cast<float>(vh)};
            dkCmdBufPushConstants(cb_, pushUbo_.gpu(), pushUbo_.size, 0, sizeof sizes, sizes);
        }
        dkCmdBufDraw(cb_, DkPrimitive_Triangles, 3, 1, 0, 0);
        rung(7);   // (the ladder's: EASU's span, apart from the present's)
        // EASU's writes done before the present pass samples them
        dkCmdBufBarrier(cb_, DkBarrier_Fragments, DkInvalidateFlags_Image);
    }
    dkCmdBufBindRenderTargets(cb_, &rt, 1, nullptr);
    dkCmdBufSetViewports(cb_, 0, &vp, 1);
    dkCmdBufSetScissors(cb_, 0, &sc, 1);
    const DkShader* psh[2] = {&shFsq_, up && !amdFsr() ? &shPresentLite_ : &shPresent_};
    dkCmdBufBindShaders(cb_, DkStageFlag_GraphicsMask, psh, 2);
    dkCmdBufBindTexture(cb_, DkStage_Fragment, 0, dkMakeTextureHandle(up ? kImgEasu : imgSlot, kSampClamp));
    const PresentPush pp{vy, vh, w_, dither_ && !raw ? 1 : 0, 1, up ? 1 : 0,
                         up ? omk::fsrRcasCon(omk::kFsrRcasStops) : 0u, raw ? 1 : 0};
    dkCmdBufPushConstants(cb_, pushUbo_.gpu(), pushUbo_.size, 0, sizeof pp, &pp);
    dkCmdBufDraw(cb_, DkPrimitive_Triangles, 3, 1, 0, 0);
}

bool DekoRenderer::presentWorld(int vy, int vh) {
    if (!swap_ || recording_) return false;
    openCb();
    return runPresent(kImgColour, vy, vh, false);
}

bool DekoRenderer::presentSurface(const omk::Surface& s) {
    if (!swap_ || recording_) return false;
    DKTRACE("present: a %dx%d surface\n", s.w, s.h);
    openCb();   // waits for the frame before, which may still read the staging buffer
    // 565 -> replicated RGBA8 through the table (`expand565Rgba`), as Vulkan:
    // a UNORM read of a 565 texture would round, not replicate
    auto* dst = static_cast<unsigned char*>(cur().surfStage.cpu());
    const unsigned char* lut = omk::expand565Rgba();
    const int n = std::min<int>(w_ * h_, static_cast<int>(s.px.size()));
    for (int i = 0; i < n; ++i) std::memcpy(dst + 4 * i, lut + 4 * static_cast<std::size_t>(s.px[static_cast<std::size_t>(i)]), 4);
    const DkCopyBuf src{cur().surfStage.gpu(), 0, 0};
    DkImageView v;
    dkImageViewDefaults(&v, &surfImg_);
    const DkImageRect r{0, 0, 0, static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
    dkCmdBufCopyBufferToImage(cb_, &src, &v, &r, 0);
    return runPresent(kImgSurface, 0, h_, true);
}

bool DekoRenderer::presentOverlay(const omk::Surface& s, const unsigned char* mask, const float fade[4], int vy,
                                  int vh) {
    // the planes cover the window: the frame must be the swapchain's size
    if (!swap_ || recording_ || s.w != w_ || s.h != h_ || !mask) return false;
    openCb();   // waits for the slot's last frame, which may still read its staging
    const std::size_t px = static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_);
    auto* st = static_cast<unsigned char*>(cur().overStage.cpu());
    std::memcpy(st, s.px.data(), px * 2);
    std::memcpy(st + px * 2, mask, px);
    return runPresent(kImgColour, vy, vh, false, fade);
}

// ---- THE TEXTURE POOL -----------------------------------------------------

bool DekoRenderer::stage(uint32_t bytes, Alloc& out) {
    const uint32_t need = alignUp(bytes, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
    if (need > ring_.size) {   // past the ring: its own staging, freed after the flush
        out = data_.alloc(need, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
        if (!out) return false;
        upOwn_.push_back(out);
        return true;
    }
    if (ringAt_ + need > ring_.size) flushUploads();   // the ring full: what it holds sent first
    if (!upOpen_) {
        dkCmdBufClear(upCb_);
        dkCmdBufAddMemory(upCb_, upCmdMem_.mb, upCmdMem_.off, upCmdMem_.size);
        upOpen_ = true;
    }
    out = ring_;
    out.off = ring_.off + ringAt_;
    out.size = need;
    out.block = -1;   // a view into the ring, never freed on its own
    ringAt_ += need;
    return true;
}

void DekoRenderer::flushUploads() {
    if (!upOpen_) return;
    // the copies done and the texture cache dropped before any draw samples them
    dkCmdBufBarrier(upCb_, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
    dkQueueSubmitCommands(queue_, dkCmdBufFinishList(upCb_));
    dkQueueWaitIdle(queue_);
    markIdle();
    upOpen_ = false;
    ringAt_ = 0;
    for (Alloc& a : upOwn_) data_.free(a);
    upOwn_.clear();
}

// a pack KTX2 (`o3de/texpack.h` PackBlocks): its levels copied as they are -
// every one when the sampler takes mips, else level 0. False: a format this
// does not know (the texture then comes from the RGBA path)
bool DekoRenderer::uploadBlocks(const omk::PackBlocks& z, DTex& out) {
    DkImageFormat fmt;
    switch (z.vkFormat) {
    case omk::kVkBc1RgbaUnorm: fmt = DkImageFormat_RGBA_BC1; break;
    case omk::kVkBc3Unorm: fmt = DkImageFormat_RGBA_BC3; break;
    case omk::kVkBc4Unorm: fmt = DkImageFormat_R_BC4_Unorm; break;
    case omk::kVkBc7Unorm: fmt = DkImageFormat_RGBA_BC7_Unorm; break;
    case omk::kVkAstc4x4Unorm: fmt = DkImageFormat_RGBA_ASTC_4x4; break;
    case omk::kVkAstc5x5Unorm: fmt = DkImageFormat_RGBA_ASTC_5x5; break;
    case omk::kVkAstc6x6Unorm: fmt = DkImageFormat_RGBA_ASTC_6x6; break;
    case omk::kVkAstc8x8Unorm: fmt = DkImageFormat_RGBA_ASTC_8x8; break;
    default: return false;
    }
    if (z.levels.empty() || z.w < 1 || z.h < 1) return false;
    const uint32_t levels = filter_ >= 2 ? static_cast<uint32_t>(z.levels.size()) : 1u;
    if (!makeImage(out.img, out.mem, fmt, z.w, z.h, 0, levels)) return false;
    for (uint32_t l = 0; l < levels; ++l) {
        const auto [off, len] = z.levels[l];
        if (off + len > z.bytes.size()) break;
        Alloc st;
        if (!stage(static_cast<uint32_t>(len), st)) break;
        std::memcpy(st.cpu(), z.bytes.data() + off, len);
        DkImageView v;
        dkImageViewDefaults(&v, &out.img);
        v.mipLevelOffset = static_cast<uint8_t>(l);
        v.mipLevelCount = 1;
        const DkCopyBuf src{st.gpu(), 0, 0};
        const DkImageRect r{0, 0, 0, std::max(1u, static_cast<uint32_t>(z.w) >> l),
                            std::max(1u, static_cast<uint32_t>(z.h) >> l), 1};
        dkCmdBufCopyBufferToImage(upCb_, &src, &v, &r, 0);
    }
    out.slot = takeSlot();
    if (out.slot != kImgWhite) writeDescriptor(out.slot, out.img);
    return true;
}

// a texture's LIGHT MAP (the pack's `_e`, the bloom's - `scene.frag` reads its
// `.r`): the BC4 block file as it is, or the decoded bytes as R8
bool DekoRenderer::uploadLightMap(const omk::PackImage& rep, DTex& t) {
    if (!rep.emissiveZ.empty()) {
        DTex e;
        if (!uploadBlocks(rep.emissiveZ, e) || e.slot == kImgWhite) return false;
        t.emImg = e.img; t.emMem = e.mem; t.emSlot = e.slot;
        return true;
    }
    if (rep.emissive.empty() || rep.ew < 1 || rep.eh < 1) return false;
    const uint32_t bytes = static_cast<uint32_t>(rep.ew) * static_cast<uint32_t>(rep.eh);
    Alloc st;
    if (!makeImage(t.emImg, t.emMem, DkImageFormat_R8_Unorm, rep.ew, rep.eh, 0) || !stage(bytes, st)) return false;
    std::memcpy(st.cpu(), rep.emissive.data(), std::min<std::size_t>(bytes, rep.emissive.size()));
    DkImageView v;
    dkImageViewDefaults(&v, &t.emImg);
    const DkCopyBuf src{st.gpu(), 0, 0};
    const DkImageRect r{0, 0, 0, static_cast<uint32_t>(rep.ew), static_cast<uint32_t>(rep.eh), 1};
    dkCmdBufCopyBufferToImage(upCb_, &src, &v, &r, 0);
    t.emSlot = takeSlot();
    if (t.emSlot == kImgWhite) { t.emSlot = kImgBlack; return false; }
    writeDescriptor(t.emSlot, t.emImg);
    return true;
}

// ---- THE MAPPED SHADOW (D4d) -------------------------------------------------
//
// Vulkan's `shadowPass`: the light's orthographic basis about the casters'
// centre, a slab `radius` wide, the casters' depth into the map, drawn with
// the SAME matrix the fragment reads it with (`sb.lightMvp`). MEASURED on the
// console (2026-11-27), against the reasoning of `pushView`: drawn with the
// matrix's second row negated (as the scene's projection is), the street
// gained a long false streak of shadow that Vulkan's frame has none of
// (2441 px darker than the fitted frame, against 385 of noise unflipped) -
// so into this offscreen target deko3d's `OriginUpperLeft` maps Y as
// Vulkan does (`todo/deko3d.md` D4d; why the scene's needs the negation is
// open). `OMK_DEKO_SHADOW_FLIP=1` draws it negated, the A/B. And the casters
// are posed here, positions alone, into one buffer (below).
void DekoRenderer::shadowPass(const omk::View& v, std::span<const omk::Draw> casters) {
    if (!dev_ || !v.shadow.on || casters.empty()) return;
    if (!shadowMade_) {
        if (!makeImage(shadow_, shadowMem_, DkImageFormat_ZF32, kShadowSide, kShadowSide,
                       DkImageFlags_UsageRender)) {   // (sampled: no depth compression)
            std::printf("deko3d: the shadow map could not be made - no mapped shadow\n");
            shadowMade_ = true;   // said once; `shadow_` stays empty
            return;
        }
        dkImageViewDefaults(&shadowView_, &shadow_);
        writeDescriptor(kImgShadow, shadow_);
        shadowMade_ = true;
        std::printf("deko3d: the mapped shadow - a %dx%d depth map\n", kShadowSide, kShadowSide);
    }
    if (!shadowMem_) return;
    const float R = v.shadow.radius > 1.0f ? v.shadow.radius : 1.0f;
    float f[3] = {v.shadow.dir[0], v.shadow.dir[1], v.shadow.dir[2]};
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl < 1e-6f) return;
    for (float& c : f) c /= fl;
    float up[3] = {0.0f, -1.0f, 0.0f};
    if (std::fabs(f[0] * up[0] + f[1] * up[1] + f[2] * up[2]) > 0.99f) {
        up[0] = 1.0f; up[1] = 0.0f; up[2] = 0.0f;
    }
    float r[3] = {up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2], up[0] * f[1] - up[1] * f[0]};
    const float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (float& c : r) c /= rl;
    const float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    const float back = R * 2.0f;
    float eye[3];
    for (int k = 0; k < 3; ++k) eye[k] = v.shadow.centre[k] - f[k] * back;
    const float far = back + R * 2.0f;
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
    std::memcpy(shadowMvp_, m, sizeof m);
    shadowStrength_ = v.shadow.strength;
    float drawM[16];
    std::memcpy(drawM, m, sizeof m);
    // (`OMK_DEKO_SHADOW_FLIP=1`, the A/B: the second row negated, as the
    // scene's projection is - the false streak)
    if (omk::envInt("OMK_DEKO_SHADOW_FLIP", 0) != 0)
        for (int c = 0; c < 4; ++c) drawM[c * 4 + 1] = -drawM[c * 4 + 1];

    // the frame's list, opened here (`begin` then finds it open)
    if (cbOpen_ && sceneSubmitted_) {
        dkQueueWaitIdle(queue_);
        markIdle();
        cbOpen_ = false;
    }
    openCb();
    // THE CASTERS' POSITIONS, one buffer for the frame: each draw's own range
    // posed (`cornerAffine`, positions alone - depth needs no normal and no
    // light). Not Vulkan's `cpuPose`: that poses and LIGHTS every corner of
    // the draw's whole geometry, then uploads it whole, once a caster - 54 ms
    // of CPU a frame for the street's nine bodies on the console (2026-11-27)
    std::size_t total = 0;
    for (const auto& d : casters)
        if (d.geo && d.count) total += std::min(d.count, d.geo->corners.size() - std::min(d.start, d.geo->corners.size()));
    if (total == 0) return;
    Alloc pos = data_.alloc(alignUp(static_cast<uint32_t>(total * 12), 256), 256);
    if (!pos) return;
    cur().extra.push_back(pos);
    float* out = static_cast<float*>(pos.cpu());
    for (const auto& d : casters) {
        if (!d.geo || !d.count) continue;
        const omk::Geometry& g = *d.geo;
        const bool posed = d.meshPose && d.meshPoses && g.cornerMesh.size() == g.corners.size();
        const std::size_t e = std::min(d.start + d.count, g.corners.size());
        for (std::size_t i = std::min(d.start, e); i < e; ++i, out += 3) {
            const omk::Corner& c = g.corners[i];
            if (const float* a = posed ? omk::cornerAffine(d, i) : nullptr) omk::posePoint(a, c.x, c.y, c.z, out);
            else { out[0] = c.x; out[1] = c.y; out[2] = c.z; }
        }
    }

    // the frame before's fragments done sampling the map before it is drawn over
    dkCmdBufBarrier(cb_, DkBarrier_Fragments, 0);
    dkCmdBufBindRenderTargets(cb_, nullptr, 0, &shadowView_);
    const DkViewport vp{0.0f, 0.0f, static_cast<float>(kShadowSide), static_cast<float>(kShadowSide), 0.0f, 1.0f};
    const DkScissor all{0, 0, static_cast<uint32_t>(kShadowSide), static_cast<uint32_t>(kShadowSide)};
    dkCmdBufSetViewports(cb_, 0, &vp, 1);
    dkCmdBufSetScissors(cb_, 0, &all, 1);
    dkCmdBufClearDepthStencil(cb_, true, 1.0f, 0, 0);
    const DkShader* sh[1] = {&shShadow_};
    dkCmdBufBindShaders(cb_, DkStageFlag_GraphicsMask, sh, 1);   // depth alone: no fragment stage
    lastFrag_ = nullptr;
    DkRasterizerState rast;
    dkRasterizerStateDefaults(&rast);
    rast.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(cb_, &rast);
    DkDepthStencilState ds;
    dkDepthStencilStateDefaults(&ds);
    ds.depthTestEnable = 1;
    ds.depthWriteEnable = 1;
    ds.depthCompareOp = DkCompareOp_Lequal;
    dkCmdBufBindDepthStencilState(cb_, &ds);
    DkVtxAttribState a;
    std::memset(&a, 0, sizeof a);
    a.bufferId = 0;
    a.offset = 0;
    a.size = DkVtxAttribSize_3x32;
    a.type = DkVtxAttribType_Float;
    dkCmdBufBindVtxAttribState(cb_, &a, 1);
    const DkVtxBufferState vb{12, 0};
    dkCmdBufBindVtxBufferState(cb_, &vb, 1);
    dkCmdBufBindUniformBuffer(cb_, DkStage_Vertex, kUboPush, pushUbo_.gpu(), pushUbo_.size);
    dkCmdBufPushConstants(cb_, pushUbo_.gpu(), pushUbo_.size, 0, sizeof drawM, drawM);
    dkCmdBufBindVtxBuffer(cb_, 0, pos.gpu(), pos.size);
    dkCmdBufDraw(cb_, DkPrimitive_Triangles, static_cast<uint32_t>(total), 1, 0, 0);
    // the map's depth written before the scene's fragments sample it
    dkCmdBufBarrier(cb_, DkBarrier_Fragments, DkInvalidateFlags_Image);
    shadowLive_ = true;
}

// ---- THE POST-PASS (D4c) ---------------------------------------------------

bool DekoRenderer::makePost() {
    if (postMade_) return true;
    gw_ = std::max(1, w_ / 4);
    gh_ = std::max(1, h_ / 4);
    // the scene's picture in RGBA8, the glow's two in half floats (Vulkan's)
    if (!makeImage(postSrc_, postSrcMem_, DkImageFormat_RGBA8_Unorm, w_, h_, DkImageFlags_UsageRender) ||
        !makeImage(glow_[0], glowMem_[0], DkImageFormat_RGBA16_Float, gw_, gh_, DkImageFlags_UsageRender) ||
        !makeImage(glow_[1], glowMem_[1], DkImageFormat_RGBA16_Float, gw_, gh_, DkImageFlags_UsageRender)) {
        std::printf("deko3d: the post-pass's images could not be made - no bloom, no grade\n");
        bloom_ = 0.0f;
        lutN_ = 0;
        return false;
    }
    dkImageViewDefaults(&postSrcView_, &postSrc_);
    dkImageViewDefaults(&glowView_[0], &glow_[0]);
    dkImageViewDefaults(&glowView_[1], &glow_[1]);
    writeDescriptor(kImgPostSrc, postSrc_);
    writeDescriptor(kImgGlow0, glow_[0]);
    writeDescriptor(kImgGlow1, glow_[1]);
    postMade_ = true;
    std::printf("deko3d: the post-pass - the glow at %dx%d, a grade's LUT when one is set\n", gw_, gh_);
    return true;
}

// the grade's LUT, its N^3 entries as an N*N x N strip (`omk::lutStrip`)
bool DekoRenderer::uploadLut() {
    lutDirty_ = false;
    if (lutN_ < 2) return false;
    const int w = lutN_ * lutN_, h = lutN_;
    if (lutMadeN_ != lutN_) {
        if (lutMadeN_) { dkQueueWaitIdle(queue_); markIdle(); images_.free(lutMem_); }
        lutMadeN_ = 0;
        if (!makeImage(lut_, lutMem_, DkImageFormat_RGBA8_Unorm, w, h, 0)) return false;
        writeDescriptor(kImgLut, lut_);
        lutMadeN_ = lutN_;
    }
    Alloc st;
    const uint32_t bytes = static_cast<uint32_t>(lutData_.size());
    if (bytes < static_cast<uint32_t>(w * h * 4) || !stage(bytes, st)) return false;
    std::memcpy(st.cpu(), lutData_.data(), bytes);
    DkImageView v;
    dkImageViewDefaults(&v, &lut_);
    const DkCopyBuf src{st.gpu(), 0, 0};
    const DkImageRect r{0, 0, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    dkCmdBufCopyBufferToImage(upCb_, &src, &v, &r, 0);
    flushUploads();
    return true;
}

// one pass of `post.frag` into `rt` (`w` x `h`): `src` and `glow` image
// descriptors, the push block Vulkan's (`PostPush`)
void DekoRenderer::postPass(const DkImageView* rt, int w, int h, uint32_t src, uint32_t glow, int srcW, int srcH,
                            float dx, float dy, int mode, float strength, float halo) {
    dkCmdBufBindRenderTargets(cb_, &rt, 1, nullptr);
    const DkViewport vp{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
    const DkScissor sc{0, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    dkCmdBufSetViewports(cb_, 0, &vp, 1);
    dkCmdBufSetScissors(cb_, 0, &sc, 1);
    dkCmdBufBindTexture(cb_, DkStage_Fragment, 0, dkMakeTextureHandle(src, kSampLinearClamp));
    dkCmdBufBindTexture(cb_, DkStage_Fragment, 1, dkMakeTextureHandle(glow, kSampLinearClamp));
    dkCmdBufBindTexture(cb_, DkStage_Fragment, 2,
                        dkMakeTextureHandle(lutMadeN_ > 1 ? kImgLut : kImgBlack, kSampLinearClamp));
    dkCmdBufBindTexture(cb_, DkStage_Fragment, 3, dkMakeTextureHandle(kImgWhite, kSampLinearClamp));
    const PostPush p{{1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h)},
                     {1.0f / static_cast<float>(srcW), 1.0f / static_cast<float>(srcH)},
                     {dx, dy}, strength, mode, lutN_ > 1 && lutMadeN_ == lutN_ ? lutN_ : 0, 0, halo};
    dkCmdBufPushConstants(cb_, pushUbo_.gpu(), pushUbo_.size, 0, sizeof p, &p);
    dkCmdBufDraw(cb_, DkPrimitive_Triangles, 3, 1, 0, 0);
    // its writes done before the next pass samples them
    dkCmdBufBarrier(cb_, DkBarrier_Fragments, DkInvalidateFlags_Image);
}

// Vulkan's `recordPost`: the glow (only where a draw bound a light map) - its
// extraction at a quarter of the size, the two blurs, the halos in the rain -
// then the final pass: the scene, the glow over it, the grade, into `colour_`
void DekoRenderer::recordPost() {
    const bool glow = bloom_ > 0.0f && lightMapNow_;
    const bool halos = glow && halo_ > 0.0f;
    dkCmdBufBarrier(cb_, DkBarrier_Fragments, DkInvalidateFlags_Image);   // the scene's writes, sampled next
    const DkShader* sh[2] = {&shFsq_, &shPost_};
    dkCmdBufBindShaders(cb_, DkStageFlag_GraphicsMask, sh, 2);
    lastFrag_ = nullptr;
    DkColorState cs;
    dkColorStateDefaults(&cs);
    dkCmdBufBindColorState(cb_, &cs);
    DkDepthStencilState ds;
    dkDepthStencilStateDefaults(&ds);
    ds.depthTestEnable = 0;
    ds.depthWriteEnable = 0;
    dkCmdBufBindDepthStencilState(cb_, &ds);
    lastBlend_ = -1;
    dkCmdBufBindVtxAttribState(cb_, nullptr, 0);
    dkCmdBufBindVtxBufferState(cb_, nullptr, 0);
    dkCmdBufBindUniformBuffer(cb_, DkStage_Fragment, kUboPush, pushUbo_.gpu(), pushUbo_.size);
    if (glow) {
        postPass(&glowView_[0], gw_, gh_, kImgPostSrc, kImgBlack, w_, h_, 0.0f, 0.0f, 0, 0.0f, 0.0f);
        postPass(&glowView_[1], gw_, gh_, kImgGlow0, kImgBlack, gw_, gh_, 1.0f, 0.0f, 1, 0.0f, 0.0f);
        postPass(&glowView_[0], gw_, gh_, kImgGlow1, kImgBlack, gw_, gh_, 0.0f, 1.0f, 1, 0.0f, 0.0f);
        // THE HALOS at the glow's size, once (`post.frag` mode 3: `srcInv` the frame's)
        if (halos) postPass(&glowView_[1], gw_, gh_, kImgGlow0, kImgBlack, w_, h_, 0.0f, 0.0f, 3, 0.0f, halo_);
    }
    postPass(&colourView_, w_, h_, kImgPostSrc, halos ? kImgGlow1 : glow ? kImgGlow0 : kImgBlack, w_, h_, 0.0f, 0.0f,
             2, glow ? bloom_ : 0.0f, 0.0f);
}

void DekoRenderer::poolTextures(std::span<const omk::Texture> t) {
    if (!dev_) return;
    // THE POOL, UPLOADED ONLY WHERE IT IS NEW (Vulkan's rule): a texture
    // already on the device - the same pixel storage, palette and size - keeps
    // its image; what no slot of the new pool uses is released, after a wait
    const auto keyOf = [](const omk::Texture& x) { return Key{x.idx.data(), x.pal.data(), x.width, x.height}; };
    for (auto& [key, u] : uploaded_) u.used = false;
    for (const auto& x : t)
        if (x.hasPixels())
            if (auto hit = uploaded_.find(keyOf(x)); hit != uploaded_.end()) hit->second.used = true;
    bool waited = false;
    for (auto it = uploaded_.begin(); it != uploaded_.end();) {
        if (it->second.used) { ++it; continue; }
        if (!waited) { dkQueueWaitIdle(queue_); markIdle(); waited = true; }
        images_.free(it->second.t->mem);
        if (it->second.t->slot >= kImgFirst) freeSlots_.push_back(it->second.t->slot);
        if (it->second.t->emSlot >= kImgFirst) {
            images_.free(it->second.t->emMem);
            freeSlots_.push_back(it->second.t->emSlot);
        }
        it = uploaded_.erase(it);
    }
    texSlot_.assign(t.size(), kImgWhite);
    texOf_.assign(t.size(), nullptr);
    // THE TEXTURE PACK (`o3de/texpack.h`), consulted only when one is shown:
    // every texture this pool brings that the device does not hold yet,
    // decoded together on the thread pool (Vulkan's)
    const omk::TexturePack& pack = omk::texturePack();
    std::vector<omk::PackImage> reps;
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    if (pack.shown()) {
        std::vector<const omk::Texture*> toDecode(t.size(), nullptr);
        for (std::size_t i = 0; i < t.size(); ++i)
            if (t[i].hasPixels() && !uploaded_.count(keyOf(t[i]))) toDecode[i] = &t[i];
        reps = pack.replacements(toDecode);
    }
    const auto t1 = clk::now();
    int fresh = 0, packed = 0, blocks = 0, lit = 0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const omk::Texture& x = t[i];
        if (!x.hasPixels()) continue;
        auto hit = uploaded_.find(keyOf(x));
        if (hit == uploaded_.end()) {
            Uploaded u;
            u.idx = x.idx; u.pal = x.pal; u.used = true;
            u.t = std::make_unique<DTex>();
            const bool hasRep = pack.shown() && i < reps.size() && reps[i].ok;
            if (hasRep && !reps[i].colourZ.empty() && uploadBlocks(reps[i].colourZ, *u.t)) {
                // the pack's BLOCK file, its levels as they are (BC7 / ASTC)
                u.t->packed = true;
                u.t->su = static_cast<float>(reps[i].w) / static_cast<float>(x.width);
                u.t->sv = static_cast<float>(reps[i].h) / static_cast<float>(x.height);
                ++blocks; ++fresh; ++packed;
                lit += uploadLightMap(reps[i], *u.t) ? 1 : 0;
                hit = uploaded_.emplace(keyOf(x), std::move(u)).first;
                texSlot_[i] = hit->second.t->slot;
                texOf_[i] = hit->second.t.get();
                continue;
            }
            // ...else the pack's RGBA replacement, or the shipped texture
            const bool rgbaRep = hasRep && reps[i].colourZ.empty() && !reps[i].rgba.empty();
            const int tw = rgbaRep ? reps[i].w : x.width, th = rgbaRep ? reps[i].h : x.height;
            Alloc st;
            const uint32_t bytes = static_cast<uint32_t>(tw) * static_cast<uint32_t>(th) * 4;
            if (makeImage(u.t->img, u.t->mem, DkImageFormat_RGBA8_Unorm, tw, th, 0) && stage(bytes, st)) {
                if (rgbaRep) {
                    std::memcpy(st.cpu(), reps[i].rgba.data(), std::min<std::size_t>(bytes, reps[i].rgba.size()));
                    u.t->packed = true;
                    u.t->su = static_cast<float>(tw) / static_cast<float>(x.width);
                    u.t->sv = static_cast<float>(th) / static_cast<float>(x.height);
                } else {
                    // alpha the COLOUR KEY: 0 where the texel is black - the cutout's discard
                    x.toRgbaKeyed(static_cast<std::uint8_t*>(st.cpu()));
                }
                DkImageView v;
                dkImageViewDefaults(&v, &u.t->img);
                const DkCopyBuf src{st.gpu(), 0, 0};
                const DkImageRect r{0, 0, 0, static_cast<uint32_t>(tw), static_cast<uint32_t>(th), 1};
                dkCmdBufCopyBufferToImage(upCb_, &src, &v, &r, 0);
                u.t->slot = takeSlot();
                if (u.t->slot != kImgWhite) writeDescriptor(u.t->slot, u.t->img);
                ++fresh;
            }
            if (hasRep) lit += uploadLightMap(reps[i], *u.t) ? 1 : 0;
            hit = uploaded_.emplace(keyOf(x), std::move(u)).first;
        }
        texSlot_[i] = hit->second.t->slot;
        texOf_[i] = hit->second.t.get();
        packed += hit->second.t->packed ? 1 : 0;
    }
    flushUploads();
    // the glow's condition: a light map in the pool (Vulkan's `poolLit_`)
    poolLit_ = std::any_of(texOf_.begin(), texOf_.end(), [](const DTex* d) { return d && d->emSlot != kImgBlack; });
    const double decodeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double uploadMs = std::chrono::duration<double, std::milli>(clk::now() - t1).count();
    if (fresh > 0)
        std::printf("deko3d: texture pool - %d new of %zu (%d from the pack's block files), %d replaced by the "
                    "pack, %d light maps; the pack's read and decode %.1f ms, the uploads %.1f ms; images %llu of "
                    "%llu KB\n", fresh, t.size(), blocks, packed, lit, decodeMs, uploadMs,
                    static_cast<unsigned long long>(images_.used() >> 10),
                    static_cast<unsigned long long>(images_.total() >> 10));
}

}  // namespace

// ---- the entry points (`backends/sdl/playgpu_deko.cpp`) -------------------
namespace omk {
Renderer* makeDekoRenderer() { return new DekoRenderer(); }
bool dekoPresentWorld(Renderer* r, int vy, int vh) {
    return static_cast<DekoRenderer*>(r)->presentWorld(vy, vh);
}
bool dekoPresentSurface(Renderer* r, const Surface& s) {
    return static_cast<DekoRenderer*>(r)->presentSurface(s);
}
const char* dekoDeviceName(Renderer* r) { return static_cast<DekoRenderer*>(r)->device(); }
void dekoSetSwapInterval(Renderer* r, int n) { static_cast<DekoRenderer*>(r)->setSwapInterval(n); }
bool dekoPresentOverlay(Renderer* r, const Surface& s, const unsigned char* mask, const float fade[4], int vy, int vh) {
    return static_cast<DekoRenderer*>(r)->presentOverlay(s, mask, fade, vy, vh);
}
void dekoTakeCpu(Renderer* r, double out[6]) { static_cast<DekoRenderer*>(r)->takeCpu(out); }
void dekoTakeCounts(Renderer* r, long out[6]) { static_cast<DekoRenderer*>(r)->takeCounts(out); }
std::string dekoTakeLadder(Renderer* r) { return static_cast<DekoRenderer*>(r)->takeLadder(); }
}  // namespace omk

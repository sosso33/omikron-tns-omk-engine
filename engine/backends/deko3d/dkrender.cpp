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
#include "ui/surface.h"
#include "platform/env.h"
#include "../vulkan/shaderblocks.h"

#include <algorithm>
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
enum : uint32_t { kImgWhite = 0, kImgBlack = 1, kImgFlat = 2, kImgColour = 3, kImgSurface = 4, kImgFirst = 8 };
// the samplers: the pool's filter (0 nearest - the original's software
// devices - 1 bilinear, its 3D cards'), and the present's nearest clamp
enum : uint32_t { kSampNearest = 0, kSampLinear = 1, kSampClamp = 2, kSampCount = 3 };

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
    const omk::Surface& readback() override;
    omk::RasterStats stats() const override { return st_; }
    const char* name() const override { return "deko3d"; }
    unsigned takes() const override { return omk::kTakesTextureFilter; }
    bool setTextureFilter(int mode) override {
        if (dev_) return false;   // the samplers exist
        filter_ = mode < 1 ? 0 : 1;   // trilinear needs the mip chain: D4
        return true;
    }
    bool bakeDepthTie(const omk::Geometry* g, std::span<const omk::Draw> order) override;

    bool presentWorld(int vy, int vh);
    bool presentSurface(const omk::Surface& s);
    const char* device() const { return "Tegra X1 (deko3d)"; }

private:
    static void geometryGone(void* self, const omk::Geometry* g);
    static void addCmdMem(void* user, DkCmdBuf cb, size_t minReq);
    bool loadShader(const char* name, DkShader& sh);
    bool makeImage(DkImage& img, Alloc& mem, DkImageFormat fmt, int w, int h, uint32_t flags);
    void writeDescriptor(uint32_t slot, const DkImage& img);
    uint32_t takeSlot();
    void waitFrame();
    void openCb();
    void bindFrameState();
    void pushView(const omk::View& v);
    bool uploadGeometry(const omk::Geometry* g);
    void poolTextures(std::span<const omk::Texture> t);
    bool stage(uint32_t bytes, Alloc& out);
    void flushUploads();
    bool runPresent(uint32_t imgSlot, int vy, int vh, bool raw);

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
    Alloc cmdMem_, upCmdMem_;
    std::vector<Alloc> cmdExtra_;   // what the frame's list outgrew into, freed with the frame
    DkFence frameFence_{};
    bool framePending_ = false;     // the last present's fence not waited for yet
    bool cbOpen_ = false;           // `cb_` holds this frame's commands
    bool recording_ = false;        // between `begin` and `end`
    bool sceneSubmitted_ = false;   // `end` sent a scene that no present has closed
    bool dirty_ = false;            // a scene ended that `readback` has not read

    // ---- the shaders (Vulkan's, `dkshaders.py`)
    DkShader shVert_{}, shFrag_{}, shFragCut_{}, shFsq_{}, shPresent_{};
    std::vector<Alloc> shaderMem_;

    // ---- the descriptors
    Alloc imgDesc_, sampDesc_;
    std::vector<uint32_t> freeSlots_;
    uint32_t nextSlot_ = kImgFirst;
    int filter_ = 0;

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
    Alloc pushUbo_, shadowUbo_, lightUbo_, poseBuf_, readBuf_, surfStage_;
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
    };
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
    struct Vbo {
        Alloc mem;
        std::size_t n = 0;
        std::uint64_t rev = 0;
    };
    std::map<const omk::Geometry*, Vbo> vbo_;
    std::vector<Alloc> deadVbo_;      // released once the frame that may draw them is done
    std::map<const omk::Geometry*, std::vector<std::uint8_t>> baked_;
};

// ---------------------------------------------------------------------------

void DekoRenderer::addCmdMem(void* user, DkCmdBuf cb, size_t minReq) {
    auto* self = static_cast<DekoRenderer*>(user);
    Alloc a = self->data_.alloc(std::max<uint32_t>(kCmdGrow, alignUp(static_cast<uint32_t>(minReq), 0x1000)),
                                DK_CMDMEM_ALIGNMENT);
    if (!a) return;   // the device reports the overflow itself
    dkCmdBufAddMemory(cb, a.mb, a.off, a.size);
    self->cmdExtra_.push_back(a);
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

bool DekoRenderer::makeImage(DkImage& img, Alloc& mem, DkImageFormat fmt, int w, int h, uint32_t flags) {
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, dev_);
    lm.flags = flags;
    lm.format = fmt;
    lm.dimensions[0] = static_cast<uint32_t>(w);
    lm.dimensions[1] = static_cast<uint32_t>(h);
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
    cmdMem_ = data_.alloc(kCmdBytes, DK_CMDMEM_ALIGNMENT);
    upCmdMem_ = data_.alloc(kUpCmdBytes, DK_CMDMEM_ALIGNMENT);
    ring_ = data_.alloc(kStagingBytes, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
    if (!cb_ || !upCb_ || !cmdMem_ || !upCmdMem_ || !ring_) return false;

    if (!loadShader("scene.vert", shVert_) || !loadShader("scene.frag", shFrag_) ||
        !loadShader("scene_cut.frag", shFragCut_) || !loadShader("fsq.vert", shFsq_) ||
        !loadShader("present.frag", shPresent_))
        return false;

    // the descriptors: one array of images, one of samplers
    imgDesc_ = data_.alloc(kMaxImages * sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT);
    sampDesc_ = data_.alloc(kSampCount * sizeof(DkSamplerDescriptor), DK_SAMPLER_DESCRIPTOR_ALIGNMENT);
    if (!imgDesc_ || !sampDesc_) return false;
    std::memset(imgDesc_.cpu(), 0, imgDesc_.size);
    {
        auto* sd = static_cast<DkSamplerDescriptor*>(sampDesc_.cpu());
        DkSampler s;
        dkSamplerDefaults(&s);   // nearest, REPEAT - `raster.cpp`'s `% t.width`
        dkSamplerDescriptorInitialize(&sd[kSampNearest], &s);
        s.minFilter = s.magFilter = DkFilter_Linear;
        dkSamplerDescriptorInitialize(&sd[kSampLinear], &s);
        dkSamplerDefaults(&s);
        s.wrapMode[0] = s.wrapMode[1] = s.wrapMode[2] = DkWrapMode_ClampToEdge;
        dkSamplerDescriptorInitialize(&sd[kSampClamp], &s);
    }

    // the targets: the scene's colour (read by the present pass and the
    // readback's copy) and depth-stencil, the uploaded surface, the swapchain
    if (!makeImage(colour_, colourMem_, DkImageFormat_RGBA8_Unorm, w, h,
                   DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine) ||
        !makeImage(depth_, depthMem_, DkImageFormat_Z24S8, w, h,
                   DkImageFlags_UsageRender | DkImageFlags_HwCompression) ||
        !makeImage(surfImg_, surfMem_, DkImageFormat_RGBA8_Unorm, w, h, DkImageFlags_Usage2DEngine))
        return false;
    dkImageViewDefaults(&colourView_, &colour_);
    dkImageViewDefaults(&depthView_, &depth_);
    writeDescriptor(kImgColour, colour_);
    writeDescriptor(kImgSurface, surfImg_);
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
    surfStage_ = data_.alloc(static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 4, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
    if (!pushUbo_ || !shadowUbo_ || !lightUbo_ || !poseBuf_ || !readBuf_ || !surfStage_) return false;
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
    std::printf("deko3d: up - %dx%d, %d swapchain images, filter %s\n", w, h, kSwapImages,
                filter_ ? "bilinear" : "nearest");
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
        r->deadVbo_.push_back(it->second.mem);   // a frame in flight may still draw it
        r->vbo_.erase(it);
    }
    r->baked_.erase(g);
}

// ---- THE FRAME ------------------------------------------------------------

void DekoRenderer::waitFrame() {
    if (framePending_) {
        DKTRACE("waiting for the frame's fence\n");
        const DkResult r = dkFenceWait(&frameFence_, kFenceTimeoutNs);
        if (r != DkResult_Success) {
            std::printf("deko3d: the frame's fence did not signal in 2 s (result %d, queue %s)\n",
                        static_cast<int>(r), dkQueueIsInErrorState(queue_) ? "IN ERROR" : "not in error");
            std::fflush(stdout);
            dkQueueWaitIdle(queue_);
        }
        framePending_ = false;
    }
}

// the frame's command list, from the start of its memory once the GPU is done with it
void DekoRenderer::openCb() {
    if (cbOpen_) return;
    waitFrame();
    for (Alloc& a : cmdExtra_) data_.free(a);
    cmdExtra_.clear();
    for (Alloc& a : deadVbo_) data_.free(a);
    deadVbo_.clear();
    dkCmdBufClear(cb_);
    dkCmdBufAddMemory(cb_, cmdMem_.mb, cmdMem_.off, cmdMem_.size);
    cbOpen_ = true;
    sceneSubmitted_ = false;
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
void DekoRenderer::bindFrameState() {
    const DkImageView* rt = &colourView_;
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
    // the stand-ins under the light map, the reflection and the shadow map
    const uint32_t samp = filter_ ? kSampLinear : kSampNearest;
    dkCmdBufBindTexture(cb_, DkStage_Fragment, kTexEmis, dkMakeTextureHandle(kImgBlack, samp));
    dkCmdBufBindTexture(cb_, DkStage_Fragment, kTexRefl, dkMakeTextureHandle(kImgBlack, samp));
    dkCmdBufBindTexture(cb_, DkStage_Fragment, kTexShadow, dkMakeTextureHandle(kImgBlack, kSampClamp));
    lastBlend_ = -1;
}

void DekoRenderer::begin(const omk::View& view) {
    if (!dev_) return;
    // a scene the last frame sent and nothing presented (a readback alone, or
    // `OMK_NO_GPU_PRESENT`): done once the queue is, and its list started over
    if (cbOpen_ && sceneSubmitted_) {
        dkQueueWaitIdle(queue_);
        framePending_ = false;
        cbOpen_ = false;
    }
    openCb();
    st_ = omk::RasterStats{};
    fog_ = view.fog;
    fogStart_ = view.fogStart;
    fogEnd_ = view.fogEnd;
    mistOn_ = view.mist > 0.0f || view.dust > 0.0f || view.veil > 0.0f;
    for (int i = 0; i < 3; ++i) fogColour_[i] = static_cast<float>(view.fogColour[i]) / 255.0f;
    rain_ = view.rain;
    dither_ = view.dither;
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
    sb.texel = 1.0f / 1024.0f;    // Vulkan's kShadowSide; the strength 0 - no shadow pass yet (D4)
    sb.shimmer = view.shimmerClock;

    bindFrameState();
    dkCmdBufPushConstants(cb_, lightUbo_.gpu(), lightUbo_.size, 0, sizeof ub, &ub);
    dkCmdBufPushConstants(cb_, shadowUbo_.gpu(), shadowUbo_.size, 0, sizeof sb, &sb);

    // the clear: the scene's fog colour (`View::clearColour`), depth far, stencil 0
    const DkScissor all{0, 0, static_cast<uint32_t>(w_), static_cast<uint32_t>(h_)};
    dkCmdBufSetScissors(cb_, 0, &all, 1);
    dkCmdBufClearColorFloat(cb_, 0, DkColorMask_RGBA, view.clearColour[0] / 255.0f, view.clearColour[1] / 255.0f,
                            view.clearColour[2] / 255.0f, 1.0f);
    dkCmdBufClearDepthStencil(cb_, true, 1.0f, 0xFF, 0);
    // the letterbox is a VIEWPORT: the picture in the top-left `vw x vh`
    picH_ = view.letterboxed() ? view.vh : h_;
    const int vw = view.letterboxed() ? view.vw : w_;
    const DkViewport vp{0.0f, 0.0f, static_cast<float>(vw), static_cast<float>(picH_), 0.0f, 1.0f};
    const DkScissor sc{0, 0, static_cast<uint32_t>(std::min(vw, w_)), static_cast<uint32_t>(picH_)};
    dkCmdBufSetViewports(cb_, 0, &vp, 1);
    dkCmdBufSetScissors(cb_, 0, &sc, 1);
    recording_ = true;
}

bool DekoRenderer::uploadGeometry(const omk::Geometry* g) {
    // cached by pointer while the CONTENT has not moved (`Geometry::revision`);
    // the frame before is done (`openCb` waited), so a buffer that still fits
    // is rewritten in place
    auto it = vbo_.find(g);
    if (it != vbo_.end() && it->second.rev == g->revision && it->second.n == g->corners.size()) return true;
    const std::size_t n = g->corners.size();
    if (!n) return false;
    const auto bk = baked_.find(g);
    const std::vector<std::uint8_t>* lose = bk == baked_.end() ? nullptr : &bk->second;
    const uint32_t bytes = static_cast<uint32_t>(n * sizeof(GpuVert));
    if (it != vbo_.end() && it->second.mem.size < bytes) {
        deadVbo_.push_back(it->second.mem);
        vbo_.erase(it);
        it = vbo_.end();
    }
    if (it == vbo_.end()) {
        Vbo v;
        // half as much again for a geometry that has grown once (Vulkan's rule)
        v.mem = data_.alloc(bytes, 256);
        if (!v.mem) return false;
        it = vbo_.emplace(g, std::move(v)).first;
        g->resident.mark();
    }
    auto* dst = static_cast<GpuVert*>(it->second.mem.cpu());
    for (std::size_t i = 0; i < n; ++i) dst[i] = vertOf(*g, i, lose);
    it->second.n = n;
    it->second.rev = g->revision;
    return true;
}

bool DekoRenderer::bakeDepthTie(const omk::Geometry* g, std::span<const omk::Draw> order) {
    if (omk::tieBakeOff() || !g) return false;
    const std::vector<std::size_t> tris = omk::bakeTieLosers(*g, order);
    std::vector<std::uint8_t> lose(g->corners.size(), 0);
    for (const std::size_t tri : tris)
        for (std::size_t k = 3 * tri; k < 3 * tri + 3 && k < lose.size(); ++k) lose[k] = 1;
    baked_[g] = std::move(lose);
    if (auto it = vbo_.find(g); it != vbo_.end()) it->second.rev = ~std::uint64_t{0};   // rewritten marked
    return true;
}

void DekoRenderer::submit(const omk::Draw& d) {
    if (!recording_ || !d.geo || !d.count) return;
    if (!uploadGeometry(d.geo)) return;
    st_.triangles += static_cast<long>(d.count / 3);

    // THE BLEND - `omk::blendRule`, Vulkan's three pipelines as three states
    const int k = d.blend == omk::Blend::Opaque ? 0 : d.blend == omk::Blend::Add ? 1 : 2;
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
    // an opaque CUTOUT draw: the shader with the late depth (`scene.frag` OMK_LATE_DEPTH)
    const DkShader* frag = d.cutout && k == 0 ? &shFragCut_ : &shFrag_;
    if (frag != lastFrag_) {
        const DkShader* sh[1] = {frag};
        dkCmdBufBindShaders(cb_, DkStageFlag_Fragment, sh, 1);
        lastFrag_ = frag;
    }
    // THE TEXTURE: the key's low six bits, or the pool's index above them
    const std::size_t slot = omk::drawTextureSlot(d);
    const uint32_t img = slot < texSlot_.size() ? texSlot_[slot] : kImgWhite;
    dkCmdBufBindTexture(cb_, DkStage_Fragment, kTexTex, dkMakeTextureHandle(img, filter_ ? kSampLinear : kSampNearest));

    push_.posed = 0;
    push_.cutout = d.cutout ? 1 : 0;
    push_.caster = d.castsShadow ? 1 : 0;
    push_.bloomMode = 0;
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
    push_.texScaled = 0;
    push_.texScaleU = push_.texScaleV = 1.0f;
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
    const Vbo& vb = vbo_.find(d.geo)->second;
    dkCmdBufBindVtxBuffer(cb_, 0, vb.mem.gpu(), vb.mem.size);
    // THE BACK-FACE CULL, one push per run of `cornerCull`
    const omk::Geometry* g = d.geo;
    const bool haveCull = g->cornerCull.size() == g->corners.size();
    std::size_t i = d.start;
    const std::size_t e = std::min(d.start + d.count, g->corners.size());
    while (i < e) {
        const std::uint8_t c = haveCull ? g->cornerCull[i] : 0u;
        std::size_t j = i;
        while (j < e && (haveCull ? g->cornerCull[j] : 0u) == c) ++j;
        push_.cull = c ? (flipX_ ? -cullSign() : cullSign()) : 0;
        dkCmdBufPushConstants(cb_, pushUbo_.gpu(), pushUbo_.size, 0, sizeof(Push), &push_);
        dkCmdBufDraw(cb_, DkPrimitive_Triangles, static_cast<uint32_t>(j - i), 1, static_cast<uint32_t>(i), 0);
        i = j;
    }
    st_.drawn += static_cast<long>(d.count / 3);
}

void DekoRenderer::end() {
    if (!recording_) return;
    recording_ = false;
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
    dkCmdBufBarrier(cb_, DkBarrier_Full, 0);
    const DkImageRect r{0, 0, 0, static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
    const DkCopyBuf dst{readBuf_.gpu(), 0, 0};
    dkCmdBufCopyImageToBuffer(cb_, &colourView_, &r, &dst, 0);
    dkQueueSubmitCommands(queue_, dkCmdBufFinishList(cb_));
    dkQueueWaitIdle(queue_);
    framePending_ = false;   // the queue is idle
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
bool DekoRenderer::runPresent(uint32_t imgSlot, int vy, int vh, bool raw) {
    DKTRACE("present: acquiring a swapchain image\n");
    const int slot = dkQueueAcquireImage(queue_, swap_);
    DKTRACE("present: image %d acquired\n", slot);
    if (slot < 0 || slot >= kSwapImages) return false;
    // what wrote the source (the scene's pass, or the surface's copy) done
    // and its image cache dropped before the pass samples it
    dkCmdBufBarrier(cb_, DkBarrier_Full, DkInvalidateFlags_Image);
    // the descriptor arrays again: this is a new command list
    dkCmdBufBindImageDescriptorSet(cb_, imgDesc_.gpu(), kMaxImages);
    dkCmdBufBindSamplerDescriptorSet(cb_, sampDesc_.gpu(), kSampCount);
    const DkImageView* rt = &swapView_[slot];
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
    DkRasterizerState rs;
    dkRasterizerStateDefaults(&rs);
    rs.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(cb_, &rs);
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
    dkCmdBufBindTexture(cb_, DkStage_Fragment, 0, dkMakeTextureHandle(imgSlot, kSampClamp));
    dkCmdBufBindUniformBuffer(cb_, DkStage_Fragment, kUboPush, pushUbo_.gpu(), pushUbo_.size);
    vh = std::clamp(vh, 1, h_);
    const PresentPush pp{vy, vh, w_, dither_ && !raw ? 1 : 0, 1, 0, 0u, raw ? 1 : 0};
    dkCmdBufPushConstants(cb_, pushUbo_.gpu(), pushUbo_.size, 0, sizeof pp, &pp);
    dkCmdBufDraw(cb_, DkPrimitive_Triangles, 3, 1, 0, 0);
    // the frame's fence INSIDE its list, as deko3d's samples do
    // (`CCmdMemRing::end`): signalled on the queue after the present (the
    // first console run) it never signalled - every wait ran out its 2 s
    dkCmdBufSignalFence(cb_, &frameFence_, true);
    dkQueueSubmitCommands(queue_, dkCmdBufFinishList(cb_));
    DKTRACE("present: submitted, presenting image %d\n", slot);
    dkQueuePresentImage(queue_, swap_, slot);
    if (traceLeft() > 0) {
        // the first frames waited for here, so a GPU fault is said at once
        const DkResult r = dkFenceWait(&frameFence_, kFenceTimeoutNs);
        std::printf("deko3d trace: present done - fence %d, queue %s\n", static_cast<int>(r),
                    dkQueueIsInErrorState(queue_) ? "IN ERROR" : "fine");
        std::fflush(stdout);
        --traceLeft();
    }
    framePending_ = true;
    cbOpen_ = false;   // the next frame starts its list from the top, once this one is done
    return true;
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
    auto* dst = static_cast<unsigned char*>(surfStage_.cpu());
    const unsigned char* lut = omk::expand565Rgba();
    const int n = std::min<int>(w_ * h_, static_cast<int>(s.px.size()));
    for (int i = 0; i < n; ++i) std::memcpy(dst + 4 * i, lut + 4 * static_cast<std::size_t>(s.px[static_cast<std::size_t>(i)]), 4);
    const DkCopyBuf src{surfStage_.gpu(), 0, 0};
    DkImageView v;
    dkImageViewDefaults(&v, &surfImg_);
    const DkImageRect r{0, 0, 0, static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
    dkCmdBufCopyBufferToImage(cb_, &src, &v, &r, 0);
    return runPresent(kImgSurface, 0, h_, true);
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
    framePending_ = false;
    upOpen_ = false;
    ringAt_ = 0;
    for (Alloc& a : upOwn_) data_.free(a);
    upOwn_.clear();
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
        if (!waited) { dkQueueWaitIdle(queue_); framePending_ = false; waited = true; }
        images_.free(it->second.t->mem);
        if (it->second.t->slot >= kImgFirst) freeSlots_.push_back(it->second.t->slot);
        it = uploaded_.erase(it);
    }
    texSlot_.assign(t.size(), kImgWhite);
    int fresh = 0;
    std::vector<std::uint8_t> rgba;
    for (std::size_t i = 0; i < t.size(); ++i) {
        const omk::Texture& x = t[i];
        if (!x.hasPixels()) continue;
        auto hit = uploaded_.find(keyOf(x));
        if (hit == uploaded_.end()) {
            Uploaded u;
            u.idx = x.idx; u.pal = x.pal; u.used = true;
            u.t = std::make_unique<DTex>();
            Alloc st;
            const uint32_t bytes = static_cast<uint32_t>(x.width) * static_cast<uint32_t>(x.height) * 4;
            if (makeImage(u.t->img, u.t->mem, DkImageFormat_RGBA8_Unorm, x.width, x.height, 0) && stage(bytes, st)) {
                // alpha the COLOUR KEY: 0 where the texel is black - the cutout's discard
                x.toRgbaKeyed(static_cast<std::uint8_t*>(st.cpu()));
                DkImageView v;
                dkImageViewDefaults(&v, &u.t->img);
                const DkCopyBuf src{st.gpu(), 0, 0};
                const DkImageRect r{0, 0, 0, static_cast<uint32_t>(x.width), static_cast<uint32_t>(x.height), 1};
                dkCmdBufCopyBufferToImage(upCb_, &src, &v, &r, 0);
                u.t->slot = takeSlot();
                if (u.t->slot != kImgWhite) writeDescriptor(u.t->slot, u.t->img);
                ++fresh;
            }
            hit = uploaded_.emplace(keyOf(x), std::move(u)).first;
        }
        texSlot_[i] = hit->second.t->slot;
    }
    flushUploads();
    if (fresh > 0)
        std::printf("deko3d: texture pool - %d new of %zu; images %llu of %llu KB\n", fresh, t.size(),
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
}  // namespace omk

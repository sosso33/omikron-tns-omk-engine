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
//   * D3DCULL_NONE. `Raster_DrawTriangles` sets it, so both windings draw and
//     the rasterizer state says `VK_CULL_MODE_NONE`.
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
#include <vulkan/vulkan.h>

#include "o3de/renderer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

const uint32_t kVert[] =
#include "scene.vert.inc"
;
const uint32_t kFrag[] =
#include "scene.frag.inc"
;
const uint32_t kFsqVert[] =
#include "fsq.vert.inc"
;
const uint32_t kFsqFrag[] =
#include "fsq.frag.inc"
;

#define VKCHECK(x, what)                                                     \
    do {                                                                     \
        const VkResult r_ = (x);                                             \
        if (r_ != VK_SUCCESS) {                                              \
            std::fprintf(stderr, "vulkan: %s -> %d\n", what, r_);            \
            return false;                                                    \
        }                                                                    \
    } while (0)

// One vertex, matching `scene.vert`'s three inputs. The UVs stay in the
// material's own PIXEL units, as the shipped data stores them, and the shader
// divides by textureSize with the sampler set to REPEAT - which is the same
// wrap `raster.cpp`'s `sample()` does with `% t.width`.
struct GpuVert {
    float x, y, z;
    float u, v;
    float r, g, b;
};

struct Push {
    float mvp[16];
    int32_t cutout;
    int32_t pad[3];
};

class VulkanRenderer : public omk::Renderer {
public:
    bool init(int w, int h) override;
    void setTextures(std::span<const omk::Texture> t) override;
    void begin(const omk::View& v) override;
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
    ~VulkanRenderer() override;

    const char* device() const { return props_.deviceName; }

private:
    bool pickDevice();
    bool makeTarget();
    bool makePipelines();
    uint32_t memType(uint32_t bits, VkMemoryPropertyFlags want) const;
    bool makeBuffer(VkDeviceSize n, VkBufferUsageFlags use,
                    VkMemoryPropertyFlags props, VkBuffer& b, VkDeviceMemory& m);
    VkCommandBuffer oneShotBegin();
    void oneShotEnd(VkCommandBuffer cb);
    bool uploadGeometry(const omk::Geometry* g);

    VkInstance        inst_ = VK_NULL_HANDLE;
    VkPhysicalDevice  phys_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props_{};
    VkPhysicalDeviceMemoryProperties memProps_{};
    VkDevice          dev_  = VK_NULL_HANDLE;
    VkQueue           queue_ = VK_NULL_HANDLE;
    uint32_t          qfam_ = 0;
    VkCommandPool     pool_ = VK_NULL_HANDLE;
    VkCommandBuffer   cb_   = VK_NULL_HANDLE;
    VkFence           fence_ = VK_NULL_HANDLE;

    int w_ = 0, h_ = 0;
    VkImage        colour_ = VK_NULL_HANDLE;  VkDeviceMemory colourMem_ = VK_NULL_HANDLE;
    VkImageView    colourView_ = VK_NULL_HANDLE;
    VkImage        depth_ = VK_NULL_HANDLE;   VkDeviceMemory depthMem_ = VK_NULL_HANDLE;
    VkFormat       dsFmt_ = VK_FORMAT_UNDEFINED;
    VkImageView    depthView_ = VK_NULL_HANDLE;
    VkRenderPass   pass_ = VK_NULL_HANDLE;
    VkFramebuffer  fbuf_ = VK_NULL_HANDLE;
    VkBuffer       readBuf_ = VK_NULL_HANDLE; VkDeviceMemory readMem_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout dsl_ = VK_NULL_HANDLE;
    VkDescriptorPool      dpool_ = VK_NULL_HANDLE;
    VkPipelineLayout      plo_ = VK_NULL_HANDLE;
    VkPipeline            pipe_[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    // The mirror pass's three extra states. `pipeStencil_` marks the mirror's
    // pixels, `pipeDepthReset_` clears depth inside that mark, and `pipeRefl_`
    // is `pipe_` again with the stencil TEST on so the reflection lands only
    // where the mirror is.
    VkPipeline            pipeStencil_ = VK_NULL_HANDLE;
    VkPipeline            pipeDepthReset_ = VK_NULL_HANDLE;
    VkPipeline            pipeRefl_[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkSampler             sampler_ = VK_NULL_HANDLE;

    struct Tex {
        VkImage img = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE; VkDescriptorSet ds = VK_NULL_HANDLE;
    };
    std::vector<Tex> tex_;
    Tex              white_;              // a material with no texture

    std::map<const omk::Geometry*, std::pair<VkBuffer, VkDeviceMemory>> vbo_;
    std::map<const omk::Geometry*, std::size_t> vboN_;
    std::map<const omk::Geometry*, std::uint64_t> vboRev_;

    Push             push_{};
    omk::RasterStats st_;
    omk::Surface     fb_{1, 1, 0};
    bool             recording_ = false;
    bool             dirty_ = false;    // a new frame is waiting in readBuf_
    VkPipeline       forcePipeline_ = VK_NULL_HANDLE;  // the mirror pass's override
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
    VkSemaphore    drawn_    = VK_NULL_HANDLE;
    VkBuffer       upBuf_ = VK_NULL_HANDLE; VkDeviceMemory upMem_ = VK_NULL_HANDLE;
    VkCommandBuffer pcb_ = VK_NULL_HANDLE;
    VkFence        pfence_ = VK_NULL_HANDLE;
    uint32_t       presentFam_ = 0;

public:
    void needExtensions(const char* const* names, unsigned n) {
        wantInstExts_.assign(names, names + n);
    }
    void* createInstanceOnly();
    bool attachSurface(unsigned long long surf);
    bool makeSwapchain(int w, int h);
    bool presentDirect();
    bool presentSurface(const omk::Surface& s);
    void setViewport(const omk::View& view);
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
    VKCHECK(vkBindBufferMemory(dev_, b, m, 0), "vkBindBufferMemory");
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
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
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
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VKCHECK(vkCreateFence(dev_, &fi, nullptr, &fence_), "vkCreateFence");
    return true;
}

bool VulkanRenderer::makeTarget() {
    // R8G8B8A8_UNORM rather than a 565 format: the readback quantises to 565
    // in software, which keeps the ONE 565 quantisation rule (`PORTING` A3 -
    // bring the other side into the framebuffer's space) in one place instead
    // of delegating it to a driver whose rounding is its own.
    auto image = [&](VkFormat f, VkImageUsageFlags use, VkImageAspectFlags asp,
                     VkImage& img, VkDeviceMemory& mem, VkImageView& view) -> bool {
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = f;
        ii.extent = {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
        ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = use; ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VKCHECK(vkCreateImage(dev_, &ii, nullptr, &img), "vkCreateImage");
        VkMemoryRequirements req; vkGetImageMemoryRequirements(dev_, img, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = memType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VKCHECK(vkAllocateMemory(dev_, &ai, nullptr, &mem), "vkAllocateMemory(image)");
        VKCHECK(vkBindImageMemory(dev_, img, mem, 0), "vkBindImageMemory");
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = f;
        vi.subresourceRange = {asp, 0, 1, 0, 1};
        VKCHECK(vkCreateImageView(dev_, &vi, nullptr, &view), "vkCreateImageView");
        return true;
    };
    if (!image(VK_FORMAT_R8G8B8A8_UNORM,
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
               VK_IMAGE_ASPECT_COLOR_BIT, colour_, colourMem_, colourView_)) return false;
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
    if (!image(dsFmt_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
               VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
               depth_, depthMem_, depthView_)) return false;

    VkAttachmentDescription att[2]{};
    att[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    att[1].format = dsFmt_;
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference cref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1; sub.pColorAttachments = &cref;
    sub.pDepthStencilAttachment = &dref;
    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 2; rpi.pAttachments = att;
    rpi.subpassCount = 1; rpi.pSubpasses = &sub;
    VKCHECK(vkCreateRenderPass(dev_, &rpi, nullptr, &pass_), "vkCreateRenderPass");

    VkImageView views[2] = {colourView_, depthView_};
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = pass_; fi.attachmentCount = 2; fi.pAttachments = views;
    fi.width = static_cast<uint32_t>(w_); fi.height = static_cast<uint32_t>(h_);
    fi.layers = 1;
    VKCHECK(vkCreateFramebuffer(dev_, &fi, nullptr, &fbuf_), "vkCreateFramebuffer");

    if (!makeBuffer(static_cast<VkDeviceSize>(w_) * h_ * 4,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    readBuf_, readMem_)) return false;
    fb_ = omk::Surface(w_, h_, 0);
    return true;
}

bool VulkanRenderer::makePipelines() {
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0; b.descriptorCount = 1;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 1; dli.pBindings = &b;
    VKCHECK(vkCreateDescriptorSetLayout(dev_, &dli, nullptr, &dsl_), "descriptorSetLayout");

    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1; pli.pSetLayouts = &dsl_;
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
    if (!vs || !fs) { std::fprintf(stderr, "vulkan: shader module\n"); return false; }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};

    VkVertexInputBindingDescription vb{0, sizeof(GpuVert), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVert, x)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(GpuVert, u)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GpuVert, r)},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = va;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // DYNAMIC, because the letterbox is a viewport: camera mode draws into the
    // top-left 800x440 of a window-sized attachment and free roaming into all
    // of it, and baking either into the pipeline would need two of every one.
    VkViewport vp{0, 0, static_cast<float>(w_), static_cast<float>(h_), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_)}};
    VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vps.viewportCount = 1; vps.pViewports = &vp;
    vps.scissorCount = 1; vps.pScissors = &sc;
    const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dsi{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dsi.dynamicStateCount = 2; dsi.pDynamicStates = dyn;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    // D3DCULL_NONE - `Raster_DrawTriangles` sets it, so both windings draw.
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    for (int k = 0; k < 3; ++k) {
        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = 0xF;
        if (k == 0) {
            cba.blendEnable = VK_FALSE;
        } else if (k == 1) {
            // additive: 0x1000|0x2000, 211 meshes (ASSETS 4b)
            cba.blendEnable = VK_TRUE;
            cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.colorBlendOp = VK_BLEND_OP_ADD;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.alphaBlendOp = VK_BLEND_OP_ADD;
        } else {
            // multiply: 0x1000|0x4000, 6 meshes and the mode-6 sprites.
            // SRCBLEND=ZERO / DESTBLEND=INVSRCCOLOR - `dst * (1 - src)`, the
            // same arithmetic `raster.cpp` does, darkening where the source
            // is bright. DST_COLOR / ZERO (until 2026-09-02) was `dst * src`.
            cba.blendEnable = VK_TRUE;
            cba.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            cba.colorBlendOp = VK_BLEND_OP_ADD;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            cba.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        // Only an OPAQUE pass owns the depth - a transparent one that wrote it
        // would hide whatever comes after it in the same bucket order. That is
        // `raster.cpp`'s rule, transcribed.
        ds.depthWriteEnable = (k == 0) ? VK_TRUE : VK_FALSE;

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
        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = 0xF;
        if (k == 1) {
            cba.blendEnable = VK_TRUE;
            cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.colorBlendOp = VK_BLEND_OP_ADD;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.alphaBlendOp = VK_BLEND_OP_ADD;
        } else if (k == 2) {
            cba.blendEnable = VK_TRUE;
            cba.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;   // dst * (1 - src)
            cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            cba.colorBlendOp = VK_BLEND_OP_ADD;
            cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            cba.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineDepthStencilStateCreateInfo rd{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        rd.depthTestEnable = VK_TRUE;
        rd.depthWriteEnable = (k == 0) ? VK_TRUE : VK_FALSE;
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

    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    // NEAREST, and REPEAT. Nearest because the software rasterizer's `sample()`
    // takes one texel with no filtering and the two are being differenced;
    // repeat because `% t.width` is what lets one atlas tile across a wall.
    si.magFilter = VK_FILTER_NEAREST; si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VKCHECK(vkCreateSampler(dev_, &si, nullptr, &sampler_), "vkCreateSampler");

    // 64 sets: the engine's texture pool is 58 slots, plus the white fallback.
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 128; dpi.poolSizeCount = 1; dpi.pPoolSizes = &ps;
    VKCHECK(vkCreateDescriptorPool(dev_, &dpi, nullptr, &dpool_), "descriptorPool");
    return true;
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
    sc.clipped = VK_TRUE;
    VKCHECK(vkCreateSwapchainKHR(dev_, &sc, nullptr, &swap_), "vkCreateSwapchainKHR");

    uint32_t ni = 0;
    vkGetSwapchainImagesKHR(dev_, swap_, &ni, nullptr);
    swapImgs_.resize(ni);
    vkGetSwapchainImagesKHR(dev_, swap_, &ni, swapImgs_.data());

    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VKCHECK(vkCreateSemaphore(dev_, &si, nullptr, &acquired_), "semaphore");
    VKCHECK(vkCreateSemaphore(dev_, &si, nullptr, &drawn_), "semaphore");
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

bool VulkanRenderer::presentDirect() {
    if (swap_ == VK_NULL_HANDLE) return false;
    uint32_t idx = 0;
    VkResult r = vkAcquireNextImageKHR(dev_, swap_, UINT64_MAX, acquired_,
                                       VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) return false;
    if (r != VK_SUCCESS) return false;

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(pcb_, 0);
    vkBeginCommandBuffer(pcb_, &bi);

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

    barrier(swapImgs_[idx], VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // The letterbox bands. The engine clears to black and the picture occupies
    // the middle rows; clearing the whole image and blitting into the middle
    // reproduces that without a second attachment.
    VkClearColorValue black{{0.0f, 0.0f, 0.0f, 1.0f}};
    VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(pcb_, swapImgs_[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &black, 1, &all);

    const int y0 = (static_cast<int>(swapExt_.height) - h_) / 2;
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {w_, h_, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {0, y0, 0};
    blit.dstOffsets[1] = {static_cast<int>(swapExt_.width), y0 + h_, 1};
    vkCmdBlitImage(pcb_, colour_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapImgs_[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_NEAREST);

    barrier(swapImgs_[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    vkEndCommandBuffer(pcb_);

    const VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1; si.pWaitSemaphores = &acquired_;
    si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1; si.pCommandBuffers = &pcb_;
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = &drawn_;
    vkResetFences(dev_, 1, &pfence_);
    vkQueueSubmit(queue_, 1, &si, pfence_);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &drawn_;
    pi.swapchainCount = 1; pi.pSwapchains = &swap_;
    pi.pImageIndices = &idx;
    vkQueuePresentKHR(queue_, &pi);
    vkWaitForFences(dev_, 1, &pfence_, VK_TRUE, UINT64_MAX);
    return true;
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
bool VulkanRenderer::presentSurface(const omk::Surface& s) {
    if (swap_ == VK_NULL_HANDLE) return false;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w_) * h_ * 4;
    if (upBuf_ == VK_NULL_HANDLE &&
        !makeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    upBuf_, upMem_)) return false;
    void* p = nullptr;
    vkMapMemory(dev_, upMem_, 0, bytes, 0, &p);
    auto* dst = static_cast<unsigned char*>(p);
    const int n = std::min<int>(w_ * h_, static_cast<int>(s.px.size()));
    for (int i = 0; i < n; ++i) {
        const std::uint16_t v = s.px[static_cast<std::size_t>(i)];
        const int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        dst[4 * i + 0] = static_cast<unsigned char>((r << 3) | (r >> 2));
        dst[4 * i + 1] = static_cast<unsigned char>((g << 2) | (g >> 4));
        dst[4 * i + 2] = static_cast<unsigned char>((b << 3) | (b >> 3));
        dst[4 * i + 3] = 255;
    }
    vkUnmapMemory(dev_, upMem_);

    VkCommandBuffer cb = oneShotBegin();
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = colour_;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
    vkCmdCopyBufferToImage(cb, upBuf_, colour_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    oneShotEnd(cb);
    return presentDirect();
}

bool VulkanRenderer::init(int w, int h) {
    w_ = w; h_ = h;
    if (!pickDevice()) return false;
    if (!makeTarget()) return false;
    if (!makePipelines()) return false;
    // The window is 640x480 while the 3D target is the letterboxed 640x352.
    if (surface_ != VK_NULL_HANDLE && !makeSwapchain(w, 480)) return false;
    return true;
}

void VulkanRenderer::setTextures(std::span<const omk::Texture> t) {
    // One VkImage and one descriptor set per material, plus a white 1x1 for a
    // material with no texture - which `raster.cpp` handles by leaving the
    // texel at 255 and letting the vertex colour stand alone.
    auto upload = [&](const unsigned char* rgb, int tw, int th, Tex& out) {
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(tw) * th * 4;
        VkBuffer sb; VkDeviceMemory sm;
        if (!makeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        sb, sm)) return;
        void* p = nullptr;
        vkMapMemory(dev_, sm, 0, bytes, 0, &p);
        auto* dst = static_cast<unsigned char*>(p);
        for (int i = 0; i < tw * th; ++i) {
            dst[4 * i + 0] = rgb[3 * i + 0];
            dst[4 * i + 1] = rgb[3 * i + 1];
            dst[4 * i + 2] = rgb[3 * i + 2];
            dst[4 * i + 3] = 255;
        }
        vkUnmapMemory(dev_, sm);

        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R8G8B8A8_UNORM;
        ii.extent = {static_cast<uint32_t>(tw), static_cast<uint32_t>(th), 1};
        ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(dev_, &ii, nullptr, &out.img);
        VkMemoryRequirements req; vkGetImageMemoryRequirements(dev_, out.img, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = memType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(dev_, &ai, nullptr, &out.mem);
        vkBindImageMemory(dev_, out.img, out.mem, 0);

        VkCommandBuffer cb = oneShotBegin();
        VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.srcQueueFamilyIndex = bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = out.img;
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
        VkBufferImageCopy cp{};
        cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        cp.imageExtent = {static_cast<uint32_t>(tw), static_cast<uint32_t>(th), 1};
        vkCmdCopyBufferToImage(cb, sb, out.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
        bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
        oneShotEnd(cb);
        vkDestroyBuffer(dev_, sb, nullptr);
        vkFreeMemory(dev_, sm, nullptr);

        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = out.img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(dev_, &vi, nullptr, &out.view);

        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = dpool_; dai.descriptorSetCount = 1; dai.pSetLayouts = &dsl_;
        vkAllocateDescriptorSets(dev_, &dai, &out.ds);
        VkDescriptorImageInfo dii{sampler_, out.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wr.dstSet = out.ds; wr.dstBinding = 0; wr.descriptorCount = 1;
        wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr.pImageInfo = &dii;
        vkUpdateDescriptorSets(dev_, 1, &wr, 0, nullptr);
    };

    // Releasing what a previous call made, so a second call REPLACES the pool
    // instead of leaking it. This is a load-time call and should happen once
    // per set, but "should" is not a guarantee: a per-frame call exhausted the
    // descriptor pool and crashed, and a resource-owning setter that cannot
    // survive being called twice is a trap for the next caller.
    if (!tex_.empty()) {
        vkDeviceWaitIdle(dev_);
        for (auto& old : tex_) {
            if (old.view) vkDestroyImageView(dev_, old.view, nullptr);
            if (old.img)  vkDestroyImage(dev_, old.img, nullptr);
            if (old.mem)  vkFreeMemory(dev_, old.mem, nullptr);
        }
        tex_.clear();
        vkResetDescriptorPool(dev_, dpool_, 0);
        white_ = Tex{};
    }

    const unsigned char white[3] = {255, 255, 255};
    if (white_.img == VK_NULL_HANDLE) upload(white, 1, 1, white_);

    tex_.assign(t.size(), Tex{});
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i].width > 0 && t[i].height > 0 && !t[i].rgb.empty())
            upload(t[i].rgb.data(), t[i].width, t[i].height, tex_[i]);
    }
}

bool VulkanRenderer::uploadGeometry(const omk::Geometry* g) {
    // Cached by pointer, but only while the CONTENT has not moved: a posed
    // character is the same object with different vertices every frame
    // (`Geometry::revision`), and returning the cached buffer for it froze the
    // character at the first pose the GPU ever saw.
    const auto seen = vboRev_.find(g);
    if (vbo_.count(g) && seen != vboRev_.end() && seen->second == g->revision)
        return true;
    if (vbo_.count(g)) {
        // Same object, new vertices. The previous frame has already been
        // waited on (present ends with `vkWaitForFences`), so the buffer is
        // not in flight and can be refilled in place when it still fits.
        const auto it = vboN_.find(g);
        if (it != vboN_.end() && it->second == g->corners.size()) {
            void* p = nullptr;
            const VkDeviceSize bytes = g->corners.size() * sizeof(GpuVert);
            if (vkMapMemory(dev_, vbo_[g].second, 0, bytes, 0, &p) == VK_SUCCESS) {
                auto* dst = static_cast<GpuVert*>(p);
                for (std::size_t i = 0; i < g->corners.size(); ++i) {
                    const auto& c = g->corners[i];
                    dst[i] = {c.x, c.y, c.z, c.u, c.v, c.r, c.g, c.b};
                }
                vkUnmapMemory(dev_, vbo_[g].second);
                vboRev_[g] = g->revision;
                return true;
            }
        }
        vkDestroyBuffer(dev_, vbo_[g].first, nullptr);
        vkFreeMemory(dev_, vbo_[g].second, nullptr);
        vbo_.erase(g); vboN_.erase(g); vboRev_.erase(g);
    }
    std::vector<GpuVert> v(g->corners.size());
    for (std::size_t i = 0; i < g->corners.size(); ++i) {
        const auto& c = g->corners[i];
        v[i] = {c.x, c.y, c.z, c.u, c.v, c.r, c.g, c.b};
    }
    const VkDeviceSize bytes = v.size() * sizeof(GpuVert);
    if (!bytes) return false;
    VkBuffer b; VkDeviceMemory m;
    if (!makeBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    b, m)) return false;
    void* p = nullptr;
    vkMapMemory(dev_, m, 0, bytes, 0, &p);
    std::memcpy(p, v.data(), bytes);
    vkUnmapMemory(dev_, m);
    vbo_[g] = {b, m};
    vboN_[g] = v.size();
    vboRev_[g] = g->revision;
    return true;
}

void VulkanRenderer::setViewport(const omk::View& view) {
    const int vw = view.letterboxed() ? view.vw : w_;
    const int vh = view.letterboxed() ? view.vh : h_;
    const VkViewport vp{0, 0, static_cast<float>(vw), static_cast<float>(vh),
                        0.0f, 1.0f};
    const VkRect2D sc{{0, 0}, {static_cast<uint32_t>(vw), static_cast<uint32_t>(vh)}};
    vkCmdSetViewport(cb_, 0, 1, &vp);
    vkCmdSetScissor(cb_, 0, 1, &sc);
}

void VulkanRenderer::pushView(const omk::View& view) {
    // The view-projection, built from the SOFTWARE rasterizer's own basis so
    // the two cannot disagree about a convention.
    omk::RCamera cam = view.cam;
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
    const float r0[4] = {s[0] / tanH, s[1] / tanH, s[2] / tanH,
                         -(s[0] * e[0] + s[1] * e[1] + s[2] * e[2]) / tanH};
    const float r1[4] = {-u[0] / tanV, -u[1] / tanV, -u[2] / tanV,
                         (u[0] * e[0] + u[1] * e[1] + u[2] * e[2]) / tanV};
    const float r2[4] = {A * f[0], A * f[1], A * f[2],
                         -A * (f[0] * e[0] + f[1] * e[1] + f[2] * e[2]) + B};
    const float r3[4] = {f[0], f[1], f[2],
                         -(f[0] * e[0] + f[1] * e[1] + f[2] * e[2])};
    const float* rows[4] = {r0, r1, r2, r3};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            push_.mvp[c * 4 + r] = rows[r][c];
}

void VulkanRenderer::begin(const omk::View& view) {
    st_ = omk::RasterStats{};
    pushView(view);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(cb_, 0);
    vkBeginCommandBuffer(cb_, &bi);
    VkClearValue clear[2]{};
    clear[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};   // black, as the engine clears
    clear[1].depthStencil = {1.0f, 0};   // depth far, stencil 0
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = pass_; rp.framebuffer = fbuf_;
    rp.renderArea = {{0, 0}, {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_)}};
    rp.clearValueCount = 2; rp.pClearValues = clear;
    vkCmdBeginRenderPass(cb_, &rp, VK_SUBPASS_CONTENTS_INLINE);
    setViewport(view);
    recording_ = true;
}

void VulkanRenderer::submit(const omk::Draw& d) {
    if (!recording_ || !d.geo || !d.count) return;
    if (!uploadGeometry(d.geo)) return;
    st_.triangles += static_cast<long>(d.count / 3);

    const int k = d.blend == omk::Blend::Opaque ? 0
                : d.blend == omk::Blend::Add    ? 1 : 2;
    // The mirror pass swaps the state under the same submissions: the stencil
    // marker while it is marking, the stencil-tested variants while it is
    // drawing the reflection. The DRAWS are unchanged, which is the point -
    // the backend is choosing how, not what.
    VkPipeline use = forcePipeline_ ? forcePipeline_
                   : reflStencil_   ? pipeRefl_[k]
                                    : pipe_[k];
    vkCmdBindPipeline(cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, use);

    // The texture is the key's LOW SIX BITS and nothing else (ASSETS 4b).
    const std::size_t slot = d.bucketKey & 0x3Fu;
    const VkDescriptorSet ds =
        (slot < tex_.size() && tex_[slot].ds != VK_NULL_HANDLE) ? tex_[slot].ds
                                                                : white_.ds;
    vkCmdBindDescriptorSets(cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, plo_, 0, 1, &ds, 0, nullptr);

    push_.cutout = d.cutout ? 1 : 0;
    vkCmdPushConstants(cb_, plo_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(Push), &push_);

    const auto it = vbo_.find(d.geo);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb_, 0, 1, &it->second.first, &off);
    vkCmdDraw(cb_, static_cast<uint32_t>(d.count), 1,
              static_cast<uint32_t>(d.start), 0);
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
    if (pipeStencil_ == VK_NULL_HANDLE || mirror.empty()) return false;

    begin(v);                       // clears colour, depth AND stencil
    for (const auto& d : scene) submit(d);          // 1. the room

    // 2. mark the mirror's visible pixels. Depth-tested, so the parts of the
    //    plane buried in the wall are not marked - which is the thing the CPU
    //    mask had to be taught by differencing two passes.
    forcePipeline_ = pipeStencil_;
    for (const auto& d : mirror) submit(d);

    // The marker writes NO COLOUR, so it has to be dropped before anything
    // that should be visible. Leaving it set drew the whole reflection through
    // a colour mask of 0 - the wall came out flat and mirrorless, while the
    // coverage number stayed at 0.998 because the wall is lit either way. The
    // picture said it instantly; the metric did not.
    forcePipeline_ = VK_NULL_HANDLE;

    // 3. depth back to the far plane inside the mark
    vkCmdBindPipeline(cb_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeDepthReset_);
    vkCmdDraw(cb_, 3, 1, 0, 0);

    // 4. the reflection, which can only land where the stencil says
    pushView(refl);
    reflStencil_ = true;
    for (const auto& d : sceneClipped) submit(d);
    reflStencil_ = false;
    pushView(v);

    // 5. the mirror's own faces, blended over the reflection
    for (const auto& d : mirror) submit(d);

    end();
    return true;
}

void VulkanRenderer::end() {
    if (!recording_) return;
    vkCmdEndRenderPass(cb_);
    // The colour attachment ends in TRANSFER_SRC_OPTIMAL (the render pass says
    // so), so the readback copy needs no further barrier.
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
    vkCmdCopyImageToBuffer(cb_, colour_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readBuf_, 1, &cp);
    vkEndCommandBuffer(cb_);

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cb_;
    vkResetFences(dev_, 1, &fence_);
    vkQueueSubmit(queue_, 1, &si, fence_);
    vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX);
    recording_ = false;
    dirty_ = true;
}

const omk::Surface& VulkanRenderer::readback() {
    // IDEMPOTENT, and that is a contract not an optimisation. The software
    // renderer returns its stored framebuffer, so a caller may read it twice
    // and may WRITE to it between the two - which is exactly what the mirror
    // pass does when it composites the reflection in. Re-reading the GPU
    // buffer on every call silently threw that composite away and the frame
    // came back nearly black on Vulkan while software was correct. So a frame
    // is converted once, when `end()` says there is a new one.
    if (!dirty_) return fb_;
    dirty_ = false;
    void* p = nullptr;
    vkMapMemory(dev_, readMem_, 0, VK_WHOLE_SIZE, 0, &p);
    const auto* src = static_cast<const unsigned char*>(p);
    // 888 -> 565 HERE, in software, and not by asking for a 565 attachment:
    // `PORTING` A3 keeps the one quantisation rule in one place rather than
    // delegating it to a driver whose rounding is its own.
    for (int i = 0; i < w_ * h_; ++i)
        fb_.px[static_cast<std::size_t>(i)] =
            omk::rgb565(src[4 * i], src[4 * i + 1], src[4 * i + 2]);
    vkUnmapMemory(dev_, readMem_);
    return fb_;
}

VulkanRenderer::~VulkanRenderer() {
    if (!dev_) return;
    vkDeviceWaitIdle(dev_);
    for (auto& [g, bm] : vbo_) {
        vkDestroyBuffer(dev_, bm.first, nullptr);
        vkFreeMemory(dev_, bm.second, nullptr);
    }
    auto killTex = [&](Tex& t) {
        if (t.view) vkDestroyImageView(dev_, t.view, nullptr);
        if (t.img)  vkDestroyImage(dev_, t.img, nullptr);
        if (t.mem)  vkFreeMemory(dev_, t.mem, nullptr);
    };
    for (auto& t : tex_) killTex(t);
    killTex(white_);
    if (sampler_) vkDestroySampler(dev_, sampler_, nullptr);
    if (dpool_) vkDestroyDescriptorPool(dev_, dpool_, nullptr);
    for (auto& p : pipe_) if (p) vkDestroyPipeline(dev_, p, nullptr);
    if (plo_) vkDestroyPipelineLayout(dev_, plo_, nullptr);
    if (dsl_) vkDestroyDescriptorSetLayout(dev_, dsl_, nullptr);
    if (readBuf_) vkDestroyBuffer(dev_, readBuf_, nullptr);
    if (readMem_) vkFreeMemory(dev_, readMem_, nullptr);
    if (fbuf_) vkDestroyFramebuffer(dev_, fbuf_, nullptr);
    if (pass_) vkDestroyRenderPass(dev_, pass_, nullptr);
    if (colourView_) vkDestroyImageView(dev_, colourView_, nullptr);
    if (colour_) vkDestroyImage(dev_, colour_, nullptr);
    if (colourMem_) vkFreeMemory(dev_, colourMem_, nullptr);
    if (depthView_) vkDestroyImageView(dev_, depthView_, nullptr);
    if (depth_) vkDestroyImage(dev_, depth_, nullptr);
    if (depthMem_) vkFreeMemory(dev_, depthMem_, nullptr);
    if (upBuf_) vkDestroyBuffer(dev_, upBuf_, nullptr);
    if (upMem_) vkFreeMemory(dev_, upMem_, nullptr);
    if (pipeStencil_) vkDestroyPipeline(dev_, pipeStencil_, nullptr);
    if (pipeDepthReset_) vkDestroyPipeline(dev_, pipeDepthReset_, nullptr);
    for (auto& pp : pipeRefl_) if (pp) vkDestroyPipeline(dev_, pp, nullptr);
    if (pfence_) vkDestroyFence(dev_, pfence_, nullptr);
    if (acquired_) vkDestroySemaphore(dev_, acquired_, nullptr);
    if (drawn_) vkDestroySemaphore(dev_, drawn_, nullptr);
    if (swap_) vkDestroySwapchainKHR(dev_, swap_, nullptr);
    if (fence_) vkDestroyFence(dev_, fence_, nullptr);
    if (pool_) vkDestroyCommandPool(dev_, pool_, nullptr);
    vkDestroyDevice(dev_, nullptr);
    if (surface_ && inst_) vkDestroySurfaceKHR(inst_, surface_, nullptr);
    if (inst_) vkDestroyInstance(inst_, nullptr);
}

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
bool vulkanPresentSurface(Renderer* r, const Surface& s) {
    auto* v = dynamic_cast<VulkanRenderer*>(r);
    return v && v->presentSurface(s);
}
const char* vulkanDeviceName(Renderer* r) {
    auto* v = dynamic_cast<VulkanRenderer*>(r);
    return v ? v->device() : "";
}

}  // namespace omk

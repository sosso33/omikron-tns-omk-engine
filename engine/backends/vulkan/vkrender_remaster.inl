// SPDX-License-Identifier: GPL-3.0-or-later
// THE REMASTER'S VULKAN BACKEND (`todo/remaster.md`) - the upscale (FSR 1's
// EASU target and pass, A6), the post pass (the glow, the AO, the grade's
// LUT, step 2) and the water reflection's target and pass (A3). Moved out of
// `vkrender.cpp` unchanged (the audit's cleanliness item, 2026-10-24) and
// included at its end, inside its anonymous namespace. Not compiled on its
// own. Still inline there: the tile block of `begin`, the pack's light and
// normal branches of `poolTextures`, the BC / R8 probe, the members.

// ---- THE UPSCALE (`todo/remaster.md` A6) -------------------------------------
//
// EASU's target, pass and pipeline, and the two sets: `colour_` LINEAR for
// EASU's gathers, `easuImg_` for the present pass's RCAS. Both sets take the
// present pass's layout (one sampler, binding 0), so this is made after it
// and torn down before it.
bool VulkanRenderer::makeUpscale() {
    if (easuPipe_ != VK_NULL_HANDLE) return true;
    if (presDsl_ == VK_NULL_HANDLE) return false;
    const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt;
    ii.extent = {static_cast<uint32_t>(w_), static_cast<uint32_t>(h_), 1};
    ii.mipLevels = 1; ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!makeImage2D(ii, VK_IMAGE_ASPECT_COLOR_BIT, "render targets", easuImg_, easuMem_, &easuView_)) return false;

    // every pixel of the picture is written; it ends where RCAS reads it
    VkAttachmentDescription a{};
    a.format = fmt; a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
    // ...and the present pass's fragment shader waits for it
    VkSubpassDependency dep{};
    dep.srcSubpass = 0; dep.dstSubpass = VK_SUBPASS_EXTERNAL;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 1; rpi.pAttachments = &a;
    rpi.subpassCount = 1; rpi.pSubpasses = &sub;
    rpi.dependencyCount = 1; rpi.pDependencies = &dep;
    VKCHECK(vkCreateRenderPass(dev_, &rpi, nullptr, &easuPass_), "upscale render pass");
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = easuPass_; fi.attachmentCount = 1; fi.pAttachments = &easuView_;
    fi.width = static_cast<uint32_t>(w_); fi.height = static_cast<uint32_t>(h_); fi.layers = 1;
    VKCHECK(vkCreateFramebuffer(dev_, &fi, nullptr, &easuFb_), "upscale framebuffer");

    // its own pool, for the reason the present pass has one
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = 2; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
    VKCHECK(vkCreateDescriptorPool(dev_, &pi, nullptr, &easuPool_), "upscale pool");
    const VkDescriptorSetLayout two[2] = {presDsl_, presDsl_};
    VkDescriptorSet sets[2]{};
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = easuPool_; dai.descriptorSetCount = 2; dai.pSetLayouts = two;
    VKCHECK(vkAllocateDescriptorSets(dev_, &dai, sets), "upscale sets");
    easuDs_ = sets[0]; presUpDs_ = sets[1];
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VKCHECK(vkCreateSampler(dev_, &si, nullptr, &easuSampler_), "upscale sampler");
    const VkDescriptorImageInfo in{easuSampler_, colourView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo out{presSampler_, easuView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr[2]{};
    for (int k = 0; k < 2; ++k) {
        wr[k] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wr[k].dstSet = sets[k]; wr[k].dstBinding = 0; wr[k].descriptorCount = 1;
        wr[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr[k].pImageInfo = k == 0 ? &in : &out;
    }
    vkUpdateDescriptorSets(dev_, 2, wr, 0, nullptr);

    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16 * sizeof(std::uint32_t)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1; pli.pSetLayouts = &presDsl_;
    pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
    VKCHECK(vkCreatePipelineLayout(dev_, &pli, nullptr, &easuPlo_), "upscale pipeline layout");
    const auto module = [&](const uint32_t* code, size_t bytes) {
        VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mi.codeSize = bytes; mi.pCode = code;
        VkShaderModule m = VK_NULL_HANDLE;
        vkCreateShaderModule(dev_, &mi, nullptr, &m);
        return m;
    };
    VkShaderModule qv = module(kFsqVert, sizeof(kFsqVert));
    VkShaderModule qf = module(kEasuFrag, sizeof(kEasuFrag));
    if (!qv || !qf) { std::fprintf(stderr, "vulkan: upscale shader module\n"); return false; }
    VkPipelineShaderStageCreateInfo st[2]{};
    st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_VERTEX_BIT, qv, "main", nullptr};
    st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
             VK_SHADER_STAGE_FRAGMENT_BIT, qf, "main", nullptr};
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    // the picture's height is the call's (a letterbox's rows), so the
    // viewport and scissor are set per run
    VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vps.viewportCount = 1; vps.scissorCount = 1;
    const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
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
    gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vps; gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms; gp.pColorBlendState = &cb; gp.pDynamicState = &ds;
    gp.layout = easuPlo_; gp.renderPass = easuPass_;
    const VkResult pr = vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &easuPipe_);
    vkDestroyShaderModule(dev_, qv, nullptr);
    vkDestroyShaderModule(dev_, qf, nullptr);
    if (pr != VK_SUCCESS) { std::fprintf(stderr, "vulkan: upscale pipeline\n"); easuPipe_ = VK_NULL_HANDLE; return false; }
    return true;
}

void VulkanRenderer::killUpscale() {
    if (easuPipe_) { vkDestroyPipeline(dev_, easuPipe_, nullptr); easuPipe_ = VK_NULL_HANDLE; }
    if (easuPlo_) { vkDestroyPipelineLayout(dev_, easuPlo_, nullptr); easuPlo_ = VK_NULL_HANDLE; }
    if (easuSampler_) { vkDestroySampler(dev_, easuSampler_, nullptr); easuSampler_ = VK_NULL_HANDLE; }
    if (easuPool_) { vkDestroyDescriptorPool(dev_, easuPool_, nullptr); easuPool_ = VK_NULL_HANDLE; }
    easuDs_ = presUpDs_ = VK_NULL_HANDLE;
    if (easuFb_) { vkDestroyFramebuffer(dev_, easuFb_, nullptr); easuFb_ = VK_NULL_HANDLE; }
    if (easuPass_) { vkDestroyRenderPass(dev_, easuPass_, nullptr); easuPass_ = VK_NULL_HANDLE; }
    if (easuView_) { vkDestroyImageView(dev_, easuView_, nullptr); easuView_ = VK_NULL_HANDLE; }
    if (easuImg_) { vkDestroyImage(dev_, easuImg_, nullptr); easuImg_ = VK_NULL_HANDLE; }
    if (easuMem_) { freeDeviceMemory(easuMem_); easuMem_ = VK_NULL_HANDLE; }
}

// ---- THE REMASTER'S POST-PASS (`todo/remaster.md` step 2) -------------------
//
// the scene into `postSrc_` (`fbufPost_`, chosen by `begin`) -> [down to glow
// A, blur A -> B, blur B -> A] -> final: `postSrc_` + glow A screen-blended,
// then the grade, into `colour_`.
// The bracket runs only with the bloom on; the grade only with a LUT.
// A failure is remembered for its size: retried, it tore down and rebuilt
// everything and said so again every frame. A new size tries once more.
bool VulkanRenderer::makePost() {
    const int gw = std::max(1, rw_ / 4), gh = std::max(1, rh_ / 4);
    if (finalPipe_ != VK_NULL_HANDLE && gw == glowW_ && gh == glowH_) return true;
    if (gw == postFailW_ && gh == postFailH_) return false;
    if (makePostAt()) return true;
    postFailW_ = gw; postFailH_ = gh;
    return false;
}

bool VulkanRenderer::makePostAt() {
    const int gw = std::max(1, rw_ / 4), gh = std::max(1, rh_ / 4);
    killPost();
    glowW_ = gw; glowH_ = gh;
    const auto image = [&](int w, int h, VkFormat f, VkImageUsageFlags use, VkImage& img,
                           VkDeviceMemory& mem, VkImageView& view, const char* what) {
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = f;
        ii.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = use; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return makeImage2D(ii, VK_IMAGE_ASPECT_COLOR_BIT, what, img, mem, &view);
    };
    const VkFormat half = VK_FORMAT_R16G16B16A16_SFLOAT;
    // (TRANSFER_SRC: `pass_` ends its attachment in that layout, as `colour_`'s)
    if (!image(rw_, rh_, VK_FORMAT_R8G8B8A8_UNORM,
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
               postSrc_, postSrcMem_, postSrcView_, "post source") ||
        !image(gw, gh, half, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               glowImg_[0], glowMem_[0], glowView_[0], "glow image") ||
        !image(gw, gh, half, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
               glowImg_[1], glowMem_[1], glowView_[1], "glow image")) {
        std::fprintf(stderr, "vulkan: post-pass images\n");
        return false;
    }
    if (aoOk_) {
        aoW_ = std::max(1, rw_ / 2); aoH_ = std::max(1, rh_ / 2);
        for (int k = 0; k < 2; ++k)
            if (!image(aoW_, aoH_, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       aoImg_[k], aoMem_[k], aoView_[k], "occlusion image")) {
                std::fprintf(stderr, "vulkan: occlusion images\n");
                return false;
            }
        if (!image(aoW_, aoH_, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                   zImg_, zMem_, zView_, "occlusion depth")) {
            std::fprintf(stderr, "vulkan: occlusion depth image\n");
            return false;
        }
    }
    const auto pass = [&](VkFormat f, VkImageLayout initial, VkImageLayout final_, VkRenderPass& out) {
        VkAttachmentDescription a{};
        a.format = f; a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = initial; a.finalLayout = final_;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
        VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpi.attachmentCount = 1; rpi.pAttachments = &a;
        rpi.subpassCount = 1; rpi.pSubpasses = &sub;
        return vkCreateRenderPass(dev_, &rpi, nullptr, &out) == VK_SUCCESS;
    };
    // every pixel of each target is written, so nothing is loaded
    if (!pass(half, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, glowPass_) ||
        !pass(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, finalPass_) ||
        (aoOk_ && !pass(VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        aoPass_)) ||
        (aoOk_ && !pass(VK_FORMAT_R32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        zPass_))) {
        std::fprintf(stderr, "vulkan: post-pass render passes\n");
        return false;
    }
    for (int k = 0; k < 2; ++k) {
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = glowPass_; fi.attachmentCount = 1; fi.pAttachments = &glowView_[k];
        fi.width = static_cast<uint32_t>(gw); fi.height = static_cast<uint32_t>(gh); fi.layers = 1;
        VKCHECK(vkCreateFramebuffer(dev_, &fi, nullptr, &glowFb_[k]), "glow framebuffer");
    }
    for (int k = 0; k < 2 && aoOk_; ++k) {
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = aoPass_; fi.attachmentCount = 1; fi.pAttachments = &aoView_[k];
        fi.width = static_cast<uint32_t>(aoW_); fi.height = static_cast<uint32_t>(aoH_); fi.layers = 1;
        VKCHECK(vkCreateFramebuffer(dev_, &fi, nullptr, &aoFb_[k]), "occlusion framebuffer");
    }
    if (aoOk_) {
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = zPass_; fi.attachmentCount = 1; fi.pAttachments = &zView_;
        fi.width = static_cast<uint32_t>(aoW_); fi.height = static_cast<uint32_t>(aoH_); fi.layers = 1;
        VKCHECK(vkCreateFramebuffer(dev_, &fi, nullptr, &zFb_), "occlusion depth framebuffer");
    }
    {
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = finalPass_; fi.attachmentCount = 1; fi.pAttachments = &colourView_;
        fi.width = static_cast<uint32_t>(rw_); fi.height = static_cast<uint32_t>(rh_); fi.layers = 1;
        VKCHECK(vkCreateFramebuffer(dev_, &fi, nullptr, &finalFb_), "post framebuffer");
    }
    {
        // THE SCENE'S TARGET WHEN THE POST RUNS: `pass_` with `postSrc_` where
        // `fbuf_` has `colour_` (the resolve, or the attachment itself without
        // MSAA) - the frame lands where the post reads it, no copy (~1.3 ms
        // and 66 MB a 4K frame on the M1, the audit's 4K trims)
        const bool msaa = samples_ != VK_SAMPLE_COUNT_1_BIT;
        VkImageView views[3] = {msaa ? msColourView_ : postSrcView_, depthView_, postSrcView_};
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = pass_; fi.attachmentCount = msaa ? 3 : 2; fi.pAttachments = views;
        fi.width = static_cast<uint32_t>(rw_); fi.height = static_cast<uint32_t>(rh_); fi.layers = 1;
        VKCHECK(vkCreateFramebuffer(dev_, &fi, nullptr, &fbufPost_), "post scene framebuffer");
    }
    {
        VkDescriptorSetLayoutBinding b[4]{};
        for (uint32_t k = 0; k < 4; ++k) {
            b[k].binding = k; b[k].descriptorCount = 1;
            b[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            b[k].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dli.bindingCount = 4; dli.pBindings = b;
        VKCHECK(vkCreateDescriptorSetLayout(dev_, &dli, nullptr, &postDsl_), "post set layout");
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * kPostSets};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = kPostSets; pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
        VKCHECK(vkCreateDescriptorPool(dev_, &pi, nullptr, &postPool_), "post pool");
        VkDescriptorSetLayout lays[kPostSets];
        for (auto& l : lays) l = postDsl_;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = postPool_; ai.descriptorSetCount = kPostSets; ai.pSetLayouts = lays;
        VKCHECK(vkAllocateDescriptorSets(dev_, &ai, postDs_), "post sets");
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = VK_FILTER_LINEAR; si.minFilter = VK_FILTER_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VKCHECK(vkCreateSampler(dev_, &si, nullptr, &postSampler_), "post sampler");
        bindPostSets();
    }
    {
        VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                static_cast<uint32_t>(std::max(sizeof(PostPush), sizeof(AoPush)))};
        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pli.setLayoutCount = 1; pli.pSetLayouts = &postDsl_;
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
        VKCHECK(vkCreatePipelineLayout(dev_, &pli, nullptr, &postPlo_), "post pipeline layout");
        const auto module = [&](const uint32_t* code, size_t bytes) {
            VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            si.codeSize = bytes; si.pCode = code;
            VkShaderModule m = VK_NULL_HANDLE;
            vkCreateShaderModule(dev_, &si, nullptr, &m);
            return m;
        };
        VkShaderModule qv = module(kFsqVert, sizeof(kFsqVert));
        VkShaderModule qf = module(kPostFrag, sizeof(kPostFrag));
        if (!qv || !qf) { std::fprintf(stderr, "vulkan: post shader module\n"); return false; }
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, qv, "main", nullptr};
        st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, qf, "main", nullptr};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dsi{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dsi.dynamicStateCount = 2; dsi.pDynamicStates = dyn;
        VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vps.viewportCount = 1; vps.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cba{};
        cba.blendEnable = VK_FALSE; cba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1; cb.pAttachments = &cba;
        for (int fin = 0; fin < 2; ++fin) {
            VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            gp.stageCount = 2; gp.pStages = st;
            gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
            gp.pViewportState = &vps; gp.pRasterizationState = &rs;
            gp.pMultisampleState = &ms; gp.pColorBlendState = &cb; gp.pDynamicState = &dsi;
            gp.layout = postPlo_; gp.renderPass = fin ? finalPass_ : glowPass_;
            if (vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr,
                                          fin ? &finalPipe_ : &glowPipe_) != VK_SUCCESS) {
                std::fprintf(stderr, "vulkan: post pipeline\n");
                vkDestroyShaderModule(dev_, qv, nullptr); vkDestroyShaderModule(dev_, qf, nullptr);
                return false;
            }
        }
        // the OCCLUSION's own module, the multisampled one where the depth is
        if (aoOk_) {
            const bool msDepth = samples_ != VK_SAMPLE_COUNT_1_BIT;
            VkShaderModule af = msDepth ? module(kAoMsFrag, sizeof(kAoMsFrag)) : module(kAoFrag, sizeof(kAoFrag));
            st[1].module = af;
            VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            gp.stageCount = 2; gp.pStages = st;
            gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
            gp.pViewportState = &vps; gp.pRasterizationState = &rs;
            gp.pMultisampleState = &ms; gp.pColorBlendState = &cb; gp.pDynamicState = &dsi;
            gp.layout = postPlo_; gp.renderPass = aoPass_;
            bool ok = af && vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &aoPipe_) == VK_SUCCESS;
            gp.renderPass = zPass_;
            ok = ok && vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gp, nullptr, &zPipe_) == VK_SUCCESS;
            if (af) vkDestroyShaderModule(dev_, af, nullptr);
            if (!ok) {
                std::fprintf(stderr, "vulkan: occlusion pipeline\n");
                vkDestroyShaderModule(dev_, qv, nullptr); vkDestroyShaderModule(dev_, qf, nullptr);
                return false;
            }
        }
        vkDestroyShaderModule(dev_, qv, nullptr);
        vkDestroyShaderModule(dev_, qf, nullptr);
    }
    if (aoOk_)
        std::printf("vulkan: the remaster's ambient occlusion - %.2f, %dx%d%s\n", static_cast<double>(ao_), aoW_, aoH_,
                    samples_ != VK_SAMPLE_COUNT_1_BIT ? ", from the multisampled depth" : "");
    std::printf("vulkan: the remaster's post-pass - bloom %.2f, grade %s, glow images %dx%d\n",
                static_cast<double>(bloom_), lutN_ > 1 ? "on" : "off", gw, gh);
    omk::event("remaster.post", {{"backend", "vulkan"}, {"bloom", static_cast<double>(bloom_)},
                                 {"grade", lutN_}, {"glow_w", gw}, {"glow_h", gh}});
    return true;
}

// The seven sets: what each pass samples. A binding a pass does not read is
// still bound to a valid image (the copy), as Vulkan requires. The occlusion's
// four read the DEPTH at binding 0 (in its read-only layout), and the three
// after the first the half-size view z at binding 2.
void VulkanRenderer::bindPostSets() {
    if (!postPool_) return;
    const VkImageView lut = lutView_ ? lutView_ : postSrcView_;
    const VkImageView occ = aoOk_ ? aoView_[0] : postSrcView_;
    const VkImageView dep = aoOk_ ? depthRead_ : postSrcView_;
    const VkImageView occ1 = aoOk_ ? aoView_[1] : postSrcView_;
    const VkImageView zv = aoOk_ ? zView_ : postSrcView_;
    // a binding a pass does not read still holds an image in SHADER_READ: never
    // the one it WRITES (the down and blur V passes bound glow A while drawing
    // into it) nor the occlusion, undefined until the AO's first run - both the
    // validation layer's (VUID-vkCmdDraw-imageLayout-00344, -None-09600,
    // 2026-10-24). `postSrc_` is in SHADER_READ through every post pass.
    const VkImageView src = postSrcView_;
    const VkImageView views[kPostSets][4] = {{postSrcView_, src, lut, src},              // down
                                     {glowView_[0], src, lut, src},              // blur H: A -> B
                                     {glowView_[1], src, lut, src},              // blur V: B -> A
                                     {postSrcView_, glowView_[0], lut, occ},     // final
                                     {dep, postSrcView_, zv, postSrcView_},      // AO -> 0
                                     {dep, occ, zv, postSrcView_},               // AO blur H: 0 -> 1
                                     {dep, occ1, zv, postSrcView_},              // AO blur V: 1 -> 0
                                     {dep, postSrcView_, lut, postSrcView_},     // AO depth -> z
                                     {glowView_[0], src, lut, src},              // the halos: A -> B
                                     {postSrcView_, glowView_[1], lut, occ}};    // final, the glow B
    for (int s = 0; s < kPostSets; ++s)
        for (uint32_t k = 0; k < 4; ++k) {
            const bool depth = aoOk_ && s >= 4 && k == 0;
            VkDescriptorImageInfo ii{postSampler_, views[s][k],
                                     depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            wr.dstSet = postDs_[s]; wr.dstBinding = k; wr.descriptorCount = 1;
            wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr.pImageInfo = &ii;
            vkUpdateDescriptorSets(dev_, 1, &wr, 0, nullptr);
        }
}

void VulkanRenderer::killPost() {
    if (!dev_) return;
    if (finalPipe_) vkDestroyPipeline(dev_, finalPipe_, nullptr);
    if (glowPipe_) vkDestroyPipeline(dev_, glowPipe_, nullptr);
    if (postPlo_) vkDestroyPipelineLayout(dev_, postPlo_, nullptr);
    if (postSampler_) vkDestroySampler(dev_, postSampler_, nullptr);
    if (postPool_) vkDestroyDescriptorPool(dev_, postPool_, nullptr);
    if (postDsl_) vkDestroyDescriptorSetLayout(dev_, postDsl_, nullptr);
    if (finalFb_) vkDestroyFramebuffer(dev_, finalFb_, nullptr);
    if (fbufPost_) vkDestroyFramebuffer(dev_, fbufPost_, nullptr);
    fbufPost_ = VK_NULL_HANDLE;
    if (aoPipe_) vkDestroyPipeline(dev_, aoPipe_, nullptr);
    if (aoPass_) vkDestroyRenderPass(dev_, aoPass_, nullptr);
    if (zPipe_) vkDestroyPipeline(dev_, zPipe_, nullptr);
    if (zPass_) vkDestroyRenderPass(dev_, zPass_, nullptr);
    if (zFb_) vkDestroyFramebuffer(dev_, zFb_, nullptr);
    if (zView_) vkDestroyImageView(dev_, zView_, nullptr);
    if (zImg_) vkDestroyImage(dev_, zImg_, nullptr);
    if (zMem_) freeDeviceMemory(zMem_);
    aoPipe_ = VK_NULL_HANDLE; aoPass_ = VK_NULL_HANDLE;
    zPipe_ = VK_NULL_HANDLE; zPass_ = VK_NULL_HANDLE; zFb_ = VK_NULL_HANDLE;
    zView_ = VK_NULL_HANDLE; zImg_ = VK_NULL_HANDLE; zMem_ = VK_NULL_HANDLE;
    for (int k = 0; k < 2; ++k) {
        if (aoFb_[k]) vkDestroyFramebuffer(dev_, aoFb_[k], nullptr);
        if (aoView_[k]) vkDestroyImageView(dev_, aoView_[k], nullptr);
        if (aoImg_[k]) vkDestroyImage(dev_, aoImg_[k], nullptr);
        if (aoMem_[k]) freeDeviceMemory(aoMem_[k]);
        aoFb_[k] = VK_NULL_HANDLE; aoView_[k] = VK_NULL_HANDLE;
        aoImg_[k] = VK_NULL_HANDLE; aoMem_[k] = VK_NULL_HANDLE;
    }
    aoW_ = aoH_ = 0;
    for (int k = 0; k < 2; ++k) {
        if (glowFb_[k]) vkDestroyFramebuffer(dev_, glowFb_[k], nullptr);
        if (glowView_[k]) vkDestroyImageView(dev_, glowView_[k], nullptr);
        if (glowImg_[k]) vkDestroyImage(dev_, glowImg_[k], nullptr);
        if (glowMem_[k]) freeDeviceMemory(glowMem_[k]);
        glowFb_[k] = VK_NULL_HANDLE; glowView_[k] = VK_NULL_HANDLE;
        glowImg_[k] = VK_NULL_HANDLE; glowMem_[k] = VK_NULL_HANDLE;
    }
    if (postSrcView_) vkDestroyImageView(dev_, postSrcView_, nullptr);
    if (postSrc_) vkDestroyImage(dev_, postSrc_, nullptr);
    if (postSrcMem_) freeDeviceMemory(postSrcMem_);
    if (finalPass_) vkDestroyRenderPass(dev_, finalPass_, nullptr);
    if (glowPass_) vkDestroyRenderPass(dev_, glowPass_, nullptr);
    finalPipe_ = glowPipe_ = VK_NULL_HANDLE; postPlo_ = VK_NULL_HANDLE;
    postSampler_ = VK_NULL_HANDLE; postPool_ = VK_NULL_HANDLE; postDsl_ = VK_NULL_HANDLE;
    finalFb_ = VK_NULL_HANDLE; finalPass_ = glowPass_ = VK_NULL_HANDLE;
    postSrcView_ = VK_NULL_HANDLE; postSrc_ = VK_NULL_HANDLE; postSrcMem_ = VK_NULL_HANDLE;
    for (auto& d : postDs_) d = VK_NULL_HANDLE;
    glowW_ = glowH_ = 0;
}

void VulkanRenderer::killLut() {
    if (!dev_) return;
    if (lutView_) vkDestroyImageView(dev_, lutView_, nullptr);
    if (lutImg_) vkDestroyImage(dev_, lutImg_, nullptr);
    if (lutMem_) freeDeviceMemory(lutMem_);
    lutView_ = VK_NULL_HANDLE; lutImg_ = VK_NULL_HANDLE; lutMem_ = VK_NULL_HANDLE;
}

// The grade's strip onto the device, on its own one-shot command buffer -
// called between the scene's render pass and the post-pass, never inside one.
bool VulkanRenderer::uploadLut() {
    lutDirty_ = false;
    vkDeviceWaitIdle(dev_);
    killLut();
    // a failure on the way frees what was made and rebinds the post sets -
    // they held the view `killLut` just destroyed
    const auto fail = [&] { killLut(); bindPostSets(); return false; };
    const int w = lutN_ * lutN_, h = lutN_;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    ii.mipLevels = 1; ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!makeImage2D(ii, VK_IMAGE_ASPECT_COLOR_BIT, "grade lut", lutImg_, lutMem_, &lutView_)) return fail();
    VkBuffer sb; VkDeviceMemory sm;
    const VkDeviceSize bytes = lutData_.size();
    if (!makeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sb, sm))
        return fail();
    void* p = nullptr;
    vkMapMemory(dev_, sm, 0, bytes, 0, &p);
    std::memcpy(p, lutData_.data(), lutData_.size());
    vkUnmapMemory(dev_, sm);
    VkCommandBuffer cb = oneShotBegin();
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = lutImg_; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy cp{};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    vkCmdCopyBufferToImage(cb, sb, lutImg_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    oneShotEnd(cb);
    vkDestroyBuffer(dev_, sb, nullptr);
    freeDeviceMemory(sm);
    bindPostSets();
    return true;
}

void VulkanRenderer::recordPost(VkCommandBuffer cb) {
    if (!makePost()) return;
    if (lutDirty_ && lutN_ > 1 && !uploadLut()) lutN_ = 0;
    // the GLOW only where a draw bound a light map: with none it is black
    const bool glow = bloom_ > 0.0f && lightMapNow_;
    const auto barrier = [&](VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags srcA,
                             VkAccessFlags dstA, VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = from; b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = srcA; b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(cb, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    };
    const VkPipelineStageFlags OUT = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               FRAG = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    const VkAccessFlags W = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, R = VK_ACCESS_SHADER_READ_BIT;
    // the scene's frame is in `postSrc_` (`fbufPost_`, the pass ends it in
    // TRANSFER_SRC); `colour_` holds nothing of this frame - the final pass
    // writes every pixel of it
    barrier(postSrc_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, W, R, OUT, FRAG);
    const auto run = [&](VkRenderPass rp, VkFramebuffer fb, VkPipeline pipe, int w, int h,
                         VkDescriptorSet ds, int srcW, int srcH, float dx, float dy, int mode, float halo) {
        VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        bi.renderPass = rp; bi.framebuffer = fb;
        bi.renderArea = {{0, 0}, {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}};
        vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        const VkViewport vp{0, 0, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
        const VkRect2D sc{{0, 0}, {static_cast<uint32_t>(w), static_cast<uint32_t>(h)}};
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPlo_, 0, 1, &ds, 0, nullptr);
        const PostPush p{{1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h)},
                         {1.0f / static_cast<float>(srcW), 1.0f / static_cast<float>(srcH)},
                         {dx, dy}, glow ? bloom_ : 0.0f, mode, lutN_ > 1 ? lutN_ : 0, aoOk_ ? 1 : 0,
                         glow ? halo : 0.0f};
        vkCmdPushConstants(cb, postPlo_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof p, &p);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRenderPass(cb);
    };
    // THE HALOS at the glow's own size (`post.frag` mode 3): its 25 taps a
    // quarter-size texel, once, not a full-size pixel's in the final pass -
    // which then reads the glow with them as its glow (glow B, set 9)
    const bool halos = glow && halo_ > 0.0f;
    if (glow) {
        run(glowPass_, glowFb_[0], glowPipe_, glowW_, glowH_, postDs_[0], rw_, rh_, 0, 0, 0, 0.0f);
        barrier(glowImg_[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, W, R, OUT, FRAG);
        run(glowPass_, glowFb_[1], glowPipe_, glowW_, glowH_, postDs_[1], glowW_, glowH_, 1, 0, 1, 0.0f);
        barrier(glowImg_[1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, W, R, OUT, FRAG);
        run(glowPass_, glowFb_[0], glowPipe_, glowW_, glowH_, postDs_[2], glowW_, glowH_, 0, 1, 1, 0.0f);
        barrier(glowImg_[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, W, R, OUT, FRAG);
        if (halos) {
            run(glowPass_, glowFb_[1], glowPipe_, glowW_, glowH_, postDs_[8], rw_, rh_, 0, 0, 3, halo_);
            barrier(glowImg_[1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, W, R, OUT, FRAG);
        }
        stamp("glow");
    } else {
        // the final pass's glow binding must still be an image in SHADER_READ
        barrier(glowImg_[0], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                0, R, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, FRAG);
    }
    // THE AMBIENT OCCLUSION: the depth to its read-only layout (the next
    // frame's pass starts it from UNDEFINED), then AO, blur, blur
    if (aoOk_) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = depth_; b.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
        b.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; b.dstAccessMask = R;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             FRAG, 0, 0, nullptr, 0, nullptr, 1, &b);
        const float nearP = omk::kNearCut, farP = 200000.0f;   // `pushView`'s
        // k: the occlusion image written, or -1 the half-size view z
        const auto ao = [&](int k, VkDescriptorSet ds, int mode) {
            VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            bi.renderPass = k < 0 ? zPass_ : aoPass_; bi.framebuffer = k < 0 ? zFb_ : aoFb_[k];
            bi.renderArea = {{0, 0}, {static_cast<uint32_t>(aoW_), static_cast<uint32_t>(aoH_)}};
            vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, k < 0 ? zPipe_ : aoPipe_);
            const VkViewport vp{0, 0, static_cast<float>(aoW_), static_cast<float>(aoH_), 0.0f, 1.0f};
            const VkRect2D sc{{0, 0}, {static_cast<uint32_t>(aoW_), static_cast<uint32_t>(aoH_)}};
            vkCmdSetViewport(cb, 0, 1, &vp);
            vkCmdSetScissor(cb, 0, 1, &sc);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, postPlo_, 0, 1, &ds, 0, nullptr);
            AoPush p{};
            for (int i = 0; i < 4; ++i) p.view[i] = aoView4_[i];
            p.depth[0] = farP / (farP - nearP);
            p.depth[1] = -farP * nearP / (farP - nearP);
            p.depth[2] = fog_ ? fogStart_ : 0.0f;
            p.depth[3] = fog_ ? fogEnd_ : 0.0f;
            p.radius = kAoRadius; p.strength = ao_; p.mode = mode;
            p.srcInv[0] = 1.0f / static_cast<float>(aoW_); p.srcInv[1] = 1.0f / static_cast<float>(aoH_);
            vkCmdPushConstants(cb, postPlo_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof p, &p);
            vkCmdDraw(cb, 3, 1, 0, 0);
            vkCmdEndRenderPass(cb);
        };
        ao(-1, postDs_[7], 3);
        barrier(zImg_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, W, R, OUT, FRAG);
        ao(0, postDs_[4], 0);
        barrier(aoImg_[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, W, R, OUT, FRAG);
        ao(1, postDs_[5], 1);
        barrier(aoImg_[1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, W, R, OUT, FRAG);
        ao(0, postDs_[6], 2);
        barrier(aoImg_[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, W, R, OUT, FRAG);
        stamp("ao");
    }
    barrier(colour_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, W, OUT, OUT);
    run(finalPass_, finalFb_, finalPipe_, rw_, rh_, halos ? postDs_[9] : postDs_[3], rw_, rh_, 0, 0, 2, 0.0f);
}

// ---- THE WATER REFLECTION (`todo/remaster.md` A3) -------------------------
//
// The second target mirrors `makeTarget`'s - the same formats, the same sample
// count, the same attachment order - so the SCENE's render pass and every
// pipeline draw into it unchanged; only the framebuffer differs.
bool VulkanRenderer::makeReflection() {
    if (reflFbuf_ != VK_NULL_HANDLE) return true;
    const bool msaa = samples_ != VK_SAMPLE_COUNT_1_BIT;
    const auto image = [&](VkFormat f, VkImageUsageFlags use, VkImageAspectFlags asp,
                           VkSampleCountFlagBits samples, VkImage& img, VkDeviceMemory& mem,
                           VkImageView& view) -> bool {
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = f;
        ii.extent = {static_cast<uint32_t>(rw_), static_cast<uint32_t>(rh_), 1};
        ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = samples; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = use; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return makeImage2D(ii, asp, "water reflection", img, mem, &view);
    };
    // (TRANSFER_DST: its first contents are CLEARED below - the validation
    // layer's VUID-vkCmdClearColorImage-image-00002 without it, 2026-10-24)
    if (!image(VK_FORMAT_R8G8B8A8_UNORM,
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT,
               VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT, reflColour_, reflColourMem_, reflColourView_) ||
        // (TRANSIENT, as the scene's: the samples resolved within the pass,
        // the depth stored only by `pass_` with the AO, which the reflection
        // uses when its own `reflPass_` could not be made)
        (msaa && !image(VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT, samples_, reflMs_, reflMsMem_, reflMsView_)) ||
        !image(dsFmt_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                           (!aoOk_ || reflPass_ ? VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : 0u),
               VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, samples_,
               reflDepth_, reflDepthMem_, reflDepthView_)) {
        std::fprintf(stderr, "vulkan: the water reflection's target - refused\n");
        killReflection();
        return false;
    }
    VkImageView views[3] = {msaa ? reflMsView_ : reflColourView_, reflDepthView_, reflColourView_};
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = pass_; fi.attachmentCount = msaa ? 3 : 2; fi.pAttachments = views;
    fi.width = static_cast<uint32_t>(rw_); fi.height = static_cast<uint32_t>(rh_); fi.layers = 1;
    if (vkCreateFramebuffer(dev_, &fi, nullptr, &reflFbuf_) != VK_SUCCESS) { killReflection(); return false; }
    // ...and the floors' one-sample one: the colour image itself, a depth of
    // its own at half height - and the FULL width, for the second level's
    // reflection in the top-right quarter (`drawTwoReflections`)
    if (reflPass1_) {
        const int hw = rw_, hh = (rh_ + 1) / 2;
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = dsFmt_;
        ii.extent = {static_cast<uint32_t>(hw), static_cast<uint32_t>(hh), 1};
        ii.mipLevels = 1; ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool ok = makeImage2D(ii, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                              "floors' reflection depth", reflDepth1_, reflDepth1Mem_, &reflDepth1View_);
        if (ok) {
            VkImageView v1[2] = {reflColourView_, reflDepth1View_};
            VkFramebufferCreateInfo f1{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            f1.renderPass = reflPass1_; f1.attachmentCount = 2; f1.pAttachments = v1;
            f1.width = static_cast<uint32_t>(hw); f1.height = static_cast<uint32_t>(hh); f1.layers = 1;
            ok = vkCreateFramebuffer(dev_, &f1, nullptr, &reflFbuf1_) == VK_SUCCESS;
        }
        if (!ok) std::fprintf(stderr, "vulkan: the floors' one-sample reflection - refused, they keep the MSAA one\n");
    }
    // its first contents, black, and in the layout the water draws read it in
    VkCommandBuffer cb = oneShotBegin();
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = reflColour_; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    const VkClearColorValue black{{0, 0, 0, 1}};
    const VkImageSubresourceRange rng{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cb, reflColour_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &rng);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    oneShotEnd(cb);
    // every texture's set reads it as binding 2 from now on
    for (auto& [key, u] : uploaded_) if (u.t.ds) bindReflection(u.t.ds);
    if (white_.ds) bindReflection(white_.ds);
    std::printf("vulkan: the water reflection's target - %dx%d (a REMASTER feature)\n", rw_, rh_);
    omk::event("vulkan.memoryless", {{"images", lazyImages_}, {"mb", static_cast<double>(lazyBytes_) / 1048576.0}});
    return true;
}

void VulkanRenderer::bindReflection(VkDescriptorSet ds) {
    const VkImageView view = reflColourView_ ? reflColourView_ : lightBlack_.view;
    if (!ds || !view) return;
    VkDescriptorImageInfo dii{sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr.dstSet = ds; wr.dstBinding = 2; wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &dii;
    vkUpdateDescriptorSets(dev_, 1, &wr, 0, nullptr);
}

void VulkanRenderer::killReflection() {
    if (!dev_) return;
    const bool had = reflColourView_ != VK_NULL_HANDLE;
    if (reflFbuf_) vkDestroyFramebuffer(dev_, reflFbuf_, nullptr);
    if (reflFbuf1_) vkDestroyFramebuffer(dev_, reflFbuf1_, nullptr);
    for (auto* p : {&reflColourView_, &reflMsView_, &reflDepthView_, &reflDepth1View_})
        if (*p) { vkDestroyImageView(dev_, *p, nullptr); *p = VK_NULL_HANDLE; }
    for (auto* p : {&reflColour_, &reflMs_, &reflDepth_, &reflDepth1_})
        if (*p) { vkDestroyImage(dev_, *p, nullptr); *p = VK_NULL_HANDLE; }
    for (auto* p : {&reflColourMem_, &reflMsMem_, &reflDepthMem_, &reflDepth1Mem_})
        if (*p) { freeDeviceMemory(*p); *p = VK_NULL_HANDLE; }
    reflFbuf_ = VK_NULL_HANDLE;
    reflFbuf1_ = VK_NULL_HANDLE;
    // the sets must not keep a view that is gone: back to black
    if (had && lightBlack_.view) {
        for (auto& [key, u] : uploaded_) if (u.t.ds) bindReflection(u.t.ds);
        if (white_.ds) bindReflection(white_.ds);
    }
}

bool VulkanRenderer::drawWaterScene(const omk::View& v, const omk::View& refl,
                                    std::span<const omk::Draw> scene, std::span<const omk::Draw> above,
                                    std::span<const omk::Draw> water, float strength, float planeY) {
    // (no water draws: the REFLECTING FLOORS' frame - its floors are in
    // `scene`, `Draw::reflect` set)
    if (strength <= 0.0f || !makeReflection()) return false;
    reflHalf_ = water.empty();
    const VkFramebuffer reflFb = reflHalf_ && reflFbuf1_ ? reflFbuf1_ : reflFbuf_;
    // 1. THE REFLECTION: the same render pass into the reflection's
    //    framebuffer, through the reflected camera; it ends in TRANSFER_SRC,
    //    as the scene's target does, and goes to SHADER_READ for the water
    passFb_ = reflFb;
    begin(refl);
    reflectingNow_ = true;
    waterY_ = planeY;
    for (const auto& d : above) submit(d);
    reflectingNow_ = false;
    vkCmdEndRenderPass(cb_);
    stamp("reflection");
    recording_ = false;
    // ...and THE SECOND LEVEL's (`drawTwoReflections`), into the top-right
    // quarter of the same image: the first's writes done and the image back
    // in COLOR_ATTACHMENT, where its keeping pass begins
    if (secondView_) {
        VkImageMemoryBarrier k{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        k.srcQueueFamilyIndex = k.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        k.image = reflColour_; k.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        k.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; k.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        k.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        k.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cb_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &k);
        second_ = true;
        begin(*secondView_);
        reflectingNow_ = true;
        waterY_ = secondY_;
        for (const auto& d : secondAbove_) submit(d);
        reflectingNow_ = false;
        vkCmdEndRenderPass(cb_);
        stamp("reflection");   // (the GPU times add both reflections)
        recording_ = false;
        second_ = false;
    }
    passFb_ = VK_NULL_HANDLE;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = reflColour_; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    // 2. THE FRAME, the water's own draws (and the floors') adding the reflection
    const bool query = levelPool_ != VK_NULL_HANDLE && pipeProbe_ != VK_NULL_HANDLE && water.empty();
    if (query) vkCmdResetQueryPool(cb_, levelPool_, 0, kLevelQueries);   // (outside the render pass)
    begin(v);
    waterStrength_ = strength;
    for (const auto& d : scene) submit(d);
    // ...then the SECOND level's floors again, each in an occlusion query
    // (`secondLevelSeen`) through the probe: after the whole scene, so what
    // covers them counts whatever order it was drawn in
    if (query) {
        forcePipeline_ = pipeProbe_;
        for (const auto& d : scene) {
            if (d.reflectPlane != 1) continue;
            if (levelIssued_ >= kLevelQueries) { levelOver_ = true; break; }
            vkCmdBeginQuery(cb_, levelPool_, levelIssued_, 0);
            submit(d);
            vkCmdEndQuery(cb_, levelPool_, levelIssued_++);
        }
        forcePipeline_ = VK_NULL_HANDLE;
    }
    waterNow_ = true;
    for (const auto& d : water) submit(d);
    waterNow_ = false;
    end();
    return true;
}

// THE SECOND REFLECTING LEVEL (`Renderer::drawTwoReflections`): the floors'
// frame of `drawWaterScene`, its pass drawing a second reflection after the
// first - and the frame's floors each sampling its own quarter (bit 22)
bool VulkanRenderer::drawTwoReflections(const omk::View& v, const omk::View& refl, std::span<const omk::Draw> scene,
                                        std::span<const omk::Draw> above, float strength, float planeY,
                                        const omk::View& refl2, std::span<const omk::Draw> above2, float planeY2) {
    if (!drawsTwoReflections() || !makeReflection()) return false;
    // (one sample: with MSAA the floors' own pass, else `pass_`'s full one)
    if (samples_ != VK_SAMPLE_COUNT_1_BIT && !reflFbuf1_) return false;
    twoRefl_ = true;
    secondView_ = &refl2;
    secondAbove_ = above2;
    secondY_ = planeY2;
    const bool ok = drawWaterScene(v, refl, scene, above, {}, strength, planeY);
    secondView_ = nullptr;
    secondAbove_ = {};
    twoRefl_ = false;
    return ok;
}

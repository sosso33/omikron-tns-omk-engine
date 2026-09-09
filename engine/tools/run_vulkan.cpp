// SPDX-License-Identifier: GPL-3.0-or-later
// THE TWO RENDERERS, DIFFERENCED - `PORTING` A1's pair across A2's boundary.
//
//     run_vulkan <gamedata> <model.3DO> <eye> <at> <hfov> <out.bin> [WxH] [msaa] [filter] [aniso]
//
// `msaa` (0/2/4/8, default 0), `filter` (0 nearest, 1 bilinear, 2 trilinear;
// default 0) and `aniso` (1..16, default 1) are the `[Enhancements]` options,
// asked of the Vulkan side only - the software reference has none, like the
// original.
//
// The same set, the same camera, the same submissions in the same order, once
// through the software reference and once through the GPU. Both come back as
// an RGB565 `Surface` because `readback()` exists for exactly this.
//
// **What is comparable, and what is not.** Not pixels: a GPU has its own fill
// rule, its own interpolation precision and its own texture filter, so
// per-pixel equality is not the criterion here any more than it is against a
// captured frame (`PORTING` B5). What is comparable is SILHOUETTE and
// COVERAGE, and both frames are written so `verify.py` can measure them with
// the same instrument it already uses against the engine's own captures.
#include "formats/tex3dt.h"
#include "o3de/geom3do.h"
#include "o3de/renderer.h"
#include "platform/datafs.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace omk {
Renderer* makeVulkanRenderer(); const char* vulkanDeviceName(Renderer*);
int vulkanSamples(Renderer*);      // what the frame was drawn with, 1 when off
int vulkanMaxSamples(Renderer*);   // what the device could do at most
int vulkanTextureFilter(Renderer*);
int vulkanAnisotropy(Renderer*);
}

namespace {
void triple(const char* s, float o[3]) { std::sscanf(s, "%f,%f,%f", &o[0], &o[1], &o[2]); }
void put(std::ofstream& o, const omk::Surface& s) {
    for (auto v : s.px) {
        const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
        o.write(b, 2);
    }
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 7) {
        std::fprintf(stderr, "usage: run_vulkan <gamedata> <model.3DO> <eye> <at> <hfov> "
                             "<out.bin> [WxH] [msaa] [filter] [aniso]\n");
        return 2;
    }
    const fs::path model = argv[2];
    const auto d = omk::DataFs::readPath(model.string());
    auto tp = model; tp.replace_extension(".3dt");
    const auto tr = omk::DataFs(model.parent_path().string()).resolve(tp.filename().string());
    const auto t = tr ? omk::DataFs::readPath(*tr) : std::vector<std::byte>{};
    if (d.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
    const auto geo = omk::buildGeometry(d, omk::DrawFilter::Engine);
    const auto tex = t.empty() ? std::vector<omk::Texture>{} : omk::textures(d, t);

    omk::RCamera cam;
    triple(argv[3], cam.eye); triple(argv[4], cam.at);
    cam.hfovDeg = static_cast<float>(std::atof(argv[5]));
    int W = 640, H = 352;
    if (argc > 7) std::sscanf(argv[7], "%dx%d", &W, &H);
    cam.w = W; cam.h = H;
    omk::View view; view.cam = cam;
    // ...WITH THE DITHER OFF, to match `run_renderer`. `engine: renderer`
    // compares this frame against that one, and BOTH backends dither
    // (`vkrender.cpp` takes `view.dither` into `quantise888Dither` exactly as
    // `raster.cpp` does), so leaving it on one side and off the other would
    // measure the 4x4 pattern instead of the two backends. Off on both is the
    // apples-to-apples comparison; the dither itself has its own check.
    view.dither = false;

    // the submissions, in `buildGeometry`'s order, which is the engine's
    std::vector<omk::Draw> draws;
    for (const auto& b : geo.batches) {
        omk::Draw dr;
        dr.bucketKey = static_cast<std::uint32_t>(b.material) & 0x3Fu;
        dr.geo = &geo; dr.start = b.start; dr.count = b.count;
        dr.blend = b.blend; dr.cutout = b.cutout;
        draws.push_back(dr);
    }

    auto run = [&](omk::Renderer& r) -> const omk::Surface* {
        if (!r.init(W, H)) return nullptr;
        r.setTextures(tex);
        r.begin(view);
        for (const auto& dr : draws) r.submit(dr);
        r.end();
        return &r.readback();
    };

    omk::SoftwareRenderer sw;
    const omk::Surface* a = run(sw);
    if (!a) { std::fprintf(stderr, "software renderer failed\n"); return 1; }
    const omk::Surface swFrame = *a;

    omk::Renderer* vk = omk::makeVulkanRenderer();
    const int msaa = argc > 8 ? std::atoi(argv[8]) : 0;
    if (msaa > 1) vk->setMultisample(msaa);
    const int filter = argc > 9 ? std::atoi(argv[9]) : 0;
    if (filter > 0) vk->setTextureFilter(filter);
    const int aniso = argc > 10 ? std::atoi(argv[10]) : 1;
    if (aniso > 1) vk->setAnisotropy(aniso);
    const omk::Surface* b = run(*vk);
    if (!b) {
        std::fprintf(stderr, "vulkan renderer failed to come up\n");
        delete vk; return 1;
    }
    std::printf("device: %s  msaa: %d of %d  filter: %d  aniso: %d\n", omk::vulkanDeviceName(vk),
                omk::vulkanSamples(vk), omk::vulkanMaxSamples(vk), omk::vulkanTextureFilter(vk),
                omk::vulkanAnisotropy(vk));

    long lit_sw = 0, lit_vk = 0, both = 0, differ = 0;
    for (std::size_t i = 0; i < swFrame.px.size(); ++i) {
        const bool ls = swFrame.px[i] != 0, lv = b->px[i] != 0;
        lit_sw += ls; lit_vk += lv; both += (ls && lv);
        differ += swFrame.px[i] != b->px[i];
    }
    const auto ss = sw.stats(), vs = vk->stats();
    std::printf("%s %dx%d\n"
                "  software: %ld tri, %ld lit\n"
                "  vulkan  : %ld tri, %ld lit\n"
                "  coverage agreement (both lit / either lit): %.4f\n"
                "  pixels differing exactly: %ld (%.1f%%) - NOT a criterion\n",
                model.filename().string().c_str(), W, H,
                ss.triangles, lit_sw, vs.triangles, lit_vk,
                (lit_sw + lit_vk - both) ? double(both) / double(lit_sw + lit_vk - both) : 0.0,
                differ, 100.0 * double(differ) / double(swFrame.px.size()));

    if (!omk::safeOutputPath(argv[6])) return 2;
    std::ofstream o(argv[6], std::ios::binary);
    const std::int32_t hdr[4] = {W, H, static_cast<std::int32_t>(lit_sw),
                                 static_cast<std::int32_t>(lit_vk)};
    o.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
    put(o, swFrame);
    put(o, *b);
    delete vk;
    return 0;
}

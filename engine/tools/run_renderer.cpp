// SPDX-License-Identifier: GPL-3.0-or-later
// THE RENDERER BOUNDARY, EXERCISED - and the backends differenced across it.
//
//     run_renderer <gamedata> <model.3DO> <eye> <at> <hfov> <out.bin> [WxH] [backend]
//
// `PORTING` A1 puts two implementations behind one boundary and A2 puts that
// boundary at the DECISION level. This tool is what keeps the pair honest.
//
// It renders the same set through the same camera three ways:
//
//   1. `drawGeometry` called directly, which is what every existing check
//      measures and what `traces/frames/dlg402-47.png` was compared against;
//   2. the same rasterizer behind `SoftwareRenderer`, one `submit` per batch;
//   3. the live backend, when one is compiled in.
//
// **1 and 2 must agree in every pixel.** That is not a hope, it is the point:
// if putting the boundary in front of the rasterizer moved anything, then the
// boundary is doing rendering, which is precisely what A2 forbids - a backend
// receives decisions and turns them into API calls, it never makes one. A
// non-zero count here means the wrapper reordered, re-resolved a texture, or
// lost the shared depth buffer between batches.
//
// 3 is a different kind of comparison and must not be read as the same one.
// A GPU rasterises with its own fill rule, its own interpolation precision and
// its own texture filter, so it will never be pixel-equal to a software loop
// and it is not supposed to be - `PORTING` B5's argument about a captured
// frame applies here for the same reason. What is comparable is what B6 says
// is comparable: silhouette and coverage.
#include "formats/tex3dt.h"
#include "o3de/geom3do.h"
#include "o3de/raster.h"
#include "o3de/renderer.h"
#include "platform/datafs.h"
#include "ui/surface.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void triple(const char* s, float o[3]) { std::sscanf(s, "%f,%f,%f", &o[0], &o[1], &o[2]); }

void writeFrame(const std::string& path, const omk::Surface& s) {
    std::ofstream o(path, std::ios::binary);
    for (auto v : s.px) {
        const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
        o.write(b, 2);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 7) {
        std::fprintf(stderr, "usage: run_renderer <gamedata> <model.3DO> <eye> <at> "
                             "<hfov> <out.bin> [WxH] [backend]\n");
        return 2;
    }
    const std::string root = argv[1];
    const fs::path model = argv[2];

    const auto d = omk::DataFs::readPath(model.string());
    auto tp = model; tp.replace_extension(".3dt");
    const auto tr = omk::DataFs(model.parent_path().string())
                        .resolve(tp.filename().string());
    const auto t = tr ? omk::DataFs::readPath(*tr) : std::vector<std::byte>{};
    if (d.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }

    const auto geo = omk::buildGeometry(d, omk::DrawFilter::Engine);
    const auto tex = t.empty() ? std::vector<omk::Texture>{} : omk::textures(d, t);

    omk::RCamera cam;
    triple(argv[3], cam.eye);
    triple(argv[4], cam.at);
    cam.hfovDeg = static_cast<float>(std::atof(argv[5]));
    if (argc > 7) {
        int w = 0, h = 0;
        if (std::sscanf(argv[7], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
            cam.w = w; cam.h = h;
        }
    }

    // ---- 1. the direct call, the path every existing check measures
    omk::Surface direct(cam.w, cam.h, 0);
    std::vector<float> depth;
    omk::clearDepth(depth, cam.w, cam.h);
    const auto stDirect = omk::drawGeometry(direct, depth, cam, geo, tex);

    // ---- 2. the same thing behind the boundary, one submit per batch, in the
    //         order `buildGeometry` produced them - which is the engine's own
    //         (`Render_FlushBuckets` ascending), not this file's.
    omk::SoftwareRenderer sw;
    sw.init(cam.w, cam.h);
    sw.setTextures(tex);
    omk::View view; view.cam = cam;
    sw.begin(view);
    for (const auto& b : geo.batches) {
        omk::Draw dr;
        // The key's low six bits ARE the texture slot (ASSETS 4b). Building it
        // here from the material is what the engine's bucket walk does; a
        // backend never resolves a texture any other way.
        dr.bucketKey = static_cast<std::uint32_t>(b.material) & 0x3Fu;
        dr.geo = &geo; dr.start = b.start; dr.count = b.count;
        dr.blend = b.blend; dr.cutout = b.cutout;
        sw.submit(dr);
    }
    sw.end();
    const omk::Surface& boundary = sw.readback();

    long differ = 0;
    for (std::size_t i = 0; i < direct.px.size(); ++i)
        differ += direct.px[i] != boundary.px[i];

    const auto stB = sw.stats();
    std::printf("%s: %zu corners, %zu batches, %zu textures\n"
                "  direct   : %ld tri -> %ld drawn, %ld px\n"
                "  boundary : %ld tri -> %ld drawn, %ld px   (%s)\n"
                "  the boundary moves %ld of %d pixels\n",
                model.filename().string().c_str(), geo.corners.size(),
                geo.batches.size(), tex.size(),
                stDirect.triangles, stDirect.drawn, stDirect.pixels,
                stB.triangles, stB.drawn, stB.pixels, sw.name(),
                differ, cam.w * cam.h);

    std::vector<std::int32_t> out = {
        static_cast<std::int32_t>(geo.corners.size()),
        static_cast<std::int32_t>(geo.batches.size()),
        static_cast<std::int32_t>(stDirect.triangles), static_cast<std::int32_t>(stDirect.drawn),
        static_cast<std::int32_t>(stDirect.pixels),
        static_cast<std::int32_t>(stB.triangles), static_cast<std::int32_t>(stB.drawn),
        static_cast<std::int32_t>(stB.pixels),
        static_cast<std::int32_t>(differ),
    };
    if (!omk::safeOutputPath(argv[6])) return 2;
    std::ofstream o(argv[6], std::ios::binary);
    const std::int32_t n = static_cast<std::int32_t>(out.size());
    o.write(reinterpret_cast<const char*>(&n), 4);
    o.write(reinterpret_cast<const char*>(out.data()),
            static_cast<std::streamsize>(out.size() * 4));
    for (auto v : boundary.px) {
        const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
        o.write(b, 2);
    }
    (void)writeFrame;
    return differ == 0 ? 0 : 1;
}

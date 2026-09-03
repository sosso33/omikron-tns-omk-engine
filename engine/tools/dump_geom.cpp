// SPDX-License-Identifier: GPL-3.0-or-later
// Dump a .3DO's assembled geometry, for the differential against
// tools/omkdata.py's decor_geometry.
//
//     dump_geom <model.3DO> <out.bin> [--engine|--viewer]
//
// --viewer (the default) uses decor_geometry's own drawable rule, so the two
// can be compared corner for corner; --engine uses `flags & 0x800043`, the
// rule the renderer actually applies, which is what a replica wants and which
// deliberately disagrees with the viewer in both directions.
//
// Layout: int32 nBatches, then per batch {int32 material, uint8 cutout,
// uint8 blend, int32 start, int32 count}; then int32 nCorners, then per
// corner nine little-endian floats (x y z u v r g b phase).
#include "o3de/geom3do.h"

#include "platform/datafs.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {


struct Out {
    std::vector<std::uint8_t> b;
    void u8(std::uint8_t v) { b.push_back(v); }
    void i32(std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) b.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    }
    void f32(float v) {
        std::uint32_t u;
        std::memcpy(&u, &v, sizeof u);
        for (int k = 0; k < 4; ++k) b.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_geom <model.3DO> <out.bin> [--engine|--viewer]\n");
        return 2;
    }
    auto filter = omk::DrawFilter::DecorViewer;
    for (int i = 3; i < argc; ++i)
        if (std::strcmp(argv[i], "--engine") == 0) filter = omk::DrawFilter::Engine;

    const auto d = omk::DataFs::readPath(argv[1]);
    const auto g = omk::buildGeometry(d, filter);

    Out o;
    o.i32(static_cast<std::int32_t>(g.batches.size()));
    for (const auto& b : g.batches) {
        o.i32(b.material);
        o.u8(b.cutout ? 1 : 0);
        o.u8(static_cast<std::uint8_t>(b.blend));
        o.i32(static_cast<std::int32_t>(b.start));
        o.i32(static_cast<std::int32_t>(b.count));
    }
    o.i32(static_cast<std::int32_t>(g.corners.size()));
    for (const auto& c : g.corners) {
        o.f32(c.x); o.f32(c.y); o.f32(c.z);
        o.f32(c.u); o.f32(c.v);
        o.f32(c.r); o.f32(c.g); o.f32(c.b);
        o.f32(c.phase);
    }

    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.b.data()),
            static_cast<std::streamsize>(o.b.size()));
    std::printf("%zu batches %zu corners (%zu triangles)\n",
                g.batches.size(), g.corners.size(), g.corners.size() / 3);
    return 0;
}

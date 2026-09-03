// SPDX-License-Identifier: GPL-3.0-or-later
// THE SOFTWARE 3D RASTERIZER, RUN - a real set through a real camera.
//
//     run_raster <gamedata> <model.3DO> <eyeX,eyeY,eyeZ> <atX,atY,atZ> <hfov> \
//                <out.bin> [WxH] [mirrored.bin]
//
// The optional `mirrored.bin` writes the MIRRORED render's framebuffer beside
// the true one, in the same layout. It exists so a downstream check can use
// the reflected reading as a NEGATIVE CONTROL against a captured frame rather
// than reconstructing it in Python from a reading of this file - the mirror is
// a negation of the right vector, and `u = s x f` flips with it, so it is a
// 180-degree rotation of the image and not the horizontal flip it looks like.
// A control derived from a reading of the code is a control that shares the
// code's mistakes.
//
// `docs/PORTING.md` B6's 3D-rasterizer row: this is a REFERENCE implementation,
// not a port - the engine has no software 3D rasterizer, Direct3D drew every
// triangle. What is checkable is geometry and ordering, and what is emitted
// here is chosen so that a wrong CONVENTION shows up as a number rather than
// as a picture nobody looks at:
//
//   * the projection is differenced against `tools/camshot.py`, which projects
//     the same records independently and has been laid over a real screenshot;
//   * the MIRRORED reading - the one that was wrong for months, because a
//     convention wrong everywhere at once looks right from inside - is
//     rendered too, and must differ;
//   * the batch order is the engine's own (opaque, additive, multiply), so
//     what overlaps what is the engine's decision and not this file's.
#include "formats/tex3dt.h"
#include "o3de/geom3do.h"
#include "o3de/raster.h"
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
void triple(const char* s, float o[3]) {
    std::sscanf(s, "%f,%f,%f", &o[0], &o[1], &o[2]);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 7) {
        std::fprintf(stderr, "usage: run_raster <gamedata> <model.3DO> <eye> <at> "
                             "<hfov> <out.bin>\n");
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
    // The frame the camera projects into, and it is NOT always the
    // framebuffer: `tools/camshot.py` uses **800x440**, the letterboxed
    // camera-mode viewport ASSETS measures at 1.818:1, and the vertical fov
    // follows from the frame's shape (`tanv = tanh / (W/H)`). Projecting into
    // 640x480 and comparing against an 800x440 reference disagrees on every
    // visible vertex while agreeing on every one behind the camera - which is
    // exactly what the first run of this differential showed, 74 "agreements"
    // that were all both-behind.
    if (argc > 7) {
        int w = 0, h = 0;
        if (std::sscanf(argv[7], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
            cam.w = w; cam.h = h;
        }
    }

    omk::Surface fb(cam.w, cam.h, 0);
    std::vector<float> depth;
    omk::clearDepth(depth, fb.w, fb.h);
    const auto st = omk::drawGeometry(fb, depth, cam, geo, tex);

    // ...and the reflected reading, which must not agree.
    omk::RCamera mir = cam; mir.mirror = true;
    omk::Surface fb2(cam.w, cam.h, 0);
    std::vector<float> d2;
    omk::clearDepth(d2, fb2.w, fb2.h);
    (void)omk::drawGeometry(fb2, d2, mir, geo, tex);
    long differ = 0;
    for (std::size_t i = 0; i < fb.px.size(); ++i) differ += fb.px[i] != fb2.px[i];

    // ...and the pre-clip reading, which DROPPED a triangle straddling the
    // near cut instead of cutting it. At most cameras this changes nothing,
    // which is the point worth measuring: the bug was invisible from the one
    // camera the silhouette check uses and showed up the moment somebody flew
    // the viewer into a room. So what is emitted is the difference AND the
    // unlit-pixel counts, because the symptom is a HOLE - geometry missing,
    // not geometry moved.
    omk::RCamera nc = cam; nc.noClip = true;
    omk::Surface fb3(cam.w, cam.h, 0);
    std::vector<float> d3;
    omk::clearDepth(d3, fb3.w, fb3.h);
    const auto stNc = omk::drawGeometry(fb3, d3, nc, geo, tex);
    long clipDiffer = 0, holeClip = 0, holeNoClip = 0;
    for (std::size_t i = 0; i < fb.px.size(); ++i) {
        clipDiffer += fb.px[i] != fb3.px[i];
        holeClip   += fb.px[i] == 0;
        holeNoClip += fb3.px[i] == 0;
    }

    std::vector<std::int32_t> out = {
        static_cast<std::int32_t>(geo.corners.size()),
        static_cast<std::int32_t>(geo.batches.size()),
        static_cast<std::int32_t>(tex.size()),
        static_cast<std::int32_t>(st.triangles), static_cast<std::int32_t>(st.drawn),
        static_cast<std::int32_t>(st.behind),    static_cast<std::int32_t>(st.offscreen),
        static_cast<std::int32_t>(st.pixels),    static_cast<std::int32_t>(st.depthRejects),
        static_cast<std::int32_t>(st.hash),      static_cast<std::int32_t>(differ),
        static_cast<std::int32_t>(clipDiffer),  static_cast<std::int32_t>(holeClip),
        static_cast<std::int32_t>(holeNoClip),  static_cast<std::int32_t>(stNc.drawn),
    };
    std::printf("%s: %zu corners, %zu batches, %zu textures\n"
                "  %ld triangles -> %ld drawn, %ld behind, %ld offscreen; "
                "%ld px, %ld depth rejects\n"
                "  mirrored reading differs in %ld of %d pixels\n"
                "  near clip: %ld pixels vs the old drop rule; unlit %ld -> %ld\n",
                model.filename().string().c_str(), geo.corners.size(),
                geo.batches.size(), tex.size(), st.triangles, st.drawn,
                st.behind, st.offscreen, st.pixels, st.depthRejects,
                differ, fb.w * fb.h, clipDiffer, holeNoClip, holeClip);

    // A deterministic sample of PROJECTED corners, so `verify.py` can compare
    // them against `tools/camshot.py`'s own projector - the one that was laid
    // over a real screenshot of this very camera. Every 97th corner, which is
    // coprime with nothing in particular and spreads the sample across the
    // whole model rather than one wall.
    std::vector<float> proj;
    for (std::size_t i = 0; i < geo.corners.size(); i += 97) {
        const float w[3] = {geo.corners[i].x, geo.corners[i].y, geo.corners[i].z};
        const auto pr = omk::project(cam, w);
        proj.insert(proj.end(), {static_cast<float>(i), pr.ahead ? 1.0f : 0.0f,
                                 pr.x, pr.y, pr.z});
    }
    out.push_back(static_cast<std::int32_t>(proj.size() / 5));

    if (!omk::safeOutputPath(argv[6])) return 2;
    std::ofstream o(argv[6], std::ios::binary);
    const std::int32_t n = static_cast<std::int32_t>(out.size());
    o.write(reinterpret_cast<const char*>(&n), 4);
    o.write(reinterpret_cast<const char*>(out.data()),
            static_cast<std::streamsize>(out.size() * 4));
    o.write(reinterpret_cast<const char*>(proj.data()),
            static_cast<std::streamsize>(proj.size() * 4));
    for (auto v : fb.px) {
        const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
        o.write(b, 2);
    }

    // The mirrored render, on request and in the same layout - the header is
    // repeated so one reader serves both files.
    if (argc > 8) {
        if (!omk::safeOutputPath(argv[8])) return 2;
        std::ofstream m(argv[8], std::ios::binary);
        m.write(reinterpret_cast<const char*>(&n), 4);
        m.write(reinterpret_cast<const char*>(out.data()),
                static_cast<std::streamsize>(out.size() * 4));
        m.write(reinterpret_cast<const char*>(proj.data()),
                static_cast<std::streamsize>(proj.size() * 4));
        for (auto v : fb2.px) {
            const char b[2] = {static_cast<char>(v & 0xFF),
                               static_cast<char>(v >> 8)};
            m.write(b, 2);
        }
    }
    return 0;
}

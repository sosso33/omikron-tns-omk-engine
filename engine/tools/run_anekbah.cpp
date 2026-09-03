// SPDX-License-Identifier: GPL-3.0-or-later
// THE ANEKBAH PREDICTION, RENDERED - the repo's oldest unfalsified claim.
//
//     run_anekbah <gamedata> <out.bin>
//
// `docs/ASSETS.md` 4b says the wrong shop sign is not a depth rule, a cull mode
// or a draw order but the **texture name cache**: `Tex3DT_BindMaterials` hands
// out 58 slots from a GLOBAL pool matching on the 19-character file name
// alone, and `Area_LoadSet` keeps TWO decor sets resident - hidden is not
// unloaded - so an incoming set cache-hits against the outgoing location's
// atlases. `traces/impasse-walk.log` announces AREAS 222 (AIMPASSE) then
// AREAS 0 (ANEKBAH), so a real capture walks that exact path, and **seven** of
// ANEKBAH's atlases name-match AIMPASSE's.
//
// The prediction that was never tested: **the same panel should look different
// depending on which location you walked in from.** Until there was a
// rasterizer that was an inference about an atlas. Now it is a picture:
// render Anekbah's sign panels twice, once with its own textures and once with
// the substitution the cache would perform, and count the pixels that move.
//
// The camera is derived, not chosen: it is placed off the centroid of the
// triangles that actually sample `BATITR12` - the atlas ASSETS names as the
// one the wrong panels use - along that surface's own average normal, so the
// framing follows the data rather than a number someone liked.
#include "formats/tex3dt.h"
#include "o3de/geom3do.h"
#include "o3de/raster.h"
#include "platform/datafs.h"
#include "ui/surface.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Model {
    omk::Geometry geo;
    std::vector<omk::Texture> tex;
};

Model load(const std::string& dir, const std::string& stem) {
    Model m;
    const auto d = omk::DataFs::readPath(dir + "/" + stem + ".3DO");
    const auto t = omk::DataFs::readPath(dir + "/" + stem + ".3DT");
    if (d.empty()) return m;
    m.geo = omk::buildGeometry(d, omk::DrawFilter::Engine);
    if (!t.empty()) m.tex = omk::textures(d, t);
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: run_anekbah <gamedata> <out.bin>\n");
        return 2;
    }
    const std::string dir = std::string(argv[1]) + "/MESHES/DECORS";
    Model ane = load(dir, "Anekbah");
    const char* neighbours[2] = {"AImpasse", "AToit"};
    if (ane.geo.corners.empty()) {
        std::fprintf(stderr, "could not load Anekbah\n");
        return 1;
    }

    // The camera, derived from the geometry that samples BATITR12 - the atlas
    // ASSETS 4b names as the one the wrong panels use.
    int target = -1;
    for (std::size_t i = 0; i < ane.tex.size(); ++i)
        if (ane.tex[i].name == "BATITR12") { target = static_cast<int>(i); break; }
    double cx = 0, cy = 0, cz = 0;
    long n = 0;
    for (const auto& b : ane.geo.batches) {
        if (b.material != target) continue;
        for (std::size_t i = b.start; i < b.start + b.count; ++i) {
            cx += ane.geo.corners[i].x; cy += ane.geo.corners[i].y;
            cz += ane.geo.corners[i].z; ++n;
        }
    }
    if (!n) { std::fprintf(stderr, "no geometry samples BATITR12\n"); return 1; }
    cx /= n; cy /= n; cz /= n;

    omk::RCamera cam;
    cam.at[0] = static_cast<float>(cx);
    cam.at[1] = static_cast<float>(cy);
    cam.at[2] = static_cast<float>(cz);
    cam.eye[0] = cam.at[0];
    cam.eye[1] = cam.at[1];
    cam.eye[2] = cam.at[2] + 900.0f;
    cam.hfovDeg = 74.0f;
    cam.w = 640; cam.h = 480;

    // The reference frame, and the sign's own coverage in it.
    omk::Surface a(cam.w, cam.h, 0);
    std::vector<float> da;
    omk::clearDepth(da, cam.w, cam.h);
    const auto sa = omk::drawGeometry(a, da, cam, ane.geo, ane.tex);

    // WHICH PIXELS THE SIGN OWNS, unambiguously. The first attempt rendered
    // the sign alone and called a pixel "the sign" where its depth tied the
    // full frame's - a loose test that admits any coplanar neighbour, and it
    // showed: two DIFFERENT substitute atlases each moved exactly 531 pixels,
    // which is not a thing two different textures do. It was measuring the
    // mask, not the game.
    //
    // This instead repaints the atlas a flat colour and re-renders the whole
    // set: a pixel belongs to the sign exactly when that changes it. No depth
    // comparison, no tie, and it answers with the frame the player sees.
    std::vector<omk::Texture> marked = ane.tex;
    if (target >= 0) {
        auto& m = marked[static_cast<std::size_t>(target)];
        for (std::size_t k = 0; k + 2 < m.rgb.size(); k += 3) {
            m.rgb[k] = 255; m.rgb[k + 1] = 0; m.rgb[k + 2] = 255;
        }
    }
    omk::Surface c(cam.w, cam.h, 0);
    std::vector<float> dc;
    omk::clearDepth(dc, cam.w, cam.h);
    omk::drawGeometry(c, dc, cam, ane.geo, marked);
    std::vector<char> isSign(a.px.size(), 0);
    long visibleSign = 0;
    for (std::size_t i = 0; i < a.px.size(); ++i)
        if (a.px[i] != c.px[i]) { isSign[i] = 1; ++visibleSign; }

    long lit = 0;
    for (auto px : a.px) if (px) ++lit;

    std::vector<std::int32_t> out = {
        static_cast<std::int32_t>(ane.tex.size()), target,
        static_cast<std::int32_t>(sa.drawn),
        static_cast<std::int32_t>(lit),
        static_cast<std::int32_t>(visibleSign),
    };
    std::printf("Anekbah: %zu textures, BATITR12 is material %d\n"
                "  %ld triangles, %ld lit px; the sign shows %ld px "
                "after occlusion\n",
                ane.tex.size(), target, sa.drawn, lit, visibleSign);

    // ...then each neighbour a real walk could leave resident.
    for (const char* nb : neighbours) {
        Model other = load(dir, nb);
        std::vector<omk::Texture> swapped = ane.tex;
        int shared = 0;
        for (auto& t : swapped)
            for (const auto& u : other.tex)
                if (!t.name.empty() && t.name == u.name) { t = u; ++shared; break; }

        omk::Surface b(cam.w, cam.h, 0);
        std::vector<float> db;
        omk::clearDepth(db, cam.w, cam.h);
        omk::drawGeometry(b, db, cam, ane.geo, swapped);

        long any = 0, vis = 0, sAny = 0, sVis = 0;
        for (std::size_t i = 0; i < a.px.size(); ++i) {
            if (a.px[i] == b.px[i]) continue;
            const int ar = ((a.px[i] >> 11) & 0x1F) * 255 / 31;
            const int ag = ((a.px[i] >> 5) & 0x3F) * 255 / 63;
            const int ab = (a.px[i] & 0x1F) * 255 / 31;
            const int br = ((b.px[i] >> 11) & 0x1F) * 255 / 31;
            const int bg = ((b.px[i] >> 5) & 0x3F) * 255 / 63;
            const int bb2 = (b.px[i] & 0x1F) * 255 / 31;
            const bool seen = std::abs(ar - br) + std::abs(ag - bg) +
                              std::abs(ab - bb2) > 24;
            ++any; vis += seen;
            if (isSign[i]) { ++sAny; sVis += seen; }
        }
        out.insert(out.end(), {shared, static_cast<std::int32_t>(any),
                               static_cast<std::int32_t>(vis),
                               static_cast<std::int32_t>(sAny),
                               static_cast<std::int32_t>(sVis)});
        std::printf("  %-9s %d atlases substituted -> frame %ld px move "
                    "(%ld visibly); of the SIGN %ld move (%ld visibly)\n",
                    nb, shared, any, vis, sAny, sVis);
    }

    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream o(argv[2], std::ios::binary);
    const std::int32_t nn = static_cast<std::int32_t>(out.size());
    o.write(reinterpret_cast<const char*>(&nn), 4);
    o.write(reinterpret_cast<const char*>(out.data()),
            static_cast<std::streamsize>(out.size() * 4));
    return 0;
}

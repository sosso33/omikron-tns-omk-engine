// SPDX-License-Identifier: GPL-3.0-or-later
// THE REMASTER'S ENGINE-SIDE HELPERS (`o3de/texpack.h`,
// `todo/remaster.md` B4) - for `tools/omktexpack.py` and for
// `verify.py: engine: texture pack`.
//
//     texpack_tool keys <gamedata>          every .3DO/.3DT pair under MESHES:
//                                           "<path> <material> <key> <w>x<h>"
//     texpack_tool png <in.png> <out.raw>   the engine's PNG reader's answer:
//                                           int32 w, int32 h, then w*h RGBA8
//     texpack_tool pngpar <in.png> <n>      the reader on the THREAD POOL: the
//                                           file decoded n times at once, first
//                                           use included - "pngpar n same k"
//     texpack_tool water <model.3DO>...     the remaster's water plane per set
//                                           (`o3de/renderer.h` waterPlane):
//                                           "<model> found y area triangles"
//                                           (OMK_WATER_LEVELS=1: every level
//                                           past 1e4 below each, "level y area n")
//     texpack_tool lights <pack> <model.3DO>   the remaster's GLOWING SURFACES
//                                           (`o3de/emitterlight.h`): per
//                                           texture that would cast light,
//                                           "TEX lit neon RRGGBB key", by lit area -
//                                           the start of a set's `lights.txt`
//     texpack_tool probe <model.3DO> x,y,z...  the remaster's BODY LIGHT
//                                           (`o3de/lightprobe.h`): the set's
//                                           mean, then per point the painted
//                                           colour and the tint at strength 1
//                                           of a grey of 0.2
//     texpack_tool cube <file.cube>         the engine's .cube reader's answer:
//                                           "cube N first r g b last r g b", or why not
//     texpack_tool normals <in.png> <out.raw> [base.png [key.cov [strength wide [fade]]]]
//                                           the remaster's NORMAL MAP of a
//                                           pack texture (`o3de/normalmap.h`),
//                                           over an AI map `base.png`, faded
//                                           at the borders of `islands`'
//                                           coverage ("-" for none of either),
//                                           as `png` writes: int32 w, h, w*h RGBA8
//     texpack_tool floors <model.3DO>       a set's LEVEL opaque triangles by
//                                           texture and height, the most area
//                                           first - the reflecting floors' list
//     texpack_tool open <pack>              the pack as the engine opens it:
//                                           "@pack files N lights N normals N
//                                           excluded N" (exclude.txt, B2)
//     texpack_tool islands <gamedata> <outdir>
//                                           every texture's COVERAGE by the
//                                           faces that sample it, <key>.cov
//     texpack_tool grey <pack> <model.3DO>  the GREYSCALE BANK through a pack:
//                                           each texture greyed as the viewer
//                                           greys it, aliased, and the pack's
//                                           answer compared with the engine's
//                                           own greyed texture enlarged by
//                                           nearest neighbour (a nearest pack)
//     texpack_tool fontline <gamedata> <tables> <fontpack|-> <face> <num> <den> <filter> <text> <out.raw>
//                                           THE FONT PACK (`ui/fontpack.h`): one
//                                           line of `text` in `face` drawn by
//                                           `TextLayout` at the glyph scale
//                                           num/den, white on black - int32 w, h,
//                                           then w*h 565 - "@fontline hd <faces
//                                           decoded> w <w> h <h>"
//     texpack_tool filmkey <file>...         THE FILM PACK (`platform/filmpack.h`):
//                                           "<key> <file>", the engine's `filmKey`
//     texpack_tool filmframe <gamedata> <filmpack|-> <FLIS/NAME.mpg> <W> <H> <n> <out.raw>
//                                           the film the player would open, its
//                                           n-th frame decoded into W x H as the
//                                           player draws it (smoothed when from
//                                           the pack) - w*h 565 - "@filmframe hd
//                                           <0|1> size <w>x<h> frames <n>
//                                           duration <s>"
//     texpack_tool switchpack <pack> <out> <colourMax> <lightMax>
//                                           THE SWITCH'S PACK (`todo/switch-port.md`):
//                                           every BC7 colour and BC4 light map
//                                           of <pack> (the desktop's KTX2s, the
//                                           normal maps left out - GLES draws no
//                                           relief) with its levels ABOVE the cap
//                                           dropped - the same blocks, no
//                                           re-encoding - into <out>; one kept
//                                           newer than its source is not redone
//     texpack_tool packarchive <dir> <out.omkp>
//                                           THE PACK'S ARCHIVE (`o3de/texpack.h`
//                                           kPackArchiveName): every .ktx2 and
//                                           .png of <dir> in one file, sorted
//                                           by name, each at a 512-byte boundary
//     texpack_tool ktx2 <file>...            THE BLOCK FORMATS (`formats/ktx2.h`):
//                                           "@ktx2 ok <vkFormat> <w>x<h> levels <n>"
//                                           or "@ktx2 refused <why>", a file a line
//
// `keys` is the engine's half of a differential: the Python tool keys the same
// textures from its own decoder, and the two must agree on every one, or a
// pack made by one is never found by the other. `png` is the engine's reader
// on a file, so the Python tool can read an upscaler's PNG fast and the check
// can difference the reader against Python's zlib.
#include "formats/tex3dt.h"
#include "o3de/colourgrade.h"
#include "o3de/geom3do.h"
#include "o3de/emitterlight.h"
#include "o3de/greybank.h"
#include "o3de/lightprobe.h"
#include "o3de/normalmap.h"
#include "o3de/renderer.h"
#include "o3de/texpack.h"
#include "platform/env.h"
#include "platform/datafs.h"
#include "platform/pngread.h"
#include "platform/threads.h"
#include "formats/ktx2.h"
#include "platform/filmpack.h"
#include "platform/movie.h"
#include "ui/fontpack.h"
#include "ui/text.h"
#include "ui/surface.h"
#include "ui/uipack.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return s;
}

// Every .3DO under the data's MESHES with its .3DT beside it, in path order
// (`keys` and `islands`); empty, said, when there is no MESHES.
std::vector<std::pair<fs::path, fs::path>> modelsWithTextures(const std::string& data) {
    std::vector<std::pair<fs::path, fs::path>> out;
    fs::path meshes;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(data, ec))
        if (e.is_directory() && lower(e.path().filename().string()) == "meshes") meshes = e.path();
    if (meshes.empty()) { std::fprintf(stderr, "no MESHES under %s\n", data.c_str()); return out; }
    std::vector<fs::path> models;
    for (const auto& e : fs::recursive_directory_iterator(meshes, ec))
        if (e.is_regular_file() && lower(e.path().extension().string()) == ".3do") models.push_back(e.path());
    std::sort(models.begin(), models.end());
    for (const auto& m : models)
        for (const char* ext : {".3dt", ".3DT", ".3Dt"}) {
            auto c = m; c.replace_extension(ext);
            if (fs::exists(c)) { out.emplace_back(m, c); break; }
        }
    return out;
}

// THE INTERFACE PACK's keys (`ui/uipack.h`): every BMP the interface loads,
// as `surfaceFromBmp` decodes it - "<key> <path> <w>x<h>", the key the
// engine's `uiKey`, so `tools/omktexpack.py ui` names its files by the
// engine's own hash rather than a re-derivation of it
int uikeys(const std::string& data) {
    omk::DataFs fs(data);
    long n = 0;
    for (const char* dir : {"I2D/bitmaps", "IMAGES", "IMAGES/French", "FONTS"})
        for (const auto& path : fs.list(dir, ".bmp")) {
            const auto s = omk::surfaceFromBmp(omk::DataFs::readPath(path));
            if (!s.valid()) continue;
            char key[17];
            std::snprintf(key, sizeof key, "%016llx", static_cast<unsigned long long>(omk::uiKey(s)));
            std::printf("%s %s %dx%d\n", key, fs::relative(path, data).generic_string().c_str(), s.w, s.h);
            ++n;
        }
    std::fprintf(stderr, "%ld bitmaps\n", n);
    return n ? 0 : 1;
}

// ...and its effect, without the viewer: the bitmap `rel` blitted whole into
// a W x H surface - the interface's own stretch (`blt`, key 0, filter 1) -
// with the pack `pack` open ("-": none). Writes the 565 frame to `out` and
// prints "@uiblit hd <0|1> drawn <n>"
int uiblit(char** argv) {
    const std::string data = argv[2], pack = argv[3], rel = argv[4], out = argv[7];
    const int w = std::atoi(argv[5]), h = std::atoi(argv[6]);
    if (pack != "-" && !omk::uiPack().open(pack)) return 1;
    omk::DataFs fs(data);
    const auto at = fs.resolve(rel);
    if (!at || w <= 0 || h <= 0) return 1;
    const auto src = omk::surfaceFromBmp(omk::DataFs::readPath(*at));
    if (!src.valid()) return 1;
    omk::Surface dst(w, h, 0x07E0);   // green: a keyed texel shows it through
    omk::blt(dst, omk::Rect{0, 0, w, h}, src, omk::Rect{0, 0, src.w, src.h}, omk::kBltKeySrc, 0, 0, 1);
    std::FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) return 1;
    std::fwrite(dst.px.data(), 2, dst.px.size(), f);
    std::fclose(f);
    const auto st = omk::uiPack().stats();
    std::printf("@uiblit hd %d drawn %zu\n", st.drawn > 0 ? 1 : 0, st.drawn);
    return 0;
}

int fontline(char** argv) {
    const std::string data = argv[2], tables = argv[3], pack = argv[4], text = argv[9], out = argv[10];
    const char face = argv[5][0];
    const int num = std::atoi(argv[6]), den = std::atoi(argv[7]), filter = std::atoi(argv[8]);
    if (pack != "-" && !omk::fontPack().open(pack)) return 1;
    const auto table = omk::FontTable::loadJson(tables + "/ui.json");
    omk::TextLayout lay(table, data + "/FONTS");
    lay.setGlyphScale(num, den, filter);
    const auto run = omk::parseMarkup(text, face).run;
    const int w = lay.measure(run) + 8, h = lay.height(run) * 2 + 8;
    if (w <= 8 || num <= 0 || den <= 0) return 1;
    omk::Surface dst(w, h, 0);
    lay.drawRun(dst, 4, 4, run);
    std::FILE* f = omk::safeOutputPath(out) ? std::fopen(out.c_str(), "wb") : nullptr;
    if (!f) return 1;
    const std::int32_t wh[2] = {w, h};
    std::fwrite(wh, 4, 2, f);
    std::fwrite(dst.px.data(), 2, dst.px.size(), f);
    std::fclose(f);
    std::printf("@fontline hd %zu w %d h %d\n", omk::fontPack().stats().decoded, w, h);
    return 0;
}

int filmframe(char** argv) {
    const std::string data = argv[2], pack = argv[3], rel = argv[4], out = argv[8];
    const int w = std::atoi(argv[5]), h = std::atoi(argv[6]), n = std::atoi(argv[7]);
    if (pack != "-" && !omk::filmPack().open(pack)) return 1;
    omk::DataFs fs(data);
    const auto real = fs.resolve(rel);
    if (!real || w <= 0 || h <= 0 || n <= 0) return 1;
    const std::string hd = omk::filmPack().find(*real);
    omk::Movie mov;
    if (!mov.open(hd.empty() ? *real : hd)) return 1;
    mov.setSmooth(!hd.empty());
    omk::Surface dst(w, h, 0);
    for (int k = 0; k < n; ++k)
        if (!mov.nextFrame(dst)) break;
    std::FILE* f = omk::safeOutputPath(out) ? std::fopen(out.c_str(), "wb") : nullptr;
    if (!f) return 1;
    std::fwrite(dst.px.data(), 2, dst.px.size(), f);
    std::fclose(f);
    std::printf("@filmframe hd %d size %dx%d frames %ld duration %.3f\n", hd.empty() ? 0 : 1, mov.info().width,
                mov.info().height, mov.framesDecoded(), mov.info().duration);
    return 0;
}

int keys(const std::string& data) {
    const auto models = modelsWithTextures(data);
    if (models.empty()) return 1;
    long n = 0;
    for (const auto& [m, t] : models) {
        const auto d = omk::DataFs::readPath(m.string());
        const auto tb = omk::DataFs::readPath(t.string());
        const auto tex = omk::textures(d, tb);
        const std::string rel = fs::relative(m, data).generic_string();
        for (std::size_t i = 0; i < tex.size(); ++i) {
            if (!tex[i].hasPixels()) continue;
            std::printf("%s %zu %s %dx%d\n", rel.c_str(), i,
                        omk::textureKeyHex(omk::textureKey(tex[i])).c_str(), tex[i].width, tex[i].height);
            ++n;
        }
    }
    std::fprintf(stderr, "%ld textures in %zu models (those with a .3DT)\n", n, models.size());
    return 0;
}

// THE ISLANDS: which texels of each texture a face samples, over every model
// that draws it - the normal maps' guard against an ATLAS (`o3de/normalmap.h`).
// BATITR09, Qalisar's floor, is three surfaces in one texture; a generated map
// draws a ridge where two meet, and a face mapped to one of them up to its
// border draws the ridge as a line along its edge. Each face's UV triangle
// (texels, the original's size) is rasterised by its texel centres, wrapped;
// one spanning the texture or more TILES, and covers it all. Coverage alone
// does not part BATITR09's beige from its green - both are drawn, and they
// touch - so the texture is cut into REGIONS: faces whose UV triangles
// OVERLAP (share a texel centre a texel or more inside each - BATITR09's
// beige and green faces meet on a line, give or take a texel) are one
// region, and a covered texel beside another region's is a BORDER. Not
// tried twice: a UV edge only one triangle has (the sets cut their faces
// freely - 170786 such edges, most inside a picture), and a 3D edge across
// which the texels jump (the paving restarts at nearly every face, 567299).
// `<outdir>/<key>.cov`: int32 w, h, then w*h bytes: 0 no face samples the
// texel, 1 covered, 2 on a chart's border.
int islands(const std::string& data, const std::string& outdir) {
    const auto models = modelsWithTextures(data);
    if (models.empty()) return 1;
    // `id` the texel's first triangle (-1 none), `up` the triangles' union-find
    struct Cover { int w = 0, h = 0; std::vector<std::uint8_t> m; std::vector<int> id, up; };
    const auto root = [](std::vector<int>& up, int a) {
        while (up[static_cast<std::size_t>(a)] != a) a = up[static_cast<std::size_t>(a)] = up[static_cast<std::size_t>(up[static_cast<std::size_t>(a)])];
        return a;
    };
    std::map<std::uint64_t, Cover> cov;
    long faces = 0, tiled = 0;
    for (const auto& [m, t] : models) {
        const auto d = omk::DataFs::readPath(m.string());
        const auto tex = omk::textures(d, omk::DataFs::readPath(t.string()));
        const omk::Geometry g = omk::buildGeometry(d, omk::DrawFilter::Engine);
        for (const auto& b : g.batches) {
            if (b.material < 0 || static_cast<std::size_t>(b.material) >= tex.size()) continue;
            const omk::Texture& x = tex[static_cast<std::size_t>(b.material)];
            if (!x.hasPixels()) continue;
            Cover& c = cov[omk::textureKey(x)];
            const int w = x.width, h = x.height;
            if (c.m.empty()) {
                c.w = w; c.h = h; c.m.assign(static_cast<std::size_t>(w) * h, 0);
                c.id.assign(c.m.size(), -1);
            }
            for (std::size_t k = b.start; k + 2 < b.start + b.count; k += 3) {
                const omk::Corner* p = &g.corners[k];
                ++faces;
                float u0 = std::min({p[0].u, p[1].u, p[2].u}), u1 = std::max({p[0].u, p[1].u, p[2].u});
                float v0 = std::min({p[0].v, p[1].v, p[2].v}), v1 = std::max({p[0].v, p[1].v, p[2].v});
                if (u1 - u0 >= w || v1 - v0 >= h) { std::fill(c.m.begin(), c.m.end(), 1); ++tiled; continue; }
                const auto mark = [&](int x0, int y0) {
                    c.m[static_cast<std::size_t>(((y0 % h) + h) % h) * w + ((x0 % w) + w) % w] = 1;
                };
                // the corners' own texels, so a sliver covering no centre still counts
                for (int q = 0; q < 3; ++q)
                    mark(static_cast<int>(std::floor(p[q].u)), static_cast<int>(std::floor(p[q].v)));
                const float area = (p[1].u - p[0].u) * (p[2].v - p[0].v) - (p[2].u - p[0].u) * (p[1].v - p[0].v);
                if (std::fabs(area) < 1e-6f) continue;
                const float s = area > 0 ? 1.0f : -1.0f;
                const int tri = static_cast<int>(c.up.size());
                c.up.push_back(tri);
                for (int y = static_cast<int>(std::floor(v0)); y <= static_cast<int>(std::ceil(v1)); ++y)
                    for (int xx = static_cast<int>(std::floor(u0)); xx <= static_cast<int>(std::ceil(u1)); ++xx) {
                        const float cu = xx + 0.5f, cv = y + 0.5f;
                        // the centre's distance inside each edge, in texels
                        float inside = 1e9f;
                        for (int q = 0; q < 3; ++q) {
                            const omk::Corner& a = p[q];
                            const omk::Corner& bb = p[(q + 1) % 3];
                            const float len = std::hypot(bb.u - a.u, bb.v - a.v);
                            if (len > 0.0f)
                                inside = std::min(inside, s * ((bb.u - a.u) * (cv - a.v) - (bb.v - a.v) * (cu - a.u)) / len);
                        }
                        if (inside < -1e-4f) continue;
                        mark(xx, y);
                        if (inside < 1.0f) continue;
                        // a texel or more inside: this triangle's region is the texel's
                        int& first = c.id[static_cast<std::size_t>(((y % h) + h) % h) * w + ((xx % w) + w) % w];
                        if (first < 0) first = tri;
                        else c.up[static_cast<std::size_t>(root(c.up, tri))] = root(c.up, first);
                    }
            }
        }
    }
    long full = 0, borders = 0, regions = 0;
    for (auto& [key, c] : cov) {
        // the regions: a texel labelled by its region, one unlabelled (only
        // within a texel of an edge) by its nearest labelled neighbour's, over
        // a few passes; a covered texel beside another region's is a border
        std::vector<int> lab(c.m.size(), -1);
        for (std::size_t i = 0; i < lab.size(); ++i)
            if (c.id[i] >= 0) lab[i] = root(c.up, c.id[i]);
        const auto at = [&](int x, int y) {
            return static_cast<std::size_t>(((y % c.h) + c.h) % c.h) * c.w + ((x % c.w) + c.w) % c.w;
        };
        for (int pass = 0; pass < 3; ++pass) {
            std::vector<int> next = lab;
            for (int y = 0; y < c.h; ++y)
                for (int x = 0; x < c.w; ++x) {
                    const std::size_t i = at(x, y);
                    if (lab[i] >= 0 || !c.m[i]) continue;
                    for (const std::size_t j : {at(x + 1, y), at(x - 1, y), at(x, y + 1), at(x, y - 1)})
                        if (lab[j] >= 0) { next[i] = lab[j]; break; }
                }
            lab.swap(next);
        }
        std::vector<int> roots;
        for (int y = 0; y < c.h; ++y)
            for (int x = 0; x < c.w; ++x) {
                const std::size_t i = at(x, y);
                if (lab[i] < 0) continue;
                roots.push_back(lab[i]);
                for (const std::size_t j : {at(x + 1, y), at(x - 1, y), at(x, y + 1), at(x, y - 1)})
                    if (lab[j] >= 0 && lab[j] != lab[i]) { c.m[i] = 2; ++borders; break; }
            }
        std::sort(roots.begin(), roots.end());
        regions += std::unique(roots.begin(), roots.end()) - roots.begin();
        const std::string out = outdir + "/" + omk::textureKeyHex(key) + ".cov";
        if (!omk::safeOutputPath(out)) return 2;
        std::FILE* f = std::fopen(out.c_str(), "wb");
        if (!f) { std::fprintf(stderr, "%s: cannot write\n", out.c_str()); return 1; }
        const std::int32_t hdr[2] = {c.w, c.h};
        std::fwrite(hdr, sizeof hdr, 1, f);
        std::fwrite(c.m.data(), 1, c.m.size(), f);
        std::fclose(f);
        full += std::count(c.m.begin(), c.m.end(), 0) == 0;
    }
    std::printf("@islands textures=%zu covered_whole=%ld faces=%ld tiling_faces=%ld regions=%ld border_texels=%ld\n",
                cov.size(), full, faces, tiled, regions, borders);
    return 0;
}

// THE FLOORS of a set (`todo/remaster.md` A3, the reflecting floors): its
// LEVEL opaque triangles by texture and height, the most area first - what a
// `floors.txt` is drafted from. One line each: "<texture> <key> y <height>
// area <m2> triangles <n>" (area in square metres, 100 units a metre).
int floors(const std::string& model) {
    const auto d = omk::DataFs::readPath(model);
    fs::path t = model;
    for (const char* ext : {".3dt", ".3DT", ".3Dt"}) {
        auto c = fs::path(model); c.replace_extension(ext);
        if (fs::exists(c)) { t = c; break; }
    }
    if (d.empty() || t == fs::path(model)) { std::fprintf(stderr, "%s: no .3DO/.3DT pair\n", model.c_str()); return 1; }
    const auto tex = omk::textures(d, omk::DataFs::readPath(t.string()));
    const omk::Geometry g = omk::buildGeometry(d, omk::DrawFilter::Engine);
    struct Row { double area = 0; long tris = 0; };
    std::map<std::pair<int, int>, Row> rows;   // (material, y / 4)
    for (const auto& b : g.batches) {
        if (b.blend != omk::Blend::Opaque || b.material < 0 || static_cast<std::size_t>(b.material) >= tex.size()) continue;
        for (std::size_t k = b.start; k + 2 < b.start + b.count; k += 3) {
            const omk::Corner* p = &g.corners[k];
            if (std::fabs(p[0].y - p[1].y) > 2.0f || std::fabs(p[0].y - p[2].y) > 2.0f) continue;   // not level
            const double ux = p[1].x - p[0].x, uz = p[1].z - p[0].z, vx = p[2].x - p[0].x, vz = p[2].z - p[0].z;
            Row& r = rows[{b.material, static_cast<int>(std::lround(p[0].y / 4.0f))}];
            r.area += std::fabs(ux * vz - uz * vx) * 0.5 / 1e4;
            ++r.tris;
        }
    }
    std::vector<std::pair<std::pair<int, int>, Row>> v(rows.begin(), rows.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.area > b.second.area; });
    for (const auto& [k, r] : v) {
        const omk::Texture& x = tex[static_cast<std::size_t>(k.first)];
        std::printf("%s %s y %d area %.0f triangles %ld\n", x.name.c_str(),
                    x.hasPixels() ? omk::textureKeyHex(omk::textureKey(x)).c_str() : "-", k.second * 4, r.area, r.tris);
    }
    return 0;
}

int png(const std::string& in, const std::string& out) {
    const auto b = omk::readWholeFile(in);
    omk::PngImage img;
    std::string why;
    if (b.empty() || !omk::decodePng(reinterpret_cast<const std::uint8_t*>(b.data()), b.size(), img, &why)) {
        std::fprintf(stderr, "%s: %s\n", in.c_str(), b.empty() ? "unreadable" : why.c_str());
        return 1;
    }
    if (!omk::safeOutputPath(out)) return 2;
    std::FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) return 1;
    const std::int32_t hdr[2] = {img.w, img.h};
    std::fwrite(hdr, sizeof hdr, 1, f);
    std::fwrite(img.rgba.data(), 1, img.rgba.size(), f);
    std::fclose(f);
    return 0;
}

int normals(int argc, char** argv) {
    const std::string in = argv[2], out = argv[3];
    const auto b = omk::readWholeFile(in);
    omk::PngImage img;
    std::string why;
    if (b.empty() || !omk::decodePng(reinterpret_cast<const std::uint8_t*>(b.data()), b.size(), img, &why)) {
        std::fprintf(stderr, "%s: %s\n", in.c_str(), b.empty() ? "unreadable" : why.c_str());
        return 1;
    }
    omk::NormalMapParams p;
    omk::PngImage base;
    if (argc > 4 && std::string(argv[4]) != "-") {
        const auto bb = omk::readWholeFile(argv[4]);
        if (bb.empty() || !omk::decodePng(reinterpret_cast<const std::uint8_t*>(bb.data()), bb.size(), base, &why) ||
            base.w != img.w || base.h != img.h) {
            std::fprintf(stderr, "%s: %s\n", argv[4], bb.empty() ? "unreadable" : why.empty() ? "not the colour's size" : why.c_str());
            return 1;
        }
        p.base = base.rgba.data();
    }
    std::vector<std::uint8_t> cov;
    if (argc > 5 && std::string(argv[5]) != "-") {
        const auto cb = omk::readWholeFile(argv[5]);
        std::int32_t hdr[2] = {0, 0};
        if (cb.size() >= sizeof hdr) std::memcpy(hdr, cb.data(), sizeof hdr);
        if (hdr[0] <= 0 || hdr[1] <= 0 || cb.size() != sizeof hdr + static_cast<std::size_t>(hdr[0]) * hdr[1]) {
            std::fprintf(stderr, "%s: not a coverage (texpack_tool islands)\n", argv[5]);
            return 1;
        }
        cov.assign(reinterpret_cast<const std::uint8_t*>(cb.data()) + sizeof hdr,
                   reinterpret_cast<const std::uint8_t*>(cb.data()) + cb.size());
        p.cover = cov.data(); p.cw = hdr[0]; p.ch = hdr[1];
    }
    if (argc > 6) p.strength = static_cast<float>(std::atof(argv[6]));
    if (argc > 7) p.wide = std::atoi(argv[7]);
    if (argc > 8) p.fade = std::atoi(argv[8]);
    std::vector<std::uint8_t> map;
    omk::normalMap(img.rgba.data(), img.w, img.h, p, map);
    if (!omk::safeOutputPath(out)) return 2;
    std::FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) return 1;
    const std::int32_t hdr[2] = {img.w, img.h};
    std::fwrite(hdr, sizeof hdr, 1, f);
    std::fwrite(map.data(), 1, map.size(), f);
    std::fclose(f);
    return 0;
}

// A NEAREST pack (`omktexpack.py build --nearest N`) answers a grey texture
// with exactly the grey texture N times larger: the luma is per texel, so
// greying the enlargement is enlarging the grey - and the key (alpha 0 where
// the grey is 0) is `toRgbaKeyed` of the grey palette.
int grey(const std::string& pack, const std::string& model) {
    if (!omk::texturePack().open(pack)) return 1;
    fs::path m = model, t;
    for (const char* ext : {".3DT", ".3dt"}) { auto c = m; c.replace_extension(ext); if (fs::exists(c)) t = c; }
    const auto tex = omk::textures(omk::DataFs::readPath(model), omk::DataFs::readPath(t.string()));
    std::vector<omk::Texture> greyed;
    for (const auto& c : tex) {
        if (!c.hasPixels()) continue;
        omk::Texture g = c;
        g.pal = omk::greyPalette(c.pal);
        omk::texturePack().aliasGrey(c, g);
        greyed.push_back(g);
    }
    std::vector<const omk::Texture*> ptrs;
    for (const auto& g : greyed) ptrs.push_back(&g);
    const auto reps = omk::texturePack().replacements(ptrs);
    long replaced = 0, exact = 0;
    std::vector<std::uint8_t> want;
    for (std::size_t i = 0; i < greyed.size(); ++i) {
        if (!reps[i].ok) continue;
        ++replaced;
        const auto& g = greyed[i];
        const int n = reps[i].w / g.width;
        want.assign(static_cast<std::size_t>(g.width) * g.height * 4, 0);
        g.toRgbaKeyed(want.data());
        bool same = reps[i].h == g.height * n;
        for (int y = 0; same && y < reps[i].h; ++y)
            for (int x = 0; same && x < reps[i].w; ++x)
                same = std::memcmp(&reps[i].rgba[(static_cast<std::size_t>(y) * reps[i].w + x) * 4],
                                   &want[(static_cast<std::size_t>(y / n) * g.width + x / n) * 4], 4) == 0;
        exact += same;
    }
    std::printf("grey: %zu textures, %ld replaced, %ld exact\n", greyed.size(), replaced, exact);
    return 0;
}

// `TexturePack::replacements` decodes a pool on the pool: the reader's
// one-time state must survive its FIRST use being parallel (the fixed codes'
// race, 2026-10-20). Nothing is decoded before the parallel pass.
int pngpar(const std::string& in, int n) {
    const auto b = omk::readWholeFile(in);
    if (b.empty()) return 1;
    std::vector<omk::PngImage> out(static_cast<std::size_t>(n));
    std::vector<int> ok(static_cast<std::size_t>(n), 0);
    omk::Threads::shared().parallelFor(0, out.size(), 1, [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i)
            ok[i] = omk::decodePng(reinterpret_cast<const std::uint8_t*>(b.data()), b.size(), out[i]) ? 1 : 0;
    });
    int same = 0;
    for (std::size_t i = 0; i < out.size(); ++i)
        same += ok[i] && ok[0] && out[i].rgba == out[0].rgba ? 1 : 0;
    std::printf("pngpar %d same %d\n", n, same);
    return 0;
}

int water(int argc, char** argv) {
    for (int i = 2; i < argc; ++i) {
        const auto d = omk::DataFs::readPath(argv[i]);
        if (d.empty()) continue;
        const auto g = omk::buildGeometry(d, omk::DrawFilter::Engine);
        const auto w = omk::waterPlane(g);
        std::printf("%s %d %.1f %.0f %ld\n", fs::path(argv[i]).filename().string().c_str(), w.found ? 1 : 0,
                    static_cast<double>(w.y), w.area, w.triangles);
        if (omk::envFlag("OMK_WATER_LEVELS"))
            for (const auto& l : w.levels)
                std::printf("    level %.1f %.0f %ld\n", static_cast<double>(l.y), l.area, l.triangles);
    }
    return 0;
}

int probe(int argc, char** argv) {
    const auto d = omk::DataFs::readPath(argv[2]);
    if (d.empty()) return 1;
    omk::LightProbes lp;
    const auto g = omk::buildGeometry(d, omk::DrawFilter::Engine);
    lp.build(g);
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& c : g.corners) {
        const float v[3] = {c.x, c.y, c.z};
        for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], v[k]); hi[k] = std::max(hi[k], v[k]); }
    }
    std::printf("mean %.4f box %.0f %.0f %.0f .. %.0f %.0f %.0f\n", static_cast<double>(lp.meanLuma()),
                static_cast<double>(lo[0]), static_cast<double>(lo[1]), static_cast<double>(lo[2]),
                static_cast<double>(hi[0]), static_cast<double>(hi[1]), static_cast<double>(hi[2]));
    for (int i = 3; i < argc; ++i) {
        float p[3] = {0, 0, 0}, rgb[3] = {0, 0, 0}, t[3];
        if (std::sscanf(argv[i], "%f,%f,%f", &p[0], &p[1], &p[2]) != 3) continue;
        const bool ok = lp.sample(p, rgb);
        lp.tint(p, 1.0f, 0.2f, t);   // at a grey of 0.2 (51, Kay'l's flat's)
        std::printf("%s %d %.4f %.4f %.4f tint %.4f %.4f %.4f\n", argv[i], ok ? 1 : 0, static_cast<double>(rgb[0]),
                    static_cast<double>(rgb[1]), static_cast<double>(rgb[2]), static_cast<double>(t[0]),
                    static_cast<double>(t[1]), static_cast<double>(t[2]));
    }
    return 0;
}

int lights(const std::string& pack, const std::string& model) {
    if (!omk::texturePack().open(pack)) return 1;
    fs::path m = model, t;
    for (const char* ext : {".3DT", ".3dt"}) { auto c = m; c.replace_extension(ext); if (fs::exists(c)) t = c; }
    const auto d = omk::DataFs::readPath(model);
    const auto tex = omk::textures(d, omk::DataFs::readPath(t.string()));
    const auto g = omk::buildGeometry(d, omk::DrawFilter::Engine);
    std::vector<const omk::Texture*> ptrs;
    for (const auto& x : tex) ptrs.push_back(&x);
    const auto packs = omk::texturePack().replacements(ptrs);
    std::vector<omk::SurfaceStat> st;
    omk::surfaceLights(g, tex, packs, 1.0f, nullptr, "", nullptr, &st);
    std::map<std::string, std::string> keyOf;
    for (const auto& x : tex) if (x.hasPixels()) keyOf[x.name] = omk::textureKeyHex(omk::textureKey(x));
    std::sort(st.begin(), st.end(), [](const auto& a, const auto& b) { return a.lit > b.lit; });
    for (const auto& x : st) {
        if (x.lit < 1.0) continue;
        const double hi = std::max({x.r, x.g, x.b});
        const auto byte = [&](double v) { return static_cast<unsigned>(hi > 0 ? std::lround(v / hi * 255.0) : 255); };
        std::printf("%s %.0f %.2f %02x%02x%02x %s\n", x.texture.c_str(), x.lit, x.neon / x.lit, byte(x.r), byte(x.g), byte(x.b),
                    keyOf[x.texture].c_str());
    }
    return 0;
}

// THE SWITCH'S PACK: the desktop pack's BC KTX2s with their top levels
// dropped. The Switch's Mesa decodes ASTC on the CPU (~221 ms a 1024 with its
// mips, against 6-8 for BC1/BC3/BC4/BC7 - `glesrender_remaster.inl`'s upload
// probe, 2026-11-26), and its card is small.
int switchpack(const std::string& src, const std::string& dst, int colourMax, int lightMax) {
    std::error_code ec;
    fs::create_directories(dst, ec);
    int made = 0, kept = 0, failed = 0;
    std::size_t bytes = 0;
    std::map<std::string, int> sizes;
    for (const auto& e : fs::directory_iterator(src, fs::directory_options::follow_directory_symlink, ec)) {
        const std::string name = e.path().filename().string();
        const bool colour = name.size() == 25 && name.compare(16, 9, ".bc7.ktx2") == 0;
        const bool light = name.size() == 27 && name.compare(16, 11, "_e.bc4.ktx2") == 0;
        if (!colour && !light) continue;
        const fs::path out = fs::path(dst) / name;
        if (fs::exists(out) && fs::last_write_time(out) >= fs::last_write_time(fs::canonical(e.path()))) {
            ++kept;
            bytes += static_cast<std::size_t>(fs::file_size(out));
            continue;
        }
        const auto raw = omk::readWholeFile(e.path().string());
        std::vector<std::uint8_t> file(reinterpret_cast<const std::uint8_t*>(raw.data()),
                                       reinterpret_cast<const std::uint8_t*>(raw.data()) + raw.size());
        omk::Ktx2 k;
        std::string why;
        if (!omk::readKtx2(std::move(file), k, &why)) {
            std::fprintf(stderr, "%s: %s\n", name.c_str(), why.c_str());
            ++failed;
            continue;
        }
        const int cap = colour ? colourMax : lightMax;
        std::size_t first = 0;
        int w = k.w, h = k.h;
        while (first + 1 < k.levels.size() && std::max(w, h) > cap) {
            ++first;
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        }
        std::vector<std::vector<std::uint8_t>> levels;
        for (std::size_t l = first; l < k.levels.size(); ++l) {
            const auto lv = k.level(l);
            levels.emplace_back(lv.begin(), lv.end());
        }
        const auto o = omk::writeKtx2(k.vkFormat, w, h, levels);
        std::FILE* f = omk::safeOutputPath(out.string()) ? std::fopen(out.string().c_str(), "wb") : nullptr;
        if (!f || std::fwrite(o.data(), 1, o.size(), f) != o.size()) {
            if (f) std::fclose(f);
            std::fprintf(stderr, "%s: not written\n", out.string().c_str());
            ++failed;
            continue;
        }
        std::fclose(f);
        ++made;
        bytes += o.size();
        ++sizes[std::string(colour ? "bc7 " : "bc4 ") + std::to_string(w) + "x" + std::to_string(h)];
    }
    std::printf("switchpack: %d made, %d up to date, %d failed - %.1f MB in %s\n", made, kept, failed,
                static_cast<double>(bytes) / 1e6, dst.c_str());
    for (const auto& [s, n] : sizes) std::printf("  %s x%d\n", s.c_str(), n);
    return failed ? 1 : 0;
}

// THE PACK'S ARCHIVE: the layout `texpack.h` states, written in one pass
// (the index first, every offset known from the sizes)
int packarchive(const std::string& dir, const std::string& out) {
    std::vector<std::pair<std::string, std::uintmax_t>> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, fs::directory_options::follow_directory_symlink, ec)) {
        const std::string n = e.path().filename().string();
        const bool ktx = n.size() > 5 && n.compare(n.size() - 5, 5, ".ktx2") == 0;
        const bool png = n.size() > 4 && n.compare(n.size() - 4, 4, ".png") == 0;
        if ((!ktx && !png) || n.size() >= omk::kPackArchiveNameBytes) continue;
        files.emplace_back(n, fs::file_size(fs::canonical(e.path())));
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) { std::fprintf(stderr, "%s: no .ktx2 or .png\n", dir.c_str()); return 1; }
    const auto align = [](std::uint64_t v) {
        return (v + omk::kPackArchiveAlign - 1) / omk::kPackArchiveAlign * omk::kPackArchiveAlign;
    };
    std::vector<std::uint8_t> head(16 + files.size() * omk::kPackArchiveEntryBytes, 0);
    std::memcpy(head.data(), omk::kPackArchiveMagic, 8);
    const auto put = [&](std::size_t at, std::uint64_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) head[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    put(8, files.size(), 4);
    std::vector<std::uint64_t> offs;
    std::uint64_t off = align(head.size());
    for (std::size_t i = 0; i < files.size(); ++i) {
        const std::size_t e = 16 + i * omk::kPackArchiveEntryBytes;
        std::memcpy(head.data() + e, files[i].first.c_str(), files[i].first.size());
        put(e + omk::kPackArchiveNameBytes, off, 8);
        put(e + omk::kPackArchiveNameBytes + 8, files[i].second, 8);
        offs.push_back(off);
        off = align(off + files[i].second);
    }
    std::FILE* f = omk::safeOutputPath(out) ? std::fopen(out.c_str(), "wb") : nullptr;
    if (!f) { std::fprintf(stderr, "%s: not written\n", out.c_str()); return 1; }
    bool ok = std::fwrite(head.data(), 1, head.size(), f) == head.size();
    std::uint64_t at = head.size();
    const std::vector<char> zeros(omk::kPackArchiveAlign, 0);
    for (std::size_t i = 0; i < files.size() && ok; ++i) {
        ok = std::fwrite(zeros.data(), 1, static_cast<std::size_t>(offs[i] - at), f) == offs[i] - at;
        const auto b = omk::readWholeFile((fs::path(dir) / files[i].first).string());
        ok = ok && b.size() == files[i].second && std::fwrite(b.data(), 1, b.size(), f) == b.size();
        at = offs[i] + files[i].second;
    }
    std::fclose(f);
    if (!ok) { std::fprintf(stderr, "%s: a write failed\n", out.c_str()); return 1; }
    std::printf("packarchive: %zu files, %.1f MB in %s\n", files.size(), static_cast<double>(at) / 1e6, out.c_str());
    return 0;
}

int cube(const std::string& path) {
    omk::ColourLut lut;
    std::string why;
    if (!omk::parseCube(omk::readTextFile(path), lut, &why)) { std::printf("refused: %s\n", why.c_str()); return 0; }
    const std::size_t k = lut.rgb.size() - 3;
    std::printf("cube %d first %.6f %.6f %.6f last %.6f %.6f %.6f\n", lut.n, lut.rgb[0], lut.rgb[1], lut.rgb[2],
                lut.rgb[k], lut.rgb[k + 1], lut.rgb[k + 2]);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "keys" && argc == 3) return keys(argv[2]);
    if (mode == "uikeys" && argc == 3) return uikeys(argv[2]);
    if (mode == "uiblit" && argc == 8) return uiblit(argv);
    if (mode == "fontline" && argc == 11) return fontline(argv);
    if (mode == "filmframe" && argc == 9) return filmframe(argv);
    if (mode == "ktx2" && argc >= 3) {
        for (int i = 2; i < argc; ++i) {
            const auto b = omk::readWholeFile(argv[i]);
            std::vector<std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(b.data()),
                                            reinterpret_cast<const std::uint8_t*>(b.data()) + b.size());
            omk::Ktx2 k;
            std::string why;
            if (omk::readKtx2(std::move(bytes), k, &why))
                std::printf("@ktx2 ok %u %dx%d levels %zu\n", k.vkFormat, k.w, k.h, k.levels.size());
            else
                std::printf("@ktx2 refused %s\n", why.c_str());
        }
        return 0;
    }
    if (mode == "filmkey" && argc >= 3) {
        for (int i = 2; i < argc; ++i)
            std::printf("%016llx %s\n", static_cast<unsigned long long>(omk::filmKey(argv[i])), argv[i]);
        return 0;
    }
    if (mode == "png" && argc == 4) return png(argv[2], argv[3]);
    if (mode == "grey" && argc == 4) return grey(argv[2], argv[3]);
    if (mode == "cube" && argc == 3) return cube(argv[2]);
    if (mode == "packarchive" && argc == 4) return packarchive(argv[2], argv[3]);
    if (mode == "switchpack" && argc == 6)
        return switchpack(argv[2], argv[3], std::atoi(argv[4]), std::atoi(argv[5]));
    if (mode == "pngpar" && argc == 4 && std::atoi(argv[3]) > 0) return pngpar(argv[2], std::atoi(argv[3]));
    if (mode == "water" && argc >= 3) return water(argc, argv);
    if (mode == "probe" && argc >= 3) return probe(argc, argv);
    if (mode == "lights" && argc == 4) return lights(argv[2], argv[3]);
    if (mode == "normals" && argc >= 4 && argc <= 9) return normals(argc, argv);
    if (mode == "islands" && argc == 4) return islands(argv[2], argv[3]);
    if (mode == "floors" && argc == 3) return floors(argv[2]);
    if (mode == "open" && argc == 3) {
        // the pack as the engine opens it: what `exclude.txt` left out
        if (!omk::texturePack().open(argv[2])) return 1;
        const auto& p = omk::texturePack();
        std::printf("@pack files %zu lights %zu normals %zu excluded %zu\n", p.size(), p.lightMaps(),
                    p.normalMaps(), p.excluded());
        return 0;
    }
    std::fprintf(stderr, "usage: texpack_tool keys <gamedata> | png <in.png> <out.raw> | pngpar <in.png> <n>\n"
                         "       | grey <pack> <model.3DO> | cube <file.cube> | water <model.3DO>...\n"
                         "       | probe <model.3DO> ... | lights <pack> <model.3DO>\n"
                         "       | normals <in.png> <out.raw> [base.png [key.cov [strength wide [fade]]]]\n"
                         "       | islands <gamedata> <outdir> | floors <model.3DO> | open <pack>\n"
                         "       | uikeys <gamedata> | uiblit <gamedata> <uipack|-> <file.bmp> <w> <h> <out.raw>\n"
                         "       | fontline <gamedata> <tables> <fontpack|-> <face> <num> <den> <filter> <text> <out.raw>\n"
                         "       | filmkey <file>... | filmframe <gamedata> <filmpack|-> <FLIS/NAME.mpg> <W> <H> <n> <out.raw>\n"
                         "       (the header says what each answers)\n");
    return 2;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// THE GLES2 BACKEND AGAINST THE REFERENCE - `run_vulkan`'s differential, for
// the backend the PS Vita will draw with, run on the machine the port is
// developed on (`todo/vita-port.md` G2).
//
//     gles_probe <gamedata> <model.3DO> <eye> <at> <hfov> [WxH]
//
// Draws one set through `SoftwareRenderer` (the reference `verify.py` checks)
// and through `GlesRenderer` (backends/gles/glesrender.cpp), same camera, and
// prints four things:
//
//   1. COVERAGE AGREEMENT - both lit / either lit, the number `run_vulkan`
//      quotes (0.995 for Vulkan on the same set). Exact per-pixel equality is
//      NOT a criterion for a GPU backend; it is printed and not judged.
//   2. the same, DITHERED, since the dither is on by default in the game.
//   3. PRESENT = READBACK, which IS exact and is judged: the present pass
//      quantises 888 -> 565 in a GLSL ES 1.00 float shader, the readback
//      through `quantise888DitherRow` in C++. Every pixel must agree, dithered
//      and not, or the Vita's screen and the port's checks disagree about the
//      picture. `present: EXACT` or `present: N DIFFERENT`.
//   4. THE LETTERBOX - the same check with a 640x352 picture in a 640x480
//      frame, which is the case the present pass's row arithmetic can get
//      wrong.
//
// HEADLESS on macOS: a CGL context with no drawable, and the present pass
// pointed at an offscreen FBO (`glesSetWindowTarget`), so nothing opens a
// window and nothing takes the keyboard (CLAUDE.md §5's `SDL_VIDEODRIVER`
// lesson does not arise). The OpenGL framework is deprecated on macOS and
// still ships the 2.1 legacy profile, which is all this needs.
//
// Build (from engine/, after `make` has built the objects):
//
//     c++ -std=c++20 -O2 -Isrc -Ithird_party -o build/gles_probe \
//         backends/gles/gles_probe.cpp backends/gles/glesrender.cpp \
//         build/obj/src/*/*.o -framework OpenGL
//
// Exit status: 0 when the present checks are exact and coverage is at least
// 0.99, 1 otherwise - so a script can use it before `verify.py` knows of it.
// Prints only; writes nothing.
//
// THE POSE MODE (todo/gpu-skinning.md step 1):
//
//     gles_probe <gamedata> --pose <character.3DO> <line.3DM> <frame> <yaw>
//
// poses the character on the line's frame, turns it by `yaw` degrees and moves
// it, and draws it through the GLES backend TWICE: posed on the CPU
// (`applyPose` and the placement written out, what the viewer does today) and
// at REST with one affine a mesh (`meshAffines`, `Draw::meshPose`), posed by
// the vertex shader. Both with the depth tie off, since it is not part of
// what is compared. Prints the coverage agreement and the differing pixels;
// fails below 0.995 coverage or when the GPU path draws nothing.
#if !defined(__APPLE__)
#  error "gles_probe makes its context with CGL; on another host, bring one up with EGL"
#endif
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>

#include "actor/pose.h"
#include "formats/light3do.h"
#include "formats/mesh3do.h"
#include "o3de/vertexlight.h"
#include "formats/tex3dt.h"
#include "o3de/geom3do.h"
#include "o3de/renderer.h"
#include "platform/datafs.h"
#include "ui/surface.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <vector>

namespace omk {
Renderer* makeGlesRenderer();
bool glesPresentWorld(Renderer*, int vy, int vh, int frameW, int frameH, int winW, int winH);
bool glesPresentSurface(Renderer*, const Surface&, int winW, int winH);
void glesSetWindowTarget(Renderer*, unsigned fbo);
void glesSetDepthTie(Renderer*, bool);
void glesSetStateCache(Renderer*, bool);
void glesTakeStateCalls(long out[3]);
void glesGeometryStats(Renderer*, long out[3]);
int glesAnisotropy(Renderer*);
int glesSupersample(Renderer*);
void glesSamples(Renderer*, int* got, int* most);
void glesSetEnhancedLighting(Renderer*, bool perPixel, bool shadowMap);
}

namespace {

void triple(const char* s, float o[3]) { std::sscanf(s, "%f,%f,%f", &o[0], &o[1], &o[2]); }


struct Coverage { long sw = 0, gl = 0, both = 0, differ = 0; double agree() const {
    const long e = sw + gl - both; return e ? double(both) / double(e) : 1.0; } };

Coverage compare(const omk::Surface& a, const omk::Surface& b) {
    Coverage c;
    for (std::size_t i = 0; i < a.px.size() && i < b.px.size(); ++i) {
        const bool la = a.px[i] != 0, lb = b.px[i] != 0;
        c.sw += la; c.gl += lb; c.both += la && lb;
        c.differ += a.px[i] != b.px[i];
    }
    return c;
}

// An offscreen "window" to present into and read back.
struct Window {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    bool make(int ww, int hh) {
        w = ww; h = hh;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return ok;
    }
    // top-left origin RGBA
    std::vector<unsigned char> read() const {
        std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4), out(px.size());
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        for (int y = 0; y < h; ++y)
            std::copy_n(px.data() + static_cast<std::size_t>(h - 1 - y) * w * 4, w * 4,
                        out.data() + static_cast<std::size_t>(y) * w * 4);
        return out;
    }
};

// The window, 1:1 with the engine's frame, against the readback: the picture's
// rows `vy .. vy+vh` must be exactly the readback's rows, and every other row
// black. COMPARED IN 565, `surface.h`'s rule: how a 565 value becomes 888 on
// the way to the screen is the HOST's (this Mac's driver expands a 565
// texture by rounding, so blue 3 shows as 25 where bit replication gives 24;
// the first version of this probe compared in 888 and reported 113303
// "differences" that were all that), and it is not what is under test.
// Truncating the window back to 565 inverts both expansions exactly.
// -> how many pixels disagree.
long presentMismatches(const Window& win, const omk::Surface& rb, int vy) {
    const auto px = win.read();
    long bad = 0;
    for (int y = 0; y < win.h; ++y)
        for (int x = 0; x < win.w; ++x) {
            const unsigned char* p = px.data() + (static_cast<std::size_t>(y) * win.w + x) * 4;
            const int py = y - vy;
            const std::uint16_t want =
                (py >= 0 && py < rb.h && x < rb.w) ? rb.at(x, py) : std::uint16_t{0};
            const std::uint16_t got = omk::rgb565(p[0], p[1], p[2]);
            const bool miss = got != want;
            static const bool dbg = std::getenv("GLES_PROBE_DEBUG") != nullptr;
            if (miss && dbg && bad < 8)
                std::printf("    miss (%d,%d) got %04x want %04x\n", x, y, got, want);
            bad += miss;
        }
    return bad;
}

}  // namespace

// The pose mode - see the top.
int poseMode(int argc, char** argv) {
    if (argc < 7) { std::fprintf(stderr, "usage: gles_probe <gamedata> --pose <char.3DO> <line.3DM> <frame> <yaw>\n"); return 2; }
    const std::string model = argv[3];
    const auto md = omk::DataFs::readPath(model);
    const auto mh = omk::readHeader(md);
    if (md.empty() || !mh) { std::fprintf(stderr, "cannot read %s\n", model.c_str()); return 1; }
    const auto meshes = omk::readMeshes(md, *mh);
    omk::Geometry rest = omk::buildGeometry(md, omk::DrawFilter::Engine);
    // A CROWD MODEL carries four LOD skeletons; the viewer draws the one the
    // tracks name (`lodRestFor`). Cut to the FIRST root's subtree, so the body
    // stays inside the program's slots and the tie is exercised on the GPU path.
    {
        std::vector<int> rootOf(meshes.size(), -1);
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            int m = static_cast<int>(i);
            for (int guard = 0; guard < 64; ++guard) {
                int next = -1;
                for (std::size_t j = 0; j < meshes.size(); ++j)
                    if (meshes[j].id == meshes[static_cast<std::size_t>(m)].parent) { next = static_cast<int>(j); break; }
                if (next < 0) break;
                m = next;
            }
            rootOf[i] = m;
        }
        int roots = 0;
        for (std::size_t i = 0; i < meshes.size(); ++i) roots += rootOf[i] == static_cast<int>(i);
        if (roots > 1) {
            const int keep = rootOf[0];
            omk::Geometry cut;
            for (const auto& b : rest.batches) {
                omk::Batch nb = b;
                nb.start = cut.corners.size(); nb.count = 0;
                for (std::size_t c = b.start; c + 3 <= b.start + b.count; c += 3) {
                    const auto mi = rest.cornerMesh[c];
                    if (mi < 0 || rootOf[static_cast<std::size_t>(mi)] != keep) continue;
                    for (int k = 0; k < 3; ++k) {
                        cut.corners.push_back(rest.corners[c + k]);
                        cut.cornerMesh.push_back(rest.cornerMesh[c + k]);
                        if (!rest.cornerVertex.empty()) cut.cornerVertex.push_back(rest.cornerVertex[c + k]);
                        if (!rest.cornerDeclared.empty()) cut.cornerDeclared.push_back(rest.cornerDeclared[c + k]);
                    }
                    nb.count += 3;
                }
                if (nb.count) cut.batches.push_back(nb);
            }
            std::printf("%d skeletons: drawn cut to the first, %zu of %zu triangles\n", roots,
                        cut.corners.size() / 3, rest.corners.size() / 3);
            rest = std::move(cut);
        }
    }
    // A COINCIDENT BATCH, for the tie: the first batch's triangles again,
    // exactly over themselves, drawn with ANOTHER material - so which of the
    // two shows is the tie's decision (first drawn) and nothing else. The
    // shipped characters have no coincident faces inside one mesh, so without
    // this the tie would have nothing to decide on either path.
    if (!rest.batches.empty()) {
        omk::Batch dup = rest.batches[0];
        // another material where the body has one; a one-batch body (a cut
        // crowd skeleton) keeps its own, and the tie still decides
        dup.material = rest.batches.size() > 1 ? rest.batches[1].material : rest.batches[0].material;
        dup.start = rest.corners.size();
        // in the OPPOSITE WINDING, as the shop signs are (CLAUDE.md 6): the
        // same corners interpolate their depth in another order, so the two
        // faces differ in the last bits and a GPU picks per pixel
        const std::size_t b0 = rest.batches[0].start, n0 = rest.batches[0].count;
        for (std::size_t c = b0; c + 2 < b0 + n0 + 0 && c + 2 < rest.corners.size(); c += 3) {
            for (const std::size_t k : {c, c + 2, c + 1}) {
                rest.corners.push_back(rest.corners[k]);
                rest.cornerMesh.push_back(rest.cornerMesh[k]);
                if (!rest.cornerVertex.empty()) rest.cornerVertex.push_back(rest.cornerVertex[k]);
                if (!rest.cornerDeclared.empty()) rest.cornerDeclared.push_back(rest.cornerDeclared[k]);
            }
        }
        rest.batches.push_back(dup);
        std::printf("coincident batch: %zu triangles over batch 0, material %d\n",
                    dup.count / 3, dup.material);
    }
    const auto t = omk::DataFs::readPath(model.substr(0, model.size() - 4) + ".3DT");
    const auto tex = t.empty() ? std::vector<omk::Texture>{} : omk::textures(md, t);
    const auto morph = omk::DataFs::readPath(argv[4]);
    const omk::NodeTracks tracks = omk::nodeTracks(morph, omk::rootTrackOf(meshes));
    const int frame = std::atoi(argv[5]);
    const float yaw = static_cast<float>(std::atof(argv[6])) * 3.14159265f / 180.0f;
    const auto pose = omk::composePose(meshes, tracks, frame, false);

    // the placement: a turn about Y and a move, as a 3x4
    const float c = std::cos(yaw), sn = std::sin(yaw);
    const float place[12] = {c, 0, sn, 140.0f,  0, 1, 0, -25.0f,  -sn, 0, c, 60.0f};
    // THE CPU PATH, what the viewer does
    omk::Geometry posed;
    omk::applyPose(posed, rest, meshes, pose);
    for (auto& k : posed.corners) {
        const float x = k.x, y = k.y, z = k.z;
        k.x = place[0] * x + place[1] * y + place[2] * z + place[3];
        k.y = place[4] * x + place[5] * y + place[6] * z + place[7];
        k.z = place[8] * x + place[9] * y + place[10] * z + place[11];
    }
    // THE GPU PATH's affines
    std::vector<float> aff;
    omk::meshAffines(meshes, pose, place, aff);

    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto& k : posed.corners) {
        const float v[3] = {k.x, k.y, k.z};
        for (int i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], v[i]); hi[i] = std::max(hi[i], v[i]); }
    }
    omk::RCamera cam;
    for (int i = 0; i < 3; ++i) cam.at[i] = (lo[i] + hi[i]) * 0.5f;
    const float span = std::max(hi[1] - lo[1], std::max(hi[0] - lo[0], hi[2] - lo[2]));
    cam.eye[0] = cam.at[0] + span * 0.6f;
    cam.eye[1] = cam.at[1] - span * 0.2f;
    cam.eye[2] = cam.at[2] - span * 1.4f;
    cam.hfovDeg = 50.0f;
    const int W = 640, H = 480;
    omk::View v;
    v.cam = cam; v.cam.w = W; v.cam.h = H;
    v.dither = false;

    const auto drawsOf = [&](const omk::Geometry& g, const float* mp, std::size_t n) {
        std::vector<omk::Draw> out;
        for (const auto& b : g.batches) {
            omk::Draw dr;
            dr.bucketKey = static_cast<std::uint32_t>(b.material) & 0x3Fu;
            dr.geo = &g; dr.start = b.start; dr.count = b.count;
            dr.blend = b.blend; dr.cutout = b.cutout;
            dr.meshPose = mp; dr.meshPoses = n;
            out.push_back(dr);
        }
        return out;
    };
    omk::Renderer* gl = omk::makeGlesRenderer();
    if (!gl->init(W, H)) { std::fprintf(stderr, "gles renderer failed to come up\n"); return 1; }
    std::printf("posesBodies %d\n", gl->posesBodies() ? 1 : 0);
    const auto run = [&](const std::vector<omk::Draw>& ds) {
        gl->setTextures(tex);
        gl->begin(v);
        for (const auto& dr : ds) gl->submit(dr);
        gl->end();
        return gl->readback();
    };
    std::printf("%s frame %d yaw %s  %zu triangles  %zu meshes\n", model.c_str(), frame, argv[6],
                rest.corners.size() / 3, meshes.size());
    bool ok = true;
    omk::Surface gpuUntied(1, 1, 0);
    // THE TIE OFF, then ON; and on, over FOUR consecutive frames through the
    // same renderer - the posed tie is resolved once and replayed per frame,
    // and a replay that drifted would show on the later frames
    for (int tie = 0; tie <= 1; ++tie) {
        omk::glesSetDepthTie(gl, tie != 0);
        for (int step = 0; step < (tie ? 4 : 1); ++step) {
            const auto poseN = omk::composePose(meshes, tracks, frame + 7 * step, false);
            omk::applyPose(posed, rest, meshes, poseN);
            for (auto& k : posed.corners) {
                const float x = k.x, y = k.y, z = k.z;
                k.x = place[0] * x + place[1] * y + place[2] * z + place[3];
                k.y = place[4] * x + place[5] * y + place[6] * z + place[7];
                k.z = place[8] * x + place[9] * y + place[10] * z + place[11];
            }
            omk::meshAffines(meshes, poseN, place, aff);
            const omk::Surface cpu = run(drawsOf(posed, nullptr, 0));
            const omk::Surface gpu = run(drawsOf(rest, aff.data(), meshes.size()));
            const omk::Surface still = run(drawsOf(rest, nullptr, 0));
            const Coverage cv = compare(cpu, gpu);
            const Coverage rs = compare(cpu, still);
            std::printf("tie %s frame %d: cpu posed lit %ld  gpu posed lit %ld  coverage %.4f  "
                        "differing %ld (%.2f%%)  [unposed rest: %.4f]\n",
                        tie ? "on " : "off", frame + 7 * step, cv.sw, cv.gl, cv.agree(), cv.differ,
                        100.0 * double(cv.differ) / double(cpu.px.size()), rs.agree());
            ok = ok && cv.gl > 0 && cv.agree() >= 0.995 && rs.agree() < cv.agree();
            if (!tie) gpuUntied = gpu;
            else if (step == 0) {
                // what the tie CHANGED on the posed path - on a model with the
                // coincident batch this must not be nothing, or the tie was
                // never reached and "tie on" above proves nothing
                const Coverage tv = compare(gpuUntied, gpu);
                std::printf("gpu tie on against tie off: %ld pixels differ\n", tv.differ);
            }
        }
    }
    // THE LIGHT (step 2): three lights around the body - one strong enough
    // to reach `lightRamp`'s 255 clamp - lit on the CPU by `applyLights` on
    // the posed normals, and on the GPU from `lightReach`'s list. From BLACK
    // (the crowd's rule) and on top of the baked colour. The largest channel
    // difference is quoted in 565 steps, the picture's own resolution.
    {
        float at[3];
        for (int i = 0; i < 3; ++i) at[i] = cam.at[i];
        std::vector<omk::Light3do> lights(3);
        const float from[3][3] = {{-300, -200, -250}, {280, -120, 200}, {20, -400, 40}};
        const float f32[3] = {0.6f, 1.4f, 0.35f};
        const std::uint32_t col[3] = {0xFF8040u, 0x40A0FFu, 0xFFFFFFu};
        for (int i = 0; i < 3; ++i) {
            auto& l = lights[static_cast<std::size_t>(i)];
            for (int k = 0; k < 3; ++k) { l.pos[k] = at[k] + from[i][k]; l.centre[k] = at[k]; }
            float dir[3], len = 0;
            for (int k = 0; k < 3; ++k) { dir[k] = l.centre[k] - l.pos[k]; len += dir[k] * dir[k]; }
            len = std::sqrt(len);
            for (int k = 0; k < 3; ++k) l.dir[k] = dir[k] / len;
            l.radiusA = 900.0f; l.radiusB = 200.0f; l.f32 = f32[i]; l.colour = col[i];
        }
        omk::glesSetDepthTie(gl, true);
        for (int black = 1; black >= 0; --black) {
            for (int step = 0; step < 2; ++step) {
                const auto poseN = omk::composePose(meshes, tracks, frame + 11 * step, false);
                omk::applyPose(posed, rest, meshes, poseN);
                for (auto& k : posed.corners) {
                    const float x = k.x, y = k.y, z = k.z;
                    k.x = place[0] * x + place[1] * y + place[2] * z + place[3];
                    k.y = place[4] * x + place[5] * y + place[6] * z + place[7];
                    k.z = place[8] * x + place[9] * y + place[10] * z + place[11];
                    const float nx = k.nx, ny = k.ny, nz = k.nz;
                    k.nx = place[0] * nx + place[1] * ny + place[2] * nz;
                    k.ny = place[4] * nx + place[5] * ny + place[6] * nz;
                    k.nz = place[8] * nx + place[9] * ny + place[10] * nz;
                    if (black) { k.r = 0.0f; k.g = 0.0f; k.b = 0.0f; }
                }
                const int litCpu = omk::applyLights(posed, 0, posed.corners.size(), at, lights);
                omk::meshAffines(meshes, poseN, place, aff);
                std::vector<float> lv;
                const int litGpu = omk::lightReach(at, lights, lv);
                auto ds = drawsOf(rest, aff.data(), meshes.size());
                for (auto& dr : ds) {
                    dr.vertexLights = lv.data();
                    dr.vertexLightCount = litGpu;
                    dr.lightsFromBlack = black != 0;
                }
                const omk::Surface cpu = run(drawsOf(posed, nullptr, 0));
                const omk::Surface gpu = run(ds);
                // and the same GPU draw with NO lights - which must differ,
                // or the light never reached the picture
                auto dark = ds;
                for (auto& dr : dark) { dr.vertexLights = nullptr; dr.vertexLightCount = 0; }
                const omk::Surface unlit = run(dark);
                const Coverage cv = compare(cpu, gpu);
                const Coverage un = compare(cpu, unlit);
                int worst = 0;
                long off = 0;
                for (std::size_t i = 0; i < cpu.px.size(); ++i) {
                    const auto a = cpu.px[i], b = gpu.px[i];
                    const int d = std::max({std::abs((a >> 11) - (b >> 11)),
                                            std::abs(((a >> 5) & 63) - ((b >> 5) & 63)),
                                            std::abs((a & 31) - (b & 31))});
                    worst = std::max(worst, d);
                    off += d > 1;
                }
                std::printf("light %s frame %d: %d/%d lights reach  coverage %.4f  differing %ld  "
                            "worst %d step(s), %ld pixels past 1  [unlit: %ld differ]\n",
                            black ? "from black" : "on baked  ", frame + 11 * step, litCpu, litGpu,
                            cv.agree(), cv.differ, worst, off, un.differ);
                // under 0.1% of the frame may differ at all (the silhouette's
                // edge: 0-17 measured); dropping the truncation moves 2793
                ok = ok && litCpu == litGpu && litGpu > 0 && cv.agree() >= 0.995 &&
                     cv.differ * 1000 < static_cast<long>(cpu.px.size()) &&
                     off * 1000 < static_cast<long>(cpu.px.size()) &&
                     // from black the light IS the colour, so an unlit draw must
                     // differ; on the baked colour the characters ship WHITE and
                     // the light saturates, which is the engine's own clamp
                     (!black || un.differ > 10 * cv.differ);
            }
        }
    }
    delete gl;
    std::printf("pose: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}

// ---- THE LIGHTING ENHANCEMENTS' MODES (`todo/enhancements.md` 6 and 7) ----
//
//     gles_probe <gamedata> --perpixel [posed]
//     gles_probe <gamedata> --shadow <dirX,dirY,dirZ> <strength> [posed]
//
// `tools/perpixel_probe.cpp` and `tools/shadow_probe.cpp` - the Vulkan probes
// `engine: perpixel lighting` and `engine: mapped shadows` read - on this
// backend, with the same synthetic scenes and the same printed lines, so one
// regex reads both. `posed` draws the lit quad (or the caster) as a body the
// RENDERER poses - every corner on mesh 0, one identity affine - which is the
// other program the enhancements live in.
namespace {

omk::Geometry flatQuad(float cx, float cy, float cz, float half, float col) {
    omk::Geometry g;
    const float p[4][3] = {{cx - half, cy, cz - half}, {cx + half, cy, cz - half},
                           {cx + half, cy, cz + half}, {cx - half, cy, cz + half}};
    const int fan[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (const auto& t : fan)
        for (int k = 0; k < 3; ++k) {
            omk::Corner c{};
            c.x = p[t[k]][0]; c.y = p[t[k]][1]; c.z = p[t[k]][2];
            c.u = 0.5f; c.v = 0.5f;
            c.r = c.g = c.b = col;
            c.nx = 0; c.ny = -1; c.nz = 0;      // facing UP in the game's Y-down world
            c.phase = -1;
            g.corners.push_back(c);
        }
    g.cornerMirror.resize(g.corners.size(), 0);
    g.cornerMesh.resize(g.corners.size(), 0);   // mesh 0: what a `posed` draw needs
    g.cornerVertex.resize(g.corners.size(), -1);
    g.cornerDeclared.resize(g.corners.size(), -1);
    return g;
}

const float kIdentityAffine[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

void posedDraw(omk::Draw& d, bool posed) {
    if (!posed) return;
    d.meshPose = kIdentityAffine;
    d.meshPoses = 1;
}

int perpixelMode(bool posed) {
    omk::Renderer* r = omk::makeGlesRenderer();
    omk::glesSetEnhancedLighting(r, true, false);
    const int W = 256, H = 256;
    if (!r->init(W, H)) { std::printf("gles renderer failed to come up\n"); return 1; }
    r->setTextures({});
    std::printf("drawsPixelLights %d  posesBodies %d\n", r->drawsPixelLights() ? 1 : 0,
                r->posesBodies() ? 1 : 0);
    const float half = 200.0f;
    omk::Light3do L{};
    L.pos[0] = 0; L.pos[1] = -150; L.pos[2] = 0;
    L.dir[0] = 0; L.dir[1] = 1; L.dir[2] = 0;
    L.radiusA = 600.0f; L.radiusB = 40.0f;
    L.colour = 0x00FFFFFF; L.f32 = 1.0f;
    omk::View v;
    v.cam.eye[0] = 0; v.cam.eye[1] = -520; v.cam.eye[2] = 0.1f;
    v.cam.at[0] = 0;  v.cam.at[1] = 0;  v.cam.at[2] = 0;
    v.cam.hfovDeg = 60.0f;
    v.dither = false;
    omk::Surface pix, vert;
    {
        omk::Geometry g = flatQuad(0, 0, 0, half, 0.0f);
        omk::View::GpuLight gl{};
        for (int k = 0; k < 3; ++k) { gl.pos[k] = L.pos[k]; gl.dir[k] = L.dir[k]; }
        gl.radiusA = L.radiusA; gl.radiusB = L.radiusB;
        gl.colour[0] = gl.colour[1] = gl.colour[2] = 1.0f;
        gl.intensity = L.f32;
        v.lights.assign(1, gl);
        std::vector<omk::Draw> d;
        d.push_back({0, &g, 0, g.corners.size(), omk::Blend::Opaque, false, 1, false});
        posedDraw(d.back(), posed);
        omk::drawWithMirror(*r, d, v, omk::MirrorPlane{});
        pix = r->readback();
    }
    {
        omk::Geometry g = flatQuad(0, 0, 0, half, 0.0f);
        const float at[3] = {0, 0, 0};
        omk::applyLights(g, 0, g.corners.size(), at, std::span<const omk::Light3do>(&L, 1));
        v.lights.clear();
        std::vector<omk::Draw> d;
        d.push_back({0, &g, 0, g.corners.size(), omk::Blend::Opaque, false, 0, false});
        omk::drawWithMirror(*r, d, v, omk::MirrorPlane{});
        vert = r->readback();
    }
    const auto green = [](const omk::Surface& s, int x, int y) {
        return (s.px[static_cast<std::size_t>(y) * s.w + x] >> 5) & 63;
    };
    const auto expect = [&](float x, float z) {
        const float dx = x - L.pos[0], dy = 0.0f - L.pos[1], dz = z - L.pos[2];
        const float dd = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dd > L.radiusA) return 0.0f;
        float fall = 1.0f - (dd - L.radiusB) / (L.radiusA - L.radiusB);
        if (fall > 1.0f) fall = 1.0f;
        return std::min(L.f32 * 256.0f * fall / 256.0f, 1.0f);
    };
    const int cx = W / 2, cy = H / 2;
    const int wantCentre = static_cast<int>(expect(0.0f, 0.0f) * 63.0f + 0.5f);
    int x0 = -1, x1 = -1;
    for (int x = 0; x < W; ++x)
        if (green(vert, x, cy) > 0) { if (x0 < 0) x0 = x; x1 = x; }
    x0 += 4; x1 -= 4;
    const auto spread = [&](const omk::Surface& sf) {
        int lo = 64, hi = -1;
        for (int x = x0; x <= x1; ++x) {
            const int g = green(sf, x, cy);
            lo = std::min(lo, g); hi = std::max(hi, g);
        }
        return x0 < x1 ? hi - lo : -1;
    };
    std::printf("%s\n", posed ? "posed by the renderer" : "plain");
    std::printf("perpixel at the centre %d, the law says %d, delta %d\n",
                green(pix, cx, cy), wantCentre, std::abs(green(pix, cx, cy) - wantCentre));
    std::printf("spread across the middle scanline: perpixel %d, pervertex %d (x %d..%d)\n",
                spread(pix), spread(vert), x0, x1);
    delete r;
    return 0;
}

int shadowMode(const char* dirArg, float strength, bool posed) {
    float dir[3] = {0.0f, 1.0f, 0.0f};
    std::sscanf(dirArg, "%f,%f,%f", &dir[0], &dir[1], &dir[2]);
    omk::Renderer* r = omk::makeGlesRenderer();
    omk::glesSetEnhancedLighting(r, false, true);
    const int W = 320, H = 320;
    if (!r->init(W, H)) { std::printf("gles renderer failed to come up\n"); return 1; }
    r->setTextures({});
    std::printf("drawsShadowMap %d  posesBodies %d  %s\n", r->drawsShadowMap() ? 1 : 0,
                r->posesBodies() ? 1 : 0, posed ? "caster posed by the renderer" : "plain caster");
    omk::Geometry ground = flatQuad(0, 0, 0, 400.0f, 1.0f);
    omk::Geometry caster = flatQuad(0, -120.0f, 0, 40.0f, 1.0f);
    omk::View v;
    v.cam.eye[0] = 0; v.cam.eye[1] = -420; v.cam.eye[2] = -420;
    v.cam.at[0] = 0;  v.cam.at[1] = 0;   v.cam.at[2] = 0;
    v.cam.hfovDeg = 60.0f;
    v.shadow.on = strength > 0.0f;
    for (int k = 0; k < 3; ++k) { v.shadow.dir[k] = dir[k]; v.shadow.centre[k] = 0.0f; }
    v.shadow.centre[1] = -60.0f;
    v.shadow.radius = 260.0f;
    v.shadow.strength = strength;
    std::vector<omk::Draw> draws;
    draws.push_back({0, &ground, 0, ground.corners.size(), omk::Blend::Opaque, false, 0, false});
    draws.push_back({0, &caster, 0, caster.corners.size(), omk::Blend::Opaque, false, 0, true});
    posedDraw(draws.back(), posed);
    omk::drawWithMirror(*r, draws, v, omk::MirrorPlane{});
    const omk::Surface& sf = r->readback();
    long dark = 0, lit = 0;
    double sx = 0, sy = 0;
    for (int y = 0; y < sf.h; ++y)
        for (int x = 0; x < sf.w; ++x) {
            const std::uint16_t px = sf.px[static_cast<std::size_t>(y) * sf.w + x];
            const int rr = (px >> 11) & 31, gg = (px >> 5) & 63, bb = px & 31;
            if (rr < 2 && gg < 4 && bb < 2) continue;
            if (rr >= 28 && gg >= 56 && bb >= 28) { ++lit; continue; }
            ++dark; sx += x; sy += y;
        }
    std::printf("dir %.2f,%.2f,%.2f  strength %.2f  lit %ld  shadowed %ld  centroid %.1f %.1f\n",
                static_cast<double>(dir[0]), static_cast<double>(dir[1]),
                static_cast<double>(dir[2]), static_cast<double>(strength), lit, dark,
                dark ? sx / static_cast<double>(dark) : -1.0,
                dark ? sy / static_cast<double>(dark) : -1.0);
    delete r;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const bool poseArg = argc > 2 && std::string(argv[2]) == "--pose";
    const bool ppArg = argc > 2 && std::string(argv[2]) == "--perpixel";
    const bool shArg = argc > 4 && std::string(argv[2]) == "--shadow";
    if (argc < 6 && !poseArg && !ppArg && !shArg) {
        std::fprintf(stderr, "usage: gles_probe <gamedata> <model.3DO> <eye> <at> <hfov> [WxH]\n");
        return 2;
    }
    // ---- the context: CGL, legacy profile, no drawable
    CGLPixelFormatAttribute attrs[] = {
        kCGLPFAAccelerated,
        kCGLPFAOpenGLProfile, static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_Legacy),
        static_cast<CGLPixelFormatAttribute>(0)};
    CGLPixelFormatObj pf = nullptr;
    GLint npf = 0;
    CGLContextObj ctx = nullptr;
    if (CGLChoosePixelFormat(attrs, &pf, &npf) != kCGLNoError || !pf ||
        CGLCreateContext(pf, nullptr, &ctx) != kCGLNoError) {
        std::fprintf(stderr, "no CGL context\n");
        return 1;
    }
    CGLDestroyPixelFormat(pf);
    CGLSetCurrentContext(ctx);
    std::printf("gl: %s | %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    if (ppArg || shArg) {
        const int rc = ppArg ? perpixelMode(argc > 3 && std::string(argv[3]) == "posed")
                             : shadowMode(argv[3], static_cast<float>(std::atof(argv[4])),
                                          argc > 5 && std::string(argv[5]) == "posed");
        CGLSetCurrentContext(nullptr);
        CGLDestroyContext(ctx);
        return rc;
    }
    if (poseArg) {
        const int rc = poseMode(argc, argv);
        CGLSetCurrentContext(nullptr);
        CGLDestroyContext(ctx);
        return rc;
    }
    // ---- the set, as run_vulkan loads it
    const std::string model = argv[2];
    const auto d = omk::DataFs::readPath(model);
    if (d.empty()) { std::fprintf(stderr, "cannot read %s\n", model.c_str()); return 1; }
    const auto t = omk::DataFs::readPath(model.substr(0, model.size() - 4) + ".3DT");
    const auto geo = omk::buildGeometry(d, omk::DrawFilter::Engine);
    const auto tex = t.empty() ? std::vector<omk::Texture>{} : omk::textures(d, t);
    std::vector<omk::Draw> draws;
    for (const auto& b : geo.batches) {
        omk::Draw dr;
        dr.bucketKey = static_cast<std::uint32_t>(b.material) & 0x3Fu;
        dr.geo = &geo; dr.start = b.start; dr.count = b.count;
        dr.blend = b.blend; dr.cutout = b.cutout;
        draws.push_back(dr);
    }
    omk::RCamera cam;
    triple(argv[3], cam.eye); triple(argv[4], cam.at);
    cam.hfovDeg = static_cast<float>(std::atof(argv[5]));
    int W = 640, H = 480;
    if (argc > 6) std::sscanf(argv[6], "%dx%d", &W, &H);

    auto run = [&](omk::Renderer& r, const omk::View& v, int w, int h) -> const omk::Surface* {
        if (!r.init(w, h)) return nullptr;
        r.setTextures(tex);
        r.begin(v);
        for (const auto& dr : draws) r.submit(dr);
        r.end();
        return &r.readback();
    };

    omk::Renderer* gl = omk::makeGlesRenderer();
    int failures = 0;
    std::printf("%s  %zu triangles  %zu textures\n", model.c_str(),
                geo.corners.size() / 3, tex.size());

    for (int dither = 0; dither <= 1; ++dither) {
        omk::View v;
        v.cam = cam; v.cam.w = W; v.cam.h = H;
        v.dither = dither != 0;
        omk::SoftwareRenderer sw;
        const omk::Surface swF = *run(sw, v, W, H);
        const omk::Surface* g = run(*gl, v, W, H);
        if (!g) { std::fprintf(stderr, "gles renderer failed to come up\n"); return 1; }
        const Coverage c = compare(swF, *g);
        std::printf("%-9s %dx%d  software lit %ld  gles lit %ld  coverage %.4f  "
                    "differing %ld (%.1f%%, not a criterion)\n",
                    dither ? "dithered" : "plain", W, H, c.sw, c.gl, c.agree(), c.differ,
                    100.0 * double(c.differ) / double(swF.px.size()));
        if (c.agree() < 0.99) ++failures;

        // present = readback, 1:1
        Window win;
        if (!win.make(W, H)) { std::fprintf(stderr, "no window FBO\n"); return 1; }
        omk::glesSetWindowTarget(gl, win.fbo);
        const omk::Surface rb = *g;   // a copy: presenting must not need it
        omk::glesPresentWorld(gl, 0, H, W, H, W, H);
        const long bad = presentMismatches(win, rb, 0);
        std::printf("          present world: %s\n",
                    bad ? (std::to_string(bad) + " DIFFERENT").c_str() : "EXACT");
        failures += bad != 0;
        // and the CPU-composed path: the readback handed back as a surface
        omk::glesPresentSurface(gl, rb, W, H);
        const long badS = presentMismatches(win, rb, 0);
        std::printf("          present surface: %s\n",
                    badS ? (std::to_string(badS) + " DIFFERENT").c_str() : "EXACT");
        failures += badS != 0;
        omk::glesSetWindowTarget(gl, 0);
    }

    // ---- TEXTURE FILTERING: a second renderer asked for BILINEAR - what the
    // original's hardware arm set, MAG/MIN LINEAR with MIP NONE
    // (`docs/ASSETS.md` 4) - against the default renderer's nearest frame,
    // same plain view. A filter touches nearly every textured pixel and moves
    // no geometry, so the frame changes over much of the picture by a SMALL
    // mean, and its coverage agreement with the reference is kept. Printed
    // for `verify.py: engine: texture filter`; a refusal or an unchanged
    // frame is a failure here too.
    {
        omk::View v;
        v.cam = cam; v.cam.w = W; v.cam.h = H;
        v.dither = false;
        omk::SoftwareRenderer sw;
        const omk::Surface swF = *run(sw, v, W, H);
        const omk::Surface n0 = *run(*gl, v, W, H);
        omk::Renderer* bl = omk::makeGlesRenderer();
        const bool took = bl->setTextureFilter(1);
        const omk::Surface* b = run(*bl, v, W, H);
        if (!b) { std::fprintf(stderr, "gles renderer failed to come up\n"); return 1; }
        long changed = 0;
        double tot = 0.0;
        for (std::size_t i = 0; i < n0.px.size() && i < b->px.size(); ++i) {
            const std::uint16_t x = n0.px[i], y = b->px[i];
            if (x == y) continue;
            ++changed;
            tot += (std::abs(((x >> 11) & 31) * 8 - ((y >> 11) & 31) * 8) +
                    std::abs(((x >> 5) & 63) * 4 - ((y >> 5) & 63) * 4) +
                    std::abs((x & 31) * 8 - (y & 31) * 8)) / 3.0;
        }
        const Coverage c = compare(swF, *b);
        std::printf("filter bilinear: %s  changed %ld (%.1f%%)  mean |d| %.1f of 255  "
                    "coverage %.4f\n", took ? "taken" : "REFUSED", changed,
                    100.0 * double(changed) / double(n0.px.size()),
                    changed ? tot / double(changed) : 0.0, c.agree());
        failures += !took || changed == 0 || c.agree() < 0.98;
        delete bl;

        // ---- TRILINEAR and ANISOTROPY (`todo/enhancements.md` 2), the
        // property `engine: mipmaps` asserts on Vulkan: a mip chain removes the
        // frequencies a minified surface cannot hold, so the frame's mean
        // NEIGHBOUR GRADIENT over lit pixels orders trilinear < anisotropic <
        // bilinear (anisotropy samples along the footprint instead of blurring
        // across it). The order is judged, not the values - they are the
        // driver's. Nearest must still equal the default renderer's frame.
        const auto grad = [](const omk::Surface& s) {
            double t = 0.0; long n = 0;
            const auto ch = [](std::uint16_t v, int k) {
                return k == 0 ? ((v >> 11) & 31) * 8 : k == 1 ? ((v >> 5) & 63) * 4 : (v & 31) * 8;
            };
            for (int y = 0; y < s.h; ++y)
                for (int x = 0; x < s.w; ++x) {
                    const std::uint16_t p = s.at(x, y);
                    if (!p) continue;
                    for (int dir = 0; dir < 2; ++dir) {
                        const int nx = x + (dir == 0), ny = y + (dir == 1);
                        if (nx >= s.w || ny >= s.h) continue;
                        const std::uint16_t q = s.at(nx, ny);
                        if (!q) continue;
                        for (int k = 0; k < 3; ++k) t += std::abs(ch(p, k) - ch(q, k));
                        n += 3;
                    }
                }
            return n ? t / double(n) : 0.0;
        };
        omk::Renderer* bi = omk::makeGlesRenderer();
        bi->setTextureFilter(1);
        const omk::Surface fb1 = *run(*bi, v, W, H);
        omk::Renderer* tri = omk::makeGlesRenderer();
        const bool tTook = tri->setTextureFilter(2);
        const omk::Surface ft = *run(*tri, v, W, H);
        omk::Renderer* an = omk::makeGlesRenderer();
        an->setTextureFilter(2);
        an->setAnisotropy(16);
        const omk::Surface fa = *run(*an, v, W, H);
        const int anGot = omk::glesAnisotropy(an);
        const double gb = grad(fb1), gt = grad(ft), ga = grad(fa);
        const Coverage ct = compare(swF, ft);
        std::printf("filter trilinear: %s  gradient bilinear %.3f  trilinear %.3f  aniso %.3f (%dx)  "
                    "coverage %.4f\n", tTook ? "taken" : "REFUSED", gb, gt, ga, anGot, ct.agree());
        failures += !tTook || !(gt < gb) || (anGot > 1 && !(gt < ga && ga < gb)) || ct.agree() < 0.98;
        delete bi; delete tri; delete an;
    }

    // ---- THE ENHANCED PROGRAMS AT REST (`todo/enhancements.md` 6 and 7):
    // linked in place of the default two when either enhancement is asked
    // for, they must draw the DEFAULT picture byte for byte where nothing is
    // lit and nothing casts - which is what keeps the two copies of the
    // scene and posing programs in step. Dithered and fogged, so the fog and
    // the shimmer paths are both compared.
    {
        omk::View v;
        v.cam = cam; v.cam.w = W; v.cam.h = H;
        v.dither = true;
        v.fog = true; v.fogStart = 300.0f; v.fogEnd = 1500.0f;
        const omk::Surface base = *run(*gl, v, W, H);
        omk::Renderer* en = omk::makeGlesRenderer();
        omk::glesSetEnhancedLighting(en, true, true);
        const omk::Surface* e = run(*en, v, W, H);
        long differ = -1;
        if (e) {
            differ = 0;
            for (std::size_t i = 0; i < base.px.size(); ++i) differ += base.px[i] != e->px[i];
        }
        std::printf("enhanced programs at rest: lights %d, shadow map %d, %ld pixels differ "
                    "from the default programs\n", en->drawsPixelLights() ? 1 : 0,
                    en->drawsShadowMap() ? 1 : 0, differ);
        failures += !en->drawsPixelLights() || !en->drawsShadowMap() || differ != 0;
        delete en;
    }

    // ---- SUPERSAMPLING (`todo/enhancements.md` 9), `engine: supersampling`'s
    // properties on this backend: 1x is BYTE-IDENTICAL to a renderer never
    // asked; 4x moves the picture by a small mean amount while its neighbour
    // energy FALLS (a resolve, not a shift); coverage is kept; and the GPU
    // present pass's resolve equals `readback`'s, dithered and plain - the
    // exact check every present here makes.
    {
        omk::View v;
        v.cam = cam; v.cam.w = W; v.cam.h = H;
        v.dither = false;
        omk::SoftwareRenderer sw;
        const omk::Surface swF = *run(sw, v, W, H);
        const omk::Surface base = *run(*gl, v, W, H);
        omk::Renderer* one = omk::makeGlesRenderer();
        one->setSupersample(1);
        const omk::Surface f1 = *run(*one, v, W, H);
        omk::Renderer* four = omk::makeGlesRenderer();
        const bool took = four->setSupersample(4);
        const omk::Surface* f4p = run(*four, v, W, H);
        if (!f4p) { std::fprintf(stderr, "gles renderer failed to come up at 4x\n"); return 1; }
        const omk::Surface f4 = *f4p;
        const auto rgbOf = [](std::uint16_t x, int k) {
            return k == 0 ? ((x >> 11) & 31) * 8 : k == 1 ? ((x >> 5) & 63) * 4 : (x & 31) * 8;
        };
        long same1 = 0, moved = 0;
        double dsum = 0.0;
        for (std::size_t i = 0; i < base.px.size(); ++i) {
            same1 += base.px[i] != f1.px[i];
            if (f4.px[i] == f1.px[i]) continue;
            ++moved;
            for (int k = 0; k < 3; ++k) dsum += std::abs(rgbOf(f4.px[i], k) - rgbOf(f1.px[i], k));
        }
        const auto energy = [&](const omk::Surface& s) {
            double t = 0.0; long n = 0;
            for (int y = 0; y + 1 < s.h; ++y)
                for (int x = 0; x + 1 < s.w; ++x) {
                    const std::uint16_t p = s.at(x, y), r = s.at(x + 1, y), d = s.at(x, y + 1);
                    for (int k = 0; k < 3; ++k)
                        t += std::abs(rgbOf(p, k) - rgbOf(r, k)) + std::abs(rgbOf(p, k) - rgbOf(d, k));
                    ++n;
                }
            return n ? t / double(n) : 0.0;
        };
        const double e1 = energy(f1), e4 = energy(f4);
        const Coverage c = compare(swF, f4);
        long badP = 0;
        for (int dither = 0; dither <= 1; ++dither) {
            omk::View vd = v;
            vd.dither = dither != 0;
            const omk::Surface rb = *run(*four, vd, W, H);
            Window win;
            win.make(W, H);
            omk::glesSetWindowTarget(four, win.fbo);
            omk::glesPresentWorld(four, 0, H, W, H, W, H);
            badP += presentMismatches(win, rb, 0);
            omk::glesSetWindowTarget(four, 0);
        }
        const int got = omk::glesSupersample(four);
        std::printf("supersample %dx: %s  1x differs %ld  4x moved %ld (mean %.1f of 765)  "
                    "energy %.2f -> %.2f (%.0f%%)  coverage %.4f  present: %s\n", got,
                    took ? "taken" : "REFUSED", same1, moved, moved ? dsum / double(moved) : 0.0,
                    e1, e4, e1 > 0 ? 100.0 * e4 / e1 : 0.0, c.agree(),
                    badP ? (std::to_string(badP) + " DIFFERENT").c_str() : "EXACT");
        failures += !took || got != 4 || same1 != 0 || moved == 0 || e4 >= 0.9 * e1 ||
                    c.agree() < 0.98 || badP != 0;
        delete one; delete four;
    }

    // ---- MSAA (`todo/enhancements.md` 0), `engine: anti-aliasing`'s
    // properties on this backend: the 4x frame differs from 1x in a small
    // fraction of the picture, every changed pixel on an EDGE of the 1x frame
    // (MSAA resolves geometry edges and leaves texture interiors alone), and
    // coverage is kept. `msaa: N of M`, M the context's limit, so a context
    // that cannot is told from a backend that did not.
    {
        omk::View v;
        v.cam = cam; v.cam.w = W; v.cam.h = H;
        v.dither = false;
        omk::SoftwareRenderer sw;
        const omk::Surface swF = *run(sw, v, W, H);
        const omk::Surface a = *run(*gl, v, W, H);
        omk::Renderer* ms = omk::makeGlesRenderer();
        ms->setMultisample(4);
        const omk::Surface b = *run(*ms, v, W, H);
        int got = 0, most = 0;
        omk::glesSamples(ms, &got, &most);
        long changed = 0, edge = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const std::uint16_t c = a.at(x, y);
                if (c == b.at(x, y)) continue;
                ++changed;
                edge += (x > 0 && a.at(x - 1, y) != c) || (x < W - 1 && a.at(x + 1, y) != c) ||
                        (y > 0 && a.at(x, y - 1) != c) || (y < H - 1 && a.at(x, y + 1) != c);
            }
        const Coverage c = compare(swF, b);
        std::printf("msaa: %d of %d  changed %ld (%.2f%%)  on an edge %.4f  coverage %.4f\n", got, most,
                    changed, 100.0 * double(changed) / double(W * H),
                    changed ? double(edge) / double(changed) : 0.0, c.agree());
        if (most >= 4)
            failures += got != 4 || changed == 0 || double(edge) / double(changed) < 0.99 ||
                        c.agree() < 0.99;
        delete ms;
    }

    // ---- THE LETTERBOX: a 640x352 picture at row 64 of a 640x480 frame, the
    // dialogue camera mode's shape. The target is the full frame and the
    // picture its top rows, as `play.cpp` sets the view up.
    // ...and again SUPERSAMPLED, where the viewport and the present's rows
    // are both scaled by the factor
    if (W == 640 && H == 480) for (int ssk = 1; ssk <= 4; ssk *= 4) {
        const int vh = 352, vy = (480 - vh) / 2;
        omk::View v;
        v.cam = cam; v.vx = 0; v.vy = vy; v.vw = W; v.vh = vh;
        omk::Renderer* lr = gl;
        if (ssk > 1) { lr = omk::makeGlesRenderer(); lr->setSupersample(ssk); }
        const omk::Surface* g = run(*lr, v, W, H);
        Window win;
        win.make(W, H);
        omk::glesSetWindowTarget(lr, win.fbo);
        // the readback is the WHOLE target; the picture is its top `vh` rows
        omk::Surface pic(W, vh, 0);
        for (int y = 0; y < vh; ++y)
            for (int x = 0; x < W; ++x) pic.set(x, y, g->at(x, y));
        omk::glesPresentWorld(lr, vy, vh, W, H, W, H);
        const long bad = presentMismatches(win, pic, vy);
        long lit = 0;
        for (auto p : pic.px) lit += p != 0;
        std::printf("letterbox 640x%d at row %d%s  picture lit %ld  present: %s\n", vh, vy,
                    ssk > 1 ? " supersampled 4x" : "", lit,
                    bad ? (std::to_string(bad) + " DIFFERENT").c_str() : "EXACT");
        failures += bad != 0 || lit == 0;
        omk::glesSetWindowTarget(lr, 0);
        if (lr != gl) delete lr;
    }

    // 5. THE DRAW-STATE CACHE (todo/optimization.md step 17): the same draws
    // with the cache OFF, then ON, then ON again as a second frame (state
    // left over from a frame must not leak into the next), with the fog on so
    // its uniforms vary per draw - in one context, so the pictures must be
    // IDENTICAL, not merely close. And the state calls each made.
    {
        omk::View v;
        v.cam = cam; v.cam.w = W; v.cam.h = H;
        v.dither = true;
        v.fog = true; v.fogStart = 300.0f; v.fogEnd = 1500.0f;
        v.fogColour[0] = 40; v.fogColour[1] = 60; v.fogColour[2] = 80;
        long calls[3];
        omk::glesTakeStateCalls(calls);
        omk::glesSetStateCache(gl, false);
        const omk::Surface off = *run(*gl, v, W, H);
        omk::glesTakeStateCalls(calls);
        const long offSet = calls[1];
        omk::glesSetStateCache(gl, true);
        const omk::Surface on1 = *run(*gl, v, W, H);
        omk::glesTakeStateCalls(calls);
        const long nDraws = calls[0], onSet = calls[1], onSkip = calls[2];
        gl->begin(v);
        for (const auto& dr : draws) gl->submit(dr);
        gl->end();
        const omk::Surface on2 = gl->readback();
        const auto differ = [](const omk::Surface& a, const omk::Surface& b) {
            long n = 0;
            for (std::size_t i = 0; i < a.px.size() && i < b.px.size(); ++i) n += a.px[i] != b.px[i];
            return a.px.size() == b.px.size() ? n : -1;
        };
        long lit = 0;
        for (auto p : off.px) lit += p != 0;
        const long d1 = differ(off, on1), d2 = differ(off, on2);
        std::printf("state cache: %ld draws, state calls %ld off / %ld on (%ld skipped); "
                    "lit %ld; on differs %ld, second frame differs %ld\n",
                    nDraws, offSet, onSet, onSkip, lit, d1, d2);
        failures += d1 != 0 || d2 != 0 || lit == 0;
    }

    // 6. THE BUFFERS OF A GEOMETRY THAT IS GONE (todo/optimization.md step
    // 26). A copy of the set, drawn and then destroyed, must release exactly
    // one geometry and leave one buffer fewer; and a NEW geometry - shifted,
    // with the SAME revision, very likely at the dead one's address - must
    // draw what a fresh geometry draws, not the dead one's buffer. Without the
    // release, a matching address and revision is exactly what skips its
    // upload and shows the old picture.
    {
        omk::View v;
        v.cam = cam; v.cam.w = W; v.cam.h = H;
        v.dither = true;
        const auto drawOf = [&](const omk::Geometry& gg) {
            std::vector<omk::Draw> ds = draws;
            for (auto& x : ds) x.geo = &gg;
            gl->begin(v);
            for (const auto& x : ds) gl->submit(x);
            gl->end();
            return gl->readback();
        };
        const auto shifted = [&] {
            auto* g = new omk::Geometry(geo);
            for (auto& c : g->corners) c.x += 40.0f;
            return g;
        };
        long s0[3], s1[3], s2[3];
        omk::glesGeometryStats(gl, s0);
        auto* a = new omk::Geometry(geo);
        const void* aAddr = a;
        (void)drawOf(*a);
        omk::glesGeometryStats(gl, s1);
        delete a;
        omk::glesGeometryStats(gl, s2);
        omk::Geometry* b = shifted();              // likely where `a` was
        const bool sameAddr = static_cast<const void*>(b) == aAddr;
        const omk::Surface picB = drawOf(*b);
        omk::Geometry* c = shifted();              // alive beside `b`: a new address
        const omk::Surface picC = drawOf(*c);
        long differ = 0;
        for (std::size_t i = 0; i < picB.px.size() && i < picC.px.size(); ++i) differ += picB.px[i] != picC.px[i];
        delete b;
        delete c;
        const long rel = s2[0] - s1[0], held = s1[1] - s2[1];
        std::printf("geometry release: drawn and destroyed, %ld released and %ld buffer fewer "
                    "(held %ld -> %ld -> %ld); a new geometry at the %s address differs from a "
                    "fresh one in %ld pixels\n", rel, held, s0[1], s1[1], s2[1],
                    sameAddr ? "SAME" : "another", differ);
        failures += rel != 1 || held != 1 || differ != 0;
    }

    delete gl;
    CGLSetCurrentContext(nullptr);
    CGLDestroyContext(ctx);
    std::printf("failures %d\n", failures);
    return failures ? 1 : 0;
}

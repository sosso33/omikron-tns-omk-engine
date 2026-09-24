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
#include <string>
#include <vector>

namespace omk {
Renderer* makeGlesRenderer();
bool glesPresentWorld(Renderer*, int vy, int vh, int frameW, int frameH, int winW, int winH);
bool glesPresentSurface(Renderer*, const Surface&, int winW, int winH);
void glesSetWindowTarget(Renderer*, unsigned fbo);
void glesSetDepthTie(Renderer*, bool);
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

int main(int argc, char** argv) {
    const bool poseArg = argc > 2 && std::string(argv[2]) == "--pose";
    if (argc < 6 && !poseArg) {
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

    // ---- THE LETTERBOX: a 640x352 picture at row 64 of a 640x480 frame, the
    // dialogue camera mode's shape. The target is the full frame and the
    // picture its top rows, as `play.cpp` sets the view up.
    if (W == 640 && H == 480) {
        const int vh = 352, vy = (480 - vh) / 2;
        omk::View v;
        v.cam = cam; v.vx = 0; v.vy = vy; v.vw = W; v.vh = vh;
        const omk::Surface* g = run(*gl, v, W, H);
        Window win;
        win.make(W, H);
        omk::glesSetWindowTarget(gl, win.fbo);
        // the readback is the WHOLE target; the picture is its top `vh` rows
        omk::Surface pic(W, vh, 0);
        for (int y = 0; y < vh; ++y)
            for (int x = 0; x < W; ++x) pic.set(x, y, g->at(x, y));
        omk::glesPresentWorld(gl, vy, vh, W, H, W, H);
        const long bad = presentMismatches(win, pic, vy);
        long lit = 0;
        for (auto p : pic.px) lit += p != 0;
        std::printf("letterbox 640x%d at row %d  picture lit %ld  present: %s\n", vh, vy, lit,
                    bad ? (std::to_string(bad) + " DIFFERENT").c_str() : "EXACT");
        failures += bad != 0 || lit == 0;
        omk::glesSetWindowTarget(gl, 0);
    }

    delete gl;
    CGLSetCurrentContext(nullptr);
    CGLDestroyContext(ctx);
    std::printf("failures %d\n", failures);
    return failures ? 1 : 0;
}

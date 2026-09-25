// SPDX-License-Identifier: GPL-3.0-or-later
// THE DEVICE FACTOR - `todo/handoff-vita.md` §1's missing number, measured
// rather than framed.
//
//     vita_bench <gamedata> [bodies] [frames] [model:asset ...]
//
// The Vita decision rests on 6.0 ms of main-thread work a frame on an M1, and
// on a ratio between an M1 core and a Vita core that nobody has: Geekbench
// does not run on a Cortex-A9, and DMIPS is not quoted for the M1. So this
// file runs the SAME source on both machines and prints the same lines, and
// the ratio is the quotient of two numbers from one program - the only kind
// of ratio that means anything here.
//
// What it runs is the per-body work the handoff's profile charged most of the
// frame to, on real data, the way the viewer calls it:
//
//   compose   `composePose`   - the bone walk (0.24 ms a frame on the M1)
//   apply     `applyPose`     - moving every corner (0.45)
//   light     `applyLights`   - the set's `.3DO` lights onto the posed
//                               corners, the crowd's per-vertex light (0.65)
//
// over `bodies` characters (45 by default: the Anekbah street count the
// profile was taken on), advancing each body's frame every tick and phasing
// them apart so no two share a pose. Then the same frames again through
// `omk::Threads` (`platform/threads.h`), because the Vita has three cores for
// a game and whether posing scales across them is the second number the
// decision needs. The threaded pass must produce the SAME BYTES as the inline
// one - it prints both hashes and `threads: EXACT` or `threads: DIFFERENT`,
// and a DIFFERENT is a bug in the pool or in a shared static inside the
// posing code, not a rounding nuance: each body writes only its own geometry.
//
// A per-stage number is reported, not only a total, because the fixes in the
// handoff's §2 are per stage - GPU skinning removes `apply`, a shared pose
// removes `compose`, a light shader removes `light` - and the decision between
// them is which stage the device actually spends its time in.
//
// **It is an instrument, not a slice of the port**: it asserts nothing about
// the game and nothing in `verify.py` runs it. It lives in `backends/vita/`
// rather than `tools/` so the Makefile's `tools/*.cpp` glob does not pick it
// up - a file that only one session needs must not become everyone's build.
//
// Host build (from engine/, after `make` has built the objects):
//
//     c++ -std=c++20 -O2 -Isrc -Ithird_party -o build/vita_bench \
//         backends/vita/bench_main.cpp build/obj/src/*/*.o
//
// Vita build: `backends/vita/CMakeLists.txt`, target `omk_bench`. On the
// device there is no command line, so the defaults below apply and the report
// is ALSO written to `ux0:data/omk/bench.txt`, since stdout goes nowhere
// without a debug-net listener.
#include "actor/pose.h"
#include "formats/light3do.h"
#include "formats/mesh3do.h"
#include "o3de/geom3do.h"
#include "o3de/pointplace.h"
#include "o3de/vertexlight.h"
#include "platform/datafs.h"
#include "platform/threads.h"

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__vita__)
#  include <psp2/kernel/processmgr.h>
#  include <psp2/power.h>
// The newlib heap defaults to far less than the 137 MB live heap the handoff
// measured for the full game; the bench needs a fraction of that, but a model
// set and 45 posed copies is not small. Raised here, as every homebrew does.
extern "C" { int _newlib_heap_size_user = 192 * 1024 * 1024; }
#endif

namespace {

#if defined(__vita__)
constexpr const char* kDefaultRoot   = "ux0:data/omk/gamedata";
constexpr const char* kReportPath    = "ux0:data/omk/bench.txt";
#else
constexpr const char* kDefaultRoot   = "../gamedata";
constexpr const char* kReportPath    = nullptr;
#endif
constexpr int         kDefaultBodies = 45;
constexpr int         kDefaultFrames = 300;   // ten seconds of game at 30 Hz
// The decor whose lights the street's walkers receive - the profile's own set.
constexpr const char* kLightSet      = "MESHES/DECORS/Anekbah.3DO";

std::FILE* g_report = nullptr;

// Everything goes to stdout AND the report file, so a device run and a host
// run leave the same text to diff.
void say(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    std::vfprintf(stdout, fmt, a);
    va_end(a);
    if (g_report) {
        va_start(a, fmt);
        std::vfprintf(g_report, fmt, a);
        va_end(a);
        std::fflush(g_report);
    }
    std::fflush(stdout);
}

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// One kind of character: its model, its rest geometry, and the line it plays.
struct Model {
    std::string name, asset;
    std::vector<omk::Mesh> meshes;
    omk::Geometry rest;
    omk::NodeTracks tracks;
    omk::FaceMesh face;
    std::vector<std::byte> morph;   // kept to re-read the face per frame
    int frames = 1;                 // how many frames the line has
};

// One body on the street: which model, where, and its phase in the line.
struct Body {
    const Model* model = nullptr;
    omk::Geometry posed;
    std::vector<omk::MeshPose> pose;
    std::vector<float> faceVerts;
    float at[3] = {0, 0, 0};
    int phase = 0;
};

// FNV-1a over the posed corners, so two runs - inline and threaded, or host
// and device - can be compared without shipping the geometry anywhere.
std::uint64_t hashBodies(const std::vector<Body>& bodies) {
    std::uint64_t h = 1469598103934665603ull;
    for (const auto& b : bodies) {
        const auto* p = reinterpret_cast<const unsigned char*>(b.posed.corners.data());
        const std::size_t n = b.posed.corners.size() * sizeof(omk::Corner);
        for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    }
    return h;
}

// Stage timings for one pass, summed over its frames.
struct Stages {
    double compose = 0, apply = 0, light = 0, total = 0;
    long lightsReached = 0;
};

// Pose and light one body at one tick - the three stages, timed separately
// when `st` is given. `lights` may be empty (no set found), in which case the
// light stage is measured as zero rather than faked.
void tickBody(Body& b, int tick, std::span<const omk::Light3do> lights, Stages* st) {
    const Model& m = *b.model;
    const int frame = 1 + (b.phase + tick) % (m.frames > 1 ? m.frames : 1);

    auto t0 = Clock::now();
    b.pose = omk::composePose(m.meshes, m.tracks, frame);
    if (!m.morph.empty()) b.faceVerts = omk::faceFrame(m.morph, frame);
    if (st) st->compose += msSince(t0);

    t0 = Clock::now();
    omk::applyPose(b.posed, m.rest, m.meshes, b.pose, &m.face, &b.faceVerts);
    // placed on the street: the viewer moves the posed corners into the
    // world before lighting them, and the lights' reach test depends on it
    for (auto& c : b.posed.corners) { c.x += b.at[0]; c.y += b.at[1]; c.z += b.at[2]; }
    if (st) st->apply += msSince(t0);

    t0 = Clock::now();
    if (!lights.empty()) {
        const int n = omk::applyLights(b.posed, 0, b.posed.corners.size(), b.at, lights);
        if (st) st->lightsReached += n;
    }
    if (st) st->light += msSince(t0);
}

bool loadModel(const omk::DataFs& fs, const std::string& spec, Model& m) {
    const auto colon = spec.find(':');
    m.name  = spec.substr(0, colon);
    m.asset = colon == std::string::npos ? std::string() : spec.substr(colon + 1);
    const auto path = fs.resolve("MESHES/PERSOS/" + m.name + ".3DO");
    if (!path) { say("no model %s\n", m.name.c_str()); return false; }
    const auto d = omk::DataFs::readPath(*path);
    const auto h = omk::readHeader(d);
    if (!h) { say("model %s: bad header\n", m.name.c_str()); return false; }
    m.meshes = omk::readMeshes(d, *h);
    m.rest   = omk::buildGeometry(d, omk::DrawFilter::Engine);
    m.face   = omk::faceMeshOf(m.meshes);
    if (!m.asset.empty()) {
        if (const auto a = fs.resolve("MORPH/" + m.asset + ".3DM")) {
            m.morph  = omk::DataFs::readPath(*a);
            m.tracks = omk::nodeTracks(m.morph, omk::rootTrackOf(m.meshes));
        } else {
            say("model %s: no line %s, posing the bind pose\n",
                m.name.c_str(), m.asset.c_str());
        }
    }
    // A model with no line is posed at frame 1 forever, which still costs the
    // full walk.
    if (m.tracks.valid()) m.frames = m.tracks.frames;
    say("model %-10s line %-8s meshes %3zu corners %6zu frames %d\n",
        m.name.c_str(), m.asset.empty() ? "-" : m.asset.c_str(),
        m.meshes.size(), m.rest.corners.size(), m.frames);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
#if defined(__vita__)
    // The handoff's figures assume 444 MHz, which the Vita runs only when a
    // title asks. Asked here so the number is the best case the port can have,
    // and REPORTED below so a run at 333 cannot pass for one at 444.
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    g_report = std::fopen(kReportPath, "w");
#else
    (void)kReportPath;
#endif
    const std::string root = argc > 1 ? argv[1] : kDefaultRoot;
    const int nBodies = argc > 2 ? std::atoi(argv[2]) : kDefaultBodies;
    const int nFrames = argc > 3 ? std::atoi(argv[3]) : kDefaultFrames;
    std::vector<std::string> specs;
    for (int i = 4; i < argc; ++i) specs.emplace_back(argv[i]);
    // The defaults: the player's model on the line a new game opens with,
    // and the street's most common walker, which carries three coincident
    // faces at rest (`tie_equiv`) and is the heavier of the two.
    if (specs.empty()) specs = {"HO1_FNM:125338", "PSH_FN:125338"};

#if defined(__vita__)
    say("platform vita  arm %d MHz  bus %d  gpu %d\n",
        scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
        scePowerGetGpuClockFrequency());
#elif defined(__aarch64__)
    say("platform host-arm64\n");
#elif defined(__x86_64__)
    say("platform host-x86_64\n");
#else
    say("platform host\n");
#endif
    say("root %s  bodies %d  frames %d\n", root.c_str(), nBodies, nFrames);

    const omk::DataFs fs(root);
    std::vector<Model> models(specs.size());
    for (std::size_t i = 0; i < specs.size(); ++i)
        if (!loadModel(fs, specs[i], models[i])) return 1;

    std::vector<omk::Light3do> lights;
    if (const auto p = fs.resolve(kLightSet)) {
        const auto d = omk::DataFs::readPath(*p);
        if (const auto h = omk::readHeader(d)) lights = omk::readLights(d, *h);
    }
    say("lights %zu from %s\n", lights.size(), kLightSet);

    // Street placement: the bodies spread over the lights' own extent, so the
    // reach test sees what a real street's walkers see - some lit by several
    // lamps, some by none - instead of every body at one lamp or at the origin.
    float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
    for (std::size_t i = 0; i < lights.size(); ++i)
        for (int k = 0; k < 3; ++k) {
            const float v = lights[i].pos[k];
            if (i == 0 || v < lo[k]) lo[k] = v;
            if (i == 0 || v > hi[k]) hi[k] = v;
        }
    std::vector<Body> bodies(static_cast<std::size_t>(nBodies));
    std::uint32_t rng = 12345u;   // fixed, so host and device place alike
    auto next = [&] { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; };
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        auto& b = bodies[i];
        b.model = &models[i % models.size()];
        b.phase = static_cast<int>(i * 7);
        for (int k = 0; k < 3; ++k) b.at[k] = lo[k] + (hi[k] - lo[k]) * next();
    }

    // Warm-up: first touches allocate every body's geometry, which is a
    // load-time cost and not the frame's.
    for (auto& b : bodies) tickBody(b, 0, lights, nullptr);

    // ---- INLINE: one core, exactly the handoff's situation
    Stages in;
    auto t0 = Clock::now();
    for (int f = 0; f < nFrames; ++f)
        for (auto& b : bodies) tickBody(b, f, lights, &in);
    in.total = msSince(t0);
    const std::uint64_t hInline = hashBodies(bodies);

    const double per = 1.0 / nFrames;
    say("inline   compose %7.3f  apply %7.3f  light %7.3f  total %7.3f ms/frame"
        "  (lights reached %.1f/body)\n",
        in.compose * per, in.apply * per, in.light * per, in.total * per,
        static_cast<double>(in.lightsReached) / (static_cast<double>(nFrames) * nBodies));

    // ---- THREADED: the same ticks through the pool, one body per item.
    // Reset every body to tick 0 first so the last frame posed is the same
    // frame the inline pass ended on, and the hashes are comparable.
    for (auto& b : bodies) tickBody(b, 0, lights, nullptr);
    omk::Threads& pool = omk::Threads::shared();
    t0 = Clock::now();
    for (int f = 0; f < nFrames; ++f)
        pool.parallelFor(0, bodies.size(), 1, [&](std::size_t a, std::size_t z) {
            for (std::size_t i = a; i < z; ++i) tickBody(bodies[i], f, lights, nullptr);
        });
    const double tThreads = msSince(t0);
    const std::uint64_t hThreads = hashBodies(bodies);

    say("threads  %d runners               total %7.3f ms/frame  speedup %.2fx\n",
        pool.runners(), tThreads * per, tThreads > 0 ? in.total / tThreads : 0.0);
    say("hash inline %016llx threads %016llx  threads: %s\n",
        static_cast<unsigned long long>(hInline),
        static_cast<unsigned long long>(hThreads),
        hInline == hThreads ? "EXACT" : "DIFFERENT");
    // The number the decision wanted, in the form the handoff frames it: the
    // M1 spent 6.0 ms on the whole frame, of which these stages were ~1.34.
    say("budget   these stages alone are %.1f%% of a 33.3 ms frame\n",
        100.0 * in.total * per / 33.333);

    // ---- PLACE: `o3de/pointplace.h`, the scripted motion's point placement,
    // GENERIC against NEON on real data - every corner of the light set,
    // through both layouts the game feeds it (a Corner's 12-float stride, and
    // packed triangles, which NEON loads four points at a time), turned by a
    // quaternion that changes every pass. Both outputs are hashed: `neon:
    // EXACT` is the claim the header makes, measured on this machine.
    bool placeOk = true;
    if (const auto p = fs.resolve(kLightSet)) {
        const auto d = omk::DataFs::readPath(*p);
        const omk::Geometry set = omk::buildGeometry(d, omk::DrawFilter::Engine);
        const std::size_t np = set.corners.size();
        std::vector<std::uint32_t> ids(np);
        for (std::size_t i = 0; i < np; ++i) ids[i] = static_cast<std::uint32_t>(i);
        std::vector<float> packed(np * 3);
        for (std::size_t i = 0; i < np; ++i) {
            packed[3 * i] = set.corners[i].x;
            packed[3 * i + 1] = set.corners[i].y;
            packed[3 * i + 2] = set.corners[i].z;
        }
        std::vector<omk::Corner> outC(set.corners);
        std::vector<float> outP(packed.size());
        const int passes = 10;
        const auto pass = [&](int k, omk::PointPlace& pp) {
            const float a = 0.37f * static_cast<float>(k + 1);
            const float h = 0.5f * a;
            pp.q = omk::Quatf{std::cos(h), 0.3f * std::sin(h), 0.9f * std::sin(h),
                              0.3162277f * std::sin(h)};
            pp.rotated = true;
            for (int c = 0; c < 3; ++c) {
                pp.origin[c] = 100.0f * static_cast<float>(c + 1);
                pp.scale[c] = 1.0f + 0.01f * static_cast<float>(c);
                pp.at[c] = -50.0f * static_cast<float>(k - c);
            }
        };
        const auto fnv = [](const void* data, std::size_t bytes, std::uint64_t h) {
            const auto* b = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < bytes; ++i) { h ^= b[i]; h *= 1099511628211ull; }
            return h;
        };
        const auto run = [&](bool neon, double& ms) {
            std::uint64_t h = 1469598103934665603ull;
            ms = 0.0;
            for (int k = 0; k < passes; ++k) {
                omk::PointPlace pp;
                pass(k, pp);
                const auto t = Clock::now();   // the placing only, not the hash
                if (neon) {
                    omk::placePointsNeon(pp, &set.corners[0].x, 12, &outC[0].x, 12, ids.data(), np);
                    omk::placePointsNeon(pp, packed.data(), 3, outP.data(), 3, ids.data(), np);
                } else {
                    omk::placePointsGeneric(pp, &set.corners[0].x, 12, &outC[0].x, 12, ids.data(), np);
                    omk::placePointsGeneric(pp, packed.data(), 3, outP.data(), 3, ids.data(), np);
                }
                ms += msSince(t);
                h = fnv(outC.data(), outC.size() * sizeof(omk::Corner), h);
                h = fnv(outP.data(), outP.size() * sizeof(float), h);
            }
            return h;
        };
        double msG = 0, msN = 0;
        const std::uint64_t hG = run(false, msG);
        const std::uint64_t hN = run(true, msN);
        const bool neon = omk::pointPlaceHasNeon();
        placeOk = !neon || hG == hN;
        // the time is per MILLION points placed, the hashing outside it
        const double mpts = 2.0 * static_cast<double>(np) * passes / 1e6;
        say("place    %zu points x2 layouts x%d  generic %.2f ms/Mpt  neon %.2f ms/Mpt  "
            "speedup %.2fx\n", np, passes, msG / mpts, msN / mpts, msN > 0 ? msG / msN : 0.0);
        say("hash generic %016llx neon %016llx  neon: %s\n",
            static_cast<unsigned long long>(hG), static_cast<unsigned long long>(hN),
            !neon ? "ABSENT (no __ARM_NEON; the generic loop ran twice)"
                  : hG == hN ? "EXACT" : "DIFFERENT");
    }

    if (g_report) std::fclose(g_report);
#if defined(__vita__)
    sceKernelExitProcess(0);
#endif
    return hInline != hThreads ? 3 : placeOk ? 0 : 4;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// THE DEPTH TIE'S NEW STORAGE GIVES THE OLD DECISIONS - draw for draw.
//
//     tie_equiv <model.3DO>...
//
// `DepthTie` (o3de/depthtie.h) replaced the Vulkan backend's in-place tie pass
// to make it cheaper, and is held to deciding exactly what that pass decided.
// This keeps the pass as it stood on 2026-09-13 - ordered sets, rebuilt from
// nothing each revision - as `Reference`, and runs both over the same draws,
// comparing the LOSERS of every draw in content and order:
//
//   * one pass over every batch in order, the blend deciding who writes depth;
//   * the same revision drawn twice (the mirror pass submits it again);
//   * 40 frames, each a new revision: a random mesh moved, a random triangle
//     SNAPPED onto another's positions (a tie made on purpose), a random subset
//     of the batches in a shuffled order, and on some frames every batch drawn
//     first as non-writing - which is the shortcut's case - and then again.
//     Each says which corners it changed, but the shuffled draws rarely let a
//     replay survive, so these exercise the fallback;
//   * 60 frames of a FIXED draw sequence - what a still camera submits - each a
//     new revision that says which corners changed (step 8's replay): three
//     "cargo" meshes moving every frame, a triangle snapped onto a cargo face or
//     a cargo face onto another every few frames, a frame whose list is invalid,
//     a frame missing one draw, a frame drawn twice, and a frame where nothing
//     moved.
//
// And beside the losers, a simulated VERTEX BUFFER driven the way the Vulkan
// backend drives it - a partial upload when the geometry says which corners
// changed since the buffer's revision, `restore` written back, losers
// degenerated - compared at the end of every frame, on every triangle drawn,
// with the full upload plus the reference's losers.
//
// One line a model: the first pass and the 40 frames as before, then the
// replay frames' draws, losers and mismatches, the buffer's bad triangles, how
// the revisions were answered, and the timings; then `mismatches <total>`.
// Prints only; writes nothing.
#include "o3de/depthtie.h"
#include "o3de/geom3do.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

// The in-backend pass as it stood before `DepthTie`, kept verbatim but for the
// GPU half (the vertex buffer write) and the unused face count.
struct Reference {
    std::uint64_t revision = 0;
    std::set<std::array<std::uint32_t, 12>> quads;
    std::set<std::array<std::uint32_t, 9>>  tris;
    std::vector<std::uint8_t> done;

    void resolve(const omk::Geometry* g, std::size_t dstart, std::size_t dcount, bool writes,
                 std::vector<std::size_t>& losers) {
        const std::size_t ntri = g->corners.size() / 3;
        auto& t = *this;
        if (t.revision != g->revision || t.done.size() != ntri) {
            t = Reference{};
            t.revision = g->revision;
            t.done.assign(ntri, 0);
        }
        const auto bits = [](float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; };
        const auto samePos = [&](std::size_t a, std::size_t b) {
            const auto& p = g->corners[a]; const auto& q = g->corners[b];
            return bits(p.x) == bits(q.x) && bits(p.y) == bits(q.y) && bits(p.z) == bits(q.z);
        };
        using P = std::array<std::uint32_t, 3>;
        const auto pos = [&](std::size_t c) {
            const auto& p = g->corners[c]; return P{bits(p.x), bits(p.y), bits(p.z)};
        };
        const std::size_t t0 = dstart / 3, t1 = std::min(ntri, (dstart + dcount) / 3);
        for (std::size_t tri = t0; tri < t1; ++tri) {
            if (t.done[tri]) continue;
            const std::size_t c = 3 * tri;
            const bool quad = tri + 1 < t1 && !t.done[tri + 1] &&
                              samePos(c, c + 3) && samePos(c + 2, c + 4);
            if (quad) {
                std::array<P, 4> ps{pos(c), pos(c + 1), pos(c + 2), pos(c + 5)};
                std::sort(ps.begin(), ps.end());
                std::array<std::uint32_t, 12> key;
                for (int k = 0; k < 4; ++k) for (int j = 0; j < 3; ++j) key[3 * k + j] = ps[k][j];
                t.done[tri] = t.done[tri + 1] = 1;
                if (t.quads.count(key)) { losers.push_back(tri); losers.push_back(tri + 1); }
                else if (writes) t.quads.insert(key);
                ++tri;
            } else {
                std::array<P, 3> ps{pos(c), pos(c + 1), pos(c + 2)};
                std::sort(ps.begin(), ps.end());
                std::array<std::uint32_t, 9> key;
                for (int k = 0; k < 3; ++k) for (int j = 0; j < 3; ++j) key[3 * k + j] = ps[k][j];
                t.done[tri] = 1;
                if (t.tris.count(key)) losers.push_back(tri);
                else if (writes) t.tris.insert(key);
            }
        }
    }
};

struct Lcg {
    std::uint64_t s = 0x9E3779B97F4A7C15ULL;
    std::uint32_t next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(s >> 33);
    }
    double unit() { return next() / 2147483648.0; }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: tie_equiv <model.3DO>...\n");
        return 2;
    }
    using clk = std::chrono::steady_clock;
    long total = 0;
    for (int a = 1; a < argc; ++a) {
        std::ifstream f(argv[a], std::ios::binary);
        const std::vector<char> raw((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
        if (raw.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[a]); return 1; }
        const std::span<const std::byte> d(reinterpret_cast<const std::byte*>(raw.data()), raw.size());
        omk::Geometry g = omk::buildGeometry(d, omk::DrawFilter::Engine);
        std::string stem = argv[a];
        if (const auto s = stem.find_last_of("/\\"); s != std::string::npos) stem = stem.substr(s + 1);
        const std::size_t ntri = g.corners.size() / 3;
        if (ntri == 0 || g.batches.empty()) { std::printf("%s empty\n", stem.c_str()); continue; }

        Reference ref;
        omk::DepthTie now;
        long draws = 0, losers = 0, mismatches = 0;
        long rDraws = 0, rLosers = 0, rMismatches = 0, vboFrames = 0, vboBad = 0;
        double refMs = 0, newMs = 0, rRefMs = 0, rNewMs = 0;
        bool replayPhase = false;
        std::vector<std::size_t> lr, ln, rs;

        // THE SIMULATED VERTEX BUFFER: positions only, the way the backend fills it
        std::vector<float> vbo;
        std::uint64_t vboRev = 0;
        const auto upload = [&]() {
            const bool partial = !vbo.empty() && g.dirtyTo != 0 && g.dirtyTo == g.revision &&
                                 vboRev == g.dirtyFrom;
            if (partial) {
                for (const std::uint32_t c : g.dirtyCorners) {
                    if (c >= g.corners.size()) continue;
                    vbo[3 * c] = g.corners[c].x; vbo[3 * c + 1] = g.corners[c].y; vbo[3 * c + 2] = g.corners[c].z;
                }
            } else {
                vbo.resize(3 * g.corners.size());
                for (std::size_t c = 0; c < g.corners.size(); ++c) {
                    vbo[3 * c] = g.corners[c].x; vbo[3 * c + 1] = g.corners[c].y; vbo[3 * c + 2] = g.corners[c].z;
                }
                now.vboReplaced();
            }
            vboRev = g.revision;
        };
        std::vector<std::uint8_t> refLoser(ntri, 0), drawn(ntri, 0);
        std::uint64_t flagsRev = ~0ull;

        const auto draw = [&](const omk::Batch& b, bool writes) {
            if (vbo.empty() || vboRev != g.revision) upload();
            if (flagsRev != g.revision) {
                std::fill(refLoser.begin(), refLoser.end(), 0);
                std::fill(drawn.begin(), drawn.end(), 0);
                flagsRev = g.revision;
            }
            lr.clear(); ln.clear(); rs.clear();
            const auto a0 = clk::now();
            ref.resolve(&g, b.start, b.count, writes, lr);
            const auto a1 = clk::now();
            now.resolve(g, b.start, b.count, writes, ln, rs);
            const auto a2 = clk::now();
            const double rm = std::chrono::duration<double, std::milli>(a1 - a0).count();
            const double nm = std::chrono::duration<double, std::milli>(a2 - a1).count();
            if (replayPhase) {
                rRefMs += rm; rNewMs += nm;
                ++rDraws;
                rLosers += static_cast<long>(lr.size());
                if (lr != ln) ++rMismatches;
            } else {
                refMs += rm; newMs += nm;
                ++draws;
                losers += static_cast<long>(lr.size());
                if (lr != ln) ++mismatches;
            }
            for (const std::size_t t : rs)
                for (std::size_t c = 3 * t; c < 3 * t + 3 && c < g.corners.size(); ++c) {
                    vbo[3 * c] = g.corners[c].x; vbo[3 * c + 1] = g.corners[c].y; vbo[3 * c + 2] = g.corners[c].z;
                }
            for (const std::size_t t : ln) {
                const std::size_t c = 3 * t;
                if (c + 2 >= g.corners.size()) continue;
                for (int k = 1; k < 3; ++k)
                    for (int j = 0; j < 3; ++j) vbo[3 * (c + k) + j] = vbo[3 * c + j];
            }
            for (const std::size_t t : lr) refLoser[t] = 1;
            const std::size_t t1 = std::min(ntri, (b.start + b.count) / 3);
            for (std::size_t t = b.start / 3; t < t1; ++t) drawn[t] = 1;
        };
        // what the backend's full upload plus the reference's losers would hold
        const auto checkVbo = [&]() {
            ++vboFrames;
            if (flagsRev != g.revision) return;           // nothing drawn this revision
            for (std::size_t t = 0; t < ntri; ++t) {
                if (!drawn[t]) continue;
                bool bad = false;
                for (std::size_t k = 0; k < 3 && !bad; ++k) {
                    const std::size_t src = refLoser[t] ? 3 * t : 3 * t + k;
                    const float want[3] = {g.corners[src].x, g.corners[src].y, g.corners[src].z};
                    bad = std::memcmp(want, &vbo[3 * (3 * t + k)], sizeof want) != 0;
                }
                if (bad) ++vboBad;
            }
        };
        const auto writesOf = [](const omk::Batch& b) { return b.blend == omk::Blend::Opaque; };
        const auto newRevision = [&](const std::vector<std::uint32_t>& dirty, bool valid) {
            const std::uint64_t prev = g.revision;
            ++g.revision;
            g.dirtyFrom = valid ? prev : 0;
            g.dirtyTo = valid ? g.revision : 0;
            g.dirtyCorners = dirty;
        };

        // one pass, then the same revision again (the mirror)
        g.revision = 1;
        for (const auto& b : g.batches) draw(b, writesOf(b));
        const long staticLosers = losers;
        for (const auto& b : g.batches) draw(b, writesOf(b));
        checkVbo();

        // changing frames
        Lcg r;
        int maxMesh = 0;
        for (auto m : g.cornerMesh) maxMesh = std::max(maxMesh, static_cast<int>(m));
        std::vector<std::size_t> order(g.batches.size());
        std::vector<std::uint32_t> dirty;
        for (int frame = 0; frame < 40; ++frame) {
            dirty.clear();
            if (!g.cornerMesh.empty() && (frame % 3) != 2) {   // move a mesh
                const int m = static_cast<int>(r.next() % static_cast<std::uint32_t>(maxMesh + 1));
                const float dx = static_cast<float>(r.unit() * 40.0 - 20.0);
                for (std::size_t c = 0; c < g.corners.size() && c < g.cornerMesh.size(); ++c)
                    if (g.cornerMesh[c] == m) {
                        g.corners[c].x += dx; g.corners[c].z -= dx;
                        dirty.push_back(static_cast<std::uint32_t>(c));
                    }
            }
            for (int k = 0; k < 3; ++k) {   // make a tie on purpose
                const std::size_t A = r.next() % ntri, B = r.next() % ntri;
                for (int v = 0; v < 3; ++v) {
                    g.corners[3 * A + v].x = g.corners[3 * B + v].x;
                    g.corners[3 * A + v].y = g.corners[3 * B + v].y;
                    g.corners[3 * A + v].z = g.corners[3 * B + v].z;
                    dirty.push_back(static_cast<std::uint32_t>(3 * A + v));
                }
            }
            newRevision(dirty, true);
            for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
            for (std::size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[r.next() % i]);
            const std::size_t keep = order.size() * 7 / 10 + 1;
            if (frame % 4 == 1)   // every drawn batch first as non-writing
                for (std::size_t i = 0; i < keep && i < order.size(); ++i)
                    draw(g.batches[order[i]], false);
            for (std::size_t i = 0; i < keep && i < order.size(); ++i) {
                const auto& b = g.batches[order[i]];
                draw(b, (frame % 5 == 3) ? !writesOf(b) : writesOf(b));
            }
            checkVbo();
        }

        // THE REPLAY FRAMES: one fixed sequence, a few meshes moving every frame
        replayPhase = true;
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        for (std::size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[r.next() % i]);
        std::vector<std::size_t> seq(order.begin(), order.begin() + static_cast<long>(order.size() * 85 / 100 + 1));
        std::vector<int> cargo;
        std::vector<std::uint32_t> cargoTris;
        if (!g.cornerMesh.empty()) {
            for (int k = 0; k < 3; ++k) {
                // a mesh that is drawn: the owner of a random corner
                const std::size_t c = r.next() % std::min(g.corners.size(), g.cornerMesh.size());
                cargo.push_back(g.cornerMesh[c]);
            }
            for (std::size_t c = 0; c + 2 < g.corners.size() && c + 2 < g.cornerMesh.size(); c += 3)
                if (std::find(cargo.begin(), cargo.end(), g.cornerMesh[c]) != cargo.end())
                    cargoTris.push_back(static_cast<std::uint32_t>(c / 3));
        }
        for (int frame = 0; frame < 60; ++frame) {
            dirty.clear();
            if (frame % 19 != 10) {                              // the cargo moves
                const float dx = static_cast<float>((frame % 10) - 5) * 0.375f;
                for (std::size_t c = 0; c < g.corners.size() && c < g.cornerMesh.size(); ++c)
                    if (std::find(cargo.begin(), cargo.end(), g.cornerMesh[c]) != cargo.end()) {
                        g.corners[c].x += dx; g.corners[c].y -= 0.5f * dx;
                        dirty.push_back(static_cast<std::uint32_t>(c));
                    }
            }
            if (frame % 7 == 3 && !cargoTris.empty()) {          // a tie made on purpose
                std::size_t A = cargoTris[r.next() % cargoTris.size()], B = r.next() % ntri;
                if (frame % 14 == 10) std::swap(A, B);           // onto a cargo face, or a cargo face onto another
                for (int v = 0; v < 3; ++v) {
                    g.corners[3 * A + v].x = g.corners[3 * B + v].x;
                    g.corners[3 * A + v].y = g.corners[3 * B + v].y;
                    g.corners[3 * A + v].z = g.corners[3 * B + v].z;
                    dirty.push_back(static_cast<std::uint32_t>(3 * A + v));
                }
            }
            newRevision(dirty, frame % 13 != 5);                 // one frame whose list is not valid
            std::vector<std::size_t> s = seq;
            if (frame % 11 == 4 && s.size() > 1) s.erase(s.begin() + static_cast<long>(frame % s.size()));
            const int passes = frame % 17 == 8 ? 2 : 1;
            for (int p = 0; p < passes; ++p)
                for (const std::size_t i : s) draw(g.batches[i], writesOf(g.batches[i]));
            checkVbo();
        }

        std::printf("%s triangles %zu batches %zu | static losers %ld | draws %ld losers %ld "
                    "mismatches %ld | ref_ms %.1f new_ms %.1f | replay frames: draws %ld losers %ld "
                    "mismatches %ld | vbo frames %ld bad triangles %ld | revisions: replayed %ld walked %ld "
                    "fallbacks %ld | replay ref_ms %.1f new_ms %.1f\n",
                    stem.c_str(), ntri, g.batches.size(), staticLosers, draws, losers, mismatches,
                    refMs, newMs, rDraws, rLosers, rMismatches, vboFrames, vboBad,
                    now.replays, now.walks, now.fallbacks, rRefMs, rNewMs);
        total += mismatches + rMismatches + vboBad;
    }
    std::printf("mismatches %ld\n", total);
    return total == 0 ? 0 : 3;
}

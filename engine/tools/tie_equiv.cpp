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
//
// One line a model with draws, losers, mismatches and both timings, then
// `mismatches <total>`. Prints only; writes nothing.
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
        double refMs = 0, newMs = 0;
        std::vector<std::size_t> lr, ln;
        const auto draw = [&](const omk::Batch& b, bool writes) {
            lr.clear(); ln.clear();
            const auto a0 = clk::now();
            ref.resolve(&g, b.start, b.count, writes, lr);
            const auto a1 = clk::now();
            now.resolve(g, b.start, b.count, writes, ln);
            const auto a2 = clk::now();
            refMs += std::chrono::duration<double, std::milli>(a1 - a0).count();
            newMs += std::chrono::duration<double, std::milli>(a2 - a1).count();
            ++draws;
            losers += static_cast<long>(lr.size());
            if (lr != ln) ++mismatches;
        };
        const auto writesOf = [](const omk::Batch& b) { return b.blend == omk::Blend::Opaque; };

        // one pass, then the same revision again (the mirror)
        g.revision = 1;
        for (const auto& b : g.batches) draw(b, writesOf(b));
        const long staticLosers = losers;
        for (const auto& b : g.batches) draw(b, writesOf(b));

        // changing frames
        Lcg r;
        int maxMesh = 0;
        for (auto m : g.cornerMesh) maxMesh = std::max(maxMesh, static_cast<int>(m));
        std::vector<std::size_t> order(g.batches.size());
        for (int frame = 0; frame < 40; ++frame) {
            ++g.revision;
            if (!g.cornerMesh.empty() && (frame % 3) != 2) {   // move a mesh
                const int m = static_cast<int>(r.next() % static_cast<std::uint32_t>(maxMesh + 1));
                const float dx = static_cast<float>(r.unit() * 40.0 - 20.0);
                for (std::size_t c = 0; c < g.corners.size() && c < g.cornerMesh.size(); ++c)
                    if (g.cornerMesh[c] == m) { g.corners[c].x += dx; g.corners[c].z -= dx; }
            }
            for (int k = 0; k < 3; ++k) {   // make a tie on purpose
                const std::size_t A = r.next() % ntri, B = r.next() % ntri;
                for (int v = 0; v < 3; ++v) {
                    g.corners[3 * A + v].x = g.corners[3 * B + v].x;
                    g.corners[3 * A + v].y = g.corners[3 * B + v].y;
                    g.corners[3 * A + v].z = g.corners[3 * B + v].z;
                }
            }
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
        }
        std::printf("%s triangles %zu batches %zu | static losers %ld | draws %ld losers %ld "
                    "mismatches %ld | ref_ms %.1f new_ms %.1f\n",
                    stem.c_str(), ntri, g.batches.size(), staticLosers, draws, losers, mismatches,
                    refMs, newMs);
        total += mismatches;
    }
    std::printf("mismatches %ld\n", total);
    return total == 0 ? 0 : 3;
}

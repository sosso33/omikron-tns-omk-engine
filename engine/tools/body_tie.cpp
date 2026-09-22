// SPDX-License-Identifier: GPL-3.0-or-later
// THE RIGID BODY TIE, held to the per-frame walk (`Geometry::tieClass`).
//
// A posed body used to be re-walked whole by the depth tie every frame. With
// a tie CLASS on each corner (its mesh; its own vertex where the face is
// morphed) the walk's answer is invariant under the pose and the next
// revision replays it. This tool poses each model through 240 rigid poses -
// the rest pose first, then random ones, with a random face stream switched
// on and off so the classes change - and, draw for draw, compares:
//
//   * REF:  the original position-only pass, walked afresh every frame
//           (`tie_equiv.cpp`'s `Reference`, verbatim);
//   * NEW:  `DepthTie` on the geometry as `applyPose` hands it to a backend,
//           class-keyed and replaying.
//
// The two may differ only where faces of DIFFERENT classes coincide - a seam
// closed in one pose, which the engine ties for that frame only and the class
// key never does. Those are counted separately (`cross`), by a third walk:
// `DepthTie` on the same corners with the class stripped, which is the
// position-only rule and must equal REF exactly (`engine: tie equivalence`).
// So: mismatches(NEW, REF) == cross, and mismatches(POS, REF) == 0.
//
//     body_tie <character.3DO>...
#include "actor/pose.h"
#include "o3de/depthtie.h"
#include "o3de/geom3do.h"
#include "formats/mesh3do.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

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
    float unit() { return static_cast<float>(next()) / 2147483648.0f; }
};

// the same losers, as sorted sets
long mismatch(std::vector<std::size_t> a, std::vector<std::size_t> b) {
    std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
    std::vector<std::size_t> d;
    std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(d));
    return static_cast<long>(d.size());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: body_tie <character.3DO>...\n");
        return 2;
    }
    using clk = std::chrono::steady_clock;
    long totalBad = 0;
    for (int a = 1; a < argc; ++a) {
        std::ifstream f(argv[a], std::ios::binary);
        const std::vector<char> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (raw.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[a]); return 1; }
        const std::span<const std::byte> md(reinterpret_cast<const std::byte*>(raw.data()), raw.size());
        std::string stem = argv[a];
        if (const auto s = stem.find_last_of("/\\"); s != std::string::npos) stem = stem.substr(s + 1);
        const auto mh = omk::readHeader(md);
        if (!mh) { std::printf("%s no header\n", stem.c_str()); continue; }
        const auto meshes = omk::readMeshes(md, *mh);
        const omk::Geometry rest = omk::buildGeometry(md, omk::DrawFilter::Engine);
        if (rest.corners.empty() || rest.cornerMesh.size() != rest.corners.size()) {
            std::printf("%s not a posable model\n", stem.c_str());
            continue;
        }
        const omk::FaceMesh face = omk::faceMeshOf(meshes);
        Lcg r;
        const auto randomPose = [&]() {
            std::vector<omk::MeshPose> p(meshes.size());
            for (auto& mp : p) {
                float q[4] = {r.unit() - 0.5f, r.unit() - 0.5f, r.unit() - 0.5f, r.unit() - 0.5f};
                const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
                if (n > 1e-6f) for (auto& v : q) v /= n; else q[0] = 1.0f;
                mp.q = omk::Quatf{q[0], q[1], q[2], q[3]};
                for (auto& v : mp.pos) v = r.unit() * 200.0f - 100.0f;
            }
            return p;
        };
        std::vector<float> faceStream(face.valid() ? 3u * static_cast<std::size_t>(face.count) : 0u);

        omk::Geometry gNew, gPos;
        omk::DepthTie tieNew, tiePos;
        Reference ref;
        long draws = 0, losersRef = 0, losersNew = 0, badNew = 0, badPos = 0, cross = 0;
        long framesBad = 0, restCross = 0;
        double msRef = 0, msNew = 0, msPos = 0;
        std::vector<std::size_t> lr, ln, lp, restore;
        // THE BACKEND'S BUFFER under the DELTA strategy (`glesrender.cpp`,
        // 2026-09-22), on a tie of its own: a same-size full rewrite FOLDS the
        // applied losers in and keeps the tie's state, and a draw writes only
        // what the tie NEWLY marks. A posed body is the case that needs the
        // fold - its coincident pairs move rigidly and stay tied, so they are
        // never newly marked again and only the fold keeps them degenerate.
        omk::DepthTie tieD;
        std::vector<float> vb;
        std::vector<std::size_t> lnD, rsD;
        std::vector<std::uint8_t> loserD, drawnD;
        long vbBad = 0, vbWrites = 0, vbWritesOld = 0;
        const auto putTri = [&](const omk::Geometry& gg, std::size_t t, bool degenerate) {
            for (std::size_t k = 0; k < 3; ++k) {
                const std::size_t src = degenerate ? 3 * t : 3 * t + k;
                vb[3 * (3 * t + k)]     = gg.corners[src].x;
                vb[3 * (3 * t + k) + 1] = gg.corners[src].y;
                vb[3 * (3 * t + k) + 2] = gg.corners[src].z;
            }
        };
        for (int step = 0; step < 240; ++step) {
            // the rest pose first (every mesh at its own place), then random
            const std::vector<omk::MeshPose> pose = step == 0
                ? omk::composePose(meshes, omk::NodeTracks{}, 0, false) : randomPose();
            const bool morph = face.valid() && (step / 50) % 2 == 1;
            if (morph) for (auto& v : faceStream) v = r.unit() * 40.0f - 20.0f;
            omk::applyPose(gNew, rest, meshes, pose, &face, morph ? &faceStream : nullptr);
            // the position-only rule on the SAME corners
            gPos = gNew;
            gPos.tieClass.clear();
            gPos.tieRigidFrom = 0;
            bool frameBad = false;
            // this pose's upload, as the backend does it
            const std::size_t ntriD = gNew.corners.size() / 3;
            if (vb.size() != 3 * gNew.corners.size()) {
                vb.assign(3 * gNew.corners.size(), 0.0f);
                for (std::size_t t = 0; t < ntriD; ++t) putTri(gNew, t, false);
                tieD.vboReplaced();
            } else {
                for (std::size_t t = 0; t < ntriD; ++t) putTri(gNew, t, tieD.isApplied(t));
            }
            loserD.assign(ntriD, 0);
            drawnD.assign(ntriD, 0);
            for (const auto& b : gNew.batches) {
                const bool writes = b.blend == omk::Blend::Opaque;
                lr.clear(); ln.clear(); lp.clear(); restore.clear();
                auto t0 = clk::now();
                ref.resolve(&gNew, b.start, b.count, writes, lr);
                auto t1 = clk::now();
                tieNew.resolve(gNew, b.start, b.count, writes, ln, restore);
                auto t2 = clk::now();
                tiePos.resolve(gPos, b.start, b.count, writes, lp, restore);
                auto t3 = clk::now();
                msRef += std::chrono::duration<double, std::milli>(t1 - t0).count();
                msNew += std::chrono::duration<double, std::milli>(t2 - t1).count();
                msPos += std::chrono::duration<double, std::milli>(t3 - t2).count();
                ++draws;
                losersRef += static_cast<long>(lr.size());
                losersNew += static_cast<long>(ln.size());
                const long bn = mismatch(ln, lr), bp = mismatch(lp, lr), cx = mismatch(ln, lp);
                badNew += bn; badPos += bp; cross += cx;
                if (step == 0) restCross += cx;
                if (bn || bp) frameBad = true;
                // the delta strategy's draw
                lnD.clear(); rsD.clear();
                tieD.resolve(gNew, b.start, b.count, writes, lnD, rsD);
                for (const std::size_t t : rsD) if (t < ntriD) putTri(gNew, t, false);
                for (const std::size_t t : tieD.newlyApplied()) if (t < ntriD) putTri(gNew, t, true);
                vbWrites += static_cast<long>(rsD.size() + tieD.newlyApplied().size());
                vbWritesOld += static_cast<long>(rsD.size() + lnD.size());
                for (const std::size_t t : lnD) if (t < ntriD) loserD[t] = 1;
                const std::size_t e = std::min(ntriD, (b.start + b.count) / 3);
                for (std::size_t t = b.start / 3; t < e; ++t) drawnD[t] = 1;
            }
            // every drawn triangle as the tie says it must be
            for (std::size_t t = 0; t < ntriD; ++t) {
                if (!drawnD[t]) continue;
                for (std::size_t k = 0; k < 3; ++k) {
                    const std::size_t src = loserD[t] ? 3 * t : 3 * t + k;
                    const float want[3] = {gNew.corners[src].x, gNew.corners[src].y, gNew.corners[src].z};
                    if (std::memcmp(want, &vb[3 * (3 * t + k)], sizeof want) != 0) { ++vbBad; break; }
                }
            }
            if (frameBad) ++framesBad;
        }
        // tieNew must have REPLAYED every rigid revision: one walk per class change
        std::printf("%s face %s | triangles %zu batches %zu | draws %ld losers ref %ld new %ld | "
                    "mismatches new %ld pos %ld cross %ld (at rest %ld) frames-with-any %ld | "
                    "walks %ld replays %ld fallbacks %ld | ms ref %.1f new %.1f pos %.1f | "
                    "buffer: bad triangles %ld writes %ld against %ld\n",
                    stem.c_str(), face.valid() ? "morphed 100 of 240" : "none", rest.corners.size() / 3,
                    rest.batches.size(), draws, losersRef,
                    losersNew, badNew, badPos, cross, restCross, framesBad, tieNew.walks,
                    tieNew.replays, tieNew.fallbacks, msRef, msNew, msPos,
                    vbBad, vbWrites, vbWritesOld);
        totalBad += badPos + (badNew - cross) + vbBad;
    }
    std::printf("bad %ld\n", totalBad);
    return totalBad == 0 ? 0 : 1;
}

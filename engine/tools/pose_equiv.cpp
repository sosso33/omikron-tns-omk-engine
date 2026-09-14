// SPDX-License-Identifier: GPL-3.0-or-later
// THE IN-PLACE POSE GIVES THE COPYING POSE'S GEOMETRY - every field, every call.
//
//     pose_equiv <character.3DO>...
//
// `applyPose` (actor/pose.cpp, todo/optimization.md step 10) stopped copying
// the whole rest geometry on every call. This keeps the version that did,
// verbatim, as `referencePose`, and drives both the same way over a sequence
// of calls built to find a difference:
//
//   * random bone poses, and the face morph on and off (a valid frame, and one
//     of the wrong size that must fall back to the bind pose);
//   * the posed geometry EDITED between calls - positions, colours, texels,
//     shimmer phases, metadata, the revision and the dirty list - the way the
//     viewer's placement and lighting passes rewrite it after every pose;
//   * a switch to a CUT rest geometry (half the meshes, as `lodRestFor` makes
//     for a crowd model's LOD) and back, so the copying path runs too;
//   * the same object posed from itself.
//
// After every call every field is compared: the corners byte for byte, each
// per-corner vector, the batches field by field, the revision, the dirty list.
// It also prints `sizeof(Geometry)`: the in-place path copies the fields it
// knows, and a field added to `Geometry` later would be silently left behind -
// `verify.py: engine: pose equivalence` pins the size so that shows.
//
// One line a model, then `mismatches <total>`. Prints only; writes nothing.
#include "actor/pose.h"
#include "formats/mesh3do.h"
#include "o3de/geom3do.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

// `applyPose` as it stood before step 10, kept verbatim.
void referencePose(omk::Geometry& g, const omk::Geometry& rest,
                   const std::vector<omk::Mesh>& meshes,
                   const std::vector<omk::MeshPose>& pose,
                   const omk::FaceMesh* face, const std::vector<float>* faceVerts) {
    using namespace omk;
    if (rest.cornerMesh.size() != rest.corners.size()) return;
    const std::uint64_t was = g.revision;
    g = rest;
    g.revision = was + 1;
    const bool morphFace =
        face && face->valid() && faceVerts &&
        faceVerts->size() == 3u * static_cast<std::size_t>(face->count) &&
        rest.cornerVertex.size() == rest.corners.size();
    for (std::size_t i = 0; i < g.corners.size(); ++i) {
        const std::int32_t mi = rest.cornerMesh[i];
        if (mi < 0 || static_cast<std::size_t>(mi) >= meshes.size() ||
            static_cast<std::size_t>(mi) >= pose.size()) continue;
        const Mesh& m = meshes[static_cast<std::size_t>(mi)];
        const MeshPose& mp = pose[static_cast<std::size_t>(mi)];
        float local[3];
        bool done = false;
        if (morphFace && mi == face->mesh) {
            const std::int32_t gv = rest.cornerVertex[i];
            const auto k = static_cast<std::size_t>(gv) - face->base;
            if (gv >= 0 && static_cast<std::size_t>(gv) >= face->base &&
                k < static_cast<std::size_t>(face->count)) {
                local[0] = (*faceVerts)[3 * k];
                local[1] = (*faceVerts)[3 * k + 1];
                local[2] = (*faceVerts)[3 * k + 2];
                done = true;
            }
        }
        if (!done) {
            local[0] = rest.corners[i].x - m.pos[0];
            local[1] = rest.corners[i].y - m.pos[1];
            local[2] = rest.corners[i].z - m.pos[2];
        }
        float r[3];
        qrot(mp.q, local, r);
        g.corners[i].x = mp.pos[0] + r[0];
        g.corners[i].y = mp.pos[1] + r[1];
        g.corners[i].z = mp.pos[2] + r[2];
        const float n0[3] = {rest.corners[i].nx, rest.corners[i].ny, rest.corners[i].nz};
        float rn[3];
        qrot(mp.q, n0, rn);
        g.corners[i].nx = rn[0];
        g.corners[i].ny = rn[1];
        g.corners[i].nz = rn[2];
    }
}

struct Lcg {
    std::uint64_t s = 0x9E3779B97F4A7C15ULL;
    std::uint32_t next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(s >> 33);
    }
    float unit() { return static_cast<float>(next()) / 2147483648.0f; }   // [0, 1)
};

bool sameGeometry(const omk::Geometry& a, const omk::Geometry& b) {
    if (a.corners.size() != b.corners.size() ||
        (!a.corners.empty() &&
         std::memcmp(a.corners.data(), b.corners.data(), a.corners.size() * sizeof(omk::Corner)) != 0))
        return false;
    if (a.batches.size() != b.batches.size()) return false;
    for (std::size_t i = 0; i < a.batches.size(); ++i) {
        const auto& x = a.batches[i]; const auto& y = b.batches[i];
        if (x.material != y.material || x.cutout != y.cutout || x.blend != y.blend ||
            x.start != y.start || x.count != y.count) return false;
    }
    return a.cornerMirror == b.cornerMirror && a.cornerMesh == b.cornerMesh &&
           a.cornerVertex == b.cornerVertex && a.cornerDeclared == b.cornerDeclared &&
           a.revision == b.revision && a.dirtyFrom == b.dirtyFrom && a.dirtyTo == b.dirtyTo &&
           a.dirtyCorners == b.dirtyCorners;
}

// Half the meshes' triangles, batches rebuilt - the shape `lodRestFor` gives.
omk::Geometry cutRest(const omk::Geometry& rest) {
    omk::Geometry g;
    for (const auto& b : rest.batches) {
        omk::Batch nb = b;
        nb.start = g.corners.size(); nb.count = 0;
        for (std::size_t c = b.start; c + 3 <= b.start + b.count; c += 3) {
            if (rest.cornerMesh[c] % 2 != 0) continue;
            for (std::size_t k = 0; k < 3; ++k) {
                g.corners.push_back(rest.corners[c + k]);
                g.cornerMesh.push_back(rest.cornerMesh[c + k]);
                if (!rest.cornerVertex.empty()) g.cornerVertex.push_back(rest.cornerVertex[c + k]);
                if (!rest.cornerDeclared.empty()) g.cornerDeclared.push_back(rest.cornerDeclared[c + k]);
            }
            nb.count += 3;
        }
        if (nb.count) g.batches.push_back(nb);
    }
    return g;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: pose_equiv <character.3DO>...\n");
        return 2;
    }
    using clk = std::chrono::steady_clock;
    std::printf("geometry size %zu\n", sizeof(omk::Geometry));
    long total = 0;
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
        const omk::Geometry cut = cutRest(rest);
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
        std::vector<float> faceGood(face.valid() ? 3u * static_cast<std::size_t>(face.count) : 0u);
        std::vector<float> faceBad(faceGood.size() + 3);
        // Edit both geometries the same way, as the viewer's later passes do.
        const auto edit = [&](omk::Geometry& g, std::uint32_t seed) {
            Lcg e; e.s = seed;
            for (int k = 0; k < 8 && !g.corners.empty(); ++k) {
                auto& c = g.corners[e.next() % g.corners.size()];
                c.x += 3.0f; c.y -= 2.0f; c.r = e.unit(); c.g = e.unit(); c.b = e.unit();
                c.u = e.unit() * 64.0f; c.phase = e.unit(); c.nx = -c.nx;
            }
            if (!g.cornerMesh.empty()) g.cornerMesh[e.next() % g.cornerMesh.size()] = -7;
            if (!g.cornerVertex.empty()) g.cornerVertex[e.next() % g.cornerVertex.size()] = 99999;
            g.batches.push_back(omk::Batch{});
            g.revision += 5;
            g.dirtyFrom = 11; g.dirtyTo = g.revision; g.dirtyCorners.assign(3, 42u);
        };
        omk::Geometry gNew, gRef;
        long calls = 0, bad = 0;
        for (int step = 0; step < 240; ++step) {
            const bool useCut = (step >= 60 && step < 70) || step == 150;
            const omk::Geometry& rs = useCut ? cut : rest;
            const auto pose = randomPose();
            for (auto& v : faceGood) v = r.unit() * 10.0f;
            const int faceMode = step % 3;   // none, a good frame, a wrong-sized frame
            const std::vector<float>* fv = faceMode == 1 ? &faceGood : faceMode == 2 ? &faceBad : nullptr;
            omk::applyPose(gNew, rs, meshes, pose, fv ? &face : nullptr, fv);
            referencePose(gRef, rs, meshes, pose, fv ? &face : nullptr, fv);
            ++calls;
            if (!sameGeometry(gNew, gRef)) ++bad;
            if (step % 4 == 2) {
                const std::uint32_t seed = r.next();
                edit(gNew, seed);
                edit(gRef, seed);
            }
            if (step == 200) {                  // posed from itself
                omk::Geometry selfNew = gNew, selfRef = gRef;
                omk::applyPose(selfNew, selfNew, meshes, pose);
                referencePose(selfRef, selfRef, meshes, pose, nullptr, nullptr);
                ++calls;
                if (!sameGeometry(selfNew, selfRef)) ++bad;
            }
        }
        // timing over the steady case: the same rest, a fresh pose each call
        const auto pose = randomPose();
        omk::Geometry tNew, tRef;
        omk::applyPose(tNew, rest, meshes, pose);
        referencePose(tRef, rest, meshes, pose, nullptr, nullptr);
        const auto t0 = clk::now();
        for (int k = 0; k < 2000; ++k) referencePose(tRef, rest, meshes, pose, nullptr, nullptr);
        const auto t1 = clk::now();
        for (int k = 0; k < 2000; ++k) omk::applyPose(tNew, rest, meshes, pose);
        const auto t2 = clk::now();
        std::printf("%s corners %zu cut %zu meshes %zu face %d | calls %ld mismatches %ld | "
                    "2000 poses: ref_ms %.1f new_ms %.1f\n",
                    stem.c_str(), rest.corners.size(), cut.corners.size(), meshes.size(),
                    face.valid() ? face.count : 0, calls, bad,
                    std::chrono::duration<double, std::milli>(t1 - t0).count(),
                    std::chrono::duration<double, std::milli>(t2 - t1).count());
        total += bad;
    }
    std::printf("mismatches %ld\n", total);
    return total == 0 ? 0 : 3;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// A DOOR'S TWINS (`o3de/settwin.h`), asked of two sets' files: the meshes of one
// name at one place, how many of their faces lie on the same corners, and
// whether the two copies are lit alike - which is what decides whether a fight
// between them could be seen at all.
//
//     door_twins <a.3DO> <b.3DO> [apart]
//
// `apart` is the corner tolerance, kTwinCornerApart by default.
#include "formats/mesh3do.h"
#include "o3de/geom3do.h"
#include "o3de/settwin.h"
#include "platform/datafs.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {
struct Loaded {
    std::vector<std::byte> bytes;
    std::vector<omk::Mesh> meshes;
    omk::Geometry geo;
};
Loaded load(const char* path) {
    Loaded l;
    l.bytes = omk::DataFs::readPath(path);
    if (const auto h = omk::readHeader(l.bytes)) {
        l.meshes = omk::readMeshes(l.bytes, *h);
        l.geo = omk::buildGeometry(l.bytes, omk::DrawFilter::Engine);
    }
    return l;
}
std::vector<omk::TwinFace> facesOf(const omk::Geometry& g, int mi, std::vector<float>* grey) {
    std::vector<omk::TwinFace> out;
    for (const auto& b : g.batches)
        for (std::size_t c = b.start; c + 2 < b.start + b.count; c += 3) {
            if (g.cornerMesh[c] != mi) continue;
            omk::TwinFace f;
            f.opaque = b.blend == omk::Blend::Opaque && !b.cutout;
            for (std::size_t k = 0; k < 3; ++k) {
                const omk::Corner& q = g.corners[c + k];
                f.xyz[3 * k] = q.x; f.xyz[3 * k + 1] = q.y; f.xyz[3 * k + 2] = q.z;
                grey->push_back(q.r + q.g + q.b);
            }
            out.push_back(f);
        }
    return out;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: door_twins <a.3DO> <b.3DO> [apart]\n"); return 2; }
    const Loaded a = load(argv[1]), b = load(argv[2]);
    const float apart = argc > 3 ? static_cast<float>(std::atof(argv[3])) : omk::kTwinCornerApart;
    std::map<std::string, const omk::Mesh*> inA;
    for (const auto& m : a.meshes) inA.emplace(m.name, &m);
    int meshes = 0, tied = 0, unlike = 0;
    for (const auto& m : b.meshes) {
        const auto it = inA.find(m.name);
        if (it == inA.end() || std::fabs(it->second->pos[0] - m.pos[0]) >= 1.0f ||
            std::fabs(it->second->pos[1] - m.pos[1]) >= 1.0f ||
            std::fabs(it->second->pos[2] - m.pos[2]) >= 1.0f) continue;
        std::vector<float> ga, gb;
        const auto fa = facesOf(a.geo, it->second->index, &ga), fb = facesOf(b.geo, m.index, &gb);
        const omk::TwinVerdict v = omk::settleTwinFaces(fa, fb, apart);
        if (!v.tied) continue;
        // the two copies' whole light, compared as a sum: the faces come in
        // different orders, and whether they are lit alike needs no pairing
        double sa = 0, sb = 0;
        for (const float x : ga) sa += x;
        for (const float x : gb) sb += x;
        const bool alike = std::fabs(sa - sb) < 0.01 * (sa + 1.0);
        ++meshes;
        tied += v.tied;
        unlike += alike ? 0 : 1;
        std::printf("%-14s tied %3d of %zu/%zu  lit %s\n", m.name, v.tied, fa.size(), fb.size(),
                    alike ? "alike" : "differently");
    }
    std::printf("twins: %d meshes, %d faces, %d lit differently\n", meshes, tied, unlike);
    return 0;
}

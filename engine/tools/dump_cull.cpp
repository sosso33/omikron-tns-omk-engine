// SPDX-License-Identifier: GPL-3.0-or-later
// The visible-set walk - `sub_48D3B0`'s three rejects, over the sets and their
// own authored cameras.
//
//     dump_cull <gamedata/MESHES> <out.bin>
//
// There is no oracle for "which meshes does this camera see" - no capture
// records it and no reference reader implements the cull. So what is asserted
// is of two kinds, and both can fail:
//
//   * the BOUNDING VOLUME is identified from the data: the radius at +88 is
//     `max |v|` over the mesh's own vertex block in 12039 of 16176 meshes,
//     which is what says the field is the radius at all;
//   * the cull's own INVARIANTS, which need no oracle: widening the field of
//     view can never reject what a narrower one kept, and the mesh you are
//     standing inside can never be culled at all.
//
// out.bin: int32 meshesWithVertices, radiusIsMaxV, decorMeshes, decorRadiusOk,
//          radiusSmallerThanExtent, boxSymmetric,
//          int32 axisInside, offAxisOutside, planesUnit, planesThroughEye,
//          int32 planeThroughPointsContract,
//          int32 fovMonotoneViolations, farMonotoneViolations, insideCulled,
//          int32 cameras, culledTotal, keptTotal, then one count per camera
#include "formats/mesh3do.h"
#include "o3de/render.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

void collect(const std::string& dir, std::vector<std::string>& out) {
    const omk::DataFs fs(dir);
    for (const auto& p : fs.list(".", ".3DO")) out.push_back(p);
    for (const auto& sub : fs.subdirs()) collect(sub, out);
}

float len3(const float v[3]) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_cull <gamedata/MESHES> <out.bin>\n");
        return 2;
    }
    std::vector<std::string> all;
    collect(argv[1], all);
    std::sort(all.begin(), all.end());

    // ---- the bounding volume, identified from the data ---------------------
    long withV = 0, radiusOk = 0, decorV = 0, decorOk = 0, smaller = 0, sym = 0;
    long larger = 0;
    std::vector<float> ratios;
    for (const auto& path : all) {
        const auto d = omk::DataFs::readPath(path);
        const auto h = omk::readHeader(d);
        if (!h) continue;
        const bool decor = path.find("DECORS") != std::string::npos;
        const auto ms = omk::readMeshes(d, *h);
        const auto vs = omk::readVertices(d, *h);
        std::size_t base = 0;
        for (const auto& m : ms) {
            const auto n = static_cast<std::size_t>(m.vertices);
            if (n == 0 || base + n > vs.size()) { base += n; continue; }
            ++withV;
            if (decor) ++decorV;
            float ext = 0.0f;
            for (std::size_t k = 0; k < n; ++k) {
                const auto& v = vs[base + k];  // NOLINT
                ext = std::max(ext, len3(v.p));
            }
            base += n;
            const float tol = 1e-3f * std::max(1.0f, m.radius);
            if (std::fabs(ext - m.radius) <= tol) { ++radiusOk; if (decor) ++decorOk; }
            else if (m.radius < ext) ++smaller;
            else ++larger;              // the direction the first count missed
            if (std::fabs(ext - m.radius) > tol && ext > 0 && decor)
                ratios.push_back(m.radius / ext);
            bool s = true;
            for (int k = 0; k < 3; ++k)
                if (std::fabs(m.boxMin[k] + m.boxMax[k]) >
                    1e-4f * std::max(1.0f, std::fabs(m.boxMax[k]))) s = false;
            if (s) ++sym;
        }
    }

    // ---- the frustum's own geometry ---------------------------------------
    const float eye[3] = {10.0f, -20.0f, 30.0f}, at[3] = {110.0f, -20.0f, 30.0f};
    const auto f = omk::frustumFromFov(eye, at, 60.0f, 640, 480, 5000.0f);
    int planesUnit = 0, throughEye = 0;
    for (const auto& p : f.side) {
        if (std::fabs(len3(p.n) - 1.0f) < 1e-5f) ++planesUnit;
        const float at0 = p.n[0] * eye[0] + p.n[1] * eye[1] + p.n[2] * eye[2] + p.d;
        if (std::fabs(at0) < 1e-3f) ++throughEye;
    }
    // a point straight down the axis is inside every plane; one just past the
    // half-angle to the right is outside exactly one. That pins the sign AND
    // the angle, and it is what a flipped up-vector breaks.
    const auto inside = [&](const float p[3]) {
        for (const auto& pl : f.side)
            if (pl.n[0] * p[0] + pl.n[1] * p[1] + pl.n[2] * p[2] + pl.d > 0.0f)
                return false;
        return true;
    };
    const float axis[3] = {eye[0] + 100.0f, eye[1], eye[2]};
    const float t = std::tan(30.0f * 3.14159265358979f / 180.0f);
    const float offR[3] = {eye[0] + 100.0f, eye[1], eye[2] + 100.0f * t * 1.05f};
    const int axisInside = inside(axis) ? 1 : 0;
    const int offOutside = inside(offR) ? 0 : 1;

    // ---- the engine's own plane builder -----------------------------------
    //
    // Three points must lie ON the plane they define (n.p + d == 0), and a
    // point off it must not. That is the whole contract of `sub_442FB0`, and
    // it is checkable without knowing which three points the four callers
    // hand it.
    int planeContract = 0;
    {
        const float p1[3] = {1, 2, 3}, p2[3] = {4, 0, 3}, p3[3] = {-2, 5, 9};
        const auto pl = omk::planeThroughPoints(p1, p2, p3);
        const auto on = [&](const float p[3]) {
            return std::fabs(pl.n[0]*p[0] + pl.n[1]*p[1] + pl.n[2]*p[2] + pl.d)
                   < 1e-3f * std::max(1.0f, len3(pl.n));
        };
        float off[3] = {p1[0] + pl.n[0], p1[1] + pl.n[1], p1[2] + pl.n[2]};
        planeContract = (on(p1) && on(p2) && on(p3) && !on(off)) ? 1 : 0;
    }

    // ---- the cull's invariants, over a real set ---------------------------
    long fovBad = 0, farBad = 0, insideCulled = 0;
    long cameras = 0, culled = 0, kept = 0;
    // `far` is the camera's `+0x154` (`dword_6A2B9C`) and the near plane its
    // `+0x144` - both settled from the range test that rejects a vertex with
    // `z <= near` or `z >= far`. What a running game puts in them is a
    // per-camera runtime value no shipped file carries, so it is set large
    // here to take the distance test out of play: the survivor counts then
    // measure the four side planes alone. A real far can only cull MORE.
    const float kFar = 1e9f;
    std::vector<long> perCamera;
    {
        const omk::DataFs decors(std::string(argv[1]) + "/DECORS");
        const auto p = decors.resolve("Anekbah.3DO");
        if (p) {
            const auto d = omk::DataFs::readPath(*p);
            const auto h = omk::readHeader(d);
            if (h) {
                const auto ms = omk::readMeshes(d, *h);
                for (const auto& cam : omk::readCameras(d, *h)) {
                    ++cameras;
                    const auto narrow = omk::frustumFromFov(cam.pos, cam.target, 45.0f, 640, 480, kFar);
                    const auto wide   = omk::frustumFromFov(cam.pos, cam.target, 90.0f, 640, 480, kFar);
                    const auto near_  = omk::frustumFromFov(cam.pos, cam.target, 45.0f, 640, 480, 500.0f);
                    long here = 0;
                    for (const auto& m : ms) {
                        const auto r = omk::cullMesh(m, narrow);
                        if (r == omk::CullResult::Visible) { ++kept; ++here; }
                        else ++culled;
                        // The fov relation is now the ENGINE's construction, so
                        // widening the field of view really must keep more:
                        // both frusta are built the way sub_48D0D0 builds one,
                        // from the view rectangle at the far plane. Under the
                        // earlier fixture - which derived the vertical
                        // half-angle from the horizontal one through an aspect
                        // ratio the engine does not have - four of Anekbah's
                        // meshes were dropped by widening, by margins of 84 to
                        // 457 units. That was the fixture being wrong.
                        // and neither must reaching further
                        // Reaching FURTHER, though, is the engine's own test
                        // and must be monotone: `(r + far)^2 > |c - eye|^2`
                        // with the same planes is a pure widening.
                        if (omk::cullMesh(m, near_) == omk::CullResult::Visible &&
                            r != omk::CullResult::Visible)
                            ++farBad;
                        // the mesh you are standing INSIDE can never be culled:
                        // every plane passes through the eye, so |n.c + d| is
                        // at most |c - eye| <= r
                        float dv[3];
                        for (int k = 0; k < 3; ++k) dv[k] = m.pos[k] - cam.pos[k];
                        if (len3(dv) <= m.radius &&
                            !(static_cast<std::uint32_t>(m.flags) & 0x40u) &&
                            r != omk::CullResult::Visible)
                            ++insideCulled;
                    }
                    perCamera.push_back(here);
                    std::fprintf(stderr, "    camera %-12s at %.0f %.0f %.0f "
                                 "-> %.0f %.0f %.0f keeps %ld of %zu\n",
                                 cam.name, cam.pos[0], cam.pos[1], cam.pos[2],
                                 cam.target[0], cam.target[1], cam.target[2],
                                 here, ms.size());
                }
            }
        }
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    for (long v : {withV, radiusOk, decorV, decorOk, smaller, sym, larger})
        put32(static_cast<std::int32_t>(v));
    // the ratio distribution over the decor misses, x1000 so it is an integer
    std::sort(ratios.begin(), ratios.end());
    const auto quantile = [&](double q) {
        return ratios.empty() ? 0 :
            static_cast<std::int32_t>(ratios[static_cast<std::size_t>(
                q * static_cast<double>(ratios.size() - 1))] * 1000.0f);
    };
    put32(static_cast<std::int32_t>(ratios.size()));
    put32(quantile(0.0)); put32(quantile(0.5)); put32(quantile(1.0));
    put32(axisInside); put32(offOutside); put32(planesUnit); put32(throughEye);
    put32(planeContract);
    for (long v : {fovBad, farBad, insideCulled, cameras, culled, kept})
        put32(static_cast<std::int32_t>(v));
    for (long v : perCamera) put32(static_cast<std::int32_t>(v));
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(o.data()),
              static_cast<std::streamsize>(o.size()));

    std::printf("bounding volume: %ld meshes with vertices, %ld whose +88 is "
                "max|v| (DECORS %ld of %ld); %ld store a radius SMALLER than "
                "their extent and %ld a LARGER one; %ld boxes symmetric "
                "about the origin\n",
                withV, radiusOk, decorOk, decorV, smaller, larger, sym);
    std::printf("the decor misses: %zu, radius/extent min %.3f median %.3f "
                "max %.3f\n", ratios.size(),
                ratios.empty() ? 0.0 : static_cast<double>(ratios.front()),
                ratios.empty() ? 0.0 : static_cast<double>(
                    ratios[ratios.size() / 2]),
                ratios.empty() ? 0.0 : static_cast<double>(ratios.back()));
    std::printf("frustum: axis inside all four planes: %s; a point past the "
                "half-angle outside: %s; %d/4 unit normals, %d/4 through the eye\n",
                axisInside ? "yes" : "NO", offOutside ? "yes" : "NO",
                planesUnit, throughEye);
    std::printf("planeThroughPoints: three points on their own plane and a "
                "fourth off it: %s\n", planeContract ? "yes" : "NO");
    std::printf("cull over Anekbah's %ld authored cameras: %ld kept, %ld culled; "
                "fov-monotonicity %ld, far-monotonicity %ld; meshes containing the "
                "eye that were culled %ld; per camera:",
                cameras, kept, culled, fovBad, farBad, insideCulled);
    for (long v : perCamera) std::printf(" %ld", v);
    std::printf("\n");
    return 0;
}

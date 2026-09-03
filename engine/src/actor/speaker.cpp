// SPDX-License-Identifier: GPL-3.0-or-later
// Staging a conversation's speaker. See `speaker.h`.
#include "actor/speaker.h"

#include <cmath>

namespace omk {

std::vector<int> lineCameraIds(const Conversation& c) {
    std::vector<int> out;
    for (const auto& n : c.nodes)
        for (int id : {static_cast<int>(n.lineCam), static_cast<int>(n.lineCam2)}) {
            if (id < 0) continue;
            bool seen = false;
            for (int k : out) if (k == id) { seen = true; break; }
            if (!seen) out.push_back(id);
        }
    return out;
}

SpeakerStage stageSpeaker(const std::vector<DialogCamera>& cams,
                          const std::vector<int>& ids,
                          const TriangleSoup* walkable) {
    std::vector<CameraRay> rays;
    for (int id : ids)
        for (const auto& c : cams)
            if (c.id == id && c.absolute()) {
                CameraRay r;
                for (int k = 0; k < 3; ++k) { r.eye[k] = c.eye[k]; r.at[k] = c.at[k]; }
                rays.push_back(r);
                break;
            }
    return stageRays(rays, walkable);
}

SpeakerStage stageRays(const std::vector<CameraRay>& rays,
                       const TriangleSoup* walkable) {
    SpeakerStage s;
    // Least squares over rays p + t*d: minimise the summed squared distance,
    // which is the linear system  sum(I - d d^T) x = sum((I - d d^T) p).
    double A[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    double b[3] = {0, 0, 0};
    std::vector<const CameraRay*> used;
    for (const auto& ray : rays) {
        const CameraRay* c = &ray;
        double d[3] = {c->at[0] - c->eye[0], c->at[1] - c->eye[1],
                       c->at[2] - c->eye[2]};
        const double n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (n < 1e-6) continue;
        for (int k = 0; k < 3; ++k) d[k] /= n;
        const double p[3] = {c->eye[0], c->eye[1], c->eye[2]};
        for (int r = 0; r < 3; ++r) {
            for (int col = 0; col < 3; ++col) {
                const double m = (r == col ? 1.0 : 0.0) - d[r] * d[col];
                A[r][col] += m;
                b[r] += m * p[col];
            }
        }
        used.push_back(c);
    }
    s.rays = static_cast<int>(used.size());
    if (s.rays < 2) return s;

    // Solve the 3x3 by Gaussian elimination with partial pivoting.
    double M[3][4] = {{A[0][0], A[0][1], A[0][2], b[0]},
                      {A[1][0], A[1][1], A[1][2], b[1]},
                      {A[2][0], A[2][1], A[2][2], b[2]}};
    for (int i = 0; i < 3; ++i) {
        int piv = i;
        for (int r = i + 1; r < 3; ++r)
            if (std::fabs(M[r][i]) > std::fabs(M[piv][i])) piv = r;
        if (std::fabs(M[piv][i]) < 1e-9) return s;
        if (piv != i) for (int k = 0; k < 4; ++k) std::swap(M[i][k], M[piv][k]);
        for (int r = 0; r < 3; ++r) {
            if (r == i) continue;
            const double f = M[r][i] / M[i][i];
            for (int k = i; k < 4; ++k) M[r][k] -= f * M[i][k];
        }
    }
    const double x[3] = {M[0][3] / M[0][0], M[1][3] / M[1][1], M[2][3] / M[2][2]};
    for (int k = 0; k < 3; ++k) s.converge[k] = static_cast<float>(x[k]);

    // How far the rays actually miss it, so a caller can refuse a bad solve
    // instead of believing a number the geometry does not support.
    double sum = 0;
    for (const auto* c : used) {
        double d[3] = {c->at[0] - c->eye[0], c->at[1] - c->eye[1],
                       c->at[2] - c->eye[2]};
        const double n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        for (int k = 0; k < 3; ++k) d[k] /= n;
        const double v[3] = {x[0] - c->eye[0], x[1] - c->eye[1], x[2] - c->eye[2]};
        const double t = v[0] * d[0] + v[1] * d[1] + v[2] * d[2];
        double e = 0;
        for (int k = 0; k < 3; ++k) {
            const double q = v[k] - t * d[k];
            e += q * q;
        }
        sum += std::sqrt(e);
    }
    s.scatter = static_cast<float>(sum / used.size());

    for (int k = 0; k < 3; ++k) s.pos[k] = s.converge[k];
    if (walkable) {
        // The game's Y points DOWN, so "under" is a LARGER y. `floorUnder`
        // already carries that convention.
        const auto y = floorUnder(*walkable, s.converge[0], s.converge[1],
                                  s.converge[2]);
        if (y) { s.pos[1] = static_cast<float>(*y); s.onFloor = true; }
    }
    s.valid = true;
    return s;
}

}  // namespace omk

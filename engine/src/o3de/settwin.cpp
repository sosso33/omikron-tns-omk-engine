// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/settwin.h"

#include <cmath>

namespace omk {

namespace {
bool sameCorner(const float* p, const float* q, float apart) {
    return std::fabs(p[0] - q[0]) < apart && std::fabs(p[1] - q[1]) < apart &&
           std::fabs(p[2] - q[2]) < apart;
}

bool sameFace(const TwinFace& f, const TwinFace& g, float apart) {
    for (int r = 0; r < 3; ++r) {
        bool all = true;
        for (int k = 0; k < 3 && all; ++k)
            all = sameCorner(&f.xyz[static_cast<std::size_t>(3 * k)],
                             &g.xyz[static_cast<std::size_t>(3 * ((r + k) % 3))], apart);
        if (all) return true;
    }
    return false;
}
}  // namespace

TwinVerdict settleTwinFaces(std::span<const TwinFace> a, std::span<const TwinFace> b, float apart) {
    TwinVerdict v;
    v.loseA.assign(a.size(), 0);
    v.loseB.assign(b.size(), 0);
    std::vector<std::uint8_t> taken(a.size(), 0);
    for (std::size_t j = 0; j < b.size(); ++j) {
        if (!b[j].opaque) continue;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (taken[i] || !a[i].opaque || !sameFace(b[j], a[i], apart)) continue;
            taken[i] = 1;
            ++v.tied;
            if (b[j].order < a[i].order) v.loseA[i] = 1;
            else v.loseB[j] = 1;
            break;
        }
    }
    return v;
}

}  // namespace omk

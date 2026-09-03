// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/walk.h"

#include <algorithm>

namespace omk {

int decorUnder(std::span<const DecorSoup> decors, double x, double y, double z) {
    int best = -1;
    double bestY = 0.0;
    for (const auto& d : decors) {
        if (!d.soup || d.area < 0) continue;
        const auto g = floorUnder(*d.soup, x, y - kStepUp - 1.0, z);
        if (!g) continue;
        if (best < 0 || *g < bestY) { best = d.area; bestY = *g; }
    }
    return best;
}

StepResult Walker::step(double dx, double dz) {
    const double nx = pos_[0] + dx;
    const double nz = pos_[2] + dz;
    const double y  = pos_[1];

    const auto g = ground(nx, y, nz);
    if (!g) return StepResult::Reverted;         // nobody walks into the void

    const double rise = y - *g;                  // Y grows downward
    if (rise > kStepUp) return StepResult::Blocked;

    const double drop = *g - y;
    if (drop > kStepDown && !ignoreLedges) return StepResult::Refused;

    pos_[0] = nx; pos_[1] = *g; pos_[2] = nz;
    if (drop > kStepDown) {
        fall_ += drop;
        vy_ = std::min(kTerminal, vy_ + drop);
    } else {
        fall_ = 0.0;
        vy_ = 0.0;
    }
    return StepResult::Moved;
}

}  // namespace omk

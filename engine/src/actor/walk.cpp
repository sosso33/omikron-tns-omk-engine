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

// `Walk_GroundResponse`'s landing: the drop decides the tier, the tier decides
// the bank group and the state, and the accumulators are cleared (LABEL_83
// zeroes +1304, +280 and +284).
void Walker::land(double y) {
    pos_[1] = y;
    tier_ = fall_ < kSnapDrop  ? 1
          : fall_ < kFallShort ? 2
          : fall_ < kFallHurt  ? 3
          : 4;
    fall_ = 0.0;
    vy_ = vx_ = vz_ = 0.0;
    airborne_ = sliding_ = false;
}

StepResult Walker::step(double dx, double dz, double dt) {
    // Already off the ground: the vertical half owns this frame. `Actor_Move`
    // still applies the horizontal delta while an actor falls - a man who
    // steps off a kerb keeps going forward - so the request is carried, but
    // the ground does not get to answer it until he lands.
    if (airborne_ || sliding_) {
        if (tick(dt) != StepResult::Moved) {
            pos_[0] += dx;
            pos_[2] += dz;
            return sliding_ ? StepResult::Slid : StepResult::Fell;
        }
        // he landed this frame: fall through and let the ground answer the
        // step from where he now stands
    }

    const double nx = pos_[0] + dx;
    const double nz = pos_[2] + dz;
    const double y  = pos_[1];

    const auto g = ground(nx, y, nz);
    if (!g) {
        // No walkable floor there - but a face past the slope limit is not a
        // hole. `Walk_GroundResponse` puts the actor on it and slides him:
        // the face normal is added to the horizontal velocity and 11.811 is
        // written into the vertical one. A walker that cannot see steep faces
        // has nowhere to put him, which is why standing on one used to revert
        // in every direction at once.
        if (steep_) {
            if (const auto s = surfaceUnder(*steep_, nx, y - kStepUp - 1.0, nz)) {
                pos_[0] = nx; pos_[1] = s->y; pos_[2] = nz;
                vx_ += s->n[0];
                vz_ += s->n[2];
                vy_ = kSlideSpeed;
                sliding_ = true;
                airborne_ = false;
                return StepResult::Slid;
            }
        }
        return StepResult::Reverted;             // nobody walks into the void
    }

    const double rise = y - *g;                  // Y grows downward
    if (rise > kStepUp) return StepResult::Blocked;

    const double drop = *g - y;
    // The stand-in for the unported swept sphere - see kMaxUnsweptDrop.
    if (drop > kMaxUnsweptDrop && !ignoreLedges) return StepResult::Refused;

    pos_[0] = nx; pos_[2] = nz;
    if (drop <= kSnapDrop || ignoreLedges) {
        // absorbed in the frame, the way a kerb or a stair nosing is
        land(*g);
        return StepResult::Moved;
    }
    // Off the edge. The horizontal move stands and the actor leaves the
    // ground; `tick` carries him down and lands him.
    airborne_ = true;
    sliding_  = false;
    return StepResult::Fell;
}

StepResult Walker::tick(double dt) {
    if (!airborne_ && !sliding_) return StepResult::Moved;

    // Actor_ApplyMotion: the vertical speed accelerates and is clamped, and
    // the frame's descent is that speed over 30. A slide does not accelerate -
    // the ground response WRITES the speed every frame it is on a steep face -
    // so only a free fall integrates.
    if (airborne_) vy_ = std::min(kTerminal, vy_ + kGravity * dt);
    const double dy = vy_ * (1.0 / 30.0) * dt;

    if (sliding_) {
        // the horizontal half of the slide: velocity is per FRAME here, where
        // the vertical is per second over 30 (`dx = +216 * dt` against
        // `dy = +220 * 0.0333 * dt` in Actor_ApplyMotion)
        pos_[0] += vx_ * dt;
        pos_[2] += vz_ * dt;
    }

    const double ny = pos_[1] + dy;              // Y grows downward
    const auto g = ground(pos_[0], pos_[1], pos_[2]);
    if (!g) {
        // nothing under him at all: hold the horizontal, keep descending. The
        // caller's own out-of-world handling owns this case.
        pos_[1] = ny;
        fall_ += dy;
        return airborne_ ? StepResult::Fell : StepResult::Slid;
    }
    if (ny >= *g) {
        fall_ += *g - pos_[1];
        // A steep landing is not a landing: the engine re-writes the slide
        // speed and keeps him moving down the face.
        if (steep_) {
            if (const auto s = surfaceUnder(*steep_, pos_[0], pos_[1] - 1.0, pos_[2])) {
                if (std::fabs(s->y - *g) < 0.01) {
                    pos_[1] = *g;
                    vx_ += s->n[0];
                    vz_ += s->n[2];
                    vy_ = kSlideSpeed;
                    sliding_ = true;
                    airborne_ = false;
                    return StepResult::Slid;
                }
            }
        }
        land(*g);
        return StepResult::Moved;
    }
    pos_[1] = ny;
    fall_ += dy;
    return airborne_ ? StepResult::Fell : StepResult::Slid;
}

}  // namespace omk

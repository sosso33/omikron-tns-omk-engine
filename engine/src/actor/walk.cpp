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

    // THE NARROW PHASE FIRST. `Actor_Move` sweeps the sphere along the move
    // and stops at the first wall before the ground is ever probed; a move
    // the sweep cancels outright is a block, and what it leaves is what the
    // probe then judges. Only the horizontal move is collided here - the
    // vertical is the ground probe's job, as in the engine's split.
    slide(dx, dz);
    if (dx == 0.0 && dz == 0.0 && slides_ > 0) return StepResult::Blocked;

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

void Walker::slide(double& dx, double& dz) {
    slides_ = 0;
    if (!blockers_ || radius_ <= 0.0) return;
    // the sphere sits one radius above the feet; y grows downward
    double p[3] = {pos_[0], pos_[1] - radius_, pos_[2]};
    for (int pass = 0; pass < 3; ++pass) {
        if (dx == 0.0 && dz == 0.0) return;
        const double d[3] = {dx, 0.0, dz};
        const auto hit = sweepSphere(*blockers_, p, d, radius_);
        if (!hit) return;
        ++slides_;
        // advance to the contact, then slide the remainder along the plane
        p[0] += dx * hit->t; p[2] += dz * hit->t;
        const double rem[3] = {dx * (1.0 - hit->t), 0.0, dz * (1.0 - hit->t)};
        // `Walk_ClampNormal`'s mask is the ACTOR's accumulated blocked-direction
        // state (+0x160 of the request, 0xC000C for a walking player: the y
        // bits, so a wall pushes horizontally). Not modelled here as the sim
        // does not model it either: mask 0 leaves the normal as the face gave
        // it, and the slide is the projection. Synthesising a mask per contact
        // is what made one wall cancel a whole move in the sim.
        double cn[3];
        if (!clampNormal(0u, false, hit->n, cn)) { dx = dz = 0.0; return; }
        const double drop = cn[0] * rem[0] + cn[1] * rem[1] + cn[2] * rem[2];
        dx = rem[0] - drop * cn[0];
        dz = rem[2] - drop * cn[2];
        // `Actor_Move`: a remaining length `<= 0.000099999997` is zero - the
        // engine's own threshold, and what keeps a head-on contact from
        // leaving a 1e-16 residual that reads as a move.
        if (std::sqrt(dx * dx + dz * dz) <= 0.000099999997) { dx = dz = 0.0; return; }
    }
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

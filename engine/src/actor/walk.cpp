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
            // A FALL IS VERTICAL. `Actor_ApplyMotion` moves the actor by
            // +216/+224 x dt, his own horizontal velocity fields, and in the
            // fall state nothing writes them: the walk's root motion reaches
            // the position through the channel's clip, and the fall state's
            // clip is a held pose. A reader watching the original (issue 68):
            // "falling mainly on a single axis (just Y)". This port carried
            // the walk's delta through the air - "continuing walking in the
            // air" - and on 2026-09-05 that walked a reader diagonally off
            // the restaurant's sunken ledge into a hole in the geometry and
            // down for ever. A SLIDE keeps its own +216/+224, which `tick`
            // applies.
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
    double push[2] = {0.0, 0.0};
    slide(dx, dz, push);
    // A hit that leaves nothing but the push-out's jitter - hundredths of a
    // unit against the wall, `Actor_Move`'s stand-off re-established every
    // frame - is a BLOCK for the caller, not a move. The engine draws no such
    // line (it returns "hit" and applies the fraction); this verdict is the
    // port's, for the channel and the checks.
    if (slides_ > 0 && std::sqrt(dx * dx + dz * dz) < 0.05) return StepResult::Blocked;

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

std::optional<SweepHit> Walker::bodyHit(const double p[3], const double d[3]) const {
    std::optional<SweepHit> best;
    if (centres_.empty()) {
        const double c[3] = {p[0], p[1] - radius_, p[2]};      // one sphere on the feet
        return sweepSphere(*blockers_, c, d, radius_);
    }
    for (const auto& off : centres_) {
        const double c[3] = {p[0] + off[0], p[1] + off[1], p[2] + off[2]};
        const auto h = sweepSphere(*blockers_, c, d, radius_);
        if (h && (!best || h->t < best->t)) best = h;
    }
    return best;
}

void Walker::slide(double& dx, double& dz, double push[2]) {
    slides_ = 0;
    push[0] = push[1] = 0.0;
    if (!blockers_ || radius_ <= 0.0) return;
    // `p` is the FEET; the body's spheres hang off it. `total` is what the
    // actor has been moved by so far across the passes - the engine adds
    // `a4 * dir + a3 * normal` to the position each pass and re-sweeps from
    // there - and what is returned is that plus the last remainder.
    double p[3] = {pos_[0], pos_[1], pos_[2]};
    double total[2] = {0.0, 0.0};
    for (int pass = 0; pass < 3; ++pass) {
        const double len = std::sqrt(dx * dx + dz * dz);
        // `Actor_Move`: a remaining length `<= 0.000099999997` is zero
        if (len <= 0.000099999997) { dx = total[0]; dz = total[1]; return; }
        const double d[3] = {dx, 0.0, dz};
        const auto hit = bodyHit(p, d);
        if (!hit) { dx = total[0] + dx; dz = total[1] + dz; return; }
        ++slides_;
        // `Walk_ClampNormal(0xC000C)`: the walking player's mask is the y bits,
        // so a wall's normal is made HORIZONTAL and renormalised. A face too
        // flat to be a wall keeps its y in the engine (the cos 30 test adds the
        // bits only for walls); the faces swept here are the steep ones, so
        // every normal is a wall's.
        double cn[3];
        if (!clampNormal(0xC000Cu, false, hit->n, cn)) { dx = dz = 0.0; return; }
        const double ux = dx / len, uz = dz / len;
        const double dist = hit->t * len;              // flt_6A5188: units, not a fraction
        double moved = 0.0;
        if (dist >= 1.0) {
            moved = dist - 1.0;                        // ONE UNIT SHORT of the contact
        } else {
            // already touching: no forward move; PUSH OUT along the normal by
            // 1 - dist, re-sweeping the same move from the pushed start and
            // growing the push 1.1x until the contact is a unit away
            // (`Actor_Move`'s loop over `sub_4AD6F0`).
            double pushLen = 1.0 - dist;
            for (int k = 0; k < 12; ++k) {
                const double q[3] = {p[0] + pushLen * cn[0], p[1], p[2] + pushLen * cn[2]};
                const auto h2 = bodyHit(q, d);
                if (!h2 || h2->t * len >= 1.0) break;
                pushLen *= 1.1;
            }
            p[0] += pushLen * cn[0]; p[2] += pushLen * cn[2];
            total[0] += pushLen * cn[0]; total[1] += pushLen * cn[2];
            push[0] += pushLen * cn[0]; push[1] += pushLen * cn[2];
        }
        p[0] += ux * moved; p[2] += uz * moved;
        total[0] += ux * moved; total[1] += uz * moved;
        // the remainder, projected along the wall (`-(n . dir)` clamped >= 0)
        const double remLen = std::max(0.0, len - moved);
        const double along = std::max(0.0, -(cn[0] * ux + cn[2] * uz));
        dx = remLen * (ux + along * cn[0]);
        dz = remLen * (uz + along * cn[2]);
    }
    // three passes spent: the last remainder is applied unswept, as the engine's is
    dx = total[0] + dx; dz = total[1] + dz;
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

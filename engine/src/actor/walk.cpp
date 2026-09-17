// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/walk.h"

#include <algorithm>
#include <cstring>

namespace omk {

namespace {
bool gVerifyOn = false;
GroundVerify gVerify;

inline bool sameAnswer(const std::optional<double>& a, const std::optional<double>& b) {
    return a.has_value() == b.has_value() && (!a || std::memcmp(&*a, &*b, sizeof(double)) == 0);
}
}  // namespace

void setGroundVerify(bool on) { gVerifyOn = on; }
const GroundVerify& groundVerify() { return gVerify; }

std::optional<SweepHit> sweepThrough(const TriangleSoup& tris, const SplitSoupGrid* grid,
                                     const double p0[3], const double d[3], double radius) {
    if (!grid) return sweepSphere(tris, p0, d, radius);
    const auto h = sweepSphere(tris, *grid, p0, d, radius);
    if (gVerifyOn) {
        ++gVerify.sweep;
        const auto l = sweepSphere(tris, p0, d, radius);
        if (h.has_value() != l.has_value() || (h && std::memcmp(&*h, &*l, sizeof(SweepHit)) != 0))
            ++gVerify.sweepBad;
    }
    return h;
}

std::optional<double> Walker::ground(double x, double y, double z) const {
    if (!grid_) return floorUnder(soup_, x, y - kStepUp - 1.0, z);
    const auto g = floorUnder(soup_, *grid_, x, y - kStepUp - 1.0, z);
    if (gVerifyOn) {
        ++gVerify.walker;
        if (!sameAnswer(g, floorUnder(soup_, x, y - kStepUp - 1.0, z))) ++gVerify.walkerBad;
    }
    return g;
}

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

int decorUnder(std::span<const DecorSoup> decors, const TriangleSoup& merged,
               const SplitSoupGrid& grid, double x, double y, double z) {
    std::size_t total = 0;
    bool usable = true;
    for (const auto& d : decors) {
        if (!d.soup || d.area < 0) { usable = false; break; }
        total += d.soup->size();
    }
    if (!usable || total != merged.size()) return decorUnder(decors, x, y, z);
    std::uint32_t tri = 0;
    int best = -1;
    if (floorUnder(merged, grid, x, y - kStepUp - 1.0, z, tri)) {
        std::size_t off = 0;
        for (const auto& d : decors) {
            off += d.soup->size();
            if (9 * static_cast<std::size_t>(tri) < off) { best = d.area; break; }
        }
    }
    if (gVerifyOn) {
        ++gVerify.decor;
        if (best != decorUnder(decors, x, y, z)) ++gVerify.decorBad;
    }
    return best;
}

// `Walk_GroundResponse`'s landing: the drop decides the tier, the tier decides
// the bank group and the state, and the accumulators are cleared (LABEL_83
// zeroes +1304, +280 and +284).
std::optional<double> Walker::probeFlags(double x, double from, double z,
                                         std::uint32_t& flags) const {
    flags = 0;
    if (!grid_) return floorUnder(soup_, x, from, z);
    std::uint32_t tri = 0;
    const auto h = floorUnder(soup_, *grid_, x, from, z, tri);
    if (h && floorFlags_ && floorFlags_->size() * 9 == soup_.size() && tri < floorFlags_->size())
        flags = (*floorFlags_)[tri];
    return h;
}

std::optional<double> Walker::surfaceAbove(double x, double y, double z) const {
    // Walk the hits downward from well above him and keep the last water
    // surface that is still above his y: the ray up, from the other end.
    std::optional<double> best;
    double from = y - 2000.0;
    for (int guard = 0; guard < 32; ++guard) {
        std::uint32_t fl = 0;
        const auto h = probeFlags(x, from, z, fl);
        if (!h || *h >= y) break;
        if (fl & 0x20000000u) best = h;
        from = *h + 0.01;
    }
    return best;
}

std::optional<double> Walker::floorThroughWater(double x, double from, double z) const {
    for (int guard = 0; guard < 8; ++guard) {
        std::uint32_t fl = 0;
        const auto h = probeFlags(x, from, z, fl);
        if (!h) return h;
        if (!(fl & 0x20000000u)) return h;
        from = *h + 0.01;
    }
    return std::nullopt;
}

void Walker::land(double y) {
    pos_[1] = y;
    // THE LANDING'S RECORD IS KEPT FROM A LANDING. `step` also calls this for
    // every ordinary snap onto the floor, and until 2026-09-17 each of those
    // rewrote the record with a fall of 0 - so a landing whose frame also ran
    // a walking step (a slide off the catacombs' ramp into a 5 m fall) read
    // back tier 1 and fall 0, and the viewer posted no message and chose no
    // reaction (`todo/falls.md`). A grounded snap still clears the
    // accumulators below, as LABEL_83 does.
    if (airborne_ || sliding_) {
        tier_ = fall_ < kSnapDrop  ? 1
              : fall_ < kFallShort ? 2
              : fall_ < kFallHurt  ? 3
              : 4;
        drop_ = y - apex_;       // the descent from the apex, for MDJUMP03
        if (drop_ < 0.0) drop_ = 0.0;
        landFall_ = fall_;       // `+280`, read before LABEL_83 clears it
    }
    fall_ = 0.0;
    vy_ = vx_ = vz_ = 0.0;
    airborne_ = sliding_ = false;
    jumping_ = false;            // MDJUMP03: dword_6A52CC = 0
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

    auto g = ground(nx, y, nz);
    // ...and WHICH triangle, for its mesh's flags: the grid probe names it
    std::uint32_t floorTri = 0;
    bool floorKnown = false;
    if (g && floorFlags_ && grid_ && floorFlags_->size() * 9 == soup_.size())
        floorKnown = floorUnder(soup_, *grid_, nx, y - kStepUp - 1.0, nz, floorTri).has_value() &&
                     floorTri < floorFlags_->size();
    std::uint32_t floorFl = floorKnown ? (*floorFlags_)[floorTri] : 0u;
    // A WATER SURFACE IS NOT A FLOOR, AND NOT A WALL EITHER (corrected
    // 2026-09-17). This was first ported as "a mesh flagged 0x20000000 refuses
    // the step whatever its height", from `21_d3d.c` 2644's
    // `|| (**mesh & 0x20000000)` - but that arm runs only for ground-probe hit
    // kind 2 and answers with a VELOCITY, an eighth of the offset from the probe
    // point, not with a refusal. As a hard block it fenced every quay: a reader
    // could get into the water by JUMPING - airborne, where the fall already
    // passes through the surface - and in many places not by walking. So the
    // step now looks THROUGH the water, as the fall does: the floor is whatever
    // lies under it, and if that is a long way down he steps off the edge and
    // falls in. LABELLED: the engine's nudge itself is not ported, and the
    // floor's own flags are not re-read here (the viewer's water entry probes
    // them itself).
    if (g && (floorFl & 0x20000000u)) {
        g = floorThroughWater(nx, y - kStepUp - 1.0, nz);
        floorFl = 0u;
    }
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
    standFlags_ = floorFl;
    if (drop <= kSnapDrop || ignoreLedges) {
        // absorbed in the frame, the way a kerb or a stair nosing is
        land(*g);
        return StepResult::Moved;
    }
    // Off the edge. The horizontal move stands and the actor leaves the
    // ground; `tick` carries him down and lands him.
    apex_ = pos_[1];             // a plain fall's apex is where he stepped off
    airborne_ = true;
    sliding_  = false;
    return StepResult::Fell;
}

std::optional<SweepHit> Walker::bodyHit(const double p[3], const double d[3]) const {
    std::optional<SweepHit> best;
    if (centres_.empty()) {
        const double c[3] = {p[0], p[1] - radius_, p[2]};      // one sphere on the feet
        return sweepThrough(*blockers_, blockerGrid_, c, d, radius_);
    }
    for (const auto& off : centres_) {
        const double c[3] = {p[0] + off[0], p[1] + off[1], p[2] + off[2]};
        const auto h = sweepThrough(*blockers_, blockerGrid_, c, d, radius_);
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
    // THE SWEEP STARTS A STEP ABOVE THE FEET, and without that the actor
    // cannot climb a stair at all.
    //
    // The model's own spheres hang off the feet, and Kay'l's lowest has its
    // BOTTOM exactly there (centre 30.90, radius 10.91, and the pelvis-to-feet
    // distance is 41.8). So every riser in the game is inside that sphere, and
    // the sweep stops him one unit short of it - a sphere-radius from the step
    // he is trying to climb, where the ground probe can never reach the tread
    // above. Measured on Anekbah's bank entrance (`todo/next-tasks.md` 20):
    // ten steps, nine of 10.25 units that squeak through and a last one of
    // 10.8 that does not, leaving him stuck at x 4605 with the door 10 units
    // away - which is exactly the reader's report.
    //
    // Starting the sweep a step-height up makes the two halves agree:
    // `Walk_ProbeGround` already casts from `feet - kStepUp - 1`, so anything
    // inside that window is something the actor CLIMBS, and it cannot also be
    // something he collides with. A wall taller than the step limit still
    // blocks - the spheres above the raise still meet it - and
    // `verify.py: engine: narrow phase` still stops him 13.0 in front of one.
    //
    // RECONSTRUCTION, and labelled as one: what the engine does here is not
    // read. `Sweep_ActorMove` (0x004AD360) and the 930-line
    // `Sweep_PolygonKernel` were deliberately not transcribed, so the sweep's
    // own start height is unknown; what IS known is that the game climbs its
    // own stairs, that its step limit is 30 cm, and that a sweep anchored at
    // the feet cannot do both. Reading `Actor_Move`'s order - whether the
    // step-up runs before the sweep - would settle it.
    double p[3] = {pos_[0], pos_[1] - kStepUp, pos_[2]};
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
    if (pos_[1] < apex_) apex_ = pos_[1];        // y grows down: smaller is higher
    const double dy = vy_ * (1.0 / 30.0) * dt;

    // THE HORIZONTAL FOLLOWS THE VELOCITY, NOT THE FLAG. Velocity is per
    // FRAME here where the vertical is per second over 30 (`dx = +216 * dt`
    // against `dy = +220 * 0.0333 * dt` in Actor_ApplyMotion), and
    // Actor_ApplyMotion applies +216/+224 unconditionally - it does not ask
    // whether the actor is sliding or falling.
    //
    // This used to be gated on `sliding_`, and that was right for the only two
    // cases the port then had: a SLIDE writes +216/+224 every frame from the
    // ground response, and a FALL leaves them zero, which is the reader's
    // "falling mainly on a single axis (just Y)" and the reason the walk's
    // delta must not be carried through the air. A JUMP is the third case and
    // it breaks the gate: `MDJUMP01` WRITES +216/+224 at take-off, so the leap
    // travels while airborne. Driving the horizontal off the velocity covers
    // all three without a flag - a fall still moves on Y alone because its
    // velocity really is zero.
    //
    // Swept, not added: `Actor_ApplyMotion` hands the horizontal to
    // `Actor_Move` with Y zero, so a jump into a wall is stopped by the same
    // collide-and-slide a walk is.
    if (vx_ != 0.0 || vz_ != 0.0) {
        double jx = vx_ * dt, jz = vz_ * dt, push[2] = {0.0, 0.0};
        slide(jx, jz, push);
        pos_[0] += jx;
        pos_[2] += jz;
    }

    const double ny = pos_[1] + dy;              // Y grows downward
    auto g = ground(pos_[0], pos_[1], pos_[2]);
    // A FALLING BODY PASSES THROUGH A 0x20000000 MESH. `Walk_GroundResponse`'s
    // descent lands him only `if (v68 <= 0.0 && (mesh & 0x20000000) == 0)`
    // (`21_d3d.c` 2548), so a water SURFACE is not somewhere to land: he goes
    // through it to what is under it - the canal's bed, whose 0x8000000 is what
    // takes him into the water (`todo/swimming.md`). Without this a jump or a
    // drop off a quay stood him on the water.
    if (g && floorFlags_ && grid_ && floorFlags_->size() * 9 == soup_.size()) {
        double from = pos_[1] - kStepUp - 1.0;
        g.reset();
        for (int guard = 0; guard < 8; ++guard) {
            std::uint32_t tri = 0;
            const auto h = floorUnder(soup_, *grid_, pos_[0], from, pos_[2], tri);
            if (!h) break;
            if (tri < floorFlags_->size() && ((*floorFlags_)[tri] & 0x20000000u)) {
                from = *h + 0.01;                // under the surface, and again
                continue;
            }
            g = h;
            break;
        }
    }
    // ...AND THE STEEP FACE HE IS ON, because `ground()` reads the WALKABLE
    // soup ALONE. That split is this port's own: the engine casts ONE probe
    // at the whole collision set and `Walk_GroundResponse` then asks
    // `cos(30) > -normal.y` of whatever it hit, so a ramp past the limit is
    // ground it stands the actor on and slides him down - never a hole.
    //
    // Splitting the soup in two and asking only the walkable half means every
    // frame of a slide answers "nothing under him", and `fc34909` - which
    // ends a slide when the ground runs out, and was right for its own case -
    // then turned the slide into a free fall one frame in. Measured in the
    // catacombs: HApyramb01's flank rises 157 units over 118 (53 degrees, in
    // the steep soup), and the walker sank THROUGH it - on the face at y 914
    // at frame 80, 41 units under it by frame 90 and 3900 under it by 270.
    // That is a reader's *"then I just went through the ground"*.
    //
    // Probed from a step-height above the feet, exactly as `ground` and as
    // `step`'s own steep arm are: `surfaceUnder` wants a surface strictly
    // below its origin, so casting from the feet of an actor standing on the
    // face finds the face he is standing on not at all.
    //
    // ...and ONLY WHILE HE IS ALREADY SLIDING. The steep soup is "every
    // collision face past 30 degrees", which is the WALLS as well as the
    // ramps, and a downward ray can rest on any wall that is not exactly
    // vertical. The engine cannot: its ground probe is a swept SPHERE and
    // `Actor_Move` has already stopped the body at the wall horizontally.
    // Consulting it for a falling actor too costs 81 of Aapkayl's 663 ledge
    // spots, which stop resolving inside ten seconds because they latch onto
    // a wall and slide off the model; keeping a slide on its ramp needs none
    // of that. LABELLED as the narrowing it is.
    const auto sf = (steep_ && sliding_)
                        ? surfaceUnder(*steep_, pos_[0], pos_[1] - kStepUp - 1.0, pos_[2])
                        : std::optional<GroundHit>{};
    // A SLIDE LASTS EXACTLY AS LONG AS THE FACE DOES, and that - not "the
    // ground ran out" - is the engine's rule. `Walk_GroundResponse`'s slide
    // arm (21_d3d.c 2587) writes `+220 = dword_910340` only on the frames its
    // probe hits a face whose `-normal.y` is under `cos(30)`; a frame with no
    // such face writes nothing, so `Actor_ApplyMotion`'s gravity is all that
    // is left and he is falling. `fc34909` tested `!g` instead, which is the
    // same thing ONLY where there is no walkable floor anywhere below - and
    // in the catacombs there is one 117 units down, so leaving the ramp he
    // went on gliding at a dead-constant 11.8 a frame across the whole room.
    if (sliding_ && !(sf && sf->y - pos_[1] <= kSnapDrop)) {
        sliding_ = false;
        airborne_ = true;
        apex_ = pos_[1];            // the descent is measured from here
    }
    if (!g && !sf) {
        // NOTHING UNDER HIM AT ALL - and a SLIDE ends here, which it did not
        // until 2026-09-12. The slide speed is not a velocity he carries: the
        // ground response WRITES `+220 = dword_910340` every frame it finds a
        // face past the slope limit under him (see `kSlideSpeed` in the
        // header). With no face there is nothing to write it, so
        // `Actor_ApplyMotion`'s gravity is all that is left and he is falling.
        //
        // Left sliding instead, he glided at a dead-constant 11.8 a frame -
        // 0.4 units, 0.3 m/s - for as long as the air lasted, because `tick`
        // only integrates gravity `if (airborne_)`. A reader walking the
        // catacombs: *"the character fall very slowly, in an not natural
        // way"*, from "the ground then a small slope then in the air" - the
        // small slope is what latched the slide and the air never cleared it.
        //
        // His horizontal keeps going, as it should: the face normal went into
        // `vx_`/`vz_` when the slide started and `Actor_ApplyMotion` applies
        // those unconditionally, so leaving a ramp throws him off it.
        pos_[1] = ny;
        fall_ += dy;
        return airborne_ ? StepResult::Fell : StepResult::Slid;
    }
    // Whichever of the two answered HIGHER is the one he meets first - and
    // with Y growing downward, higher is the smaller number.
    const bool onSteep = sf && (!g || sf->y < *g);
    const double surf  = onSteep ? sf->y : *g;
    // ...OR THE SURFACE IS INSIDE THE FRAME'S ABSORBED WINDOW. The engine's
    // ground response runs on EVERY frame of a grounded actor, and its BELOW
    // branch absorbs a drop under 7.874 units outright (`v68 < 7.8740158`,
    // `kSnapDrop`) - it does not wait for him to pass through the face. On a
    // ramp that matters: HApyramb01's flank falls 1.33 units for every unit
    // of x, so a slider crossing it at 0.8 a frame sees the surface drop 1.06
    // while the slide speed carries him only 0.39, and a landing test that
    // asks `ny >= surf` alone never fires - he descends beside the face for
    // ever instead of on it. Absorbing is what keeps him ON the ramp.
    // ...and the absorb is gated by the JUMP FLAG, `if (!dword_6A52CC)`
    // (21_d3d.c 2729), which is the whole above-the-surface arm. It was gated
    // on `vy_ >= 0` until 2026-09-14, which was a misreading: `+220 >= 0` gates
    // the OTHER arm, the one for a body that has passed through the surface
    // (`v68 <= 0`, 21_d3d.c 2587). A velocity gate keeps a RISING leap up but
    // snaps a DESCENDING one onto the floor as soon as it is within 7.874 -
    // and the jump's apex is only 9 units up, so it landed after 9 airborne
    // frames of its 13 (`engine: player jump`, red since `d589d30`). The flag
    // holds for the whole flight, as `MDJUMP0A` sets it and only the landing
    // clears it. NOT ported from the same arm: its `+1304` fall-tier test and
    // `sub_47CF00()`.
    if (ny >= surf || (!jumping_ && surf - ny <= kSnapDrop)) {
        fall_ += surf - pos_[1];
        // A steep landing is not a landing: the engine re-writes the slide
        // speed and keeps him moving down the face.
        if (onSteep || (sf && std::fabs(sf->y - surf) < 0.01)) {
            pos_[1] = surf;
            vx_ += sf->n[0];
            vz_ += sf->n[2];
            vy_ = kSlideSpeed;
            sliding_ = true;
            airborne_ = false;
            return StepResult::Slid;
        }
        land(surf);
        return StepResult::Moved;
    }
    pos_[1] = ny;
    fall_ += dy;
    return airborne_ ? StepResult::Fell : StepResult::Slid;
}

}  // namespace omk

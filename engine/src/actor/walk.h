// SPDX-License-Identifier: GPL-3.0-or-later
// The walker - one frame of Actor_Move + Walk_GroundResponse.
//
// The order is the engine's: try the move, probe under the result, and let the
// ground decide.
//
// ---------------------------------------------------------------------------
// A DROP IS A FALL, NOT A REFUSAL (2026-09-04)
//
// This file used to answer a drop past the step limit with `Refused` and leave
// the actor where he was, on the reading that "ledges are obeyed". That is not
// what the engine does, and it is why a player who got onto a bench could
// never get off one: measured over three shipped sets, the old walker could
// not take a single downward step anywhere (`engine/tools/stuck_probe.cpp`:
// 0 descents from 671 / 36 / 342 spots standing beside a drop in Aapkayl,
// AImpasse and Anekbah).
//
// `Walk_GroundResponse` (0x00465460) branches on whether the ground is above
// the feet after the frame's gravity or below them:
//
//   * ABOVE or level - the grounded branch. It snaps the actor up onto it, and
//     this is the only branch carrying a ledge or slope refusal. Both are
//     gated (`(fallByte == 0 || 2) && actor+256 + gap < 0`) on quantities the
//     port does not compute, and a refusal there does not stop the actor: it
//     restores the last safe position and re-issues the SAME delta through
//     `Actor_Move` in mode 4, the collide-and-slide.
//   * BELOW - the airborne branch, and this is the one that matters here. A
//     drop under 7.874 units is absorbed in the frame; anything more sets the
//     fall byte at actor+1304 and enters ACTOR_STATE 18, tiered by the drop at
//     59.055 (`dword_910350`), 118.11 and 196.85. There is no refusal in this
//     branch at all.
//
// So the port now falls. What it still does NOT have is the reason the engine
// rarely needs to: `Actor_Move`'s swept sphere (`Sweep_ActorMove` 0x004AD360 ->
// `Sweep_PolygonKernel` 0x004A9D30, 930 lines) stops the player at a wall long
// before he reaches a balcony edge. Until that is ported, `kMaxUnsweptDrop`
// below keeps the old refusal for drops past the engine's own no-damage tier -
// a LABELLED stand-in for the missing wall, not a rule of the game.
// ---------------------------------------------------------------------------
#pragma once

#include "o3de/collision.h"

#include <span>
#include <string>
#include <vector>

namespace omk {

// The engine's own tunables, in world units (1 unit ~ 2.54 cm). Every one is
// a constant the binary states about itself, at the address named.
inline constexpr double kStepUp   = 11.811023622;  // 30 cm: dword_910340
inline constexpr double kStepDown = 11.811023622;  // the same limit, down
inline constexpr double kTerminal = 787.40155;     // 20 m/s: Actor_ApplyMotion
inline constexpr double kFallHurt = 118.11024;     // 3 m
inline constexpr double kFallKill = 196.85039;     // 5 m

// The drop the ground response absorbs in the frame it happens - a kerb, a
// stair nosing. Anything past it leaves the ground (`Walk_GroundResponse`'s
// `v68 < 7.8740158` snap).
inline constexpr double kSnapDrop = 7.8740158;     // 20 cm

// The lowest tier that changes the actor's state at all: below it the fall
// byte at +1304 is set to 2 and no bank group is asked for; at or above it the
// actor enters ACTOR_STATE 18. `dword_910350`.
inline constexpr double kFallShort = 59.055118;    // 150 cm

// Gravity: added to the actor's vertical speed (+220) every frame and clamped
// at kTerminal, with the frame's descent being `speed * (1/30) * dt`. Written
// into actor+228 by `Actor_LoadModel` (0x0041A730,
// `mov dword ptr [ebx+0E4h], 414DC637h`), so every actor gets it. It is 9.8
// m/s^2 in the engine's own unit - an inch - at 30 Hz.
inline constexpr double kGravity = 12.860892;

// The vertical speed the engine WRITES (not adds) while the actor stands on a
// face steeper than the slope limit, alongside adding the face normal to his
// horizontal velocity. Numerically the same float as the step limit, and from
// the same global - `f32(a1, 220) = dword_910340`.
inline constexpr double kSlideSpeed = 11.811023622;

// **RECONSTRUCTION, and the only number here that is not the engine's.**
//
// The engine stops the player at a wall with a swept sphere (`Actor_Move` ->
// `Sweep_ActorMove` 0x004AD360), which is not ported. Without it the walker's
// only reason to stay out of a stairwell or off a balcony is the ledge rule
// this file used to apply to every drop - so removing that rule outright does
// not restore the engine's behaviour, it removes a wall the port was leaning
// on. Until the sweep is ported the refusal is kept for drops past the
// engine's own no-damage tier: below 3 m the engine falls and the actor is
// unhurt, so a bench, a kerb, a crate and a flight of stairs all descend,
// while a drop deep enough that the engine would have stopped him at a
// railing still refuses. This bound is this port's, not the game's, and it
// goes away when the sweep arrives.
// 2026-09-05: THE CAPSULE HAS LANDED (`setBlockers` with the model's sphere
// list, `slide` with the engine's stand-off and push-out), so a railing or a
// bench edge stops the body the way `Actor_Move` stops it, and the guard this
// bound was is RETIRED: `Walk_GroundResponse` has no refusal in its
// below-the-feet branch, a drop is a fall, and this now says so. Kept as a
// name so the callers that quote it still compile; it no longer refuses.
inline constexpr double kMaxUnsweptDrop = 1.0e9;

enum class StepResult {
    Moved,      // the step took, and the actor snapped to the new floor
    Reverted,   // no floor under the destination - nobody walks into the void
    Blocked,    // a rise past the step limit: a wall, not a stair
    Fell,       // the step took and left the ground: the actor is airborne
    Slid,       // the step landed on a face past the slope limit: he slides
    Refused,    // a drop past kMaxUnsweptDrop - see the note there
};

class Walker {
public:
    Walker(const TriangleSoup& soup, double x, double y, double z)
        : soup_(soup), pos_{x, y, z} {}

    // Walk_ProbeGround: the surface under a point.
    //
    // The ray starts a step-height ABOVE the feet, not at them. Casting from
    // the feet exactly finds nothing - the probe wants a surface strictly
    // below its origin - and every step then reads as a hole; casting from a
    // step above also picks up a rise the actor is allowed to climb, which is
    // the same window the step limit describes.
    std::optional<double> ground(double x, double y, double z) const {
        return floorUnder(soup_, x, y - kStepUp - 1.0, z);
    }

    // The horizontal half. `dt` is the engine's own frame delta (30/fps, so
    // 1.0 at 30 Hz) and is only used to carry an ALREADY-FALLING actor down
    // this frame, so a caller that has not yet been taught about `tick` still
    // lands him rather than walking him through the air.
    StepResult step(double dx, double dz, double dt = 1.0);

    // One frame of the vertical half - `Actor_ApplyMotion`'s gravity plus
    // `Walk_GroundResponse`'s answer to it.
    //
    // The controller should call this EVERY frame, not only when the actor is
    // moving horizontally: an actor who steps off a ledge and then lets go of
    // the stick is still falling, and `step` is not called when the walk clip
    // stops producing a root delta. It is cheap and a no-op on the ground.
    // Without it he hangs in the air until he asks to move again, which is
    // what `step`'s own `dt` is a floor under rather than a substitute for.
    StepResult tick(double dt);

    // A TELEPORT: `actor.goto_address` writes the actor's position outright
    // (`sub_41BF50`), and the fall accounting starts over from the new floor.
    void moveTo(double x, double y, double z) {
        pos_[0] = x; pos_[1] = y; pos_[2] = z;
        fall_ = 0.0; vy_ = 0.0; vx_ = 0.0; vz_ = 0.0;
        airborne_ = false; sliding_ = false;
    }

    // The steep faces of the same set - what the actor slides down. Optional:
    // with none installed a steep face is a hole, which is what the walker did
    // before it could see them.
    void setSteep(const TriangleSoup* steep) { steep_ = steep; }
    // THE NARROW PHASE (collision.h): the faces a horizontal move is swept
    // against - every collision face steeper than the walker climbs, the
    // engine's split between "the probe answers with a floor" and "the sweep
    // answers with a wall" - and the sphere's radius. Radius 0 leaves the
    // sweep off, which is how a check shows what it is worth.
    // `centres` are the swept body's sphere centres RELATIVE TO THE FEET
    // (`Actor_Move` builds a capsule from the model's own list at descriptor
    // +244/+248 - HO1_FN: four of radius 10.9 from the feet to the head);
    // empty means one sphere sitting on the feet, the simulator's shape.
    void setBlockers(const TriangleSoup* blockers, double radius,
                     std::vector<std::array<double, 3>> centres = {}) {
        blockers_ = blockers; radius_ = radius; centres_ = std::move(centres);
    }
    // sweep, clamp, slide - `Actor_Move`'s three passes over `Sweep_ActorMove`
    // and `Walk_ClampNormal`, with the engine's STAND-OFF (a hit at distance
    // d >= 1 moves d - 1) and PUSH-OUT (d < 1: the body is pushed along the
    // clamped normal by 1 - d, re-swept, growing 1.1x until clear).
    // -> (dx, dz) becomes the whole displacement to apply; (0, 0) with a hit
    // is a block. `push` receives the push-out part, already inside dx/dz.
    void slide(double& dx, double& dz, double push[2]);
    int  lastSlides() const { return slides_; }   // passes that hit, last step

    const double* pos() const { return pos_; }
    const TriangleSoup& soup() const { return soup_; }
    double fall() const { return fall_; }
    bool   airborne() const { return airborne_; }
    bool   sliding() const { return sliding_; }

    // The tier the LAST landing arrived in - 0 none, 1 a step, 2 a fall,
    // 3 hurt, 4 killed. The engine reads the same four bands off the drop and
    // asks for .CTL bank group 2, 4 or 5 and ACTOR_STATE 18 or 19.
    int lastLandingTier() const { return tier_; }

    // `g_IgnoreLedges` - the engine's own global, whose only writer is
    // `sub_41C260` (0x0041C260), a five-line setter with no caller in the
    // decompilation. With it on, both the ledge refusal and the fall are
    // skipped and every drop is snapped.
    bool ignoreLedges = false;

private:
    // Land on `y`, ending an airborne or sliding stretch and banding the drop
    // the way `Walk_GroundResponse` does.
    void land(double y);

    const TriangleSoup& soup_;
    const TriangleSoup* steep_ = nullptr;
    const TriangleSoup* blockers_ = nullptr;
    double radius_ = 0.0;
    std::vector<std::array<double, 3>> centres_;
    int    slides_ = 0;
    // the earliest hit of every sphere of the body swept by `d` from feet `p`
    std::optional<SweepHit> bodyHit(const double p[3], const double d[3]) const;
    double pos_[3];
    double fall_ = 0.0;
    double vy_   = 0.0;
    double vx_   = 0.0;   // the slide's horizontal velocity (+216 / +224)
    double vz_   = 0.0;
    bool   airborne_ = false;
    bool   sliding_  = false;
    int    tier_ = 0;
};

// `Walk_ProbeGround`'s OTHER answer: which decor is under the point. The
// probe runs over every decor in state 2 and `Game_HandleEvent` case 9 fires
// when the owner changes (0x00467030 raises it; `sub_459AA0` too), which is
// what moves the ACTIVE row between two resident areas - not the load
// (todo/pending/T11.md, finding 2). With two shown sets the nearest floor
// below the step window decides, the same window `Walker::ground` casts from.
struct DecorSoup {
    int area = -1;                        // the AREA id the decor belongs to
    const TriangleSoup* soup = nullptr;   // its walkable soup
};
// -> the area of the nearest floor under (x, y, z), or -1 when no shown decor
// has one there. Game Y grows downward, so the nearest is the SMALLEST y.
int decorUnder(std::span<const DecorSoup> decors, double x, double y, double z);

}  // namespace omk

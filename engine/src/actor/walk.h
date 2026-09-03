// SPDX-License-Identifier: GPL-3.0-or-later
// The walker - one frame of Actor_Move + Walk_GroundResponse.
//
// The order is the engine's: try the move, probe under the result, and let the
// ground decide - revert where there is no floor, refuse a drop past the step
// limit, otherwise snap and keep the fall accounting.
#pragma once

#include "o3de/collision.h"

#include <span>
#include <string>
#include <vector>

namespace omk {

// The engine's own tunables, in world units (1 unit ~ 2.54 cm).
inline constexpr double kStepUp   = 11.87;   // 30 cm, up
inline constexpr double kStepDown = 11.87;   // the same limit, down
inline constexpr double kTerminal = 787.4;   // 20 m/s
inline constexpr double kFallHurt = 118.1;   // 3 m
inline constexpr double kFallKill = 196.9;   // 5 m

enum class StepResult {
    Moved,      // the step took, and the actor snapped to the new floor
    Reverted,   // no floor under the destination - nobody walks into the void
    Blocked,    // a rise past the step limit: a wall, not a stair
    Refused,    // a drop past the step limit: a ledge, and ledges are obeyed
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

    StepResult step(double dx, double dz);

    // A TELEPORT: `actor.goto_address` writes the actor's position outright
    // (`sub_41BF50`), and the fall accounting starts over from the new floor.
    void moveTo(double x, double y, double z) {
        pos_[0] = x; pos_[1] = y; pos_[2] = z;
        fall_ = 0.0; vy_ = 0.0;
    }

    const double* pos() const { return pos_; }
    const TriangleSoup& soup() const { return soup_; }
    double fall() const { return fall_; }
    bool   ignoreLedges = false;

private:
    const TriangleSoup& soup_;
    double pos_[3];
    double fall_ = 0.0;
    double vy_   = 0.0;
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

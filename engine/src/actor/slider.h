// SPDX-License-Identifier: GPL-3.0-or-later
// THE PLAYER'S RIDE - `Slider_TickRide` (0x00458150) and the flight model
// under it, transcribed 2026-09-07.
//
// The road traffic has been ported since `docs/STREET_LIFE.md` §2b - the
// sliders and motos on the `.OPT` circuit's vehicle lanes, spawned and driven
// by the walkers' own mover - and this is the other half: what happens when
// the PLAYER is on one. `todo/standing-unknowns.md` §5 recorded the decision
// not to port it with the size measured rather than guessed, and
// `todo/slider.md` is the plan that reversed it.
//
// ---------------------------------------------------------------- THE ENTRY
//
// The binary names its own preconditions, in the debug strings of the two
// special moves that bracket a ride:
//
//     MDSLIDIN  (tab_special_move[12], 0x0046B7F0)
//       player[+404] != 6        -> "bad mode getting in slider !"
//       no active slider         -> "no active slider !"
//       slider mode != 3         -> "slider is not in open mode !"
//       else: slider mode = 4, player[+404] = 7, node flag 8,
//             and sub_41DF30(7,...) or "cant find slider interface !"
//
//     MDSLIDOU  (tab_special_move[13], 0x0046B890)
//       player[+404] != 8        -> "bad mode getting out of the slider !"
//       else: player[+404] = 1
//
// So the mount is entered from ACTOR_STATE **6** and not from 7 - CLAUDE.md
// §4's "7 and 8 are the mount and the ride" is right about the two ride
// states and says nothing about the gate, which is 6 plus a slider standing
// OPEN (its mode 3). 7 is the state `Slider_TickRide` binds the body in, and
// 8 is the one `MDSLIDOU` will leave from.
//
// ------------------------------------------------------------------ THE TICK
//
// `Slider_TickRide` is 82 lines around three helpers, and the first thing it
// does is `flt_4C30D8 *= 0.5` - **the whole ride advances at half a frame per
// frame** - restored on the way out. Then, on `player[+404] == 7`, it binds
// the player's node under the slider's, calls `sub_438420(slider, 3)` and
// requests **camera mode 8 with the slider as BOTH subjects**, not the
// player. After the three helpers it drops the player onto the surface under
// him and runs `Actor_ScanZones`, so riding still triggers zones.
//
// The three helpers are what this file is:
//
//   `sub_4573E0` (387 lines)  the flight model - steer, bank, thrust, drag,
//                             skid and the two-probe pitch
//   `sub_458600` (75)         the HOVER: the height over the surface and the
//                             bob
//   `sub_457F50` (66)         where the rider and the vehicle's node go
//
// What is NOT here: the pool, the reservation and the arrival - `sub_452570`'s
// other arm - which is `todo/slider.md` step 2b, and the sound.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace omk {

// WHAT THE RIDE MEETS (drift audit M3) - the world `sub_458600`'s helpers
// read, handed in by the frontend each frame:
//
//   `surface`   `World_ProbePoint(-1, x, y, z, &mesh, ..., &drop)` WITH the hit
//               mesh's NAME (its first two bytes, `+16`): the road is a name
//               starting `X` or `OP`, a pedestrian crossing `O`
//   `vehicles`  the 40 slots' live vehicle nodes but the ridden one (slot
//               order - `sub_458880` takes the FIRST it overlaps): body
//               (mover +36), heading (+24), speed (+52 / 256, `sub_4382A0`)
//               and model radius (+88, `sub_438040`)
//   `walkers`   the street's 200 walker records in order: body and heading
//   `radius`    the RIDDEN slider's model radius
//   `setSpeed`  `sub_4382D0` on a vehicle hit: its +52 = speed * 256
struct RideVehicle { int slot = -1; double x = 0, y = 0, z = 0, dirx = 0, dirz = 0, speed = 0, radius = 0; };
struct RideWalker  { double x = 0, y = 0, z = 0, dirx = 0, dirz = 0; };
struct RideWorld {
    std::function<bool(double x, double y, double z, double& drop, char name[2])> surface;
    std::vector<RideVehicle> vehicles;
    std::vector<RideWalker>  walkers;
    double radius = 0.0;
    std::function<void(int slot, double speed)> setSpeed;
};

// One ride, as the engine's own globals. Named for what they hold; the
// address each one is stands beside it so the transcription stays checkable.
struct SliderRide {
    double x = 0, y = 0, z = 0;        // 8F5DA4 / 8F5DA8 / 8F5DAC
    double vx = 0, vz = 0;             // 8F5DB0 / 8F5DB8 (8F5DB4 is cleared)
    double speed = 0;                  // 8F5DBC, SIGNED - sqrt(vx^2+vz^2)
    double thrust = 0;                 // 8F5DCC
    double steerTerm = 0;              // 8F5DD0
    double pitch = 0;                  // 8F5DD4, radians, from two probes
    double yaw = 0;                    // 8F5DD8, degrees
    double roll = 0;                   // 8F5DDC, degrees, wrapped 0..360
    double bobPhase = 0;               // 8F5E00, degrees
    double lastSpeed = 0;              // flt_53997C
    int    turnSign = 0;               // 539978, -1 / 0 / +1
    int    noThrust = 0;               // 8F5DF0, a countdown that kills thrust
    int    settle = 0;                 // 8F5E08: 40 after a vehicle hit, counted down by
                                       //         `Slider_TickRide`, 0 after a road-edge push
    double pushX = 0, pushZ = 0;       // 8F5E20 / 8F5E24 - written by `sub_458880`, read nowhere
    int    vehicleHits = 0;            // instruments: `sub_458880`'s bounces
    int    walkerSteps = 0;            //              `sub_459970`'s side-steps
    bool   stopped = false;            // `sub_4570F0` was reached

    // THE INPUT WORD is the interface's own 14-slot word (`dword_4E9718`),
    // which `Slider_TickRide` copies into `dword_8F5DE0` unchanged. Its low
    // four bits are the ones this reads, and they are the SAME four the UI
    // walker uses (`kUiLeft`/`kUiRight`/`kUiUp`/`kUiDown`):
    //
    //     0x1 / 0x2   STEER, +-5 degrees a frame
    //     0x4 / 0x8   THRUST, from a six-value ladder of +-0.615, +-1.845
    //                 and +-3.690 chosen by the sign of the speed
    //     0x20        BRAKE: under 10 units of speed it stops the ride
    //
    static constexpr std::uint32_t kSteerLeft  = 0x1;
    static constexpr std::uint32_t kSteerRight = 0x2;
    static constexpr std::uint32_t kThrustUp   = 0x4;
    static constexpr std::uint32_t kThrustDown = 0x8;
    static constexpr std::uint32_t kStop       = 0x20;

    // The engine's own numbers, kept as named constants because each is a
    // measurement: the hover height and the bob are `sub_458600`'s, the bank
    // limit and its rate are `sub_4573E0`'s, and the pitch probe's 39.370079
    // is exactly ONE METRE at the world's 39.3701 units to the metre.
    static constexpr double kHover      = 30.75;        // over the surface
    static constexpr double kBobRate    = 8.4300003;    // degrees a frame                                                        // (one cycle: 42.7 frames)
    static constexpr double kBobAmp     = 30.75 * 0.0625;   // 1.922 units
    static constexpr double kBankLimit  = 11.0;         // degrees
    static constexpr double kBankRate   = 1.75;         // degrees a frame
    static constexpr double kSteerRate  = 5.0;          // degrees a frame
    static constexpr double kProbeSpan  = 39.370079;    // 1.00 m fore and aft
    static constexpr double kRiderUp    = 33.149605;    // the seat, over the hull
    static constexpr double kNodeUp     = 10.0;         // the actor's own +248

    // `World_ProbePoint(-1, x, y, z, &hitMesh, ..., &drop)` - and what it
    // writes is a DROP, the signed distance from the probe point down to the
    // surface, not an absolute height. `Slider_TickRide` uses it the same way
    // (`player[+248] += drop - 1.0`), and the hover adds `drop - 30.75`
    // straight onto `y`. `braking` is the engine's `strncmp("OP", meshName,
    // 1)` - a surface that damps the ride. -> false when there is no surface,
    // which is what sends the hover looking 39 units lower.
    using Probe = std::function<bool(double x, double y, double z,
                                     double& drop, bool& braking)>;

    // `sub_4573E0`, and only that: the steer, the bank, the thrust ladder, the
    // drag, the skid test and the two-probe pitch. `dt` is the engine's frame
    // delta ALREADY halved by the caller, because `Slider_TickRide` halves it
    // once for all three helpers.
    void fly(std::uint32_t input, double dt, const Probe& probe);
    // `sub_458600`'s height arm: the hover over the surface, the bob, and the
    // 0.75 damping the "OP" surfaces apply.
    void hover(double dt, const Probe& probe);
    // ...the same with the world (`sub_458600` whole, in its order): the
    // vehicle push first, the noThrust countdown, the probe; the "OP" arm
    // only for a WALKER ahead on a crossing (`sub_458490`), who is stepped
    // round (`sub_459970`); then the height. The `Probe` form above keeps
    // the bare arm for the flight probes.
    void hover(double dt, const RideWorld& world);
    // `sub_458880`: the first vehicle the ride overlaps, pushed out to the
    // edge of the two ellipses if the ground there is ROAD, the velocity
    // bounced by 2/3 of the closing speed, `settle` = 40 and the speed
    // handed to the vehicle hit. -> whether it bounced.
    bool collideVehicles(const RideWorld& world);

    // `sub_457F50`: where the RIDER sits - the slider's x and z, and its y
    // plus `kRiderUp`. The actor's own `+248` takes `kNodeUp` instead, which
    // is the second of the two heights that function writes.
    void riderAt(double out[3]) const {
        out[0] = x; out[1] = y + kNodeUp; out[2] = z;
    }
    void seatAt(double out[3]) const {
        out[0] = x; out[1] = y + kRiderUp; out[2] = z;
    }
};

}  // namespace omk

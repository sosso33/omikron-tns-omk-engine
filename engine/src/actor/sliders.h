// SPDX-License-Identifier: GPL-3.0-or-later
// THE PROCEDURAL PEDESTRIANS - the anonymous walkers a city street is filled
// with, spawned along the `.OPT` lanes and driven by the traffic tick
// (docs/STREET_LIFE.md 2). Mechanism A of a street; the authored extras are
// scene programs (`SceneRunner`) and the crowd push is the spatial index.
//
// Read from `Slider_Init` (0x00453450) - the pools, the models, the spawner
// `sub_453B40` and its callback `sub_453ED0` - and from `Sliders_Tick`
// (0x00454BB0) and what it calls per pedestrian: `sub_454F40` (the MOVER:
// lanes, routes, lists, reservation groups), `sub_455830` (the BODY: the walk
// clip's root motion chasing the mover, following, overtaking, the gait),
// `sub_455E90` / `sub_4561B0` / `sub_456250` (the action phases). Every
// method below names the function it transcribes.
//
// The engine keeps a pedestrian in two records: a 192-byte MOVER slot shared
// with the vehicles - position, direction, the length left on the current
// segment, speed, radius, lane/route/segment, flags, list node - and a
// 72-byte record binding it to a body: the clip, its clock, the sex, the
// speed factor, the overtaking side-step. `Pedestrian` carries both.
//
// THE MOVER IS A CARROT. It walks the lane network at its speed; the body is
// placed by the walk clip's root motion, aimed at the mover every frame, and
// the gait (`sub_455D10`) keeps the mover 19.5..58.5 units ahead: nearer than
// 2 the body idles, nearer than 19.5 the mover doubles its speed, further than
// 58.5 the mover stops. So the body never leaves the lanes by more than that
// lag, which is what `verify.py: engine: pedestrians` asserts over a run.
//
// THE DENSITY. `Slider_Init` spaces the walkers `39 * (5 - level) * h[3]`
// units apart along every pedestrian lane, `level` being options row 6
// ("Niveau d'activite dans les rues", 0..4). See `kDefaultStreetActivity`.
//
// Units are the engine's (an inch, docs/PORTING A2) and the ×256 lengths are
// kept as the engine keeps them: a segment's remaining length and a speed
// are `units * 256`, the mover advances `speed * dt / 256` a frame.
#pragma once

#include "formats/opt.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

// THE DEFAULT DENSITY. The engine's own initial value: `sub_41F4C0` writes
// `dword_90E724 = 0x01030000`, whose byte 2 is the street activity, 3 =
// "Important". TODO(options): replace with the value the options menu holds -
// row 6 of tables/ui.json, read by `Opt_ReadStreetActivity` (0x0048FC70) and
// written by its apply hook (0x0048FC40) into that byte. `Session::
// setStreetActivity` is the one entry point; nothing else reads the default.
inline constexpr int kDefaultStreetActivity = 3;
inline constexpr int kMaxWalkers = 200;             // the 72-byte pool, 200 slots
inline constexpr float kSpawnSpacingFactor = 39.0f; // `v20 = (float)(39 * a4)`
inline constexpr float kCarrotBehind = 117.0f;      // the "previous" point at spawn
inline constexpr float kOvertakeReach = 39.0f;      // `if (v66 <= 39.0)`
// `unk_4C8880`: the gait thresholds, units the body moved this frame
inline constexpr float kGaitNear = 19.5f, kGaitFar = 58.5f;
// the four LOD distances a pedestrian model is drawn at (`dword_4C8870`,
// 10/20/30/40 m in inches) - for a frontend; the pool does not draw
inline constexpr float kLodDistances[4] = {393.7007751f, 787.4015503f, 1181.1024170f, 1574.8031006f};

// ------------------------------------------------------------ the vehicles
//
// The ROAD TRAFFIC - the hover-taxis and the motos that share the circuit
// with the walkers (todo/road-traffic.md, docs/STREET_LIFE.md 2b). Read from
// `Slider_Init`'s vehicle half, `sub_4543F0` (the spawner over lanes
// `header[2]..header[5]`), `sub_4544B0` (the spawn callback and the 40-slot
// ride pool), and the tick `Sliders_Tick` -> `sub_456530` -> `sub_456C70`
// (the drive) + `sub_456B40` (the sound).
//
// A vehicle is a MOVER like a walker - the engine's 240-slot pool is shared
// and so is this one - but its body is not posed by a clip: it is a point
// chasing the mover at a speed of its own, accelerating to a cap and braking
// when the mover is blocked. So `Vehicle` holds the ride record and the mover
// stays in the shared pool, which is what keeps the occupancy lists and the
// reservation groups common to both classes. That sharing is not a
// convenience: 70 of Anekbah's groups are reachable from both a pedestrian
// and a vehicle route, and they are what stops the two crossing into each
// other.
inline constexpr int   kMaxVehicles = 40;          // `Mem_Calloc(0x28, 0x18)`
inline constexpr float kVehGaitNear = 195.0f;      // `unk_4C8888`
inline constexpr float kVehGaitFar  = 390.0f;
inline constexpr float kVehSpawnSpeed = 256.0f;    // mover +52/+56 at spawn, and the brake floor
inline constexpr float kVehSpeedCap = 5000.0f;     // the ride record's +12
inline constexpr float kVehAccel = 256.0f;         // per frame, while running
inline constexpr float kVehBrake = 768.0f;         // per frame, while blocked or braking for the player
inline constexpr float kVehRunOver = 1706.6666f;   // above this a touch raises message 17
inline constexpr float kVehBrakeRange = 195.0f;    // it brakes for a player this near
inline constexpr float kVehSoundRange = 585.0f;    // sliderm01 starts inside, stops outside
inline constexpr float kVehNodeLift = 30.75f;      // `sub_437F80(node, x, y - 30.75, z)`
// `dword_4C8860`: the four LOD distances a vehicle is drawn at, 20/30/40/50 m
// in inches - the crowd's (`kLodDistances`) start at 10
inline constexpr float kVehLodDistances[4] = {787.4015503f, 1181.1024170f, 1574.8031006f, 1968.5039062f};

// The two model tables compiled into the executable, 12 bytes a row like the
// crowd's: `aSliFn` is TWO rows, both "sli_fn", and `aMoto` is one. Row 0 of
// the sliders is the slot `sub_4544B0` reserves for the player's own vehicle,
// so an ambient slider is only ever drawn from row 1 up - which is why an
// area whose slider mask is 1 (Qalisar) puts nothing but motos on its roads.
// `kind` is 1 for the sliders and 0 for the motos, the sense of
// `sub_4544B0`'s coin.
const std::vector<std::string>& vehModelTable(int kind);

// WHERE A CALLED SLIDER COMES TO - `sub_452570`'s search, and `sub_452A80`.
//
// The sneak's slider page hands `sub_452570` a point (a destination's address,
// or the player's own position) and it looks for the nearest point on a
// VEHICLE lane. The scan is `for (lane = header[2]; lane < header[5])` - which
// is exactly the range after the pedestrian one, `pedEnd .. laneCount` - and
// inside each lane it walks the polyline key by key, asking `sub_452A80` for
// the distance from the target to that segment:
//
//     u = -dot(point - target, delta) / |delta|^2
//     inside  (0 <= u < 1)  the perpendicular foot
//     outside               the nearer of the two ENDPOINTS
//
// behind a 3900-unit box reject on each axis (99 m), which is what stops the
// search crossing a city. The best is kept with its lane and its key.
//
// `sub_452570` then picks a ROUTE off that lane round-robin, from a global
// counter: `dword_4C8854 = (dword_4C8854 + 1) & 0x7FFFFFFF` and
// `firstRoute + counter % routeCount` - with `routeCount` 0 read as 1, which
// the engine WRITES back into the record rather than only reading.
struct LanePoint {
    int   lane = -1;          // into `OptTrack::lanes`
    int   key  = -1;          // which segment of that lane
    float at[3] = {0, 0, 0};  // the closest point itself, in world units
    float dist = -1.0f;       // -1 when nothing was in range
    bool  found() const { return lane >= 0; }
};
LanePoint nearestVehicleLane(const OptTrack& t, const float target[3]);

// ...AND WHAT `sub_452CC0` THEN DOES WITH IT - the DECISIONS, which is the
// half of that function a port can hold to a number.
//
// Its bookkeeping is the engine's own linked lists: the mover is unlinked
// from whatever lane list it is on and relinked onto the chosen one, and if
// another vehicle is already there with a priority no higher than the call's
// (`+186 <= a1[4]`) the two are SWAPPED outright - node blocks and all - so
// the vehicle that was in the way BECOMES the player's slider. This port's
// traffic keeps its own occupancy structures (`docs/STREET_LIFE.md` §2b), so
// none of that is transcribed and this carries only what the function
// DECIDES:
//
//   * the mover goes to the chosen lane's ORIGIN, set back **39 units** along
//     the lane's own direction in x and z - the y is the origin's, untouched;
//   * the direction is the lane's FIRST key delta, normalised;
//   * `mover+56 = 256.0` and `mover+52 = 0`, `mover+186 = 1` (the priority
//     the swap test compares against), and the node's flag word takes `| 8`;
//   * the node itself sits at `y - 30.75` - the SAME hover height the ride
//     uses (`SliderRide::kHover`), which is the third place that number
//     turns up.
struct SliderCall {
    LanePoint at;                    // the lane the search chose
    int   route = -1;                // the round-robin route off it
    float place[3] = {0, 0, 0};      // where the mover is put
    float dir[3] = {0, 0, 0};        // the lane's normalised direction
    float nodeY = 0.0f;              // `place[1] - 30.75`, where the node sits
    static constexpr float kSetBack = 39.0f;    // a flat 39, not the 39.370079
                                                // that is a metre elsewhere
    static constexpr float kSpeed   = 256.0f;   // mover +56
    bool ok() const { return at.found(); }
};
SliderCall planSliderCall(const OptTrack& t, const float target[3],
                          unsigned counter);

// THE RIDE STATE MACHINE - `sub_456530` (0x00456530), on the slot's `+8`.
//
// One switch, eight states, and every arm of it re-arms `flt_536C28 = 90.0`.
//
// **That is a ninety-FRAME latch, not a field of view**, and this port had
// already read it as one before the slider work began (`tickVehicles`: "the
// 90-frame latch that lets one bump be reported at a time"). The listing is
// unambiguous - `fld flt_536C28 / fsub flt_4C30D8 / fst flt_536C28`, and at
// zero it clears itself and `dword_538E20` with it. Reading `= 90.0` in these
// arms as a camera fov was a wrong turn on 2026-09-08, caught by looking at
// what DECREMENTS it. What the states are:
//
//   0  AMBIENT   not in the switch at all; the default arm falls through to
//                the ordinary drive, which is what the traffic already does
//   1  IDLE      a 600-unit countdown (`flt_8F5E90`); when it runs out the
//                slot goes back to 0, the mover's `+180 & 4` is cleared and
//                the assignment is released. A slider you called and did not
//                board gives up after 600
//   2  COMING    the TRANSPORT arm. Camera mode 8 on the SLIDER while it
//                drives to the pickup point; within **117 units** of it
//                (2.97 m, so three metres) the camera hands back to mode 0 on
//                the PLAYER with parameter 56, `Screen_Fade(0)` fades in and
//                `Actor_HoldAnimation(player, 0)` releases the hold that
//                `sub_452570` put on. The slot then goes to 1 with the 600
//                on its clock
//   3  OPEN      it publishes the assignment (`dword_538E20`) and waits -
//                and this is the mode `MDSLIDIN` demands before it will let
//                the player in ("slider is not in open mode !")
//   4/5 ABOARD   nothing but the fov: `Slider_TickRide` owns the body now
//   6  FETCHING  the arm `sub_452570` takes when a slider was ALREADY
//                assigned. The same 117-unit arrival test, and on arrival the
//                slot goes to **4**, the player is probed onto the ground,
//                the slider's node is re-parented, and the camera goes to
//                **mode 10** framed between the DESTINATION's own address
//                record and the vehicle
//   7  LEAVING   once the player is more than **300 units** away AND in front
//                of the slider (a dot product against his facing), it is put
//                back where it was parked and released
//
// So the cameras a ride passes through are 8 while it comes, 0 when it
// arrives, 10 when it leaves with you, and 17 when you get off
// (`sub_4570F0`) - four modes, none of them guessed.
//
// The LINKED LISTS this machine runs over are the engine's and are not
// transcribed (see `planSliderCall`); what is here is the machine.
struct RideMachine {
    int   state = 0;              // the slot's `+8`
    float idleClock = 0.0f;       // `flt_8F5E90`, in frames
    int   camera = -1;            // the mode this tick asked for, -1 for none
    bool  fadeIn = false;         // `Screen_Fade(0)` fired this tick
    bool  released = false;       // the hold was released this tick

    static constexpr float kArrive = 117.0f;   // 2.97 m
    static constexpr float kIdle   = 600.0f;
    static constexpr float kLeave  = 300.0f;
    // `flt_536C28`, re-armed by every arm: ninety FRAMES, the latch that lets
    // one bump be reported at a time. Not a fov.
    static constexpr float kLatchFrames = 90.0f;

    // One tick. `toTarget` is the distance from the slider to its pickup
    // point, `toPlayer` the distance from the player to the slider, and
    // `ahead` whether he is in front of it. `dt` is the engine's frame delta.
    void tick(float dt, float toTarget, float toPlayer, bool ahead);
};
// The route `sub_452570` would take for that lane on the `counter`-th call.
int laneRoute(const OptTrack& t, int lane, unsigned counter);

// `MDACTION`'s slider arm, the two constants it tests against.
// 157.48032 inches is 4.000 m and 59.0551 (camera preset 9's eye lift) is
// 1.500 m, which is the kind of roundness a wrong unit does not produce.
inline constexpr float kBoardReach = 157.48032f;   // flt_4BC924
// ...and the seat drop the arm folds into d.y and into the placement:
// `flt_4BC91C` is -33.149605 (subtracted, so added) and `flt_4BC928` is
// +33.149605 (subtracted). The same number `sub_468FA0` seats a rider at.
inline constexpr float kBoardSeatY = 33.149605f;

struct Vehicle {
    bool  live = false;
    int   kind = 1;                 // 1 a slider, 0 a moto
    std::string model;              // the .3do stem, as the table spells it
    int   mover = -1;               // into the shared pool - the record's +0
    int   state = 0;                // +8: 0 ambient traffic, 1..7 the player's ride
    float speedCap = kVehSpeedCap;  // +12
    int   sound = -1;               // +20, the sliderm01 voice; -1 = not playing
    bool  reserved = false;         // +22 == 1: slot 0, the player's own slider
    // Which sub-object of the model is the body. `sub_4544B0` hands ambient
    // traffic `v16[1]` (sub-object 0, the heaviest) and the reserved slider
    // `v16[2]` (sub-object 1); the LOD chain is built over 1..3 either way.
    // READ FROM THE CALL SITES AND NOT YET JUDGED BY EYE.
    int   lodBase = 0;
    // counters for the checks
    int   stops = 0, brakes = 0, bumps = 0, soundOn = 0;
};

// One clip of the pedestrian library (ANIMS\PASSANTH.ANI) as the pool needs
// it. `slot` is the id the action points name (`sub_434630` matches +4);
// `type` is what `List_PickRandomByType` picks by: 9 the walk, 11 the idle,
// 14/15/16 an action's enter/main/exit clip sharing one name.
struct PedClip {
    int          slot = 0;
    int          type = 0;
    std::string  name;
    int          frames = 0;
    std::size_t  descriptor = 0;          // the descriptor's offset in the library, for a frontend's poses
    // the root bone's keys, 3 floats a key: key 0 is the rest pose, and key
    // k >= 1 is the root's MOTION over frame (k-1, k] in the clip's own frame
    // (a walk is along -Z) - what `Anim_RootDelta` sums, fractionally at
    // both ends, and turns by the instance's orientation matrix
    std::vector<float> root;
    // `sub_453D80`: the root's xz travel from frame 1 to frames-1, ×256, per
    // frame - the mover's base speed before the walker's own factor
    float        strideX256 = 0.0f;
};

struct PedClips {
    std::vector<PedClip> men, women;      // groups 1 and 2
    const std::vector<PedClip>& group(int sex) const { return sex == 2 ? women : men; }
    const PedClip* randomOfType(int sex, int type, std::uint32_t& rng) const;
    const PedClip* bySlot(int sex, int slot) const;
    const PedClip* byNameType(int sex, const std::string& name, int type) const;
    bool empty() const { return men.empty() && women.empty(); }
};
// The clips of one library; empty when `ani` is not a "3.0V" file.
PedClips pedClipsFrom(std::span<const std::byte> ani);
// One GROUP of a library, by CHARACTER TYPE - the same walk `pedClipsFrom`
// does for the pedestrians' types 1 and 2, for any type. A shoot-mode
// character's clips come from here: he carries no `.CTL` at all and his type
// is the actor record's `+176`.
std::vector<PedClip> animGroupClips(std::span<const std::byte> ani, int group);

// The model tables compiled into the executable (`aPshFn` / `aFshFn`, 12
// bytes a row: an 8-char name and a weight), which the AREA header's masks
// at +164 (men) and +168 (women) select bits of.
const std::vector<std::string>& pedModelTable(int sex);
// ...and the first names the pool cycles through (`off_4C88A0` / `off_4C8920`).
const std::vector<std::string>& pedNameTable(int sex);

// `Anim_RootDelta` (0x004711D0) over the root track: the motion between the
// clocks `t0` and `t1` (frames), the partial keys at both ends scaled by
// their fraction, turned by the orientation `heading` (a unit forward
// vector; nullptr for the clip's own frame). `sub_453330` builds the matrix
// from the heading: row 2 is -forward (a clip walks along -Z), row 0 the
// horizontal right, row 1 their cross - taken as the orthonormal frame the
// decompiled function's rows spell out, except its last element, which
// reads `v8 / v7` where the frame needs `v8 * v7`.
void pedRootDelta(const PedClip& c, float t0, float t1, const float* heading, float out[3]);
// The engine's heading recipe: a forward vector to the facing in degrees
// (`atan2(z, x) + 90`, so facing 0 looks down -Z), and back.
float pedFacingOf(const float heading[3]);
void  pedHeadingOf(float facingDeg, float heading[3]);

struct Pedestrian {
    bool         live = false;
    // -1 for a walker; otherwise the `Vehicle` this mover belongs to. The
    // engine has one 240-slot mover pool for both classes and so has this.
    int          vehicle = -1;
    int          sex = 1;                     // 1 man, 2 woman (record +20)
    std::string  model, name;
    // ---- the mover (the 192-byte slot)
    float        pos[3] = {0, 0, 0};          // +0
    float        prev[3] = {0, 0, 0};         // +12 (the point behind, the action offsets are added to it)
    float        dir[3] = {0, 0, 0};          // +24, unit
    float        remaining = 0.0f;            // +48, units*256 left on the segment
    float        baseSpeed = 0.0f;            // +52, units*256 a frame
    float        speed = 0.0f;                // +56
    float        radius = 0.0f;               // +60, half the model's bounding radius
    int          ahead = -1;                  // +68, the walker in front this frame
    int          lane = -1;                   // +72
    int          route = -1;                  // +76
    int          seg = 0;                     // +186: on a lane the 1-based key index being walked,
                                              //       on a route the 0-based step index
    std::uint32_t flags = 0;                  // +180: 1 blocked, 0x10 on a route, 0x20 overtaking,
                                              //       0x80 in an action, 0x100 idling
    // ---- the body
    float        body[3] = {0, 0, 0};         // mover +36/+40/+44: where the model stands
    const PedClip* clip = nullptr;            // record +8
    float        clock = 1.0f;                // record +12, frames into the clip
    int          frames = 0;                  // record +16
    float        footY = 0.0f;                // record +24, the root's summed y
    float        bodyRadius = 0.0f;           // record +28
    float        side[2] = {0, 0};            // record +32/+36, the overtaking side-step (x, z)
    float        sideLen = 0.0f;              // record +40
    float        speedFactor = 1.0f;          // record +56, 0.75..1.20
    // ---- the action (mover +176 -> a state)
    int          action = -1;                 // the action point, while in one
    // ---- the orientation (mover +140, the 3x3 `sub_453330` builds)
    float        heading[3] = {0, 0, -1};     // unit forward; the root motion is turned by it
    // ---- for a frontend
    float        facing = 0.0f;               // degrees, `pedFacingOf(heading)`
    // ---- counters for the checks
    int          laneChanges = 0, blockedFrames = 0, actionsVisited = 0, overtakes = 0;
};

// `sub_453330`: the orientation from a direction, stored as the unit forward
// (and the facing kept beside it). Shared with the vehicles' drive.
void setHeading(Pedestrian& m, float x, float y, float z);
float len3(const float v[3]);

class Sliders {
public:
    // The whole of `Slider_Init`'s pedestrian half: the models the masks
    // select (quotas of 100 over them), the clips, then `sub_453B40` over the
    // pedestrian lanes with `spacing = (5 - level) * track.pedSpacing`. The
    // track is copied; `seed` feeds the pool's own xorshift where the engine
    // calls `rand()`.
    // `sliMask` / `motoMask` are the AREA header's `+172` / `+174` (int16,
    // `sub_40EA10` / `sub_40E9D0`): passing them adds the ROAD TRAFFIC of
    // `Slider_Init`'s vehicle half, which the same call places and the same
    // `tick` drives. Zero leaves the roads empty, so a caller that has not
    // read them yet keeps exactly the crowd it had.
    void load(const OptTrack& track, const PedClips& clips, std::uint32_t menMask,
              std::uint32_t womenMask, int streetActivity, std::uint32_t seed = 1u,
              std::uint32_t sliMask = 0, std::uint32_t motoMask = 0);
    void clear();
    bool loaded() const { return loaded_; }

    // `Sliders_Tick`'s pedestrian loop, `dt` in frames (1.0 at 30 fps).
    void tick(float dt);

    // The shared mover pool - walkers and vehicle movers both. A vehicle's
    // entry carries `vehicle >= 0`; `liveCount` counts the walkers alone, so
    // every pedestrian number a check quotes is unmoved by the traffic.
    const std::vector<Pedestrian>& movers() const { return movers_; }
    int  liveCount() const;
    const std::vector<Vehicle>& vehicles() const { return vehicles_; }
    int  liveVehicles() const;
    const OptTrack& track() const { return track_; }
    int  streetActivity() const { return level_; }
    // the models the masks selected, with their remaining quotas
    struct ModelQuota { std::string name; int sex; int quota; };
    const std::vector<ModelQuota>& models() const { return models_; }
    // `sub_438040`: the body's radius is the model's root mesh `+88`, which
    // the pool cannot read itself (it holds no models); the Session hands it
    // in after the load and every walker wearing the model takes it (the
    // mover's `+60` is half of it, the record's `+28` the whole)
    void setModelRadius(const std::string& model, float radius);

    // `sub_452280`: the walker in front of the player within 117 units, for
    // the action press - returns its index or -1. `facingDeg` is the player's.
    int  nearestInFront(const float pos[3], float facingDeg) const;
    // The walker the player is talking to (`dword_53992C`): its action
    // countdown is held while set.
    void setTalkTarget(int w) { talkTarget_ = w; }
    // the action phase of walker `w`: 0 walking to the point, 1 enter, 2 main
    // (the talkable one), 3 exit; -1 when not in an action
    int  actionPhase(int w) const;
    int  talkTarget() const { return talkTarget_; }

    // How many the spawner would place at `level` on this track - the rule
    // alone, for the check against tools/opt_track.py.
    static int spawnCount(const OptTrack& t, int level);
    // ...and along the VEHICLE lanes, where the density plays no part:
    // `sub_4543F0` passes `header[4]` to the same walk, undivided.
    // `cap` false gives what the walk WOULD place with no pool limit, which
    // is the number that separates the three circuits (126 / 46 / 85 against
    // three identical 40s).
    static int vehicleSpawnCount(const OptTrack& t, bool cap = true);

    // `sub_438040` for a vehicle: the body radius of one of the two shipped
    // models, which the pool cannot read itself.
    void setVehicleModelRadius(const std::string& model, float radius);

    // ------------------------------------------------- THE PLAYER'S SLIDER
    //
    // `sub_452570`'s ARM arm, in this pool's own terms. It puts a vehicle on
    // the lane `planSliderCall` chose, marks it the player's (`slot+22 == 1`,
    // which this port's read of `Slider_Init` already called "slot 0, the
    // player's own slider") and drives it with `RideMachine` state 2 until it
    // is inside the 117 units of the pickup point - at which point it stops
    // and goes OPEN (state 3), which is what `MDSLIDIN` demands.
    //
    // -> false when the area has no vehicle lanes at all, which is what
    // LAHOREY is and what makes a call there fail in the engine too.
    bool callSlider(const float target[3]);
    // Which vehicle the call is using, -1 when none is out.
    int  calledVehicle() const { return called_; }
    // Its state machine, for a caller that wants the camera or the mount gate.
    const RideMachine& callMachine() const { return callRide_; }
    // Where it is, and whether it is OPEN and near enough to board.
    bool calledAt(float out[3]) const;
    // ...and which way it points, for the camera that watches it come.
    float calledYaw() const;
    bool calledIsOpen() const { return called_ >= 0 && callRide_.state == 3; }
    // One-shot: the called slider has just been handed back to the ordinary
    // traffic (`sub_456530` case 7's `sub_438420(slider, 0)`). Consumed by the
    // viewer so a reader can see it happen rather than infer it from a slider
    // that stopped blocking the lane.
    bool takeReleasedNotice() { const bool r = releasedTold_; releasedTold_ = false; return r; }
    // The slider's own FRAME: where it is, and the rows of the 3x3 that
    // `sub_438450(slider)` hands `Matrix3x3_RotateVector` - the local +X and
    // the forward. `Matrix3x3_FromEulerAngles(0, y, 0)` is
    // `[[cy,0,sy],[0,1,0],[-sy,0,cy]]`, so with the mover's unit heading `f`
    // as row 2 the local X is `(f.z, 0, -f.x)`. Taken as vectors and never as
    // an angle: the pool's heading and the player's euler do NOT share a yaw
    // convention (`calledYaw` inverts one against the other), and turning the
    // door geometry into a yaw would decide that question by accident.
    bool calledFrame(float pos[3], float localX[3], float localZ[3]) const;

    // THE BOARDING GATE, and it is `MDACTION`'s (0x0046AEC0, `loc_46AFF8`),
    // not `MDSLIDIN`'s. Two geometric tests on `d = slider - player`, with
    // the seat drop folded into d.y:
    //
    //     dot(d, localX) >= 0      the CORRECT SIDE - the man must stand on
    //                              the slider's -X side, which is where its
    //                              door is and where camera preset 9 watches
    //                              from
    //     |d| < 157.48032          FOUR METRES exactly, at 0.0254 m an inch
    //
    // `MDSLIDIN` is the move at the far END of the door clip and carries no
    // geometry at all; it was this port's gate because the animation between
    // them had not been found.
    bool canMount(const float playerPos[3]) const;
    // The player got on: the slot goes to 4 (aboard) and the vehicle stops
    // being driven, because `Slider_TickRide` owns the body from here.
    void mountCalled();
    // ...and off, which is `sub_4570F0`'s slot state 7 - it drives away once
    // he is 300 clear and in front of it.
    void dismountCalled();
    // THE JOURNEY: send the called (and boarded) vehicle to the lane nearest
    // `target` - `sub_452570`'s arm when a slider is ALREADY assigned, which
    // sets its state to **6** rather than 2. `sub_456530` then drives it and
    // the same 117-unit arrival test ends at state 4 and camera mode 10.
    // -> false when the target has no vehicle lane within reach.
    bool sendCalledTo(const float target[3]);
    // ...and whether that journey has ARRIVED (state 6 -> 4 this tick or since).
    bool journeyArrived() const { return called_ >= 0 && callRide_.state == 4 && journeyDone_; }
    // Put the called vehicle where the ride is, so it is DRAWN under him.
    void placeCalled(const float pos[3], float yawDeg);
    // The player's position, for `sub_456C70`'s two tests - the brake and the
    // run-over. `onRoad` is `dword_8F5E38`, which `Sliders_Tick` sets when the
    // player's ground probe lands on a mesh named `X...` or `OP...`. Cleared
    // by `clear()`; a caller that never sets it gets traffic that neither
    // brakes nor bumps, and `bumped()` stays empty.
    void setPlayer(const float pos[3], bool onRoad);
    // ...and his FACING, which `sub_456530` case 7 needs: the release tests
    // that he is not merely 300 clear but IN FRONT of the slider, against his
    // own Euler at +420. Defaulted rather than required, so a caller that
    // never sets it simply never releases - which is what happened.
    // NOTE `setPlayer` above is DEAD - nothing in the tree calls it, so
    // `playerKnown_` is always false and every player-aware arm of this class
    // (the run-over latch, the on-road test) has never run. Found while
    // wiring the release, and deliberately NOT woken here: switching it on
    // changes crowd behaviour that has nothing to do with the slider. The
    // release therefore takes its own input.
    //
    // `sub_456530` case 7 needs the rider AFTER he is out: it releases the
    // slider back to the traffic only once he is 300 clear AND in front of
    // him, against his own Euler at +420.
    void setRider(const float pos[3], float facingDeg) {
        for (int k = 0; k < 3; ++k) riderPos_[k] = pos[k];
        riderFacing_ = facingDeg;
        riderKnown_ = true;
    }
    // The vehicles that raised message 17 this tick (`Game_RaiseEvent(43,
    // {17, player, 0})`), for the Session to post. The 90-frame latch
    // (`dword_538E20` / `flt_536C28`) is inside.
    const std::vector<int>& bumped() const { return bumped_; }
    // How often a vehicle was held at a reservation group a WALKER had
    // marked, and the reverse. Nonzero only because the two classes share
    // `groupBusy_`, the way the engine's one 240-slot mover pool does.
    int  crossClassWaits(bool vehicleWaiting) const {
        return vehicleWaiting ? crossWaitVeh_ : crossWaitPed_;
    }

private:
    // THE PLAYER'S SLIDER - the one `sub_452570` reserves out of the 40.
    int         called_ = -1;
    bool        releasedTold_ = false;   // case 7 handed it back this tick
    RideMachine callRide_;
    float       callTarget_[3] = {0, 0, 0};
    bool        journeyDone_ = false;
    void placeOnLane(int vi, const SliderCall& c);

    struct ActionState {                      // one of `dword_539928`'s 48-byte records
        bool  used = false;
        float point[3] = {0, 0, 0};           // +0
        const PedClip* main = nullptr;        // +16
        const PedClip* enter = nullptr;       // +20
        const PedClip* exit = nullptr;        // +24
        int   phase = 0;                      // +28: 0 walking there, 1 enter, 2 main, 3 exit
        int   actionIndex = -1;               // +32
        int   savedClip = 0;                  // +36, restored to the point at the end
        float facing = 0.0f;                  // +40
        int   count = 0;                      // +44
    };

    // sub_453B40, over one class's lanes, with its own callback: the walkers'
    // `sub_453ED0` and the vehicles' `sub_4544B0`. The engine has one walk and
    // two callers (`Slider_Init` and `sub_4543F0`), and so has this.
    void spawnAlongLanes(std::uint32_t firstLane, std::uint32_t endLane, float spacing, bool vehicles);
    bool spawnOne(int lane, const float at[3], const float dir[3], float segLen, int keyIndex);
    // sub_4544B0: the vehicle spawn callback, and `Slider_Init`'s vehicle half
    bool spawnVehicle(int lane, const float at[3], const float dir[3], float segLen, int keyIndex);
    void loadVehicles(std::uint32_t sliMask, std::uint32_t motoMask);
    // Sliders_Tick's vehicle loop -> sub_456530 -> sub_456C70 / sub_456B40
    void tickVehicles(float dt);
    void vehicleDrive(int vi, float dt);          // sub_456C70
    void vehicleSound(int vi);                    // sub_456B40
    int  newMover();                              // the free-slot scan both callbacks open with
    // sub_454F40 and its helpers
    void moverStep(int w, float dt);
    bool stepLaneKey(const Pedestrian& m, float p[3]) const;                          // sub_455570
    void aimAtRouteStep(const Pedestrian& m, const float p[3], int step, float dir[3], float& rem) const;  // sub_4554B0
    int  aimAtLaneKey(int lane, int key, float dir[3], float& rem);               // sub_4555C0 -> the action or -1
    bool checkAhead(Pedestrian& m, const float p[3], int candidate);                  // sub_455230/2B0/340's test
    bool checkAheadOnLane(Pedestrian& m, const float p[3], int lane, int fromKey);    // sub_455340
    int  changeSegment(int w, int lane, std::uint32_t newFlags, int seg);       // sub_455680
    // sub_453230 (true = wait). `vehicle` is the ENTERING mover's class,
    // which the engine has no need of - it keeps one busy count per group -
    // and which is here only to count the CROSS-CLASS waits: a slider held
    // at a crossing by a walker, and the reverse. That number is what says
    // the two populations really share the groups; give them a counter each
    // and it is 0 by construction.
    bool reserve(const OptRoute& r, int leaving, int entering, bool vehicle);
    // sub_455830 and its helpers
    void bodyStep(int w, float dt);
    // sub_455D10. The two thresholds are the caller's: `unk_4C8880` (19.5 /
    // 58.5) for a walker, `unk_4C8888` (195 / 390) for a vehicle.
    int  gait(Pedestrian& m, const float toMover[3], float dist, float near, float far);
    void setClip(Pedestrian& m, const PedClip* c);                      // sub_455E20
    bool beginAction(Pedestrian& m, int actionIndex);                   // sub_455830's 0x80 branch
    // sub_455E90 / sub_4561B0 / sub_456250
    void actionStep(int w, float dt);
    bool advanceActionClip(Pedestrian& m, float dt, float delta[3]);
    void actionTransition(Pedestrian& m, ActionState& s);
    void finishAction(Pedestrian& m, ActionState& s);

    std::vector<int>& listFor(int lane, int seg);                   // the list a segment index lives in
    void removeFromLists(int w);
    float rand01();
    std::uint32_t rnd();

    OptTrack track_;
    PedClips clips_;
    std::vector<ModelQuota> models_;
    std::vector<Pedestrian> movers_;
    std::vector<ActionState> states_;
    std::vector<std::int16_t> actionClip_;   // the points' clip ids, cleared while a state uses one
    std::vector<std::vector<int>> laneHead_, keyList_, routeHead_;  // newest first
    std::vector<int> groupBusy_;
    std::vector<int> groupBusyVeh_;      // of those marks, the vehicles' share
    std::vector<Vehicle> vehicles_;
    int   crossWaitVeh_ = 0, crossWaitPed_ = 0;
    std::vector<int> bumped_;
    float playerPos_[3] = {0, 0, 0};
    float riderPos_[3] = {0, 0, 0};  // the rider, for case 7's release test
    float riderFacing_ = 0.0f;       // his +420
    bool  riderKnown_ = false;
    bool  playerKnown_ = false, playerOnRoad_ = false;
    float bumpHold_ = 0.0f;                  // `flt_536C28`, 90 frames
    int   bumpLatch_ = -1;                   // `dword_538E20`
    int   nSliderModels_ = 0, nMotoModels_ = 0;   // dword_539934 / dword_539930
    int   level_ = kDefaultStreetActivity;
    int   talkTarget_ = -1;
    std::uint32_t rng_ = 1u;
    std::uint32_t counter_ = 0;              // dword_4C8854
    int   nameNext_[3] = {0, 0, 0};          // dword_539940 / dword_539944
    bool  loaded_ = false;
};

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/bytes.h"
#include "actor/player.h"
#include "o3de/camobstruct.h"
#include "platform/env.h"
#include "o3de/vecmath.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <set>
#include <cstring>

namespace omk {
namespace {

using omk::le::f32;
using omk::le::u32;

// The tracks bind by KEY (`omk::meshOfTrack`, `actor/pose.h`), never by name.
// Binding by name agreed with it everywhere both resolve (0 disagreements over
// H1Avnt, F1Avnt, H1Cmbt, Meca and Sham against their models) and bound 0 of
// Sham.CTL's 484 tracks to SHU_FN, whose meshes are `Fi*`/`Shm*` where the
// tracks are `Ka*`/`Sm*` - the ridden Sham stood in its rest pose, the rider
// in a T-pose (AREA 137, `todo/scene-gameplay-audit.md` 4).

const std::string kEmpty;

// `Actor_SetHeadLook`'s ease (0x00468B50), as `aimHead` reads it: an eighth
// of the way a frame, and a step that would pass the target lands on it
float easeLook(float cur, float want, float dt) {
    const float next = cur + (want - cur) * 0.125f * dt;
    return ((want - cur) * (want - next) < 0.0f) ? want : next;
}

float wrap360(float a) {
    // Cef_ApplyTurn's own loops: subtract while > 360, add while < 0.
    while (a > 360.0f) a -= 360.0f;
    while (a < 0.0f) a += 360.0f;
    return a;
}
float wrap180(float a) {
    while (a < -180.0f) a += 360.0f;
    while (a > 180.0f) a -= 360.0f;
    return a;
}

}  // namespace

void yawSinCos(float yawDeg, float& cs, float& sn) {
    const float t = yawDeg * kDegToRadF;
    cs = std::cos(t);
    sn = std::sin(t);
}

void rotateYaw(float yawDeg, const float in[3], float out[3]) {
    // Matrix3x3_FromEulerAngles(0, y, 0) is [[cy,0,sy],[0,1,0],[-sy,0,cy]] and
    // Matrix3x3_RotateVector multiplies as a ROW vector: x' = x cy - z sy,
    // z' = x sy + z cy. The same two lines `resolveCamera` carries.
    float cs, sn;
    yawSinCos(yawDeg, cs, sn);
    rotateYawCS(cs, sn, in, out);
}

void rotateEuler(const float eulerDeg[3], const float in[3], float out[3]) {
    // `Matrix3x3_FromEulerAngles` (0x00441EB0) WHOLE, not just its yaw, and
    // `Matrix3x3_RotateVector`'s row-vector product. A camera point relative to
    // an actor is rotated by all three of his Euler angles: `sub_414F30` copies
    // the actor's +416/+420/+424 into the camera block's +112/+116/+120 and
    // `sub_415D10` hands the three to this. With the pitch and the roll at zero
    // it IS `rotateYaw` above, term for term. Two things write +416 and +424 and
    // so can tell the two apart: `applyTurn` (`Cef_ApplyTurn`, a clip's own
    // turn) and the hurt shove.
    const double sx = std::sin(eulerDeg[0] * kDegToRad), cx = std::cos(eulerDeg[0] * kDegToRad);
    const double sy = std::sin(eulerDeg[1] * kDegToRad), cy = std::cos(eulerDeg[1] * kDegToRad);
    const double sz = std::sin(eulerDeg[2] * kDegToRad), cz = std::cos(eulerDeg[2] * kDegToRad);
    const double m[9] = {cz * cy,                -(sz * cy),              sy,
                         sy * sx * cz + sz * cx, cz * cx - sx * sz * sy,  -(sx * cy),
                         sz * sx - sy * cz * cx, cx * sz * sy + sx * cz,  cy * cx};
    const double x = in[0], y = in[1], z = in[2];
    for (int j = 0; j < 3; ++j)
        out[j] = static_cast<float>(x * m[j] + y * m[3 + j] + z * m[6 + j]);
}

float headingFromClipRoot(std::span<const std::byte> clip, int frame) {
    // A `.3DA` clip: +0 frames, +4 tracks, 40-byte tracks at +8 with the
    // rotation keys at +32/+36 (pose.cpp, clipTracks). The root is the track
    // that carries position keys (+24/+28) - the pelvis, in every shipped
    // model - and its quaternion at key `frame + 1` is the node's own
    // rotation.
    if (clip.size() < 8) return 0.0f;
    const int n = static_cast<int>(u32(clip, 4));
    const int frames = static_cast<int>(u32(clip, 0));
    if (n <= 0 || n >= 512 || frames <= 0) return 0.0f;
    for (int i = 0; i < n; ++i) {
        const std::size_t o = 8u + 40u * static_cast<std::size_t>(i);
        const int pk = static_cast<int>(u32(clip, o + 24));
        const std::size_t po = u32(clip, o + 28);
        const int rk = static_cast<int>(u32(clip, o + 32));
        const std::size_t ro = u32(clip, o + 36);
        if (!po || pk <= 0 || !ro || rk <= 0) continue;
        int key = frame + 1;
        if (key < 0) key = 0;
        if (key >= rk) key = rk - 1;
        const std::size_t q = ro + 16u * static_cast<std::size_t>(key);
        if (q + 16 > clip.size()) return 0.0f;
        // stored as the conjugate of the rotation to apply (pose.h)
        const Quatf rot{f32(clip, q), -f32(clip, q + 4),
                        -f32(clip, q + 8), -f32(clip, q + 12)};
        const float fwd[3] = {0.0f, 0.0f, -1.0f};
        float v[3];
        qrot(rot, fwd, v);
        return wrap360(std::atan2(v[2], v[0]) * kRadToDegF + 90.0f);
    }
    return 0.0f;
}

// --------------------------------------------------------------- setup

// `walk_set.cpp`: start on the floor UNDER the authored position, probed
// downward from one unit above it - probing up from far below finds the
// ceiling. A pelvis point (a scene clip's root) and a feet point (an
// ADDRESSES record) both land on the same floor this way.
// ONE UNIT ABOVE IS `p[1] - 2`: `floorUnder` takes a surface more than a unit
// below its origin, so `p[1] - 1` asked for one strictly below the point - and
// a point already ON a floor (the walker's own, saved and put back) then kept
// or missed it on the float rounding of its height, and missing it put him on
// whatever lay under it.
static double seatOnFloor(const TriangleSoup& soup, const float p[3]) {
    if (const auto g = floorUnder(soup, p[0], p[1] - 2.0, p[2])) return *g;
    return p[1];
}

std::vector<std::array<double, 3>> sweptBodyCentres(const std::vector<CollisionSphere>& sph,
                                                    double radius, double lift) {
    double top = 0.0;
    for (std::size_t k = 0; k < sph.size(); ++k) {
        const double t = static_cast<double>(sph[k].pos[1]) - static_cast<double>(sph[k].radius);
        if (k == 0 || t < top) top = t;
    }
    const double highest = top + radius;         // y down: no centre above this
    std::vector<std::array<double, 3>> centres;
    for (const auto& c : sph)
        centres.push_back({0.0, std::max(static_cast<double>(c.pos[1]), highest) - lift, 0.0});
    return centres;
}

void PlayerController::placeAt(const float pos[3], float facing) {
    walker_.moveTo(pos[0], seatOnFloor(walker_.soup(), pos), pos[2]);
    for (int k = 0; k < 3; ++k) pos_[k] = static_cast<float>(walker_.pos()[k]);
    for (int k = 0; k < 3; ++k) start_[k] = pos_[k];
    euler_[1] = wrap360(facing);
}

// The rider's, and the difference is the whole point: no `seatOnFloor`.
void PlayerController::rideAt(const float pos[3], float facing) {
    walker_.moveTo(pos[0], pos[1], pos[2]);
    for (int k = 0; k < 3; ++k) pos_[k] = static_cast<float>(walker_.pos()[k]);
    for (int k = 0; k < 3; ++k) start_[k] = pos_[k];
    euler_[1] = wrap360(facing);
}

// `Cef_FindGroupById(actor+180, id)` then `SetPersoBankGroup(actor+396, g)` -
// the pair MDACTION ends on (`loc_46AFD0`: id 0x2D = 45) to carry the machine
// out of the action state and into the group whose entry is MDGETOBJ. It is
// the handler that switches the group, not a transition: entry 24, the
// MDACTION state, has NO children at all.
bool PlayerController::enterGroupById(int id) {
    const int g = rt_.channel().findGroupById(id);
    if (g < 0) return false;
    // `SetPersoBankGroup` -> `GoToMove` -> `Actor_PlayClip`: the previous
    // frame (+192) is ZEROED, so the next tick's delta runs from 0 and takes
    // the clip's key 1 - which H_SLDOUT spends on its whole drop into the
    // seat (+12.49). And the node is snapped onto the actor.
    clipEntered_ = true;
    nodeDrop_ = 0.0f;
    return rt_.channel().setBankGroup(g);
}

void PlayerController::primeClipEntry() {
    if (!clipEntered_ || !channelOnly_) return;
    clipEntered_ = false;
    nodeDrop_ = 0.0f;
    float local[3] = {0, 0, 0};
    if (const RootTrack* rt = rootTrackOf(clip())) rootDelta(*rt, 0.0f, rt_.channel().frame(), local);
    float world[3];
    rotateByFacing(local, world);
    nodeDrop_ += world[1];
    const double* w = walker_.pos();
    walker_.moveTo(w[0] + world[0], w[1], w[2] + world[2]);
    pos_[0] = static_cast<float>(walker_.pos()[0]);
    pos_[1] = static_cast<float>(walker_.pos()[1]);
    pos_[2] = static_cast<float>(walker_.pos()[2]);
}

// ---- THE JUMP -----------------------------------------------------------
//
// `MDJUMP0A` (0x0046BB50), read from the image - it has no `proc` label:
//
//     dword_6A52CC = 1                      -- the airborne flag
//     dword_53AE40/44/48 = actor +244/+248/+252   -- the take-off, latched
//     N = sub_45AC60(actor+396)             -- u32(entry, 12) on the LIVE
//                                              `.CTL` entry
//     sub_47DF00(entry, N >> 1)             -- "SetITPNbFrames", the binary's
//                                              own name: N/2 IS a frame count
//     Matrix3x3_RotateVector(0, 0, -dword_910348, actor+288, &X, &Y, &Z)
//                                           -- dword_910348 = 98.4252 = 2.5 m,
//                                              along -Z, which is forward
//     flt_53AE50 = X / N                    -- per frame
//     flt_53AE54 = -(actor[228] * (N/2) * (1/30))
//     flt_53AE58 = Z / N
//
// then `MDJUMP01` (0x0046BD50) is six instructions that move those into the
// actor: `+216 = flt_53AE50`, `+220 = flt_53AE54 * 30.0`, `+224 = flt_53AE58`.
// The 30 and the 1/30 cancel, so the launch is exactly `-g * N/2` per second -
// the ballistic speed for N frames of hang, zero at the apex on N/2.
//
// **N IS `entry+12` AND THAT FIELD HAS A SECOND CONSUMER**, which is why this
// is labelled rather than called settled. `ctl.h` reads its low half as a ROLE
// (`Fight_Begin` caches six codes off it in the combat banks). In `H1AVNT` it
// is **14 on exactly the five jump entries** - H_SDJUMP, H_WKJUMPL, H_WKJUMPR,
// H_RLJUMP, H_RRJUMP - and **0 on every other state that owns a clip**, and
// its high half is 0 everywhere, so nothing here reads as a role. Two things
// corroborate it as a duration: `SetITPNbFrames` takes its half, and the
// engine has no other route to a length (a clip's frame count lives on the
// clip, not the entry). The alternative that was tested and does NOT hold is
// "N is the clip's own frame count": the five jump clips run 8, 8, 10, 10 and
// 19 frames and none of them is 14.
//
// What it produces for Kay'l: 2.5 m forward and **12.0 units (0.30 m) up**
// over 14 frames (0.47 s). That is a flat running LEAP, not a vertical hop -
// and the 0.30 m is `dword_910340`, the step-up constant, to three figures.
bool PlayerController::jumpPrepare() {
    const int st = rt_.channel().state();
    if (st < 0 || st >= static_cast<int>(ctl_->states.size())) return false;
    const double n = static_cast<double>(ctl_->states[static_cast<std::size_t>(st)].flags12);
    if (!(n > 0.0)) return false;             // no hang time authored: not a jump entry
    const double yaw = euler_[1] * kPi / 180.0;
    // `Matrix3x3_RotateVector(0, 0, -L)` by the facing: the engine's forward is
    // -Z and maps to (sin y, 0, -cos y) (`sub_456530` case 7 tests exactly
    // that against the actor's own +420).
    const double L = 98.4252;                 // dword_910348, 2.5 m
    jumpV_[0] = L * std::sin(yaw) / n;
    jumpV_[2] = -L * std::cos(yaw) / n;
    jumpV_[1] = -kGravity * n * 0.5;          // actor+228, by Actor_LoadModel
    jumpArmed_ = true;
    return true;
}

bool PlayerController::jumpLaunch() {
    if (!jumpArmed_) return false;
    jumpArmed_ = false;
    return walker_.jump(jumpV_[1], jumpV_[0], jumpV_[2]);
}

// The breath groups by ID: H1Avnt's 164 (H_RUN-SD -> H_BREATH -> H_STAND)
// and 166 (H_WK-SD -> H_BREATH -> H_STAND).
bool PlayerController::breathMove(const std::string& mv) {
    if (mv == "MDRUN")  { runLoops_  = static_cast<std::uint32_t>(rt_.channel().repeat()); return false; }
    if (mv == "MDWALK") { walkLoops_ = static_cast<std::uint32_t>(rt_.channel().repeat()); return false; }
    bool took = false;
    if (mv == "MDSTOPR") {
        if (runLoops_ > 30) took = enterGroupById(164);
        runLoops_ = 0;
    } else if (mv == "MDSTOPW") {
        if (runLoops_ > 30 && walkLoops_ < 3) took = enterGroupById(166);
        runLoops_ = 0;
        walkLoops_ = 0;
    }
    return took;
}

// ---- THE LANDING, `MDJUMP03` (0x0046BE40) --------------------------------
//
//     dword_6A52CC = 0                          -- the airborne flag
//     d = actor[264] - actor[276]               -- the CLEARANCE, the same
//                                                  pair `Walk_GroundResponse`
//                                                  takes its `v67` from
//     d >= 196.85039 (5.00 m)      -> +1304 = 4
//     d >= 118.11024 (3.00 m)      -> +1304 = 3
//     d >= dword_910350 (1.50 m)   -> +1304 = 1
//     otherwise                    -> +1304 = 2 and RETURN
//     then, for 4 / 3 / 1 only:
//         sub_465340(actor, 2)                  -- bank group 2 by id, unless
//                                                  ACTOR_STATE is 2, 3 or 15
//         sub_414DE0(actor, 18, 1)              -- CAMERA 18, travel 60
//
// (`todo/falls.md` 1): this line is NOT an ACTOR_STATE write - there is no
// ACTOR_STATE 18. `sub_414DE0` is a camera request with the actor as both subjects; the
// frontend makes it, since the camera is the frontend's.
//
// So a short landing is silent and a long one plays the landing reaction. Note
// the band order is 2, 1, 3, 4 with distance: `+1304` is a CODE, not a
// severity rank, and 2 is the one that returns early.
//
// **THESE ARE NOT THE WALKER'S FOUR BANDS.** `land()` bands `fall_` at
// 20 cm / 1.50 m / 3.00 m, which is `Walk_GroundResponse`'s ordinary fall;
// MDJUMP03 bands at 1.50 / 3.00 / 5.00 m. Two different tables for two
// different events, so this does not reuse `lastLandingTier()`.
//
// **ONE SUBSTITUTION, LABELLED**: the engine bands the CLEARANCE
// `actor[264] - actor[276]` and `actor+276` is untraced (`todo/player-vertical.md`
// §4). This bands the walker's own accumulated drop instead. For a jump the
// two coincide whenever he lands at or below the height he left, which is
// every case the thresholds are about - a flat leap on level ground falls
// 9 units from the apex and lands in band 2, silent, which is right.
int PlayerController::jumpLand() {
    const double d = walker_.lastLandingDrop();
    const int band = jumpBand(d);
    if (band == 2) return band;                 // short: no reaction
    enterGroupById(2);                          // sub_465340(actor, 2)
    return band;                                // camera 18 is the caller's
}

bool PlayerController::goToMove(int groupId) {
    if (!enterGroupById(groupId)) return false;
    euler_[0] = 0.0f;                          // +416, the pitch
    // +216..+224, +280/+284, +1304 and dword_6A52CC are the engine's carried
    // motion state; this controller derives its motion from the clip every
    // tick and carries none between frames, so there is nothing to clear.
    // The facing matrix is rebuilt from euler_ wherever it is read.
    return true;
}

// ---- IN THE WATER, `sub_4A8F30` (0x004A8F30) - `todo/swimming.md` 3 --------
//
// `Actor_ApplyMotion` sends ACTOR_STATEs 11..14 here INSTEAD of gravity and the
// ground probe, and every state ends in `sub_4A9470`: the frame's move -
// VERTICAL INCLUDED, since +248 takes the delta in these states - through
// `Actor_Move`, which the port stands in for with the horizontal wall sweep and
// the floor under the water as a limit.
//
//   11, 12  the move alone (12 skips even the collide).
//   13      AT THE SURFACE. The probe starts at the head node's TOP (`actor+16`,
//           its y plus its mesh's box min, `sub_437D60`): over the water surface
//           more than 11.811 below that point, sink him until it is 11.811;
//           otherwise, not diving (`+1288 & 2`), cast up to the surface and lift
//           the same point to 11.811 above it. So he floats with the crown of
//           his head 30 cm out of the water.
//   14      UNDERWATER. The probe starts at the head node itself:
//             - the water surface under it: his head is OUT - with clearance he
//               is pulled back down by it;
//             - the bed (0x8000000): drift UP 0.15 a frame, pitch +0.2;
//             - nothing: still under;
//             - any other floor: he has left the water - bank group 301,
//               ACTOR_STATE 13, pitch 0, the dive flag cleared, the breath
//               reset, MESSAGE 21;
//           while under, the pitch is held in 200..340 (under 180 it is reset
//           to 270), and THE BREATH runs: 40 000 ms, MESSAGE 12 once when it is
//           spent. `Hud_DrawBar` mode 1 is the frontend's (step 4).
//
// **Reconstruction, labelled.** The head node's point comes from this model's
// rest pose (`headLift_` over `camLift_`, the crown above the feet) rather than
// the posed node; the pitch turns the root motion but is not yet applied to the
// drawn body (step 3b); `Actor_Move`'s 3D collide is the horizontal sweep plus
// the floor limit.
void PlayerController::waterTick(double dx, double dy, double dz, float dt) {
    const int st = static_cast<int>(rt_.state());
    const double crown = static_cast<double>(camLift_ + headLift_);   // feet -> crown
    const double* w = walker_.pos();
    double push[2] = {0.0, 0.0};
    if (st != 12) walker_.slide(dx, dz, push);
    double nx = w[0] + dx, ny = w[1] + dy, nz = w[2] + dz;
    // the bed is a floor he does not pass (Y grows downward) - probed from where
    // he WAS, since a move that already crossed it would find nothing under him
    // ...and it stops a body COMING DOWN onto it, nothing else. A rule of
    // "below the nearest floor: put him on it" would also SNAP UP a body that is
    // legitimately under a floor's level - the climb out, whose feet come up the
    // quay's face from 70 cm under its top: the moment his drift carries him over
    // the edge he is lifted ~11 units and `H_WO_SD`'s remaining rise goes on top,
    // so he ends that much over the platform and falls to it (seen in play). How
    // early he crosses the edge depends on the approach, which is why a scripted
    // climb can land exactly and a played one not.
    if (const auto bed = walker_.floorThroughWater(nx, w[1] - kStepUp - 1.0, nz))
        if (w[1] <= *bed + 0.5 && ny > *bed) ny = *bed;

    if (st == 13) {
        const double top = ny - crown;
        std::uint32_t fl = 0;
        const auto h = walker_.probeFlags(nx, top, nz, fl);
        if (h && (fl & 0x20000000u)) {
            const double d = *h - top;
            if (d > 11.811024) ny += d - 11.811024;
        } else if (!(waterFlags_ & 2u)) {
            if (const auto s = walker_.surfaceAbove(nx, ny, nz))
                ny = *s - 11.811024 + crown;
        }
    } else if (st == 14) {
        // ---- `sub_4A8F30` case 14 WHOLE ------------------------------------
        //
        // The engine probes from the NODE - `v15 = actor+16` then `v15[11..13]`,
        // the o3de node's own world position (which node: below) - and
        // `World_ProbePoint` boxes from there with `+INF` for the Y maximum, so
        // it is the nearest surface BELOW him and the distance comes back in the
        // out-param. Probed from a point of the port's own invention
        // (`camLift_ + 0.8 * headLift_`) he is pinned a head's height too low,
        // and swimming up does nothing visible: the up/down animation plays, and
        // each time its loop restarts his position is reset.
        //
        // Then the three arms, in the engine's own order:
        //  * the nearest floor below is the WATER SURFACE (0x20000000), which
        //    means he has risen above the water line. At a distance of 0 or
        //    less nothing happens. Otherwise a SECOND probe two metres above
        //    him (`v34 - 78.740158`, the body sphere's radius, reach 7.874):
        //      - no water up there: he is out of the water, so he is moved
        //        DOWN by the distance - pinned at the surface - and the tick
        //        ends without the clamp or the breath;
        //      - water up there as well: he SURFACES (below).
        //  * the nearest floor below is the BED (0x8000000): up 0.15 a frame
        //    and the pitch turns +0.2, and he stays under.
        //  * anything else under him - a bank, a step, the quay: he SURFACES.
        // THE PROBE POINT IS THE HEAD, AS POSED. `actor+16` is what
        // `Actor_LoadModel` fills with `o3de_FindMeshByName(root, "Tete")`, and
        // the arm reads that node's WORLD position - not the root (the pelvis),
        // and not a standing head's height over the feet, which for a PRONE
        // swimmer is a point in the water far above his real head. The head
        // is the pelvis plus the rest offset turned by his whole Euler
        // - level with him and ahead of him when he lies flat. LABELLED: the
        // clip's own bend of the neck and spine is not in it.
        const float headRest[3] = {0.0f, -0.8f * headLift_, 0.0f};
        float headW[3];
        rotateEuler(euler_, headRest, headW);
        const double hx = nx + static_cast<double>(headW[0]);
        const double hz = nz + static_cast<double>(headW[2]);
        const double node = ny - static_cast<double>(camLift_) + static_cast<double>(headW[1]);
        std::uint32_t fl = 0;
        auto h = walker_.probeFlags(hx, node, hz, fl);
        // A PORT GUARD, labelled: the engine probes EVERY mesh of the sector and
        // this probes the walkable floor alone, so a head held against a canal
        // wall is over nothing at all and every water rule went quiet - he rose
        // out of the water in ACTOR_STATE 14. With nothing under the head, what
        // is under the body answers.
        if (!h) h = walker_.probeFlags(nx, node, nz, fl);
        bool under = true, done = false;
        if (omk::envFlag("OMK_SWIMTRACE"))
            std::printf("  waterprobe: head at %.1f %.1f %.1f -> %s y %.1f flags %08x\n", hx, node, hz,
                        h ? "hit" : "NOTHING", h ? *h : 0.0, static_cast<unsigned>(fl));
        if (h && (fl & 0x20000000u)) {
            const double d = *h - node;
            if (d <= 0.0) { done = true; }
            else {
                // THE SECOND PROBE GOES DOWN, from two metres over his head
                // (`sub_443560` boxes y from `head - 78.74` to FLT_MAX, fat by the
                // body sphere's radius; its 7.874 is a tolerance between two hits,
                // not a reach). What it meets FIRST decides: the water means two
                // metres of clear air over him, and he SURFACES; anything else - a
                // sewer's roof - or nothing means he cannot come up here, and he is
                // put back down by the distance. Read the other way round ("water
                // above him"), it pins him under open sky for ever and he can
                // never reach the surface.
                std::uint32_t fl2 = 0;
                const auto h2 = walker_.probeFlags(hx, node - 78.740158, hz, fl2);
                const bool clearAbove = h2 && (fl2 & 0x20000000u);
                if (clearAbove) under = false;                  // up he comes
                else { ny += d; done = true; }                  // a roof: back under
            }
        } else if (h && (fl & 0x8000000u)) {
            ny -= 0.15 * static_cast<double>(dt);
            euler_[0] += 0.2f * dt;
        } else if (h) {
            under = false;
        }
        if (!done) {
        if (under) {
            if (euler_[0] < 180.0f) euler_[0] = 270.0f;
            else {
                if (euler_[0] < 200.0f) euler_[0] = 200.0f;
                if (euler_[0] > 340.0f) euler_[0] = 340.0f;
            }
            if (breathMs_ < 0.0) breathMs_ = 0.0;               // Hud_Refresh here
            breathMs_ += static_cast<double>(dt) * 1000.0 / 30.0;
            if (breathMs_ > 40000.0 && !drowned_) {
                drowned_ = true;
                waterMsgs_.push_back(12);
            }
        } else {
            enterGroupById(301);
            rt_.setState(ActorState::Surface13, "sub_4A8F30");
            euler_[0] = 0.0f;
            waterFlags_ &= ~2u;
            breathMs_ = -1.0;
            drowned_ = false;
            waterMsgs_.push_back(21);
        }
        }
    }
    walker_.moveTo(nx, ny, nz);
    last_.step = StepResult::Moved;
}

int PlayerController::waterClimbOut(float* platformOverWater) {
    const double* w = walker_.pos();
    const double crown = static_cast<double>(camLift_ + headLift_);
    std::uint32_t fl = 0;
    const auto surf = walker_.probeFlags(w[0], w[1] - crown, w[2], fl);
    if (!surf || !(fl & 0x20000000u)) return 1;
    const double from = *surf - 78.740158;                   // two metres over the water
    const double yaw = static_cast<double>(euler_[1]) * kDegToRad;
    const double sx = std::sin(yaw) * 31.496063 * 0.2, sz = std::cos(yaw) * 31.496063 * 0.2;
    double px = w[0], pz = w[2], platform = 0.0;
    int k = 0;
    for (; k < 5; ++k) {
        px += sx; pz -= sz;
        std::uint32_t f2 = 0;
        const auto h = walker_.probeFlags(px, from, pz, f2);
        if (h && !(f2 & 0x28000000u)) { platform = *h; break; }
    }
    if (k == 5) return 2;
    const double over = *surf - platform;                    // Y grows downward
    if (platformOverWater) *platformOverWater = static_cast<float>(over);
    if (over > 29.527559 || over < 0.0) return 3;
    const double bx = (px - w[0]) * 0.2, bz = (pz - w[2]) * 0.2;
    int j = 0;
    for (; j < 5; ++j) {
        px -= bx; pz -= bz;
        std::uint32_t f2 = 0;
        const auto h = walker_.probeFlags(px, from, pz, f2);
        if (h && (f2 & 0x20000000u)) break;
    }
    if (j == 5) return 4;
    // THE ENGINE'S POSITION IS THE NODE - the PELVIS - and this port's is the
    // FEET, `camLift_` below it. `sub_4A9580` puts the pelvis 27.56 (70 cm) under
    // the platform; putting the FEET there would start him 42 units too high,
    // and `H_WO_SD`'s root, which rises 79 to stand him up, would then carry him
    // 51 over the quay to drop 1.2 m when the clip ends. With the pelvis there
    // the same rise ends ~10 over the platform, which is the engine's own
    // arithmetic.
    walker_.moveTo(px - bx * 2.5, platform + 27.559055 + static_cast<double>(camLift_),
                   pz - bz * 2.5);
    for (int c = 0; c < 3; ++c) pos_[c] = static_cast<float>(walker_.pos()[c]);
    rt_.setState(ActorState::Scripted12, "sub_4A9580");
    euler_[0] = 0.0f;
    waterFlags_ &= ~2u;
    breathMs_ = -1.0;
    enterGroupById(303);
    return 0;
}

int PlayerController::ctlGroupId() const {
    const int g = ctlGroup();
    if (g < 0 || g >= static_cast<int>(ctl_->groupList.size())) return -1;
    return ctl_->groupList[static_cast<std::size_t>(g)].id;
}

int PlayerController::ctlGroup() const {
    const int s = rt_.channel().state();
    if (s < 0 || s >= static_cast<int>(ctl_->states.size())) return -1;
    return ctl_->states[static_cast<std::size_t>(s)].group;
}

void PlayerController::nudge(const float d[3]) {
    // THE PUSH IS SWEPT, not placed. `Actor_TickNpc` adds the
    // spatial query's push to +244..252 outright, but `Actor_ApplyMotion`
    // right after takes EVERYTHING since the last safe position (+232..240) -
    // the push included - undoes it and hands it to `Actor_Move`, the
    // collide-and-slide, then puts him back at the safe position if no floor
    // is under him. So a push never crosses a wall - which `moveTo`, testing
    // nothing, would let it do. LABELLED: the engine sweeps push
    // and the frame's own motion as ONE delta; this sweeps the push first.
    // The push is horizontal (both per-entry tests write y = 0).
    // ...and an AIRBORNE actor's push carries NO TIME: the frame's own
    // `Actor_ApplyMotion` is his one gravity tick, and with dt 1.0 he fell one
    // extra frame (todo/drift-audit.md T7). On the GROUND it keeps the frame's
    // dt: the engine's single delta spends the frame's gravity on a drop within
    // it and simply follows it, where dt 0 made a hundredth of a unit's drop a
    // fall of no speed and a landing the next tick - a parked slider's shove,
    // `engine: slider journey`. LABELLED: a push off a drop bigger than that
    // gravity still has the extra tick, for that one frame.
    walker_.step(d[0], d[2], walker_.airborne() ? 0.0 : 1.0);
    for (int k = 0; k < 3; ++k) pos_[k] = static_cast<float>(walker_.pos()[k]);
}

bool PlayerController::moveBy(float dx, float dz) {
    // `Actor_Move` is the collide-and-slide alone - gravity is
    // `Actor_ApplyMotion`'s - so an airborne walker's step takes no time (dt 1.0
    // was an extra fall frame, todo/drift-audit.md T7); on the ground it keeps
    // the frame's dt, for the reason `nudge` gives.
    const StepResult r = walker_.step(dx, dz, walker_.airborne() ? 0.0 : 1.0);
    for (int k = 0; k < 3; ++k) pos_[k] = static_cast<float>(walker_.pos()[k]);
    return r == StepResult::Moved || r == StepResult::Slid;
}

// `Actor_LoadBankList` (0x00419CB0) as a fight needs it: the bank list is
// swapped and the channel lands on the new bank's default entry, and nothing
// touches the body. The engine does not rebuild an actor to change his bank -
// `fight.begin` moves both fighters to `.CTL` slot 2 mid-scene and the player
// keeps standing where he stood - so this mirrors the constructor's channel
// half (`rt_.loadModel()` then `SetPersoBankGroup(Cef_DefaultGroup)`) and
// leaves `pos_`, `euler_`, `walker_` and the camera state as they are.
void PlayerController::setBank(const CtlFile& ctl, std::span<const std::byte> data) {
    ctl_ = &ctl;
    data_ = data;
    rt_ = ActorRuntime(ctl, true);
    rt_.loadModel();
    const int g = rt_.channel().defaultGroup();
    if (g >= 0) rt_.channel().setBankGroup(g);
    frameBefore_ = frameAfter_ = rt_.channel().frame();
    stateBefore_ = rt_.channel().state();
    // ...AND EVERY CLIP-KEYED CACHE IS NOW WRONG. `clipTracks`, `rootOf` and
    // the variant grid all memoise on the clip INDEX, and an index means
    // something different in every bank: clip 0 of `H1AVNT` is Kay'l's idle
    // and clip 0 of `H1CMBT` is his guard. Left alone, the fight teardown puts
    // the channel back on the adventure bank - the state, the group and the
    // frame all correct - while the POSE keeps coming out of the combat bank's
    // cached tracks, so the winner of a fight walks around Anekbah still
    // holding his fists up (`todo/fight-mode.md` 15.6).
    //
    // Nothing in the engine has this cache to invalidate: `Actor_LoadBankList`
    // swaps the list and the clips are read through it, so this is a cost the
    // port's memoisation creates and the port's bank swap has to pay.
    tracks_.clear();
    roots_.clear();
    gridBlend_.tracks = NodeTracks{};
    gridBlend_.clip = gridBlend_.gen = -1;
    // The track table is counted per bank, so it is re-counted here for the
    // same reason the constructor counts it: a bank whose tracks do not name
    // the model's meshes poses nothing, and the number is how that is seen.
    total_ = matched_ = 0;
    if (meshes_) {
        for (std::size_t c = 0; c < ctl_->clips.size(); ++c) {
            const auto d = animDescriptor(data_, ctl_->clips[c].offset);
            if (!d) continue;
            for (const auto& t : d->tracks) {
                ++total_;
                if (meshOfTrack(t, *meshes_) >= 0) ++matched_;
            }
        }
    }
}

PlayerController::PlayerController(const Setup& s)
    : ctl_(s.ctl), data_(s.ctlData), meshes_(s.meshes),
      rt_(*s.ctl, true),
      walker_(*s.soup, s.pos[0], seatOnFloor(*s.soup, s.pos), s.pos[2]) {
    // Actor_LoadModel -> Actor_LoadBankList: state 1 on the bank's default
    // group; then the cutscene had him (ScriptObject_StartOnActor -> 4) and
    // its program ended (Actor_TickScxDriven -> 1, +1308 set), which is the
    // hand-over this controller starts at.
    rt_.loadModel();
    rt_.scxStart();
    rt_.scxDrivenDone();
    walker_.setSteep(s.steep);
    walker_.setBlockers(s.blockers, s.sweepRadius);   // rebound below once camLift_ is known
    // Actor_TickNpc's +1308 pass: the facing is already derived by the caller
    // (headingFromClipRoot); `SetPersoBankGroup(channel, Cef_DefaultGroup)`
    // resets the machine to the default group's default entry.
    const int g = rt_.channel().defaultGroup();
    if (g >= 0) rt_.channel().setBankGroup(g);

    // Onto the floor: the walker was seated by `seatOnFloor`.
    for (int k = 0; k < 3; ++k) pos_[k] = static_cast<float>(walker_.pos()[k]);
    for (int k = 0; k < 3; ++k) start_[k] = pos_[k];
    euler_[0] = 0.0f; euler_[1] = wrap360(s.facing); euler_[2] = 0.0f;

    frameBefore_ = frameAfter_ = rt_.channel().frame();
    stateBefore_ = rt_.channel().state();

    // The track table, counted once: every track of every clip the bank
    // names must resolve to a mesh of the model by its key.
    if (meshes_) {
        for (std::size_t c = 0; c < ctl_->clips.size(); ++c) {
            const auto d = animDescriptor(data_, ctl_->clips[c].offset);
            if (!d) continue;
            for (const auto& t : d->tracks) {
                ++total_;
                if (meshOfTrack(t, *meshes_) >= 0) ++matched_;
            }
        }
    }
    // THE CAMERA'S SUBJECT IS A BODY POINT, not the floor point `pos_` is.
    // The model's hierarchy root is the pelvis (`pose.cpp`: track 2,
    // `UBassin`), and this is its height above the model's lowest extent -
    // 41.9 for `HO1_FNM`, the same number the dialogue staging measured from
    // the other side. Y points DOWN, so the feet are the LARGER y.
    if (meshes_ && !meshes_->empty()) {
        float feet = -1e30f;
        for (const auto& m : *meshes_)
            feet = std::max(feet, m.pos[1] + m.boxMax[1]);
        int root = 0;
        for (std::size_t i = 0; i < meshes_->size(); ++i)
            if ((*meshes_)[i].parent < 0) { root = static_cast<int>(i); break; }
        camLift_ = feet - (*meshes_)[static_cast<std::size_t>(root)].pos[1];
        if (!(camLift_ > 0.0f) || camLift_ > 200.0f) camLift_ = 0.0f;  // refuse a wild one
        // ...and THE SHOOT CAMERA'S LIFT, which is the engine's own rule and
        // not a reconstruction. `sub_414520` case 4 - the mode `Shoot_Enter`
        // requests - computes it as
        //
        //     lift = 0.7 * actor[+276]
        //
        // and `+276` is written by `Actor_LoadModel`, which walks the model's
        // SPHERE list taking `max(centre.y + radius)`: the lowest point of
        // the body below the actor's origin. So the eye rides seven tenths of
        // the way from the pelvis down to the feet - measured with the
        // spheres, not the bones, which is why it is a little more than the
        // bone span alone.
        //
        // (`case 4` falls into `case 5`, which recomputes the same value; and
        // the whole thing is gated on the actor's `+16`, which this port does
        // not model - so the lift is unconditional here.)
        // ...and `camLift_` IS that number: it is already "the root above the
        // model's lowest extent", 41.9 for HO1_FNM against the sphere table's
        // own 41.81. So the lift needs no new data.
        headLift_ = 0.7f * camLift_;

        // WHERE THAT PUTS THE EYE, and it is the check that the constant is
        // understood rather than merely copied: HO1_FN's four body spheres
        // (all radius 10.91) run from `y+r = 41.81` at the feet to
        // `y-r = -29.02` at the crown, so 0.7 of the lower extent is **29.27**
        // and the crown is **29.02** above the origin. They agree to 0.9%.
        // The engine's rule puts the first-person eye AT THE TOP OF THE HEAD,
        // which is what the 0.7 is for.
        if (!(headLift_ > 0.0f) || headLift_ > 200.0f) headLift_ = 0.0f;
    }
    // THE SWEPT BODY: the model's sphere list sits about the PELVIS (the
    // actor's node), and the walker's origin is the feet, `camLift_` below it
    // (y grows down: pelvis.y = feet.y - camLift_). Each centre becomes an
    // offset from the feet ON THE NODE'S VERTICAL: `Actor_Move` (0x00469580)
    // walks the list reading the y (`+252 + 16k`) and the radius (`+260 +
    // 16k`) and NOTHING ELSE - the lowest `y - r`, the highest `y + r`, the
    // largest `r` - so its capsule is a vertical segment through the node and
    // a sphere's x and z are never read. They were kept here, unturned, which
    // HO1_FN's -0.3 / -1.9 hid; the Sham's (SHU_FN) three lie along its body
    // at z -7.5, -37.2 and -75.3, and the rider was swept 75 units off in a
    // fixed world direction - jammed in the pen's gap whatever his facing
    // (`todo/scene-gameplay-audit.md` row 4).
    //
    // AND THE TOP IS THE BAND'S. `Actor_Move` folds the list into the least
    // `y - r` and one radius, the largest, so a small sphere at the crown
    // swept with that radius would put the head ABOVE the model's own top.
    // HO1_FN's and SHU_FN's radii are all equal, which hid it; MCG_FN's are
    // 25.1, 19.2, 13.4 and 20.7, and its crown sphere (y -17.2, r 13.4) swept
    // at 25.1 stood 116.1 above his feet - against the 110.2 of the only
    // doorway out of AREA 63's shoot start ('Depart Shoot 03'), which the
    // Mecagarde must take. RECONSTRUCTION, labelled: `Sweep_PolygonKernel` is
    // not transcribed (collision.h), and a capsule `[top, bottom] +- R` would
    // stand 129.6 tall - ruled out by the same doorway; the band's own 104.5
    // is what the data leaves. So a centre sits no higher than `top + R`.
    if (!s.sweepSpheres.empty() && s.sweepRadius > 0.0f) {
        auto centres = sweptBodyCentres(s.sweepSpheres, s.sweepRadius, camLift_);
        walker_.setBlockers(s.blockers, s.sweepRadius, std::move(centres));
    }
    {
    }
    // the first camera frame snaps (flags & 1 after Camera_LoadParams)
    for (int k = 0; k < 3; ++k) block_.euler[k] = euler_[k];
    resolveSteady(cam_, euler_);
    block_.fresh = false;
    last_.ground = pos_[1];
    last_.onGround = true;
}

// ----------------------------------------------------------- the clip

const std::string& PlayerController::ctlStateName() const {
    const int s = rt_.channel().state();
    if (s < 0 || s >= static_cast<int>(ctl_->states.size())) return kEmpty;
    return ctl_->states[static_cast<std::size_t>(s)].name;
}

int PlayerController::clipOwner() const {
    int s = rt_.channel().state();
    const auto& S = ctl_->states;
    for (int guard = 0; guard < 64; ++guard) {
        if (s < 0 || s >= static_cast<int>(S.size())) return -1;
        if (!(S[static_cast<std::size_t>(s)].flags & ctl_entry::NoClipName)) return s;
        s = S[static_cast<std::size_t>(s)].gotoIdx;
    }
    return -1;
}

int PlayerController::clip() const {
    const int s = clipOwner();
    if (s < 0 || s >= static_cast<int>(ctl_->states.size())) return -1;
    const int c = ctl_->states[static_cast<std::size_t>(s)].clip;
    return (c >= 0 && c < static_cast<int>(ctl_->clips.size())) ? c : -1;
}

const std::string& PlayerController::clipName() const {
    const int c = clip();
    return c < 0 ? kEmpty : ctl_->clips[static_cast<std::size_t>(c)].name;
}

int PlayerController::clipFrames() const {
    const int c = clip();
    return c < 0 ? 0 : ctl_->clips[static_cast<std::size_t>(c)].frames;
}

const PlayerController::RootTrack* PlayerController::rootTrackOf(int clip) {
    if (clip < 0) return nullptr;
    auto it = roots_.find(clip);
    if (it != roots_.end()) return it->second.keys > 0 ? &it->second : nullptr;
    RootTrack r;
    if (const auto d = animDescriptor(data_, ctl_->clips[static_cast<std::size_t>(clip)].offset)) {
        // the one track with position keys - `sub_437FE0` walks the tracks
        // for the first whose position pointer is set, and the shipped
        // clips have exactly one
        for (const auto& t : d->tracks) {
            if (!t.posOffset || t.posKeys <= 0) continue;
            if (t.posOffset + 12 * std::uint64_t(t.posKeys) > data_.size()) continue;   // 64 bits: a 32-bit size_t wraps
            r.offset = t.posOffset;
            r.keys   = t.posKeys;
            break;
        }
    }
    it = roots_.emplace(clip, r).first;
    return it->second.keys > 0 ? &it->second : nullptr;
}

// Anim_RootDelta (0x004711D0), transcribed. `key(k)` is the 12-byte position
// key k; the sum runs over the keys whose interval (k-1, k] the span
// (prev, cur] covers, with the two fractional ends scaled.
void PlayerController::rootDelta(const RootTrack& t, float prev, float cur,
                                 float out[3]) const {
    const auto key = [&](int k, float* v) {
        if (k < 0) k = 0;
        if (k >= t.keys) k = t.keys - 1;
        const std::size_t o = t.offset + 12u * static_cast<std::size_t>(k);
        for (int c = 0; c < 3; ++c) v[c] = f32(data_, o + 4u * static_cast<std::size_t>(c));
    };
    float acc[3] = {0, 0, 0}, kv[3];
    const float c0 = std::ceil(prev);
    if (c0 <= cur) {
        if (c0 > prev) {                       // the fractional head
            key(static_cast<int>(c0), kv);
            for (int c = 0; c < 3; ++c) acc[c] += (c0 - prev) * kv[c];
        }
        const float f1 = std::floor(cur);
        float k = c0;
        while (k < f1) {                       // whole keys c0+1 .. f1
            key(static_cast<int>(k) + 1, kv);
            for (int c = 0; c < 3; ++c) acc[c] += kv[c];
            k += 1.0f;
        }
        if (cur > k) {                         // the fractional tail
            key(static_cast<int>(k) + 1, kv);
            for (int c = 0; c < 3; ++c) acc[c] += kv[c] * (cur - k);
        }
    } else {
        // both ends inside one key's interval
        key(static_cast<int>(c0), kv);
        for (int c = 0; c < 3; ++c) acc[c] = (cur - prev) * kv[c];
    }
    for (int c = 0; c < 3; ++c) out[c] = acc[c];
}

void PlayerController::rotateByFacing(const float in[3], float out[3]) const {
    // `Anim_RootDelta`'s 3x3 is node+156, which is actor+288 - the facing
    // matrix - unless something installed another one. `MDACTION`'s slider
    // arm installs the SLIDER's, so the door clip's authored step runs in the
    // vehicle's frame; `Matrix3x3_RotateVector` in the row-vector convention
    // is then `in.x * row0 + in.y * row1 + in.z * row2`, and row 1 is Y.
    if (haveRootFrame_) {
        for (int k = 0; k < 3; ++k)
            out[k] = in[0] * rootFrameX_[k] + in[2] * rootFrameZ_[k];
        out[1] += in[1];
        return;
    }
    // IN THE WATER +288 CARRIES THE PITCH, and EVERYTHING that goes through it
    // must be turned by it - not the root delta alone. Left on the yaw, this
    // function hands `Cef_ApplyRootShift`'s glide - which `H_SWIMIN` applies
    // over frames 19..27 of every stroke, along the upright clip's own "up" -
    // to the world UNTURNED: he rises 1.2 a tick through the strong half of
    // each stroke however far down his nose is, and sinks only in the weak
    // half, so he goes up whatever direction he is given. Scoped to the water
    // states, so no land baseline moves.
    {
        const int ws = static_cast<int>(rt_.state());
        if (ws >= 11 && ws <= 14) { rotateEuler(euler_, in, out); return; }
    }
    rotateYaw(euler_[1], in, out);
}

void PlayerController::setRootFrame(const float localX[3], const float localZ[3]) {
    for (int k = 0; k < 3; ++k) { rootFrameX_[k] = localX[k]; rootFrameZ_[k] = localZ[k]; }
    haveRootFrame_ = true;
}

// `MDACTION`'s slider arm, the placement half - see the header.
bool PlayerController::boardOffset(std::span<const std::byte> refClip, int groupId,
                                   float out[3]) {
    float ref[3];
    if (!clipRootStart(refClip, ref)) return false;
    const int g = rt_.channel().findGroupById(groupId);
    if (g < 0) return false;
    const int e = ctl_->groupList[static_cast<std::size_t>(g)].defaultEntry;
    if (e < 0 || e >= static_cast<int>(ctl_->states.size())) return false;
    const int clip = ctl_->states[static_cast<std::size_t>(e)].clip;
    const RootTrack* t = rootTrackOf(clip);
    if (!t) return false;
    for (int k = 0; k < 3; ++k)
        out[k] = f32(data_, t->offset + 4u * static_cast<std::size_t>(k)) - ref[k];
    return true;
}

void PlayerController::applyTurn(const float d[3]) {
    // Cef_ApplyTurn: add, then wrap +416 and +420 into 0..360 (+424 is
    // added and not wrapped - the original wraps v2[104] and v2[105] only).
    euler_[0] = wrap360(euler_[0] + d[0]);
    euler_[1] = wrap360(euler_[1] + d[1]);
    euler_[2] = euler_[2] + d[2];
}

// Cef_TickChannel 29_win32.c 330-410: how much of a state's window
// [start, end) this tick's frame advance (prev -> cur) covered.
bool PlayerController::windowPortion(float prev, float cur, float start,
                                     float end, float& portion) {
    bool a = false, b = false;
    if (prev >= cur) {
        // a wrap or a fresh entry: the advance is measured from frame 1, or
        // the window is closed out whole
        if (cur >= start && cur < end) { prev = 1.0f; a = true; }
        if (cur >= end) { prev = start; cur = end; a = true; }
    } else {
        if (prev >= start && cur < end) a = true;
        if (prev < end && cur > end) b = true;
    }
    if (a) portion = cur - prev;
    if (b) portion = end - prev;
    return a || b;
}

// ---------------------------------------------------------------- tick

void PlayerController::tick(float dt, std::uint32_t word) {
    ++ticks_;
    last_ = Frame{};
    const auto& S = ctl_->states;

    // sub_4A7A20 (0x004A7A20): `*word = 0; if (!held) *word = 0x40000000;` -
    // nothing held is the IDLE word, not zero, and that is what opens the
    // stop edges (an entry whose +4 only FORBIDS bits, 0x20000 = "forward
    // released", opens on it).
    if (word == 0) word = kIdleInput;

    // ---- Cef_TickChannel's window passes, on LAST tick's advance -------
    //
    // The engine reads the channel's +12/+16 - the previous and current
    // frame as the previous tick left them - against the CURRENT entry's
    // window, before it searches for a transition.
    const int cur = rt_.channel().state();
    // ...and THE CURRENT ENTRY'S MOVE, EVERY TICK, when it carries 0x10 and
    // 0x200000 (0x004A85B7: `test byte [esi+4], 2; jnz` - channel flag 2 -
    // then `test al, 10h` / `test eax, 200000h` on the entry the tick began
    // in, and `Cef_QueueSpecialMove(actor, entry+64)`). The channel emits a
    // move only when an entry is ENTERED, and the candidate loop's for an
    // entry it does not take; this is the third site, for the entry he is
    // IN. It is what keeps `MDHEAD00` coming while L is held - H_STAND entry
    // 18 loops its clip, and the head arm clears +1305 every frame - and
    // without it the free look lasted one frame in every 53, the clip's
    // length. Queued after the tick's other moves, as the engine reaches it.
    // The window shift and turn below sit under the SAME `flag 2` test
    // (`Cef_TickChannel`'s `if ((u8(ch, 4) & 2) == 0)`), and NOTHING in the
    // shipped engine sets channel bit 2: every write of the flags dword is a
    // setter of another bit (`Perso_SetInputEnabled` 0x80, `Perso_SetNoPlayback`
    // 0x200, 0x1, 0x4, 0x100) or a read-modify-write clearing one (0x800, 0x1),
    // and the one `or [reg+4], 2` in the image (`sub_4B7AB1`) is a library
    // object's. So the block always runs. A gate on 0x200 instead would change
    // nothing visible: under 0x200 the channel's frame does not advance
    // (`Channel::tick`), so the window's portion is 0 either way
    // (todo/scene-gameplay-audit.md 23).
    std::string tickMove;
    if (cur >= 0 && cur < static_cast<int>(S.size()) && !(rt_.channel().flags() & 0x2u)) {
        const CtlState& st0 = S[static_cast<std::size_t>(cur)];
        if ((st0.flags & ctl_entry::MoveName) && (st0.flags & ctl_entry::MoveEveryTick)) tickMove = st0.moveName;
    }
    if (cur >= 0 && cur < static_cast<int>(S.size()) &&
        !(rt_.channel().flags() & 0x2u)) {
        const CtlState& st = S[static_cast<std::size_t>(cur)];
        float portion = 0.0f;
        if ((st.flags & ctl_entry::ShiftInWindow) && st.hasShift &&
            windowPortion(frameBefore_, frameAfter_, st.shift[0], st.shift[1], portion)) {
            const float local[3] = {portion * st.shift[2], portion * st.shift[3],
                                    portion * st.shift[4]};
            float w[3];
            rotateByFacing(local, w);              // Cef_ApplyRootShift: by +288
            for (int k = 0; k < 3; ++k) last_.shift[k] = w[k];
        }
        if ((st.flags & ctl_entry::TurnInWindow) && st.hasTurn &&
            windowPortion(frameBefore_, frameAfter_, st.turn[0], st.turn[1], portion)) {
            const float d[3] = {portion * st.turn[2], portion * st.turn[3],
                                portion * st.turn[4]};
            applyTurn(d);                          // Cef_ApplyTurn
            last_.turn = d[1];
        }
    }

    // ---- the machine ---------------------------------------------------
    const float f0 = rt_.channel().frame();
    const int   s0 = rt_.channel().state();
    const long  tr0 = rt_.channel().stats().transitions;
    // the pose on screen, which a blend out of a state with no clip starts from
    const NodeTracks* shownBefore = poseTracks();
    const int shownRow = poseFrame();
    rt_.tick(dt, word);
    const float f1 = rt_.channel().frame();
    const int   s1 = rt_.channel().state();
    // ---- Actor_BlendToClip: the transition's blend, baked ----------------
    //
    // A blended transition is NOT the target clip played from frame 1. While
    // it runs the channel's frame counts the BLEND (1 .. blendFrames) and the
    // entry may be a pass-through with no clip, and the port used to read the
    // clip behind its GoTo at that count - which replays the target from its
    // start. H1Avnt's `player.move 111` is the case a reader saw: H_SNKON
    // (19 frames) ends, the pass-through behind it blends 40 frames into
    // H_SNKON at frame 19 - its own last pose, so the engine HOLDS - and the
    // port raised the wrist a second time (SCENE 53, the call at the Telis
    // lunch). `sub_471820` reads each node's two keys at the blend record's
    // +12 (the frame left) and +16 (the start frame), as integers, and slerps
    // them by +8 = frame / blendFrames truncated to 255ths. A from-state with
    // no clip of its own passes 0, and `Actor_BlendToClip` snapshots the pose
    // instead (`sub_471710` into actor+460). The root delta is NOT changed
    // here: it still reads the clip owner (`sub_471460`'s blended root is
    // not ported).
    {
        const ChannelEvent* bl = nullptr;
        for (const auto& e : rt_.channel().events())       // the LAST clip change
            if (e.kind == ChannelEvent::Kind::Blend || e.kind == ChannelEvent::Kind::Cut)
                bl = e.kind == ChannelEvent::Kind::Blend ? &e : nullptr;
        if (bl && !(rt_.channel().flags() & channel_flag::NoPlayback) &&
            bl->to >= 0 && bl->to < static_cast<int>(S.size())) {
            const int fromClip = (bl->from >= 0 && bl->from < static_cast<int>(S.size()))
                                     ? S[static_cast<std::size_t>(bl->from)].clip : -1;
            NodeTracks snap;
            const NodeTracks* a = fromClip >= 0 ? clipTracks(fromClip) : nullptr;
            int rowA = static_cast<int>(f0) - 1;
            if (!a && shownBefore) {
                snap = blendTracks(*shownBefore, shownRow, false, *shownBefore, shownRow, false, 0.0f);
                a = &snap;
                rowA = 0;
            }
            const NodeTracks* b = clipTracks(S[static_cast<std::size_t>(bl->to)].clip);
            const int len = bl->frames > 0 ? bl->frames : 1;
            NodeTracks out;
            if (a && b) {
                const int rowB = static_cast<int>(bl->startFrame) - 1;
                for (int k = 0; k < len; ++k) {
                    NodeTracks row = blendTracks(*a, rowA, false, *b, rowB, false,
                                                 static_cast<float>(k + 1) / static_cast<float>(len));
                    if (k == 0) { out.ids = row.ids; out.count = row.count; out.rootTrack = row.rootTrack; }
                    out.quats.push_back(std::move(row.quats[0]));
                    out.trans.push_back(row.trans[0]);
                }
                out.frames = len;
            }
            transBlend_.tracks = std::move(out);
            transBlend_.transitions = rt_.channel().stats().transitions;
            transBlend_.state = s1;
        }
    }
    frameBefore_ = f0;
    frameAfter_  = f1;
    stateBefore_ = s0;
    // The whole-on-transition blocks (0x100 turn / 0x200 shift) arrive as
    // channel events; apply the ones this tick produced.
    moves_.clear();
    for (const auto& e : rt_.channel().events()) {
        // `Cef_QueueSpecialMove` -> `tab_special_move[]`, the binary's own
        // 66-row table of engine callbacks. The channel has always emitted
        // these and nothing consumed them, which is why the world TAKE never
        // worked: it IS one of those handlers (MDACTION scans for an object,
        // MDGETOBJ takes it, MDPUTSNK banks it, MDLETOBJ puts it back). The
        // handlers need the world, which this class does not have, so the
        // names are collected and the frontend runs them.
        if (e.kind == ChannelEvent::Kind::Move && !e.name.empty())
            moves_.push_back(e.name);
        if (e.kind == ChannelEvent::Kind::Turn && e.from >= 0 &&
            e.from < static_cast<int>(S.size())) {
            const CtlState& f = S[static_cast<std::size_t>(e.from)];
            const float d[3] = {f.turn[2], f.turn[3], f.turn[4]};
            applyTurn(d);
            last_.turn += d[1];
        } else if (e.kind == ChannelEvent::Kind::Shift && e.from >= 0 &&
                   e.from < static_cast<int>(S.size())) {
            const CtlState& f = S[static_cast<std::size_t>(e.from)];
            const float local[3] = {f.shift[2], f.shift[3], f.shift[4]};
            float w[3];
            rotateByFacing(local, w);
            for (int k = 0; k < 3; ++k) last_.shift[k] += w[k];
        } else if (e.kind == ChannelEvent::Kind::TurnRate && e.to >= 0 &&
                   e.to < static_cast<int>(S.size())) {
            // sub_45C080: the CANDIDATE's block, times the frame dt, with the
            // state unchanged. This is how the game turns while walking - the
            // gait keeps playing and only the facing moves - and it is what
            // makes DIAGONAL movement work. H1Avnt group 0 carries the turn on
            // four aliases (input 0x01/0x02, +-5 deg/frame and +-3), never on
            // H_WALK itself, so reading `from` here applies zero.
            const CtlState& c = S[static_cast<std::size_t>(e.to)];
            const float d[3] = {c.turn[2] * dt, c.turn[3] * dt, c.turn[4] * dt};
            applyTurn(d);
            last_.turn += d[1];
        } else if (e.kind == ChannelEvent::Kind::ShiftRate && e.to >= 0 &&
                   e.to < static_cast<int>(S.size())) {
            const CtlState& c = S[static_cast<std::size_t>(e.to)];
            const float local[3] = {c.shift[2] * dt, c.shift[3] * dt,
                                    c.shift[4] * dt};
            float w[3];
            rotateByFacing(local, w);
            for (int k = 0; k < 3; ++k) last_.shift[k] += w[k];
        }
    }
    if (!tickMove.empty() && tickMove != "none") moves_.push_back(tickMove);
    rt_.channel().clearEvents();

    // ---- sub_45C680: the clip's root delta ------------------------------
    //
    // The delta a tick applies is (the frame shown last tick, the frame shown
    // this tick] - `Anim_SetFrame(node, clip, +192, +188)` - and on the tick
    // the clip CHANGES it is (0, the new start]: `Cef_TickChannel` either
    // ticks the clip (`sub_45C680(frame)`) or returns `GoToMove`, which calls
    // `sub_45C680(startFrame)` itself after `Actor_PlayClip` /
    // `Actor_BlendToClip` ZEROED +192 (both write `u32i(actor, 48) = 0`). A
    // looping clip's end is such a change too (`GoToMove(cur, cur, 1.0)`). So
    // every new clip's KEY 1 is applied, on the transition tick, and the old
    // clip's last interval is not - with x and z zeroed when the start was a
    // seek (`seekNeeded`, start != 1.0), so a phase-matched entry does not
    // jump. Applying NOTHING on a transition would drop each new clip's key 1:
    // H_SLDOUT's whole +12.49 drop into the seat (a fall after the exit) and a
    // frame of stride at every walk cycle. A clip set from OUTSIDE the tick (`enterGroupById`) is the same
    // `GoToMove`, so its first tick runs from 0 too.
    float local[3] = {0, 0, 0};
    const bool transitioned = rt_.channel().stats().transitions != tr0;
    const bool fromZero = transitioned || clipEntered_;
    const bool seek = transitioned && rt_.channel().lastSeek();
    clipEntered_ = false;
    if (fromZero) {
        if (channelOnly_) nodeDrop_ = 0.0f;        // `o3de_SetNodePos(+244..+252)`
        const int variants = variantCount();
        const NodeTracks* baked = variants > 1 && !transBlendTracks() ? poseTracks() : nullptr;
        if (baked && !baked->trans.empty()) {
            // the grid's blended window, summed from its start (see below)
            const int last = static_cast<int>(baked->trans.size()) - 2 > 0
                                 ? static_cast<int>(baked->trans.size()) - 2 : 0;
            const int b = static_cast<int>(std::floor(f1)) - 1;
            const int ib = b < 0 ? 0 : (b > last ? last : b);
            for (int k = 0; k < 3; ++k)
                local[static_cast<std::size_t>(k)] =
                    baked->trans[static_cast<std::size_t>(ib)][static_cast<std::size_t>(k)];
        } else if (const RootTrack* rt = rootTrackOf(clip())) {
            rootDelta(*rt, 0.0f, f1, local);
        }
        if (seek) { local[0] = 0.0f; local[2] = 0.0f; }
    } else if (s1 == s0 && f1 >= f0) {
        // A VARIANT-GRID CLIP MOVES BY THE CELL THE BLEND CHOSE, not by the
        // raw frame (omk-play 69). `H_ADJSTP`'s six cells are six DIRECTIONS
        // of one 50 cm step - 19.69 inches, measured:
        //
        //     cell 0  dx +19.69          cell 2,3  dz -19.69   (back)
        //     cell 1  dx -19.69          cell 4,5  dz +19.67   (forward)
        //
        // Reading the raw track at frames 1..len is cell 0 every time, so the
        // character always stepped RIGHT whatever direction he needed, which
        // is why the approach angle was unchanged across the step (66.4 ->
        // 66.5 degrees, where squaring up is the whole point of it).
        //
        // AND THAT IS THE ENGINE'S READING TOO, not a port divergence.
        // `Anim_RootDelta` indexes by the raw frame - it does - but the
        // channel tick never calls it
        // directly for a grid clip: `sub_45CE90` sees flag bit 4 of +1252
        // and routes through `sub_4725B0`, whose position half `sub_472820`
        // makes FOUR `Anim_RootDelta` calls at the four cell offsets and
        // mixes them with the same two 0..256 weights as the rotations. The
        // baked window below is exactly that blend.
        const int variants = variantCount();
        const NodeTracks* baked = variants > 1 && !transBlendTracks() ? poseTracks() : nullptr;
        if (baked && !baked->trans.empty()) {
            const int a = static_cast<int>(std::floor(f0)) - 1;
            const int b = static_cast<int>(std::floor(f1)) - 1;
            // THE WINDOW'S LAST FRAME IS THE SEAM, AND IT MUST NOT BE READ
            // HERE EITHER. `poseFrame()` already holds the pose one frame
            // short of it, because both the pose and the root snap back to
            // standing there. Read up to the seam, the last tick of every
            // grid state sums the exact negative of the whole window - a
            // step of 17.8 units toward the object followed by one tick of
            // `dxz +17.81 -0.56` (measured in play, six presses, six
            // snap-backs) - and the character stands where he pressed when
            // `MDADJSTP` re-measures, so the adjust step "moves him" to the
            // eye and leaves the angle unchanged in the log.
            const int last = static_cast<int>(baked->trans.size()) - 2 > 0
                                 ? static_cast<int>(baked->trans.size()) - 2 : 0;
            const int ia = a < 0 ? 0 : (a > last ? last : a);
            const int ib = b < 0 ? 0 : (b > last ? last : b);
            for (int k = 0; k < 3; ++k)
                local[static_cast<std::size_t>(k)] =
                    baked->trans[static_cast<std::size_t>(ib)][static_cast<std::size_t>(k)] -
                    baked->trans[static_cast<std::size_t>(ia)][static_cast<std::size_t>(k)];
        } else if (const RootTrack* rt = rootTrackOf(clip())) {
            rootDelta(*rt, f0, f1, local);
        }
    }
    // `sub_466540`, from the channel tick while `dword_53AE1C` is set and the
    // actor is the player: the root delta's x and z scaled by `dword_6A5380`,
    // which `sub_465D30` set to the distance error over the 50 cm step. That
    // is how one authored H_ADJSTP lands on the target point whatever the
    // distance - and the missing "path by which the displacement reaches the
    // actor" the take handoff asked about: it is this scale, on THIS delta.
    if (adjust_) { local[0] *= stepScale_; local[2] *= stepScale_; }
    float world[3];
    rotateByFacing(local, world);                  // Anim_RootDelta's 3x3 = +288
    // (in the water `rotateByFacing` IS the whole Euler - see it)
    for (int k = 0; k < 3; ++k) last_.rootDelta[k] = world[k];
    for (int k = 0; k < 3; ++k) last_.rootLocal[k] = local[k];

    // ---- Actor_ApplyMotion: try the move, let the ground decide ----------
    //
    // The position delta since the last safe stance is what Actor_Move gets:
    // the root delta and any root shift, x and z only in state 1 (+248 takes
    // the delta only in states 11..14). The walker port is the probe half:
    // no floor under the destination -> revert; a rise past the step -> a
    // wall; a drop past it -> a ledge. Actor_Move's collide-and-slide is
    // not ported (README, `engine walk`), so a blocked step stops instead of
    // sliding.
    double dx = static_cast<double>(world[0] + last_.shift[0] + shootMotion_[0]);
    double dz = static_cast<double>(world[2] + last_.shift[2] + shootMotion_[1]);
    const int waterState = static_cast<int>(rt_.state());
    // THE SHOOT MOVER'S HALF (`actor/shootmove.h`): its vertical lifts him
    // before the frame is tried, and in the air its horizontal is swept
    // outright - the walk's airborne rule drops a clip's root delta, and this
    // is not one. Only in ACTOR_STATE 3, where the mover runs at all.
    if (waterState == 3) {
        walker_.lift(static_cast<double>(shootMotion_[2]));
        if (walker_.airMove(static_cast<double>(shootMotion_[0]),
                            static_cast<double>(shootMotion_[1]))) {
            dx -= static_cast<double>(shootMotion_[0]);
            dz -= static_cast<double>(shootMotion_[1]);
        }
    }
    shootMotion_[0] = shootMotion_[1] = shootMotion_[2] = 0.0f;
    if (waterState >= 11 && waterState <= 14) {
        waterTick(dx, static_cast<double>(world[1] + last_.shift[1]), dz, dt);
    } else if (waterState == 16 || waterState == 17) {
        // IN A CONVERSATION THE BODY DOES NOT MOVE. `Actors_TickAll` sends
        // ACTOR_STATEs 16 and 17 to `Actor_TickDialogue` (0x00466950), which
        // is `Cef_TickChannel` and `Actor_ScanZones` and nothing else: no
        // `Actor_ApplyMotion`, so a clip's root motion never reaches the
        // actor. Walking into Telis's zone in Kay'l's flat leaves the channel
        // mid-stride, `SetPersoBankGroup` then plays `H_WK-SD` out of it, and
        // applying its root delta would slide him 2.8 units after
        // `actor.goto_address 678` - into the first line camera, which is
        // authored nine units from his head.
        last_.stepped = false;
    } else if (channelOnly_) {
        // `Actor_TickChannelOnly` (0x00466B00) is `Cef_TickChannel` and
        // nothing else: the root delta reaches the body through
        // `Actor_MoveBy` - `o3de_MoveNodeBy` plus the outright write of
        // +244..+252 - and no `Actor_ApplyMotion` runs at all, so there is no
        // gravity, no ground probe and no collision slide. Ticking the walker
        // here made it fight a body climbing into a vehicle that hovers.
        // ...and only x and z reach the POSITION (`sub_45C680` cases 6/8:
        // +244/+252 and +232/+240, never +248); the y goes to the NODE,
        // which the frontend draws through `nodeDrop()`. Moving his position
        // in y would leave him standing 12 units above the road when
        // H_SLDOUT's stand-up ends, and he would fall.
        const double* w = walker_.pos();
        nodeDrop_ += world[1];
        walker_.moveTo(w[0] + dx,
                       w[1] + static_cast<double>(last_.shift[1]),
                       w[2] + dz);
    } else if (std::fabs(dx) > 1e-6 || std::fabs(dz) > 1e-6) {
        last_.stepped = true;
        last_.step = walker_.step(dx, dz, dt);
    } else {
        // Standing still horizontally is not standing still. An actor who
        // stepped off a ledge is FALLING, and `Walk_GroundResponse` runs every
        // frame whatever `Actor_Move` was handed - so the vertical half is
        // owed a tick even when the clip produces no root delta. Without it he
        // hangs in the air until he asks to move again.
        last_.step = walker_.tick(dt);
    }
    pos_[0] = static_cast<float>(walker_.pos()[0]);
    pos_[1] = static_cast<float>(walker_.pos()[1]);
    pos_[2] = static_cast<float>(walker_.pos()[2]);
    // Actor_ApplyMotion's 1/8 steer toward the motion direction: the delta
    // was rotated by the facing, so the two headings coincide and the turn
    // is 0. Written out rather than skipped so the reason is on the page.
    if (last_.stepped && last_.step == StepResult::Moved) {
        // `mx, mz` would be what Actor_Move let through; with no slide it is
        // the request itself.
        const double mx = dx, mz = dz;
        if (std::fabs(dx) > 0.0001 && std::fabs(mx) > 0.0001) {
            double turn = (std::atan2(mz, mx) - std::atan2(dz, dx)) * kRadToDeg;
            if (turn < -180.0) turn += 360.0;
            if (turn > 180.0) turn -= 360.0;
            euler_[1] = wrap360(euler_[1] + static_cast<float>(turn * 0.125));
        }
    }
    if (const auto g = walker_.ground(pos_[0], pos_[1], pos_[2])) {
        last_.ground = *g;
        last_.onGround = std::fabs(*g - pos_[1]) <= kStepUp + 1.0;
    }

    // ---- the camera tick, sub_417CF0 -> sub_415D10 / sub_415E60 ---------
    FollowCamera target;
    if (block_.fresh) {
        for (int k = 0; k < 3; ++k) block_.euler[k] = euler_[k];
        resolveSteady(cam_, block_.euler);
        // `sub_413C00` seeds `+340` from the subject's own y at setup, so the
        // first tick's `+340 - +344` is zero rather than the whole height.
        collide_.subjY = collide_.prevSubjY = pos_[1] - camLift_;
        collide_.prevValid = false;
        block_.fresh = false;
        collide_.justChanged = true;        // flag 1, for this tick's collision pass
    } else {
        // the lagged Euler triple: chase, snapping inside 0.1 degrees
        const int k46 = block_.f46 == 1 ? 2 : block_.f46;
        for (int k = 0; k < 3; ++k) {
            if (!k46) { block_.euler[k] = euler_[k]; continue; }
            const float d = wrap180(euler_[k] - block_.euler[k]);
            if (std::fabs(d) <= 0.1f) block_.euler[k] = euler_[k];
            else block_.euler[k] += d * dt / static_cast<float>(k46);
        }
        resolveSteady(target, block_.euler);
        const int k42 = block_.f42 == 1 ? 2 : block_.f42;
        const int k44 = block_.f44 == 1 ? 2 : block_.f44;
        for (int k = 0; k < 3; ++k) {
            if (k42) cam_.at[k] += (target.at[k] - cam_.at[k]) * dt / static_cast<float>(k42);
            else cam_.at[k] = target.at[k];
            if (k44) cam_.eye[k] += (target.eye[k] - cam_.eye[k]) * dt / static_cast<float>(k44);
            else cam_.eye[k] = target.eye[k];
        }
        cam_.fov = block_.fov;
    }
    // `+344 = +340; +340 = +156` - the tick latches the eye subject's own Y
    // and the frame before it, in that order and IMMEDIATELY before the
    // collision pass (04_sys.c 3785). `+156` is the subject's origin, which
    // for this controller is the pelvis.
    collide_.prevSubjY = collide_.subjY;
    collide_.subjY     = pos_[1] - camLift_;
    cameraCollide(dt);
    // ...and `sub_4133B0`/`sub_4133E0` close the tick by writing the final
    // eye and target back into `+20..+28` and `+32..+40`, which is what the
    // NEXT tick's height ease anchors on.
    collide_.prevEyeY  = cam_.eye[1];
    collide_.prevAtY   = cam_.at[1];
    collide_.prevValid = true;
    collide_.justChanged = false;      // flag 1: the tick clears it on its way out
}

// `sub_417070` (04_sys.c 3370), the arm flag 8 selects - the adventure
// camera's collision pass. Transcribed:
//
//     D = eye - target ;  d = |D|                       (+52..+60, +64..+72)
//     P = target + D * (+300)/d                         ; +300 is 1.2
//     if (cast target -> P hits) {
//         if (flags & 0x1000 && hit->flags & 0x20000000) return;   see-through
//         if (+208 == 0) +328 = d ;  +208 = 1
//         V = hit - target ;  r = |V| / (+300)
//         if (r > +328 && !(flags & 1))
//             r = (r - +328) * dt / (+320) + +328       ; ease OUT over 8
//         +328 = r ;  eye = target + V * r/|V|
//     } else if (+208 == 1) {                           ; THE SECOND RAY
//         A = +100.. - R(+112..) * +124.. ; B = +152.. - R(+76..) * +176..
//         if (cast A -> A + (B-A)*1.2 hits) {           ; still blocked
//             eye = target + normalise(eye-target) * +328
//             dy = +340 - +344
//             eye.y = +24 + dy ; target.y = +36 + dy ; return      ; NO push
//         }
//         +208 = 2 ;                                    ; fall through
//     } else if (+208 == 2) {
//         if (d <= +328) { +208 = 0; return; }
//         +328 = (d - +328) * dt / (+320) + +328        ; ease back OUT
//         eye = target + D * (+328)/d
//     }
//     t = (r <= d/2) ? 0 : (r - d/2) / (d - d/2)        ; the LIFT's blend
//     eye.y    = (+312 + +156)*(1-t) + (+56) * t        ; +56 is the UNPULLED y
//     target.y = (+316 + +156)*(1-t) + (+68) * t
//     ...each eased toward +24 / +36 over +324 frames when flag 1 is clear,
//     and +24 / +36 are LAST FRAME'S FINAL eye and target y (`sub_4133B0` /
//     `sub_4133E0` write them back at the end of every tick), so the ease
//     CONVERGES on the push rather than holding a dt/4 fraction of it.
//
// The whole function is transcribed line by line in
// `todo/camera-obstruction.md` 5, with every offset resolved against its
// writer - and 6 there settles the SCOPE: `sub_414520` arms this pass only
// for a camera whose EYE SUBJECT is 0 (or 5, which never ships), so an
// absolute world or dialogue camera never reaches it. That is why the port
// runs it here, on the follow camera, and nowhere else.
//
// `sub_413C00` loads the constants: +300 = 1.2, +320 = 8, +324 = 4, and
// +312/+316 = -0.7 x the subject's pelvis height, which with Y growing
// DOWNWARD lifts the pinched camera ABOVE him rather than dropping it.
//
// THE SEE-THROUGH FLAG IS UNREACHABLE FOR THIS CAMERA, which is why it is not
// modelled. `sub_417070` and `sub_416570` both guard their hit with
//
//     if ((cam[356] & 0x1000) && (hit->flags & 0x20000000)) return;
//
// and 93 of the 12203 shipped decor meshes do carry 0x20000000 (41 in
// Lahoreh, 10 in Jangir), so the surface half is live. The CAMERA half is
// not: `Camera_LoadParams` sets `+356` to 1 on every request, `sub_413C00`
// then ORs in `0x1C` through a LOBYTE write that cannot reach bit 12, and the
// only write in the binary that sets 0x1000 is `sub_413CD0`'s ACTOR_STATE 13
// arm - and `sub_413CD0` runs only for states 11, 13 and 14. So the ordinary
// follow camera never carries 0x1000 and the test can never fire for it,
// which puts it with `nullsub_9` and the six dead spell recipes.
//
// The soups are still this port's rather than the engine's set - `sub_444810`
// walks the scene's meshes where these are the walker's walkable faces and
// the steep complement - but that is a difference in WHICH faces exist, not
// the flag rule.
void PlayerController::cameraCollide(float dt) {
    if (!collide_.solidA && !collide_.solidB) return;
    // `sub_417070`, now `obstructCamera` (o3de/camobstruct.h), with
    // `sub_413C00`'s pushes: +312 = +316 = -0.7 x his pelvis height
    CamObstruct st;
    st.dist = collide_.dist; st.block = collide_.block;
    st.prevEyeY = collide_.prevEyeY; st.prevAtY = collide_.prevAtY; st.prevValid = collide_.prevValid;
    st.subjY = collide_.subjY; st.prevSubjY = collide_.prevSubjY;
    const TriangleSoup* const solids[2] = {collide_.solidA, collide_.solidB};
    const SplitSoupGrid* const grids[2] = {collide_.gridA, collide_.gridB};
    const auto cast = [&](const double from[3], const double ray[3]) {
        double best = 2.0;
        for (int k = 0; k < 2; ++k) {
            const TriangleSoup* s = solids[k];
            if (!s || s->empty()) continue;
            if (const auto h = sweepThrough(*s, grids[k], from, ray, 0.0))
                if (h->t < best) best = h->t;
        }
        return best;
    };
    // the second ray: `B` is `sub_415E60`'s eye, which resolves against the
    // LAGGED Euler triple `+76..+84` and not the actor's own -
    // `resolveSteady` splits the two, its argument for the eye and `euler_`
    // for the target, which is exactly `+76..+84` against `+112..+120`
    const auto unlagged = [&](float e[3], float a[3]) {
        FollowCamera fc;
        resolveSteady(fc, block_.euler);
        for (int k = 0; k < 3; ++k) { e[k] = fc.eye[k]; a[k] = fc.at[k]; }
        return true;
    };
    const double lift = -0.7 * static_cast<double>(camLift_);
    obstructCamera(st, cam_.eye, cam_.at, lift, lift, dt, collide_.justChanged, cast, unlagged);
    collide_.dist = st.dist; collide_.block = st.block;
}

// ------------------------------------------------------------- posing

const NodeTracks* PlayerController::transBlendTracks() const {
    const auto& ch = rt_.channel();
    if (!transBlend_.tracks.valid() || ch.state() != transBlend_.state ||
        ch.stats().transitions != transBlend_.transitions) return nullptr;
    return &transBlend_.tracks;
}

const NodeTracks* PlayerController::poseTracks() {
    if (const NodeTracks* bt = transBlendTracks()) return bt;   // a blend runs
    const NodeTracks* raw = clipTracks(clip());
    if (!raw) return nullptr;
    const int v = variantCount();
    return v > 1 ? gridTracks(*raw, v) : raw;
}

int PlayerController::groupDefaultClip(int groupId) const {
    if (!ctl_) return -1;
    for (const auto& g : ctl_->groupList) {
        if (static_cast<int>(g.id) != groupId) continue;
        const int e = g.defaultEntry;
        if (e < 0 || e >= static_cast<int>(ctl_->states.size())) return -1;
        return ctl_->states[static_cast<std::size_t>(e)].clip;
    }
    return -1;
}

const NodeTracks* PlayerController::clipTracks(int c) {
    if (c < 0 || !meshes_ || !ctl_ || c >= static_cast<int>(ctl_->clips.size())) return nullptr;
    auto it = tracks_.find(c);
    if (it != tracks_.end()) return it->second.valid() ? &it->second : nullptr;
    NodeTracks t;
    const auto d = animDescriptor(data_, ctl_->clips[static_cast<std::size_t>(c)].offset);
    if (d && d->frames > 0) {
        t.count = static_cast<int>(d->tracks.size());
        t.frames = d->frames;
        t.rootTrack = -1;
        for (const auto& tr : d->tracks) t.ids.push_back(meshOfTrack(tr, *meshes_));
        t.quats.assign(static_cast<std::size_t>(d->frames), {});
        // THE ROOT MOTION.
        //
        // The pelvis (`UBassin`) is the hierarchy root and carries POSITION
        // keys beside its rotations; `Anim_RootDelta` sums them from key 1.
        // For a take that sum IS the crouch - H_TAKL12's pelvis drops 24.3
        // units (62 cm) inside one of its six cells and H_TAKL22 lifts it back
        // - so dropping it leaves the body upright while the legs bend, which
        // looks like floating (omk-play 69).
        t.trans.assign(static_cast<std::size_t>(d->frames), {0.0f, 0.0f, 0.0f});
        for (const auto& tr : d->tracks) {
            if (!tr.posOffset || tr.posKeys <= 1) continue;
            float acc[3] = {0.0f, 0.0f, 0.0f};
            for (int f = 0; f < d->frames; ++f) {
                const int key = f + 1 < tr.posKeys ? f + 1 : tr.posKeys - 1;
                const std::size_t o = tr.posOffset + 12u * static_cast<std::size_t>(key);
                if (o + 12 > data_.size()) break;
                for (int k = 0; k < 3; ++k)
                    acc[static_cast<std::size_t>(k)] +=
                        f32(data_, o + 4u * static_cast<std::size_t>(k));
                for (int k = 0; k < 3; ++k)
                    t.trans[static_cast<std::size_t>(f)][static_cast<std::size_t>(k)] =
                        acc[static_cast<std::size_t>(k)];
            }
            break;                 // the pelvis is the only root track
        }
        for (int f = 0; f < d->frames; ++f) {
            auto& row = t.quats[static_cast<std::size_t>(f)];
            row.resize(d->tracks.size());
            for (std::size_t i = 0; i < d->tracks.size(); ++i) {
                const AnimTrack& tr = d->tracks[i];
                if (!tr.rotOffset || tr.rotKeys <= 0) continue;
                // key 0 is the REST SENTINEL: frame f reads key f + 1
                int key = f + 1;
                if (key >= tr.rotKeys) key = tr.rotKeys - 1;
                const std::size_t o = tr.rotOffset + 16u * static_cast<std::size_t>(key);
                if (o + 16 > data_.size()) continue;
                row[i] = {f32(data_, o), f32(data_, o + 4),
                          f32(data_, o + 8), f32(data_, o + 12)};
            }
        }
    }
    it = tracks_.emplace(c, std::move(t)).first;
    return it->second.valid() ? &it->second : nullptr;
}

// The four cells and the two weights, `sub_466390` transcribed. `keys` is the
// clip's KEY count (frames + 1, key 0 being the rest sentinel), which is what
// divides exactly by the variant count: 6 x 21 = 126 and 9 x 21 = 189.
PlayerController::GridSample PlayerController::gridSample(int n, int keys) const {
    GridSample g;
    g.len = keys / n;
    // The engine clamps both axes before using them, and each weight is
    // normalised by ITS OWN clamp - 256/51, 256/53, 256/50 against +51, -53
    // and +-50. LABELLED: that correspondence is one hypothesis fitted three
    // times, not three confirmations (todo/omk-play.md 69).
    float a = take_.angle;
    // THE ADJUST STEP IS A DIFFERENT BUILDER (`sub_466210`, group 600). Its
    // angle is NOT clamped to the 50 degree cone: the whole circle is split
    // into quadrants, one cell chosen by the quadrant and the other by the
    // sign, blended by `|angle| * 256/90` - and `out[4]` is written 0, so the
    // cross-blend collapses and there is no second axis.
    if (adjust_) {
        int quad, sgn;
        float v;
        if (a >= 0.0f) {
            if (a > 90.0f) { v = (180.0f - a) * 256.0f / 90.0f; quad = 5; sgn = 1; }
            else           { v = a * 256.0f / 90.0f;            quad = 3; sgn = 1; }
        } else {
            if (a < -90.0f) { v = (a + 180.0f) * 256.0f / 90.0f; quad = 4; sgn = 0; }
            else            { v = a * 256.0f / 90.0f;            quad = 2; sgn = 0; }
        }
        g.cell[0] = quad; g.cell[1] = sgn;      // out[8],  out[0Ah]
        g.cell[2] = quad; g.cell[3] = sgn;      // out[0Eh], out[0Ch] - the same pair
        float w = std::fabs(v) / 256.0f;
        if (w > 1.0f) w = 1.0f;
        g.wSecond = w;
        g.wAngle  = 0.0f;                       // out[4] = 0
        return g;
    }
    if (a >  50.0f) a =  50.0f;
    if (a < -50.0f) a = -50.0f;
    float s = take_.second;
    const int col = a >= 0.0f ? 2 : 0;
    if (n == 9) {
        if (s >  51.0f) s =  51.0f;
        if (s < -53.0f) s = -53.0f;
        const int row = s >= 0.0f ? 0 : 6;
        g.cell[0] = 4;  g.cell[1] = row + 1;
        g.cell[2] = col + 3;  g.cell[3] = col + row;
        g.wSecond = std::fabs(s) / (s >= 0.0f ? 51.0f : 53.0f);
    } else {
        // n == 6: the second axis arrives pre-scaled by 1/29.527559 - 75 cm,
        // the same constant `sub_465D30`'s low arm uses.
        g.cell[0] = 1;  g.cell[1] = 4;
        g.cell[2] = col;  g.cell[3] = col + 3;
        g.wSecond = std::fabs(s);
    }
    g.wAngle = std::fabs(a) / 50.0f;
    if (g.wSecond > 1.0f) g.wSecond = 1.0f;
    if (g.wAngle  > 1.0f) g.wAngle  = 1.0f;
    return g;
}

// `sub_4725B0`: four key offsets added to the frame, two slerps along one axis
// and one across. Baked for the whole window because the geometry is fixed for
// the duration of one take.
const NodeTracks* PlayerController::gridTracks(const NodeTracks& base, int n) {
    const int c = clip();
    if (gridBlend_.clip == c && gridBlend_.gen == take_.gen && gridBlend_.tracks.valid())
        return &gridBlend_.tracks;
    const int keys = base.frames + 1;         // key 0 is the rest sentinel
    if (n <= 1 || keys % n != 0) return &base;
    const GridSample g = gridSample(n, keys);

    NodeTracks out;
    out.count = base.count;
    out.rootTrack = base.rootTrack;
    out.ids = base.ids;
    out.frames = g.len;
    out.quats.assign(static_cast<std::size_t>(g.len), {});
    out.trans.assign(static_cast<std::size_t>(g.len), {0.0f, 0.0f, 0.0f});
    const std::size_t nodes = base.quats.empty() ? 0 : base.quats[0].size();
    for (int f = 0; f < g.len; ++f) {
        auto& row = out.quats[static_cast<std::size_t>(f)];
        row.resize(nodes);
        // KEY 0 IS THE REST SENTINEL - a T-pose - and this must never read it.
        //
        // The engine takes `key = offset + curFrame` with the channel's frame
        // running 1..len, so the lowest key it can reach is `offset + 1`. The
        // port's `poseFrame()` is `channelFrame - 1`, and a baked frame `x`
        // holds key `x + 1`, so the baked index is
        //
        //     key           = cell*len + (f + 1)
        //     baked frame   = key - 1 = cell*len + f
        //
        // Taking `cell*len + f - 1` read key `cell*len + f`, which for cell 0
        // at f == 0 is KEY 0 itself: one frame of T-pose at the start of every
        // take, which is exactly the fault CLAUDE.md 5 records for reading the
        // sentinel as frame 0.
        const auto at = [&](int cell, std::size_t node) -> Quatf {
            int b = g.cell[cell] * g.len + f;
            if (b < 0) b = 0;
            if (b >= base.frames) b = base.frames - 1;
            const auto& r = base.quats[static_cast<std::size_t>(b)];
            return node < r.size() ? r[node] : Quatf{1.0f, 0.0f, 0.0f, 0.0f};
        };
        for (std::size_t i = 0; i < nodes; ++i) {
            const Quatf A = qslerp(at(0, i), at(1, i), g.wSecond);
            const Quatf B = qslerp(at(2, i), at(3, i), g.wSecond);
            row[i] = qslerp(A, B, g.wAngle);
        }
        // THE ROOT MOTION IS PER CELL, AND RELATIVE TO IT.
        //
        // The pelvis is the hierarchy root in all 181 character models, so the
        // crouch is a root TRANSLATION and not just rotation - blend the
        // quaternions alone and the legs animate while the hips stay put,
        // which looks like the character floating.
        //
        // `clipRootMotion` accumulates from key 1, so at cell k it already
        // carries every EARLIER cell's motion: six concatenated takes summed
        // together. What a cell means on its own is the motion SINCE THE CELL
        // BEGAN, so each is re-based on its own first key and the four are
        // then mixed with the same weights as the rotations.
        const auto tr = [&](int cell) -> std::array<float, 3> {
            const int b0 = g.cell[cell] * g.len;
            int b = b0 + f;
            if (b >= base.frames) b = base.frames - 1;
            if (b < 0 || b0 < 0 || b0 >= static_cast<int>(base.trans.size()) ||
                b >= static_cast<int>(base.trans.size()))
                return {0.0f, 0.0f, 0.0f};
            const auto& a = base.trans[static_cast<std::size_t>(b)];
            const auto& z = base.trans[static_cast<std::size_t>(b0)];
            return {a[0] - z[0], a[1] - z[1], a[2] - z[2]};
        };
        const auto t0 = tr(0), t1 = tr(1), t2 = tr(2), t3 = tr(3);
        for (int k = 0; k < 3; ++k) {
            const float A = t0[static_cast<std::size_t>(k)] * (1.0f - g.wSecond) +
                            t1[static_cast<std::size_t>(k)] * g.wSecond;
            const float B = t2[static_cast<std::size_t>(k)] * (1.0f - g.wSecond) +
                            t3[static_cast<std::size_t>(k)] * g.wSecond;
            out.trans[static_cast<std::size_t>(f)][static_cast<std::size_t>(k)] =
                A * (1.0f - g.wAngle) + B * g.wAngle;
        }
    }
    gridBlend_.tracks = std::move(out);
    gridBlend_.clip = c;
    gridBlend_.gen  = take_.gen;
    return &gridBlend_.tracks;
}

int PlayerController::poseFrame() const {
    // the channel's frame runs 1 .. clipLen; the tracks are 0-based
    int f = static_cast<int>(std::floor(rt_.channel().frame())) - 1;
    if (const NodeTracks* bt = transBlendTracks()) {           // one row a blend frame
        const int n = bt->frames;
        return f < 0 ? 0 : (f >= n ? n - 1 : f);
    }
    int n = clipFrames();
    // A variant-grid clip is played as ONE cell, so the window is `len` and
    // not the whole 125 or 188 (omk-play 69). Without this the channel walks
    // the full clip and every variant plays in turn, which is the report.
    const int v = variantCount();
    // A grid window is `keys / variants` frames, and its LAST one is a SEAM:
    // measured, both the pose and the root snap back to standing there
    // (H_TAKL12 f20: the feet return to +0.67 and the root to -0.22 after
    // reaching -19.09 and +16.03 at f18). Holding the frame one short keeps
    // the window on real motion; a magnitude guard on the root delta did the
    // same job but also ate genuine motion mid-crouch, which left the body
    // floating three units at the deepest point (omk-play 69).
    if (v > 1 && n > 0) { n = (n + 1) / v; if (n > 1) --n; }
    if (f < 0) f = 0;
    if (n > 0 && f >= n) f = n - 1;
    return f;
}

float PlayerController::poseFrameF() const {
    const int f = poseFrame();
    const double c = rt_.channel().frame();
    const double fl = std::floor(c);
    // clamped below or above: no fraction
    if (static_cast<int>(fl) - 1 != f) return static_cast<float>(f);
    // the window, as `poseFrame` sizes it
    int n = clipFrames();
    const int v = variantCount();
    if (const NodeTracks* bt = transBlendTracks()) n = bt->frames;
    else if (v > 1 && n > 0) { n = (n + 1) / v; if (n > 1) --n; }
    if (n > 0 && f + 1 >= n) return static_cast<float>(f);
    return static_cast<float>(f) + static_cast<float>(c - fl);
}

// ------------------------------------------------- THE VARIANT GRID (69)

void PlayerController::setTakeGeometry(float angleDeg, float second) {
    take_.angle  = angleDeg;
    take_.second = second;
    ++take_.gen;                       // the baked window is no longer valid
}

const std::vector<CtlEffect>& PlayerController::stateEffects() const {
    static const std::vector<CtlEffect> none;
    const int e = rt_.channel().state();
    if (e < 0 || !ctl_ || e >= static_cast<int>(ctl_->states.size())) return none;
    return ctl_->states[static_cast<std::size_t>(e)].effects;
}

int PlayerController::variantCount() const {
    const int e = rt_.channel().state();
    if (e < 0 || !ctl_ || e >= static_cast<int>(ctl_->states.size())) return 0;
    return ctl_->states[static_cast<std::size_t>(e)].playBits >> 12;
}

// ------------------------------------------------------------- camera

void PlayerController::setCameraOffsets(const float eyeOff[3], const float atOff[3],
                                        float fov, int f42, int f44, int f46) {
    for (int k = 0; k < 3; ++k) { block_.eyeOff[k] = eyeOff[k]; block_.atOff[k] = atOff[k]; }
    block_.fov = fov > 1.0f ? fov : kFollowFov;
    block_.f42 = f42; block_.f44 = f44; block_.f46 = f46;
    block_.fresh = true;      // Camera_LoadParams sets flag 1: the next frame snaps
    // ...and `Camera_Request` does `memset(cam + 208, 0, 0x94)` before it
    // calls `sub_414520`, which clears `+208` AND `+328`. So a new camera
    // never inherits the previous one's block state or its kept distance -
    // which is the fault `todo/camera-obstruction.md` 2 records as attempt
    // 2's, and it was still latent here because the two live in the
    // controller rather than in a per-camera block.
    collide_.block = 0;
    collide_.dist  = 0.0f;
}

void PlayerController::resolveSteady(FollowCamera& c, const float e[3]) const {
    // point = subjectPos - R(euler) * offset, per point (sub_415D10 for the
    // target with the SUBJECT's euler, sub_415E60 for the eye with the lagged
    // copy). Only the yaw is non-zero here - see the header.
    float r[3];
    // The subject, which is the actor's ORIGIN and not the floor point - see
    // `cameraLift()`. Y points down, so raising it is a subtraction.
    const float sub[3] = {pos_[0], pos_[1] - camLift_, pos_[2]};
    rotateYaw(e[1], block_.eyeOff, r);
    for (int k = 0; k < 3; ++k) c.eye[k] = sub[k] - r[k];
    rotateYaw(euler_[1], block_.atOff, r);
    for (int k = 0; k < 3; ++k) c.at[k] = sub[k] - r[k];
    c.fov = block_.fov;
}

FollowCamera PlayerController::resolveOffsets(const float eyeOff[3], const float atOff[3],
                                              float fov) const {
    FollowCamera c;
    float r[3];
    const float sub[3] = {pos_[0], pos_[1] - camLift_, pos_[2]};
    // The WHOLE Euler, which is what `sub_415D10` rotates a subject-relative
    // point by - and it matters here because the hurt shove (`sub_47D1F0`)
    // moves +416 and +424. With both at zero this is exactly
    // `rotateYaw(euler_[1], ..)`, term for term.
    rotateEuler(euler_, eyeOff, r);
    for (int k = 0; k < 3; ++k) c.eye[k] = sub[k] - r[k];
    rotateEuler(euler_, atOff, r);
    for (int k = 0; k < 3; ++k) c.at[k] = sub[k] - r[k];
    c.fov = fov > 1.0f ? fov : kFollowFov;
    return c;
}

FollowCamera PlayerController::resolveOffsetsYaw(const float eyeOff[3], const float atOff[3],
                                                 float fov) const {
    const float yawOnly[3] = {0.0f, euler_[1], 0.0f};
    FollowCamera c;
    float r[3];
    const float sub[3] = {pos_[0], pos_[1] - camLift_, pos_[2]};
    rotateEuler(yawOnly, eyeOff, r);
    for (int k = 0; k < 3; ++k) c.eye[k] = sub[k] - r[k];
    rotateEuler(yawOnly, atOff, r);
    for (int k = 0; k < 3; ++k) c.at[k] = sub[k] - r[k];
    c.fov = fov > 1.0f ? fov : kFollowFov;
    return c;
}

FollowCamera PlayerController::followCameraSteady() const {
    FollowCamera c;
    resolveSteady(c, euler_);
    return c;
}

double PlayerController::distanceWalked() const {
    const double dx = pos_[0] - start_[0], dz = pos_[2] - start_[2];
    return std::sqrt(dx * dx + dz * dz);
}

bool PlayerController::lookTick(std::uint32_t bits, int mdx, int mdy, int sensX, int sensY,
                                float dt, bool& mouseMoved) {
    mouseMoved = false;
    float& p = headEuler_[0];
    float& y = headEuler_[1];
    const int st = static_cast<int>(rt_.state());
    if ((st == 1 || st == 13) && look_.flag) {
        if (p >= 180.0f) p -= 360.0f;
        if (y >= 180.0f) y -= 360.0f;
        if (bits & 4u) p -= dt * 6.0f;
        if (bits & 8u) p += dt * 6.0f;
        if (bits & 1u) y += dt * 6.0f;
        if (bits & 2u) y -= dt * 6.0f;
        if (look_.dropMouse) { mdx = mdy = 0; look_.dropMouse = false; }
        y -= static_cast<float>(static_cast<double>(sensX * mdx) * dt * 0.0099999998);
        p -= static_cast<float>(static_cast<double>(sensY * mdy) * dt * 0.0099999998);
        if (p < -40.0f) p = -40.0f;
        if (p >  40.0f) p =  40.0f;
        if (y >  70.0f) y =  70.0f;
        if (y < -70.0f) y = -70.0f;
        if (p < 0.0f) p += 360.0f;
        if (y < 0.0f) y += 360.0f;
        mouseMoved = mdx != 0 || mdy != 0;
        look_.flag = false;
        return true;
    }
    // `Actor_SetHeadLook(a, 0, 0)`: the player has no look-at target
    if (p >= 180.0f) p -= 360.0f;
    if (y >= 180.0f) y -= 360.0f;
    p = easeLook(p, 0.0f, dt);
    y = easeLook(y, 0.0f, dt);
    if (p < 0.0f) p += 360.0f;
    if (y < 0.0f) y += 360.0f;
    return false;
}

void headCamera(const float head[3], const float euler[3], float eye[3], float at[3]) {
    float r[3];
    rotateEuler(euler, kHeadCamEye, r);
    for (int k = 0; k < 3; ++k) eye[k] = head[k] - r[k];
    rotateEuler(euler, kHeadCamAt, r);
    for (int k = 0; k < 3; ++k) at[k] = head[k] - r[k];
}

}  // namespace omk

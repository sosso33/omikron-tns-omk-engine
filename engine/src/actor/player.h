// SPDX-License-Identifier: GPL-3.0-or-later
// THE PLAYER CONTROLLER - adventure mode: the keyboard word into the `.CTL`
// channel, the clip's root motion out of it, through the walker, onto the
// floor, and the follow camera behind him.
//
// Every piece below already existed as a ported, tested DECISION module and
// nothing ran them from a keyboard: `input/bindings.h` makes the word,
// `actor/state.h` is `ACTOR_STATE` 0..17, `actor/channel.h` is the `.CTL`
// machine that picks the clip from the word, `actor/walk.h` is the ground
// probe. This file is the per-frame chain that joins them, read from
//
//     Actors_TickAll      0x004681C0   state 1 -> Actor_TickNpc
//     Actor_TickNpc       0x00466580   +1308 face-along-node pass, then
//                                      Cef_TickChannel, then Actor_ApplyMotion,
//                                      then Actor_ScanZones
//     Cef_TickChannel     0x004A8160   the machine; ends in sub_45C680
//     sub_45C680          0x0045C680   case 1: sub_45CE90 -> the clip's ROOT
//                                      DELTA between the previous frame and
//                                      this one, MOVED onto the node and added
//                                      to the position (+244/+252; +248 only
//                                      in states 11..14), then frame += dt
//     sub_45CE90          0x0045CE90   Anim_SetFrame(node, clip, prev, cur, &d)
//     Anim_SetFrame       0x004715B0   Anim_RootDelta(clip, node+156, prev, cur)
//     Anim_RootDelta      0x004711D0   the sum of the root's 12-byte position
//                                      keys over (prev, cur], fractional ends
//                                      included, then ROTATED by the 3x3 at
//                                      node+156 when there is one
//     Actor_ApplyMotion   0x004672D0   gravity, the try-undo-Actor_Move step,
//                                      the 1/8 steer, the ground probe
//     Cef_ApplyTurn       0x0045C1B0   a turn block added to +416/+420/+424,
//                                      wrapped 0..360
//     Cef_ApplyRootShift  0x0045C2F0   a shift block rotated by the matrix at
//                                      actor+288 and added to the position
//
// **What the optional 3x3 in `Anim_RootDelta` is** - listed open in
// CLAUDE.md 6 - is answered here for the ACTOR path: node+156 is installed by
// `sub_437140(node, actor+288)` in `Actor_LoadModel` (0x0041A730) and
// `sub_452570`, and actor+288 is `Matrix3x3_FromEulerAngles(+416, +420, +424)`
// rebuilt every frame in `Actors_TickAll` (21_d3d.c 4448). It is the actor's
// FACING matrix, the same one `Cef_ApplyRootShift` rotates by. So a clip's
// root keys are authored in the character's own frame and turned into the
// world by his facing - which is why a walk clip walks the way he faces. (The
// SCENE path is different: `Script_SelectRelativeBodyAnimation` binds a
// `.3DA` track whose +156 is whatever `sub_437140(node, 0)` left, and that
// half stays as CLAUDE.md records it.)
//
// **Turning.** `docs/ASSETS.md` gives the two blocks. The locomotion group's
// turn states carry flag 0x40 - a turn `(startFrame, endFrame, dX, dY, dZ)`
// applied OVER THE WINDOW: `Cef_TickChannel` (29_win32.c 330-410) takes the
// frames advanced inside [start, end) times the per-frame rate and hands it
// to `Cef_ApplyTurn`, which adds to the Euler triple and wraps. So left/right
// is the `.CTL`, not the walker - and `Actor_ApplyMotion`'s 1/8 steer toward
// the motion direction is INERT for a root-driven walk, because the delta was
// rotated by the facing it would steer toward. Both are transcribed below.
//
// ------------------------------------------------------------ the camera
//
// `Camera_Request(mode, request)` (04_sys.c 1529) loads the preset row at
// 0x004C20C8 + 48*mode through `Camera_LoadParams` (0x004146C0): eye offset
// -> +176/+180/+184 when its subject is not -1, target offset -> +124/+128/
// +132, fov -> +48, and the three trailing shorts `f42/f44/f46` -> +136,
// +188, +192. Then the per-frame tick `sub_417CF0` (04_sys.c 3672) does, for
// any mode but 13, in this order:
//
//     +52..+60 = +20..+28 ; +64..+72 = +32..+40     the absolute slots
//     if +88  != -1: sub_415D10   the TARGET from its subject
//     if +140 != -1: sub_415E60   the EYE from its subject
//     ... flag 4 / 0x10 / 8 passes, sub_4133B0(eye), sub_4133E0(target)
//
// and the two resolvers are (sub_414F30 fills +100..+120 / +152..+172 with the
// subject actor's +244..+252 position and +416/+420/+424 Euler triple):
//
//     sub_415D10 (0x00415D10), the target:
//         n = +136 (f42; 1 -> 2; 0 when flags & 0x81)
//         P = subjPos - R(subjEuler) * targetOffset
//         if n: +64 += (P - +64) * dt / n      else +64 = P
//     sub_415E60 (0x00415E60), the eye:
//         m = +188 (f44), k = +192 (f46), both 1 -> 2, both 0 on flags & 0x81
//         if k: +76..+84 += wrap180(subjEuler - +76..+84) * dt / k
//               (a component within 0.1 degrees SNAPS)
//         else  +76..+84 = subjEuler
//         E = subjPos - R(+76..+84) * eyeOffset
//         if m: +52 += (E - +52) * dt / m      else +52 = E
//
// so mode 0 - `eye (0,0,-118.11) target (0,0,0) fov 75, f42 3 f44 8 f46 8`,
// which is 3.00 m behind at exactly the fov 4543 of the 5381 world cameras
// share - is: the target chases the player's position a third of the way per
// frame, the camera's yaw chases his facing an eighth of the way, and the eye
// chases the point 118.11 behind that lagged yaw an eighth of the way.
// `R` is `Matrix3x3_FromEulerAngles` applied by `Matrix3x3_RotateVector` in
// the row-vector convention, which with only the middle angle non-zero is the
// rotation about Y that `o3de/worldcam.h`'s `resolveCamera` already ports and
// this reuses rather than restates. `dt` is `flt_4C30DC`, the frame delta.
//
// **RECONSTRUCTION, and labelled so in the source header, the check and the
// coverage row (PORTING B2):**
//   * the three flagged passes after the resolve are NOT ported. `sub_414520`
//     case 0 -> `sub_413C00` gives mode 0 (and a world camera whose eye
//     subject is 0, through case 12 -> LABEL_19) flags 4|8|0x10 and a bank of
//     tunables (+312/+316 = -0.7 x actor height, +300 = 1.2, +320 = 8, +324 =
//     4, +288 = 8): flag 8 is `sub_417070`, a 271-line obstruction rule that
//     ray-tests from the target toward the eye (`sub_444810`) and pulls the
//     camera in and up to 0.7 x height when a wall is between; flag 0x10 is
//     `sub_416450`, a floor clamp that re-probes when the subject drops more
//     than two inches. With them absent the eye sits at the preset's own
//     height above the subject and passes through walls.
//   * whether the subject position `+244..+252` is the FEET or the pelvis is
//     not settled by this read. `ADDRESSES` records are on the floor and the
//     walker port keeps its position there, so the controller's `pos()` is the
//     feet and the model is drawn with its feet on it.
//   * which `Matrix3x3_FromEulerAngles` axis the facing turns about is the
//     same open item `worldcam.h` records; only the yaw is non-zero here.
//
// ------------------------------------------------------------ the standard
//
// TIER 5, data-constrained, like the two modules it joins: no capture can
// reach this (`state.h` says why - the trace rig sees only what a VM handler
// narrates), so what is asserted is what the shipped data can falsify: the
// clip named by the machine has a root track, its keys are finite and the
// integral of them is a walk (`verify.py: engine player walk`), every track
// resolves to a mesh of the model, the state machine leaves and re-enters the
// group's default entry, and the camera lands where the preset says.
#pragma once

#include "actor/pose.h"
#include "actor/state.h"
#include "actor/spatial.h"
#include "actor/walk.h"
#include "formats/anim.h"
#include "formats/ctl.h"
#include "formats/mesh3do.h"
#include "o3de/collision.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace omk {

// The camera-mode preset row for mode 0, out of `tables/camera_presets.json`.
// Quoted here so the controller can stand without the table file; the check
// asserts the table agrees.
inline constexpr float kFollowEyeBack = -118.1102f;   // inches: 3.00 m
inline constexpr float kFollowFov     = 75.0f;
inline constexpr int   kFollowF42 = 3, kFollowF44 = 8, kFollowF46 = 8;

struct FollowCamera {
    float eye[3] = {0, 0, 0};
    float at[3]  = {0, 0, 0};
    float fov = kFollowFov;
};

class PlayerController {
public:
    struct Setup {
        const CtlFile*              ctl     = nullptr;
        std::span<const std::byte>  ctlData;            // the bank's own bytes
        const std::vector<Mesh>*    meshes  = nullptr;  // the player's .3DO
        const TriangleSoup*         soup    = nullptr;  // the set's walkable soup
        // The narrow phase's faces (the steep soup serves: a wall is what
        // the walker cannot climb) and the sweep radius - the model's largest
        // collision sphere, `Actor_Move`'s `v60 * dword_910358` with the
        // tunable at 1.0. 0 leaves the sweep off.
        const TriangleSoup*         blockers = nullptr;
        float                       sweepRadius = 0.0f;
        // the model's own sweep spheres (`modelSweepSpheres`), in the model's
        // frame about the pelvis; the controller hangs them off the feet
        std::vector<CollisionSphere> sweepSpheres;
        const TriangleSoup*         steep   = nullptr;  // its faces PAST the
                                    // slope limit. `Walk_GroundResponse` does
                                    // not treat a steep face as a hole: it
                                    // stands the actor on it, adds the face
                                    // normal to his horizontal velocity and
                                    // writes 11.811 into the vertical one, and
                                    // he slides off. Optional - with none the
                                    // walker behaves as it did before it could
                                    // see them.
        float pos[3] = {0, 0, 0};   // where he stands; probed DOWN onto the
                                    // floor, so a pelvis or a feet point both
                                    // land him on the ground
        float facing = 0.0f;        // degrees, actor +420
    };
    explicit PlayerController(const Setup& s);

    // One frame. `dtFrames` is the engine's delta - `30.0 / fps`, one unit
    // per frame at 30 Hz (PORTING A7); `inputWord` is the 14-bit word
    // `Input::frame` made, 0 when nothing is held.
    void tick(float dtFrames, std::uint32_t inputWord);

    // A TELEPORT from a script - `actor.goto_address` (73) through the
    // Session's `placeActorAt`: the position is written outright and seated
    // on the floor under it the way the constructor seats the start, the
    // facing is the address's yaw. The `.CTL` channel is NOT reset: the
    // engine's `sub_41BF50` writes +404/+408/+412 and the euler and touches
    // nothing else. `start_` moves too, so `distanceWalked` measures the walk
    // from where he was put down, not from where he was first built.
    void placeAt(const float pos[3], float facing);
    // ...and the same WITHOUT the floor seat, for a body the ground does not
    // hold up. `sub_457F50` writes a rider's `+248` straight from the
    // slider's own y (`slider.y + 10`) and calls `o3de_SetNodePos` - there is
    // no `Walk_ProbeGround` anywhere in it, because a hovering vehicle is not
    // standing on anything. Going through `placeAt` instead dropped the rider
    // onto the pavement while his slider hovered 30 units over him.
    void rideAt(const float pos[3], float facing);

    // ---- what `MDACTION`'s slider arm installs, and it is two facts -----
    //
    // **The root frame.** `Anim_RootDelta`'s optional 3x3 is node+156, and
    // `sub_437140(node, matrix)` is what writes it. For an ordinary actor
    // `Actor_LoadModel` points it at actor+288, his own facing matrix - which
    // is why a walk clip walks the way he faces (see the header). The slider
    // arm of `MDACTION` (0x0046AEC0) points it at the SLIDER'S matrix
    // instead, `sub_438450(slider)` = slider node+140, so `H_SLDIN`'s
    // authored step-through-the-door runs in the VEHICLE's frame however the
    // man happens to be turned. Given as the matrix's own rows rather than an
    // angle, because the pool's heading and this class's euler do not share a
    // yaw convention and converting between them here would decide that
    // question silently.
    void setRootFrame(const float localX[3], const float localZ[3]);
    void clearRootFrame() { haveRootFrame_ = false; }

    // **No motion pass.** ACTOR_STATE 6 and 8 tick through
    // `Actor_TickChannelOnly` (0x00466B00), whose whole body is
    // `Cef_TickChannel` and an error trace - there is no `Actor_ApplyMotion`,
    // so no gravity, no ground probe and no walker collision. The clip's root
    // delta still moves him, through `Actor_MoveBy`. Ticking the walker as
    // well makes the two fight over a body that is climbing into a vehicle
    // hovering 30 units off the road.
    void setChannelOnly(bool on) { channelOnly_ = on; }

    // ...and where the arm PUTS him before the clip plays. The engine snaps
    // the actor to
    //
    //     slider + M_slider . (root0(clip of group `groupId`) - root0(ref))
    //
    // where `root0` is `sub_471100` - the first position key of the first
    // track that has any - and `ref` is `ANIMS\slf_112.3da`, which
    // `Game_Init` loads into `dword_90EF28` (05_sys.c 1842) for exactly this.
    // Both clips are authored around one slider, so the difference is where
    // the man stands beside its door. This returns that offset, in the
    // slider's own frame; the caller supplies the reference clip because it
    // is a `.3DA` from the tree and not part of the bank.
    bool boardOffset(std::span<const std::byte> refClip, int groupId,
                     float out[3]);
    // The crowd push (`Actor_TickNpc`: `f32(actor,244) += push[0]` ... and
    // `o3de_MoveNodeBy`, before `Actor_ApplyMotion`): the position moved
    // outright, no floor probe - the next tick's motion probes the ground.
    void nudge(const float d[3]);
    // The `tab_special_move[]` names the channel fired THIS tick, in order.
    // The handlers are the engine's and most of them need the world, so the
    // frontend runs them; see player.cpp. Cleared at the top of every tick.
    const std::vector<std::string>& specialMoves() const { return moves_; }
    // `SetPersoBankGroup`'s three input writes without its group switch - the
    // queue, its count and `lastInput_`, and NOT the 20-slot latch, which the
    // engine's memset stops one dword short of (`CefChannel::setBankGroup`).
    // The frontend calls it when an MDACTION did something but the bank switch
    // could not be made, the way `sub_465D30` does.
    void resetInputQueue() { rt_.channel().resetInputQueue(); }

    // `sub_465D30`'s tail is
    //
    //     if (dy <= 27.472441)  g = Cef_FindGroupById(bank, 143);   // low
    //     else                  g = Cef_FindGroupById(bank, 41);    // standing
    //     SetPersoBankGroup(channel, g);
    //
    // and it is reached ONLY when MDACTION's object scan found something.
    // There WAS an `enterActionBank()` here that installed group 41 on any
    // successful press; it is gone, because a press that merely activates a
    // zone never reaches `sub_465D30` and installing the take bank for one
    // played a take of nothing - a reader finishing a shop seller's
    // conversation watched the whole graph run (2026-09-06). The take's own
    // switch lives where the scan does, in `play.cpp`, through the call below.
    //
    // 27.472441 units is 0.6978 m - the low/standing split. **The LOW arm is
    // still not ported**: choosing it needs the object's height, which
    // `sub_465D30` computes and the press does not carry, so a low object
    // plays the standing take. `todo/next-tasks.md` 1.
    //
    // `Cef_FindGroupById` + `SetPersoBankGroup`, by the group's ID.
    bool enterGroupById(int id);
    // `Player_GoToMove` (0x0041B6F0) - the whole of `player.move` (63) and the
    // start of `player.move.wait` (89): `Cef_FindGroupById(actor+180, id)`,
    // `SetPersoBankGroup(actor+396, group)` - the queue cleared and the
    // machine put on the group's flag-0x20 entry THIS tick - then the pitch
    // (+416) zeroed, the motion state cleared (+216..+224, +280/+284, byte
    // +1304, `dword_6A52CC`) and the facing matrix at +288 rebuilt. Group 100
    // is the locomotion group and its entry is the stand, so `player.move
    // 100` - 60 of the 312 shipped sites, in front of every staged sequence
    // - stops a walking player dead where he is: the walk clip is left, not
    // played out, and no root motion follows. False when the bank has no such
    // group ("SetPersoBank, error on GoToMove"), and the caller runs on.
    bool goToMove(int groupId);
    // `sub_45ABB0(channel)`: `[state + 56]`, the group the channel's current
    // entry belongs to - what `Game_Tick` compares to the group a
    // `player.move.wait` recorded, to release the script when it changes.
    // -1 with no current entry.
    int  ctlGroup() const;

    // `Actor_EnterDialogueMode` (0x00468DE0) and `Actor_LeaveDialogueMode`
    // (0x00468E80), which bracket every conversation. The runtime has carried
    // both since the ACTOR_STATE machine was ported; these are the frontend's
    // handles on them, because it is the frontend that knows when the
    // Session's conversation opens and closes.
    //
    // The half that matters outside the state machine is the BANK SWITCH:
    // enter installs group 400 with the channel's input disabled, leave
    // installs group 100 - the bank's default, `H_STAND` - and re-enables it.
    // `SetPersoBankGroup`'s memset clears the queue and the latches, so the
    // ENTER that dismisses a conversation's last line cannot go on to match
    // `H_STAND -> 24` and fire MDACTION while it is still held.
    bool enterDialogueMode() { return rt_.enterDialogue(); }
    bool leaveDialogueMode() { return rt_.leaveDialogue(); }

    // SHOOT MODE's half of the same shape: `Shoot_Enter` puts the player in
    // `ACTOR_STATE` 3 and installs `.CTL` group 200, `Shoot_Leave` puts him
    // back to 1 - and, unlike the dialogue pair, nothing here blocks his
    // input, because the whole point of the mode is that he still moves.
    // `actor/shootmode.h` is the mode; this is the actor.
    bool enterShootMode() { return rt_.shootEnter(); }
    // `Shoot_Leave` (0x00422730) puts the player in state 1 AND ends with
    // `SetPersoBankGroup(bank, Cef_DefaultGroup(+180))` - the machine back on
    // the bank's default group, the locomotion one. Without it he stayed on
    // group 200's shoot stance after a phase ended, whose movement entries
    // queue the shoot mover's moves with no mover to answer them: he could
    // not walk (a reader, 2026-09-10, after the supermarket's ending).
    bool leaveShootMode() {
        const bool ok = rt_.shootLeave(true);
        const int g = rt_.channel().defaultGroup();
        if (g >= 0) rt_.channel().setBankGroup(g);
        return ok;
    }

    // --- what the zone scan and the frontend take ---------------------
    const float* pos() const { return pos_; }
    float facing() const { return euler_[1]; }
    // FIRST-PERSON AIM. Shoot mode turns the body with the mouse rather than
    // with the walker's own turn keys, so the frontend needs a way in. It
    // writes the same `euler_[1]` the walker does and wraps it the way
    // `sub_424DE0`'s epilogue wraps the AI's - into (-360, 360).
    void aimYawBy(float deg) {
        euler_[1] += deg;
        if (euler_[1] >= 360.0f) euler_[1] -= 360.0f;
        else if (euler_[1] < 0.0f) euler_[1] += 360.0f;
    }
    // SHOOT MODE'S MOVER (`actor/shootmove.h`): `sub_47D4D0`'s world step in
    // x and z, added to THIS tick's motion before `Actor_ApplyMotion` tries it
    // against the ground - the engine writes it into +244/+252 beside the
    // clip's root delta, so the walls that stop a walk stop this too.
    void addShootMotion(float dx, float dz) { shootMotion_[0] += dx; shootMotion_[1] += dz; }
    const float* euler() const { return euler_; }
    ActorState state() const { return rt_.state(); }
    int ctlState() const { return rt_.channel().state(); }
    // The current entry's `+12` whole - `sub_45AB80` (0x0045AB80) is
    // "channel +184 is set and its +12 is -1", which is how the channel tick
    // chooses the SHOOT branch (`actor/shootfire.h`). 0 when no entry.
    std::uint32_t ctlEntryFlags12() const {
        const int s = rt_.channel().state();
        if (!ctl_ || s < 0 || s >= static_cast<int>(ctl_->states.size())) return 0;
        return ctl_->states[static_cast<std::size_t>(s)].flags12;
    }
    // The channel's own frame (1..len) - the clock `Cef_TickEffects` reads.
    float channelFrame() const { return rt_.channel().frame(); }
    // The current entry's +28 effect records (`CtlEffect`), empty when none.
    const std::vector<CtlEffect>& stateEffects() const;
    // The entry that OWNS the clip being played: a junction (flag 0x8000)
    // or an alias (flag 2) is a state the channel sits in while blending,
    // and `Actor_BlendToClip` binds the clip behind its GoTo chain - which
    // is what `sub_45C680` reads the root delta from and what the skeleton
    // shows. `GoToMove` chases the same chain (`channel.cpp`, "real").
    int clipOwner() const;
    const std::string& ctlStateName() const;
    int clip() const;                  // index into CtlFile::clips, -1
    const std::string& clipName() const;
    float clipFrame() const { return rt_.channel().frame(); }
    // THE SOUNDS THIS FRAME STARTED - the `.CTL` state's effect records, which
    // is where adventure mode's footsteps and jump land (`H_WALK` carries the
    // pair 203/199, one per footfall). Ids into the RESIDENT scene's chunk-3
    // table, resolved by `Scene_FindSoundIndex`, not indices.
    const std::vector<CefChannel::EffectSound>& sounds() const {
        return rt_.channel().sounds();
    }
    int clipFrames() const;

    // --- posing --------------------------------------------------------
    // The current clip's tracks as `composePose` takes them: `ids` are mesh
    // indices matched by NAME against the model, quaternions keyed the way
    // `.3DA` clips are (key 0 the rest sentinel, frame f reads key f + 1),
    // and NO root translation - the root motion is `pos()`, integrated,
    // which is what the engine does with it (the node moves; the pose is the
    // quaternions alone).
    const NodeTracks* poseTracks();
    // Any clip's RAW tracks, cached and without the variant grid, by its
    // index into `CtlFile::clips`. The shoot aim layer samples `S_AUTOLK`
    // and the stance beside the running clip (`actor/shootaim.h`).
    const NodeTracks* clipTracks(int clip);
    // The clip of a `.CTL` group's default entry - `Cef_DefaultClip(
    // Cef_FindGroupById(bank, id))` - or -1.
    int groupDefaultClip(int groupId) const;
    int poseFrame() const;             // 0-based frame into the tracks

    // ---- THE VARIANT GRID (omk-play 69) --------------------------------
    //
    // A take clip is not one motion: it is a GRID of them, and the engine
    // plays a 21-frame window blended from the four cells nearest the
    // approach. The entry's `playBits` (+0x4C) high nibble is the variant
    // count - `sub_4A7F25` does `mov ax,[edi+4Ch]; shr eax,0Ch` and hands it
    // to `sub_45C3B0`, which stores it at actor+0x4F4 - and it is > 1 for
    // exactly nine states in `H1Avnt`: the take/put families and `H_ADJSTP`.
    //
    // `sub_465D30` computes the two axes at the moment the take starts and
    // leaves them at actor+0x1C4 (the signed approach ANGLE) and +0x1C8, and
    // `sub_466390` turns them into four key offsets and two 0..256 weights.
    // They are constant for the whole of one take, so the caller sets them
    // once and this bakes the blended window.
    //
    // `second` is the raw second axis; the +-50 and [-53, +51] clamps are
    // applied here, as the engine applies them inside `sub_466390`.
    void setTakeGeometry(float angleDeg, float second);
    // The ADJUST STEP uses the OTHER builder, `sub_466210`: the angle's
    // QUADRANT picks one cell, its sign picks the other, and a 0..256 fraction
    // of `angle * 256/90` blends them - no second axis at all (`out[4]` is
    // written 0, so the cross-blend collapses). Group id 600, clip H_ADJSTP.
    void setAdjustStep(bool on) { if (adjust_ != on) { adjust_ = on; ++takeGen_; } }
    // THE STEP IS SCALED TO THE DISTANCE, NOT PLAYED AS AUTHORED. `sub_465D30`
    // sets `dword_6A5380 = |D - target| * 0.0508` (the error over the 50 cm
    // step) and `dword_53AE1C = 1`, and the channel tick (`sub_45C680` case
    // 0xD/0xE/0x10) then runs the root delta through `sub_466540` - x and z
    // multiplied by that global - while the flag is set and the actor is the
    // player. So one authored step lands exactly on the target point.
    void setStepScale(float s) { stepScale_ = s; }
    // `Actor_Move(actor, dx, 0, dz, ..., 1, 1, 0)` from `sub_465D30`: the
    // move to the target point, through the walker's floor probe, before a
    // take starts. Returns whether the walker let it stand.
    bool moveBy(float dx, float dz);
    // The current entry's variant count, 0 or 1 meaning "an ordinary clip".
    int variantCount() const;

  private:
    // The four cells and the two 0..1 weights `sub_466390` produces.
    struct GridSample { int cell[4] = {0, 0, 0, 0}; int len = 0;
                        float wSecond = 0.0f, wAngle = 0.0f; };
    GridSample gridSample(int n, int keys) const;
    const NodeTracks* gridTracks(const NodeTracks& base, int n);

  public:
    int tracksMatched() const { return matched_; }
    int tracksTotal() const { return total_; }

    // --- the follow camera --------------------------------------------
    // Mode 0 by default. A world camera whose subjects are the player (SCENE
    // 55's camera 0, which `camera.set 0,0,2` names) carries its own offsets
    // and travels the same code path, so they can be handed in.
    void setCameraOffsets(const float eyeOff[3], const float atOff[3], float fov,
                          int f42 = kFollowF42, int f44 = kFollowF44,
                          int f46 = kFollowF46);
    // The smoothed camera as it stands after the last tick.
    // How far the camera's SUBJECT sits above the controller's own position,
    // in inches. `pos()` is a FLOOR point - the walker keeps it there and the
    // model is drawn with its feet on it - but the camera preset's subject is
    // the actor's own origin, and preset mode 0 offsets the eye by
    // (0, 0, -118.11) and the TARGET by (0, 0, 0): a third-person camera whose
    // eye and target both sit at the subject's own height only makes sense if
    // that subject is a BODY point. It is the model's hierarchy root, the
    // pelvis, and this is its height above the feet - 41.9 for `HO1_FNM`,
    // which is the 41.8 the dialogue staging measured from the other side
    // (CLAUDE.md 6, "HO1_FNM's own standing pelvis->feet is 41.8").
    //
    // Filed as issue 49 from a play report - *the camera on adventure mode is
    // set too low* - and it is what `player.h` above records as unsettled:
    // "whether the subject position +244..+252 is the FEET or the pelvis is
    // not settled by this read". It is the pelvis.
    float cameraLift() const { return camLift_; }
    // THE SHOOT CAMERA'S EYE LIFT, and it is the ENGINE'S rule.
    //
    // `Camera_Request` stores the subject at `cam[23]` and calls
    // `sub_414520`, whose **case 4** - the mode `Shoot_Enter` asks for - is:
    //
    //     v2 = actor[+16] ? 0.7 : 0.0;
    //     f32(cam, 180) = f32(cam, 128) = v2 * actor[+276];
    //
    // and `+276` is written by `Actor_LoadModel` from the model's own SPHERE
    // list, `max(centre.y + radius)` - the lowest point of the body below the
    // actor's origin. So the eye sits **seven tenths of the way from the
    // pelvis down to the feet**, which for HO1_FN is about 25 units.
    //
    // This replaced a reconstruction (the `...Tete` bone, 20.8) that was only
    // ever a guess dressed as a measurement. The reader's instruction was the
    // right one: look at the original code.
    float headLift() const { return headLift_; }
    const FollowCamera& followCamera() const { return cam_; }
    // --- the camera's COLLISION pass ------------------------------------
    // `sub_413C00` sets the ordinary follow camera's flags to `0x1C` - 4 | 8 |
    // 0x10 - and in the tick `sub_417CF0` flag 4 opens the collision branch,
    // 0x10 runs `sub_416450` first and flag 8 selects `sub_417070` as the arm.
    // (A camera with 4 but not 8 takes `sub_416570` instead, which is the same
    // idea in nine lines: cast target -> eye, and on a hit put the eye at the
    // hit point.) Hand it the solid faces and it runs; hand it nothing and the
    // camera behaves as it did before, which is what every headless check that
    // does not set one still measures.
    void setCameraSolids(const TriangleSoup* a, const TriangleSoup* b) {
        camSolidA_ = a; camSolidB_ = b;
    }

    // Where it settles for the current position and facing - the resolve
    // with no lag, which is what a check can pin.
    FollowCamera followCameraSteady() const;
    // The same resolve for ANOTHER preset's offsets, against his own facing
    // with no lag - what `Camera_Request(mode)` gives a preset whose three
    // smoothing divisors are 0. Mode 1, the TAKE camera, is one (omk-play 69).
    FollowCamera resolveOffsets(const float eyeOff[3], const float atOff[3], float fov) const;

    // --- diagnostics ---------------------------------------------------
    struct Frame {
        float rootDelta[3] = {0, 0, 0};   // the clip's, already rotated
        float shift[3]     = {0, 0, 0};   // Cef_ApplyRootShift's, this tick
        float turn         = 0.0f;        // Cef_ApplyTurn's yaw, this tick
        StepResult step    = StepResult::Moved;
        bool  stepped      = false;       // was a step asked for at all
        double ground      = 0.0;         // the floor under him after the tick
        bool  onGround     = false;
    };
    const Frame& last() const { return last_; }
    const ActorRuntime& runtime() const { return rt_; }
    // ...and the writable half, for the two slider states. `MDACTION` writes
    // `+404 = 6` and `sub_468FA0` writes both `+404` and `+408` to 8; the
    // port drove the `.CTL` group for both and left ACTOR_STATE at 1, so the
    // table's own `kAny -> 6` and `6 -> 7` rows were never exercised by the
    // path that is supposed to exercise them.
    bool setActorState(ActorState to, const char* writer) { return rt_.setState(to, writer); }
    // THE JUMP, `tab_special_move` rows 14/15/16 (`MDJUMP0A`, `MDJUMP0B`,
    // `MDJUMP01`) - the arithmetic, so the frontend only has to name the move.
    // Returns false when the entry carries no hang time or he is already off
    // the ground. `walk.h`'s `jump` has the reading; this supplies its three
    // numbers from the live entry and the facing.
    // TWO STAGES, because the engine has two and they land on DIFFERENT
    // entries. `MDJUMP0A` computes the launch and parks it in
    // `flt_53AE50/54/58`; `MDJUMP01` copies those into the actor some frames
    // later. Measured in `H1AVNT`, 0A fires on `H_WKJUMPR` (whose `+12` is 14)
    // and 01 on `H_JUMPONR` (whose `+12` is 0), so reading N at the launch
    // reads the wrong entry and gets nothing - which is exactly what a first
    // version of this did, and the trace said `refused` on every jump.
    bool jumpPrepare();      // MDJUMP0A / MDJUMP0B
    bool jumpLaunch();       // MDJUMP01
    bool jumpArmed() const { return jumpArmed_; }
    // MDJUMP03 (0x0046BE40) - the LANDING. Clears the airborne flag, bands the
    // drop and, for anything but a short one, puts him into the landing
    // reaction. -> the band it decided (2 ordinary, 1 / 3 / 4 a reaction).
    int  jumpLand();

    // MDJUMP03's band table on its own, so it can be TESTED without a drop to
    // stand under. The reaction arm needs 1.50 m of fall and every fixture in
    // the suite stands on flat ground, so asserting this through a jump would
    // exercise band 2 and nothing else; a check that instead grepped the
    // source for the three constants would go red the day somebody moved them,
    // which is the failure shape a concurrent session hit three times over on
    // 2026-09-09 with call-site scans of `play.cpp`.
    //
    // 5.00 m -> 4, 3.00 m -> 3, 1.50 m -> 1, otherwise 2. The order is 2, 1,
    // 3, 4 with distance because `+1304` is a CODE, not a severity rank.
    static int jumpBand(double drop) {
        return drop >= 196.85039 ? 4
             : drop >= 118.11024 ? 3
             : drop >= 59.055118 ? 1
             : 2;
    }

    const Walker& walker() const { return walker_; }
    long ticks() const { return ticks_; }
    // How far the position has moved from the start, in the ground plane.
    double distanceWalked() const;

private:
    struct RootTrack { std::size_t offset = 0; int keys = 0; };
    const RootTrack* rootTrackOf(int clip);
    void rootDelta(const RootTrack& t, float prev, float cur, float out[3]) const;
    void rotateByFacing(const float in[3], float out[3]) const;
    void applyTurn(const float d[3]);
    static bool windowPortion(float prev, float cur, float start, float end,
                              float& portion);
    void resolveSteady(FollowCamera& c, const float eulerUsed[3]) const;

    const CtlFile*             ctl_;
    std::span<const std::byte> data_;
    const std::vector<Mesh>*   meshes_;
    ActorRuntime rt_;
    Walker       walker_;
    // the parked launch - `flt_53AE50` (x, per frame), `flt_53AE54 * 30`
    // (y, per second) and `flt_53AE58` (z, per frame)
    double       jumpV_[3] = {0.0, 0.0, 0.0};
    bool         jumpArmed_ = false;
    std::vector<std::string> moves_;
    float pos_[3];
    float start_[3];
    float euler_[3] = {0, 0, 0};       // +416, +420, +424
    float shootMotion_[2] = {0, 0};    // `addShootMotion`, spent by the next tick
    float camLift_ = 0.0f;             // pelvis above the feet - see cameraLift()
    float headLift_ = 0.0f;            // 0.7 * the model's extent - see headLift()
    // `sub_417070`'s state. `+328` is the distance it keeps between frames -
    // the camera snaps IN to a blocking hit and eases back OUT over `+320`
    // frames - and `+208` is 0 clear / 1 blocked / 2 recovering.
    const TriangleSoup* camSolidA_ = nullptr;
    const TriangleSoup* camSolidB_ = nullptr;
    float camDist_ = 0.0f;             // +328
    int   camBlock_ = 0;               // +208
    // FLAG 1, the "just changed" bit: `Camera_LoadParams` sets it, the tick
    // clears it on its way out, and while it is set BOTH of the collision
    // pass's easings are skipped and the camera snaps. `camFresh_` is the
    // same bit for the eye/target smoothing; this is the frame it covers.
    bool  camJustChanged_ = false;
    void  cameraCollide(float dt);
    float frameBefore_ = 1.0f;         // the channel's +12/+16 pair as the
    float frameAfter_  = 1.0f;         // window rule reads them
    int   stateBefore_ = -1;
    long  ticks_ = 0;
    // **There is no queue shadow here any more.** This controller used to
    // carry one, because `CefChannel::tick`'s commit was missing
    // `Cef_TickChannel`'s LABEL_75 - a queue whose only word is the idle one
    // is DROPPED before a pressed word is pushed - so a press queued behind
    // the idle word `setBankGroup` seeds and the machine stalled until
    // something popped it. The shadow re-injected the word to force the same
    // effect. `channel.cpp` carries the rule itself since 2026-09-02
    // (todo/pending/T17.md) and the shadow is gone; the measured walk is
    // unchanged by its removal, which is what says the rule and the shadow
    // were doing the same job.
    Frame last_;

    std::map<int, RootTrack>  roots_;
    std::map<int, NodeTracks> tracks_;
    // The blended window for a variant-grid clip, and the geometry it was
    // baked from. Keyed on the clip AND on a generation that the setter bumps,
    // because unlike an ordinary clip the result depends on the approach.
    NodeTracks gridTracks_;
    int   gridClip_ = -1, gridGen_ = -1;
    float takeAngle_ = 0.0f, takeSecond_ = 0.0f;
    int   takeGen_ = 0;
    bool  adjust_ = false;
    float stepScale_ = 1.0f;           // `dword_6A5380`, 1.0 outside a step
    bool  haveRootFrame_ = false;      // node+156 is not actor+288
    float rootFrameX_[3] = {1, 0, 0};  // the matrix's row 0 and row 2
    float rootFrameZ_[3] = {0, 0, 1};
    bool  channelOnly_ = false;        // Actor_TickChannelOnly: no motion pass
    int matched_ = 0, total_ = 0;

    // the camera block's live state
    float camEyeOff_[3] = {0, 0, kFollowEyeBack};
    float camAtOff_[3]  = {0, 0, 0};
    float camFov_ = kFollowFov;
    int   camF42_ = kFollowF42, camF44_ = kFollowF44, camF46_ = kFollowF46;
    float camEuler_[3] = {0, 0, 0};    // +76..+84, the lagged copy
    bool  camFresh_ = true;            // flags & 1: the first frame snaps
    FollowCamera cam_;
};

// The heading `Actor_TickNpc` derives when +1308 is set - and
// `Actor_TickScxDriven` sets it for the player when his scene program ends,
// which is exactly the hand-over: `Matrix3x3_RotateVector(0, 0, -1, node+92)`
// then `atan2(z, x) * 180/pi + 90`. The node's matrix after a scene clip is
// the clip's ROOT quaternion, so this takes that quaternion (stored as the
// conjugate, like every other track - `pose.h`) at the frame reached and
// returns the heading in the +420 convention. RECONSTRUCTION in one respect:
// `Actor_SetEuler(param 4/5/6)` composes over it, and the Impasse's objects
// author 0 there, so the composition is not exercised.
float headingFromClipRoot(std::span<const std::byte> clip, int frame);

// Facing -> the row-vector rotation `Matrix3x3_FromEulerAngles(0, yaw, 0)`
// applies: forward is (sin yaw, -cos yaw), the convention `ADDRESSES` and
// `resolveCamera` use.
void rotateYaw(float yawDeg, const float in[3], float out[3]);

}  // namespace omk

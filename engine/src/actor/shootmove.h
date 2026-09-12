// SPDX-License-Identifier: GPL-3.0-or-later
//
// MOVING IN FIRST PERSON - shoot mode's own mover (`todo/shoot-mode.md` 8.5b).
//
// Asked for 2026-09-10 (*"don't forget the integration of moving while in fps
// mode"*): in ACTOR_STATE 3 the port never moved the player, and the reason is
// in the data. `H1Avnt` group 200 - the shoot stance - has NO walk clip. Its
// movement entries are all `clip -1`, and each queues a SPECIAL MOVE:
//
//     bit 4     Avancer            -> MDAV     bit 8     Reculer   -> MDAR
//     bit 0x400 Glisser a gauche   -> MDDG     bit 0x800 Glisser a droite -> MDDD
//     bit 1/2   Tourner            -> MDRG / MDRD
//     bit 64    S'accroupir        -> MDDO, and MDUP to stand
//
// The handlers are 16-byte stubs at 0x0046B630.. with no `proc` label, read
// from the BYTES, and they write INTENTS into a 0x6C-byte block at
// `dword_6579B0` and nothing else:
//
//   * `sub_47CFC0(1 / 0)` (MDAV / MDAR): the forward intent `flt_6579B8 =
//     -/+ accel` and flag 4 - a reversal zeroes the velocity first - or, in
//     HEAD mode (flag 0x200), a pitch step of 2 a frame, clamped at +-45;
//   * `sub_47CF70(0 / 1)` (MDDG / MDDD): the side intent `flt_6579B4 = +/-
//     accel` and flag 2;
//   * `sub_47CF50` / `sub_47CF40` (MDDO / MDUP): the crouch flag 0x20 on, off;
//   * both movers refuse while flag 0x800 or the actor's fall byte (+1304) is
//     set;
//   * MDRG / MDRD reach `sub_47D370(-/+50, 0)` - the TURN the mouse drives too.
//
// `sub_47D4D0` is the mover, and `Actor_TickShoot` calls it BEFORE the
// channel tick: the forward (`dword_6579C4`) and side (`dword_6579BC`)
// velocities accelerate while their intent is raised and brake toward 0 when
// it is not, the pair is turned by the facing into a world step, and the step
// goes to `o3de_MoveNodeBy` AND into +244/+252 - the motion `Actor_ApplyMotion`
// then tries against the ground, so the walls are the walker's. The intents are
// ONE-SHOT: the tick ends `flags &= 0x17F0`, so a held key re-queues its move
// every frame and a released one brakes.
//
// The speeds come from `sub_47CC70`, the mode's init (`Shoot_Enter`): property
// 3 - SPEED, record +158 - picks a row of the table at 0x004CF7D0.
//
// THE SHOVE, `sub_47D1F0` - the HURT REACTION, ported 2026-09-12 - is the one
// thing in this block that is not the player's own doing. `sub_4240E0` calls
// it on every bolt he SURVIVES (the death returns before it), and on the
// explosion arm with a fixed band 2; it plays a sound, points the shove by the
// hit's DIRECTION BAND and arms a four-frame timer that the mover below spends
// on the actor's own Euler:
//
//   * the sound is `word_657A14` through `Scene_FindSoundIndex(&stru_930780)`,
//     i.e. the resident library, which in shoot mode is `shoot2.scx`. Its `a3`
//     is **0**, so `Sound_Play3D` takes the `v6[12] = 4` arm and the sound is
//     NOT positional - it is his own hurt, played flat. The fourth argument is
//     the block's address, an owner tag, not a position;
//   * band 0/1 (`|dot| <= 0.5`, the bolt from the SIDE) sets `dword_657A08`,
//     which moves +424 - the ROLL; band 2/3 (from the front or behind) sets
//     `dword_657A0C`, which moves +416 - the PITCH. Each is +-1.0, and the two
//     halves of the reading corroborate: a bolt ACROSS him rolls him, one
//     ALONG him tips him;
//   * `dword_6579F4 = 4.0` and flag 0x400. The mover then spends it: while the
//     timer is at or above **2.0** it SUBTRACTS the pair times dt, below 2.0 it
//     ADDS them back, and at 0 it clears the flag and zeroes both angles. At 30
//     fps that is two frames out to 2 degrees and two frames back.
//
// It reaches the CAMERA because a subject-relative camera point is rotated by
// the subject's whole Euler, not by its yaw: `sub_414F30` copies the actor's
// +416/+420/+424 into the camera block's +112/+116/+120 and `sub_415D10` hands
// all three to `Matrix3x3_FromEulerAngles`. (That closes `o3de/worldcam.h`'s
// "written by something this read did not find" - the writer is the subject
// resolver, one of `sub_414F30`..`sub_415460`, and it is the actor's Euler.)
//
// **Only the PITCH bands can show**, and that is the engine's arithmetic and
// not a simplification here: the shoot preset's eye offset is (0,0,0) and its
// target offset (0, 0, 787.4016), and a roll turns about that very axis - so
// bands 0 and 1 move the first-person view by nothing at all, while 2 and 3
// swing the target 27.5 inches, a 2-degree tip. Worth saying rather than
// claiming a roll nobody can see.
//
// NOT modelled, each labelled where it would sit: the head bob and the
// footsteps (+188/+192, the stance clip's frame over its length, which also
// HOLDS the stance at frame 1 while he stands; `word_657A16`/`word_657A18` are
// their two sounds and are carried below), the turn momentum (flag 8 - nothing
// writes it),
// the jump in shoot mode (MDJP -> `sub_47D2E0`, and the vertical `flt_6579C0`
// it feeds, which is decayed here but never raised), and the HEAD mode's key
// (action 7 has no keyboard binding in the shipped scheme). MDCO (run, flag 1)
// and MDTR (flag 0x40) are ported but no H1Avnt entry queues either.
#pragma once

#include <cstdint>

namespace omk {

// 0x004CF7D0, thirty rows of {Speed up to, top-speed term, acceleration term};
// the first row whose bound is not below the actor's Speed is his. Speed is
// clamped to 200 by `Actor_SetProperty` (`cmp esi, 0C8h`), so the walk - which
// has no bound of its own - always stops on or before the last row.
struct ShootSpeedRow { int upTo, top, accel; };
inline constexpr ShootSpeedRow kShootSpeedTable[30] = {
    {50, 9, 7},    {55, 9, 8},    {60, 10, 8},   {65, 10, 9},   {70, 11, 9},
    {75, 11, 10},  {80, 12, 10},  {85, 12, 11},  {90, 13, 11},  {95, 13, 12},
    {100, 14, 12}, {105, 14, 13}, {115, 15, 13}, {120, 15, 14}, {125, 16, 15},
    {130, 16, 15}, {135, 17, 16}, {140, 17, 16}, {145, 18, 17}, {150, 18, 17},
    {155, 19, 18}, {160, 19, 18}, {165, 20, 19}, {170, 20, 20}, {175, 20, 20},
    {180, 21, 21}, {185, 22, 22}, {190, 22, 22}, {195, 23, 23}, {200, 24, 23}};

// `dword_6579B0`'s bits, as the handlers and the mover use them.
enum : std::uint32_t {
    kShootMoveRun    = 0x1,     // MDCO (0x0047CF20)
    kShootMoveSide   = 0x2,     // a side intent this frame
    kShootMoveFwd    = 0x4,     // a forward intent this frame
    kShootMoveTurn   = 0x8,     // the turn momentum - nothing writes it
    kShootMoveHold   = 0x10,    // the velocities frozen - nothing sets it
    kShootMoveCrouch = 0x20,    // MDDO; halves the top speed
    kShootMoveTr     = 0x40,    // MDTR (0x0047CF30)
    kShootMoveHead   = 0x200,   // MDHEAD01: the move keys pitch the look
    kShootMoveShove  = 0x400,   // `sub_47D1F0`'s knockback
    kShootMoveBlock  = 0x800,   // refuses the intents
    kShootMoveMeca   = 0x1000,  // the shooter is a Mecagarde: he cannot PITCH
};

// The block at `dword_6579B0`, the fields the mover reads.
struct ShootMover {
    bool          active = false;   // `dword_6579CC`, the shooter, non-null
    std::uint32_t flags = 0;        // `dword_6579B0`
    float sideIntent = 0.0f;        // `flt_6579B4`
    float fwdIntent  = 0.0f;        // `flt_6579B8`
    float side       = 0.0f;        // `dword_6579BC`, the side velocity
    float vertical   = 0.0f;        // `flt_6579C0`
    float fwd        = 0.0f;        // `dword_6579C4`, the forward velocity
    float period     = 0.0f;        // `dword_6579C8`, the stance clip's frames
    float top        = 0.0f;        // `flt_6579D0`
    float accel      = 0.0f;        // `flt_6579D8`
    float brake      = 0.0f;        // `flt_6579E4`
    float lastFacing = 0.0f;        // `dword_6579FC`, the facing the step turns by
    int   row        = -1;          // which table row, for the log
    // THE SHOVE (`sub_47D1F0`), the hurt reaction's own three.
    float shoveRoll  = 0.0f;        // `dword_657A08`, what +424 moves by
    float shovePitch = 0.0f;        // `dword_657A0C`, what +416 moves by
    float shoveTimer = 0.0f;        // `dword_6579F4`, 4.0 at the hit
    // The three sound ids `sub_47CC70` picks off the shooter's character type
    // (property 7, the record's +80). Type 5 is the Mecagarde - the same test
    // that gives him HUD screen 33 instead of 34 - and he is mechanical: his
    // hurt is MVTMECA03.WAV and his steps 0041/0042.WAV, where flesh takes
    // IMPACT03.WAV and STPL/STPR.WAV. Flag 0x1000 goes up for the Mecagarde -
    // `BYTE1 |= 0x10` is bit TWELVE, which is the pitch lock below.
    int hurtSound = 191;            // `word_657A14`  - IMPACT03.WAV
    int stepLeft  = 161;            // `word_657A16`  - STPL.WAV, NOT MODELLED
    int stepRight = 162;            // `word_657A18`  - STPR.WAV, NOT MODELLED
};

// The walk over the table: `for (i = &unk_4CF7D0; speed > *i; i += 3)`.
int  shootSpeedRow(int speed);
// `sub_47CC70`: the block zeroed, the shooter set, and the three speeds from
// the row - `top = (30 * a / 100 + 5) * 1.3` with the division an INTEGER one,
// `brake = top * 0.2`, `accel = b * 0.043333333`. `charType` is property 7,
// which picks the hurt and footstep sounds and flag 0x10.
void shootMoveInit(ShootMover& m, int speed, float periodFrames, int charType = 0);

// `sub_47D1F0`, the HURT REACTION - what `sub_4240E0` does with a bolt the
// player SURVIVES. `band` is `sub_423E20`'s (`shootHitBand`), 0..3; the two
// Euler angles are the actor's +416 and +424, which it zeroes before arming
// the shove. False when no shooter is installed, in which case nothing is
// touched. The SOUND is the caller's: play `m.hurtSound` flat (see above).
bool shootHurt(ShootMover& m, int band, float& eulerPitch, float& eulerRoll);

// `sub_47D370`'s own guard: false for a Mecagarde, whose pitch is pinned at 0.
inline bool shootMovePitches(const ShootMover& m) { return !(m.flags & kShootMoveMeca); }
// `sub_47CE70`, `Shoot_Leave`'s: the shooter cleared.
void shootMoveLeave(ShootMover& m);
// `sub_47CFC0` (MDAV = forward, MDAR = back). `falling` is the actor's fall
// byte, +1304. In HEAD mode it steps `pitchDeg` instead. False when refused.
bool shootMoveForward(ShootMover& m, bool forward, bool falling, float dt, float& pitchDeg);
// `sub_47CF70` (MDDG = left, MDDD = right). False when refused.
bool shootMoveStrafe(ShootMover& m, bool right, bool falling);
// `sub_47CF50` (MDDO, down) and `sub_47CF40` (MDUP).
void shootMoveCrouch(ShootMover& m, bool down);
// `sub_47D370`'s YAW half, what MDRG / MDRD and the mouse turn by: `+420 -=
// sensitivity * 0.01 * a1`, the sensitivity being options row 23. MDRG hands
// it -50 and MDRD +50.
float shootTurnDegrees(int a1, int sensitivity);
// `sub_47D370`'s PITCH half, what the mouse's dy and MDLUP / MDLDO (-/+25)
// move: `pitch += sensitivity * 0.01 * (inverted ? dy : -dy) * delta`, the
// sensitivity options row 24 and `inverted` row 25 ("Souris inversée"),
// `delta` the frame's 30/fps (`flt_4C30D8`), then clamped to +-45 degrees
// (0x4BCB34 / 0x4BCB38). The result is what `sub_47C260` hands the camera,
// times pi/180, and it is in the ENGINE's sign - the caller owns how its own
// camera reads it. **The arm that zeroes it instead is flag 0x1000, and its
// writer is `sub_47CC70`** (found 2026-09-12, where this said "no traced
// writer"): `BYTE1 |= 0x10` is bit TWELVE, not bit four, and it goes up for
// exactly the character type that takes the mechanical sounds. So a Mecagarde
// in shoot mode cannot look up or down at all - his pitch is pinned at 0 and
// `sub_47C260` is not even called, so the aim keeps its last value - while his
// yaw, written before the test, turns as anyone's does. Kay'l is not one, so
// no route this port can reach exercises it; `shootMovePitches` is the test.
float shootPitchStep(float pitchDeg, int dy, int sensitivity, bool inverted, float delta);

// One frame of `sub_47D4D0`'s motion: the world step it hands
// `o3de_MoveNodeBy` in x and z, and the `+248` fall of `flt_6579C0`.
struct ShootMoveStep { float dx = 0.0f, dz = 0.0f, dy = 0.0f; };
// `eulerPitch` / `eulerRoll` are the actor's +416 and +424, which the SHOVE
// arm spends and then zeroes. Pass neither and the arm is skipped - which is
// what every probe that only wants the step does.
ShootMoveStep shootMoveTick(ShootMover& m, float facingDeg, float dt,
                            float* eulerPitch = nullptr, float* eulerRoll = nullptr);

}  // namespace omk

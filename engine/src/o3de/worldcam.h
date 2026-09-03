// SPDX-License-Identifier: GPL-3.0-or-later
// THE WORLD CAMERA TABLE - `Camera_FindWorld` (0x0040B220) and the record it
// hands to `Camera_Request(12, ...)`.
//
// A world script does not carry a camera; it NAMES one. `camera.set` (op 95)
// and `camera.set.wait` (op 96) take an id, `Camera_FindWorld` looks it up,
// and `Game_HandleEvent` case 8 unpacks the record it returns into the
// mode-12 request block. So the framing of every scripted shot in the game is
// this table, and a replica that draws the world has to read it before it can
// point a camera anywhere.
//
// **Where it lives, and in what ORDER.** `Camera_FindWorld` walks the two
// resident chunk slots, and inside each slot the AREA table first and the
// SCENE table second; only when no slot matched does it fall back on GLOBAL:
//
//     AREA  +64 -> the array,  +84 -> int16 count
//     SCENE +32 -> the array,  +52 -> int16 count
//     GLOBAL+20 -> the array,  +30 -> int16 count
//
// 5381 records ship, of which GLOBAL holds **four**. The order is followed
// because the code says so and NOT because anything here could tell: measured,
// 280 chunk records carry an id GLOBAL also has and **every one of them is
// byte-identical to GLOBAL's**, so the two orders resolve to the same camera
// in all 280. `tools/cutscene.py` takes GLOBAL first and `setdefault`s - the
// opposite order - and cannot be distinguished from this one by any shipped
// file. That is worth writing down rather than leaving as an implied
// difference: the order is TIER 6, read and explained, and the data cannot
// falsify it.
//
// **The record is 44 bytes**, and `Area_Load` / `Scene_Load` / `Global_Load`
// all convert it IN PLACE the moment the chunk is read, which is why the
// numbers on disk are not the numbers the camera code sees:
//
//     +0   int32[3]  eye     ) `* 100 / 256 / 2.54 - 1` - the shipped fixed
//     +12  int32[3]  at      )  point into the world's INCH unit
//     +24  int16     id       the thing `camera.set` names
//     +26  int16     mode     12 in 5342 of 5381; 20 in 38, 4 in one
//     +28  int16     roll     4096ths of a turn -> degrees
//     +30  int16     fov      4096ths of a turn -> degrees; 853 (= 75.0) in
//                             4543 of them, which is the third-person preset's
//                             own fov (docs/ASSETS 1963)
//     +32  int16     the TARGET's subject actor, -1 for an absolute point
//     +34  int16     the EYE's subject actor, -1 for an absolute point
//     +36  int16[3]  three further fields mode 12 carries, unread here
//
// **A quarter of the table is not absolute at all**, and reading it as if it
// were is what draws a black frame. `Camera_LoadParams` (0x004146C0) decides
// per POINT, not per camera:
//
//     subject == -1   the point goes to the camera's absolute slot
//     otherwise       it goes to the OFFSET slot, to be resolved against
//                     that actor by `sub_414520`
//
// 1440 of 5381 records set at least one - 959 track an actor with a fixed eye,
// 406 are wholly relative. SCENE 55's camera 0, the one the Impasse's startup
// script cuts to, has both fields 0: an eye 119 units behind actor 0 and 27
// above, looking just in front of him. That is the third-person follow, and
// the preset table's mode 0 says the same thing in metric - 3.00 m behind,
// which is 118.11 inches. Read as absolute it sits at the world origin while
// the Impasse's geometry is 7000 units away, so the frame is empty and nothing
// says why.
//
// **And the fov really is what it looks like.** `sub_4B2xxx`'s projection
// setup takes `tan(fov * 0.5 * pi/180)` off camera `+48`, which
// `Camera_LoadParams` fills from this field - so it is the FULL HORIZONTAL
// angle in degrees, and the 247 records above 105 degrees are drawn as the
// fisheye they ask for. Six of AREA 118's intro cameras are in that group at
// 171-175 degrees, and rendered they give the radial smear of a wormhole,
// which is what that sequence is.
//
// **The roll WRAPS and the wrap is invisible standing still.** `Global_Load`
// multiplies the signed field by 360/4096 without wrapping, so a small
// negative roll is stored near 4096 and reads as ~+359 degrees. That is the
// same rotation in any single frame - and travelling from it to 0 sweeps the
// long way round, which is the "camera makes several complete loops where the
// game turns a few degrees" the title sequence showed. Wrap to (-180, 180]
// first and the short arc is also the numerically short one. CLAUDE.md 1 has
// this as its worked example of an error that is invisible at rest.
//
// TIER 3, differential: every number here is read out of the shipped chunks
// and `tools/cutscene.py` reads the same records independently, so the two can
// be differenced - which is `verify.py: engine world camera`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace omk {

struct WorldCamera {
    int   id   = -1;
    int   mode = 0;
    float eye[3] = {0, 0, 0};
    float at[3]  = {0, 0, 0};
    float roll = 0.0f;      // degrees, wrapped to (-180, 180]
    float fov  = 0.0f;      // degrees, HORIZONTAL
    // Which actor each point is an OFFSET from; -1 is an absolute point.
    int   eyeSubject = -1;      // record +34
    int   atSubject  = -1;      // record +32
    bool  valid() const { return id >= 0; }
    // Whether both points are world coordinates, so the camera can be used
    // without an actor runtime to resolve it against.
    bool  absolute() const { return eyeSubject == -1 && atSubject == -1; }
};

// RESOLVING A SUBJECT-RELATIVE CAMERA.
//
// `Camera_LoadParams` decides PER POINT: `+34` guards the eye and `+32` the
// target, `-1` meaning an absolute world coordinate and anything else naming
// an actor the point is an OFFSET from. The resolve itself is `sub_415D10`
// (the target) and `sub_415E60` (the eye), and both do the same thing:
//
//     Matrix3x3_FromEulerAngles(subject euler, M);
//     Matrix3x3_RotateVector(offset, M, tmp);
//     point = subjectPos - tmp;                    <- SUBTRACT
//
// The sign is not a detail and the data corroborates it. The Impasse's
// camera 0 offsets the eye by (-1, 26, -119) from a player standing at
// ADDRESSES[654] = (6753, 397, 3021); subtracting puts the eye at y 371,
// which is above the floor (the set bottoms out at 399 and Y points DOWN),
// while adding puts it at 423 - twenty-six units underground.
//
// **What is NOT established: the rotation.** The engine rotates the offset by
// the SUBJECT's own Euler angles, which live on the live camera block at
// `+164`/`+168`/`+172` (eye) and `+112`/`+116`/`+120` (target) and are
// written by something this read did not find - no write to those offsets
// appears in the decompilation at all, which is CLAUDE.md 1's missing-`proc`
// trap. What is passed here is the subject's yaw, applied about Y in the
// game's own facing convention, and for the one case that can be checked -
// the intro's arrival - ADDRESSES[654] carries heading **0**, so the rotation
// is the identity and that case cannot discriminate the convention. Treat a
// non-zero heading as untested.
struct ResolvedCamera {
    float eye[3] = {0, 0, 0};
    float at[3]  = {0, 0, 0};
};

// `subjectPos`/`subjectYaw` describe the actor both subjects name. Only one
// actor is modelled, which is what every shipped relative camera but 78 of
// 1443 asks for; a camera naming any other subject is returned unresolved and
// the caller must not draw it.
ResolvedCamera resolveCamera(const WorldCamera& c,
                             const float subjectPos[3], float subjectYaw);

// The engine's own raw -> world conversion, as `Global_Load` applies it.
float rawToWorld(std::int32_t v);
// A 4096-per-turn angle field, in degrees on (-180, 180].
float angle4096(std::int16_t raw);

class WorldCameras {
public:
    // The three tables, each taking a whole chunk (or the GLOBAL file) and
    // finding its own array. A table that is absent or empty is not an error:
    // AREA 118 declares 27 cameras and no scene at all.
    void setArea(std::span<const std::byte> chunk);
    void setScene(std::span<const std::byte> chunk);
    void setGlobal(std::span<const std::byte> file);
    void clearChunk() { area_.clear(); scene_.clear(); }

    // `Camera_FindWorld`'s order: the chunk's own tables, then GLOBAL.
    // -> nullptr when nothing has that id, which is what the engine returns
    // and is not a decode failure - a script can name a camera belonging to an
    // area that is not resident.
    const WorldCamera* find(int id) const;

    std::size_t count() const { return area_.size() + scene_.size() + global_.size(); }
    const std::vector<WorldCamera>& area() const { return area_; }
    const std::vector<WorldCamera>& scene() const { return scene_; }
    const std::vector<WorldCamera>& global() const { return global_; }

private:
    static void read(std::span<const std::byte> b, std::size_t arrOff,
                     std::size_t cntOff, std::vector<WorldCamera>& out);
    std::vector<WorldCamera> area_, scene_, global_;
};

}  // namespace omk

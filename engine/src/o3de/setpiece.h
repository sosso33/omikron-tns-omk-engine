// SPDX-License-Identifier: GPL-3.0-or-later
// THE SET PIECES - `.SFX` section E, run the way the engine runs them.
//
// A set piece is a row of section E: an effect, a section F block of
// WAYPOINTS, a link, a delay, a loop count and a handful of flags. Five
// functions in `Runtime 2.exe` are the whole machine, and every rule below
// names the one it came from:
//
//   SetPiece_Show   0x00451340   show a row: init it, chain into the row and
//                                the records it links (type 1), reset the
//                                clock, take the direction from flag 0x4,
//                                start the delay (flag 0x20) when +56 != 0
//   sub_451220      0x00451220   init: find the block by +16, resolve the row's
//                                link (+40/+44 -> +48) and every record's
//                                (+24/+28 -> +32), point +24 at the first
//                                record (the last-but-one when reversed) and
//                                write its position into +28..+36
//   sub_451600      0x00451600   EVERY FRAME, for every shown row that is not
//                                waiting: advance, then register ONE emitter at
//                                the row's position with the row's effect -
//                                `+52` directly, or, when `+52 <= 0`, the
//                                current record's `+4` (the record before it
//                                when reversed) - then the end test
//   sub_450E50      0x00450E50   advance: walk the records by their `+20`
//                                durations to the segment the clock is in and
//                                LERP the two positions; write +24 and +28..
//   sub_450D60      0x00450D60   the end test: `clock += dt`; while waiting,
//                                stop waiting when it passes +56; otherwise
//                                when it passes the block's duration, HIDE the
//                                row if `+68 >= +64`, else flip the direction
//                                under flag 0x10, zero the clock and count
//                                the loop (unless +64 is 999)
//
// **The position is a LINK.** `sub_450330` puts a record's `+8` into the frame
// of whatever it is linked to: type 1 a section E row (`sub_450200` case 1 -
// that row's current +28..+36; `sub_450280` case 1 - a heading built from its
// direction of travel, `sub_450940`), type 2 an ACTOR found by the first three
// letters of its name (`sub_450FC0` case 2, the id packed big-endian: 'HO1' is
// 0x484F31 and names Kay'l), type 3 the PLAYER (`unk_8F5EA0`). The row's own
// link wins over the record's when both are set. The heading matrix is
// `sub_442400` -> `sub_441FF0`, applied through `Matrix3x3_RotateVectorT`.
//
// **What this settles for the intro**, and could not be settled without it:
// `ttt`, GRID's one multiply effect, is row 3, linked (type 1) to row 0 and
// carrying a 27-record block whose positions lie on a tilted circle of radius
// 39.4 units at 0.385 frames a segment - ten frames a turn, which is `ttt`'s
// lifetime. So its eleven live particles are spaced round that circle, and
// the capture's dark spiky ring is eleven small dark starbursts orbiting the
// disc's centre. Rows 0-2 travel a 4-unit line over 22 frames and loop for
// ever; row 4 (`1KaylArrives`, the arrival) is the indirect form linked to
// actor 'HO1', playing `kaylarr` then `kay arr` at his position after a
// 6-frame delay, forward then back, twice; row 5 (`3KaylLeaves`) plays the
// same block once, reversed, relative to the player, after 21 frames.
//
// **Duration** (block +12) is the sum of the first n-1 records' `+20`, or the
// only record's when n == 1; `SetPiece_Show` and the ping-pong flip both
// compute it that way.
//
// **NOT ported, and counted:** the smoothed heading under flag 0x40 (8 of 382
// shipped rows), the roll under 0x80 (4) and its interpolation under 0x100
// (4) - `sub_450940`'s second arm and `sub_450830`; a linked row's heading is
// the plain difference of its current and next waypoint. Record links of type
// 2 (1 record) and 1 (2 records) resolve through the same path as a row's and
// are exercised nowhere here. The effect's SOUND (`+4`, played through the
// row's +36 countdown) is not played. And a link the caller cannot resolve
// leaves the record ABSOLUTE - a labelled fallback, not the engine's answer.
//
// TIER: transcription of the five functions and `sub_450330`, checked by
// `tools/verify.py: engine particles` over GRID (the orbit's radius and
// period, the arrival's delay, effects and loop count), and by eye against
// `traces/frames/intro-75.png`.
#pragma once

#include "formats/sfx.h"
#include "o3de/particles.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace omk {

// What a link resolves to: where the linked thing is, and its frame.
struct PieceLink {
    float pos[3] = {0, 0, 0};
    // A type-2 link (an ACTOR) hands over its own 3x3 (`+56`, `sub_450280`
    // case 2); a type-3 one (the PLAYER) a heading and a roll in degrees,
    // which `headingMatrix` turns into the same thing (case 3).
    bool  hasMatrix = false;
    float mat[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float dir[3] = {0, 0, -1};
    float roll = 0;
};

// -> false when the caller has nothing under that type and id; the record's
// position then stands absolute, which is labelled above as a fallback.
using PieceLinkResolver =
    std::function<bool(int type, std::uint32_t id, PieceLink& out)>;

// `sub_442400` then `sub_441FF0`: the 3x3 the engine builds from a heading and
// a roll in degrees. A zero heading gives the identity here; the engine
// divides by zero.
void headingMatrix(const float dir[3], float rollDeg, float mat[9]);
// `Matrix3x3_RotateVectorT`: out[i] = sum_j mat[3i+j] * v[j].
void rotateT(const float v[3], const float mat[9], float out[3]);

class SetPieceRunner {
public:
    struct State {
        bool  shown    = false;   // +72 bit 0
        bool  waiting  = false;   // +72 bit 0x20: the +56 delay is running
        bool  reversed = false;   // +72 bit 0x8
        bool  pingpong = false;   // +72 bit 0x10
        float clock    = 0;       // +60, in frames
        int   iter     = 1;       // +68
        int   cur      = 0;       // +24, as a record index
        float pos[3]   = {0, 0, 0};   // +28..+36
        float duration = 0;       // the block's +12
        int   chain    = -1;      // +48 for a type-1 row link, as a row index
        int   effectLast = 0;     // the effect registered on the last tick
    };

    // Fresh state for every row of `sfx`, nothing shown. The file must outlive
    // the runner.
    void attach(const SfxFile* sfx);
    void setLinks(PieceLinkResolver r) { links_ = std::move(r); }

    // `sub_451470(a1, a2)`, and the `(1, -1)` walk `Sfx_BindAmbientEffects`
    // ends with: show every row keyed (a1, a2).
    void showKeyed(int a1, int a2);
    // `SetPiece_Show` on one row.
    void show(int row);
    // `sub_451600` for every shown row, spawning through `fx`, then the
    // end test. Call before `fx.tick()`, as the engine's frame does.
    void tick(float dt, ParticleField& fx);

    const std::vector<State>& states() const { return st_; }
    int shownCount() const;
    int registered() const { return registered_; }   // emitters on the last tick
    int shows() const { return shows_; }             // SetPiece_Show calls so far

private:
    int   rowById(std::int32_t id) const;
    void  init(int row, int depth);                              // sub_451220
    void  recordPos(int row, int rec, float out[3], int depth) const;   // sub_450330
    void  rowHeading(int row, float dir[3], int depth) const;    // sub_450940
    void  advance(int row);                                      // sub_450E50
    bool  endTest(int row, float dt);                            // sub_450D60
    float blockDuration(int row) const;
    int   indirectEffect(int row) const;

    const SfxFile*     sfx_ = nullptr;
    std::vector<State> st_;
    PieceLinkResolver  links_;
    int registered_ = 0, shows_ = 0;
};

}  // namespace omk

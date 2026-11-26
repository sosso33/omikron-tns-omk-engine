// SPDX-License-Identifier: GPL-3.0-or-later
// THE CITY WEATHER - `sub_4815D0` (0x004815D0) and its two helpers,
// `sub_481200` (the roof ray) and `sub_481D90` (the sprite weathers), read
// 2026-10-11 (`todo/scene-gameplay-audit.md` 8).
//
// Where it comes from: `Area_TickLoad` case 9 hands `sub_41D3F0` the AREA
// chunk's `+176`, whose low word picks a SCENE flag - 1 -> 0x10000, 2 ->
// 0x20000, 3 -> 0x40000 - and `sub_479C20` turns those into the 3D view's
// `+80` bits 2, 4 and 8 when it submits the world. The view's display-list
// draw calls `sub_4815D0` whenever `+80 & 0xE`, every drawn frame, and it
// does nothing at all unless option row 7, *Niveau de detail*, is 2
// (`cmp byte ptr [0x90E727], 2` - the settings header's +1447; the boot
// default 0x01030000 holds 1 there, so by default nobody sees weather).
//
// 12 areas ship a type (8 more set only `+176`'s byte 2, the decor slot's
// `+109`): 1 (the RAIN - grey streaks) in Anekbah, the Impasse and its
// airlock, the rooftops, Khonsu; 2 (SNOW - `Images\snow.bmp` blitted) in
// Mahaleel (145, reached in play; its ice cave's roof holds the snow off, the
// open part snows) and SASNEIGE (147: no zone and no address, unreachable);
// 3 in Jaunpur,
// Boz's interior, the docks and the two rooftop streets -
// whose cycle RUNS (messages 23 and 24 are posted) but which DRAWS NOTHING:
// the sprite arm is called on `test al, 4` alone, so its sand branch
// (`Images\sand.bmp`, size 15, speed 30) is unreachable.
//
// THE CYCLE (`dword_4CFE10`), on the WALL clock (`Sys_GetTimeMs`) - this port
// hands it the frame's milliseconds summed, so a headless run is
// deterministic; at the game's 30 fps the two agree:
//   4 (the boot value, with start and length 0): bits 2|8 - the intensity
//     falls `(1 - t/len) * 200` and at the end the cycle goes to 0 with a wait
//     of `rand() % 360000 + 480000`; bit 4 alone (snow) - straight to 2. On
//     EVERY frame of state 4 the 200 drops are scattered afresh in a box of
//     1000 (300 with bit 8, 400 for the snow);
//   0: intensity 0; at the end of the wait, MESSAGE 23 (`Game_RaiseEvent(43)`,
//     `Message_RunHandlers` on the current area) and 1 for 20000 ms;
//   1: the intensity rises `200 * t / 20000`; then 2 for `rand() % 60000 +
//     150000`;
//   2: 200; with bits 2|8, at the end MESSAGE 24 and 4 for 10000 ms.
// The ROOF RAY (`sub_481200`) runs first, every frame: a segment from the
// camera's eye 196.85 up (5 m; Y points down) through `sub_444810`. Meeting
// something sends 1 or 2 to 4 for 200 ms - and posts NO message 24, so the
// rain's track keeps playing - and in 0 restarts the wait.
// And whenever there is no player or he is in ACTOR_STATE 14 (water), the
// intensity and the state go to 0.
//
// THE DROPS are 200 points in a box centred a third of its size AHEAD of the
// eye along the view direction, moved per FRAME (no delta): the rain by
// `(0.5, 0.866, 0) * 64` - the globals at 0x4D0784 / 0x4D0788 / 0x657B3C
// times 0x4D078C - wrapping past half the box to minus half; the snow by
// (-17.32, +6, 0) (`speed * 0.866`, `speed * 0.3`, speed 20), its z only
// wrapped. The position drawn is the one BEFORE this frame's step.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace omk {

inline constexpr int kWeatherDrops = 200;

// The view's `+80` weather bits from the AREA chunk's `+176` low word.
inline unsigned weatherBitsOfAreaType(int type) {
    return type == 1 ? 2u : type == 2 ? 4u : type == 3 ? 8u : 0u;
}

// One drop as the draw needs it: the world point, and the second point the
// engine projects with it - the rain's tail (`+ wind * 12`), the snow's size
// (`+ (8, 0, 0)`).
struct WeatherDrop { float p[3], q[3]; };

// THE SAND'S GRAINS (`Images\sand.bmp` - a 128x128 FIELD of 63 grains, 3 to 5
// texels each, keyed on black): the bounding rectangle of each 8-connected
// run of `solid` texels, in reading order - what the restored sand draws one
// of per drop (OMK's design: the arm would blit the field's top-left 64x64)
struct GrainRect { int x, y, w, h; };
std::vector<GrainRect> grainsOf(int w, int h, const std::function<bool(int, int)>& solid);
inline constexpr float kSandGrainWorld = 4.0f;   // a 4-texel grain drawn this many units across (OMK's)

class Weather {
public:
    // `sub_4815D0`'s state, its per-frame half. `bits` the view's +80 & 0xE;
    // `covered` the roof ray's answer; `noPlayer` no player record or
    // ACTOR_STATE 14; `rnd` the CRT `rand()`; `post(23|24)` raises the
    // message. Call only when the detail gate passes.
    void tick(unsigned bits, long nowMs, bool covered, bool noPlayer,
              const std::function<int()>& rnd, const std::function<void(int)>& post);
    // The drops to draw this frame (empty when nothing draws), stepping them:
    // the rain's first `count()` (bit 2), or all 200 snowflakes (bit 4).
    // `eye` and `at` are the scene camera's two points.
    // ...and THE SAND (bit 8 - `[Restored] weather` only: the caller masks the
    // bit otherwise, as the original's gate leaves it out): `sub_481D90`'s else
    // branch - box 300, speed 30, the snow's direction (x back 0.866, y down
    // 0.3, z only wrapped), the second point 15 along x. The arm itself walks
    // all 200 for the whole shower; the RESTORED draw takes the first
    // `count()` as the rain does, so it ramps in and fades (OMK's design, the
    // reader's choice "as the asset suggests", todo/restore-weather.md)
    std::vector<WeatherDrop> step(unsigned bits, const float eye[3], const float at[3]);

    // AN INSTRUMENT, not the original's: the performance sweep (`--bench`)
    // holds the shower at its full 200 (state 2) or away (state 0), each for
    // as long as a step lasts - the cycle's own first rain is 8-14 minutes
    // off. The roof ray still ends a held shower over a covered player.
    void holdForBench(bool rain, long nowMs) {
        state_ = rain ? 2 : 0;
        start_ = nowMs;
        length_ = 1L << 30;
        count_ = rain ? 200 : 0;
    }

    int  state() const { return state_; }
    int  count() const { return count_; }
    long waitEndsAt() const { return start_ + length_; }

private:
    int  state_ = 4;            // dword_4CFE10
    long start_ = 0;            // dword_4CFE14
    long length_ = 0;           // dword_4CFE18
    int  count_ = 0;            // dword_4CFE1C
    float x_[kWeatherDrops]{}, y_[kWeatherDrops]{}, z_[kWeatherDrops]{};   // unk_4D0140 -/+ 800
};

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
// `Script_PlayScript` (0x0044C860) - the SCENE OBJECT interpreter.
//
// The other half of the game's logic.  The world scripts (`script/interp.h`)
// decide *what* happens; these decide *how it looks while it happens* - an
// object IS a program, and `Script_PlayAllScripts` runs one frame of every
// object of both resident scenes.
//
// The structure:
//
// * the function at the PROGRAM COUNTER runs together with everything reached
//   through its `sync` link.  Chained functions execute the same tick, which
//   is how an animation carries its sounds;
// * each handler returns a BUSY bit.  While anything in the chain is busy the
//   pc holds; when all are done it advances.  A function that has already run
//   its `+16` repeat limit reports done immediately (-1 = for ever);
// * past the last function, `loopsDone` is compared with the object's `+52`
//   loop count: 1 ends the program, -1 rewinds.
// * the clock advances by the frame dt - and it is the SAME clock the camera
//   editing is sampled on, which is why a cutscene's camera cannot drift from
//   its animation.
//
// **The rewind zeroes the counter it just tested.** It goes through
// `Script_StartScript`, which clears `loopsDone` along with the pc, the clock
// and every run counter, so a finite loop count above 1 would in fact loop for
// ever.  That never bites, because the shipped data uses exactly two values:
// 1 (3551 objects) and -1 (960).  Modelled the same way here rather than
// "fixed", because the engine's behaviour is the specification.
//
// **What `busy` MEANS per function is not in `Script_PlayScript`** - it is in
// each handler - so it is a model, stated here rather than buried:
//
//     SelectBodyAnimation / SelectRelative   the clip's frame count
//     Wait                                   its float parameter
//     PlaySyncSound                          until param 1 <= the clock
//                                            (`if (param1 > obj+88) return 1`)
//     MoveObjectOnPath                       the .3DP path's duration
//     everything else (sprites, sounds)      never busy
#pragma once

#include "formats/scx.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace omk {

// The function ids whose busy window is modelled above.  They are the object
// script ids, not VM opcodes - a different numbering entirely.
constexpr std::uint32_t kFnSelectBodyAnimation = 0x02000004;
constexpr std::uint32_t kFnSelectRelativeAnim  = 0x0200002A;
constexpr std::uint32_t kFnWait                = 0x06000017;
constexpr std::uint32_t kFnPlaySyncSound       = 0x05000015;
constexpr std::uint32_t kFnMoveObjectOnPath    = 0x03000008;

// One .SCX's objects plus the clip lengths their programs need.  The frame
// counts come from the STREAMED animations, so a scene must carry its stream.
class ScxRuntime {
public:
    ScxRuntime(std::span<const std::byte> file);

    bool valid() const { return scene_.valid; }
    const ScxScene& scene() const { return scene_; }
    const ScxObject* byName(const std::string& n) const;

    // How long animation `i` runs, in frames - the busy window of a body
    // animation.  0 when the index names nothing, which is not an error: a
    // program can reference a clip the scene does not stream.
    int clipFrames(int i) const;
    std::string clipName(int i) const;

    // The clip's own bytes, for a caller that wants to POSE with it rather
    // than only time it - `actor/pose.h`'s `clipTracks` takes exactly this.
    std::span<const std::byte> clipData(int i) const;
    // The scene's authored PATHS. `Script_SelectRelativeBodyAnimation` places
    // its character at `Path_Sample` of the one its param 8 names, which is
    // where a scene object's character actually stands - `GRID`'s two are
    // `UBas.p1` and `UBas.p2-3`, Kay'l's pelvis before and after his arrival.
    const std::vector<ScxPath>& paths() const { return stream_.paths; }

private:
    std::vector<std::byte> data_;
    ScxScene  scene_;
    ScxStream stream_;
    mutable std::map<int, int> frames_;
};

// One running object.  Ticking it is `Script_PlayScript` for one frame.
class Program {
public:
    Program(const ScxRuntime& rt, const ScxObject& obj);

    // `Script_StartScript`: running, pc, loops, clock and run counters cleared.
    void start();

    // One frame.  -> true while the program is still running.
    bool tick(float dt = 1.0f);

    bool  running() const { return running_; }
    int   pc()      const { return pc_; }
    int   loops()   const { return loops_; }
    float clock()   const { return clock_; }
    int   restarts() const { return restarts_; }

    // Every body animation the program has begun, in order - instrumentation,
    // not engine state, and what the alternation test reads.
    const std::vector<std::string>& animTrace() const { return trace_; }

private:
    std::vector<int> chain(int i) const;
    float busySpan(int k) const;

    const ScxRuntime* rt_;
    const ScxObject*  obj_;
    bool  running_ = false;
    int   pc_ = 0, loops_ = 0, restarts_ = -1;
    float clock_ = 0.0f;
    std::vector<int>   runs_;
    std::map<int, float> busyUntil_;
    std::vector<std::string> trace_;
};

}  // namespace omk

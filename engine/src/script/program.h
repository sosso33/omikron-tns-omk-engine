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
#include <set>
#include <span>
#include <string>
#include <vector>

namespace omk {

// The function ids whose busy window is modelled above.  They are the object
// script ids, not VM opcodes - a different numbering entirely.
constexpr std::uint32_t kFnSelectBodyAnimation = 0x02000004;
constexpr std::uint32_t kFnSelectRelativeAnim  = 0x0200002A;
constexpr std::uint32_t kFnWait                = 0x06000017;
constexpr std::uint32_t kFnPlaySound           = 0x05000014;
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

    // A chunk-3 SOUND, which the object programs name by index. The bytes are
    // a whole RIFF file - `audio::loadWav` reads them as they stand.
    int wavCount() const { return static_cast<int>(stream_.wavs.size()); }
    std::string wavName(int i) const;
    // The record's `+24` ID, which is what a `.CTL` effect matches on
    // (`Scene_FindSoundIndex`) - a different question from the INDEX the
    // scene programs use. -1 when `i` names nothing.
    int wavId(int i) const;
    // `Scene_FindSoundIndex` (0x0048CC80): the index of the record whose id is
    // `id`, or -1. A linear search, which is what the engine does.
    int wavBydId(int id) const;
    std::span<const std::byte> wavData(int i) const;
    // The scene's authored PATHS. `Script_SelectRelativeBodyAnimation` places
    // its character at `Path_Sample` of the one its param 8 names, which is
    // where a scene object's character actually stands - `GRID`'s two are
    // `UBas.p1` and `UBas.p2-3`, Kay'l's pelvis before and after his arrival.
    const std::vector<ScxPath>& paths() const { return stream_.paths; }
    // `Script_MoveObjectOnPath` addresses a path in two parts: param 1 the
    // `.3dp` FILE (a chunk-0 record) and param 2 the path inside it. -> null
    // when either names nothing, which the engine logs and refuses.
    const ScxPath* pathIn(int file, int index) const;
    // An object's own first string table, which is how a scene function names
    // a node: `Script_MoveObjectOnPath` param 0 and
    // `Script_SelectBodyAnimation` param 0 are both indices into it, resolved
    // by `o3de_FindNodeByName` and cached in the table's pointer slot.
    static std::string objectName(const ScxObject& o, int index);

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

    // THE SOUNDS THIS TICK STARTED, in the order the chain ran them. Cleared
    // and refilled by every `tick`, so a caller reads it after ticking.
    //
    // The two play functions do NOT share a parameter layout, and reading one
    // as the other invents a cue time for every call of the second - from
    // their handlers:
    //
    //   `Script_PlaySyncSound` (0x004A14D0)  0 sound  1 the FRAME on the
    //     object's program clock to fire at (`if (param1 > obj+88) return 1`,
    //     so a pending cue also HOLDS the chain)  2 &1 loop  3 a runtime
    //     latch, 0 on disk  4 the node to position it at, -1 = non-positional
    //   `Script_PlaySound`     (0x004A12D0)  0 sound  1 &1 loop  2 the latch
    //     3 the node
    //
    // The latch is why each fires once per run of its function rather than
    // once a frame; a rewind clears it with everything else.
    struct SoundCue {
        int  wav = -1;       // index into the scene's chunk-3 array
        bool loop = false;
        int  node = -1;      // -1 = not positioned at a node
        bool sync = false;   // it came from PlaySyncSound
        float at = 0.0f;     // its cue frame, for a caller that logs
    };
    const std::vector<SoundCue>& sounds() const { return sounds_; }

    // THE OBJECT MOTIONS running this tick - `Script_MoveObjectOnPath`, which
    // is the most-used script function in the game (4841 sites) and moved
    // nothing here until 2026-09-03. The handler ends in
    // `o3de_SetNodePos(node, x, y, z)` with the path sample OUTRIGHT, so the
    // position is absolute world and not a delta; `name` is param 0 resolved
    // through the object's own first string table, which is how a scene names
    // a mesh of the resident set (`Caisse01`, `Caisse1`, `Caisse 13`,
    // `Caisse 14` for the Impasse's four crates).
    struct NodeMotion {
        std::string name;        // the set mesh to place
        float pos[3] = {0, 0, 0};
        float t = 0.0f;          // where along the path, in the path's frames
        bool  placed = false;    // false when the sample fell outside every span
    };
    const std::vector<NodeMotion>& motions() const { return motions_; }

    // THE FUNCTION DRIVING THE POSE THIS TICK - the first
    // `SelectBodyAnimation` / `...Relative` in the chain at the program
    // counter, or -1 when the step at the pc plays none.
    //
    // A program is a SEQUENCE of steps and the pc walks it, so "the object's
    // clip" is not a property of the object: `Impasse.SCX`'s `A_2_DemonLook`
    // is clip 15 (the demon perched, 91 frames) and THEN clip 17 (his jump
    // off the wall, 41). Reading only the first - which `SceneRunner` did
    // until 2026-09-03 - poses the whole 132-frame shot with the perch clip,
    // which clamps at its last frame: the demon hangs 267 units up the wall
    // for the whole shot and his descent never happens.
    int animFn() const;
    // That function itself, or nullptr - what a caller needs to read its clip,
    // path and placement parameters.
    const ScxFunction* animFunction() const;
    // Its OWN frame. `Script_SelectBodyAnimation` (0x004A35D0) keeps the
    // frame in params 2/3, starts it at 1.0 on the tick where param 2 is
    // still 0, and adds `Script_GetFrameTime()` each tick - so it counts from
    // when the FUNCTION was entered, not from when the program started. The
    // program clock is the wrong number to sample a second step's clip with.
    float animClock() const;

private:
    std::vector<int> chain(int i) const;
    float busySpan(int k) const;
    bool  isAnim(int k) const;

    const ScxRuntime* rt_;
    const ScxObject*  obj_;
    bool  running_ = false;
    int   pc_ = 0, loops_ = 0, restarts_ = -1;
    float clock_ = 0.0f;
    std::vector<int>   runs_;
    std::map<int, float> busyUntil_;
    std::map<int, float> entryAt_;   // when the current run of k began
    std::set<int>        fired_;     // the sound functions' latch (+8 / +12)
    std::vector<NodeMotion> motions_; // what moved this tick
    std::vector<SoundCue> sounds_;   // what this tick started
    std::vector<std::string> trace_;
};

}  // namespace omk

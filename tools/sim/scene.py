#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""The SCX object interpreter, ticked - phase 6 stage 4.

`Script_PlayScript` (0x0044C860, CLEAN) runs one scene object's program for one
frame, and `Script_PlayAllScripts` calls it for every object of both resident
scenes. This is that loop: an object IS a program, and the interpreter is the
other half of the game's logic - the world scripts decide *what* happens, these
decide *how it looks while it happens*.

    python3 tools/sim/scene.py                     # the stage-4 test
    python3 tools/sim/scene.py Re14.SCX Telis_eat  # tick one object
    python3 tools/sim/scene.py Re14.SCX            # every object's program

The structure, transcribed:

* the function at the **program counter** runs together with everything reached
  through its `sync` link - chained functions execute the same tick, which is
  how an animation carries its sounds;
* each handler returns a **busy** bit. While any function in the chain is busy
  the pc holds; when all are done it advances. A function that has already run
  its `+16` repeat limit reports done immediately (-1 = forever);
* past the last function, `loopsDone` is compared with the object's `+52` loop
  count: 1 ends the program, -1 rewinds and goes again. Note the rewind goes
  through `Script_StartScript`, which **zeroes `loopsDone`** (`u32(a1,56)=0`)
  along with the pc, the clock and every run counter - so a finite loop count
  above 1 would in fact loop for ever. It never bites, because the shipped data
  uses exactly two values: 1 (3551 objects) and -1 (960). The counter here is
  modelled the same way, and `restarts` below is separate instrumentation
  rather than engine state;
* the clock advances by the frame dt every tick - and it is the SAME clock the
  camera editing is sampled on, which is why a cutscene's camera cannot drift
  from its animation;
* a **linked pair** alternates: a finished program clears its own turn byte and
  sets its partner's.

What `busy` means per function is the one thing not in `Script_PlayScript` - it
is in each handler - so it is modelled here from the data each one reads, and
the model is stated rather than buried:

    SelectBodyAnimation / SelectRelative  busy for the clip's frame count
    Wait                                  busy for its float parameter
    PlaySyncSound                         busy until param 1 <= the clock
                                          (its handler: `if param1 > obj+88
                                          return 1`)
    MoveObjectOnPath                      busy for the .3DP path's duration
    everything else (sprites, sounds)     never busy

The stage-4 test is the plan's own: `Telis_eat` must alternate its two clips
forever, as the authored program says.
"""
import omkpaths
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))

import scene_scx as S
import anim_3da

SCPTDATA = omkpaths.data("SCPTDATA")
FPS = 30.0

ANIM_FNS  = (0x02000004, 0x0200002A)
WAIT_FN   = 0x06000017
SYNC_SND  = 0x05000015
PATH_FN   = 0x03000008


def _f32(bits):
    return struct.unpack("<f", struct.pack("<i", bits))[0]


class Scene:
    """One .SCX, its objects and the clip lengths their programs need."""

    def __init__(self, fn):
        self.file = fn
        self.raw = S.scene(os.path.join(SCPTDATA, fn))
        self.objects = self.raw["objects"]
        self.byname = {o["name"]: o for o in self.objects}
        self.byhandle = {o["handle"] >> 16: o for o in self.objects}
        self.stream = anim_3da.scx_stream(os.path.join(SCPTDATA, fn))
        self._frames = {}

    def clip_frames(self, i):
        """How long anim `i` runs - the busy window of a body animation."""
        if i in self._frames: return self._frames[i]
        n = 0
        if 0 <= i < len(self.stream["anims"]):
            a = self.stream["anims"][i]
            try:
                r = anim_3da.descriptor(self.stream["data"], a["offset"],
                                        a["declared"])
                n = max(1, r["frames"]) if r else 0
            except Exception:
                n = 0
        self._frames[i] = n
        return n

    def clip_name(self, i):
        a = self.stream["anims"]
        return a[i]["name"] if 0 <= i < len(a) else "?"


class Program:
    """One running object: Script_StartScript's state, ticked."""

    def __init__(self, scene, obj, trace=None):
        self.scene, self.obj = scene, obj
        self.fns = obj["functions"]
        self.nfn = obj["nfn"]
        self.nsync = obj["nsync"]
        self.loop = obj["loop"]
        self.trace = trace if trace is not None else []
        self.start()

    def start(self):
        """Script_StartScript: running, pc, loops and clock cleared, and every
        function's run counter reinitialised."""
        self.running = True
        self.pc = 0
        self.loops = 0
        self.clock = 0.0
        self.runs = [0] * len(self.fns)
        self.busy_until = {}          # function index -> clock it frees at
        self.myTurn = True
        if not hasattr(self, "restarts"): self.restarts = -1
        self.restarts += 1            # instrumentation; not engine state

    # ------------------------------------------------------------ the chain
    def chain(self, i):
        """The function at `i` and everything its sync link reaches.

        The `+12` sync field indexes the object's SYNC array, not the flattened
        function list: `scene_read_objects` (0x00449750) resolves it as
        `obj->syncFunctions + fn->sync` and refuses the file past
        `syncFunctions + syncCount` ("Address of SyncFunction isn't valid."),
        and `Script_FunctionsIndexesToAdresses` does the same for the sync
        records' own links. `self.fns` holds both arrays end to end, main
        first, so the sync array starts at `nfn`.

        Read flat, all 6308 shipped links land one array too early. Usually
        that only turns a leading `sync = 0` into a self-loop and drops whatever
        hung off it, but at 20 sites it lands on a different MAIN step and merges two
        program steps into one, ending the object at the longer of the two
        instead of their sum. `Impasse.SCX`'s `A_2_DemonLook` - the demon's
        jump off the wall - ran 92 frames instead of 91 + 41 = 132, which is
        exactly the duration of `sautdemon`, the editing linked to it, so the
        shot was cut short and the demon's line came in early. See
        engine/src/script/program.cpp for the full note, including why the
        corpus margin over the 95 linked editings is only 65 -> 66.
        """
        out, seen = [], set()
        while 0 <= i < len(self.fns) and i not in seen:
            seen.add(i); out.append(i)
            s = self.fns[i].get("sync", -1)
            i = -1 if not (0 <= s < self.nsync) else self.nfn + s
        return out

    def _busy_span(self, k):
        """How long function `k` stays busy, in frames. See the header."""
        f = self.fns[k]
        fid, p = f["id"], f["params"]
        if fid in ANIM_FNS and len(p) > 1:
            return self.scene.clip_frames(p[1])
        if fid == WAIT_FN and p:
            return max(0.0, _f32(p[0]))
        if fid == SYNC_SND and len(p) > 1:
            return 0.0                       # handled by its own cue time
        if fid == PATH_FN:
            return 0.0                       # a path's own duration; stage 5
        return 0.0

    def tick(self, dt=1.0):
        """One frame. -> True while the program is still running."""
        if not self.running or not self.myTurn: return False
        busy = False
        if self.nfn:
            for k in self.chain(self.pc):
                f = self.fns[k]
                if f["repeat"] != -1 and self.runs[k] >= f["repeat"]:
                    continue                    # this one has run its count out
                if f["id"] == SYNC_SND:
                    # its handler holds the chain while param1 > the clock
                    at = _f32(f["params"][1]) if len(f["params"]) > 1 else 0.0
                    if self.clock < at: busy = True
                    continue
                end = self.busy_until.get(k)
                if end is None:
                    # The last frame is drawn on the tick that reports DONE, so
                    # a function of `span` frames occupies exactly `span` ticks
                    # and the pc advance costs none - `Script_SelectBodyAnimation`
                    # returns 0 on the tick it clamp-draws the last frame, and
                    # `Script_PlayScript` advances the pc inside that same tick.
                    # See engine/src/script/program.cpp for the listing.
                    end = self.clock + max(0.0, self._busy_span(k) - 1.0)
                    self.busy_until[k] = end
                    if f["id"] in ANIM_FNS:
                        self.trace.append(("anim", self.pc,
                                           self.scene.clip_name(f["params"][1]),
                                           round(self.clock, 1)))
                if self.clock < end: busy = True
                else:
                    # `Script_SelectBodyAnimation`'s tail: the run counter goes
                    # up and the function is done only when its `+16` count is
                    # spent (-1 = for ever, ended by the object's loop);
                    # otherwise it WRAPS the frame and returns busy, so the
                    # clip plays again from the leftover. Ending after one run
                    # regardless - which this did until 2026-09-03 - releases
                    # the Impasse's `C_2_MecaSpeaks` at frame 31 instead of
                    # 18 x 31 = 558, the duration of `mecaspeak`, the editing
                    # linked to it. `engine/src/script/program.cpp` has had
                    # this since 2026-09-02 and nothing compared the two on a
                    # repeat above 1: `Telis_eat`, the only object either side
                    # runs, is authored 1.
                    self.runs[k] += 1
                    if f["repeat"] != -1 and self.runs[k] >= f["repeat"]:
                        self.busy_until.pop(k, None)
                    else:
                        self.busy_until[k] = end + self._busy_span(k)
                        busy = True                  # the wrap tick is this run's last

            if not busy:
                if self.pc + 1 < self.nfn:
                    self.pc += 1
                    busy = True
                else:
                    self.loops += 1
                    if self.loop == -1 or self.loops < self.loop:
                        self.start()            # rewind and go again
                        busy = True
        self.clock += dt
        if not busy:
            self.running = False
        return self.running


# ------------------------------------------------------------------- the test
def stage4(fn="Re14.SCX", name="Telis_eat", frames=400):
    r"""The plan's own test: `Telis_eat` alternates its two clips forever.

    Loop count -1 with two `SelectBodyAnimation` functions, each once - so the
    interpreter must cycle pc 0, 1, 0, 1 ... and never stop. Anything that
    mishandles the repeat limit, the loop count or the busy window collapses
    this to one clip or to a program that ends.
    """
    sc = Scene(fn)
    obj = sc.byname.get(name)
    if not obj: return None
    p = Program(sc, obj)
    alive = True
    for _ in range(frames):
        alive = p.tick() or alive
        if not p.running: alive = False; break
    seq = [t[2] for t in p.trace]
    return {"file": fn, "object": name, "loop": obj["loop"],
            "functions": obj["nfn"], "frames": frames, "running": p.running,
            "clips": seq, "distinct": sorted(set(seq)), "loops": p.loops,
            "restarts": p.restarts, "trace": p.trace}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) == 1:
        sc = Scene(args[0])
        print("%s: %d objects" % (args[0], len(sc.objects)))
        for o in sc.objects:
            print("  [%2d] %-24s loop %-3d %d fn + %d sync"
                  % (o["index"], o["name"], o["loop"], o["nfn"], o["nsync"]))
        return 0
    if len(args) >= 2:
        r = stage4(args[0], args[1])
        if not r: print("no such object"); return 1
        for k, pc, clip, at in r["trace"]:
            print("  frame %6.0f  pc %d  %s" % (at, pc, clip))
        return 0

    r = stage4()
    print("stage 4 - the SCX object interpreter")
    print("  %s / %s: loop %d, %d main functions"
          % (r["file"], r["object"], r["loop"], r["functions"]))
    print("  over %d frames it played %d clips: %s"
          % (r["frames"], len(r["clips"]), " ".join(r["clips"][:8]) +
             (" ..." if len(r["clips"]) > 8 else "")))
    print("  distinct clips: %s" % r["distinct"])
    print("  still running: %s, program restarts: %d"
          % (r["running"], r["restarts"]))
    alt = all(r["clips"][i] != r["clips"][i + 1] for i in range(len(r["clips"]) - 1))
    ok = (r["running"] and len(r["distinct"]) == 2 and alt and r["restarts"] >= 4)
    print("stage 4: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

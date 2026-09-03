#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""The pieces wired together - phase 6 stage 4, second half.

Stages 1 and 3 execute scripts and run the trigger lifecycle, but `scx.play*`
and `dialog.start` are still stubs there: the trace says a scene object was
asked to run and a conversation was asked to load, without either happening.
This closes that, so a launch runs *end to end*:

    zone 3732 armed -> its activate script -> scx.play.player 22 / 32 starts
    those objects on the scene -> dialog.start 387 loads the conversation

    python3 tools/sim/run.py          # the stage-4 test
    python3 tools/sim/run.py --frames 200

Which `.SCX` a scene uses is not stored in the SCENE chunk; it comes from the
AREA the scene is loaded over, whose `+97` names it (CUTSCENES 1). SCENE 53 is
played over AREA 217 `Anekbah Restaurant 2`, whose stem is `RE14` - the file
`Telis_eat` lives in, which is the other half of the same test.
"""
import os, sys, struct

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))

import dialog_triggers as T
import omkdata
import gamestate
import omkpaths
from vm import VM, Trace, Halt
import world as W
import scene as SC
import actor as AC
import dialogue as DL

SCX_PLAY = {57: "scene", 58: "scene", 59: "actor", 60: "actor",
            46: "player", 90: "player"}
# the waiting variants: 58 scx.play.wait, 60 scx.play.actor.wait,
# 46 scx.play.player.wait - each parks its caller until the object is done
SCX_WAIT = {46, 58, 60}
DIALOG_START = 61
AREA_GOTO = 47
SCENE_LOAD = 71
UI_OPEN = 70


def ui_answer(screen, fallback=None, presses=None, name="Kay'l"):
    """Walk the screen's widget tree and return the answer it produces.

    The presses are the player's half and are the only thing supplied; the
    navigation between them is the engine's own (`Ui_MoveSelection`,
    `Ui_ConfirmSelection`, and the panel hooks). For screen 29 the default
    path is confirm on "Nouvelle partie", DOWN onto the button list - the
    confirm dialog's own panel hook moves lists with up/down, not left/right -
    then confirm on "Confirmer", whose callback writes 1, which is what the
    shipped save records for the intro. `name` is typed into the field after
    the first press, because that callback REFUSES an empty one - pass
    `name=""` to walk the refusal.

    Returns `fallback` when the path ends somewhere the simulator does not
    model - a native list hook, an item callback whose effect has not been
    read, or a panel hook that was stepped over (`Ui.approx`) - so an
    unmodelled screen degrades to the old supplied number instead of
    inventing one.
    """
    from sim.ui import Ui, CONFIRM, DOWN
    try:
        u = Ui()
        u.open(screen)
    except (ValueError, KeyError):
        return fallback
    seq = presses or (CONFIRM, DOWN, CONFIRM)
    for i, bits in enumerate(seq):
        u.press(bits)
        # Typing the name is the player's half too, and it is REQUIRED: the
        # `Confirmer` callback opens by testing the name field's cursor and
        # returns without writing anything when it is empty (`sim.ui`
        # ANSWER_NEEDS_NAME). The first press is what enters the panel whose
        # builder clears the buffer, so the name is typed straight after it.
        if i == 0 and name:
            u.type_name(name)
    if u.answer is None or u.approx:
        return fallback
    return u.answer


def scx_for(arch, chunk):
    r"""The .SCX a chunk plays its objects from, via `AREA +97`."""
    host = chunk if arch == "AREA" else omkdata._scene_area().get(chunk)
    if host is None: return None
    b = T.archive(omkpaths.data("IAM/AREA")).get(host)
    if b is None or len(b) < 106: return None
    stem = b[97:106].split(b"\0")[0].decode("cp1252", "replace")
    if not stem: return None
    for f in os.listdir(omkpaths.data("SCPTDATA")):
        if f.upper() == (stem + ".SCX").upper(): return f
    return None


class Session:
    """One resident chunk: its zones, its scene objects, and the dialogue."""

    def __init__(self, arch, chunk, state=None):
        self.state = state or gamestate.load()
        self.trace = Trace(keep=True)
        self.arch, self.chunk = arch, chunk
        self.world = W.World(arch, chunk, self.state, self.trace)
        fn = scx_for(arch, chunk)
        self.scene = SC.Scene(fn) if fn else None
        self.scx = fn
        self.programs = []          # what scx.play* started
        self.dialog = None
        self.events = []
        self.scene_chunk = None     # the SCENE loaded over this AREA, if any
        self.startup = []           # what load_area() ran
        # the player as an ACTOR rather than a teleport: while this is set, the
        # frame feeds his real position to `Script_Pump`'s zone scan, so a
        # trigger fires because he walked into it
        self.player = None
        self.facing = 0
        self.action = False

    # ------------------------------------------------------- the opcode hooks
    def start_object(self, oid, how):
        """`scx.play*`: Script_StartScript on the scene object with that id."""
        if not self.scene: return None
        obj = self.scene.byhandle.get(oid)
        if not obj:
            self.events.append(("scx.miss", oid, how)); return None
        p = SC.Program(self.scene, obj)
        self.programs.append(p)
        self.events.append(("scx.start", oid, obj["name"], how))
        return p

    def start_dialog(self, did):
        """`dialog.start`: Dialog_Load, which also writes `g_DialogState = 3`.

        That is what blocks the calling script: the handler leaves the context
        running with its pc past the operands, `Script_Execute` returns for the
        frame, and the whole pump then refuses to run until the conversation
        closes (`World.pump`). So a launch script does not "suspend" - the
        world stops around it.
        """
        c = omkdata.conversation(did)
        self.dialog = c
        self.world.dialog_state = 3
        self.events.append(("dialog", did, c["name"] if c else None,
                            len(c["nodes"]) if c else 0))
        return c

    def run_dialog(self, did=None, choose=None):
        """Actually walk the conversation, then close it.

        `end_dialog` on its own says a conversation happened; this makes it one
        - the branch conditions evaluate and the chosen branch's actions run
        against the same `GameState` everything else reads. How long it takes
        and which replies a person picks are still not modelled.
        """
        did = self.dialog["id"] if (did is None and self.dialog) else did
        if did is None: return None
        b = T.archive(omkpaths.data("IAM/DIALOG")).get(did)
        if b is None: return None
        conv = DL.Conversation(did, b)
        r = DL.run(conv, self.state, choose=choose, trace=self.trace)
        self.events.append(("dialog.run", did, len(r["path"]), r["end"]))
        self.end_dialog()
        return r

    def end_dialog(self):
        """`Game_HandleEvent` case 63 - the conversation is over.

            Dialog_Unload(); Dialog_ClearSubjectActor(); g_DialogState = 1;

        The pump starts again on the next frame and the launch script resumes
        at the instruction after its `dialog.start`. How LONG a conversation
        lasts is not modelled here - there is no dialogue UI - so this is the
        one thing a caller has to say; everything either side of it is the
        engine's own control flow.
        """
        self.dialog = None
        self.world.dialog_state = 1
        self.events.append(("dialog.end",))
        return True

    def make_vm(self, ctx=None):
        sess = self
        owner = ctx
        class Wired(VM):
            def step(self, op, raw, code, start, pc):
                if op in SCX_PLAY and len(raw) >= 2:
                    f = struct.unpack_from("<%dh" % (len(raw) // 2), raw, 0)
                    # 59/60 name the actor first, the object second
                    oid = f[1] if op in (59, 60) else f[0]
                    prog = sess.start_object(oid, SCX_PLAY[op])
                    if op in SCX_WAIT and prog is not None:
                        # ScriptObject_Start gets the context's slot instead of
                        # -1, and the handler ends `mov [esi+16h], 4`.
                        #
                        # Only when the object actually resolved. In the engine
                        # it always does, and a slot holding 0 would never be
                        # released by `Game_Tick`'s scan - so parking on a
                        # missed object would deadlock the run and hide the
                        # real fault, which is that the scene is wrong.
                        # `start_object` records the miss instead.
                        sess.world.wait_on(owner, prog)
                        VM.step(self, op, raw, code, start, pc)
                        raise Halt(4)
                elif op == DIALOG_START and len(raw) >= 2:
                    sess.start_dialog(struct.unpack_from("<h", raw, 0)[0])
                elif op == AREA_GOTO and len(raw) >= 2:
                    # The handler announces and starts the staged load, and
                    # then `Script_Execute`'s per-instruction status test stops
                    # the script - `if (u16(a1,22) != 1) goto LABEL_8;`. So the
                    # announce happens first, and the halt carries status 10,
                    # which is what `Area_Transition` case 0 writes.
                    VM.step(self, op, raw, code, start, pc)
                    sess.world.begin_area(struct.unpack_from("<h", raw, 0)[0],
                                          owner)
                    raise Halt(10)
                elif op == UI_OPEN and len(raw) >= 6:
                    # field 0 the screen, field 2 the result variable
                    f = struct.unpack_from("<3h", raw, 0)
                    VM.step(self, op, raw, code, start, pc)
                    sess.world.ui_open(owner, f[0], f[2])
                    raise Halt(6)
                elif op == SCENE_LOAD and len(raw) >= 4:
                    a, sc = struct.unpack_from("<2h", raw, 0)
                    sess.scene_loaded(a, sc)
                return VM.step(self, op, raw, code, start, pc)
        return Wired(self.state, self.trace)

    def scene_loaded(self, area, scene):
        """`scene.load`: the scene becomes resident and its `+4` script runs.

        The chunk also changes which `.SCX` the scene's objects come from -
        a SCENE has no stem of its own, it plays over the area's (CUTSCENES 1).
        """
        self.world.load_scene(area, scene)
        fn = scx_for("SCENE", scene) or scx_for("AREA", area)
        if fn and fn != self.scx:
            self.scx, self.scene = fn, SC.Scene(fn)
        self.events.append(("scene.load", area, scene, self.scx))

    # -------------------------------------------------------- the area load
    def load_area(self):
        r"""`Area_TickLoad` case 9 - what the engine does when an area arrives.

        The startup scripts at `AREA +4` / `SCENE +4` are in no record table,
        so nothing in the zone pump ever reaches them; this is the path that
        does. Transcribed from the tail of the state machine (0x0040C7E0):

            dword_69BC60 = slot; dword_69BC64 = areaId
            v18 = the AREA block;  Script_NewContext(slot, v18[1], 0, 0)
            *v18 = ctx;            Script_QueueAction(ctx, 1)
            v20 = i16(u32(g_GameDB, 12), 2 * areaId)      ; the scene over it
            if (v20 != -1):
                v21 = the SCENE block; Script_NewContext(slot, v21[1], 0, 0)
                *v21 = ctx;            Script_QueueAction(ctx, 1)
            Zones_RegisterAll()

        Two things worth naming because they are easy to get wrong:

        * **which scene** is not a parameter - it is read from the game DB's
          per-area table at `+12` (`scene_of_area`, GAME_STATE 1), indexed by
          area id, `-1` meaning none. So what a load does depends on the save.
        * the AREA script runs **before** the SCENE script, and both are queued
          rather than called, so they interleave with the pump the same way a
          zone's do.

        -> the list of (arch, chunk, offset) actually run, in order.
        """
        ran = []
        order = [("AREA", self.chunk if self.arch == "AREA" else None)]
        if self.arch == "AREA":
            scene = self.state.scene_of(self.chunk)
            if scene is not None and scene != -1:
                self.scene_chunk = scene
                self.world.add_chunk("SCENE", scene)
                order.append(("SCENE", scene))
        else:
            order = [(self.arch, self.chunk)]

        self.world.new_vm = self.make_vm            # the wired VM
        for arch, chunk in order:
            if chunk is None: continue
            at, _b = W.startup_script(arch, chunk)
            if at is None: continue
            site = W.StartupSite(arch, chunk, at)
            ctx = W.Context(site)
            ctx.queue_action(W.ENTER)
            self.world.contexts.append(ctx)         # the context table
            ran.append((arch, chunk, at))
        self.world.pump(1)                          # ProcessActions + Execute
        self.startup = ran
        return ran

    # ------------------------------------------------------------- the frames
    def enter_zone(self, zone_id, action=True):
        """Stand in a zone and press action, running its scripts wired up."""
        z = next((x for x in self.world.zones if x.id == zone_id), None)
        if not z: return False
        cx, cy, cz = z.centre()
        self.world.new_vm = self.make_vm          # use the wired VM
        self.world.step((cx, cy, cz), z.arcMid)
        self.world.step((cx, cy, cz), z.arcMid, action=action)
        return True

    def spawn_player(self, area=None, pos=None):
        r"""Put a walking actor in the resident area's set.

        The set is `AREA +88` (FILE_FORMATS, the asset manifest) and the
        default position is the area's first `ADDRESSES` entry - somewhere
        `actor.goto_address` puts an actor, so a person can stand there by
        construction. The walker carries the narrow phase, so he collides.
        """
        area = self.chunk if area is None else area
        b = T.archive(omkpaths.data("IAM/AREA")).get(area)
        if b is None or len(b) < 97: return None
        setname = b[88:97].split(b"\0")[0].decode("cp1252", "replace")
        if not setname: return None
        if pos is None: pos = AC.area_start(area)
        if pos is None: return None
        g = omkdata.decor_geometry_cached(setname)
        y = omkdata.floor_under(g, [pos[0], pos[1] - 60.0, pos[2]]) if g else None
        if y is not None: pos = [pos[0], y, pos[2]]
        self.player = AC.Walker(setname, pos, ignore_ledges=True)
        self.events.append(("player", setname, [round(v, 1) for v in pos]))
        return self.player

    def walk(self, dx, dz, frames=1, facing=None, action=False):
        """Move the player and run that many frames with him where he lands."""
        if facing is not None: self.facing = facing
        for _ in range(frames):
            if self.player is not None: self.player.step(dx, dz)
            self.action = action
            self.frame(1)
        return self.player.pos if self.player else None

    def answer_ui(self, value):
        """Close the open UI screen with `value` - `Game_HandleEvent` case 5."""
        return self.world.answer_ui(value)

    def frame(self, n=1):
        """One frame, in `Game_Tick`'s order.

        `Script_Pump` runs the contexts first, then the scene interpreter, then
        the wait-slot scan - so a script that resumed this frame can start an
        object before it is ticked, and an object that finished releases the
        script waiting on it, which resumes on the NEXT pump. That ordering is
        the pacing: it is why a `.wait` costs at least one frame and why a
        cutscene advances a beat at a time.
        """
        for _ in range(n):
            if self.player is not None:
                # `Script_Pump`'s own first job: scan the zones at the actor's
                # position and facing, arm what he is standing in, then drain
                # the contexts. `pump` is the no-player path.
                self.world.step(tuple(self.player.pos), self.facing,
                                action=self.action)
                self.action = False
            else:
                self.world.pump(1)
            for p in self.programs: p.tick()
            self.world.release_waits()

    def tick(self, n=1):
        """Kept as the older name for `frame`; there is only one frame path."""
        return self.frame(n)


# -------------------------------------------------------- the area-load test
def area_load(area=118, interface=1, trace_path=None, frames=30):
    r"""Boot an area the way the engine does, and diff against the capture.

    AREA 118 is "Introduction Kay'l" - what a NEW GAME enters - and its
    startup script is the game's opening: the world-camera intro and
    conversation 272, then `area.goto 222` / `scene.load(222, 55)` into
    Impasse. `traces/intro.log` is the engine running exactly this.

    Two simplifications, stated rather than hidden, because both bound what
    this can claim:

    * **`ui.open` is a player question.** Its handler (0x00403860) stores the
      result-variable index in `dword_4E6B28` and sets the context status to
      **6 - suspended**; the UI later writes the player's answer there and
      resumes. Here the script runs straight through it, so `Interface`
      (variable 19) has to be seeded with what the player answered - the
      capture is the authority, and it took the non-zero arm. Seeding one
      variable from the capture is not fitting the result: the branch is
      binary and the other arm is a different, shorter intro.
    * **execution stops at `dialog.start`**, which is what the engine's
      `Script_Execute` does too (stage 1: 140 slots of 5785 end that way).
      Carrying on to the `area.goto`/`scene.load` at the script's tail needs
      the conversation to hand control back, which is not modelled - so this
      reaches trace event 7 and not 17.

    -> {"events": what the sim announced, "matched": how many the capture
        confirms in order, "trace": the capture's length, "why": where it
        stopped}
    """
    import re
    import goldentrace as G
    s = Session("AREA", area)

    seen, stamp, now = [], [], [0]
    base = s.make_vm
    def wrap(ctx=None):
        vm = base(ctx); cls = type(vm)
        class Tapped(cls):
            def step(self, op, raw, code, start, pc):
                for e in G.loggable(op, raw):
                    seen.append(e); stamp.append(now[0])
                return cls.step(self, op, raw, code, start, pc)
        return Tapped(s.state, s.trace)
    s.make_vm = wrap
    ran = s.load_area()

    tp = trace_path or os.path.join(ROOT, "traces/intro.log")
    pat = re.compile(r'Call KERNEL32\.GetPrivateProfileStringA\(\w+ "([A-Z]+)"'
                     r',\w+ "(-?\d+)"')
    tr = []
    for l in open(tp, encoding="utf-8", errors="replace"):
        m = pat.search(l)
        if m: tr.append((m.group(1), m.group(2)))

    def agree(evs):
        """-> (matched, furthest trace index reached, trace events skipped)"""
        i = n = far = 0
        cov = set()
        for e in evs:
            j = i
            while j < len(tr) and tr[j] != e: j += 1
            if j < len(tr):
                n += 1; i = j + 1; far = j + 1; cov.add(j + 1)
        return n, far, [k for k in range(1, far + 1) if k not in cov]

    # Advance frame by frame, closing a conversation whenever the world is
    # actually blocked on one. With pacing in, WHEN that happens is decided by
    # the run rather than by this driver - the intro parks on a
    # `scx.play.actor.wait` before it ever reaches `dialog.start` - so the old
    # fixed "run, end the dialog, run" sequence no longer describes it.
    # `Game_HandleEvent` case 63 is the only thing supplied: how long a
    # conversation lasts is not modelled.
    at_dialog = None
    for f in range(frames):
        now[0] = f
        # `Game_HandleEvent` case 5: the start menu closes with the player's
        # answer - and since 2026-08-30 that answer is DERIVED, by walking the
        # real widget tree with the engine's own input words rather than being
        # supplied as a literal. `ui_answer` opens the screen the script named
        # and presses confirm / right / confirm; what it returns is whatever
        # the item callback at the end of that path writes. See sim/ui.py.
        if s.world.ui:
            s.answer_ui(ui_answer(s.world.ui[2], interface))
        if s.world.dialog_state == 3:
            if at_dialog is None:
                at_dialog = (len(seen), agree(seen)[0], 3)
            s.run_dialog()          # walk the conversation, then close it
        s.frame(1)
    if at_dialog is None: at_dialog = (len(seen), agree(seen)[0], s.world.dialog_state)

    matched, far, skipped = agree(seen)
    # the frame the last confirmed event fired on - the pacing itself, which a
    # count of events cannot see: before `.wait` was modelled the whole run
    # finished in a handful of frames
    last = 0
    i = 0
    for e, f in zip(seen, stamp):
        j = i
        while j < len(tr) and tr[j] != e: j += 1
        if j < len(tr): last = f; i = j + 1
    ran_ctx = [(e[1], e[2], e[3]) for e in s.world.log if e[0] == "start"]
    return {"startup": ran, "events": seen, "matched": matched,
            "reach": far, "skipped": skipped,
            "trace": len(tr), "scx": s.scx, "started": ran_ctx,
            "last_frame": last,
            "at_dialog": at_dialog, "scene_of_222": s.state.scene_of(222)}


def tutorial_walk(area=222, zone=(3803, 3801, 3795), frames=2500,
                  trace_path=None):
    r"""The opening, and then the player WALKS - phase 6's last stage.

    `area_load` gets to trace event 42 with the player as a teleport. The
    capture's 43-58 are the tutorial, and they fire because a person walks into
    a trigger, so this puts a real `Walker` on the area's set - carrying the
    narrow phase, so he collides - and steers him to zone 3795's quad.

    **The path is supplied and that is the honest part.** Where the player
    walked is player input; it is not in the data and nothing here recovers it.
    What is tested is the other half: that walking there produces the engine's
    decisions, in the engine's order. The steering is a straight line to the
    zone's centre, which is the least interesting path that reaches it.

    Zone **3796** stays out of reach on purpose. Its save bit is 0 at new game
    and `Zones_RegisterAll` only rebuilds on an area load or transition (three
    call sites, none per-frame), so it goes live when the player crosses into
    area 142 - which the capture does at event 45 and this run does not.

    -> the same shape as `area_load`, plus what the walk added.
    """
    import re
    import goldentrace as G
    seen, stamp, now = [], [], [0]
    s = Session("AREA", 118)
    base = s.make_vm
    def wrap(ctx=None):
        vm = base(ctx); cls = type(vm)
        class Tapped(cls):
            def step(self, op, raw, code, start, pc):
                for e in G.loggable(op, raw):
                    seen.append(e); stamp.append(now[0])
                return cls.step(self, op, raw, code, start, pc)
        return Tapped(s.state, s.trace)
    s.make_vm = wrap
    s.load_area()
    for f in range(frames):
        now[0] = f
        if s.world.ui: s.answer_ui(1)
        if s.world.dialog_state == 3: s.run_dialog()
        s.frame(1)
    before = len(seen)

    s.spawn_player(area)
    reached = {}
    for zid in (zone if isinstance(zone, (list, tuple)) else [zone]):
        z = next((x for x in s.world.zones if x.id == zid), None)
        if z is None or s.player is None:
            reached[zid] = None; continue
        cx, _cy, cz = z.centre()
        r = AC.nav_route(s.player.set, s.player.pos, (cx, cz))
        if r is None:
            reached[zid] = None; continue
        for tx, tz in r:                      # follow the derived route
            for _ in range(30):
                px, _py, pz = s.player.pos
                vx, vz = tx - px, tz - pz
                L = (vx * vx + vz * vz) ** 0.5
                if L < 8.0: break
                s.walk(vx / L * 8.0, vz / L * 8.0, 1, facing=z.arcMid)
        s.walk(0.0, 0.0, 40, facing=z.arcMid, action=True)
        reached[zid] = round(((s.player.pos[0] - cx) ** 2 +
                              (s.player.pos[2] - cz) ** 2) ** 0.5, 1)

    tp = trace_path or os.path.join(ROOT, "traces/intro.log")
    pat = re.compile(r'Call KERNEL32\.GetPrivateProfileStringA\(\w+ "([A-Z]+)"'
                     r',\w+ "(-?\d+)"')
    tr = []
    for l in open(tp, encoding="utf-8", errors="replace"):
        m = pat.search(l)
        if m: tr.append((m.group(1), m.group(2)))
    i = far = 0
    cov = set()
    for e in seen:
        j = i
        while j < len(tr) and tr[j] != e: j += 1
        if j < len(tr):
            i = j + 1; far = j + 1; cov.add(j + 1)
    fired = sorted({e[1] for e in s.world.log
                    if e[0] == "ran" and isinstance(e[1], int) and e[1] > 0})
    return {"beforeWalk": before, "events": len(seen), "matched": len(cov),
            "reach": far, "trace": len(tr),
            "skipped": [k for k in range(1, far + 1) if k not in cov],
            "zonesFired": fired, "reached": reached,
            "registered": sorted(x.id for x in s.world.zones)}


# ------------------------------------------------------------------- the test
def stage4b(zone_id=3732):
    s = Session("SCENE", 53)
    s.enter_zone(zone_id)
    s.tick(120)
    started = [e for e in s.events if e[0] == "scx.start"]
    missed = [e for e in s.events if e[0] == "scx.miss"]
    dlg = next((e for e in s.events if e[0] == "dialog"), None)
    order = [n for n, _f in (s.trace.calls or [])]
    return {"scx": s.scx, "started": started, "missed": missed, "dialog": dlg,
            "order": order, "programs": len(s.programs),
            "running": sum(1 for p in s.programs if p.running)}


def main():
    r = stage4b()
    print("stage 4 - a launch end to end")
    print("  SCENE 53 plays its objects from %s" % r["scx"])
    for e in r["started"]:
        print("    scx.play.%-6s object %-4d %s" % (e[3], e[1], e[2]))
    if r["missed"]:
        print("    NOT FOUND: %s" % r["missed"])
    print("  dialog: %s" % (r["dialog"],))
    print("  the opcode order: %s" % " -> ".join(
        n for n in r["order"] if n in ("player.anim.hold", "scx.play.player.wait",
                                       "scx.play.player", "character.look_at_player",
                                       "dialog.start")))
    print("  %d scene programs started, %d still running after 120 frames"
          % (r["programs"], r["running"]))
    ok = (r["scx"] == "Re14.SCX" and len(r["started"]) == 2 and not r["missed"]
          and r["dialog"] and r["dialog"][1] == 387 and r["dialog"][3] == 13)
    print("stage 4 (launch): %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

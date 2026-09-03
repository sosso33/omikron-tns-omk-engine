#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Zones, the script scheduler and the trigger lifecycle - phase 6 stage 3.

Stage 1 executes one script. This is what decides *which* script runs and
*when*: the resident areas' trigger zones, the 16-slot prompt table, the
per-context action FIFO and the pump that drains it. Transcribed from
`Zones_RegisterAll` (0x00406560), `Script_Pump` (0x00407DC0) and
`Script_ProcessActions` (0x00408220), all three CLEAN.

    python3 tools/sim/world.py                 # the stage-3 test
    python3 tools/sim/world.py SCENE 53        # every zone of one chunk
    python3 tools/sim/world.py --walk 3732     # stand in a zone, print the trace

The shape, and it is small once the three functions are read together:

* a zone is registered only while its **save bit** is set (`Zone_StateBit`,
  the game-DB `+28` bitmap that `zone.enable`/`disable` flips), which is how a
  spent trigger stays retired across a save;
* standing inside its quad raises **event 8** - touch; facing into its arc as
  well raises **event 7**, which arms one of the 16 prompt slots;
* the pump turns an armed slot into a **context** holding the zone's three
  script slots, and queues action 1. `Script_ProcessActions` drains one action
  per frame, pointing the context's pc at slot n-1 and letting `Script_Execute`
  run it; action 4 frees the context.

  action 1 -> slot +0, the **enter** script
  action 2 -> slot +4, the **activate** script (where `dialog.start` lives),
              queued only when the player presses action inside the zone
  action 3 -> slot +8, the **leave** script
  action 4 -> free

The stage-3 test is the plan's own: put the player inside zone **3732** and its
activate script must fire. That zone is record 0 of `SCENE 53`, and its own
launch script is what starts dialog 387 - so the trace is checkable by name,
not just by "something ran".
"""
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
ROOT = os.path.dirname(os.path.dirname(HERE))

import dialog_disasm as D
import dialog_triggers as T
import gamestate
import omkpaths
from vm import VM, Trace

ZONE_STRIDE = 68
# where each archive keeps its zone table: (pointer, count)
ZONE_TABLE = {"AREA": (48, 76), "SCENE": (16, 44)}

ENTER, ACTIVATE, LEAVE, FREE = 1, 2, 3, 4


# the loaders' own unit conversion, `(100 * v) * 0.00390625 * 0.3937... - 1`
UNIT = 100.0 * 0.00390625 * 0.3937007874015748


def _conv(v):  return v * UNIT - 1.0


def _corner(c):
    """One quad corner, converted: y carries the loaders' extra -9."""
    return (_conv(c[0]), _conv(c[1]) - 9.0, _conv(c[2]))


class Zone:
    """One 68-byte trigger record, read as the runtime sees it."""

    __slots__ = ("arch", "chunk", "scripts", "quad", "arcMid", "arcWide",
                 "id", "camera")

    def __init__(self, arch, chunk, b, o):
        self.arch, self.chunk = arch, chunk
        self.scripts = struct.unpack_from("<3i", b, o)          # +0/+4/+8
        # The quad is stored in AUTHORING units and the loader converts it.
        # `Area_Load`/`Scene_Load` run `(100 * v) * 0.00390625 * 0.3937... - 1`
        # over +12/+20/+24/+32/+36/+44/+48/+56 - the four corners' x and z -
        # and the same with a further **-9** over +16/+28/+40/+52, their y.
        # Reading them raw is self-consistent as long as nothing else is in the
        # frame, which is why it survived until an actor standing at an
        # authored ADDRESSES position was 43000 units from the zone he was in.
        self.quad = [_corner(struct.unpack_from("<3i", b, o + 12 + 12 * i))
                     for i in range(4)]                          # +12
        self.arcMid, self.arcWide = struct.unpack_from("<2H", b, o + 60)
        self.id, self.camera = struct.unpack_from("<2h", b, o + 64)

    def __repr__(self):
        return "<zone %d %s %d scripts=%s>" % (self.id, self.arch, self.chunk,
                                               [s for s in self.scripts if s])

    # ------------------------------------------------------------ geometry
    def contains(self, x, z):
        """Point in the quad's XZ footprint, by the winding test.

        The quad is authored as four corners in order, so a crossing count
        over its edges is the containment the spatial index approximates.
        """
        inside = False
        n = len(self.quad)
        for i in range(n):
            x1, _y1, z1 = self.quad[i]
            x2, _y2, z2 = self.quad[(i + 1) % n]
            if (z1 > z) != (z2 > z):
                t = (z - z1) / float(z2 - z1) if z2 != z1 else 0.0
                if x < x1 + t * (x2 - x1): inside = not inside
        return inside

    def faces(self, facing):
        """Is `facing` (0..4095) inside the arc? Width 0 means any facing."""
        if not self.arcWide: return True
        d = (int(facing) - int(self.arcMid) + 2048) % 4096 - 2048
        return abs(d) <= self.arcWide // 2

    def centre(self):
        return (sum(c[0] for c in self.quad) / 4.0,
                sum(c[1] for c in self.quad) / 4.0,
                sum(c[2] for c in self.quad) / 4.0)


def zones_of(arch, chunk):
    """Every zone record of one chunk, registered or not."""
    b = T.archive(omkpaths.data("IAM", arch)).get(chunk)
    if b is None: return [], None
    po, co = ZONE_TABLE[arch]
    if len(b) < co + 2: return [], b
    p, n = struct.unpack_from("<I", b, po)[0], struct.unpack_from("<h", b, co)[0]
    if n <= 0 or p + ZONE_STRIDE * n > len(b): return [], b
    return [Zone(arch, chunk, b, p + ZONE_STRIDE * i) for i in range(n)], b


# --------------------------------------------------------- startup scripts
# `AREA +4` / `SCENE +4` is the script the chunk runs when it is LOADED, and
# it is not in any record table - see FILE_FORMATS "AREA +4 / SCENE +4". The
# engine runs it in `Area_TickLoad` (0x0040C7E0) case 9, once for the AREA
# block and once for the SCENE the game DB says is over it:
#
#     mov  ecx, [esi+4]                   ; the startup script
#     call sub_406290                     ; Script_NewContext(slot, script, 0, 0)
#     mov  [esi], eax                     ; the context goes to block +0
#     call sub_4063D0                     ; Script_QueueAction(ctx, 1)
#
# Action 1 is the same action the zone pump uses for an enter script, so a
# startup context is just a context whose slot 0 is the +4 pointer. That is
# what `StartupSite` is: a zone-shaped stand-in so one pump drains both.

def startup_script(arch, chunk):
    """The chunk's startup script offset, or None. -> (offset, buffer)."""
    b = T.archive(omkpaths.data("IAM", arch)).get(chunk)
    if b is None or len(b) < 8: return None, b
    p = struct.unpack_from("<I", b, 4)[0]
    return (p if 0 < p < len(b) else None), b


class StartupSite:
    """Not a zone - the load-time context's stand-in, so one pump serves both.

    It carries the same three script slots a zone does, with only the first
    filled, because `Script_QueueAction(ctx, 1)` is exactly what the engine
    queues for it.
    """
    __slots__ = ("arch", "chunk", "scripts", "id")

    def __init__(self, arch, chunk, at):
        self.arch, self.chunk = arch, chunk
        self.scripts = (at, 0, 0)
        self.id = -1                     # no ZONES id: it is not a trigger

    def __repr__(self):
        return "<startup %s %d @%d>" % (self.arch, self.chunk, self.scripts[0])


class Context:
    """Script_NewContext's block, reduced to what the pump actually uses."""

    def __init__(self, zone):
        self.zone = zone
        self.slots = zone.scripts        # [0] enter, [1] activate, [2] leave
        self.queue = []                  # the 4-deep action FIFO
        self.pc = None                   # ctx+12, the resume point
        self.status = 0                  # 0 idle, 1 running
        self.freed = False
        self.vm = None                   # the context's own 64-byte stack
        self.act = None

    def queue_action(self, n):
        if len(self.queue) < 4: self.queue.append(n)
        return True


class World:
    r"""The resident areas, their zones, and the pump over them.

    One simplification, stated rather than hidden: the engine keeps **two**
    area slots and rebuilds the index on a transition, while this holds one
    chunk at a time. Nothing in stage 3's behaviour depends on the second slot
    - `Zones_RegisterAll` walks the slots independently - and stage 5 is where
    a real transition belongs.
    """

    def __init__(self, arch, chunk, state=None, trace=None):
        self.state = state or gamestate.load()
        self.trace = trace or Trace(keep=True)
        self.arch, self.chunk = arch, chunk
        self.all, self.code = zones_of(arch, chunk)
        # a script offset is relative to ITS OWN chunk, so once two chunks are
        # resident the pump has to know which buffer a context runs against
        self.buffers = {(arch, chunk): self.code}
        self.zones = self.register()
        self.slots = {}                  # zone id -> Context, the 16 prompts
        # the engine keeps the PROMPT slots (unk_4E6B30) and the CONTEXT table
        # (dword_4E61E8) apart, and the pump walks the second: a context can
        # outlive its prompt, and a startup context never had one at all
        self.contexts = []
        # g_DialogState: 1 = free, 3 = a conversation is on screen. Dialog_Load
        # writes 3 and `Game_HandleEvent` case 63 writes 1 back.
        self.dialog_state = 1
        # `area.goto` parks its caller at status 10 and stages a load into the
        # other slot; `Script_Pump`'s tail finishes it once `Area_TickLoad`
        # reports done. Held as one pending request rather than the two-slot
        # machinery, which nothing here depends on yet.
        self.pending = None
        # `dword_910500/4/8` - the 32 wait slots. `scx.play*.wait` registers
        # the object it started against the calling context and parks that
        # context at status 4; `Game_Tick` clears the slot and raises event 3
        # once the object stops being busy, and `Game_HandleEvent` case 3 puts
        # the context back to 1. That is the whole of the game's pacing.
        self.waits = {}                  # id(ctx) -> (ctx, program)
        # `ui.open`'s pending question: (context, result variable). The handler
        # stores the variable index in `dword_4E6B28` and parks the caller at
        # status 6; `Game_HandleEvent` case 5 writes the player's answer into
        # that variable and puts the context back to 1.
        self.ui = None
        self.log = []

    def add_chunk(self, arch, chunk):
        """A second resident chunk - the SCENE loaded over the AREA.

        `Zones_RegisterAll` walks the slots independently, so its zones simply
        join the list and go through the same save-bit filter.
        """
        if (arch, chunk) in self.buffers:
            # already resident. `Zones_RegisterAll` rebuilds from the slots, it
            # does not append, so re-adding would double every zone - which is
            # what happened the first time an area was entered twice.
            self.zones = self.register()
            return self.buffers[(arch, chunk)]
        zs, b = zones_of(arch, chunk)
        self.buffers[(arch, chunk)] = b
        self.all = self.all + zs
        self.zones = self.register()
        return b

    def code_for(self, site):
        """The buffer a context's script offsets point into."""
        return self.buffers.get((site.arch, site.chunk), self.code)

    def register(self):
        """Zones_RegisterAll: only those whose save bit is set."""
        out = []
        for z in self.all:
            if self.state.zone_state(z.id): out.append(z)
        return out

    # --------------------------------------------------------------- events
    def scan(self, pos, facing):
        """Actor_ScanZones: -> (touched, activatable) by zone id."""
        x, _y, z = pos
        touched, arm = [], []
        for zn in self.zones:
            if not zn.contains(x, z): continue
            touched.append(zn)
            if zn.faces(facing): arm.append(zn)
        return touched, arm

    def step(self, pos, facing, action=False):
        """One frame of Script_Pump(1) at that position."""
        touched, arm = self.scan(pos, facing)
        armed = {z.id for z in arm}

        for zn in arm:                                   # event 7
            if zn.id in self.slots: continue
            ctx = Context(zn)
            self.slots[zn.id] = ctx
            self.contexts.append(ctx)
            if zn.scripts[0]: ctx.queue_action(ENTER)
            self.log.append(("enter", zn.id))

        for zid, ctx in list(self.slots.items()):
            if zid not in armed:                          # left the zone
                if ctx.slots[2]: ctx.queue_action(LEAVE)
                ctx.queue_action(FREE)
                self.log.append(("leave", zid))
            elif action and ctx.slots[1]:
                ctx.queue_action(ACTIVATE)
                self.log.append(("activate", zid))

        self.pump(1)
        return touched, arm

    def new_vm(self, ctx=None):
        """The VM a context runs on. `Session` swaps this for the wired one.

        The context is passed because `area.goto` has to park **its own
        caller** at status 10.
        """
        return VM(self.state, self.trace)

    def process(self, ctx):
        """`Script_ProcessActions` - arm the next action. It does NOT execute.

        The engine's loop is `Script_ProcessActions(c); Script_Execute(c);`
        per context per frame, and keeping them apart is what makes a yield
        possible: this refuses to touch a context whose status is already 1
        (`if (u16i(ctx,11)) return`), so a script that stopped mid-way keeps
        its own pc and stack until it finishes.
        """
        if ctx.status or not ctx.queue: return
        act = ctx.queue.pop(0)
        if act == FREE:
            ctx.freed = True
            return
        p = ctx.slots[act - 1]
        if not p: return
        ctx.act, ctx.pc, ctx.status = act, p, 1
        ctx.vm = self.new_vm(ctx)

    def execute(self, ctx):
        r"""`Script_Execute` (0x00406460) - run from `ctx.pc` while status is 1.

        The one subtlety is `dialog.start`, and it is not a suspend. The
        handler writes no status; the interpreter's loop simply does

            (*(&off_4C0140 + 2 * v2))(a1);
            if (v4 == 61) return;          /* dialog.start */

        so the context is left **running, with its pc already past the
        operands** - it yields for the frame and the pump resumes it on the
        next one. The context's stack survives because it belongs to the
        context, which is why the VM is created once in `process` and reused
        here rather than rebuilt per call.
        """
        if ctx.status != 1 or ctx.pc is None: return
        why, pc = ctx.vm.run(self.code_for(ctx.zone), ctx.pc)
        name = {1: "enter", 2: "activate", 3: "leave"}.get(ctx.act, "?")
        if why == "dialog":
            ctx.pc = pc                   # status stays 1: resumed next frame
            self.log.append(("yield", ctx.zone.id, name, why))
        elif why == "suspended":
            # `area.goto` parked it at 10; the pc is already past the operands,
            # exactly as `Script_Execute` leaves it
            ctx.pc = pc
            self.log.append(("suspend", ctx.zone.id, name, ctx.status))
        else:
            ctx.status, ctx.pc = 0, None
            self.log.append(("ran", ctx.zone.id, name, why))

    def start_script(self, arch, chunk, at, why=""):
        """`Script_NewContext(slot, script, 0, 0)` + `Script_QueueAction(ctx,1)`.

        The one way a chunk's startup script is ever launched, used by the area
        load, by `scene.load` (whose handler does exactly this - see
        `Session`), and by `Session.load_area`.
        """
        if not at: return None
        ctx = Context(StartupSite(arch, chunk, at))
        ctx.queue_action(ENTER)
        self.contexts.append(ctx)
        self.log.append(("start", arch, chunk, at, why))
        return ctx

    def load_scene(self, area, scene):
        """VM op 71 `scene.load`: make the scene resident and run its `+4`.

        The handler ends `Scene_Block(scene)` -> `[ebx+4]` ->
        `Script_NewContext` -> `Script_QueueAction(ctx, 1)`, so the opcode is
        itself a launcher - it does not wait for an area load. It also calls
        `Area_SetLoadedScene(area, scene)`, which is why the state write lives
        in the VM.
        """
        self.add_chunk("SCENE", scene)
        at, _b = startup_script("SCENE", scene)
        return self.start_script("SCENE", scene, at, "scene.load %d" % scene)

    def begin_area(self, area, ctx):
        """VM op 47 `area.goto` -> `Area_Transition` case 0.

        `Area_LoadIntoSlot(1 - slot, area)` starts a staged load and the caller
        is parked at **status 10** (`u16(a3,22) = 10`), which
        `Script_ProcessActions` refuses to touch. `Script_Pump`'s tail finishes
        it when `Area_TickLoad` reports done, and only then does the caller go
        back to 1 and resume after its `area.goto`.
        """
        if ctx is not None: ctx.status = 10
        self.pending = (area, ctx)
        self.log.append(("area.goto", area))

    def finish_area(self):
        """`Script_Pump`'s tail + `Area_TickLoad` case 9, once the load is done.

        Runs the AREA's startup script, then the SCENE's if the game DB says
        one is over it, then releases the waiting context. The startup contexts
        are appended AFTER the waiting one, so within the frame the caller
        resumes first - which is the order the capture shows.
        """
        area, ctx = self.pending
        self.pending = None
        self.add_chunk("AREA", area)
        if ctx is not None: ctx.status = 1
        at, _b = startup_script("AREA", area)
        self.start_script("AREA", area, at, "area %d" % area)
        sc = self.state.scene_of(area)
        if sc is not None and sc != -1:
            self.add_chunk("SCENE", sc)
            at2, _b2 = startup_script("SCENE", sc)
            self.start_script("SCENE", sc, at2, "scene %d over area %d" % (sc, area))

    def ui_open(self, ctx, screen, var):
        """VM op 70 `ui.open` - ask the player, and park the caller at 6.

        The handler (0x00403860) stores the result variable's index in
        `dword_4E6B28`, writes **6** into the context's status word and hands
        the screen to `UI_OpenScreen`. Nothing resumes until the screen closes.
        """
        if ctx is not None: ctx.status = 6
        self.ui = (ctx, var, screen)
        self.log.append(("ui.open", screen, var))

    def answer_ui(self, value):
        """`Game_HandleEvent` case 5 - the screen closed with an answer.

            if (dword_4E6B28 != -1) { Var_Set(dword_4E6B28, u32(a2,8)); ... }
            u16(ctx, 22) = 1;

        The caller's argument block carries the context index at `+4` and the
        chosen value at `+8` (`sub_466B60` fills both from `dword_930744` /
        `dword_930750`). Which value a person picks is player input and is not
        in the data - but the MECHANISM is, so the simulator suspends and
        resumes exactly where the engine does, and the one supplied number is
        the answer rather than a variable quietly set before the run.
        """
        if not self.ui: return False
        ctx, var, _screen = self.ui
        self.ui = None
        if var is not None and var != -1:
            struct.pack_into("<i", self.state.raw,
                             self.state.offset(0) + 4 * var, value)
        if ctx is not None and ctx.status == 6: ctx.status = 1
        self.log.append(("ui.answer", var, value))
        return True

    def wait_on(self, ctx, program):
        """`scx.play*.wait`: register the object and park the caller at 4."""
        if ctx is None or program is None: return
        self.waits[id(ctx)] = (ctx, program)
        ctx.status = 4
        self.log.append(("wait", getattr(ctx.zone, "chunk", None), program.obj["name"]))

    def release_waits(self):
        """`Game_Tick`'s slot scan, then `Game_HandleEvent` case 3.

            if (*v9 && !ScriptObject_IsBusy(*v9)) { *v9 = 0; RaiseEvent(3, v8); }
            case 3: if (status == 4) status = 1;
        """
        for key, (ctx, prog) in list(self.waits.items()):
            if not prog.running:
                del self.waits[key]
                if ctx.status == 4: ctx.status = 1
                self.log.append(("release", prog.obj["name"]))

    def pump(self, n=1):
        """`Script_Pump` phase 1's context loop, n frames of it:

            for (c = dword_4E61E8; c < &unk_4E6268; ++c)
                if (*c) { Script_ProcessActions(*c); Script_Execute((int)*c); }
        """
        for _ in range(n):
            # `Script_Pump` case 1's own first test - the WHOLE per-frame step
            # is frozen while a conversation is up, prompt slots and
            # Script_Execute included:
            #     if (!g_DialogState) { g_DialogState = 1; return 1; }
            #     if (g_DialogState != 1) return 1;
            # This is what makes `dialog.start` block. The handler writes no
            # status and Script_Execute has no gate of its own, so a context
            # that yielded there would otherwise resume on the very next frame
            # - and the capture rules that out: conversation 272 is announced
            # at t=41.4 s and the next camera at t=151.5 s, 110 seconds later.
            if self.dialog_state != 1: return
            if self.pending: self.finish_area()
            for ctx in list(self.contexts):
                self.process(ctx)
                self.execute(ctx)
                if ctx.freed:
                    if ctx in self.contexts: self.contexts.remove(ctx)
                    self.slots.pop(ctx.zone.id, None)


# ------------------------------------------------------------------- the test
def stage3(zone_id=3732):
    r"""The plan's own test: stand in zone 3732, its activate script must fire.

    Zone 3732 is record 0 of SCENE 53, and its activate slot is the script that
    launches dialog 387 - so this checks a named outcome, not merely that
    something executed.
    """
    state = gamestate.load()
    found = None
    for arch in ("SCENE", "AREA"):
        for k in sorted(T.archive(omkpaths.data("IAM", arch)).keys()):
            zs, _b = zones_of(arch, k)
            for z in zs:
                if z.id == zone_id: found = (arch, k, z)
            if found: break
        if found: break
    if not found: return None
    arch, chunk, z = found

    w = World(arch, chunk, state)
    before = state.zone_state(zone_id)
    # compare by id: World re-reads the records, so the objects differ
    if zone_id not in {zz.id for zz in w.zones}:   # its save bit must be set
        return {"zone": zone_id, "arch": arch, "chunk": chunk,
                "registered": False, "scripts": [bool(s) for s in z.scripts],
                "ran": [], "calls": [], "dialogs": []}
    cx, cy, cz = z.centre()
    w.step((cx, cy, cz), z.arcMid)             # walk in, facing into the arc
    w.step((cx, cy, cz), z.arcMid, action=True)  # press action
    ran = [e for e in w.log if e[0] == "ran"]
    calls = [c for c in (w.trace.calls or [])]
    # the activate script retires its own zone: `zone.disable 3732`. Re-running
    # Zones_RegisterAll must now drop it, which is the lifecycle closing.
    after = state.zone_state(zone_id)
    again = zone_id in {zz.id for zz in w.register()}
    return {"zone": zone_id, "arch": arch, "chunk": chunk,
            "registered": True, "scripts": [bool(s) for s in z.scripts],
            "ran": ran, "calls": calls, "bitBefore": before, "bitAfter": after,
            "reRegisters": again,
            "dialogs": [f[0] for n, f in calls if n == "dialog.start"]}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--walk" in sys.argv:
        zid = int(args[0]) if args else 3732
        r = stage3(zid)
        print(r if not r else "\n".join("  %s" % (k,) for k in r.items()))
        return 0
    if len(args) >= 2:
        arch, chunk = args[0].upper(), int(args[1])
        zs, _ = zones_of(arch, chunk)
        st = gamestate.load()
        print("%s %d: %d zone records" % (arch, chunk, len(zs)))
        for z in zs:
            print("  id %-5d %s scripts %-9s arc %5d/%-5d cam %-5d  centre %s"
                  % (z.id, "on " if st.zone_state(z.id) else "off",
                     "".join("EAL"[i] if s else "-"
                             for i, s in enumerate(z.scripts)),
                     z.arcMid, z.arcWide, z.camera,
                     tuple(round(v) for v in z.centre())))
        return 0

    r = stage3()
    if not r: print("zone 3732 not found"); return 1
    print("stage 3 - the trigger lifecycle")
    print("  zone 3732 lives in %s %d, registered %s, scripts E/A/L = %s"
          % (r["arch"], r["chunk"], r["registered"], r["scripts"]))
    for e in r["ran"]: print("  ran %s script of zone %d -> %s" % (e[2], e[1], e[3]))
    print("  calls: " + ", ".join("%s%s" % (n, list(f) if f else "")
                                  for n, f in r["calls"]))
    print("  dialogs launched: %s" % r["dialogs"])
    print("  zone save bit %d -> %d, re-registers: %s"
          % (r["bitBefore"], r["bitAfter"], r["reRegisters"]))
    ok = r["dialogs"] == [387] and r["bitBefore"] == 1 and r["bitAfter"] == 0 \
         and not r["reRegisters"]
    print("stage 3: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

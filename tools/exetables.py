#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Lift the tables compiled into `Runtime 2.exe` out into data files.

**Why this exists.**  A replica engine asks the user for their own copy of the
game and reads `gamedata/`.  These tables are not in `gamedata/` — they are `.data` and
`.rdata` in the executable — so a replica that needs them has nowhere to get
them from.  `RECONSTRUCTION.md` names this as the one gap a level-C build hits
and deliberately deferred it ("doing it now only creates a second copy to
drift"); that reason expires the moment `engine/` exists, which is what this
closes.

They are **facts about the game, not code**, so lifting them is tier 1 of the
porting plan — the tier that needs no CLEAN body and carries no
original-assembly risk.  Nothing here transcribes an instruction.

    python3 tools/exetables.py            # write tables/*.json
    python3 tools/exetables.py --check    # re-derive and diff against disk
    python3 tools/exetables.py --show camera_presets

`verify.py: exe tables` runs `--check`, so a stale file fails the suite the way
a stale `INDEX.md` does.

**Every table here is self-checking**, because a wrong base address produces
plausible-looking numbers and nothing else would catch it.  The checks are in
`_TABLES` beside each entry; the strongest are:

  * the **camera presets** index themselves — each row's `mode` field at +36
    equals its own row number, for 22 rows, and row 22 reads 24909.  That
    fixes both the base and the 48-byte stride with no external evidence;
  * the **ADPCM** step and index tables come out byte-identical to the ones
    `tools/adp.py` transcribed by hand from `sub_483200` — two independent
    readings of the same 105 numbers;
  * the **VM** handler addresses match `clean/_vmsummary.json`, which was
    built by a different tool from the disassembly;
  * **`tab_special_move`** terminates on three zero dwords at exactly 66 rows,
    and all 54 distinct move names the shipped `.CTL` files use resolve in it.

**What is NOT here, and why.**  `RECONSTRUCTION.md` §4 listed the weapon stats
among the compiled tables.  They are not: `Weapon_SlotForObject` indexes
`IAM\GLOBAL +32`, which ships.  So do the fight-AI profiles (`.CTL` +76/+80)
and the combination table (`GLOBAL +12`).  A replica reads those from the
user's data like everything else, and lifting them here would be the second
copy the plan warned about.
"""
import omkpaths
import bisect, json, os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
OUT = os.path.join(ROOT, "tables")

from ui_tables import Exe
import ui_tables as U


# --------------------------------------------------------------- the tables

def t_vm_opcodes(e):
    r"""The 153-entry VM dispatch table at 0x004C0140, 8 bytes an entry.

    `handler` and `operands` are the table's own two dwords.  `length` is what
    a decoder must actually use: **12 of the table's operand counts are
    wrong**, recovered from the handlers' own assembly and corpus-confirmed
    (`dialog_disasm.LEN_FIX`), and a port that trusts the table instead will
    desynchronise on the world scripts.  The disagreements are kept visible in
    `table_says` rather than silently overwritten.

    `tag` is the `IAM\*.TAG` section the handler announces its operand to —
    which is how most of the unnamed opcodes are identified at all — and
    `name` the mnemonic where one has been established.
    """
    import dialog_disasm as D
    tab = json.load(open(omkpaths.clean("_vmsummary.json")))
    rows = []
    for r in tab:
        op = r["op"]
        ent = struct.unpack_from("<2I", e.read(0x004C0140 + 8 * op, 8))
        rows.append({
            "op": op,
            "handler": "0x%08X" % ent[0],
            "length": D.oplen(op),
            "table_says": ent[1],
            "corrected": op in D.LEN_FIX,
            "name": D.NAME.get(op),
            "tag": D.SECTION.get(op),
        })
    return rows


def c_vm_opcodes(rows, e):
    r"""Against `clean/_vmsummary.json`, which a different tool derived from
    the disassembly - so agreement is two readings of the same table.

    **152, not 153, and the missing one is the point of doing this.** Op 2's
    handler is 0 in `_vmsummary.json`: the extraction pass could not bound
    that block (`CLAUDE.md` 1 lists the same trap for opcodes 77, 120 and
    152). The image says 0x00401B80 and the image is the authority, so lifting
    from it fills a hole the derived JSON has. Every one of the other 152
    agrees exactly."""
    tab = json.load(open(omkpaths.clean("_vmsummary.json")))
    byop = {r["op"]: r for r in tab}
    def h(r): return int(r, 16) if isinstance(r, str) else r
    agree = sum(1 for r in rows
                if h(r["handler"]) == h(byop[r["op"]]["handler"]))
    blank = [r["op"] for r in rows if not h(byop[r["op"]]["handler"])]
    return [("entries", len(rows), 153),
            ("handlers agreeing with clean/_vmsummary.json", agree, 152),
            ("opcodes that JSON left unbound and the image supplies",
             blank, [2]),
            ("...and none of them is null in the image",
             all(int(rows[o]["handler"], 16) for o in blank), True),
            ("lengths corrected against the table",
             sum(1 for r in rows if r["corrected"]), 21),   # 43, 44, 16 and 62 added 2026-09-02
            ("named", sum(1 for r in rows if r["name"]), 129)]   # case.i16, case.i32


def t_special_moves(e):
    r"""`tab_special_move[]` at 0x004CB168 — 12 bytes a row.

    The engine's own name for it, from its debug string.  A row is `char[8]`
    packed as two dwords (which is why a string search never finds it) plus
    the handler's address.  `Cef_QueueSpecialMove` matches a `.CTL` state's
    `0x10` move name against it, player only, and queues the handler: these
    are the gameplay events a state can raise — locomotion, jumps, dives,
    object pickup, camera modes, the 2D-menu keys.
    """
    rows, i = [], 0
    while True:
        r = e.read(0x004CB168 + 12 * i, 12)
        if struct.unpack("<3I", r) == (0, 0, 0): break
        rows.append({"index": i,
                     "name": r[:8].rstrip(b"\0").decode("ascii"),
                     "handler": "0x%08X" % struct.unpack_from("<I", r, 8)[0]})
        i += 1
    return rows


def c_special_moves(rows, e):
    import glob, anim_ctl
    have = {r["name"] for r in rows}
    files = (sorted(glob.glob(omkpaths.data("ANIMS/*.CTL"))) +
             sorted(glob.glob(omkpaths.data("ANIMS/*.ctl"))))
    used, miss = set(), set()
    for f in files:
        for st in anim_ctl.walk(f)["states"]:
            if not st["mdname"]: continue
            n = st["mdname"][:8]
            used.add(n)
            if n not in have and n.lower() != "none": miss.add(n)
    return [("rows before the zero terminator", len(rows), 66),
            ("distinct names the shipped .CTL files use", len(used), 54),
            ("of those the table lacks", len(miss), 0)]


CAM_PRESETS = (0x004C20C8, 48)

def t_camera_presets(e):
    r"""The camera-mode preset table at 0x004C20C8 — 48 bytes a mode.

    Field layout from `Camera_LoadParams` (0x004146C0), which is the only
    consumer.  `subject` decides what each point MEANS, per point rather than
    per camera: −1 makes it absolute scene coordinates, anything else makes it
    an offset from that actor.

    Offsets are **inches** and authored in metric — mode 0, the default
    third-person follow, sits at −118.11 on z, which is exactly 3.00 m.

    Modes 12 and 13 are blank on purpose: dialogue framing and `player.move`
    carry their parameters in the request instead, and mode 16 loads nothing
    at all.
    """
    base, stride = CAM_PRESETS
    rows = []
    m = 0
    while True:
        r = e.read(base + stride * m, stride)
        mode = struct.unpack_from("<h", r, 36)[0]
        if mode != m: break          # the table indexes itself; see c_ below
        eye = struct.unpack_from("<3f", r, 4)
        tgt = struct.unpack_from("<3f", r, 16)
        roll, fov = struct.unpack_from("<2f", r, 28)
        s0, s1, f42, f44, f46 = struct.unpack_from("<5h", r, 38)
        rows.append({
            "mode": m,
            "eye": [round(v, 4) for v in eye],
            "target": [round(v, 4) for v in tgt],
            "roll": round(roll, 4), "fov": round(fov, 4),
            "eyeSubject": s0, "targetSubject": s1,
            "f42": f42, "f44": f44, "f46": f46,
        })
        m += 1
    return rows


def c_camera_presets(rows, e):
    base, stride = CAM_PRESETS
    # the walk stopped because a row's +36 stopped matching its index - so the
    # base and the stride are both pinned by the data, not chosen
    nxt = struct.unpack_from("<h", e.read(base + stride * len(rows), 48), 36)[0]
    m0 = rows[0]
    return [("modes before the self-index breaks", len(rows), 22),
            ("the row after it reads", nxt, 24909),
            ("mode 0 sits this far back, in inches", m0["eye"][2], -118.1102),
            ("...which is this many metres", round(-m0["eye"][2] * 0.0254, 2), 3.0),
            ("mode 0 fov", m0["fov"], 75.0),
            ("modes with no preset of their own (12/13/16 and friends)",
             sum(1 for r in rows if r["fov"] == 0.0), 5)]


ADPCM = {"index": (0x004BCC10, 16), "step": (0x004BCC50, 89)}

def t_adpcm(e):
    r"""The OTNS ADPCM step and index tables, at dword_4BCC10 / dword_4BCC50.

    Read by `sub_483200` (mono) and `sub_483340` (stereo).  They are the
    standard IMA tables; the codec around them is not (high nibble first, and
    no `step >> 3` bias term) — see `tools/adp.py`, which is the decoder.
    """
    return {k: list(struct.unpack_from("<%di" % n, e.read(va, 4 * n)))
            for k, (va, n) in ADPCM.items()}


def c_adpcm(rows, e):
    import adp
    return [("step entries", len(rows["step"]), 89),
            ("index entries", len(rows["index"]), 16),
            ("step identical to adp.py's hand transcription",
             rows["step"] == adp.STEP, True),
            ("index identical to adp.py's",
             rows["index"] == adp.INDEX, True)]


def t_key_bindings(e):
    r"""The four default control schemes — 4 context groups x 14 actions.

    Three compiled tables, one per device, at 0x004C8F90 / 0x004C9070 /
    0x004C9150.  A keybind option row's apply hook writes to
    `table[group*14 + action]`, which is what fixes the shape.

    The groups are contexts and the engine installs them where their names
    say: `Game_Init` and the end of a fight or a shoot install 0 (Aventure),
    `Fight_Begin` 3, `Shoot_Enter` 2, the swim transitions 1.  Rebinding is
    group-local — `Opt_RebindKey` scans all 74 option rows but clears only a
    row in the same group, which is why "Avancer" is UP in Aventure and
    "Reculer" is LEFT in Combat without either disturbing the other.

    `bit` is the action's bit in the 14-slot input word — the same word `.CTL`
    transitions match on, and the one `Game_Frame` edge-filters for the UI.
    """
    from sim.ui import bindings, BIND_GROUPS, BIND_SLOTS
    b = bindings(e)
    return {"groups": {str(g): BIND_GROUPS[g] for g in BIND_GROUPS},
            "slots": BIND_SLOTS,
            "rows": [dict(group=g, action=a, **b[g][a])
                     for g in BIND_GROUPS for a in range(BIND_SLOTS)]}


def c_key_bindings(rows, e):
    r = rows["rows"]
    kb = [x["keyboard"] for x in r]
    ms = [x["mouse"] for x in r]
    js = [x["joystick"] for x in r]
    # every code must live inside the space Input_ReadOneControl produces for
    # its own device: a scan code 1..255, a mouse button 12..14, a joystick
    # button its index plus 48
    return [("cells", len(r), 4 * 14),
            ("keyboard codes set", sum(1 for x in kb if x), 41),
            ("...all of them scan codes", all(0 < x < 256 for x in kb if x), True),
            ("joystick codes set", sum(1 for x in js if x), 48),
            ("...all in its own space",
             all(x == 4 or 48 <= x <= 57 for x in js if x), True),
            ("mouse codes set", sum(1 for x in ms if x), 4),
            ("...all buttons 12..14", all(x in (12, 13, 14) for x in ms if x), True),
            # per group, because the total hides which context is sparse:
            # Nager has 7 of its 14 slots labelled and Tirer 13
            ("labelled cells per group",
             {g: sum(1 for x in r if x["group"] == g and x["label"])
              for g in sorted({x["group"] for x in r})},
             {0: 10, 1: 7, 2: 13, 3: 10})]


def t_ui(e):
    r"""The interface's three compiled tables, plus the option tree's shape.

    Already read by `tools/ui_tables.py`; carried here so a replica has them
    as data.  The screens table is the one with an order worth trusting: its
    `+4` is 0..36 in the code, so the row order is the screen id.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import fnt
    # The 13-record FONT TABLE at 0x004C7090, walked by `Font_Find`: each face
    # is keyed by an ASCII LETTER, and carries the kerning added to every
    # advance and the advance a glyph the file does not have falls back to.
    # A `.FNT` alone cannot lay text out - the advance is width + kern, and
    # both live here rather than in the glyph.
    return {"screens": U.screens(e), "sounds": U.sounds(e),
            "options": U.options(e), "pages": U.pages(e),
            "oscillators": U.oscillators(e),
            "fonts": fnt.table(e)}


def c_ui(rows, e):
    return [("screens", len(rows["screens"]), 37),
            ("interface sounds", len(rows["sounds"]), 45),
            ("option rows", len(rows["options"]), 74),
            ("option pages", len(rows["pages"]), 13),
            ("animation oscillators", len(rows["oscillators"]), 8),
            ("font faces", len(rows["fonts"]), 13),
            ("...keyed by a distinct letter",
             len({f["letter"] for f in rows["fonts"]}), 13)]


def t_vm_announce(e):
    r"""Which `IAM\*.TAG` domain each VM handler announces its operand to.

    Read from the assembly by `tools/vm_announce.py`: every announcing handler
    passes a section name to `Dbg_LogTagged` (sub_40EC70), and that call sits
    before the debug window's `if (hWnd)` - which is what makes the golden
    traces possible at all, since the engine narrates itself through
    `GetPrivateProfileStringA` whether or not anything is listening.

    `field` is WHICH operand is announced, and it is not always the first:
    `scene.load(area, scene)` announces the **scene**, `inventory.add(list,
    object)` the **object**. 49 handlers announce; every other opcode is
    silent, and a reader must treat that as silence rather than as a default.

    Emitted as data because the alternative is a hand-written map, and a
    hand-written one was wrong three ways within an hour of being written:
    `scx.play` (57) and `music.play` (103) announce **nothing**, and
    `scx.play.actor.wait` (60) announces CHARACTERS - the actor - not the
    object. Deriving it removes the guess.
    """
    import vm_announce
    return [{"op": r["op"], "name": r["name"], "domain": r["domain"],
             "field": r["map"], "agrees": r["agrees"]}
            for r in sorted(vm_announce.audit(), key=lambda r: r["op"])]


def c_vm_announce(rows, e):
    import dialog_disasm as D
    silent = [op for op in range(153)
              if op not in {r["op"] for r in rows}]
    return [("announcing handlers", len(rows), 49),
            ("...where the assembly and the field map agree",
             sum(1 for r in rows if r["agrees"]), len(rows)),
            ("opcodes that announce nothing", len(silent), 104),
            ("scx.play (57) is silent", 57 in silent, True),
            ("music.play (103) is silent", 103 in silent, True),
            ("scx.play.actor.wait (60) announces",
             next((r["domain"] for r in rows if r["op"] == 60), None),
             "CHARACTERS"),
            ("scene.load (71) announces its SECOND field",
             next((r["field"] for r in rows if r["op"] == 71), None), 1)]


#: The ANSWER global. `ui.open` suspends a script at status 6; `UI_LoadScreen`
#: sets this to -1, a widget callback writes the chosen value, and
#: `Ui_CloseScreenDefault` -> `sub_466B60` hands it to `Game_HandleEvent`
#: case 5, which stores it in the variable the `ui.open` site named
#: (docs/SCRIPT_VM.md 70).
UI_ANSWER = 0x00930750

#: The terminal family's shared activate function. Seven screens share panel
#: 0x4E4108 and this one callback; it switches on the screen instance's `+4` -
#: the fixed parameter from the screen table - through a jump table, exactly
#: the way `Ui_OpenShop` does. A LINEAR scan over its bytes attributes every
#: arm to one screen, which is the error the shop titles already taught.
TERM_DISPATCH = 0x004AF410
TERM_JUMPTAB = 0x004AF578
TERM_PANEL = 0x004E4108


def ui_answers(e):
    """Every write to the answer global, attributed to the code that makes it.

    Recovered from the IMAGE rather than from `readable/`, because it has to
    be: of the 17 writes, only 3 sit inside a function IDA gave a `proc` label
    (the `-1` reset and the LIFT's two). The rest are in unlabelled regions
    following an `endp` - the widget callbacks are reached only as dwords in
    the widget tree, never by a direct call, which is what leaves them out of
    every decompilation (CLAUDE.md section 1).
    """
    G = struct.pack("<I", UI_ANSWER)
    REGS = {0x0D: "ecx", 0x15: "edx", 0x1D: "ebx",
            0x2D: "ebp", 0x35: "esi", 0x3D: "edi"}
    sites = []
    for sva, vsz, ptr, rsz in e.sec:
        blob = e.d[ptr:ptr + rsz]
        for i in range(len(blob) - 10):
            va = e.base + sva + i
            if blob[i] == 0xC7 and blob[i + 1] == 0x05 and blob[i + 2:i + 6] == G:
                sites.append({"at": va, "kind": "imm",
                              "value": struct.unpack_from("<i", blob, i + 6)[0]})
            elif blob[i] == 0xA3 and blob[i + 1:i + 5] == G:
                sites.append({"at": va, "kind": "eax", "value": None})
            elif blob[i] == 0x89 and blob[i + 1] in REGS and blob[i + 2:i + 6] == G:
                sites.append({"at": va, "kind": REGS[blob[i + 1]], "value": None})
    sites.sort(key=lambda s: s["at"])

    # The terminal family's jump table: seven targets, one per screen, and the
    # case index IS the screen's own parameter.
    jt = [struct.unpack("<I", e.read(TERM_JUMPTAB + 4 * k, 4))[0] for k in range(7)]
    return {"global": UI_ANSWER, "sites": sites,
            "termDispatch": TERM_DISPATCH, "termCases": jt}


def answer_screens(e, panels, sites, term):
    """Which screens each answer site can serve.

    A site belongs to the callback it sits in, and a callback is named by the
    widget tree - so the mapping is tree -> screen. Two things it has to get
    right, both learned by getting them wrong:

    * a CHILD panel records `screen: -1`, and the start menu's whole tree
      hangs off `0x004CF218`, whose parent is 0. Resolving it by the `parent`
      chain therefore fails and screen 29 - the one screen whose answer was
      already read - drops out. It is recovered the way the data actually
      states it: **screen 29's open callback references `0x004CF218`**, so a
      child panel is attributed to whichever screen's open mentions it.
    * `0x004AF410` serves SEVEN screens through a jump table, so its four
      sites are attributed to all seven rather than to whichever the byte
      order happens to reach first.
    """
    screens = U.screens()
    hooks = {}
    def mark(addr, ids):
        if addr:
            hooks.setdefault(addr, set()).update(i for i in ids if i >= 0)
    per_panel = {}
    for p in panels:
        per_panel.setdefault(p["panel"], set()).add(p["screen"])
    # a child panel's owner is the screen whose OPEN callback names it
    for pan, who in per_panel.items():
        if any(i >= 0 for i in who):
            continue
        tag = struct.pack("<I", pan)
        for sd in screens:
            if sd["cb"][0] and tag in e.read(sd["cb"][0], 800):
                who.add(sd["id"])
    for sd in screens:
        for a in sd["cb"]:
            mark(a, {sd["id"]})
    for p in panels:
        who = per_panel[p["panel"]]
        mark(p.get("hook"), who)
        for l in p["lists"]:
            mark(l.get("hook"), who)
            for it in l["items"]:
                mark(it.get("callback"), who)
    mark(term, per_panel.get(TERM_PANEL, set()))
    order = sorted(hooks)
    out = {}
    for st in sites:
        if st["value"] == -1:
            continue                      # UI_LoadScreen's reset, not an answer
        j = bisect.bisect_right(order, st["at"]) - 1
        out[str(st["at"])] = sorted(hooks[order[j]]) if j >= 0 else []
    return out


def t_ui_widgets(e):
    r"""The WIDGET TREE - panels, lists and items - lifted out of the image.

    `tables/ui.json` carries the three tables the interface is INDEXED by;
    this is the tree those tables point into, and it is the part a replica
    cannot read out of `gamedata/` at all.  A screen's open callback installs a
    panel, the panel names lists, a list names items, and every one of those
    records is `.data` in `Runtime 2.exe`.

    The record fields, all established in `docs/UI.md` 3b/3c:

        panel  +0   parent panel, for the BACK bit
               +16  the panel's own input hook, or 0
               +28  int16 list count
               +32  the list pointers, 4 bytes each
        list   +0   int16 item count
               +4   the list's input hook - 0 means `Ui_MoveSelection`
               +12  the item pointer array
               +16  flags A: bit 2 hides the list, 0x80000 pins the selection
        item   +28  int16 string index into the screen's own IAM text file
               +40  the confirm callback
               +48/+52/+56   the three flag banks

    **The open callback's broadcasts are carried too**, because the static
    records are not what the screen shows: the start menu centres its four
    buttons with one `I2D_SetFlagOnAllRows(list, 0x80000010, 1)` rather than
    storing the flag, so a reader that trusts the record alone gets the
    alignment wrong.  Both the raw banks and the broadcasts are here, so a
    consumer can apply them and a reader can see why.

    **The tree DOES link downward, through the item.**  `+0` on a panel is its
    parent, which is why a first version of this lift recorded "top panels
    only" as a limit - but `Ui_ConfirmSelection` takes an item's `+40`
    callback when there is one and otherwise descends into its `+44`, and 13
    items name a child panel there.  Following them transitively reaches 6
    panels, 5 of which no screen names directly: the start menu's confirm
    dialog among them.  A child is recorded with `screen: -1`.

    What still needs native code is a panel a CALLBACK installs rather than a
    `+44` - and the answers those callbacks write, which is why
    `tools/sim/ui.py` keeps an `ANSWER` map of the ones whose effect has been
    read.

    Nine of the 37 screens are absent on purpose: the sneak family branches on
    its `+4` parameter and offers three panels, SHOOT MECA names both shoot
    panels, and the rest install none that this recovery can see.  Recording
    28 with a reason beats recording 37 with a guess.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools", "sim"))
    import ui as UI
    u = UI.Ui(e)

    def opt_pages(_u):
        """Per page: {row: [option item, page]} plus the rows a BRANCH binds."""
        o = UI.OptionsUi(e)
        out = []
        for k in sorted(o.bind):
            # A binding's page field is an ADDRESS; resolve it to the page's
            # INDEX here so a consumer never has to know the base or stride.
            # 0 means "no page behind this row", which is what makes a header
            # unselectable.
            bind = {}
            for r, (item, va) in sorted(o.bind[k].items()):
                bind[str(r)] = [item, o._page_index(va) if va else -1]
            out.append({"page": k, "bind": bind,
                        "branch": sorted(o.branch.get(k, set()))})
        return out

    def read_panel(panel, screen, blists, bitems, binds=None, bpage=None,
                   blayout=None, bcur=None, bsel=None):
        rec = {"screen": screen, "panel": panel,
               "parent": u._u32(panel), "hook": u._u32(panel + 16),
               # `Ui_ItemScreenX/Y` (0x004297C0 / 0x004297E0) are four lines
               # each: an item's own `+0`/`+2` plus the PANEL's current offset
               # at `+84`/`+86`. So a replica cannot place a single row without
               # these, and the widget lift did not carry them until
               # 2026-09-01 - it modelled the walk, which needs no geometry.
               "offset": [u._i16(panel + 84), u._i16(panel + 86)],
               "slide": [u._i16(panel + 88), u._i16(panel + 90),
                         u._i16(panel + 92), u._i16(panel + 94),
                         u._u32(panel + 96)],
               # `Ui_DrawPanelBack` (0x00476040): without flag 0x40004000 the
               # background is 80 tile ids, a 10-wide by 8-deep grid of 64x64
               # cells picked out of the screen's own 640x480 sheet as
               # (id % 10, id / 10).
               #
               # `panel + 20` is a POINTER to those bytes, not the bytes -
               # `v6 = u32(a2, 20)` then `i8(v6, cell)` - and they are read
               # SIGNED. Reading the array in place gave ids of 240, which
               # cannot index a 10x8 grid and is what showed the mistake.
               "tilesAt": u._u32(panel + 20),
               "tiles": ([u._i8(u._u32(panel + 20) + k) for k in range(80)]
                         if u._u32(panel + 20) else []),
               # `panel+24`, the CURRENT LIST - but only when the screen's
               # own open callback WROTE it. It is runtime state, so the disk
               # image says nothing and a reader must fall back on
               # `Ui_MoveBetweenLists`'s rule; `Ui_OpenSneakFamily` is the one
               # callback that sets it, and it is what decides which page of
               # the sneak device comes up (`sim/ui.py: open_state`). -1 means
               # "not written", not "list -1".
               "current": (bcur or {}).get(panel, -1),
               # `panel+72`, the panel's own flag word. `sub_42A5C0` reads
               # only its 0x80000 - the same NOWRAP bit a list uses at `+16`,
               # deciding whether moving off the last list wraps to the first.
               # Every sneak page ships 0x20000030, so they all wrap; lifted
               # rather than assumed, because "they all wrap" is a fact about
               # the shipped data and not about the format.
               "flags": u._u32(panel + 72),
               "lists": []}
        for lst in u.lists(panel):
            l = {"addr": lst, "hook": u._u32(lst + 4),
                 "flags": u._u32(lst + 16),
                 # `list+20`, bank B - and it is the DRAW gate, which is a
                 # DIFFERENT flag from the walk's. The panel drawer skips a
                 # list on `0x40000001` (`sub_429080(list, 1073741825)`),
                 # while `Ui_MoveBetweenLists` skips it on `+16 & 4`. A list
                 # can be one and not the other: the sneak's bottom bar ships
                 # `+16 = 0x20000004` and `+20 = 0`, so it is DRAWN and not
                 # navigable, which is exactly what a status bar should be.
                 # Not lifted until 2026-09-04, so the composer had only the
                 # walk's flag and drew the wrong set of lists.
                 "flagsB": u._u32(lst + 20),
                 # `list+2`, the SELECTED ROW, on the same terms as `current`
                 # above: the open callback's write, or -1 for none.
                 "select": (bsel or {}).get(lst, -1),
                 "broadcast": blists.get(lst, []),
                 # ...and the LIST's own flag words at +16/+20, which
                 # `I2D_SetListFlag` (0x004290D0) writes. A third helper with
                 # the same call shape as the other two, and it was missing
                 # from the scan until 2026-09-01 - 41 edits over eleven
                 # screens simply were not in this table.
                 "listflag": (bpage or {}).get(lst, []),
                 # The open callback's LAYOUT pass. Without it the
                 # static x/y/h are not what the screen shows: the start
                 # menu is laid out to y 120 step 80 where the records
                 # say 150 step 60, and its confirm dialog's two buttons
                 # both ship at y=330 and are separated to 260 and 320.
                 "layout": (blayout or {}).get(lst, {}),
                 "items": []}
            for it in u.items(lst):
                l["items"].append({
                    "addr": it, "string": u._i16(it + 28),
                    # the item's own coordinate, which `Ui_ItemScreenX/Y` add
                    # the panel offset to
                    "x": u._i16(it), "y": u._i16(it + 2),
                    "w": u._i16(it + 4), "h": u._i16(it + 6),
                    # WHAT MAKES AN ITEM SHOW TEXT, and it is neither of
                    # the two fields this table used to carry. `Ui_DrawItem`
                    # (0x004764A0) never reads `+28`: it takes `+24` as a
                    # resolved `char *` and, when that is null, calls `+32` to
                    # fill the buffer. An item with both zero draws NO TEXT AT
                    # ALL, whatever `+28` says - which is what the sneak's six
                    # tab icons and its three 50x50 buttons are, and why a
                    # composer keyed on `+28` printed five labels the game
                    # never shows.
                    #
                    # The callbacks seen on screen 9:
                    #   0x00476860  the generic one - draws string `+28`, with
                    #               `+30` as a printf argument when it is not -1
                    #   0x0042AA00  the inventory row: reads the item's `+60`
                    #               tag and asks `Game_RaiseEvent(33)` for a name
                    #   0x0049DC20  the sneak's echo bar
                    #   0x0049E090  the sneak's clock row
                    # `+8/+9/+10` the fill/outline COLOUR and `+11` its
                    # layer, packed by `Ui_DrawItemFill` as
                    # `(alpha << 24) | (+8 << 16) | (+9 << 8) | +10`.
                    "rgb": [u._u8(it + 8), u._u8(it + 9), u._u8(it + 10)],
                    "layer": u._u8(it + 11),
                    # `+36` IS A FONT, not a character - it goes into
                    # `Text_DrawBlock`'s `params[2]`, whose global default is
                    # **74** and steps to 76 below 640x480. The ids are ASCII
                    # because they are meant to be typed: 74 = 'J' JOURNAL,
                    # 83 = 'S' SNEAK, 73 = 'I' MENUINTR, 67 = 'C'. 255 is
                    # "unset" and takes the default.
                    #
                    # `docs/UI.md` identified this field and it was never
                    # lifted, so every screen drew in whatever face the
                    # composer picked. It shows on the SNEAK first because
                    # the sneak is the screen with a font of its own.
                    "font": u._u8(it + 36),
                    "text": u._u32(it + 24),
                    "textFn": u._u32(it + 32),
                    # `+30`, the format argument the generic callback passes to
                    # `sub_43FEA0` when it is not -1.
                    "textArg": u._i16(it + 30),
                    # The SPRITE source rects, `Ui_DrawItemSprite`
                    # (0x00476E60): `+12/+14` is the LIT top-left and
                    # `+16/+18` the UNLIT one, each `w x h` from the item's own
                    # size, blitted out of the screen's artwork. `ui sprites`
                    # has read these out of the image since 2026-09-01; they
                    # were never in the table, so nothing could DRAW one.
                    "lit": [u._i16(it + 12), u._i16(it + 14)],
                    "unlit": [u._i16(it + 16), u._i16(it + 18)],
                    "callback": u._u32(it + 40),
                    # `+44` is the CHILD panel. `Ui_ConfirmSelection` takes the
                    # `+40` callback when there is one and otherwise descends
                    # into `+44`, so the tree does link downward after all -
                    # 13 items name one, reaching 6 panels of which 5 are not
                    # a screen's top panel. The start menu's confirm dialog
                    # (0x004CF280, panel hook 0x0047A230) is one of them.
                    "child": u._u32(it + 44),
                    "flags": [u._u32(it + 48), u._u32(it + 52), u._u32(it + 56)],
                    "setflag": bitems.get(it, []),
                    # what the screen's OPEN callback writes into this item:
                    # its string id at +28 and its tag at +60. Both ship as
                    # -1/0 in the record, so without these the widget walk
                    # finds a screen with no labels.
                    "bind": (binds or {}).get(it, {}),
                })
            rec["lists"].append(l)
        return rec

    # PANELS NAMED FROM CODE, not from an item's `+44`.
    #
    # The walk below follows `+44`, which is how `Ui_ConfirmSelection`
    # descends - and it is not the only way a panel is entered.
    # `sub_42A370(screen, panel)` installs one directly, and two of the
    # sneak device's panels are reached only that way, so nothing in the
    # tree pointed at them and the lift had never seen either:
    #
    #   0x004DEEB8  the VERB panel. `loc_49BE7B`, the plain arm of the
    #               inventory row's confirm callback, is
    #               `sub_42A370(screen, off_4DEEB8)`. Its builder
    #               0x0049B810 clears 0x20000004 on the verb list and SETS
    #               it on the tabs, the previews and the rows - so while a
    #               verb is being chosen nothing else can be reached, which
    #               is why the verbs are locked until a row is confirmed.
    #
    #   0x004DEF20  the EXAMINE page. `sub_49BFF0` ("Examiner") takes the
    #               selected row's tag, calls `sub_42B420(tag, 4)` and then
    #               `sub_42A370(screen, off_4DEF20)`. Its own list
    #               0x004DE760 is where a capture of the original shows the
    #               3D model and the object's name.
    #
    # Both parse as panels at the family's 0x68 stride and both carry the
    # right `+0` parent, which is the check on this table: it names an
    # address, and the record there has to agree.
    CODE_NAMED = {0x004DEE50: [0x004DEEB8],
                  0x004DEEB8: [0x004DEF20]}
    out, skipped, seen = [], [], set()   # `seen` tracks CHILD panels only
    for sid in sorted(u.screens):
        try:
            panel = u.panel_of(sid)
        except (ValueError, KeyError) as err:
            # KeyError is the screen whose open callback is 0 - five of the 37
            # rows name no callback at all (docs/UI.md 3d), and that is a fact
            # about the table rather than a failure to recover one.
            skipped.append({"screen": sid, "why": str(err)})
            continue
        blists, bitems, bpage, blayout = u.open_flags(sid)
        # The bindings need the item addresses first, so the panel is walked
        # once for those and then again with them in hand. Resolving the
        # callback's stores against REAL item records is what separates a
        # binding from the many other globals a linear scan trips over.
        addrs = sorted({it for l in u.lists(panel) for it in u.items(l)})
        stack0 = list(addrs)
        while stack0:
            a = stack0.pop(0)
            kid = u._u32(a + 44)
            if kid:
                more = sorted({i for l in u.lists(kid) for i in u.items(l)})
                for m in more:
                    if m not in addrs:
                        addrs.append(m); stack0.append(m)
        binds = u.open_binds(sid, sorted(addrs))
        # ...and the one binding that scan gets WRONG. `Ui_OpenShop` picks the
        # title out of a jump table on the screen's `+8` parameter, which a
        # linear walk runs straight through - leaving all ten shops bound to
        # the last arm's string. Follow the table and override.
        shops = u.shop_titles()
        par = u.screens[sid].get("param", -1)
        if par in shops:
            item = u.SHOP_TITLE_FIELD - 28
            binds.setdefault(item, {})["string"] = shops[par]
        # ...and the panel's CURRENT LIST and each list's SELECTED ROW, which
        # need the panel and list addresses in hand for the same reason the
        # bindings need the item ones: a store only counts when it lands on a
        # real record at the right offset. The reachable set is the screen's
        # own panel plus every panel an item's `+44` descends into.
        reach, stack1 = {panel}, [panel]
        while stack1:
            pn = stack1.pop(0)
            for l in u.lists(pn):
                for it in u.items(l):
                    kid = u._u32(it + 44)
                    if kid and kid not in reach:
                        reach.add(kid); stack1.append(kid)
            for kid in CODE_NAMED.get(pn, ()):
                if kid not in reach:
                    reach.add(kid); stack1.append(kid)
        allLists = {l for pn in reach for l in u.lists(pn)}
        bcur, bsel = u.open_state(sid, reach, allLists)
        # the screen's own panel, then every panel reachable through `+44`,
        # transitively. A child carries `screen: -1` - it belongs to whichever
        # panel descended into it, and the open callback's flag edits were
        # collected for the screen as a whole.
        # A screen ALWAYS gets its own record, even when the panel address is
        # one another screen already named: the 28 screens share only 13
        # distinct top panels, and de-duplicating them would leave 15 screens
        # unable to look themselves up.
        rec = read_panel(panel, sid, blists, bitems, binds, bpage, blayout,
                         bcur, bsel)
        out.append(rec)
        stack = [it["child"] for l in rec["lists"] for it in l["items"]
                 if it["child"]] + list(CODE_NAMED.get(panel, ()))
        # A CHILD, by contrast, is recorded once: it belongs to whichever
        # panel descended into it and carries `screen: -1`.
        while stack:
            addr = stack.pop(0)
            if addr in seen:
                continue
            seen.add(addr)
            kid = read_panel(addr, -1, blists, bitems, binds, bpage, blayout,
                             bcur, bsel)
            out.append(kid)
            for l in kid["lists"]:
                for it in l["items"]:
                    if it["child"] and it["child"] not in seen:
                        stack.append(it["child"])
            for nxt in CODE_NAMED.get(addr, ()):
                if nxt not in seen:
                    stack.append(nxt)
    # The name field's character switch, read from the compiled JUMP TABLE
    # rather than transcribed - a wrong reading shows up as a wrong label.
    # `sub_47A390` handles codes 8..27: backspace deletes, TAB and ESC are
    # ignored, RETURN moves focus (but only on a non-empty buffer), anything
    # else inserts. The 20-character cap is the buffer itself, 0x0069BDA0 to
    # 0x0069BDB4, and the save slot that receives the name has room for 32 -
    # so the field is the tighter of the two.
    return {"panels": out, "unresolved": skipped,
            "gridHook": UI.GRID_HOOK, "nameHook": UI.NAME_HOOK,
            # `sub_42A710` - `Ui_MoveBetweenLists` bound to LEFT and
            # RIGHT. A PANEL hook rather than a list one, and the sneak
            # device's pages are the only records that name it.
            "moveListsHook": UI.MOVE_LISTS_HOOK,
            "nameSwitch": {str(k): v for k, v in UI.name_switch(e).items()},
            "nameMax": UI.NAME_MAX,
            "startConfirmHook": 0x0047A230,
            "startNameList": 0x004CE890, "startButtonList": 0x004CE948,
            # `Confirmer` writes `mov dword_930750, 1` at 0x0047A354, and 1 is
            # what the shipped save records for the intro's `Interface`
            # variable - so this is the one item callback whose effect on the
            # answer has been READ. It is GATED: the callback's first
            # instruction tests the name cursor and jumps straight to the ret
            # when it is zero, writing neither the answer nor the screen's
            # state word, so an empty field leaves the script suspended for
            # ever.
            "answers": [{"callback": 0x0047A2B0, "value": 1,
                         "needsName": True}],
            # ...and every write to the answer global, found mechanically.
            # The hand-read entry above stays because it carries the GATE,
            # which is a branch and not a store.
            "answerSites": dict(ui_answers(e),
                                screens=answer_screens(
                                    e, out, ui_answers(e)["sites"],
                                    TERM_DISPATCH)),
            # ------------------------------------------------ the OPTIONS tree
            #
            # Screen 35 is not like the others. Its "panel" is one of THIRTEEN
            # page records and every page fills the SAME sixteen row widgets -
            # so what a page shows is not in the row, it is in the calls its
            # builder makes to `Opt_BindRow(row, item, page)`. Those are
            # recovered from the builder's bytes, the same way the open
            # callbacks' flag edits are, and with the same hazard: a builder
            # has BRANCHES, so a row bound twice with different items was seen
            # on two arms and the scan cannot tell which one runs. Such rows
            # are listed in `branch` and a walker must treat them as unknown
            # rather than take the last one.
            # `bind` maps a row to (option item, the page INDEX behind it or
            # -1) - the address is resolved here so no consumer needs the
            # table's base or stride.
            "optionPages": opt_pages(u),
            "optionRows": UI.OPT_ROWS[2],
            # ---------------------------------------------- the LOAD panel
            #
            # `sub_47A6D0` builds the start menu's "Charger une partie" panel
            # and takes one of two branches on the save directory, which is
            # the whole of what its shape depends on:
            #
            #   profiles > 0    focus the slot list, show it, leave "Charger"
            #                   and "Detruire" selectable
            #   profiles == 0   focus the BUTTONS, hide the slot list, and make
            #                   both unselectable (`word_4CEA9A = 3`)
            #
            # "Nouvelle partie" is hidden either way on screen 29: it belongs
            # to screen 30, the SAVE panel, which shares this panel and is told
            # apart by `word_4CEA9A` - 0 here, 1 there.
            "loadPanel": {"panel": UI.LOAD_PANEL, "slotList": UI.LOAD_SLOTLIST,
                          "charger": UI.LOAD_CHARGER,
                          "nouvelle": UI.LOAD_NOUVELLE,
                          "detruire": UI.LOAD_DETRUIRE,
                          "buttonList": u._u32(UI.LOAD_PANEL + 36)},
            # `IAM\GAMES`: 3496 + 256 * 32808, and a 72-byte directory entry
            # per slot lifted from the slot head.
            "savesHeader": UI.GAMES_HEADER, "saveSlot": UI.GAMES_SLOT,
            "saveSlots": UI.GAMES_SLOTS}


def c_ui_widgets(rows, e):
    ps = rows["panels"]
    tops = [p for p in ps if p["screen"] >= 0]
    kids = [p for p in ps if p["screen"] < 0]
    lists = [l for p in ps for l in p["lists"]]
    items = [i for l in lists for i in l["items"]]
    hooks = [l["hook"] for l in lists if l["hook"]]
    def mapped(va):
        try: e.read(va, 4); return True
        except Exception: return False
    # THE SNEAK FAMILY, added 2026-09-04. Screens 0, 7 and 9 (VIDEOPHONE,
    # SLIDER, SNEAK) were three of the nine this lift skipped, because
    # `Ui_OpenSneakFamily` writes a different panel on each arm of its `+4`
    # branch and `panel_of` could not say which was whose. Following the
    # branch (`sim/ui.py: SNEAK_ARM`) adds all three - and with them the
    # sneak DEVICE, whose tab column descends into five sibling pages, so
    # most of the counts below move by more than three.
    return [("screens with a panel", len(tops), 31),
            ("screens without one", len(rows["unresolved"]), 6),
            # 31 screens share only 16 distinct top panels, so a record per
            # screen is not a record per address - and de-duplicating by
            # address would leave 15 screens unable to look themselves up.
            ("distinct top panel addresses",
             len(set(p["panel"] for p in tops)), 16),
            # 7 -> 13: the sneak device's tab column names five pages the
            # family's three screens do not open on (Identite, Memoire,
            # Options, Quitter, and the videophone's own), and one of them
            # descends further. 13 -> 15 on 2026-09-04: two more that no
            # item's `+44` names at all - `CODE_NAMED` above - the VERB
            # panel 0x004DEEB8 and the EXAMINE page 0x004DEF20, both
            # installed by `sub_42A370` from a callback.
            ("child panels", len(kids), 15),
            ("lists", len(lists), 134),
            ("items", len(items), 611),
            ("item records inside the image",
             sum(1 for i in items if mapped(i["addr"])), len(items)),
            # 75 across the whole tree but only 16 distinct item RECORDS
            # (9 before the sneak family, and its tab column adds seven):
            # 0x004DE210 is one list carried by nine of the panels, so each
            # of its child-naming items is counted once per panel.
            ("items naming a child panel",
             sum(1 for i in items if i["child"]), 88),
            ("...of which distinct item records",
             len({i["addr"] for i in items if i["child"]}), 17),
            ("lists with a non-default input hook", len(hooks), 52),
            # The two RUNTIME fields, and only where the open callback writes
            # them. Neither was in this table before 2026-09-04, because the
            # scan had no reason to look: `panel+24` is the CURRENT LIST and
            # `list+2` the SELECTED ROW, both runtime state that the disk
            # image says nothing about - so the walk fell back on
            # `Ui_MoveBetweenLists`'s rule for every screen. The sneak family
            # is what showed they are recoverable, and going looking found
            # them on eleven more: `Ui_OpenShop`'s first instruction is
            # `mov dword_4E3988, 0`, which is the ten shops' shared panel's
            # `+24`, and FIGHT SIM, SAVE GAME, PAUSE GAME and SHOOT HUMAN
            # each set one too.
            #
            # Counted as RECORDS, not addresses: 28 screens share 16 top
            # panels and one list can be carried by several, so the same
            # write is recorded once per panel that has it (8 distinct list
            # addresses behind the 27).
            ("panels whose open callback sets the current list",
             sum(1 for p in ps if p.get("current", -1) >= 0), 15),
            ("lists whose open callback sets the selection",
             sum(1 for l in lists if l.get("select", -1) >= 0), 27),
            ("...distinct list records among them",
             len({l["addr"] for l in lists if l.get("select", -1) >= 0}), 8),
            # SNEAK opens on its INVENTORY page with the tab column already on
            # "Inventaire" - list 3 of panel 0x004DEE50, row 2 of 0x004DE210.
            ("SNEAK's current list and tab row",
             [[p["current"] for p in ps if p["screen"] == 9],
              [l["select"] for p in ps if p["screen"] == 9
               for l in p["lists"] if l["addr"] == 0x004DE210]],
             [[3], [2]]),
            # what the OPEN callbacks bind into the items - the string id at
            # +28 and the tag at +60, neither of which is in the record: the
            # items ship with -1 and the callback writes them, so a reader
            # that trusts the record alone finds a screen with no labels.
            # TERMINAL is the shape: strings 5..10 on six rows, tags 1..4.
            ("items with a bound string",
             sum(1 for i in items if "string" in (i.get("bind") or {})), 35),
            ("items with a bound tag",
             sum(1 for i in items if "tag" in (i.get("bind") or {})), 22),
            ("TERMINAL's bound strings",
             sorted(i["bind"]["string"] for p in ps if p["screen"] == 5
                    for l in p["lists"] for i in l["items"]
                    if "string" in (i.get("bind") or {})),
             [5, 6, 7, 8, 9, 10]),
            ("distinct hooks among them", len(set(hooks)), 13),
            ("lists taking Ui_MoveSelection, the default walk",
             sum(1 for l in lists if not l["hook"]), 82),
            ("the LIFT grid hook is present", rows["gridHook"] in hooks, True),
            # It is here only because the walk follows `+44`: the name field
            # is in the start menu's confirm dialog, a CHILD panel. A lift
            # that stopped at the top panels could not see it, and the first
            # version of this one recorded that as a limit of the format.
            ("the name-field hook is present, via a child panel",
             rows["nameHook"] in hooks, True),
            ("name-field switch entries", len(rows["nameSwitch"]), 20),
            ("...of which RETURN, BACKSPACE and two ignores",
             sorted(set(rows["nameSwitch"].values())),
             ["backspace", "ignore", "insert", "return"]),
            ("the name buffer's cap", rows["nameMax"], 20),
            ("option pages", len(rows["optionPages"]), 13),
            ("rows bound across all thirteen",
             sum(len(p["bind"]) for p in rows["optionPages"]), 191),
            # 12, and `tools/sim/ui.py` documents exactly ONE of them
            # (`Opt_PageRoot`'s row 4, bound to "Retour" only when the screen
            # parameter is 1). The other eleven are detected the same way and
            # have not been read, which is why a walker must treat the whole
            # set as unknown rather than trust the last binding.
            ("rows a BRANCH binds - the scan cannot resolve these",
             sum(len(p["branch"]) for p in rows["optionPages"]), 12),
            ("the load panel's button list is inside the image",
             mapped(rows["loadPanel"]["buttonList"]), True),
            ("the save file's geometry",
             rows["savesHeader"] + rows["saveSlots"] * rows["saveSlot"],
             8402344)]



def t_shoot_ai(e):
    r"""The shoot AI's compiled tables at 0x004CFA30 - which the docs said did
    not exist.

    `docs/ASSETS.md` recorded the shoot AI as "four functions, no data at all"
    and `CLAUDE.md` §4 as "No data table".  That is right about the DISPATCH -
    `Shoot_ActorEnter` switches on the character type in a `switch`, not a
    table - and wrong about the AI, because **Gandhar plays three compiled
    behaviour scripts through two twelve-entry handler tables**, and the
    character types themselves are a name table the binary carries.  The
    negative result was about the dispatch and got generalised to the
    subsystem; this is the same shape CLAUDE.md §1 warns about.

    Five tables, and they **chain end to end**, which is the check a wrong
    base address could not survive:

        0x004CFA30  character types      14 pointers to their own names
        0x004CFA70  behaviour: healthy   14 {action, repeats}, ends 0x004CFAE0
        0x004CFAE0  behaviour: wounded   10 entries,           ends 0x004CFB30
        0x004CFB30  behaviour: critical  13 entries,           ends 0x004CFB98
        0x004CFB98  action ENTER handlers  12 pointers,        ends 0x004CFBC8
        0x004CFBC8  action TICK  handlers  12 pointers,        ends 0x004CFBF8
        0x004CFBF8  'No one' - the first type name

    Every one of those end addresses is the next table's start, and the last
    lands on the string the first table points at.

    **The behaviour script.**  `sub_47FB40` walks it: play entry `[+144]`
    `repeats` times (counter at `+100`), then step to the next; a `{0, ...}`
    entry rewinds to the start.  Which script is playing is chosen by health
    (`+92`) every frame - **> 100 healthy, <= 100 wounded, <= 50 critical** -
    and changing band resets both the index and the counter, so a wounded
    Gandhar restarts his routine rather than resuming it.  The three read as
    one routine getting more repetitive as he is hurt: they share the same
    five-action tail (22, 18, 16x2, 17, 19) and differ only in the head, where
    healthy varies its actions and critical repeats 24 and 23 five and three
    times over.

    **The actions.**  An action code 16..27 selects a row through a `switch`
    permutation, not by subtraction, and each row is an ENTER handler that
    sets `+156`, sets or clears channel flag `0x800` and picks an animation by
    TYPE from the character's own list, plus a TICK handler run while the
    action lasts.  `clip_type` is the second argument of
    `List_PickRandomByType` - read with a nesting-aware parse, because the
    naive `[^,]+` pattern CLAUDE.md warns about reads
    `List_PickRandomByType(u32(a1, 20), 11)` as type 20.

    Action 16 is the only one that takes no animation: it waits
    `(rand() & 0x1F) + 30` frames - 30 to 61 at 30 Hz, one to two seconds.

    **What is deliberately NOT here**: the mask at record `+84`
    (0x004C3798, and 0x004C37E8 for Astaroth).  It looked like AI and is not -
    `sub_434C30` hands it straight to the poser `sub_471950`, so it is an
    animation node mask.  Located, not lifted: nothing in this tree poses a
    skeleton, and lifting it would be the second copy the plan warns about.
    """
    TYPES, HEALTHY, WOUNDED, CRITICAL = 0x004CFA30, 0x004CFA70, 0x004CFAE0, 0x004CFB30
    ENTER, TICK = 0x004CFB98, 0x004CFBC8

    types = []
    for i in range(14):
        p, = struct.unpack("<I", e.read(TYPES + 4 * i, 4))
        types.append({"value": i, "name": e.cstr(p)})

    def script(addr):
        out, i = [], 0
        while True:
            a, n = struct.unpack("<ii", e.read(addr + 8 * i, 8))
            if a == 0:
                out.append({"action": 0, "repeats": n, "end": True})
                return out, addr + 8 * (i + 1)
            out.append({"action": a, "repeats": n})
            i += 1
            if i > 64:
                return out, addr + 8 * i

    scripts = []
    for nm, addr, band in (("healthy", HEALTHY, "hp > 100"),
                           ("wounded", WOUNDED, "hp <= 100"),
                           ("critical", CRITICAL, "hp <= 50")):
        rows, end = script(addr)
        scripts.append({"name": nm, "band": band, "at": "0x%08X" % addr,
                        "ends_at": "0x%08X" % end, "entries": rows})

    # the code -> row permutation, transcribed from Gandhar's own switch. It is
    # NOT code-16, which is what makes it worth carrying as data.
    PERM = {16: 11, 17: 0, 18: 1, 19: 2, 20: 5, 21: 6,
            22: 9, 23: 3, 24: 4, 25: 7, 26: 8, 27: 10}
    # per row: the clip TYPE the enter handler asks for, whether it sets
    # channel flag 0x800, and a note. Read from the twelve enter handlers at
    # 0x0047E230..0x0047E4F0.
    ENTERS = {
        16: (None, False, "wait (rand() & 0x1F) + 30 frames; no animation"),
        17: (None, True,  "no animation of its own"),
        18: (None, True,  "no animation of its own"),
        19: (17,   True,  ""),
        20: (11,   False, ""),
        21: (18,   True,  ""),
        22: (18,   True,  ""),
        23: (19,   True,  "also stores the clip length in +164"),
        24: (11,   False, "also stores the clip length in +164"),
        25: (1,    True,  "picked by sub_434630, not List_PickRandomByType"),
        26: (9,    True,  "picked by sub_434630"),
        27: (6,    True,  "picked by sub_434630"),
    }
    actions = []
    for code in sorted(PERM):
        row = PERM[code]
        clip, flag, note = ENTERS[code]
        en, = struct.unpack("<I", e.read(ENTER + 4 * row, 4))
        tk, = struct.unpack("<I", e.read(TICK + 4 * row, 4))
        actions.append({"code": code, "row": row,
                        "enter": "0x%08X" % en, "tick": "0x%08X" % tk,
                        "clip_type": clip, "sets_flag_0x800": flag,
                        "note": note})

    # Shoot_ActorEnter's switch: the type picks the per-frame callback.
    dispatch = [
        {"type": 7,  "name": "X-Tech",   "callback": "0x0045B6C0",
         "label": "nullsub_9", "note": "does nothing - an inert target"},
        {"type": 10, "name": "Gandhar",  "callback": "0x0047F6F0",
         "label": "sub_47F6F0", "note": "the behaviour-script machine below"},
        {"type": 13, "name": "Astaroth", "callback": "0x004800C0",
         "label": "sub_4800C0", "note": "a hand-written state machine on +156"},
        {"type": None, "name": "(default)", "callback": "0x00424DE0",
         "label": "sub_424DE0", "note": "the generic shooter; 16 states"},
    ]
    return {"character_types": types, "dispatch": dispatch,
            "actions": actions, "behaviour_scripts": scripts}


def c_shoot_ai(rows, e):
    """The chain is the check: every table ends where the next begins."""
    ends = [s["ends_at"] for s in rows["behaviour_scripts"]]
    codes = sorted(a["code"] for a in rows["actions"])
    perm = sorted(a["row"] for a in rows["actions"])
    names = [t["name"] for t in rows["character_types"]]
    return [
        ("character types", len(names), 14),
        ("type 7 / 10 / 13 names", [names[7], names[10], names[13]],
         ["X-Tech", "Gandhar", "Astaroth"]),
        ("behaviour scripts", len(rows["behaviour_scripts"]), 3),
        ("their entry counts",
         [len(s["entries"]) for s in rows["behaviour_scripts"]], [14, 10, 13]),
        ("each ends where the next begins", ends,
         ["0x004CFAE0", "0x004CFB30", "0x004CFB98"]),
        ("action rows", len(rows["actions"]), 12),
        ("their codes are 16..27", codes, list(range(16, 28))),
        ("the code->row map is a bijection onto 0..11", perm, list(range(12))),
        ("every action a script names is a real row",
         sorted({en["action"] for s in rows["behaviour_scripts"]
                 for en in s["entries"] if not en.get("end")} - set(codes)), []),
        ("dispatch arms", len(rows["dispatch"]), 4),
    ]


_TABLES = [
    ("vm_opcodes",     t_vm_opcodes,     c_vm_opcodes,     "SCRIPT_VM"),
    ("special_moves",  t_special_moves,  c_special_moves,  "ASSETS"),
    ("camera_presets", t_camera_presets, c_camera_presets, "ASSETS"),
    ("adpcm",          t_adpcm,          c_adpcm,          "ASSETS"),
    ("key_bindings",   t_key_bindings,   c_key_bindings,   "ASSETS"),
    ("ui",             t_ui,             c_ui,             "UI"),
    ("ui_widgets",     t_ui_widgets,     c_ui_widgets,     "UI"),
    ("vm_announce",    t_vm_announce,    c_vm_announce,    "SCRIPT_VM"),
    ("shoot_ai",       t_shoot_ai,       c_shoot_ai,       "ASSETS"),
]


# ---------------------------------------------------------------- the driver

def build():
    """-> {name: {"table": ..., "checks": [...]}} for every table."""
    e = Exe()
    out = {}
    for name, take, check, doc in _TABLES:
        rows = take(e)
        out[name] = {"table": rows, "checks": check(rows, e), "doc": doc,
                     "about": (take.__doc__ or "").strip()}
    return out


def payload(name, built):
    """What actually lands on disk: the data, its provenance and its checks."""
    b = built[name]
    return {
        "_": "Lifted from gamedata/Runtime 2.exe by tools/exetables.py - do not "
             "hand-edit; run the tool. These tables are compiled into the "
             "executable and are NOT in the shipped gamedata/ data, which is why a "
             "replica engine needs this file.",
        "table": name,
        "documented_in": "docs/%s.md" % b["doc"],
        "about": b["about"],
        "checks": [{"what": w, "got": g, "want": v} for w, g, v in b["checks"]],
        "rows": b["table"],
    }


def write():
    built = build()
    os.makedirs(OUT, exist_ok=True)
    bad = 0
    for name, _, _, _ in _TABLES:
        p = payload(name, built)
        for c in p["checks"]:
            if c["got"] != c["want"]:
                print("  FAIL %s: %s = %r, want %r"
                      % (name, c["what"], c["got"], c["want"]))
                bad += 1
        path = os.path.join(OUT, name + ".json")
        open(path, "w").write(json.dumps(p, indent=1, ensure_ascii=False) + "\n")
        n = len(p["rows"]) if isinstance(p["rows"], list) else len(p["rows"])
        print("  %-16s %5d %s  ->  tables/%s.json"
              % (name, n, "rows" if isinstance(p["rows"], list) else "keys", name))
    return bad


def check():
    """Re-derive and diff against what is on disk. -> list of complaints."""
    built = build()
    out = []
    for name, _, _, _ in _TABLES:
        path = os.path.join(OUT, name + ".json")
        want = payload(name, built)
        for c in want["checks"]:
            if c["got"] != c["want"]:
                out.append("%s: %s = %r, want %r"
                           % (name, c["what"], c["got"], c["want"]))
        if not os.path.exists(path):
            out.append("tables/%s.json is missing" % name); continue
        have = json.load(open(path))
        # compare through a JSON round-trip: several of these tables carry
        # tuples, which come back from the file as lists and would otherwise
        # read as "stale" on every run
        if have.get("rows") != json.loads(json.dumps(want["rows"])):
            out.append("tables/%s.json is stale - re-run tools/exetables.py"
                       % name)
    return out


def main(argv):
    if "--check" in argv:
        bad = check()
        for b in bad: print("FAIL " + b)
        print("%d tables, %d complaints" % (len(_TABLES), len(bad)))
        return 1 if bad else 0
    if "--show" in argv:
        name = argv[argv.index("--show") + 1]
        b = build()
        print(json.dumps(payload(name, b), indent=1, ensure_ascii=False))
        return 0
    print("lifting the compiled tables out of gamedata/Runtime 2.exe:")
    bad = write()
    print("%d tables%s" % (len(_TABLES),
                           "" if not bad else ", %d CHECKS FAILED" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

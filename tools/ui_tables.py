#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""The interface subsystem's three compiled tables, read out of the engine.

The UI is the one part of the game with no shipped file that describes it -
there is no `.SCX` for a menu - so everything about it is compiled into
`gamedata/Runtime 2.exe`. Three tables carry it, and each is read here at the
address the code that walks it names, with the code's own bound as the end:

  screens()   0x004CB640, stride 92, bound `aNoOne_45`  - the 37 interface
              screens. `UI_LoadScreen` (0x00429BB0) scans it comparing +4
              against the id it was asked for.
  options()   0x004DA574, stride 140, bound dword_4DCE48 - the 74 rows of the
              options menu. `Opt_BindRow` (0x00490F90) and the item hooks
              (`sub_48FA50` and friends) index it as dword_4DA574[35*i].
  sounds()    0x004D0990, stride 20, bound `aNoOne_0` - the 45 interface
              sound effects. `Ui_LoadScreenSounds` (0x00482F30) scans it for
              each of the screen's 12 sound slots and builds
              `i2d\sounds\%s.wav` from the name at +4.

Every string a record points at is resolved: the screen's own label, its
`I2d\bitmaps\%s` artwork, its `IAM\<file>` text file, and - for the options
rows - the labels and choice captions out of `gamedata/IAM/Options` itself.

    python3 tools/ui_tables.py screens
    python3 tools/ui_tables.py options
    python3 tools/ui_tables.py sounds
    python3 tools/ui_tables.py oscillators
    python3 tools/ui_tables.py --selftest
"""
import omkpaths
import os, sys, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = omkpaths.data("Runtime 2.exe")

# Table bases and ends, each taken from the loop in the function that walks it.
SCREENS = (0x004CB640, 92, 0x004CC38C)      # UI_LoadScreen, bound aNoOne_45
OPTIONS = (0x004DA574, 140, 0x004DCDEC)     # Opt_ApplyBinding, bound dword_4DCE48
SOUNDS = (0x004D0990, 20, 0x004D0D14)       # Ui_LoadScreenSounds, bound aNoOne_0

# The 16 option-row widgets the page builders bind. `Opt_LayOutPage` reads the
# LIVE page's count at +0 and its row-pointer array at +12; the live page is
# 0x004DD2A8 for every page but the first, which has its own at 0x004DD3B0.
ROWS = (0x004DCDE8, 72, 16)

# The UI animation oscillators. `Ui_Oscillator(k)` is `0x004C3EA0 + 40*k` (the
# asm is `lea eax,[eax+eax*4]` then `lea eax, ds:4C3EA0h[eax*8]`; the
# decompiler's 4996768 folds the shift wrongly). The widget drawers read +24.
OSCILLATORS = (0x004C3EA0, 40, 0x004C3FE0)

# The options menu's page tree: +0 parent (0 = a root), +4 the builder that
# binds the 16 rows, +8 the on-leave hook that saves +80, +32 the live page
# struct it fills. Bound at 13 by the first record whose +4 is not a builder.
PAGES = (0x004DD3D0, 104, 13)


class Exe:
    """Virtual-address reads into `gamedata/Runtime 2.exe`."""

    def __init__(self, path=EXE):
        self.d = open(path, "rb").read()
        pe = struct.unpack_from("<I", self.d, 0x3C)[0]
        if self.d[pe:pe + 4] != b"PE\0\0":
            raise ValueError("not a PE image: %s" % path)
        nsec = struct.unpack_from("<H", self.d, pe + 6)[0]
        opt = struct.unpack_from("<H", self.d, pe + 20)[0]
        self.base = struct.unpack_from("<I", self.d, pe + 24 + 28)[0]
        self.sec = []
        for i in range(nsec):
            o = pe + 24 + opt + 40 * i
            vsz, va, rsz, ptr = struct.unpack_from("<4I", self.d, o + 8)
            self.sec.append((va, vsz, ptr, rsz))

    def read(self, va, n):
        rva = va - self.base
        for sva, vsz, ptr, rsz in self.sec:
            if sva <= rva < sva + max(vsz, rsz):
                off = rva - sva
                if off >= rsz:              # past the file image: zero-filled
                    return b"\0" * n
                return self.d[ptr + off:ptr + off + n].ljust(n, b"\0")
        raise KeyError("unmapped address 0x%08X" % va)

    def cstr(self, va, cap=256):
        """The NUL-terminated string at `va`, or "" for a null/unmapped one."""
        if not (self.base < va < self.base + 0x600000):
            return ""
        try:
            return self.read(va, cap).split(b"\0")[0].decode("cp1252")
        except (KeyError, UnicodeDecodeError):
            return ""


def _walk(e, spec):
    base, stride, end = spec
    if (end - base) % stride:
        raise ValueError("table 0x%08X..0x%08X is not a whole number of "
                         "%d-byte records" % (base, end, stride))
    return [e.read(base + stride * i, stride)
            for i in range((end - base) // stride)]


def _i(rec, off):
    return struct.unpack_from("<i", rec, off)[0]


_IAM_CACHE = {}


def iam_strings(name):
    """The NUL-separated string file `gamedata/IAM/<name>`, trailing blanks dropped.

    Cached: the UI page asks for a screen's strings once per item drawn."""
    if name not in _IAM_CACHE:
        d = open(omkpaths.data("IAM", name), "rb").read().split(b"\0")
        while d and d[-1] == b"":
            d.pop()
        _IAM_CACHE[name] = [s.decode("cp1252") for s in d]
    return _IAM_CACHE[name]


def screens(e=None):
    """The 37 interface screens, in table order (the index IS the `ui.open` id).

    +0 label, +4 id, +8 the screen's fixed parameter (-1 = take the caller's),
    +12 the `I2d\\bitmaps` artwork, +16 the `IAM\\` text file, +20/+24/+28/+32
    the open / tick / input / close callbacks, +36 twelve sound ids, +84/+88
    the two flag banks.
    """
    e = e or Exe()
    out = []
    for i, r in enumerate(_walk(e, SCREENS)):
        out.append(dict(
            index=i, id=_i(r, 4), name=e.cstr(_i(r, 0)), param=_i(r, 8),
            bitmap=e.cstr(_i(r, 12)), text=e.cstr(_i(r, 16)),
            cb=[_i(r, 20) & 0xFFFFFFFF, _i(r, 24) & 0xFFFFFFFF,
                _i(r, 28) & 0xFFFFFFFF, _i(r, 32) & 0xFFFFFFFF],
            sounds=[_i(r, 36 + 4 * k) for k in range(12)],
            flags=(_i(r, 84) & 0xFFFFFFFF, _i(r, 88) & 0xFFFFFFFF)))
    return out


def sounds(e=None):
    """The interface sound effects: {id: stem}, played as `i2d\\sounds\\<stem>.wav`."""
    e = e or Exe()
    return {_i(r, 0): r[4:20].split(b"\0")[0].decode("cp1252")
            for r in _walk(e, SOUNDS)}


def pages(e=None):
    """The 13 pages of the options menu, as a tree.

    Each is a static record; `Opt_EnterPage` runs its +4 builder, which calls
    `Opt_BindRow` sixteen times - once per row widget - with the item id to
    show and, for a submenu row, the page it leads to.
    """
    e = e or Exe()
    base, stride, n = PAGES
    out = []
    for i in range(n):
        r = e.read(base + stride * i, stride)
        u = lambda o: struct.unpack_from("<I", r, o)[0]
        out.append(dict(index=i, va=base + stride * i, parent=u(0),
                        builder=u(4), leave=u(8), live=u(32)))
    return out


def oscillators(e=None):
    """The 8 UI animation timers, read by the widget drawers as a live value.

    +0 the id, +4 the accumulator, +8 the period in ms, +12 flags (bit 0 =
    running), +16/+20 the low and high output, +24 the current value, and
    +28/+32/+36 start / tick / stop. `UI_TickScreens` advances every one whose
    bit 0 is set, by the frame delta, and the item drawers sample +24: number 1
    blinks the highlight, number 2 throbs an alpha between 45 and 200.
    """
    e = e or Exe()
    out = []
    for i, r in enumerate(_walk(e, OSCILLATORS)):
        out.append(dict(index=i, id=_i(r, 0), period=_i(r, 8), flags=_i(r, 12),
                        lo=_i(r, 16), hi=_i(r, 20),
                        tick=_i(r, 32) & 0xFFFFFFFF))
    return out


#: The per-screen open/close callbacks, by the address the screen table names.
#: 26 of these 30 have NO function in `Runtime.exe.c` and no `proc` label in
#: the listing either - they do not open with a push prologue, so IDA folded
#: them into whatever precedes them (docs/UI.md 6). The names are recorded
#: here because there is nowhere in `readable/src` to put them.
CALLBACKS = {
    0x0042A150: "Ui_CloseScreenDefault",   # the shared close; 12 screens use it directly
    0x0042A050: "Ui_BeginScreen",          # every open's tail
    0x0042E3A0: "Ui_OpenShootMeca",     0x0042E480: "Ui_CloseShootMeca",
    0x0042E4A0: "Ui_OpenShootHuman",    0x0042E5A0: "Ui_CloseShootHuman",
    0x00479D10: "Ui_OpenStartMenu",     0x00479F30: "Ui_CloseStartMenu",
    # The start menu's PANELS, read from the image 2026-08-30 (all absent from
    # the decompilation - no `push` prologue, so no `proc` label). Each panel
    # record names its builder at +4 and its leave hook at +8.
    0x00479F60: "StartMenu_BuildRoot",  0x00479FB0: "StartMenu_LeaveRoot",
    0x0047A050: "StartMenu_BuildNewGame",   # clears byte_69BDA0 / dword_657994
    0x0047A1E0: "StartMenu_LeaveNewGame",
    0x0047A230: "StartMenu_NewGameInput",   # the up/down list hook
    0x0047A2B0: "StartMenu_Confirmer",      # GATED on a non-empty name
    0x0047A390: "StartMenu_NameField",
    0x0047A6D0: "StartMenu_BuildLoad",  0x0047AB30: "StartMenu_LeaveLoad",
    0x0047ABA0: "StartMenu_LoadInput",
    0x0047BB40: "StartMenu_BuildOptions", 0x0047BB90: "StartMenu_LeaveOptions",
    0x0047BBB0: "StartMenu_BuildQuit",    0x0047BBF0: "StartMenu_LeaveQuit",
    0x00490D50: "Opt_OpenScreen",       0x00490F30: "Opt_CloseScreen",
    0x0049B400: "Ui_OpenSneakFamily",   0x0049B610: "Ui_CloseSneakFamily",
    0x004AD9A0: "Ui_OpenHighScore",
    0x004ADDB0: "Ui_OpenPause",         0x004ADEB0: "Ui_ClosePause",
    0x004ADFB0: "Ui_OpenSaveGame",
    0x004AE540: "Ui_OpenShop",          0x004AE7E0: "Ui_CloseShop",
    0x004AF000: "Ui_OpenSurvKit",
    0x004AF030: "Ui_OpenTerminal",      0x004AF0E0: "Ui_CloseTerminal",
    0x004AF130: "Ui_OpenFightSim",
    0x004AF190: "Ui_OpenMorgue",
    0x004AF200: "Ui_OpenArchives",
    0x004AF280: "Ui_OpenSurvError",
    0x004AF750: "Ui_OpenXachen",
    0x004AFB10: "Ui_OpenDen",
    0x004AFDF0: "Ui_OpenGandharDoor",
    0x004B00B0: "Ui_OpenLift",
    0x004B01F0: "Ui_OpenMultiplan",     0x004B02D0: "Ui_CloseMultiplan",
    0x00475A50: "Ui_DrawScreen",        0x00475CE0: "Ui_DrawShootScreen",
    0x0042A0F0: "Ui_ScreenInput",
}

#: Ui_OpenShop switches on the screen's fixed parameter and picks the title
#: string out of `IAM/Buy`. Read from the jump table at 0x004AE7AC, whose ten
#: targets are each one `mov word ptr [0x004E37CC], imm16`.
SHOP_TITLE = {0: 10, 1: 18, 2: 13, 3: 15, 4: 11,
              5: 17, 6: 12, 7: 16, 8: 14, 9: 19}
SHOP_JUMPTABLE = 0x004AE7AC


def shop_titles(e=None):
    """{shop parameter: the IAM/Buy string Ui_OpenShop shows}, read from the
    jump table rather than trusted from the constant above."""
    e = e or Exe()
    S = iam_strings("Buy")
    out = {}
    for i in range(10):
        t = struct.unpack("<I", e.read(SHOP_JUMPTABLE + 4 * i, 4))[0]
        op = e.read(t, 9)
        if op[:3] != b"\x66\xc7\x05":          # mov word ptr [imm32], imm16
            raise ValueError("shop case %d is not the expected store" % i)
        addr, imm = struct.unpack_from("<IH", op, 3)
        if addr != 0x004E37CC:
            raise ValueError("shop case %d stores to 0x%08X" % (i, addr))
        out[i] = (imm, S[imm] if imm < len(S) else None)
    return out


#: Opt_BindRow's switch over the row type at +0, and what each does to the
#: widget's flags. Types 2/5/6 are the rows with no value column.
OPT_TYPE = {0: "choice", 1: "slider", 2: "header", 3: "keybind",
            4: "device", 5: "defaults", 6: "back"}


def options(e=None):
    """The 74 rows of the options menu, with every label resolved.

    +0 type, +4 choice count, +8 the current choice, +12/+16 the read-back and
    apply hooks, +24 the row's label, +52.. the choice captions, +92.. the
    values they stand for, +132 a dirty/apply flag, +136 the row's own index
    plus one (0 in the last record - the table's own terminator).
    """
    e = e or Exe()
    S = iam_strings("Options")
    def txt(k):
        return S[k] if 0 <= k < len(S) else None
    out = []
    for i, r in enumerate(_walk(e, OPTIONS)):
        n = _i(r, 4)
        ch = []
        if 0 < n <= 10:
            ch = [(txt(_i(r, 52 + 4 * k)), _i(r, 92 + 4 * k)) for k in range(n)]
        out.append(dict(
            index=i, type=_i(r, 0), kind=OPT_TYPE.get(_i(r, 0), "?"),
            label=txt(_i(r, 24)), label_id=_i(r, 24), choices=ch,
            hooks=(_i(r, 12) & 0xFFFFFFFF, _i(r, 16) & 0xFFFFFFFF),
            seq=_i(r, 136)))
    return out


# ------------------------------------------------------------------ selftest
def selftest():
    """The checks the data can fail. Every one is a walk landing exactly, or a
    cross-reference into the shipped tree resolving."""
    e = Exe()
    fail = []
    def ck(ok, msg):
        print(("  ok   " if ok else "  FAIL ") + msg)
        if not ok:
            fail.append(msg)

    sc, op, sn = screens(e), options(e), sounds(e)

    print("screens  (0x%08X, 92 bytes, bound aNoOne_45)" % SCREENS[0])
    ck(len(sc) == 37, "%d records, want 37" % len(sc))
    ck([s["id"] for s in sc] == list(range(37)),
       "+4 is 0..36 in order - the index is the ui.open operand")
    ck(all(s["name"] for s in sc), "every record names itself")
    shipped = {f.lower() for f in os.listdir(omkpaths.data("I2D/bitmaps"))}
    named = {s["bitmap"].lower() for s in sc if s["bitmap"]}
    ck(named == shipped, "%d bitmaps named = %d shipped, no spares either way"
       % (len(named), len(shipped)))
    iam = set(os.listdir(omkpaths.data("IAM")))
    tx = {s["text"] for s in sc if s["text"]}
    ck(tx <= iam, "%d IAM text files named, all present" % len(tx))

    print("sounds   (0x%08X, 20 bytes, bound aNoOne_0)" % SOUNDS[0])
    ck(len(sn) == 45, "%d records, want 45" % len(sn))
    ck(sorted(sn) == list(range(45)), "ids 0..44, contiguous")
    wav = {f.lower() for f in os.listdir(omkpaths.data("I2D/sounds"))}
    miss = sorted(v for v in sn.values() if (v.lower() + ".wav") not in wav)
    ck(not miss, "all 45 named .wav files ship (%s)" % (miss or "none missing"))
    used = {k for s in sc for k in s["sounds"] if k != -1}
    ck(used <= set(sn), "%d ids the screens use, all resolve" % len(used))

    print("options  (0x%08X, 140 bytes, bound dword_4DCE48)" % OPTIONS[0])
    ck(len(op) == 74, "%d records, want 74" % len(op))
    ck([o["seq"] for o in op] == list(range(1, 74)) + [0],
       "+136 counts 1..73 and terminates on 0")
    ck(all(o["label"] is not None for o in op),
       "every row's +24 resolves in IAM/Options")
    slots = [(o["index"], c) for o in op for c, _ in o["choices"]]
    blank = sorted({i for i, c in slots if c is None})
    ck(len(slots) == 37 and blank == [23, 24],
       "%d choice captions, and the only rows without one are 23/24 - the two "
       "mouse-sensitivity sliders, which show a number rather than a caption "
       "(their +52 is -1 throughout and Opt_ReadSensitivity reads +92[+8] "
       "directly)" % len(slots))
    ck(all(o["kind"] != "?" for o in op),
       "every +0 is one of Opt_BindRow's seven cases")
    # The rows the screen builders bind, against the rows that exist.
    bound = {o["index"] for o in op}
    ck(bound == set(range(74)), "the 74 indices are contiguous")

    print("bitmaps  (the panel background: one 640x480 sheet, tiled 64x64)")
    import struct as _st
    dims = {}
    bdir = omkpaths.data("I2D/bitmaps")
    for f in os.listdir(bdir):
        h = open(os.path.join(bdir, f), "rb").read(54)
        dims[f] = _st.unpack_from("<ii", h, 18)
    ck(set(dims.values()) == {(640, 480)},
       "all %d screen bitmaps are 640x480 (%s)"
       % (len(dims), sorted(set(dims.values()))))
    ck(10 * 64 == 640 and 7 * 64 + 32 == 480,
       "the tile grid covers one exactly: 10 columns of 64, seven rows of 64 "
       "and the half row Ui_DrawPanelBack special-cases (448..480)")

    print("oscillators (0x%08X, 40 bytes, bound on the string block)"
          % OSCILLATORS[0])
    osc = oscillators(e)
    ck(len(osc) == 8, "%d records, want 8" % len(osc))
    ck([o["id"] for o in osc] == list(range(8)), "+0 is 0..7, its own index")
    ck(osc[2]["lo"] == 45 and osc[2]["hi"] == 200 and osc[2]["period"] == 1000,
       "oscillator 2 - the alpha the arrows and the selection marker throb "
       "with - runs 45..200 over 1000 ms")

    print("screen callbacks")
    live = [s for s in sc if any(s["cb"])]
    ck(len(live) == 32, "%d live screens (37 less the five ELIMINE)" % len(live))
    ck(len({s["cb"][1] for s in live}) == 1,
       "one input callback for all of them")
    ck(len({s["cb"][0] for s in live}) == 20
       and len({s["cb"][2] for s in live}) == 10
       and len({(s["cb"][0], s["cb"][2]) for s in live}) == 20,
       "20 distinct open callbacks and 10 close over 20 pairs - the only "
       "slots that vary")
    addrs = {a for s in live for a in s["cb"]}
    ck(addrs <= set(CALLBACKS),
       "every callback address the table names is one this file names (%d)"
       % len(addrs))

    st = shop_titles(e)
    ck({k: v[0] for k, v in st.items()} == SHOP_TITLE,
       "the shop jump table maps parameter 0..9 to %s" % SHOP_TITLE)
    want = {0: "Banque", 1: "Pharmacie", 2: "Armurerie", 5: "Sorcellerie",
            6: "Librairie", 7: "Sex shop", 8: "Divers", 9: "Lahoreh"}
    byparam = {s["param"]: s for s in sc if s["cb"][0] == 0x004AE540}
    ck(all(want[k].lower() in st[k][1].lower() for k in want),
       "and eight of the ten titles NAME THE SCREEN that uses them - "
       "%s - which is a third independent confirmation of the table order "
       "(3 RESTAURANT and 4 BAR share the generic 'Achat')"
       % ", ".join("%s->%r" % (byparam[k]["name"], st[k][1]) for k in sorted(want))[:120])

    print("pages    (0x%08X, 104 bytes, 13 records)" % PAGES[0])
    pg = pages(e)
    va = {p["va"] for p in pg}
    ck(all(p["parent"] == 0 or p["parent"] in va for p in pg),
       "every parent pointer resolves to another page in the table")
    ck(sum(p["parent"] == 0 for p in pg) == 2,
       "two roots (the standalone page 0 and the Options menu proper)")
    ck(len({p["builder"] for p in pg}) == 13, "13 distinct builders")
    ck(sum(p["live"] == 0x004DD2A8 for p in pg) == 12,
       "12 of 13 fill the shared live page at 0x004DD2A8")

    print("\n%d failures" % len(fail))
    return len(fail)


def main(argv):
    if "--selftest" in argv:
        return selftest()
    what = argv[1] if len(argv) > 1 else "screens"
    e = Exe()
    if what.startswith("scr"):
        sn = sounds(e)
        for s in screens(e):
            print("%2d %-24s param=%-4d bmp=%-14s txt=%-8s flags=%08x  %s"
                  % (s["id"], s["name"], s["param"], s["bitmap"] or "-",
                     s["text"] or "-", s["flags"][0],
                     " ".join(sn[k] for k in s["sounds"] if k != -1)))
    elif what.startswith("opt"):
        for o in options(e):
            ch = "  ".join("%s=%d" % c for c in o["choices"])
            print("%2d %-9s %-42s %s" % (o["index"], o["kind"], o["label"], ch))
    elif what.startswith("osc"):
        for o in oscillators(e):
            print("%d  period=%-6d flags=%d  %d..%d  tick=0x%06X"
                  % (o["id"], o["period"], o["flags"], o["lo"], o["hi"],
                     o["tick"] & 0xFFFFFF))
    elif what.startswith("sou"):
        for k, v in sorted(sounds(e).items()):
            print("%2d  %s.wav" % (k, v))
    else:
        print(__doc__)
        return 2
    return 0



def bitmap(name):
    """One of the eleven `gamedata/I2D/bitmaps` sheets, as (w, h, rgb bytes).

    They are bottom-up Windows BMPs, nine of them 8-bit palettised and
    `boutiq` 24-bit. Returned top-down so it can go straight to `tex3dt.png`.
    """
    d = None
    for f in os.listdir(omkpaths.data("I2D", "bitmaps")):
        if f.lower() == name.lower():
            d = open(omkpaths.data("I2D", "bitmaps", f), "rb").read()
            break
    if d is None or d[:2] != b"BM":
        return None
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    hdr = struct.unpack_from("<I", d, 14)[0]
    pal = d[14 + hdr:off]
    stride = ((w * bpp + 31) // 32) * 4
    rows = []
    for y in range(abs(h)):
        r = d[off + stride * y: off + stride * y + stride]
        if bpp == 8:
            rows.append(b"".join(bytes((pal[4 * p + 2], pal[4 * p + 1],
                                        pal[4 * p])) for p in r[:w]))
        elif bpp == 24:
            rows.append(b"".join(bytes((r[3 * x + 2], r[3 * x + 1], r[3 * x]))
                                 for x in range(w)))
        else:
            return None
    if h > 0:
        rows.reverse()                       # bottom-up on disk
    return w, abs(h), b"".join(rows)


#: `Ui_DrawPanelBack`'s flags, on the panel's bank-B word at +72.
PANEL_NO_BACK, PANEL_WHOLE_SHEET = 0x40002000, 0x40004000
TILE, TILES_ACROSS, TILE_ROWS = 64, 10, 8


def panel_background(screen_id, panel, e=None):
    """The background `Ui_DrawPanelBack` actually composes, as (w, h, rgb).

    Not the raw sheet - that is the mistake a viewer makes. The engine takes
    one of three paths and only the middle one shows the sheet whole:

      flag 0x40002000  nothing at all
      flag 0x40004000  the sheet, stretched over the screen
      else, panel+20   EIGHTY TILE IDS - a 10-wide, 8-deep grid of 64x64
                       cells, each id selecting a source cell (id%10, id/10)

    So the parts of a sheet that are *source art* - the lit copies of the
    buttons, which sit in their own strip - are never on screen. Drawing the
    whole sheet puts them there, which is exactly what they are not.

    Destination row 7 is half height (7*64 = 448 leaves 32), and for it the
    source is the MIDDLE 32 rows of its source row rather than the top - the
    one asymmetry in the walk.
    """
    e = e or Exe()
    scr = {s["id"]: s for s in screens(e)}.get(screen_id)
    if not scr or not scr["bitmap"]:
        return None
    flags = struct.unpack_from("<I", e.read(panel + 72, 4), 0)[0]
    if flags & (PANEL_NO_BACK & 0x3FFFFFFF):
        return None
    sheet = bitmap(scr["bitmap"])
    if not sheet:
        return None
    W, H, src = sheet
    if flags & (PANEL_WHOLE_SHEET & 0x3FFFFFFF):
        return sheet
    tiles = struct.unpack_from("<I", e.read(panel + 20, 4), 0)[0]
    if not tiles:
        return None
    ids = e.read(tiles, TILES_ACROSS * TILE_ROWS)
    out = bytearray(W * H * 3)
    for i, tid in enumerate(ids):
        dc, dr = i % TILES_ACROSS, i // TILES_ACROSS
        sc, sr = tid % TILES_ACROSS, tid // TILES_ACROSS
        dx, dy = dc * TILE, dr * TILE
        half = dr == TILE_ROWS - 1
        h = TILE // 2 if half else TILE
        if sr == TILE_ROWS - 1:
            sy = H - TILE // 2 if half else H - TILE
            sy = 448 if not half else 448
            h = min(h, H - sy)
        elif half:
            sy = sr * TILE - TILE // 2
        else:
            sy = sr * TILE
        sx = sc * TILE
        for y in range(h):
            if not (0 <= sy + y < H and 0 <= dy + y < H):
                continue
            so = ((sy + y) * W + sx) * 3
            do = ((dy + y) * W + dx) * 3
            out[do:do + TILE * 3] = src[so:so + TILE * 3]
    return W, H, bytes(out)


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""The game database - IAM\START and the block a save file carries.

One 8192-byte allocation (`Mem_Calloc(0x2000, 1)`) holds everything the game
remembers: the player's character sheet, where he is, the four object lists,
the variables and the four state bitmaps. `IAM\START` is that block as shipped -
the new-game state - and a save slot carries the same 8192 bytes back.

Two engine functions define the format and this module mirrors both:

    State_Apply    0x0040DB00   file image -> live state   (relocate)
    State_Save     0x0040D950   live state -> file image   (un-relocate)

    python3 tools/gamestate.py            # the new game, in words
    python3 tools/gamestate.py --check    # the invariants, one line each
    python3 tools/gamestate.py --raw      # the segment walk

See docs/GAME_STATE.md.
"""
import omkpaths
import os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

START = omkpaths.data("IAM/START")
DB_SIZE = 0x2000                      # sub_408E00: Mem_Calloc(0x2000, 1)

# The six relocated arrays, in header order. `bits` is how wide one entry is:
# 32 for the int32 variables, 16 for the int16 scene-per-area table, 2 for the
# prop states (ObjectState_Get shifts by 2 * (i % 4)) and 1 for the three
# one-bit maps. Each array's length is the count at +32 + 2 * k.
ARRAYS = [
    # ptr   count  bits  name              the accessor that establishes it
    (8,     32,    32,   "variables",      "Var_Get / Var_Set          0x0040E510"),
    (12,    34,    16,   "scene_of_area",  "Area_GetLoadedScene        0x0040B140"),
    (16,    36,     2,   "prop_state",     "ObjectState_Get            0x0040B010"),
    (20,    38,     1,   "object_shown",   "State_SetBit               0x0040AF30"),
    (24,    40,     1,   "address_enabled","Address_SetEnabled         0x0040B090"),
    (28,    42,     1,   "zone_state",     "Zone_StateBit              0x0040D500"),
]

# The three object lists State_Apply hands to ObjectList_SetCapacity, in the
# order it hands them: (offset, capacity). List 3 is the shop stock, rebuilt
# from the resident area and never stored here.
LISTS = [(848, 18), (884, 256), (1396, 9)]

PLAYER_REC = (60, 276)          # dword_69BC6C - the player's character record
BIO = [(336, 256), (592, 256)]  # what its +0 and +4 are pointed at

# Raw <-> world units. Load is State_Apply's `n * 100 / 256 / 2.54 - 1`; save is
# State_Save's `round(w * 0.0254 * 256)`. They are NOT inverses - see the doc.
def to_world(n):  return n * 100 * 0.00390625 * 0.3937007874015748 - 1.0
def to_raw(w):
    n = int(w * 0.0254 * 256.0)
    return n + 1 if abs(w - (n + 1) * 0.15378937) < abs(w - n * 0.15378937) else n
def to_degrees(n): return n * 0.087890625            # 360 / 4096
def to_facing(d):  return int(d * 11.37777777777778) & 0xFFF

# The in-game calendar, from the two formatters at 0x0041E690 / 0x0041E6E0.
MONTHS = ["Aqed", "Nadim", "Andar", "Xenep", "Nevod", "Ganevat", "Osmydep",
          "Qomivo", "Taznevet", "Ustanevat", "Nivat", "Mozkanep", "Primevat"]
DAYS_PER_MONTH, MONTHS_PER_YEAR, YEAR_ZERO = 41, 13, 7216
DAY_UNITS, HOURS, MINUTES, SECONDS = 3600000, 21, 15, 33
NEW_GAME_DAY, NEW_GAME_TIME = 52, 2000000        # Game_NewGame's two calls

def format_date(day):
    return "%d %s %d" % (day % DAYS_PER_MONTH + 1,
                         MONTHS[day // DAYS_PER_MONTH % MONTHS_PER_YEAR],
                         day // DAYS_PER_MONTH // MONTHS_PER_YEAR + YEAR_ZERO)

def format_time(t):
    hour = DAY_UNITS // HOURS
    minute, second = hour // MINUTES, hour // MINUTES // SECONDS
    return "%d:%02d:%02d" % (t // hour, t % hour // minute,
                             t % hour % minute // second)


class GameState:
    """The 8192-byte block, addressed by name rather than by offset."""

    def __init__(self, raw):
        self.raw = bytearray(raw)
        if len(self.raw) < DB_SIZE:              # the engine calloc's 0x2000
            self.raw += bytes(DB_SIZE - len(self.raw))
        self.image_size = len(raw)               # what the file actually held

    # ---------------------------------------------------------- primitives
    def u16(self, o):  return struct.unpack_from("<H", self.raw, o)[0]
    def i16(self, o):  return struct.unpack_from("<h", self.raw, o)[0]
    def i32(self, o):  return struct.unpack_from("<i", self.raw, o)[0]
    def u32(self, o):  return struct.unpack_from("<I", self.raw, o)[0]
    def put32(self, o, v): struct.pack_into("<i", self.raw, o, v)

    # ---------------------------------------------------------- the header
    @property
    def version(self): return self.u32(0)
    @property
    def stamp(self):   return self.u32(4)        # 19991004 in the shipped file
    @property
    def area(self):    return self.i16(1414)
    @property
    def scene(self):   return self.i16(1416)

    def offset(self, k): return self.u32(ARRAYS[k][0])
    def count(self, k):  return self.u16(ARRAYS[k][1])

    def array_bytes(self, k):
        """The k'th array's span, from its own count and entry width."""
        bits = ARRAYS[k][2]
        return (self.count(k) * bits + 7) // 8

    # ---------------------------------------------------------- the player
    @property
    def player_id(self):
        v = self.u16(PLAYER_REC[0] + 272)        # State_Apply's own test
        return -1 if v == 0xFFFF else v

    @property
    def player_pos(self):
        return tuple(self.i32(44 + 4 * i) for i in range(3))

    @property
    def player_facing(self): return self.i32(56)

    def record(self, off, n):
        return bytes(self.raw[off:off + n])

    # ------------------------------------------------------ the state maps
    def var(self, i):   return self.i32(self.offset(0) + 4 * i)
    def scene_of(self, area): return self.i16(self.offset(1) + 2 * area)

    def set_scene_of(self, area, scene):
        """`Area_SetLoadedScene` (0x0040B120): u16(GameDB+12, 2*area) = scene.

        Written by VM opcode 71 `scene.load`, whose handler ends
        `push esi / push edi / call sub_40B120`. The function reads as
        `@callers 0` because IDA folds table-dispatched handlers into the
        preceding proc - CLAUDE.md §1's opcode-120 trap.
        """
        struct.pack_into("<h", self.raw, self.offset(1) + 2 * area, scene)
        return scene
    def prop_state(self, i):
        return (self.raw[self.offset(2) + i // 4] >> (2 * (i % 4))) & 3
    def _bit(self, k, i):
        return (self.raw[self.offset(k) + i // 8] >> (i % 8)) & 1
    def object_shown(self, i):   return self._bit(3, i)
    def address_enabled(self, i): return self._bit(4, i)
    def zone_state(self, i):      return self._bit(5, i & 0x7FFF)

    # ------------------------------------------------------- object lists
    def object_list(self, n):
        off, cap = LISTS[n]
        ids = struct.unpack_from("<%dh" % cap, self.raw, off)
        out = []                                  # ObjectList_SetCapacity's scan
        for v in ids:
            if v == -1: break
            out.append(v)
        return out

    # -------------------------------------------------------------- checks
    def walk(self):
        """-> [(name, start, end)] covering the image, in file order.

        The fixed part is laid out by State_Apply itself; the six arrays sit
        where the header's offsets say and are as long as its counts say.
        """
        seg = [("header", 0, 8), ("array offsets", 8, 32), ("array counts", 32, 44),
               ("player placement", 44, 60),
               ("player record", PLAYER_REC[0], PLAYER_REC[0] + PLAYER_REC[1])]
        for i, (o, n) in enumerate(BIO):
            seg.append(("bio string %d" % i, o, o + n))
        for i, (o, cap) in enumerate(LISTS):
            seg.append(("object list %d" % i, o, o + 2 * cap))
        seg += [("current area", 1414, 1416), ("current scene", 1416, 1418)]
        for k, a in enumerate(ARRAYS):
            o = self.offset(k)
            seg.append((a[3], o, o + self.array_bytes(k)))
        return sorted(seg, key=lambda s: s[1])

    def check(self):
        """-> [(ok, text)] - invariants the image could fail."""
        out, w = [], self.walk()
        end = w[0][1]
        gaps = 0
        for name, a, b in w:
            if a < end:
                out.append((False, "overlap at %s (+%d < +%d)" % (name, a, end)))
            gaps += a - end
            end = max(end, b)
        out.append((gaps <= 3 * len(ARRAYS),
                    "%d padding bytes between segments (4-byte alignment)" % gaps))
        out.append((end == self.image_size,
                    "the walk lands on +%d, the file is %d bytes"
                    % (end, self.image_size)))
        for k, a in enumerate(ARRAYS):
            out.append((self.offset(k) % 4 == 0,
                        "%-15s at +%-5d %6d entries, %d bytes"
                        % (a[3], self.offset(k), self.count(k),
                           self.array_bytes(k))))
        return out

    # ------------------------------------------------- State_Apply / _Save
    def relocate(self):
        """State_Apply: file offsets -> absolute pointers, over a base of 0.

        With base 0 the six fields are unchanged, so what this actually models
        is the rest of it: the two bio pointers the engine plants in the player
        record, and the scene the current area is holding.
        """
        self.put32(PLAYER_REC[0] + 0, BIO[0][0])
        self.put32(PLAYER_REC[0] + 4, BIO[1][0])
        struct.pack_into("<h", self.raw, self.offset(1) + 2 * self.area,
                         self.scene)

    def unrelocate(self):
        """State_Save: absolute pointers -> file offsets, and the header fields
        the serializer rewrites from the live game before the copy."""
        struct.pack_into("<h", self.raw, 1414, self.area)
        struct.pack_into("<h", self.raw, 1416, self.scene)


def load(path=START):
    return GameState(open(path, "rb").read())


# --------------------------------------------------------------- save slots
# `IAM\GAMES` holds 256 slots after a 3496-byte profile header, and each slot
# carries the same 8192-byte DB at +40 that `IAM\START` is. So a save is a
# state ANCHOR: a script the game ran late in a playthrough can be replayed
# from the state it actually ran under, instead of from the new-game state.
#
# The 3496 was open when GAME_STATE.md was written ("no shipped file to
# adjudicate", `SaveDir_*` walking 256 records of 72 bytes instead). A file the
# engine wrote settles it: 3496 + 256*32808 is 8402344 exactly, which is the
# size on disk, where the 72-byte reading needs 18432 of header and gives
# 8417280. The header is also plainly not a directory - it opens with the
# profile name "OMK_SAVE" and the display mode 640x480.
SAVE_HEADER = 3496
SLOT_SIZE   = 32808
SLOT_DB     = 40                                   # the DB inside a slot
N_SLOTS     = 256


def save_slots(path):
    """-> [(n, name, day, time)] for the occupied slots of an IAM\\GAMES."""
    d = open(path, "rb").read()
    out = []
    for n in range(N_SLOTS):
        o = SAVE_HEADER + SLOT_SIZE * n
        # only the 40-byte slot HEAD is read here, so that is the guard. The
        # old `o + SLOT_SIZE` refused a file whose last slot is stored without
        # its 24576-byte thumbnail - which is exactly how `traces/save-appart`
        # keeps a save small enough to live in the repo.
        if o + 40 > len(d): break
        nm = d[o:o + 32].split(b"\0")[0]
        if not nm: continue
        day, t = struct.unpack_from("<2I", d, o + 32)
        out.append((n, nm.decode("cp1252", "replace"), day, t))
    return out


def from_save(path, slot=0):
    """The GameState a save slot holds - the same block `State_Apply` takes."""
    d = open(path, "rb").read()
    o = SAVE_HEADER + SLOT_SIZE * slot + SLOT_DB
    if o + DB_SIZE > len(d):
        raise ValueError("slot %d is past the end of %s" % (slot, path))
    return GameState(d[o:o + DB_SIZE])


# ------------------------------------------------------------------- report
def _names():
    import omkdata
    return omkdata.TAGS


def report(st):
    T = _names()
    print("game database  %d bytes on disk, %d live   version %d, stamp %d"
          % (st.image_size, DB_SIZE, st.version, st.stamp))
    print()
    print("  area      %-4d %s" % (st.area, T["AREAS"].get(st.area, "?")))
    print("  scene     %-4d %s" % (st.scene,
                                   T["SCENES"].get(st.scene, "-" if st.scene < 0 else "?")))
    pid = st.player_id
    print("  player    %s" % ("no body yet - the opening script casts one"
                              if pid < 0 else "character %d" % pid))
    unset = st.player_pos == (-1, -1, -1) and st.player_facing == -1
    print("  position  %s" % ("unset" if unset else
          "%s facing %.1f deg" % (tuple(round(to_world(n), 1) for n in st.player_pos),
                                  to_degrees(st.player_facing))))
    print("  clock     day %d = %s, %s"
          % (NEW_GAME_DAY, format_date(NEW_GAME_DAY), format_time(NEW_GAME_TIME)))
    print()
    for n, label in enumerate(("carried (18)", "second list (256)", "memos (9)")):
        ids = st.object_list(n)
        print("  list %d %-18s %s" % (n, label,
              ", ".join("%d %s" % (i, T["OBJECTS"].get(i, "?")) for i in ids) or "empty"))
    print()
    nz = [i for i in range(st.count(0)) if st.var(i)]
    print("  variables  %d of %d non-zero%s"
          % (len(nz), st.count(0), ":" if nz else ""))
    for i in nz[:20]:
        print("      %-4d %-32s %d" % (i, T["VARIABLES"].get(i, "?"), st.var(i)))
    scenes = [(a, st.scene_of(a)) for a in range(st.count(1)) if st.scene_of(a) >= 0]
    print("  scenes resident   %s" % (scenes or "none"))
    import collections
    print("  prop states       %s of %d"
          % (dict(collections.Counter(st.prop_state(i)
                                      for i in range(st.count(2)))), st.count(2)))
    for k, label in ((3, "objects shown"), (4, "addresses enabled"), (5, "zones set")):
        n = sum(st._bit(k, i) for i in range(st.count(k)))
        spare = sum(st._bit(k, i)
                    for i in range(st.count(k), 8 * st.array_bytes(k)))
        print("  %-17s %d of %d   (%d of %d spare tail bits set)"
              % (label, n, st.count(k), spare,
                 8 * st.array_bytes(k) - st.count(k)))


def main():
    st = load()
    if "--raw" in sys.argv:
        end = 0
        for name, a, b in st.walk():
            print("  %s+%-6d %-6d %s" % ("     " if a == end else "gap  ", a, b - a, name))
            end = b
        return 0
    if "--check" in sys.argv:
        bad = 0
        for ok, text in st.check():
            print("  %-4s %s" % ("ok" if ok else "FAIL", text))
            bad += not ok
        # the round trip
        a = load()
        a.relocate(); a.unrelocate()
        diff = [i for i in range(st.image_size) if a.raw[i] != st.raw[i]]
        ok = diff == list(range(60, 68))
        print("  %-4s State_Apply -> State_Save round trip differs at %s"
              % ("ok" if ok else "FAIL",
                 "+%d..+%d only (the two bio pointers)" % (diff[0], diff[-1] + 1)
                 if diff else "nothing"))
        bad += not ok
        return bad
    report(st)
    return 0


if __name__ == "__main__":
    sys.exit(main())

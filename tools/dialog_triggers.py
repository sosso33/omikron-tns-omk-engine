#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Where each conversation is launched from.

A conversation is only ever started one way: VM opcode 61, `dialog.start`,
whose handler at 0x00403560 reads a 2-byte DIALOGS index and calls Dialog_Load
(0x00401800) itself. Bit 14 of the operand makes it indirect - the index is
fetched from the running script's own operand table instead of being a literal.

The scripts that use it are not the conversations' own (opcode 61 appears in
none of the 612 scripts in IAM\DIALOG) and not the scene scripts in SCPTDATA
(none of its 17 functions touches conversations). They live in the *other* IAM
archives, which hold bytecode in the same format:

    IAM\AREA    trigger volumes - by far the bulk
    IAM\SCENE   per-scene scripts
    IAM\GLOBAL  a small global set

AREA and SCENE share a 68-byte record carrying up to three script offsets, at
+0, +4 and +8. Where that record array lives is the only thing that differs:

    AREA    header is 8 int32 at +40; records are [hdr[12], hdr[13])
    SCENE   header is 12 int32 at +0; records are [hdr[4], hdr[5]),
            and hdr[11] is the record count

Verified: all 259 AREA chunks have a record section that divides by 68 exactly,
4989 of 5024 script slots decode cleanly to `end` (0.70% fail), and **every one
of the 907 dialog.start operands names a real conversation**. SCENE agrees on
49 of its 71 chunks, for a further 84 sites, again all valid. GLOBAL uses a
different header - a leading (offset, id) table - and gives 75 more.

The 22 SCENE chunks that do not match are not explained; their headers have a
different shape and are left alone rather than guessed at.
"""
import omkpaths
import struct, sys, os, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dialog_disasm as D, omkdata

DIALOG_START = 61


def archive(path):
    """IAM directory: (offset, size) pairs until the first payload."""
    d = open(path, "rb").read(); n = len(d); first = None
    for i in range(n // 8):
        off, size = struct.unpack_from("<II", d, 8 * i)
        if off and size and off + size <= n:
            if first is None or off < first: first = off
        if first is not None and 8 * (i + 1) > first: break
    out = {}
    for i in range(first // 8):
        off, size = struct.unpack_from("<II", d, 8 * i)
        if off and size and off + size <= n and size >= 4:
            out[i] = d[off:off + size]
    return out


def _slots(b, lo, n, stride, fields):
    for i in range(n):
        o = lo + stride * i
        for field in fields:
            if o + field + 4 > len(b): return
            p = struct.unpack_from("<i", b, o + field)[0]
            if 0 < p < len(b):
                yield i, field, p


def _scripts_from_records(b, lo, n):
    return _slots(b, lo, n, 68, (0, 4, 8))


# Straight from the loaders - sub_40C120 for SCENE, sub_40CC90 for AREA. They
# are the same header, AREA's shifted 32 bytes on by a leading run of eight -1:
#
#     SCENE   records at +16, count int16 at +44
#     AREA    records at +48, count int16 at +76
#
# Both relocate the record's first three int32 from file offsets to pointers,
# which is what makes +0/+4/+8 the script slots, and both convert the rest of
# the record with the same `* 100/256/2.54 - 1` used on DialogCamera positions
# (and *360/4096 on the two int16 at +60/+62), so a record is four XYZ points
# and two angles - a trigger volume.
def _records(b, ptr_at, count_at):
    if len(b) < count_at + 2: return None
    lo = struct.unpack_from("<i", b, ptr_at)[0]
    n  = struct.unpack_from("<h", b, count_at)[0]
    # count 0 is a real, empty area - the loader's own guard is `if (n > 0)`,
    # so an empty record array is not a parse failure
    if n < 0 or lo <= 0 or lo + 68 * n > len(b): return None
    return lo, n


def area_records(b):  return _records(b, 48, 76)
def scene_records(b): return _records(b, 16, 44)

# Each of the three archives also carries a *second* script table, 8 bytes per
# entry with the offset at +0 - the shape GLOBAL uses for its only one. The
# loaders relocate it in a separate loop after the 68-byte records:
#
#     SCENE   table at +36, count int16 at +54
#     AREA    table at +68, count int16 at +86
#
# AREA's holds 6 dialog.start sites that the record walk alone never sees.
#
# The third relocated array in each - SCENE +24 / AREA +56, stride 276 - carries
# no bytecode, and that is not a dead end: it is the *actor table*, keyed by
# actor id at +272, naming each character's model at +144 and its .CTL at +72.
# Actor_FindById (0x0040B190) scans exactly those two arrays. See
# omkdata.dialog_actor and FILE_FORMATS.md section 5e.
SECOND_TABLE = {"SCENE": (36, 54), "AREA": (68, 86)}


def _second_table(name, b):
    at = SECOND_TABLE.get(name)
    if not at: return []
    ptr_at, cnt_at = at
    if len(b) < cnt_at + 2: return []
    lo = struct.unpack_from("<i", b, ptr_at)[0]
    n  = struct.unpack_from("<h", b, cnt_at)[0]
    if n <= 0 or lo <= 0 or lo + 8 * n > len(b): return []
    return list(_slots(b, lo, n, 8, (0,)))


def global_file(path=None):
    r"""IAM\GLOBAL is a plain file, not an IAM archive.

    sub_40DE60 fopen's it through sub_411940 and reads a fixed header:

        +8   int32  script table   (file-relative, relocated in place)
        +20  int32  record array   (44 bytes each)
        +24  int16  script count
        +30  int16  record count

    The table is 8 bytes per entry with the script offset at +0. The header
    checks out exactly: +20 plus 44 * count lands on the file size.

    Treating it as an archive - which is what an earlier pass did - finds a
    plausible-looking chunk and then has to guess where the table ends. That
    guess overran by one entry and missed two scripts.
    """
    path = path or omkpaths.data("IAM", "GLOBAL")
    d = open(path, "rb").read()
    tbl = struct.unpack_from("<i", d, 8)[0]
    n   = struct.unpack_from("<h", d, 24)[0]
    slots = []
    for i in range(n):
        p = struct.unpack_from("<i", d, tbl + 8 * i)[0]
        if 0 < p < len(d): slots.append((i, 0, p))
    return d, slots


LAYOUT = {"AREA": area_records, "SCENE": scene_records}


def triggers(root=None):
    """-> {dialog id: [(archive, chunk, record, field)]}, plus stats."""
    root = root or omkpaths.data("IAM")
    found = collections.defaultdict(list)
    stats = collections.Counter()
    for name in ("AREA", "SCENE", "GLOBAL"):
        path = os.path.join(root, name)
        if not os.path.isfile(path): continue
        if name == "GLOBAL":
            b, slots = global_file(path)
            stats["GLOBAL:chunks"] += 1
            for rec, field, p in slots:
                ops, st = D.disasm(b, p, len(b))
                if st != "ok": stats["GLOBAL:bad scripts"] += 1; continue
                stats["GLOBAL:scripts"] += 1
                for pc, op, operand in ops:
                    if op == DIALOG_START and len(operand) == 2:
                        found[struct.unpack("<h", operand)[0]].append(
                            (name, 0, rec, field))
            continue
        for k, b in sorted(archive(path).items()):
            r = LAYOUT[name](b)
            if not r: stats[name + ":unmatched chunks"] += 1; continue
            slots = list(_scripts_from_records(b, r[0], r[1]))
            slots += _second_table(name, b)
            stats[name + ":chunks"] += 1
            for rec, field, p in slots:
                ops, st = D.disasm(b, p, len(b))
                if st != "ok": stats[name + ":bad scripts"] += 1; continue
                stats[name + ":scripts"] += 1
                for pc, op, operand in ops:
                    if op == DIALOG_START and len(operand) == 2:
                        v = struct.unpack("<h", operand)[0]
                        found[v].append((name, k, rec, field))
    return found, stats


if __name__ == "__main__":
    found, stats = triggers()
    valid = set(omkdata.CHUNKS)
    names = omkdata.TAGS.get("DIALOGS", {})
    total = sum(len(v) for v in found.values())
    good = sum(len(v) for k, v in found.items() if k in valid)
    for k in sorted(stats): print("%-26s %d" % (k, stats[k]))
    print("\ndialog.start sites: %d, naming a real conversation: %d (%d%%)" % (
        total, good, 100 * good // max(1, total)))
    print("distinct conversations launched: %d of %d" % (
        len([k for k in found if k in valid]), len(valid)))
    print("\nmost-triggered:")
    for v, where in sorted(found.items(), key=lambda kv: -len(kv[1]))[:12]:
        src = collections.Counter(w[0] for w in where)
        print("  %-4d %-34s %3d  %s" % (v, names.get(v, "?"), len(where), dict(src)))

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""ANIMS/*.CTL - the state tables, and the animations they carry.

A .CTL is a saved memory image: every pointer field in it holds whatever
address the authoring tool happened to have, so following them gets nowhere.
The loader, sub_45D270 (0x0045D270), does not follow them either - it walks the
file in a fixed order and *overwrites* each one with a real address, allocating
the variable-length blocks back to back as it goes. Mirroring that walk is the
only way to find anything, and it is what this module does.

Layout (all offsets verified: the walk lands exactly on the file size for all
seven shipped .CTL files):

    +0    char[4]  "CE70"
    +12   int32    groupCount
    +16   ptr      groups          -> +88          (fixed up)
    +76   int32    tableCount
    +80   ptr      table           (fixed up)
    +88   Group[groupCount]        32 bytes each

    Group   +4   int32  entryCount
            +12  ptr    entries    (fixed up)

    Entry (88 bytes)
            +8   uint32 flags      gates the optional blocks below
            +28  ptr    -> 8 + 32*count      if byte +76 has bit 3
            +32  ptr    -> 4 * byte at +86
            +36  ptr    -> 4 * byte at +87
            +44  ptr    -> 24 bytes          if flags & 0x140
            +48  ptr    -> 20 bytes          if flags & 0x280
            +52  ptr    -> 40 bytes          if flags & 0x2000000
            +64  ptr    -> 12 bytes          if flags & 0x10
            +68  ptr    -> the 12-byte clip name, unless flags & 0x8002
            +72  ptr    the loaded clip (filled in at run time)

The blocks are allocated in the order the loops appear in sub_45D270, which is
the order reproduced below - get it wrong and everything after it shifts.

After all of that comes the bulk of the file: one `int32 length` followed by a
clip, per named entry, in entry order. Entries whose name repeats an earlier one
share that clip and consume nothing - the loader looks the name up (uppercased)
before reading, which is why the chain only lines up if the same de-duplication
is applied here.

A clip is exactly an `.ani` descriptor with no "3.0V" wrapper: the wrapper is a
property of the library file, not of a clip. sub_45D1F0 hands each block
straight to Anim_RegisterClip, the same function .ani clips go through. That is
why searching a .CTL for "3.0V" finds nothing.

Checked over the shipped files: 7/7 walk to exactly the file size, 398/398 clips
produce a valid descriptor, and all 296177 rotation keys across their 8433
tracks are unit quaternions.
"""
import struct, os, glob, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import anim_ani
import omkpaths

u32 = lambda d, o: struct.unpack_from("<I", d, o)[0]


def walk(path):
    """-> {clips: [{name, offset, length}], groups, entries, size}."""
    d = open(path, "rb").read()
    if d[:4] != b"CE70":
        raise ValueError("not a CE70 control file: %r" % d[:4])

    groupCount = u32(d, 12)
    off = 32 * groupCount + 88
    groups = []
    for i in range(groupCount):
        g = 88 + 32 * i
        n = u32(d, g + 4)
        groups.append((g, n, off))
        off += 88 * n
    v5 = off
    ents = [base + 88 * k for _, n, base in groups for k in range(n)]

    named, childOff, parentOff = {}, {}, {}
    for e in ents:                                  # +68 -> 12-byte name
        if not (u32(d, e + 8) & 0x8002):
            named[e] = v5; v5 += 12
    for e in ents:                                  # +36 -> child id list
        if d[e + 87]: childOff[e] = v5; v5 += 4 * d[e + 87]
    for e in ents:                                  # +32 -> parent id list
        if d[e + 86]: parentOff[e] = v5; v5 += 4 * d[e + 86]
    turn, shift, mdname, combat = {}, {}, {}, {}
    for e in ents:                                  # +44: a turn (degrees)
        if u32(d, e + 8) & 0x140:
            turn[e] = struct.unpack_from("<6f", d, v5); v5 += 24
    for e in ents:                                  # +48: root shift over frames
        if u32(d, e + 8) & 0x280:
            shift[e] = struct.unpack_from("<5f", d, v5); v5 += 20
    for e in ents:                                  # +64: authoring move name
        if d[e + 8] & 0x10:
            mdname[e] = (d[v5:v5+12].split(b"\0")[0]
                         .decode("cp1252", "replace")); v5 += 12
    for e in ents:                                  # +52: combat action window
        if u32(d, e + 8) & 0x2000000:
            combat[e] = struct.unpack_from("<10f", d, v5); v5 += 40

    # The +76/+80 table is the FIGHT AI, one 156-byte profile per difficulty
    # level. Fight_TickAI reads the profile the level selects (sub_45DCB0
    # matches +0 against level+1) and injects a MOVE - a whole sequence of
    # input bitfields - into the actor's queue with Perso_InjectInput.
    #
    #   +0        int32  the id sub_45DCB0 matches: the difficulty level + 1
    #   +4/+6     u16    a delay range in ms, used when the fight is entered
    #   +8/+10    u16    a delay range in ms, used between moves
    #   +16+12k   {int32 count, ptr moves, int32}   twelve SITUATION SLOTS
    #   move (16 bytes)  +4 how many input words, +8 the words themselves
    #
    # Slot 7 is the one Fight_TickAI reads in the state-6/21 branch, at +100
    # and +104 - which is what fixes the slot stride and the field order.
    tableCount = u32(d, 76)
    table, pos = v5, v5 + 156 * tableCount
    slots, profiles = [], []
    for i in range(tableCount):                     # 12 slots of {count, ptr}
        rec = table + 156 * i
        prof = {"offset": rec, "id": u32(d, rec),
                "enter_delay": struct.unpack_from("<2H", d, rec + 4),
                "move_delay": struct.unpack_from("<2H", d, rec + 8),
                "slots": []}
        for k in range(12):
            cnt = u32(d, rec + 16 + 12 * k)
            prof["slots"].append({"count": cnt, "at": pos, "moves": []})
            slots.append((cnt, pos)); pos += 16 * cnt
        profiles.append(prof)
    for cnt, blk in slots:                          # then each item's own list
        for j in range(cnt):
            pos += 4 * u32(d, blk + 16 * j + 4)
    # Second pass: now that every move block is placed, read the input words.
    ip = v5 + 156 * tableCount + sum(16 * c for c, _ in slots)
    for prof in profiles:
        for sl in prof["slots"]:
            for j in range(sl["count"]):
                n = u32(d, sl["at"] + 16 * j + 4)
                sl["moves"].append([u32(d, ip + 4 * t) for t in range(n)])
                ip += 4 * n
    for e in ents:                                  # +28
        if d[e + 76] & 8:
            pos = 32 * u32(d, pos) + pos + 8

    clips, seen = [], set()
    for e in ents:
        if e not in named: continue
        o = named[e]
        name = d[o:o+12].split(b"\0")[0].decode("cp1252", "replace").upper()
        if name in seen: continue                   # shares an earlier clip
        seen.add(name)
        if pos + 4 > len(d): break
        length = u32(d, pos)
        if length <= 0 or pos + 4 + length > len(d): break
        clips.append({"name": name, "offset": pos + 4, "length": length})
        pos += 4 + length

    # The state graph. Ids are resolved by InitCEFFile through Cef_FindState:
    # parents and children within the state's own group, a GoTo across the
    # whole file. Reproduced here so the same lookups can be checked.
    groupOf = {}
    for gi, (_, n, base) in enumerate(groups):
        for k in range(n): groupOf[base + 88 * k] = gi
    byId = {}                       # (group, id) -> entry, and (None, id)
    for e in ents:
        byId[(groupOf[e], u32(d, e))] = e
        byId.setdefault((None, u32(d, e)), e)

    clipOf = {c["name"]: i for i, c in enumerate(clips)}
    states = []
    for e in ents:
        nm = (d[named[e]:named[e]+12].split(b"\0")[0].decode("cp1252", "replace")
              .upper() if e in named else "")
        g = groupOf[e]
        kids = [u32(d, childOff[e] + 4*i) for i in range(d[e+87])] if e in childOff else []
        pars = [u32(d, parentOff[e] + 4*i) for i in range(d[e+86])] if e in parentOff else []
        gt = u32(d, e + 40)
        states.append({
            "index": ents.index(e), "offset": e, "group": g,
            "id": u32(d, e), "name": nm, "flags": u32(d, e + 8),
            "parents": pars, "children": kids, "goto": gt or None,
            "clip": clipOf.get(nm) if nm else None,
            "turn": turn.get(e), "shift": shift.get(e),
            "mdname": mdname.get(e), "combat": combat.get(e),
            "childOk": all((g, c) in byId for c in kids),
            "parentOk": all((g, c) in byId for c in pars),
            "gotoOk": (not gt) or ((None, gt) in byId)})

    return {"file": os.path.basename(path), "size": len(d), "end": pos,
            "exact": pos == len(d), "groups": groupCount,
            "entries": len(ents), "clips": clips, "states": states,
            "ai": profiles, "data": d}


def clip(path_or_walk, name):
    """-> (data, descriptor) for one clip, ready for anim_ani.rotations()."""
    w = path_or_walk if isinstance(path_or_walk, dict) else walk(path_or_walk)
    for c in w["clips"]:
        if c["name"] == name.upper():
            return w["data"], anim_ani.descriptor(w["data"], c["offset"])
    return None, None


def list_all(root=None):
    root = root or omkpaths.data("ANIMS")
    out = []
    for p in sorted(glob.glob(os.path.join(root, "*.CTL")) +
                    glob.glob(os.path.join(root, "*.ctl"))):
        w = walk(p)
        for c in w["clips"]:
            t = anim_ani.descriptor(w["data"], c["offset"])
            if not t: continue
            out.append({"file": w["file"], "name": c["name"],
                        "frames": t["frames"], "bones": t["bones"]})
    return out


if __name__ == "__main__":
    for p in sorted(glob.glob(omkpaths.data("ANIMS/*.CTL")) + glob.glob(omkpaths.data("ANIMS/*.ctl"))):
        w = walk(p)
        print("%-14s %8d bytes  groups %3d  entries %4d  clips %3d  %s" % (
            w["file"], w["size"], w["groups"], w["entries"], len(w["clips"]),
            "exact" if w["exact"] else "MISMATCH end=%d" % w["end"]))

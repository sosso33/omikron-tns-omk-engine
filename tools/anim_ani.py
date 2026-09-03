#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Read the body-animation libraries in gamedata/ANIMS (*.ani, *.CTL).

Both use the same container - magic "3.0V" - and are loaded by sub_434010
(0x00434010), which slurps the file and relocates the offsets in place.

    +0   char[4]  "3.0V"
    +4   int32    groupCount
    +8   group[groupCount], 24 bytes:
             +0  int32  index
             +4  int32  offset of the first clip node, 0 if the group is empty
             ...
    clip node, 36 bytes (a singly-linked list):
             +0  int32  type   - the behaviour slot this clip can fill
             +4  int32  slot
             +8  int32  offset of the animation descriptor
             +20 int32  -1
             +24 int32  offset of the next node, 0 at the end
             +28 char[8] clip name
    animation descriptor:
             +0  int32  frames
             +4  int32  boneCount
             +8  int32
             +12 bone[boneCount], 40 bytes:
                     +0  char[20] bone name
                     +20 int32 posKeys
                     +24 int32 posOffset     12 bytes per key, 0 if absent
                     +28 int32 rotKeys
                     +32 int32 rotOffset     16 bytes per key

    Both offsets are relative to the start of the descriptor. Taken as file
    offsets they mostly still land on quaternions - the file is full of them -
    but the root bone's tracks fall inside the bone table and decode to
    garbage, which is the giveaway.
                     +36 int32 flags

The two track kinds match the .3DM: 12-byte translations and 16-byte
quaternions. In practice only the root carries translation.

Clip `type` (node+0) is how the engine asks for an animation. It never names a
clip: List_PickRandomByType (0x004345E0) walks this list matching type at +0 -
following the next pointer at +24, the same list this loader rebuilds - and
returns a *random* one of the matching clips. There are 91 call sites passing a
literal type.

Only type 11 can be named with confidence: the engine's fallback chain ends
"anim ATTENTE non existante dans le .ANI" after asking for it, and two of the
shipped clips of that type are named `attend`. It is the idle a character
returns to, including when a dialogue line finishes - nothing in IAM\DIALOG
says what to do afterwards, so this is what decides it.

Everything else is unnamed on purpose. 30 of the 34 types have no clip names at
all in the shipped files (they are all "NULL"), and the error strings sit at the
end of fallback chains rather than against one type, so pinning a name on them
would be guesswork. What can be said from the data: type 13 is the partner in
"anim ATTENTE et RECULE" (backing away); types 14 and 16 hold the same
contextual set - Assis, Poteau, Scato, Appel, Attend - and type 15 holds named
set pieces like Debout, path1, rechd.

Key counts run one above the frame count: key 0 is a rest sentinel, so frame f
reads key f+1. See docs/ASSETS.md.
"""
import omkpaths
import struct, sys, os, glob

def load(path):
    d = open(path, "rb").read()
    if d[:4] != b"3.0V": raise ValueError("not a 3.0V animation file")
    n = struct.unpack_from("<I", d, 4)[0]
    clips = []
    for g in range(n):
        o = 8 + 24 * g
        idx, head = struct.unpack_from("<2I", d, o)
        p, guard = head, 0
        while p and p + 36 <= len(d) and guard < 4096:
            ctype = struct.unpack_from("<I", d, p)[0]
            slot = struct.unpack_from("<I", d, p + 4)[0]
            desc = struct.unpack_from("<I", d, p + 8)[0]
            name = d[p+28:p+36].split(b"\0")[0].decode("cp1252", "replace")
            nxt  = struct.unpack_from("<I", d, p + 24)[0]
            clips.append({"group": idx, "slot": slot, "name": name,
                          "type": ctype, "desc": desc, "node": p})
            p = nxt; guard += 1
    return d, clips

def descriptor(d, off):
    frames, bones, x = struct.unpack_from("<3i", d, off)
    if not (0 < bones < 256): return None
    out = []
    for i in range(bones):
        o = off + 12 + 40 * i
        if o + 40 > len(d): return None
        name = d[o:o+20].split(b"\0")[0].decode("cp1252", "replace")
        pk, po, rk, ro, fl = struct.unpack_from("<5i", d, o + 20)
        # track offsets are relative to the descriptor, not the file
        out.append({"name": name, "posKeys": pk, "posOff": po + off if po else 0,
                    "rotKeys": rk, "rotOff": ro + off if ro else 0, "flags": fl})
    return {"frames": frames, "bones": bones, "unknown": x, "tracks": out}

def keys(d, off, count, size, fmt):
    return [struct.unpack_from(fmt, d, off + size * i) for i in range(count)]

def rotations(d, t):
    return keys(d, t["rotOff"], t["rotKeys"], 16, "<4f") if t["rotOff"] else []

def positions(d, t):
    return keys(d, t["posOff"], t["posKeys"], 12, "<3f") if t["posOff"] else []

if __name__ == "__main__":
    for p in (sys.argv[1:] or sorted(glob.glob(omkpaths.data("ANIMS/*.[aA][nN][iI]")))):
        try: d, clips = load(p)
        except Exception as e:
            print(f"{os.path.basename(p)}: {e}"); continue
        ok = 0
        for c in clips:
            dd = descriptor(d, c["desc"])
            if dd: ok += 1
        print(f"{os.path.basename(p):16} {len(d):8} bytes, {len(clips):4} clips, "
              f"{ok} with a readable descriptor")
        for c in clips[:3]:
            dd = descriptor(d, c["desc"])
            if not dd: continue
            print(f"    {c['name']:10} frames={dd['frames']:4} bones={dd['bones']:3} "
                  f"first={dd['tracks'][0]['name']}")


# The one type the evidence names. Everything else is deliberately absent -
# see the module docstring.
TYPE_NAMES = {11: "ATTENTE (idle)"}

def type_name(t):
    return TYPE_NAMES.get(t, "type %d" % t)

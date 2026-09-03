#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""SCPTDATA/*.SCX - the scene scripts, one per playable location.

Loaded by sub_449750 (0x00449750). Like a .CTL, an .SCX is a saved memory
image: the pointer fields hold dead addresses and the loader overwrites them as
it walks. Unlike a .CTL it is also a *stream* - most of the file sits after the
structural block and is pulled in by fread as each resource record is reached,
which is why Aapkayl.SCX is 7 MB with a 39 KB block.

    +0   int32  0x00DEAD00        magic
    +4   int32  5                 version; the loader rejects anything else
    +8   int32
    +12  int32  blockSize
    +16  the block: a stream of chunks, each tagged 0xDEAD00NN,
         terminated by 0xDEADFFFF
    then: the streamed resources, in the order the chunks reference them

A dword that is not a known tag is skipped and the walk continues - that is
literally the loader's `default:` case - so the block may contain padding
between chunks, and does.

    chunk  in-block record   holds
      0    32 bytes          .3dp meshes
      1    36 bytes          .3DA animations
      2    variable          the script objects (below)
      3    26 bytes          .WAV sounds
      4    36 bytes          .3DO sprites/effects
      5    28 bytes          empty in all 220 shipped files
      6    792 bytes         empty in all 220 shipped files
      7    32 bytes          copied to a fixed global array
     10    no count          a streamed block of its own

Chunk 2 is the substance - every one of the 220 files has it, 4511 objects in
all. Each object is a named script entity, and they come in state pairs
(CoffreOpen / CoffreClosed, PorteCuiOpen / PorteCuiClosed) linked by name at
load time:

    object record, 100 bytes
      +0   ptr     the scene (set at load)
      +4   char[20] name
      +24  int32   handle
      +32  int32   functionCount
      +40  ptr     functions          (fixed up)
      +44  int32   syncFunctionCount
      +48  ptr     sync functions     (fixed up)
      +94..97      runtime bytes, cleared after load

then, per object, in object order:
      [u8 hasLink][21-byte partner name if hasLink]
      function[functionCount]      24 bytes each
      function[syncFunctionCount]  24 bytes each
      two tables, each: [u32 n][u32 x n][u32 x n][21 bytes x n]

    function record, 24 bytes
      +0   int32  id            (category << 24) | index; only 17 distinct
      +4   int32  paramCount
      +8   int32  index into the scene's parameter pool  -> pointer at load
      +12  int32  index of a sync function, -1 for none  -> pointer at load

The parameter pool is one shared array of int32 for the whole scene, and the
functions consume it strictly in order: each function's index is the previous
one's index plus its paramCount, with no gaps. That is the strongest check on
this layout, and it holds throughout.

Verified: the chunk-2 walk lands exactly on the following chunk tag in all 220
files, and the chunk stream reaches 0xDEADFFFF in all 220.
"""
import omkpaths
import struct, os, sys, glob

u32 = lambda b, o: struct.unpack_from("<I", b, o)[0]
i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]

STRIDE = {0: 32, 1: 36, 3: 26, 4: 36, 5: 28, 6: 792, 7: 32}
CHUNK_NAME = {0: "meshes (.3dp)", 1: "animations (.3DA)", 2: "script objects",
              3: "sounds (.WAV)", 4: "sprites (.3DO)", 5: "(unused)",
              6: "(unused)", 7: "global table", 10: "streamed block"}


# The engine's own names, taken from its dispatchers (17_script.c and the
# Reinit_ variants in 16_o3de.c). Only ids that actually occur in the shipped
# scenes are listed; three of the seventeen have no name in the binary.
FUNCTIONS = {
    0x01000001: "Script_InterpolateCameras?",
    0x01000002: "Script_InterpolateCameras",
    0x02000004: "Script_SelectBodyAnimation",
    0x0200002A: "Script_SelectRelativeBodyAnimation",
    0x03000008: "Script_MoveObjectOnPath",
    0x0300001A: "Script_AnimationFromExternalScene",
    0x03000021: "Script_MorphObject",
    0x03000023: "Script_ScaleObjectX",
    0x03000024: "Script_ScaleObjectY",
    0x03000025: "Script_ScaleObjectZ",
    0x0300002B: "Script_SwapObject",
    0x0400000D: "Script_Display3DSpriteOnPath",
    0x04000011: "Script_ChainObjects",
    0x0400001B: "Script_ScaleSpriteOnX",
    0x0400001C: "Script_ScaleSpriteOnY",
    0x0400001D: "Script_SetSpriteRolling",
    0x04000020: "Script_MorphPaletteSprite",
    0x04000028: "Script_Display3DSprite",
    0x05000014: "Script_PlaySound",
    0x05000015: "Script_PlaySyncSound",
    0x05000016: "Script_StopSound",
    0x06000017: "Script_Wait",
    0x06000027: "Script_SendMessage",
}

def function_name(fid):
    return FUNCTIONS.get(fid, "fn_%02X_%d" % (fid >> 24, fid & 0xFFFFFF))


def load(path):
    d = open(path, "rb").read()
    magic, version = struct.unpack_from("<2I", d, 0)
    if magic != 0x00DEAD00: raise ValueError("bad magic %08X" % magic)
    if version != 5:        raise ValueError("version %d, loader wants 5" % version)
    unk, blockSize = struct.unpack_from("<2I", d, 8)
    return {"file": os.path.basename(path), "size": len(d), "unk": unk,
            "blockSize": blockSize, "block": d[16:16 + blockSize],
            "streamed": len(d) - 16 - blockSize}


def objects(b):
    """Chunk 2. -> (objects, offset just past the chunk)."""
    n = u32(b, 4)
    base = 8
    after = base + 100 * n
    pcount = u32(b, after)
    pbase = after + 4
    o = pbase + 4 * pcount
    out = []
    for i in range(n):
        r = base + 100 * i
        if b[o]:
            link = b[o+1:o+22].split(b"\0")[0].decode("cp1252", "replace"); o += 22
        else:
            link = None; o += 1
        nf, ns = u32(b, r + 32), u32(b, r + 44)
        fns = []
        for k in range(nf + ns):
            fid, pn, pi, si = struct.unpack_from("<4i", b, o + 24 * k)
            rep, run = struct.unpack_from("<2i", b, o + 24 * k + 16)
            fns.append({"id": fid & 0xFFFFFFFF, "kind": "fn" if k < nf else "sync",
                        "params": [i32(b, pbase + 4 * (pi + j)) for j in range(pn)],
                        "sync": si, "repeat": rep, "runs": run})
        o += 24 * (nf + ns)
        tabs = []
        for _ in range(2):
            c = u32(b, o)
            tabs.append([b[o+4+8*c+21*k : o+4+8*c+21*k+21].split(b"\0")[0]
                         .decode("cp1252", "replace") for k in range(c)])
            o += 4 + 29 * c
        out.append({"index": i, "name": b[r+4:r+24].split(b"\0")[0]
                    .decode("cp1252", "replace"), "handle": u32(b, r + 24),
                    "link": link, "functions": fns, "tables": tabs,
                    "nfn": nf, "nsync": ns, "loop": i32(b, r + 52)})
    return out, o


def chunks(b):
    """-> {type: [record bytes]}, plus where the stream ended."""
    objs, o = objects(b)
    out = {2: objs}
    while o + 4 <= len(b):
        t = u32(b, o)
        if t == 0xDEADFFFF: return out, o + 4
        ty = t & 0xFFFF
        if (t >> 16) != 0xDEAD or ty not in STRIDE:
            o += 4                      # the loader's default: skip and carry on
            continue
        c = u32(b, o + 4); s = STRIDE[ty]
        out[ty] = [b[o+8+s*k : o+8+s*(k+1)] for k in range(c)]
        o += 8 + s * c
    return out, o


def resource_names(recs):
    return [r.split(b"\0")[0].decode("cp1252", "replace") for r in recs]


def scene(path):
    h = load(path)
    ch, end = chunks(h["block"])
    h["chunks"] = ch
    h["objects"] = ch.get(2, [])
    h["endedAt"] = end
    h["complete"] = end <= len(h["block"])
    return h


if __name__ == "__main__":
    args = sys.argv[1:] or sorted(glob.glob(omkpaths.data("SCPTDATA/*.SCX")) +
                                  glob.glob(omkpaths.data("SCPTDATA/*.scx")))
    for p in args:
        try: s = scene(p)
        except Exception as e:
            print("%-16s FAILED %s" % (os.path.basename(p), e)); continue
        parts = []
        for ty in sorted(s["chunks"]):
            n = len(s["chunks"][ty])
            if n: parts.append("%d:%d" % (ty, n))
        print("%-16s %8d bytes (%7d block + %8d streamed)  %s" % (
            s["file"], s["size"], s["blockSize"], s["streamed"], " ".join(parts)))

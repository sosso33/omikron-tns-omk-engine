#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""SCX chunk 10 - the scripted camera "editings" (Cam_PlayEditing's data).

The chunk-10 payload is what the engine calls a *camera file* - its loader
(0x0049EEF0, called from Scene_LoadSCX case 0xDEAD000A into scene +92) rejects
any version but 3 with "Invalid camera file version".  Four arrays, each
cross-referenced by id and resolved to pointers at load - the loader returns 0
on any miss, so "every reference resolves" is the shipped invariant:

    +0   u32 version = 3
    +4   u32 nCameras   +8 u32 nKeys   +12 u32 nTracks   +16 u32 nEditings
    +20  16 runtime bytes (the four array base pointers, dead on disk)
    +36  camera[52]  x nCameras:
           +0 u32 id   +4 char[12] name   +16 f32 pos[3]   +28 f32 target[3]
           +40 f32 roll   +44 f32 fov   +48 runtime
    then key[28]     x nKeys:
           +0 u32 id   +4 u32 camera id -> ptr   +8 f32 frame
           +16 runtime   +20 u32 mode   +24 runtime
    then track[24]   x nTracks:
           +0 u32 id   +4 char[10] name   +14 u16 nKeys
           +16 ptr -> its id list (runtime)   +20 runtime
    then u32 key-id lists, one per track, concatenated in track order
    then editing[32] x nEditings:
           +0 u8 id   +1 char[11] name   +12 u16 nTracks   +16/+20 runtime
           +24 u32 duration (frames)
           +28 u16 target script object id (the u16 at object +26; 0 = not
               wired to any object - 30 of the 125 shipped ship unlinked)
    then u32 track-id lists, likewise

An *editing* is a keyframed camera cut sequence: tracks in order, each track a
run of (frame, camera) keys interpolated linearly (Cam_PlayEditing,
0x0049ECE0, the engine's own name).  Scene_LoadSCX then links each editing to
the script object its +28 handle names (Script_LinkCamEditing - "You cannot
link more editings to this script" past four); Script_PlayScript samples the
linked editing at the object's program clock into the scene's active camera.

    python3 tools/cam_editing.py             # every scene
    python3 tools/cam_editing.py --selftest
"""
import omkpaths
import os, sys, glob, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def f32(b, o): return struct.unpack_from("<f", b, o)[0]


def payload(path):
    """Locate the chunk-10 streamed payload. -> bytes or None."""
    import scene_scx as S
    d = open(path, "rb").read()
    blockSize = u32(d, 12)
    block = d[16:16 + blockSize]
    _, o = S.objects(block)
    order, counts = [2], {}
    while o + 4 <= len(block):
        t = u32(block, o)
        if t == 0xDEADFFFF: break
        if (t >> 16) != 0xDEAD: o += 4; continue
        ty = t & 0xFFFF; o += 4
        order.append(ty)
        if ty in S.STRIDE:
            c = u32(block, o); counts[ty] = c
            o += 4 + S.STRIDE[ty] * c
    if 10 not in order: return None

    def resync(q):
        while q + 8 <= len(d):
            if u32(d, q) == q: return q
            q += 1
        return len(d)

    pos = 16 + blockSize
    for ty in order:
        if ty == 10:
            a, size = u32(d, pos), u32(d, pos + 4)
            if a != pos: pos = resync(pos); size = u32(d, pos + 4)
            return d[pos + 8: pos + 8 + size]
        if ty not in (0, 1, 3, 4): continue
        hdr = 12 if ty == 4 else 8
        for i in range(counts.get(ty, 0)):
            a, size = u32(d, pos), u32(d, pos + 4)
            if a != pos:
                pos = resync(pos); size = u32(d, pos + 4)
            pos += hdr + size
        if ty == 4 and pos < len(d) and u32(d, pos) != pos:
            pos = resync(pos)
    return None


def parse(body):
    nCam, nKey, nTrk, nEdt = struct.unpack_from("<4I", body, 4)
    o = 36
    cams = {}
    for _ in range(nCam):
        cams[u32(body, o)] = {
            "name": body[o+4:o+16].split(b"\0")[0].decode("cp1252", "replace"),
            "pos":    struct.unpack_from("<3f", body, o + 16),
            "target": struct.unpack_from("<3f", body, o + 28),
            "roll": f32(body, o + 40), "fov": f32(body, o + 44)}
        o += 52
    keys = {}
    for _ in range(nKey):
        keys[u32(body, o)] = {"camera": u32(body, o + 4),
                              "frame": f32(body, o + 8),
                              "mode": u32(body, o + 20)}
        o += 28
    trks = []
    for _ in range(nTrk):
        trks.append({"id": u32(body, o), "n": u16(body, o + 14),
                     "name": body[o+4:o+14].split(b"\0")[0]
                             .decode("cp1252", "replace")})
        o += 24
    for t in trks:
        t["keys"] = [u32(body, o + 4 * k) for k in range(t["n"])]
        o += 4 * t["n"]
    tbyid = {t["id"]: t for t in trks}
    edts = []
    for _ in range(nEdt):
        edts.append({"id": body[o], "n": u16(body, o + 12),
                     "name": body[o+1:o+12].split(b"\0")[0]
                             .decode("cp1252", "replace"),
                     "duration": u32(body, o + 24),
                     "target": u16(body, o + 28)})
        o += 32
    for e in edts:
        e["tracks"] = [u32(body, o + 4 * k) for k in range(e["n"])]
        o += 4 * e["n"]
    unresolved = (sum(k["camera"] not in cams for k in keys.values())
                  + sum(i not in keys for t in trks for i in t["keys"])
                  + sum(i not in tbyid for e in edts for i in e["tracks"]))
    return {"cameras": cams, "keys": keys, "tracks": tbyid, "editings": edts,
            "end": o, "size": len(body), "unresolved": unresolved}


def selftest():
    import scene_scx as S
    files = (sorted(glob.glob(omkpaths.data("SCPTDATA/*.SCX"))) +
             sorted(glob.glob(omkpaths.data("SCPTDATA/*.scx"))))
    n10 = exact = 0
    unresolved = badhandle = edts = 0
    for f in files:
        body = payload(f)
        if body is None: continue
        n10 += 1
        r = parse(body)
        if r["end"] == r["size"]: exact += 1
        else: print("  size FAIL", os.path.basename(f), r["end"], r["size"])
        unresolved += r["unresolved"]
        b = S.load(f)["block"]
        n = u32(b, 4)
        hs = {u16(b, 8 + 100 * i + 26) for i in range(n)}
        for e in r["editings"]:
            edts += 1
            if e["target"] and e["target"] not in hs: badhandle += 1
    print("chunk-10 scenes %d, walk exact %d, editings %d, "
          "unresolved refs %d, handles missing from chunk 2: %d"
          % (n10, exact, edts, unresolved, badhandle))
    return n10, exact, edts, unresolved, badhandle


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        selftest(); sys.exit(0)
    files = sys.argv[1:] or sorted(glob.glob(omkpaths.data("SCPTDATA/*.SCX")))
    for f in files:
        body = payload(f)
        if body is None: continue
        r = parse(body)
        print("%-16s cams %3d keys %3d tracks %3d editings %3d  %s"
              % (os.path.basename(f), len(r["cameras"]), len(r["keys"]),
                 len(r["tracks"]), len(r["editings"]),
                 "exact" if r["end"] == r["size"] else
                 "END %d != %d" % (r["end"], r["size"])))

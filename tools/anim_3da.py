#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Read .3DA animations - standalone files in gamedata/ANIMS and the clips embedded
in the SCPTDATA/*.SCX streamed sections.

The format, from sub_46E880 (0x0046E880), the loader both paths share:

    +0   int32  frames
    +4   int32  trackCount
    +8   track[trackCount], 40 bytes:
             +0   int32    node        (0 = the root)
             +4   char[20] bone name   ('TeBassin' - Telis's pelvis,
                                        'TeCuissed' - cuisse droite...)
             +24  int32    posKeys
             +28  int32    posOffset   12 bytes per key; payload-relative,
                                       fixed up to a pointer at load
             +32  int32    rotKeys
             +36  int32    rotOffset   16 bytes per key, w first
    then the key data, tracks in order, positions before rotations.

The same family as the .ani/.CTL descriptor (40-byte bone tracks, 12-byte
translations, 16-byte quaternions, keys = frames + 1 because key 0 is the
rest sentinel) with two differences: a leading int32 per track, and offsets
relative to the payload rather than the descriptor.

Embedded clips: after an SCX's structural block, every record that the block
declared streams its payload in block order - chunk records each carry an
8-byte [tag, size] pre-header (12 bytes for the sprites of chunk 4) and then
`size` bytes. Chunk 1's payloads are these .3DA descriptors, named by the
36-byte records in the block (name at +0, id at +32).

    python3 tools/anim_3da.py --selftest       # every source, every invariant
    python3 tools/anim_3da.py gamedata/SCPTDATA/Aapkayl.SCX
"""
import omkpaths
import os, struct, sys, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

u32 = lambda b, o: struct.unpack_from("<I", b, o)[0]
i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]


def descriptor(d, base=0, size=None):
    """Parse one .3DA payload. -> dict or None; strict, so it can be a test."""
    if size is None: size = len(d) - base
    if size < 8: return None
    frames, n = struct.unpack_from("<2i", d, base)
    if not (0 <= frames < 20000 and 0 < n < 512): return None
    if 8 + 40 * n > size: return None
    tracks, hi = [], 8 + 40 * n
    for i in range(n):
        t = base + 8 + 40 * i
        node, = struct.unpack_from("<i", d, t)
        name = d[t + 4:t + 24].split(b"\0")[0].decode("cp1252", "replace")
        pk, po, rk, ro = struct.unpack_from("<4i", d, t + 24)
        for k, o, s in ((pk, po, 12), (rk, ro, 16)):
            if o:
                if o < 8 + 40 * n or o + s * k > size: return None
                hi = max(hi, o + s * k)
        tracks.append({"node": node, "name": name,
                       "posKeys": pk, "posOffset": po,
                       "rotKeys": rk, "rotOffset": ro})
    return {"frames": frames, "tracks": tracks, "end": hi, "size": size}


def rotations(d, base, tr):
    """-> [(w,x,y,z)] for one track (key 0 is the rest sentinel)."""
    return [struct.unpack_from("<4f", d, base + tr["rotOffset"] + 16 * k)
            for k in range(tr["rotKeys"])]


def positions(d, base, tr):
    return [struct.unpack_from("<3f", d, base + tr["posOffset"] + 12 * k)
            for k in range(tr["posKeys"])]


# ------------------------------------------------------- the SCX stream walk
def scx_stream(path):
    """Walk one SCX's streamed section.

    -> {"anims": [...], "wavs": [...], "sprites": [...], "end", "size", "data"}.

    The structural block declares the records (chunk 2 first, then tagged
    chunks in file order); the stream then carries their payloads in that
    same order. Every streamed record is [u32 own offset, u32 size][payload],
    so the walk is self-checking.

    Chunk 4 - the effect sprites - is the exception, and its header is
    THREE words: [own offset, model size, TEXTURE size]. The payload is a
    whole .3DO immediately followed by its .3dt, which is why a walk that read
    only the second word ran short and had to resync forward to the next
    self-locating header. Read as 12 + model + texture it is exact: all 230
    sprite records in the 220 shipped files land on the next record's own
    offset or on the file size, with no resync at all.
    """
    import scene_scx as S
    d = open(path, "rb").read()
    blockSize = u32(d, 12)
    block = d[16:16 + blockSize]

    # chunk order: objects sit at the top, then the tag stream
    order = [2]
    _, o = S.objects(block)
    counts = {}
    sc_anims = sc_wavs = sc_sprites = None
    while o + 4 <= len(block):
        t = u32(block, o)
        if t == 0xDEADFFFF: break
        ty = t & 0xFFFF
        if (t >> 16) != 0xDEAD: o += 4; continue
        o += 4
        order.append(ty)
        if ty in S.STRIDE:
            c = u32(block, o)
            counts[ty] = c
            if ty == 1:
                sc_anims = [block[o + 4 + 36 * i:o + 4 + 36 * (i + 1)]
                            for i in range(c)]
            if ty == 3:                           # the .WAV registry
                sc_wavs = [block[o + 4 + 26 * i:o + 4 + 26 * (i + 1)]
                           for i in range(c)]
            if ty == 4:                           # the effect-sprite registry
                sc_sprites = [block[o + 4 + 36 * i:o + 4 + 36 * (i + 1)]
                              for i in range(c)]
            o += 4 + S.STRIDE[ty] * c
        # chunks 8 and 10 carry nothing in the block

    # Every streamed record's first header word is ITS OWN FILE OFFSET, which
    # makes the walk self-checking - and, for the two record kinds whose
    # declared size does not cover their payload (chunk 4's sprites embed a
    # whole .3DO that reads its own length; chunk 10 similarly), makes a
    # principled resync possible: scan forward to the next u32 equal to its
    # own position.
    def resync(pos):
        q = pos
        while q + 8 <= len(d):
            if u32(d, q) == q: return q
            q += 1
        return len(d)

    pos = 16 + blockSize
    anims, wavs, sprites, resyncs = [], [], [], 0
    for ty in order:
        if ty == 10:
            a, size = u32(d, pos), u32(d, pos + 4)
            if a != pos: pos = resync(pos); a, size = u32(d, pos), u32(d, pos + 4)
            pos += 8 + size
            if pos < len(d) and u32(d, pos) != pos: pos = resync(pos)
            continue
        if ty not in (0, 1, 3, 4):                # nothing streamed
            continue
        hdr = 12 if ty == 4 else 8
        for i in range(counts.get(ty, 0)):
            a, size = u32(d, pos), u32(d, pos + 4)
            if a != pos:
                if ty in (0, 1, 3):
                    raise ValueError("%s: chunk %d record %d not at %d"
                                     % (os.path.basename(path), ty, i, pos))
                resyncs += 1
                pos = resync(pos); a, size = u32(d, pos), u32(d, pos + 4)
            payload = pos + hdr
            if ty == 4:
                # [own, model size, texture size]: a whole .3DO then its .3dt
                tsz = u32(d, pos + 8)
                rec = sc_sprites[i] if sc_sprites else b""
                sprites.append({"name": rec[:24].split(b"\0")[0]
                                .decode("cp1252", "replace"),
                                "offset": payload, "model": size,
                                "texture": tsz})
                pos = payload + size + tsz
                continue
            if ty == 1:
                rec = sc_anims[i]
                name = rec[:24].split(b"\0")[0].decode("cp1252", "replace")
                anims.append({"name": name, "id": i32(rec, 32),
                              "offset": payload, "declared": size})
            if ty == 3:
                # the sounds are plain RIFF/WAVE, 16-bit PCM mono - the same
                # payloads Sound_Play3D hands to DirectSound
                rec = sc_wavs[i] if sc_wavs else b""
                wavs.append({"name": rec[:24].split(b"\0")[0]
                             .decode("cp1252", "replace"),
                             "offset": payload, "size": size})
            pos = payload + size
    return {"anims": anims, "wavs": wavs, "sprites": sprites,
            "end": pos, "size": len(d), "resyncs": resyncs, "data": d}


def selftest():
    import math
    files = (sorted(glob.glob(omkpaths.data("ANIMS/*.3DA"))) +
             sorted(glob.glob(omkpaths.data("ANIMS/*.3da"))))
    ok = bad = 0
    for f in files:
        d = open(f, "rb").read()
        r = descriptor(d)
        if r and r["end"] == len(d): ok += 1
        else: bad += 1; print("  standalone FAIL", f, r and (r["end"], len(d)))
    print("standalone .3DA: %d exact, %d fail" % (ok, bad))

    scx = (sorted(glob.glob(omkpaths.data("SCPTDATA/*.SCX"))) +
           sorted(glob.glob(omkpaths.data("SCPTDATA/*.scx"))))
    exact = clips = cbad = fbad = 0
    qtot = qunit = 0
    for f in scx:
        try:
            st = scx_stream(f)
        except Exception as e:
            fbad += 1; print("  stream FAIL", os.path.basename(f), e); continue
        if st["end"] == st["size"]: exact += 1
        else: fbad += 1; print("  stream short", os.path.basename(f),
                               st["end"], st["size"])
        d = st["data"]
        for a in st["anims"]:
            r = descriptor(d, a["offset"], a["declared"])
            if not r: cbad += 1; continue
            clips += 1
            for tr in r["tracks"][:2]:
                if tr["rotOffset"] and tr["rotKeys"]:
                    for q in rotations(d, a["offset"], tr)[1:3]:
                        qtot += 1
                        qunit += abs(sum(x * x for x in q) - 1) < .01
    print("SCX streams: %d exact of %d; embedded clips: %d parse, %d fail" %
          (exact, len(scx), clips, cbad))
    print("sampled rotation keys: %d, unit %d" % (qtot, qunit))
    return fbad + cbad


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(1 if selftest() else 0)
    for f in sys.argv[1:]:
        if f.upper().endswith(".SCX"):
            st = scx_stream(f)
            print("%s: %d anims, end %d of %d %s" %
                  (os.path.basename(f), len(st["anims"]), st["end"],
                   st["size"], "EXACT" if st["end"] == st["size"] else ""))
            for a in st["anims"]:
                r = descriptor(st["data"], a["offset"], a["declared"])
                print("  %-24s id %-4d %s" % (a["name"], a["id"],
                      "%d frames, %d tracks" % (r["frames"], len(r["tracks"]))
                      if r else "UNPARSED"))
        else:
            d = open(f, "rb").read()
            r = descriptor(d)
            print(f, r and {"frames": r["frames"], "tracks": len(r["tracks"]),
                            "exact": r["end"] == len(d)})

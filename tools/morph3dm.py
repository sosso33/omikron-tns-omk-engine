#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read the PC morph files, gamedata/MORPH/*.3DM.

A 3DM is one recorded line of dialogue: a talking-head animation with the voice
interleaved frame by frame at 30 fps. The layout comes from the game's own
loader, sub_42C300 (0x0042C300), which computes the frame count as

    (fileSize - 4*hdr[3] - 16) / ((hdr[0] & 0xFFFFFF) + 24*hdr[1] + 16*hdr[3] + 12)

so the record size and the header size both fall straight out of it.

    header, 16 bytes
        +0   uint24 audioBytes    per frame: 368 mono, 736 stereo
        +3   uint8  channels - 1  0 = mono, 1 = stereo (as in the .ADP header)
        +4   uint32 vertexCount
        +8   uint32 frameCount    nominal; the real count comes from the size
        +12  uint32 nodeCount
    preamble, 4 * nodeCount bytes
        uint32 index[nodeCount]   always 0,1,2,... in all 708 shipped files
    then frameCount records of
        (audioBytes + 24*vertexCount + 16*nodeCount + 12) bytes:
        +0                    float  unknown[3]
        +12                   float  node[nodeCount][4]   rotation quaternions
        +12+16*nodeCount      struct { float pos[3]; float normal[3]; }
                                     vertex[vertexCount]
        (record end - audioBytes)    ADPCM voice for this frame

Evidence for the record body, sampled over the shipped files: every one of 1368
sampled vertex normals is a unit vector, and 1263 of 1368 node entries have
sum-of-squares 1. The exceptions are only ever nodes 0 and 1, which hold
something else - node 1 is never a unit quaternion.

Frame counts derived this way account for all 708 files exactly: 549 end on a
record boundary, and in the other 159 the last frame simply carries no audio.

    python3 tools/morph3dm.py gamedata/MORPH/000000.3DM out.wav
    python3 tools/morph3dm.py --info gamedata/MORPH/000000.3DM
    python3 tools/morph3dm.py --scan
"""
import os, struct, sys, glob

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adp
import omkpaths

def header(path):
    with open(path, "rb") as f:
        h = struct.unpack("<4I", f.read(16))
    return {"audio": h[0] & 0xFFFFFF, "channels": (h[0] >> 24) + 1,
            "vertices": h[1], "nominal_frames": h[2], "nodes": h[3], "raw": h}

def layout(path):
    """-> dict with the sizes and the real frame count."""
    h = header(path)
    size = os.path.getsize(path)
    rec = h["audio"] + 24 * h["vertices"] + 16 * h["nodes"] + 12
    pre = 16 + 4 * h["nodes"]
    frames, rem = divmod(size - pre, rec)
    last_audio = True
    if rem:
        # the only remainder that occurs is a record short of its audio block
        frames += 1
        last_audio = False
    h.update(record=rec, preamble=pre, frames=frames, last_frame_audio=last_audio,
             tail=rem, size=size)
    return h

def frame_offset(L, i):
    return L["preamble"] + i * L["record"]

def audio_blocks(path, L=None):
    L = L or layout(path)
    d = open(path, "rb").read()
    n = L["frames"] if L["last_frame_audio"] else L["frames"] - 1
    return [d[frame_offset(L, i) + L["record"] - L["audio"]:
              frame_offset(L, i) + L["record"]] for i in range(n)]

def read(path):
    """-> (pcm, channels, layout)"""
    L = layout(path)
    pcm, ch = adp.decode(b"".join(audio_blocks(path, L)), L["channels"] == 2)
    return pcm, ch, L

def frame(path, i, L=None):
    """-> dict with this frame's quaternions and vertices."""
    L = L or layout(path)
    d = open(path, "rb").read()
    o = frame_offset(L, i)
    head = struct.unpack_from("<3f", d, o)
    q = [struct.unpack_from("<4f", d, o + 12 + 16 * k) for k in range(L["nodes"])]
    vb = o + 12 + 16 * L["nodes"]
    verts = [(struct.unpack_from("<3f", d, vb + 24 * k),
              struct.unpack_from("<3f", d, vb + 24 * k + 12))
             for k in range(L["vertices"])]
    return {"head": head, "nodes": q, "vertices": verts}

if __name__ == "__main__":
    a = sys.argv[1:]
    if a and a[0] == "--scan":
        import collections
        exact = short = bad = 0
        combos = collections.Counter()
        files = sorted(glob.glob(omkpaths.data("MORPH/*.3DM")))
        for f in files:
            L = layout(f)
            combos[(L["vertices"], L["nodes"])] += 1
            if L["tail"] == 0: exact += 1
            elif L["tail"] == L["record"] - L["audio"]: short += 1
            else: bad += 1
        print(f"{len(files)} PC morph files")
        print(f"  end on a record boundary            : {exact}")
        print(f"  last frame carries no audio         : {short}")
        print(f"  unaccounted for                     : {bad}")
        print(f"  (vertices, nodes) combinations      : {dict(sorted(combos.items()))}")
        sys.exit()
    if len(a) == 2 and a[0] == "--info":
        L = layout(a[1])
        for k in ("size","audio","channels","vertices","nodes","nominal_frames",
                  "frames","record","preamble","last_frame_audio","tail"):
            print(f"  {k:18} {L[k]}")
        print(f"  {'duration':18} {L['frames']/30:.2f}s at 30 fps")
        sys.exit()
    if len(a) != 2: sys.exit(__doc__)
    pcm, ch, L = read(a[0])
    open(a[1], "wb").write(adp.wav(pcm, ch))
    print(f"{a[1]}: {len(pcm)//(2*ch)/adp.RATE:.2f}s, {ch}ch, "
          f"{L['frames']} frames, record={L['record']}")

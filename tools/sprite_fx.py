#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""The effect sprites - the game's particles - out of the .SCX stream.

Fire, smoke, explosions, impacts, muzzle flashes and glows are not a particle
system in the modern sense. They are 26 small `.3DO` models, stored WHOLE
inside the `.SCX` files that use them, and registered per scene in chunk 4.
The game names them itself: `EFFECTS2_SMOKE1`, `EFFECTS1_EXPLO1`,
`EFFECTS1_M16I` (an M16 muzzle flash), `IMPFUM` (impact fumee).

    python3 tools/sprite_fx.py                  # the corpus, one line each
    python3 tools/sprite_fx.py --png out/       # a contact sheet per sprite
    python3 tools/sprite_fx.py --selftest

## The record

Chunk 4's streamed record is the one whose header is THREE words rather than
two, and that is the whole reason a walk that assumed two had to scan forward
to resynchronise:

    [u32 own file offset][u32 model size][u32 TEXTURE size]
    [model size bytes: a whole .3DO][texture size bytes: its .3dt]

Read that way the walk is exact - **230 records across the 220 shipped files,
0 resyncs, and all 220 files land on their own size**. Two more invariants come
free and are worth keeping because they cross into other decoders: every
payload begins with `OD3X`, and `tex3dt`'s walk over the embedded texture
consumes it exactly, 26 of 26.

## What a sprite is

One mesh, one material, one 256x256 atlas, and a flat sheet of quads at z = 0.
**Each quad is one frame** and carries its own cell of the atlas - which is
what the engine's own error string calls `NbTrames`, the frame count:

    EFFECTS2_SMOKE1   8 quads in a row      8 frames of 32x32 texels
    EFFECTS1_EXPLO1  16 quads in two rows  16 frames
    EFFECTS2_GLOW     1 quad                a still billboard

The quads sit SIDE BY SIDE in space rather than on top of each other, so the
model is a frame library, not something to draw as it stands: the engine picks
one quad and places it. `Sprite_SpawnInstance` (0x0048EBF0) and the `.CTL`
effect records do that for character effects, and five SCX script functions -
`Script_Display3DSprite` and friends - do it for scene effects. Those five are
still RAW, so how a frame is chosen over time is NOT established here.

Every sprite mesh carries flags `0x3800` (25 of them) or `0x3000` (1): that is
**additive** transparency, `0x1000 | 0x2000`, plus cutout on all but `IMPFUM`.
See docs/ASSETS.md 4c - drawing these at 50% alpha, as this repo did until
2026-08-29, is visibly wrong for exactly the things that most need to glow.
"""
import omkpaths
import sys, os, glob, struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import anim_3da, mesh3do, tex3dt

u32 = lambda b, o: struct.unpack_from("<I", b, o)[0]


def sprites(path):
    """-> [{name, model, texture, offset}] for one .SCX, payloads as bytes."""
    st = anim_3da.scx_stream(path)
    d, out = st["data"], []
    for s in st["sprites"]:
        o, m, t = s["offset"], s["model"], s["texture"]
        out.append({"name": s["name"], "offset": o,
                    "model": d[o:o + m], "texture": d[o + m:o + m + t]})
    return out


def scx_files():
    return sorted(set(glob.glob(omkpaths.data("SCPTDATA/*.SCX")) +
                      glob.glob(omkpaths.data("SCPTDATA/*.scx"))))


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def library():
    """Every distinct sprite in the game -> {name: {model, texture, used_by}}.

    Deduped by name; `used_by` is the .SCX files that register it, which is how
    many instances of it the game can stage.
    """
    lib = {}
    for p in scx_files():
        try: recs = sprites(p)
        except Exception: continue
        for s in recs:
            e = lib.setdefault(s["name"], {"model": s["model"],
                                           "texture": s["texture"],
                                           "used_by": []})
            e["used_by"].append(os.path.basename(p))
    return lib


def frames(model, texture):
    """-> [{u0, v0, u1, v1, w, h, rgb}] one per quad, cut from the atlas.

    A quad's four UV pairs bound one cell. The UV bytes run 0..255 across the
    texture, and every effect atlas is 256x256, so they are pixel coordinates
    as they stand.
    """
    h = mesh3do.header_bytes(model, len(model))
    txs = tex3dt.textures_bytes(model, texture)
    if not txs: return []
    atlas = txs[0]
    out = []
    for q in range(h["quads"]):
        o = h["quadOff"] + 32 * q
        uv = struct.unpack_from("<8B", model, o + 8)
        us, vs = uv[0::2], uv[1::2]
        u0, u1 = min(us), max(us)
        v0, v1 = min(vs), max(vs)
        cw = max(1, round((u1 - u0) / 255 * atlas["w"]))
        ch = max(1, round((v1 - v0) / 255 * atlas["h"]))
        x0 = round(u0 / 255 * atlas["w"])
        y0 = round(v0 / 255 * atlas["h"])
        rgb = bytearray(cw * ch * 3)
        for y in range(ch):
            sy = min(atlas["h"] - 1, y0 + y)
            src = (sy * atlas["w"] + x0) * 3
            rgb[y * cw * 3:(y + 1) * cw * 3] = atlas["rgb"][src:src + cw * 3]
        out.append({"u0": u0, "v0": v0, "u1": u1, "v1": v1,
                    "w": cw, "h": ch, "rgb": bytes(rgb)})
    return out


def contact_sheet(model, texture, pad=2):
    """The frames laid left to right -> (w, h, rgb), ready for tex3dt.png."""
    fr = frames(model, texture)
    if not fr: return None
    ch = max(f["h"] for f in fr)
    cw = sum(f["w"] for f in fr) + pad * (len(fr) - 1)
    out = bytearray(cw * ch * 3)
    x = 0
    for f in fr:
        for y in range(f["h"]):
            dst = ((y * cw) + x) * 3
            out[dst:dst + f["w"] * 3] = f["rgb"][y*f["w"]*3:(y+1)*f["w"]*3]
        x += f["w"] + pad
    return cw, ch, bytes(out)


def selftest():
    """Every number the docstring quotes, re-measured."""
    files = recs = resyncs = ends = od3x = texexact = 0
    lib = {}
    for p in scx_files():
        st = anim_3da.scx_stream(p)
        files += 1
        resyncs += st["resyncs"]
        ends += st["end"] == st["size"]
        d = st["data"]
        for s in st["sprites"]:
            recs += 1
            o, m, t = s["offset"], s["model"], s["texture"]
            od3x += d[o:o + 4] == b"OD3X"
            lib.setdefault(s["name"], (d[o:o + m], d[o + m:o + m + t]))
    for n, (m, t) in lib.items():
        tx = tex3dt.textures_bytes(m, t)
        texexact += bool(tx) and tx[-1]["consumed"] == len(t)
    print("SCX files walked            %d" % files)
    print("  landing exactly on EOF    %d" % ends)
    print("  resyncs needed            %d" % resyncs)
    print("sprite records              %d" % recs)
    print("  payload begins with OD3X  %d" % od3x)
    print("distinct sprites            %d" % len(lib))
    print("  texture walk exact        %d" % texexact)
    ok = (files == ends and resyncs == 0 and recs == od3x
          and len(lib) == texexact)
    print("OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--selftest" in args:
        sys.exit(selftest())
    lib = library()
    if "--png" in args:
        out = args[args.index("--png") + 1]
        os.makedirs(out, exist_ok=True)
        for n, e in sorted(lib.items()):
            sheet = contact_sheet(e["model"], e["texture"])
            if not sheet: continue
            w, h, rgb = sheet
            fn = os.path.join(out, n.replace(".3DO", "") + ".png")
            open(fn, "wb").write(tex3dt.png(w, h, rgb))
            print("%-24s %2d frames  %4dx%-4d -> %s"
                  % (n, len(frames(e["model"], e["texture"])), w, h, fn))
        sys.exit(0)
    print("%-24s %6s %6s %5s %s" % ("sprite", "model", "tex", "frames", "scenes"))
    for n, e in sorted(lib.items()):
        print("%-24s %6d %6d %5d  %d" % (n, len(e["model"]), len(e["texture"]),
                                         len(frames(e["model"], e["texture"])),
                                         len(e["used_by"])))

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""THE TEXTURE PACK PIPELINE - the remaster's first feature (todo/remaster.md B).

Makes the directory `omk-play --texture-pack DIR` (or `[Remaster] texturepack`)
reads: one `<key>.png` per texture, RGBA, the original's size times a power of
two each way, the key the engine's `textureKey` (engine/src/o3de/texpack.h).

    omktexpack.py export  OUT [--match S ...]     the shipped textures, ready to upscale
    omktexpack.py upscale OUT --bin PATH [--model M] [--tile N]
                                                  run Real-ESRGAN (ncnn-vulkan) on them
    omktexpack.py build   OUT [--no-anchor | --anchor-passes N] [--grain F] [--nearest N | --solid R,G,B N] [--match S ...]
                                                  -> OUT/pack, the directory to hand the viewer
    omktexpack.py emissive PACK [--match S ...]   a light map <key>_e.png per texture, by the rule
                                                  below - a START, to be curated by hand
    omktexpack.py materials PACK [--deepbump DIR [--python PY]] [--jobs N] [--force] [--match S ...]
                                                  a NORMAL MAP <key>_n.png per colour texture
                                                  (B3, engine/src/o3de/normalmap.h): the
                                                  engine's generator, over DeepBump's map
                                                  when DIR (its cli.py) is given - see below
    omktexpack.py curate  ROOT                    the CURATION's draft (B2): ROOT/exclude-draft.txt,
                                                  the textures most likely to carry lettering, and
                                                  contact sheets in ROOT/curation/ - NOT applied:
                                                  the engine reads PACK/exclude.txt alone
    omktexpack.py ui      ROOT [--bin PATH] [--model M]
                                                  the INTERFACE PACK (engine/src/ui/uipack.h):
                                                  every BMP the interface loads, upscaled 4x,
                                                  anchored, its black key a smooth alpha ->
                                                  ROOT/ui/pack (`omk-play --ui-pack`, or the
                                                  --remaster preset's ROOT/ui/pack)
    omktexpack.py fonts   ROOT [--k K]            the FONT PACK (engine/src/ui/fontpack.h): every
                                                  face's glyphs K (8) times finer, by
                                                  engine/build/font_hd - the game's own
                                                  FONTS/OMIKRON.TTF for its alien script
                                                  (MENUINTR, SNEAK), the glyph's own
                                                  antialiasing super-resolved for the rest ->
                                                  ROOT/fonts/pack (`omk-play --font-pack`, or
                                                  the --remaster preset's ROOT/fonts/pack)
    omktexpack.py vita    ROOT --from PACK [--max 512] [--light-max 256] [--jobs N]
                                                  the PS VITA's pack: PACK's colours as DXT1/DXT5
                                                  and its light maps as BC4, in KTX2, capped to
                                                  what a 960x544 screen uses -> ROOT/pack (no
                                                  normal maps); see cmd_vita
    omktexpack.py films   ROOT --bin PATH [--model M] [--q Q]
                                                  the FILM PACK (engine/src/platform/filmpack.h):
                                                  the three FLIS films' frames (ffmpeg) upscaled
                                                  4x (M: realesr-animevideov3-x4, a video model -
                                                  steady from frame to frame, the films' own tones),
                                                  re-encoded as MPEG-1 (quality Q, 2) with the
                                                  original MP2 sound copied untouched ->
                                                  ROOT/films/pack/<key>.mpg (`omk-play --film-pack`,
                                                  or the --remaster preset's ROOT/films/pack)
    omktexpack.py compress PACK [--codec bc|astc|all] [--astcenc PATH] [--astc-block 6x6] [--jobs N]
                                                  the pack in GPU BLOCK FORMATS beside its PNGs
                                                  (engine/src/o3de/texpack.h): <key>.bc7.ktx2 (BC7),
                                                  <key>_n.bc7.ktx2, <key>_e.bc4.ktx2 for desktop GPUs;
                                                  <key>[_n|_e].astc.ktx2 (ASTC, 6x6 by default) for
                                                  Android and the Quest - each with its mip chain,
                                                  by engine/build/tex_encode; read instead of the PNG
                                                  by a backend that samples the format
    omktexpack.py grades  DIR [--depth G]         the city grades as .cube LUTs (+ map.txt);
                                                  --depth: every channel to the power G after
                                                  the grade - darker mids, black and white kept
                                                  (1, the previews'; 1.2 with the emitters' light)
                                                  
`export` covers every .3DO/.3DT pair under MESHES and the 26 effect sprites
stored inside the .SCX files (`SCX:<name>` in the index; `--match SCX:` for
them alone). It writes, under OUT:
    index.json        key -> {name, model, material, w, h, keyed}
    src/<key>.png     the texture as shipped (RGB), for looking at
    in/<key>.png      the UPSCALER'S input: wrapped by PAD texels on every
                      side (the engine REPEATS textures, so an edge must be
                      upscaled knowing what is across it - no seams), and the
                      colour-key texels (pure black, cut out on a flag-0x800
                      mesh) filled from their neighbours, so the upscaler does
                      not smear black into the edge
    in/<key>_mask.png the key as white/black, padded the same way - upscaled
                      too, and thresholded, so a cutout's edge comes out
                      smooth instead of in 4x4 steps
`upscale` runs the binary over in/ into up/ (it keeps the names).
`build` crops the pad back off, puts the key back in alpha (colour 0 where it
is 0, as `Texture::toRgbaKeyed` does) and writes pack/<key>.png. `--grain F`
(0..1) mixes a plain bilinear enlargement of the original back in at F: the
upscalers DENOISE, and the painted grain of a floor is part of the art (the
pilot of 2026-10-20 - todo/remaster.md). By DEFAULT the result is ANCHORED to
the shipped texels (back-projection, `anchor` below): its box average at the
original's size is the original again, so the grain and the painted colours
stay the artists' and the upscaler adds only sub-texel sharpness;
`--no-anchor` keeps the upscaler's raw picture; `--anchor-passes N` (1 by
default, the judged pack's) takes the box averages closer to the shipped texels. `--nearest N`
and `--solid` build from the ORIGINALS instead: the first is the shipped
picture at N times the texels (the engine must draw it identically - the
check's invariant), the second one colour (the engine must visibly draw it).

`materials` (the remaster's B3, todo/remaster.md): for every `<key>.png` of
PACK, `texpack_tool normals` writes `<key>_n.png` beside it - the normal in
OpenGL's convention, the height in alpha. With `--deepbump DIR` - HugoTini's
DeepBump (GPL-3), DIR its unpacked source, `--python` an interpreter with
numpy, onnxruntime and imageio - its colour-to-normals map is made first
(cached in PACK/../materials-ai/) and the engine's generator lays its own
grain over it, blurred: the COMBINATION the reader chose (2026-10-21). An
existing `<key>_n.png` is kept unless `--force`; DeepBump's maps are cached
(delete PACK/../materials-ai/ to make them again). Every map fades flat at the
borders of the REGIONS the faces draw from (`texpack_tool islands`, made once
into PACK/../materials-cov/): an atlas's pictures meet in a step the maps read
as a ridge, drawn along every face mapped up to it - Qalisar's floor, BATITR09.

NOTHING THIS WRITES MAY BE COMMITTED: the pixels are the game's, the repo is
public. So OUT is refused inside the repository and inside the data tree.
Stdlib only; the PNG READING of an upscaler's output goes through the engine's
own reader (`engine/build/texpack_tool png`), because pure-Python un-filtering
of a 3072x3072 image takes tens of seconds.
"""
import json
import re
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import omkpaths  # noqa: E402
import tex3dt    # noqa: E402

ROOT = os.path.dirname(HERE)
PAD = 16
TOOL = os.path.join(ROOT, "engine", "build", "texpack_tool")


# ---- the key: FNV-1a 64, exactly `textureKey` ------------------------------
def texture_key(w, h, idx, pal):
    hsh = 0xcbf29ce484222325
    data = struct.pack("<II", w, h) + idx[:w * h] + pal[:768].ljust(768, b"\0")
    for b in data:
        hsh ^= b
        hsh = (hsh * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % hsh


# ---- PNG, written (RGB or RGBA, filter 0) ------------------------------------
def write_png(path, w, h, px, channels):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
    stride = w * channels
    raw = b"".join(b"\0" + px[y * stride:(y + 1) * stride] for y in range(h))
    ctype = {1: 0, 3: 2, 4: 6}[channels]
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n"
                + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6))
                + chunk(b"IEND", b""))


def read_png(path):
    """-> (w, h, rgba) through the engine's reader."""
    if not os.path.exists(TOOL):
        sys.exit(f"{TOOL} is missing - run `make` in engine/ first")
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "x.raw")
        r = subprocess.run([TOOL, "png", path, out], capture_output=True, text=True)
        if r.returncode != 0:
            raise ValueError(r.stderr.strip() or f"cannot read {path}")
        raw = open(out, "rb").read()
    w, h = struct.unpack_from("<ii", raw, 0)
    return w, h, raw[8:]


# ---- where we may write ------------------------------------------------------
def safe_out(out):
    o = os.path.realpath(out)
    for bad, why in ((os.path.realpath(ROOT), "the repository (the pixels must never be committed)"),
                     (os.path.realpath(omkpaths.data_root()), "the game data (it is input only)")):
        if o == bad or o.startswith(bad + os.sep):
            sys.exit(f"refusing to write inside {why}: {out}")
    return o


# ---- the shipped textures ----------------------------------------------------
_SPRITES = None


def sprite_library():
    """The effect sprites stored WHOLE inside the .SCX files (sprite_fx.py):
    {name: {model, texture, used_by}}, read once."""
    global _SPRITES
    if _SPRITES is None:
        import sprite_fx
        _SPRITES = sprite_fx.library()
    return _SPRITES


def source_textures(model):
    """The decoded textures of an index entry's `model`: a path under the data
    tree, or `SCX:<name>` for an effect sprite."""
    if model.startswith("SCX:"):
        e = sprite_library()[model[4:]]
        return tex3dt.textures_bytes(e["model"], e["texture"])
    return tex3dt.textures(os.path.join(omkpaths.data_root(), model))


def shipped(match=()):
    """-> {key: entry} over every .3DO/.3DT pair under MESHES, then the effect
    sprites inside the .SCX files (`SCX:<name>`); the first model wins."""
    data = omkpaths.data_root()
    meshes = next((os.path.join(data, d) for d in os.listdir(data) if d.lower() == "meshes"), None)
    if not meshes:
        sys.exit(f"no MESHES under {data}")
    out = {}
    models = []
    for dp, _dn, fn in os.walk(meshes):
        for f in fn:
            if f.lower().endswith(".3do"):
                models.append(os.path.join(dp, f))
    sources = [os.path.relpath(m, data).replace(os.sep, "/") for m in sorted(models)]
    if not match or any(s.upper().startswith("SCX:") for s in match):
        sources += ["SCX:" + n for n in sorted(sprite_library())]
    for rel in sources:
        if match and not any(s.lower() in rel.lower() for s in match):
            continue
        for i, tx in enumerate(source_textures(rel)):
            w, h = tx["w"], tx["h"]
            if w <= 0 or h <= 0:
                continue
            k = texture_key(w, h, tx["idx"], tx["pal"])
            if k in out:
                continue
            out[k] = {"name": tx["name"], "model": rel, "material": i, "w": w, "h": h,
                      "rgb": tx["rgb"]}
    return out


def key_mask(rgb, n):
    """1 per texel the engine KEYS (pure black), else 0 - `toRgbaKeyed`'s rule."""
    return bytes(1 if rgb[3 * i] == 0 and rgb[3 * i + 1] == 0 and rgb[3 * i + 2] == 0 else 0
                 for i in range(n))


def dilate(rgb, keyed, w, h, passes=PAD, wrap=True):
    """Fill keyed texels from unkeyed neighbours (wrapping, or not past the
    edge for the interface's pictures), up to `passes` deep."""
    px = bytearray(rgb)
    known = bytearray(1 - k for k in keyed)
    for _ in range(passes):
        grow = []
        for i in range(w * h):
            if known[i]:
                continue
            x, y = i % w, i // w
            acc = [0, 0, 0]; c = 0
            for j in (y * w + (x - 1) % w, y * w + (x + 1) % w,
                      ((y - 1) % h) * w + x, ((y + 1) % h) * w + x):
                if not wrap and (abs(j % w - x) > 1 or abs(j // w - y) > 1):
                    continue   # wrapped round an edge
                if known[j]:
                    acc[0] += px[3 * j]; acc[1] += px[3 * j + 1]; acc[2] += px[3 * j + 2]; c += 1
            if c:
                grow.append((i, acc[0] // c, acc[1] // c, acc[2] // c))
        if not grow:
            break
        for i, r, g, b in grow:
            px[3 * i:3 * i + 3] = bytes((r, g, b)); known[i] = 1
    return bytes(px)


def padded(px, w, h, ch, pad=PAD, wrap=True):
    """The image wrapped by `pad` texels on every side - or its edge texels
    repeated (`wrap` False: the interface's pictures do not tile)."""
    at = (lambda v, n: v % n) if wrap else (lambda v, n: min(max(v, 0), n - 1))
    rows = []
    for yy in range(-pad, h + pad):
        y = at(yy, h)
        row = px[y * w * ch:(y + 1) * w * ch]
        left = b"".join(row[(at(x, w) * ch):(at(x, w) * ch) + ch] for x in range(-pad, 0))
        right = b"".join(row[(at(x, w) * ch):(at(x, w) * ch) + ch] for x in range(w, w + pad))
        rows.append(left + row + right)
    return b"".join(rows), w + 2 * pad, h + 2 * pad


def nearest(px, w, h, ch, n):
    """`px` with every texel an n x n block."""
    rows = []
    for y in range(h):
        row = px[y * w * ch:(y + 1) * w * ch]
        big = b"".join(row[x * ch:(x + 1) * ch] * n for x in range(w))
        rows.append(big * n)
    return b"".join(rows)


def bilinear(px, w, h, ch, n):
    """`px` n times larger, bilinear, WRAPPING (the engine repeats textures)."""
    def taps(size):
        out = []
        for o in range(size * n):
            f = (o + 0.5) / n - 0.5
            i0 = int(f // 1)
            t = int(round((f - i0) * 256))
            out.append((i0 % size, (i0 + 1) % size, 256 - t, t))
        return out
    tx, ty = taps(w), taps(h)
    W = w * n
    rows = []
    for y in range(h):
        r = px[y * w * ch:(y + 1) * w * ch]
        row = bytearray(W * ch)
        for o, (a, b, wa, wb) in enumerate(tx):
            for c in range(ch):
                row[o * ch + c] = (r[a * ch + c] * wa + r[b * ch + c] * wb + 128) >> 8
        rows.append(bytes(row))
    out = bytearray()
    for a, b, wa, wb in ty:
        ra, rb = rows[a], rows[b]
        out += bytes((x * wa + z * wb + 128) >> 8 for x, z in zip(ra, rb))
    return bytes(out)


def bilinear_signed(vals, w, h, ch, n):
    """`bilinear` over a list of signed ints (a residual), wrapping."""
    def taps(size):
        out = []
        for o in range(size * n):
            f = (o + 0.5) / n - 0.5
            i0 = int(f // 1)
            t = (f - i0)
            out.append((i0 % size, (i0 + 1) % size, 1.0 - t, t))
        return out
    tx, ty = taps(w), taps(h)
    W = w * n
    rows = []
    for y in range(h):
        r = vals[y * w * ch:(y + 1) * w * ch]
        rows.append([r[a * ch + c] * wa + r[b * ch + c] * wb
                     for (a, b, wa, wb) in tx for c in range(ch)])
    out = []
    for a, b, wa, wb in ty:
        ra, rb = rows[a], rows[b]
        out.extend(x * wa + z * wb for x, z in zip(ra, rb))
    return out


def anchor(up, src, w, h, n, passes=1):
    """BACK-PROJECTION: correct the upscaled `up` (RGB, w*n x h*n) toward an
    n x n box average that is the SHIPPED texel, by adding the bilinear
    enlargement of the residual - so the texel-scale content (the grain, the
    colours the artists painted) leans to the original's, and what the
    upscaler contributes is mostly what lies between texels.

    ONE pass does not land on it: the bilinear spread of a residual is not
    box-exact. Measured (the review of 2026-10-21) on a grainy 32x32 at 4x,
    the box averages' mean error 5.65 -> 3.23 after one pass, 2.04 after two,
    0.63 after six. The pack the reader judged was built with one; `passes`
    (`build --anchor-passes N`) goes further, for a rebuild."""
    for _ in range(max(1, passes) - 1):
        up = _anchor_once(up, src, w, h, n)
    return _anchor_once(up, src, w, h, n)


def _anchor_once(up, src, w, h, n):
    W = w * n
    down = [0] * (w * h * 3)
    inv = 1.0 / (n * n)
    for y in range(h):
        for x in range(w):
            acc = [0, 0, 0]
            for yy in range(y * n, y * n + n):
                base = (yy * W + x * n) * 3
                seg = up[base:base + 3 * n]
                acc[0] += sum(seg[0::3]); acc[1] += sum(seg[1::3]); acc[2] += sum(seg[2::3])
            k = (y * w + x) * 3
            down[k] = acc[0] * inv; down[k + 1] = acc[1] * inv; down[k + 2] = acc[2] * inv
    res = [s_ - d for s_, d in zip(src, down)]
    big = bilinear_signed(res, w, h, 3, n)
    return bytes(min(255, max(0, int(u + r + 0.5))) for u, r in zip(up, big))


def mix(a, b, f):
    """(1-f)*a + f*b, bytewise."""
    k = int(round(f * 256))
    return bytes((x * (256 - k) + z * k + 128) >> 8 for x, z in zip(a, b))


def crop(px, w, x0, y0, cw, ch_, ch):
    return b"".join(px[((y0 + y) * w + x0) * ch:((y0 + y) * w + x0 + cw) * ch] for y in range(ch_))


def keyed_rgba(rgb, alpha, n):
    """RGBA from RGB and a 0/255 alpha, the colour 0 where alpha is 0."""
    rgba = bytearray(4 * n)
    rgba[0::4] = rgb[0::3]; rgba[1::4] = rgb[1::3]; rgba[2::4] = rgb[2::3]
    m4 = bytearray(4 * n)
    m4[0::4] = alpha; m4[1::4] = alpha; m4[2::4] = alpha; m4[3::4] = b"\xff" * n
    v = int.from_bytes(rgba, "little") & int.from_bytes(m4, "little")
    rgba = bytearray(v.to_bytes(4 * n, "little"))
    rgba[3::4] = alpha
    return bytes(rgba)


# ---- the emissive maps (todo/remaster.md "Step 2, as decided") ---------------
def emissive_factors(rgb, n):
    """Per texel, how much of it is LIGHT, 0..1: much brighter than its own
    texture's median (lettering, windows, bulbs), or bright and strongly
    coloured for its texture (neon). 0 on a keyed texel. The rule the reader
    chose from the previews of 2026-10-20; it also marks quay edges and pale
    facade patches in the daylight cities, so the maps are curated after."""
    lum = [(299 * rgb[3*i] + 587 * rgb[3*i+1] + 114 * rgb[3*i+2]) / 255000 for i in range(n)]
    mxs = [max(rgb[3*i], rgb[3*i+1], rgb[3*i+2]) / 255 for i in range(n)]
    # the medians over the texels DRAWN: a keyed one (black, cut out) is not
    # the texture, and a mostly-cut-out one's median fell toward 0 - marking
    # more of what is left as light (the review of 2026-10-21)
    drawn = [i for i in range(n) if rgb[3*i] or rgb[3*i+1] or rgb[3*i+2]] or list(range(n))
    med = sorted(lum[i] for i in drawn)[len(drawn) // 2]
    vmed = sorted(mxs[i] for i in drawn)[len(drawn) // 2]
    lo = max(med + 0.30, 0.55)
    out = bytearray(n)
    for i in range(n):
        if rgb[3*i] == 0 and rgb[3*i+1] == 0 and rgb[3*i+2] == 0:
            continue
        f = min(1.0, max(0.0, (lum[i] - lo) / 0.15))
        mx = mxs[i]
        mn = min(rgb[3*i], rgb[3*i+1], rgb[3*i+2]) / 255
        if mx > 0:
            sat = (mx - mn) / mx
            fn = (min(1.0, max(0.0, (sat - 0.55) / 0.2)) *
                  min(1.0, max(0.0, (mx - max(vmed + 0.25, 0.5)) / 0.15)))
            f = max(f, fn)
        out[i] = int(f * 255 + 0.5)
    return bytes(out)


def cmd_emissive(pack, match):
    pack = safe_out(pack)
    os.makedirs(pack, exist_ok=True)
    tex = shipped(match)
    lit = 0
    for k, e in tex.items():
        # no CHARACTER emits light in the game, and the rule took pale skin and
        # white cloth for it: their faces glowed under the bloom (the reader,
        # 2026-10-22). A texture a set or an object shares keeps its map - the
        # first model wins, and MESHES/PERSOS sorts after both
        if "/PERSOS/" in e["model"].upper():
            continue
        w, h = e["w"], e["h"]
        f = emissive_factors(e["rgb"], w * h)
        if not any(f):
            continue
        lit += 1
        write_png(os.path.join(pack, k + "_e.png"), w, h, bytes(v for v in f for _ in range(3)), 3)
    print(f"{lit} light maps of {len(tex)} textures into {pack} - curate them: black is not light")


# ---- the material maps (B3) -------------------------------------------------
def cmd_materials(pack, match, deepbump=None, python=None, jobs=3, force=False):
    import concurrent.futures
    pack = safe_out(pack)
    if not os.path.exists(TOOL):
        sys.exit(f"{TOOL} is missing - run `make build/texpack_tool` in engine/ first")
    keys = sorted(n[:16] for n in os.listdir(pack) if len(n) == 20 and n.endswith(".png"))
    if match:
        index = os.path.join(os.path.dirname(pack), "index.json")
        names = json.load(open(index))["textures"] if os.path.exists(index) else {}
        keys = [k for k in keys if any(m.lower() in names.get(k, {}).get("name", "").lower() or m == k for m in match)]
    cache = safe_out(os.path.join(os.path.dirname(pack), "materials-ai"))
    # the faces' REGIONS of every texture (`texpack_tool islands`), made once:
    # the map fades flat at their borders, where an atlas's pictures meet
    # (`.done` written LAST: a run cut short is made again, not trusted)
    covdir = safe_out(os.path.join(os.path.dirname(pack), "materials-cov"))
    if not os.path.exists(os.path.join(covdir, ".done")):
        os.makedirs(covdir, exist_ok=True)
        r = subprocess.run([TOOL, "islands", omkpaths.data_root(), covdir], capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("texpack_tool islands failed: " + r.stderr.strip())
        print(r.stdout.strip())
        open(os.path.join(covdir, ".done"), "w").write(r.stdout)
    if deepbump:
        os.makedirs(cache, exist_ok=True)
        if not os.path.exists(os.path.join(deepbump, "cli.py")):
            sys.exit(f"{deepbump}/cli.py not found - DeepBump's unpacked source")

    def one(k):
        out = os.path.join(pack, k + "_n.png")
        if os.path.exists(out) and not force:
            return "kept"
        src = os.path.join(pack, k + ".png")
        base = "-"
        with tempfile.TemporaryDirectory() as td:
            if deepbump:
                base = os.path.join(cache, k + ".png")
                if not os.path.exists(base):
                    w, h, rgba = read_png(src)
                    rgb = os.path.join(td, "rgb.png")
                    three = bytearray(w * h * 3)
                    three[0::3], three[1::3], three[2::3] = rgba[0::4], rgba[1::4], rgba[2::4]
                    write_png(rgb, w, h, bytes(three), 3)
                    # under another name, then renamed: an interrupted run leaves
                    # no half-written map that a later one would take as made
                    part = os.path.join(cache, k + ".part.png")
                    r = subprocess.run([python or sys.executable, "cli.py", rgb, part, "color_to_normals",
                                        "--color_to_normals-overlap", "SMALL"],
                                       cwd=deepbump, capture_output=True, text=True)
                    if r.returncode != 0 or not os.path.exists(part):
                        return "deepbump failed: " + (r.stderr.strip().splitlines() or ["?"])[-1]
                    os.replace(part, base)
            raw = os.path.join(td, "n.raw")
            cov = os.path.join(covdir, k + ".cov")
            r = subprocess.run([TOOL, "normals", src, raw, base, cov if os.path.exists(cov) else "-"],
                               capture_output=True, text=True)
            if r.returncode != 0:
                return "failed: " + r.stderr.strip()
            b = open(raw, "rb").read()
            w, h = struct.unpack_from("<ii", b, 0)
            part = os.path.join(pack, k + "_n.part.png")   # not a name the engine lists
            write_png(part, w, h, b[8:], 4)
            os.replace(part, out)
        return "made"

    counts = {}
    with concurrent.futures.ThreadPoolExecutor(max(1, jobs)) as ex:
        for n, (k, res) in enumerate(zip(keys, ex.map(one, keys)), 1):
            counts[res.split(":")[0]] = counts.get(res.split(":")[0], 0) + 1
            if res not in ("made", "kept"):
                print(f"  {k}: {res}")
            if n % 100 == 0:
                print(f"  {n} of {len(keys)}", flush=True)
    print(f"normal maps into {pack}: " + ", ".join(f"{v} {k}" for k, v in sorted(counts.items()))
          + (" (over DeepBump's)" if deepbump else " (the engine's generator alone)"))


# ---- the curation (B2) -----------------------------------------------------
# Texture names that say lettering (the score's bonus): the rest is the image's.
LETTERING_NAMES = ("TEXT", "AFFICH", "PANN", "ENSEIGN", "PUB", "LOGO", "SIGN", "ECRAN", "MAPS", "PLAN",
                   "JOURNAL", "TITRE", "MENU", "ETIQ", "NEON")


def lettering_score(w, h, rgba):
    """How much of a texture reads as LETTERING: its "ink" (texels further than
    40 from their 5x5 mean, by luma) cut into 4-connected marks of a glyph's
    size (3..150 texels, 2..20 down, at most 20 across) - and only those in a
    LINE counted: four or more on one baseline (within a texel), of heights
    within two of each other, each within twice its height of the next. Per
    32x32 block. Speckle makes as many small marks as text does (the first cut
    drafted noisy weapons and mottled walls); it does not line them up."""
    n = w * h
    lum = [(299 * rgba[4 * i] + 587 * rgba[4 * i + 1] + 114 * rgba[4 * i + 2]) // 1000 for i in range(n)]
    # the summed-area table, for each texel's 5x5 mean
    sat = [0] * ((w + 1) * (h + 1))
    for y in range(h):
        run = 0
        for x in range(w):
            run += lum[y * w + x]
            sat[(y + 1) * (w + 1) + x + 1] = sat[y * (w + 1) + x + 1] + run
    ink = bytearray(n)
    for y in range(h):
        y0, y1 = max(0, y - 2), min(h, y + 3)
        for x in range(w):
            x0, x1 = max(0, x - 2), min(w, x + 3)
            tot = sat[y1 * (w + 1) + x1] - sat[y0 * (w + 1) + x1] - sat[y1 * (w + 1) + x0] + sat[y0 * (w + 1) + x0]
            mean = tot // ((y1 - y0) * (x1 - x0))
            if abs(lum[y * w + x] - mean) > 40 and rgba[4 * (y * w + x) + 3]:
                ink[y * w + x] = 1
    seen = bytearray(n)
    marks = []
    for start in range(n):
        if not ink[start] or seen[start]:
            continue
        stack = [start]; seen[start] = 1
        area, xs0, xs1, ys0, ys1 = 0, w, -1, h, -1
        while stack:
            i = stack.pop(); area += 1
            x, y = i % w, i // w
            xs0, xs1, ys0, ys1 = min(xs0, x), max(xs1, x), min(ys0, y), max(ys1, y)
            for j in (i - 1 if x > 0 else -1, i + 1 if x < w - 1 else -1, i - w, i + w):
                if 0 <= j < n and ink[j] and not seen[j]:
                    seen[j] = 1; stack.append(j)
        if 3 <= area <= 150 and xs1 - xs0 < 20 and 1 <= ys1 - ys0 < 20:
            marks.append((ys1, xs0, xs1, ys1 - ys0 + 1))
    # the LINES: by baseline, then left to right, runs of close marks alike in height
    marks.sort()
    inline = 0
    i = 0
    while i < len(marks):
        j = i
        while j + 1 < len(marks) and marks[j + 1][0] - marks[i][0] <= 1:
            j += 1
        row = sorted(marks[i:j + 1], key=lambda m: m[1])
        run = [row[0]] if row else []
        for m in row[1:]:
            prev = run[-1]
            if m[1] - prev[2] <= 2 * max(prev[3], m[3]) and abs(m[3] - prev[3]) <= 2:
                run.append(m)
            else:
                if len(run) >= 4:
                    inline += len(run)
                run = [m]
        if len(run) >= 4:
            inline += len(run)
        i = j + 1
    return inline / max(1.0, n / 1024.0)


def cmd_curate(root, count=48):
    """The CURATION's draft (todo/remaster.md B2): the textures most likely to
    carry LETTERING the upscaler garbles, scored on the ORIGINALS (ROOT/src),
    written as ROOT/exclude-draft.txt - NOT applied: the engine reads
    PACK/exclude.txt alone, so the reader moves the lines they agree with -
    and contact sheets ROOT/curation/sheet-NN.png, each row the original (x4,
    nearest) beside the pack's, for judging."""
    root = safe_out(root)
    idx = json.load(open(os.path.join(root, "index.json")))["textures"]
    pack = os.path.join(root, "pack")
    scored = []
    for n, (k, e) in enumerate(sorted(idx.items()), 1):
        src = os.path.join(root, "src", k + ".png")
        if not os.path.exists(src) or not os.path.exists(os.path.join(pack, k + ".png")):
            continue
        w, h, rgba = read_png(src)
        sc = lettering_score(w, h, rgba)
        name = e.get("name", "")
        if any(t in name.upper() for t in LETTERING_NAMES):
            sc += 2.0
        scored.append((sc, k, name, e.get("model", "")))
        if n % 200 == 0:
            print(f"  {n} of {len(idx)}", flush=True)
    scored.sort(reverse=True)
    top = scored[:count]
    with open(os.path.join(root, "exclude-draft.txt"), "w") as f:
        f.write("# THE CURATION's DRAFT (omktexpack.py curate) - NOT APPLIED. The engine reads\n"
                "# PACK/exclude.txt: move there the lines whose upscale garbles lettering\n"
                "# (see curation/sheet-NN.png, the original beside the pack's). A listed key\n"
                "# draws as shipped; its light map stays.\n#\n#   KEY               score  NAME (model)\n")
        for sc, k, name, model in top:
            f.write(f"{k}  # {sc:5.2f}  {name} ({model})\n")
    # the sheets: 6 rows a page, original x4 (nearest) | the pack's, both 512 across
    out = os.path.join(root, "curation"); os.makedirs(out, exist_ok=True)
    S = 512
    for page in range(0, len(top), 6):
        rows = top[page:page + 6]
        W, H = 2 * S + 16, len(rows) * (S + 8)
        img = bytearray(W * H * 3)
        for r, (sc, k, name, model) in enumerate(rows):
            for col, path in enumerate((os.path.join(root, "src", k + ".png"), os.path.join(pack, k + ".png"))):
                w, h, rgba = read_png(path)
                for yy in range(S):
                    sy = yy * h // S
                    for xx in range(S):
                        sx = xx * w // S
                        q = 4 * (sy * w + sx); o = 3 * ((r * (S + 8) + yy) * W + col * (S + 16) + xx)
                        img[o:o + 3] = rgba[q:q + 3]
        write_png(os.path.join(out, "sheet-%02d.png" % (page // 6 + 1)), W, H, bytes(img), 3)
    print(f"curation: {len(top)} candidates of {len(scored)} into {root}/exclude-draft.txt and "
          f"{(len(top) + 5) // 6} sheets in {out} - NOT applied (PACK/exclude.txt is the reader's)")


# ---- the city grades ---------------------------------------------------------
def _s(v, a):
    return min(1.0, max(0.0, v + a * (v - 0.45) * (1 - abs(2 * v - 1)) * 1.6))


def _sat(r, g, b, k):
    l = 0.299 * r + 0.587 * g + 0.114 * b
    return l + (r - l) * k, l + (g - l) * k, l + (b - l) * k


def grade_rgb(kind, r, g, b):
    """The grades of the 2026-10-20 previews, 0..1 channels (todo/remaster.md
    keeps every one; these four were chosen). No vignette: a LUT is per
    colour, the noir preview's vignette was spatial."""
    l = 0.299 * r + 0.587 * g + 0.114 * b
    if kind == "filmic":
        r, g, b = (_s(v * 1.06, 0.35) for v in (r, g, b))
        r, g, b = _sat(r, g, b, 1.12)
    elif kind == "noir":
        r, g, b = _sat(r, g, b, 0.82)
        r, g, b = r * 0.94, g * 0.99, b * 1.08
        r, g, b = (_s(max(0.0, v - 0.02) * 1.04, 0.3) for v in (r, g, b))
    elif kind == "sunlit":
        hi = max(0.0, l * 1.4 - 0.3)
        r, g, b = r + 0.045 * hi, g + 0.022 * hi, b - 0.03 * hi
        r, g, b = (0.03 + v * 0.97 for v in (r, g, b))
        r, g, b = (_s(v * 1.04, 0.22) for v in (r, g, b))
        r, g, b = _sat(r, g, b, 1.06)
    elif kind == "neon2":
        r, g, b = _sat(r, g, b, 1.15)
        sh = max(0.0, 1 - l * 2.5)
        r, g, b = r + 0.012 * sh, g - 0.01 * sh, b + 0.03 * sh
        r, g, b = (0.012 + max(0.0, v) ** 0.95 * 0.988 for v in (r, g, b))
        r, g, b = (_s(v * 1.03, 0.22) for v in (r, g, b))
    return tuple(min(1.0, max(0.0, v)) for v in (r, g, b))


CITY_GRADES = {"ANEKBAH": "noir", "JAUNPUR": "filmic", "LAHOREH": "sunlit", "QALISAR": "neon2"}
# interiors and other sets that take a city's grade (`map.txt`); the rest none
CITY_OF = {"AAPKAYL": "ANEKBAH"}
# THE NIGHT (`night.txt`, `CityGrades::nightOf`): how much of a set's painted
# light stays when the emitters' lights are on - Qalisar's neons its night's
# light, the Blade Runner streets it is drawn from (the reader, 2026-10-20)
CITY_NIGHT = {"QALISAR": 0.5}
# THE LIGHT SOURCES (`lights.txt`, `o3de/emitterlight.h` SurfaceCuration):
# which textures, and which parts of them, cast light with the emitters'
# light on - the data cannot tell a neon tube from a bright painted panel.
# Written only where there is none (the reader edits it); first drafts of
# the four cities, from their atlases, 2026-10-20.
# THE REFLECTING FLOORS (`floors.txt`, `o3de/floorreflect.h`): which floors
# are wet or polished, and how much - a FIRST DRAFT, Qalisar alone, read off
# `texpack_tool floors` (its level opaque area by texture and height): the
# street's paving, BATITR09 at 0 and 4 (4060 m2) and CATITR03 at 0 (862), and
# the raised walks, CATITR02 at -316 and -136 (774). To be judged in play.
CITY_FLOORS = """# THE REFLECTING FLOORS - which floors mirror the scene, and how much
# (omk-play's --floors; `o3de/floorreflect.h`). One plane a frame: the listed
# floor's level below the eye with the most of it near, the water first.
#
#   SET TEXTURE = STRENGTH       # 0..1, times --floors (the preset's 1)
#
# `texpack_tool floors <set.3DO>` lists a set's level floors by texture and area.
#
# QALISAR - a first draft, 2026-10-21: the neon street wet, its raised walks less.
Qalisar BATITR09 = 0.5
Qalisar CATITR03 = 0.5
Qalisar CATITR02 = 0.35
"""

CITY_LIGHTS = """# THE LIGHT SOURCES - which textures, and which parts of them, cast light
# (omk-play's emitters' light, --fx-light; `o3de/emitterlight.h`).
# A set this file names is curated: only what is listed lights. A set it does
# not name keeps the automatic rules (the light maps and the neon rule).
#
#   SET TEXTURE [U0,V0,U1,V1] = GAIN [RRGGBB]
#
# U/V in the ORIGINAL texture's texels (256x256 here, wrapped); a texel takes
# the first rectangle it lies in, then the texture's line without one. Which
# texels of it glow is still the rules' (strong light map, or a neon colour).
# GAIN: 1 a lit panel, 3-4 a neon tube. RRGGBB: the light's colour, else the
# lit texels'. `texpack_tool lights <pack> <set.3DO>` lists the candidates.
#
# QALISAR - a first draft, 2026-10-20: the neon letters strong and coloured,
# the signs bright, the lit windows and panels low.
QALISAR CATITR05 224,0,256,128 = 2.0 ff2ec8   # the pink letters ("XOOC")
QALISAR CATITR05 192,0,224,128 = 2.5          # the white/turquoise letters beside them
QALISAR CATITR05 0,0,128,128 = 1.0            # the yellow emblem panel
QALISAR CATITR05 = 0.5                        # its lit windows
QALISAR CATITR20 0,0,128,128 = 0.5            # the lit glass panel
QALISAR BATITR11 128,128,256,256 = 1.0        # the strip lamp
QALISAR CATITR17 0,0,128,128 = 2.5            # the turquoise letter signs
QALISAR CATITR17 = 0.5                        # the yellow window
QALISAR CATITR10 0,0,128,128 = 2.0            # the orange sign
QALISAR CATITR10 128,128,256,256 = 1.5        # the white-lettered signs
QALISAR CATITR07 128,64,256,128 = 2.0         # the yellow sign
QALISAR BATITR12 0,0,128,128 = 1.5            # the sign bands
QALISAR BATITR12 = 0.5                        # the lit panel
QALISAR CATITR13 128,0,256,128 = 1.5          # the signs
QALISAR CATITR13 = 0.5                        # the windows
QALISAR CATITR18 = 0.5                        # the posters
QALISAR BATITR16 128,0,256,32 = 3.0           # the orange letters
QALISAR BATITR04 = 0.5                        # the lit panel
QALISAR BATITR19 = 0.6                        # the emblem
QALISAR BATITR15 = 1.5                        # the sign atlas
#
# ANEKBAH - a first draft, 2026-10-20: the letters and the neon star strong,
# the signs and the lamps bright, the lit windows low; the paintings out.
ANEKBAH BATITR16 128,0,256,32 = 3.0           # the orange letters
ANEKBAH BATITR13 0,0,128,128 = 2.0            # the neon star
ANEKBAH BATITR13 128,0,192,128 = 1.5          # the small signs
ANEKBAH BATITR12 0,0,128,128 = 1.5            # the sign bands
ANEKBAH BATITR12 128,128,256,256 = 0.5        # the lit panel
ANEKBAH BATITR15 0,0,256,128 = 1.5            # the sign atlas
ANEKBAH BATITR15 = 0.5                        # its posters
ANEKBAH BATITR18 224,0,256,128 = 2.0          # the letter column
ANEKBAH BATITR18 64,0,224,128 = 0.5           # the lit panel
ANEKBAH BATITR18 128,128,256,256 = 0.4        # the food posters
ANEKBAH BATITR20 192,128,256,256 = 2.0        # the green letters
ANEKBAH BATITR20 128,128,192,256 = 1.0        # the lamp
ANEKBAH BATITR20 0,0,128,128 = 1.0            # the green lamp strips
ANEKBAH BATITR14 = 1.0                        # the yellow lamps
ANEKBAH BATITR17 128,64,256,128 = 1.0         # the lamps
ANEKBAH BATITR11 128,128,256,256 = 1.0        # the strip lamp
ANEKBAH BATITR08 64,0,192,128 = 0.5           # the lit window
ANEKBAH BATITR04 128,128,256,256 = 0.5        # the lit panel
ANEKBAH BATITR10 0,128,128,256 = 0.5          # the lit window
ANEKBAH BATITR19 0,128,128,256 = 0.6          # the emblem
#
# JAUNPUR - a first draft, 2026-10-20: a sunlit city - its signs alone, at 1;
# its light maps mostly mark white sandstone, which is bright, not light.
JAUNPUR TATITR15 64,0,128,128 = 1.0           # the sign column
JAUNPUR TATITR17 0,0,128,128 = 1.0            # the sign panels
JAUNPUR TATITR18 128,0,256,128 = 1.0          # the signs
JAUNPUR TATITR12 0,0,64,128 = 1.0             # the sign column
JAUNPUR TATITR12 128,0,256,128 = 1.0          # the sign bands
JAUNPUR TATITR05 192,128,256,256 = 1.0        # the signs
JAUNPUR SOUK02 128,0,192,128 = 1.0            # the sign column
#
# LAHOREH - a first draft, 2026-10-20: sunlit too - its signs and its lamp.
LAHOREH VENISED3 0,0,128,128 = 1.0            # the sign columns
LAHOREH VENISE3 0,0,128,128 = 1.0             # the sign columns
LAHOREH VENISE19 0,0,256,128 = 1.0            # the sign atlas
LAHOREH VENISE18 0,0,32,256 = 1.0             # the letter column
LAHOREH BATITR11 128,128,256,256 = 1.0        # the strip lamp
"""


def write_cube(path, kind, size=33, depth=1.0):
    with open(path, "w") as f:
        f.write(f"TITLE \"OMK {kind}\"\nLUT_3D_SIZE {size}\n")
        for bi in range(size):
            for gi in range(size):
                for ri in range(size):
                    r, g, b = grade_rgb(kind, ri / (size - 1), gi / (size - 1), bi / (size - 1))
                    if depth != 1.0:
                        r, g, b = (v ** depth for v in (r, g, b))
                    f.write(f"{r:.6f} {g:.6f} {b:.6f}\n")


def cmd_grades(out, depth=1.0):
    out = safe_out(out)
    os.makedirs(out, exist_ok=True)
    for city, kind in CITY_GRADES.items():
        write_cube(os.path.join(out, city + ".cube"), kind, depth=depth)
    with open(os.path.join(out, "night.txt"), "w") as f:
        f.write("# a set name = the share of its painted light kept under the emitters' lights\n")
        for k, v in CITY_NIGHT.items():
            f.write(f"{k} = {v}\n")
    for name, text in (("lights.txt", CITY_LIGHTS), ("floors.txt", CITY_FLOORS)):
        lp = os.path.join(out, name)
        if not os.path.exists(lp):
            with open(lp, "w") as f:
                f.write(text)
        else:
            print(f"{lp} kept - it is the reader's to edit")
    with open(os.path.join(out, "map.txt"), "w") as f:
        f.write("# a set name = the city whose grade it takes (omk-play --grade DIR)\n")
        for k, v in CITY_OF.items():
            f.write(f"{k} = {v}\n")
    print(f"{len(CITY_GRADES)} city grades (.cube, 33^3, depth {depth}), night.txt, lights.txt, floors.txt "
          f"and map.txt into {out}")


# ---- the commands ------------------------------------------------------------
def cmd_export(out, match):
    out = safe_out(out)
    for d in ("src", "in"):
        os.makedirs(os.path.join(out, d), exist_ok=True)
    tex = shipped(match)
    index = {}
    for k, e in tex.items():
        w, h, rgb = e["w"], e["h"], e["rgb"]
        keyed = key_mask(rgb, w * h)
        nkey = sum(keyed)
        write_png(os.path.join(out, "src", k + ".png"), w, h, rgb, 3)
        fill = dilate(rgb, keyed, w, h) if 0 < nkey < w * h else rgb
        p, pw, ph = padded(fill, w, h, 3)
        write_png(os.path.join(out, "in", k + ".png"), pw, ph, p, 3)
        if nkey:
            mask = bytes(0 if kk else 255 for kk in keyed for _ in range(3))
            mp, _, _ = padded(mask, w, h, 3)
            write_png(os.path.join(out, "in", k + "_mask.png"), pw, ph, mp, 3)
        index[k] = {kk: e[kk] for kk in ("name", "model", "material", "w", "h")}
        index[k]["keyed"] = nkey
    with open(os.path.join(out, "index.json"), "w") as f:
        json.dump({"pad": PAD, "textures": index}, f, indent=1, sort_keys=True)
    print(f"exported {len(index)} textures ({sum(1 for v in index.values() if v['keyed'])} keyed) "
          f"to {out}")


# ---- the INTERFACE PACK (todo/remaster.md Tier E; engine/src/ui/uipack.h) --
def read_bmp(path):
    """-> (w, h, rgb) of an uncompressed 8- or 24-bit BMP, top row first -
    the formats `surfaceFromBmp` reads, at their FULL colour (the engine keeps
    565; the upscaler is better fed the 8 bits)."""
    d = open(path, "rb").read()
    if d[:2] != b"BM":
        return None
    bits, hdr = struct.unpack_from("<II", d, 10)
    w, hraw = struct.unpack_from("<ii", d, 18)
    bpp, comp = struct.unpack_from("<HI", d, 28)
    if comp != 0 or bpp not in (8, 24) or w <= 0 or hraw == 0:
        return None
    h = abs(hraw)
    out = bytearray(3 * w * h)
    if bpp == 24:
        stride = (w * 3 + 3) // 4 * 4
        for y in range(h):
            r0 = bits + stride * (y if hraw < 0 else h - 1 - y)
            row = d[r0:r0 + 3 * w]
            seg = bytearray(3 * w); seg[0::3] = row[2::3]; seg[1::3] = row[1::3]; seg[2::3] = row[0::3]
            out[3 * w * y:3 * w * (y + 1)] = seg
    else:
        ncol = struct.unpack_from("<I", d, 46)[0] or 256
        pal = d[14 + hdr:14 + hdr + 4 * ncol]
        lut = [bytes((pal[4 * i + 2], pal[4 * i + 1], pal[4 * i])) if 4 * i + 3 < len(pal) else b"\0\0\0"
               for i in range(256)]
        stride = (w + 3) // 4 * 4
        for y in range(h):
            r0 = bits + stride * (y if hraw < 0 else h - 1 - y)
            out[3 * w * y:3 * w * (y + 1)] = b"".join(lut[v] for v in d[r0:r0 + w])
    return w, h, bytes(out)


# the faces drawn from the game's vector font rather than super-resolved:
# its alien script (the sheet of 2026-10-22 - MENUINTR and SNEAK are its
# rasterisations; PARCHEMI is another script, fitted worse than its own
# coverage)
TTF_FACES = ("MENUINTR", "SNEAK")


def cmd_fonts(root, k):
    """The font pack, into ROOT/fonts: `font_hd` writes raw/<key>.raw (int32
    w, h, then the grey coverage) and says how each face came out; each
    becomes pack/<key>.png, grey."""
    out = safe_out(os.path.join(root, "fonts"))
    tool = os.path.join(os.path.dirname(TOOL), "font_hd")
    if not os.path.exists(tool):
        sys.exit(f"{tool} is missing - run `make build/font_hd` in engine/ first")
    data = omkpaths.data_root()
    ttf = next((os.path.join(data, "FONTS", n) for n in sorted(os.listdir(os.path.join(data, "FONTS")))
                if n.lower() == "omikron.ttf"), None)
    raw, pack = os.path.join(out, "raw"), os.path.join(out, "pack")
    os.makedirs(raw, exist_ok=True)
    os.makedirs(pack, exist_ok=True)
    cmd = [tool, data, os.path.join(ROOT, "tables"), raw, str(k)]
    if ttf:
        cmd += [ttf, ",".join(TTF_FACES)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.returncode != 0:
        sys.exit("font_hd failed: " + r.stderr.strip())
    made = 0
    for line in r.stdout.split("\n"):
        f = line.split()
        if len(f) < 4 or f[0] != "@fonthd":
            continue
        key = f[f.index("key") + 1]
        with open(os.path.join(raw, key + ".raw"), "rb") as fh:
            b = fh.read()
        w, h = struct.unpack_from("<ii", b)
        write_png(os.path.join(pack, key + ".png"), w, h, b[8:8 + w * h], 1)
        made += 1
    print(f"wrote {made} faces into {pack}")


def film_key(path):
    """The engine's `filmKey` (engine/src/platform/filmpack.h): FNV-1a 64 over
    the file's size as eight little-endian bytes, then its first MiB - enough
    to tell the films (and another language's) apart without reading 46 MB at
    every launch."""
    h = 0xcbf29ce484222325
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        head = f.read(1 << 20)
    for b in struct.pack("<Q", size) + head:
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return "%016x" % h


FILMS = ("EIDOS", "QUANTIC", "GAME")


def cmd_films(root, binary, model, q):
    """The film pack, end to end, into ROOT/films: frames/<NAME>/ (every
    frame the decoder gives, PNG), up/<NAME>/ (the upscaler's, 4x), and
    pack/<key>.mpg - MPEG-1 video at 4x the size, the original's MP2 audio
    stream COPIED (no re-encode: the sound stays the shipped bytes), muxed as
    a program stream like the original, so the engine's own decoder (pl_mpeg)
    plays it. Each step skips what is done; a frame count that differs
    between the steps stops it (the picture would drift from the sound).
    A film's frames are DELETED once its .mpg is written: GAME's upscaled
    PNGs alone are ~5 GB, and the three at once filled a disk (2026-10-22)."""
    out = safe_out(os.path.join(root, "films"))
    ff = shutil.which("ffmpeg")
    if not ff:
        sys.exit("films needs ffmpeg on the PATH")
    if not binary:
        sys.exit("films needs --bin PATH (realesrgan-ncnn-vulkan)")
    data = omkpaths.data_root()
    flis = os.path.join(data, next(n for n in os.listdir(data) if n.upper() == "FLIS"))
    pack = os.path.join(out, "pack")
    os.makedirs(pack, exist_ok=True)
    for name in FILMS:
        src = next((os.path.join(flis, n) for n in os.listdir(flis) if n.upper() == name + ".MPG"), None)
        if not src:
            print(f"{name}: not in {flis}, skipped")
            continue
        key = film_key(src)
        dst = os.path.join(pack, key + ".mpg")
        frames, up = os.path.join(out, "frames", name), os.path.join(out, "up", name)
        if os.path.exists(dst):
            print(f"{name}: {dst} already made")
            shutil.rmtree(frames, ignore_errors=True)
            shutil.rmtree(up, ignore_errors=True)
            continue
        os.makedirs(frames, exist_ok=True)
        os.makedirs(up, exist_ok=True)
        if not os.listdir(frames):
            subprocess.run([ff, "-v", "error", "-i", src, "-map", "0:v:0", "-vsync", "passthrough",
                            os.path.join(frames, "%06d.png")], check=True)
        n = len([f for f in os.listdir(frames) if f.endswith(".png")])
        if len([f for f in os.listdir(up) if f.endswith(".png")]) != n:
            cmd = [binary, "-i", frames, "-o", up, "-n", model, "-s", "4", "-f", "png"]
            mdir = os.path.join(os.path.dirname(os.path.abspath(binary)), "models")
            if os.path.isdir(mdir):
                cmd += ["-m", mdir]
            print(f"{name}: {n} frames -> {model}")
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        nu = len([f for f in os.listdir(up) if f.endswith(".png")])
        if nu != n:
            sys.exit(f"{name}: {nu} upscaled frames for {n} - not encoded")
        rate = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                               "stream=r_frame_rate", "-of", "csv=p=0", src],
                              capture_output=True, text=True).stdout.strip() or "30000/1001"
        tmp = dst + ".part"
        subprocess.run([ff, "-v", "error", "-y", "-framerate", rate, "-i", os.path.join(up, "%06d.png"),
                        "-i", src, "-map", "0:v:0", "-map", "1:a:0", "-c:v", "mpeg1video", "-q:v", str(q),
                        "-pix_fmt", "yuv420p", "-c:a", "copy", "-f", "mpeg", tmp], check=True)
        os.replace(tmp, dst)
        print(f"{name}: {n} frames, {os.path.getsize(dst) / 1e6:.0f} MB -> {dst}")
        shutil.rmtree(frames, ignore_errors=True)
        shutil.rmtree(up, ignore_errors=True)
    print(f"film pack: {pack}")


def cmd_compress(pack, codec, astcenc, block, jobs):
    """Every PNG of PACK as KTX2 beside it, in the codecs asked - skipping an
    output newer than its PNG. Says the counts, the failures, each kind's
    mean round-trip PSNR (tex_encode's, at level 0) and the bytes made."""
    import concurrent.futures
    pack = safe_out(pack)
    tool = os.path.join(os.path.dirname(TOOL), "tex_encode")
    if not os.path.exists(tool):
        sys.exit(f"{tool} is missing - run `make build/tex_encode` in engine/ first")
    want = ["bc", "astc"] if codec == "all" else [codec]
    if "astc" in want and not (astcenc and os.path.exists(astcenc)):
        sys.exit("astc needs --astcenc PATH (ARM's astcenc)")
    work, linked = [], 0
    for name in sorted(os.listdir(pack)):
        if not name.endswith(".png") or len(name) not in (20, 22):
            continue
        stem, kind = name[:-4], ("light" if name.endswith("_e.png") else
                                 "normal" if name.endswith("_n.png") else "colour")
        src = os.path.join(pack, name)
        for c in want:
            if c == "bc":
                ext, args = (".bc4.ktx2", ["bc4"]) if kind == "light" else \
                    (".bc7.ktx2", ["bc7n" if kind == "normal" else "bc7"])
            else:
                ext, args = ".astc.ktx2", ["astc"]
            dst = os.path.join(pack, stem + ext)
            if os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src):
                continue
            # a LINKED PNG (a pack made over another, `full-anchor3`'s light
            # and normal maps): its KTX2 is the target's twin, linked too,
            # when that one is made - not a second copy of the same bytes
            if os.path.islink(src):
                twin = os.path.realpath(src)[:-4] + ext
                if os.path.exists(twin):
                    if os.path.lexists(dst):
                        os.remove(dst)
                    os.symlink(os.path.relpath(twin, pack), dst)
                    linked += 1
                    continue
            cmd = [tool, args[0], src, dst] + ([astcenc, block] if c == "astc" else [])
            work.append((c, kind, cmd))
    print(f"compress: {len(work)} files to make in {pack} ({', '.join(want)}), {jobs} at once; "
          f"{linked} linked to the KTX2 beside a linked PNG's target")
    stats, failed, done = {}, [], 0

    def run(w):
        r = subprocess.run(w[2], capture_output=True, text=True)
        return w, r
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        for w, r in ex.map(run, work):
            done += 1
            m = re.search(r"psnr (\S+)", r.stdout)
            if r.returncode != 0 or not m:
                failed.append((w[2][2], r.stderr.strip()[:200]))
                continue
            st = stats.setdefault((w[0], w[1]), [0, 0.0])
            st[0] += 1
            st[1] += float(m.group(1))
            if done % 250 == 0:
                print(f"  {done} of {len(work)}")
    for (c, kind), (n, p) in sorted(stats.items()):
        print(f"  {c} {kind}: {n} made, mean PSNR {p / n:.1f} dB")
    for f, why in failed[:20]:
        print(f"  FAILED {f}: {why}")
    for c, ext in (("bc", (".bc7.ktx2", ".bc4.ktx2")), ("astc", (".astc.ktx2",))):
        files = [n for n in os.listdir(pack) if n.endswith(ext)]
        if files:
            own = [n for n in files if not os.path.islink(os.path.join(pack, n))]
            print(f"  {c}: {len(files)} files ({len(files) - len(own)} links), "
                  f"{sum(os.path.getsize(os.path.join(pack, n)) for n in own) / 1e9:.2f} GB of their own")
    if failed:
        sys.exit(f"{len(failed)} failed")


def cmd_vita(src, root, colour_max, light_max, jobs):
    """The PS VITA's pack: SRC (a desktop pack/, its PNGs) -> ROOT/pack, DXT
    in KTX2 at a resolution the console can use (todo/remaster.md "The Vita's
    pack"). Its GPU (SGX543MP4+, sceGxm) samples UBC1..5 and neither BC7 nor
    ASTC; its screen is 960x544, so the desktop pack's 4x is wasted there:
      * a colour: at most COLOUR_MAX (512 - 2x a 256 original) each way, BC1
        (4 bits a texel) when opaque, BC3 (8) when its alpha carries the key -
        <key>.bc1.ktx2 / <key>.bc3.ktx2; 512 BC1 with its mips is 171 KB,
        LESS than the 256 KB the port holds today for a 256 RGBA8 original
      * a light map: at most LIGHT_MAX (256 - a glow mask is low-frequency),
        BC4 - <key>_e.bc4.ktx2
      * the normal maps: NOT made - per-pixel lighting is outside the Vita's
        remaster (its frame has no room for it)
    The levels above the cap are dropped from the full image's own 2x2 chain
    (`tex_encode --max`), so a replacement stays the original's size times a
    power of two. An output newer than its PNG is kept."""
    import concurrent.futures
    src = os.path.realpath(src)
    out = os.path.join(safe_out(root), "pack")
    tool = os.path.join(os.path.dirname(TOOL), "tex_encode")
    if not os.path.exists(tool):
        sys.exit(f"{tool} is missing - run `make build/tex_encode` in engine/ first")
    if not os.path.isdir(src):
        sys.exit(f"{src}: not a directory")
    os.makedirs(out, exist_ok=True)
    work, kept, normals = [], 0, 0
    for name in sorted(os.listdir(src)):
        if not name.endswith(".png") or len(name) not in (20, 22):
            continue
        if name.endswith("_n.png"):
            normals += 1
            continue
        light = name.endswith("_e.png")
        pin = os.path.join(src, name)
        stem = os.path.join(out, name[:-4])
        made = [stem + e for e in ((".bc4.ktx2",) if light else (".bc1.ktx2", ".bc3.ktx2"))]
        if any(os.path.exists(m) and os.path.getmtime(m) >= os.path.getmtime(pin) for m in made):
            kept += 1
            continue
        for m in made:
            if os.path.lexists(m):
                os.remove(m)
        cmd = [tool, "--max", str(light_max if light else colour_max)] + \
            (["bc4", pin, made[0]] if light else ["dxt", pin, stem])
        work.append(("light" if light else "colour", cmd))
    if not work and not kept:
        sys.exit(f"{src}: no pack PNG read - is it a pack/ directory?")
    print(f"vita: {len(work)} files to make in {out} ({kept} up to date, {normals} normal maps left out), "
          f"colours <= {colour_max}, light maps <= {light_max}, {jobs} at once")
    stats, failed, done = {}, [], 0

    def run(w):
        return w, subprocess.run(w[1], capture_output=True, text=True)
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        for w, r in ex.map(run, work):
            done += 1
            m = re.search(r"@tex_encode (\S+) (\d+)x(\d+) levels \d+ psnr (\S+)", r.stdout)
            if r.returncode != 0 or not m:
                failed.append((w[1][4], r.stderr.strip()[:200]))
                continue
            st = stats.setdefault(m.group(1), [0, 0.0, {}])
            st[0] += 1
            st[1] += float(m.group(4))
            size = f"{m.group(2)}x{m.group(3)}"
            st[2][size] = st[2].get(size, 0) + 1
            if done % 250 == 0:
                print(f"  {done} of {len(work)}")
    for c, (n, p, sizes) in sorted(stats.items()):
        print(f"  {c}: {n} made, mean PSNR {p / n:.1f} dB, sizes "
              + ", ".join(f"{k} x{v}" for k, v in sorted(sizes.items(), key=lambda kv: -kv[1])))
    for f, why in failed[:20]:
        print(f"  FAILED {f}: {why}")
    for ext in (".bc1.ktx2", ".bc3.ktx2", ".bc4.ktx2"):
        files = [n for n in os.listdir(out) if n.endswith(ext)]
        print(f"  {ext[1:4]}: {len(files)} files, "
              f"{sum(os.path.getsize(os.path.join(out, n)) for n in files) / 1e6:.1f} MB")
    if failed:
        sys.exit(f"{len(failed)} failed")


def cmd_ui(root, binary, model, tile, anchor_passes):
    """The interface pack, end to end, into ROOT/ui: `export` (the engine's
    keys from `texpack_tool uikeys`, each BMP padded by its edge, its key -
    pure black - filled from its neighbours and kept as a mask), `upscale`
    (with --bin; skipped when up/ is there), `build` (cropped, ANCHORED to the
    original's colours, the mask thresholded into alpha) -> ROOT/ui/pack, the
    directory `omk-play --ui-pack` reads (or the `--remaster` preset, as
    ROOT/ui/pack). Each step skips what is already done."""
    out = safe_out(os.path.join(root, "ui"))
    if not os.path.exists(TOOL):
        sys.exit(f"{TOOL} is missing - run `make build/texpack_tool` in engine/ first")
    r = subprocess.run([TOOL, "uikeys", omkpaths.data_root()], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("texpack_tool uikeys failed: " + r.stderr.strip())
    for d in ("src", "in", "up", "pack"):
        os.makedirs(os.path.join(out, d), exist_ok=True)
    index, data = {}, omkpaths.data_root()
    for line in r.stdout.split("\n"):
        if not line.strip():
            continue
        key, rel, size = line.split()
        b = read_bmp(os.path.join(data, rel))
        if not b:
            print(f"{rel}: not read - skipped"); continue
        w, h, rgb = b
        keyed = key_mask(rgb, w * h)
        nkey = sum(keyed)
        index[key] = {"path": rel, "w": w, "h": h, "keyed": nkey}
        if os.path.exists(os.path.join(out, "in", key + ".png")):
            continue
        write_png(os.path.join(out, "src", key + ".png"), w, h, rgb, 3)
        fill = dilate(rgb, keyed, w, h, wrap=False) if 0 < nkey < w * h else rgb
        p, pw, ph = padded(fill, w, h, 3, wrap=False)
        write_png(os.path.join(out, "in", key + ".png"), pw, ph, p, 3)
        if nkey:
            mask = bytes(0 if kk else 255 for kk in keyed for _ in range(3))
            mp, _, _ = padded(mask, w, h, 3, wrap=False)
            write_png(os.path.join(out, "in", key + "_mask.png"), pw, ph, mp, 3)
    with open(os.path.join(out, "index.json"), "w") as f:
        json.dump({"pad": PAD, "bitmaps": index}, f, indent=1, sort_keys=True)
    print(f"exported {len(index)} interface bitmaps to {out}")
    todo = [k for k in index if not os.path.exists(os.path.join(out, "up", k + ".png"))]
    if todo:
        if not binary:
            print(f"{len(todo)} not upscaled yet - run again with --bin PATH (realesrgan-ncnn-vulkan)")
            return
        cmd = [binary, "-i", os.path.join(out, "in"), "-o", os.path.join(out, "up"),
               "-n", model, "-s", "4", "-f", "png"]
        if tile:
            cmd += ["-t", str(tile)]
        mdir = os.path.join(os.path.dirname(os.path.abspath(binary)), "models")
        if os.path.isdir(mdir):
            cmd += ["-m", mdir]
        print(" ".join(cmd))
        if subprocess.run(cmd).returncode != 0:
            sys.exit("the upscaler failed")
    made = 0
    for key, e in sorted(index.items()):
        dst = os.path.join(out, "pack", key + ".png")
        if os.path.exists(dst):
            made += 1; continue
        w, h = e["w"], e["h"]
        uw, uh, rgba = read_png(os.path.join(out, "up", key + ".png"))
        s = uw // (w + 2 * PAD)
        if s < 2 or uw != s * (w + 2 * PAD) or uh != s * (h + 2 * PAD) or s & (s - 1):
            print(f"{e['path']}: {uw}x{uh} is not a power-of-two multiple of the padded size - skipped")
            continue
        c = crop(rgba, uw, PAD * s, PAD * s, w * s, h * s, 4)
        rgb = bytearray(3 * w * s * h * s)
        rgb[0::3] = c[0::4]; rgb[1::3] = c[1::4]; rgb[2::3] = c[2::4]
        src = read_bmp(os.path.join(data, e["path"]))[2]
        rgb = anchor(bytes(rgb), src, w, h, s, anchor_passes)
        n = w * s * h * s
        if e["keyed"]:
            mw, mh, mr = read_png(os.path.join(out, "up", key + "_mask.png"))
            mc = crop(mr, mw, PAD * s, PAD * s, w * s, h * s, 4)
            alpha = mc[0::4].translate(bytes(255 if v >= 128 else 0 for v in range(256)))
        else:
            alpha = b"\xff" * n
        write_png(dst, w * s, h * s, keyed_rgba(bytes(rgb), alpha, n), 4)
        made += 1
        print(f"  {e['path']} -> {w * s}x{h * s}")
    print(f"built {made} of {len(index)} into {os.path.join(out, 'pack')}")


def cmd_upscale(out, binary, model, tile):
    out = safe_out(out)
    os.makedirs(os.path.join(out, "up"), exist_ok=True)
    cmd = [binary, "-i", os.path.join(out, "in"), "-o", os.path.join(out, "up"),
           "-n", model, "-s", "4", "-f", "png"]
    if tile:
        cmd += ["-t", str(tile)]
    mdir = os.path.join(os.path.dirname(os.path.abspath(binary)), "models")
    if os.path.isdir(mdir):
        cmd += ["-m", mdir]
    print(" ".join(cmd))
    sys.exit(subprocess.run(cmd).returncode)


def cmd_build(out, nearest_n, solid, match, grain=0.0, anchored=True, anchor_passes=1):
    out = safe_out(out)
    idx = json.load(open(os.path.join(out, "index.json")))
    pad = idx["pad"]
    pack = os.path.join(out, "pack")
    os.makedirs(pack, exist_ok=True)
    need_src = nearest_n or solid
    tex = shipped(match) if need_src else None
    made = skipped = 0
    for k, e in sorted(idx["textures"].items()):
        if match and not any(s.lower() in e["model"].lower() for s in match):
            continue
        w, h = e["w"], e["h"]
        if need_src:
            if k not in tex:
                skipped += 1
                continue
            rgb = tex[k]["rgb"]
            keyed = key_mask(rgb, w * h)
            n = nearest_n or solid[1]
            alpha = nearest(bytes(0 if kk else 255 for kk in keyed), w, h, 1, n)
            col = nearest(rgb, w, h, 3, n) if nearest_n else bytes(solid[0]) * (w * n * h * n)
            write_png(os.path.join(pack, k + ".png"), w * n, h * n,
                      keyed_rgba(col, alpha, w * n * h * n), 4)
            made += 1
            continue
        up = os.path.join(out, "up", k + ".png")
        if not os.path.exists(up):
            skipped += 1
            continue
        uw, uh, rgba = read_png(up)
        s = uw // (w + 2 * pad)
        if s < 1 or uw != s * (w + 2 * pad) or uh != s * (h + 2 * pad) or s & (s - 1):
            print(f"{k}: {uw}x{uh} is not a power-of-two multiple of {w + 2 * pad}x{h + 2 * pad} - skipped")
            skipped += 1
            continue
        c = crop(rgba, uw, pad * s, pad * s, w * s, h * s, 4)
        rgb = bytearray(3 * w * s * h * s)
        rgb[0::3] = c[0::4]; rgb[1::3] = c[1::4]; rgb[2::3] = c[2::4]
        n = w * s * h * s
        if anchored or grain > 0:
            src = source_textures(e["model"])[e["material"]]["rgb"]
        if anchored:
            rgb = bytearray(anchor(bytes(rgb), src, w, h, s, anchor_passes))
        if grain > 0:
            # THE GRAIN KEPT: an AI upscaler denoises, and the painted grain
            # of a floor or a ceiling is part of the art - mix a plain
            # bilinear enlargement of the original back in
            rgb = bytearray(mix(bytes(rgb), bilinear(src, w, h, 3, s), grain))
        if e["keyed"]:
            mup = os.path.join(out, "up", k + "_mask.png")
            if os.path.exists(mup):
                mw, mh, mr = read_png(mup)
                mc = crop(mr, mw, pad * s, pad * s, w * s, h * s, 4)
                alpha = mc[0::4].translate(bytes(255 if v >= 128 else 0 for v in range(256)))
            else:
                src = source_textures(e["model"])[e["material"]]["rgb"]
                alpha = nearest(bytes(0 if kk else 255 for kk in key_mask(src, w * h)), w, h, 1, s)
        else:
            alpha = b"\xff" * n
        write_png(os.path.join(pack, k + ".png"), w * s, h * s, keyed_rgba(bytes(rgb), alpha, n), 4)
        made += 1
    print(f"built {made} replacement textures into {pack} ({skipped} without an upscaled image)")


def main(argv):
    argv = omkpaths.take_flags(list(argv))
    if len(argv) < 3 or argv[1] not in ("export", "upscale", "build", "emissive", "grades", "materials", "curate", "ui", "fonts", "films", "compress", "vita"):
        sys.exit(__doc__)
    cmd, out, rest = argv[1], argv[2], argv[3:]
    match, binary, model, tile, nearest_n, solid, grain, anchored = [], None, None, 0, 0, None, 0.0, True
    anchor_passes = 1
    depth = 1.0
    fonts_k = 8
    film_q = 2
    codec, astcenc, astc_block = "bc", None, "6x6"
    vita_src, colour_max, light_max = None, 512, 256
    deepbump, python, jobs, force = None, None, 3, False
    # a flag without its value, or a value that is not a number: said, with
    # the usage - not a traceback
    try:
        i = 0
        while i < len(rest):
            a = rest[i]
            if a == "--match": match.append(rest[i + 1]); i += 2
            elif a == "--depth": depth = float(rest[i + 1]); i += 2
            elif a == "--k": fonts_k = int(rest[i + 1]); i += 2
            elif a == "--q": film_q = int(rest[i + 1]); i += 2
            elif a == "--codec": codec = rest[i + 1]; i += 2
            elif a == "--from": vita_src = rest[i + 1]; i += 2
            elif a == "--max": colour_max = int(rest[i + 1]); i += 2
            elif a == "--light-max": light_max = int(rest[i + 1]); i += 2
            elif a == "--astcenc": astcenc = rest[i + 1]; i += 2
            elif a == "--astc-block": astc_block = rest[i + 1]; i += 2
            elif a == "--bin": binary = rest[i + 1]; i += 2
            elif a == "--model": model = rest[i + 1]; i += 2
            elif a == "--tile": tile = int(rest[i + 1]); i += 2
            elif a == "--nearest": nearest_n = int(rest[i + 1]); i += 2
            elif a == "--grain": grain = float(rest[i + 1]); i += 2
            elif a == "--no-anchor": anchored = False; i += 1
            elif a == "--anchor-passes": anchor_passes = int(rest[i + 1]); i += 2
            elif a == "--deepbump": deepbump = os.path.abspath(rest[i + 1]); i += 2
            elif a == "--python": python = rest[i + 1]; i += 2
            elif a == "--jobs": jobs = int(rest[i + 1]); i += 2
            elif a == "--force": force = True; i += 1
            elif a == "--solid":
                solid = (tuple(int(v) for v in rest[i + 1].split(",")), int(rest[i + 2])); i += 3
            else: sys.exit(f"unknown argument {a}\n\n{__doc__}")
    except (IndexError, ValueError) as e:
        sys.exit(f"bad arguments ({e})\n\n{__doc__}")
    for n in (nearest_n, solid[1] if solid else 0):
        if n and (n < 1 or n > 16 or n & (n - 1)):
            sys.exit("the factor must be a power of two, 1..16")
    if cmd == "export":
        cmd_export(out, match)
    elif cmd == "ui":
        cmd_ui(out, binary, model or "realesrgan-x4plus", tile, anchor_passes)
    elif cmd == "fonts":
        cmd_fonts(out, fonts_k)
    elif cmd == "compress":
        if codec not in ("bc", "astc", "all"):
            sys.exit("--codec is bc, astc or all")
        cmd_compress(out, codec, astcenc, astc_block, jobs)
    elif cmd == "vita":
        if not vita_src:
            sys.exit("vita needs --from PACK (a desktop pack/ directory)")
        for n in (colour_max, light_max):
            if n < 4 or n & (n - 1):
                sys.exit("--max / --light-max: a power of two, at least 4")
        cmd_vita(vita_src, out, colour_max, light_max, jobs)
    elif cmd == "films":
        cmd_films(out, binary, model or "realesr-animevideov3-x4", film_q)
    elif cmd == "emissive":
        cmd_emissive(out, match)
    elif cmd == "grades":
        cmd_grades(out, depth)
    elif cmd == "curate":
        cmd_curate(out)
    elif cmd == "materials":
        cmd_materials(out, match, deepbump, python, jobs, force)
    elif cmd == "upscale":
        if not binary:
            sys.exit("upscale needs --bin PATH (realesrgan-ncnn-vulkan)")
        cmd_upscale(out, binary, model or "realesrgan-x4plus", tile)
    else:
        cmd_build(out, nearest_n, solid, match, grain, anchored, anchor_passes)


if __name__ == "__main__":
    main(sys.argv)

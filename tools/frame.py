#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Captured frames from the original engine - reading them, and the one
invariant that makes a capture worth trusting.

`goldentrace.py` gives this repo the engine's own DECISIONS. This gives it the
engine's own PIXELS: `goldentrace.py capture` runs the game under CrossOver in
a Wine virtual desktop, grabs the window with `screencapture`, and recovers the
game's 640x480 framebuffer from it. The frames live in `traces/` beside the
logs, and once one is committed **nothing needs CrossOver to use it** - which is
the point. The rig makes an oracle; the file IS the oracle.

WHY THE RECOVERY IS EXACT, AND WHY THAT IS CHECKED RATHER THAN ASSUMED
----------------------------------------------------------------------
On a Retina display `screencapture` returns the window at 2x, so a 640x480 game
frame comes back as 1280x960. That is only useful if the scaling is
NEAREST-NEIGHBOUR, because then every game pixel is exactly one uniform 2x2
block and taking every other pixel recovers the framebuffer bit for bit. It is:
measured over a real capture, **0 of 307200 blocks were non-uniform**.

But that is a fact about one display on one day. A different monitor, a
scaling change, a CrossOver update - any of them could start interpolating, and
the recovered frame would then be *nearly* the framebuffer while every pixel
diff built on it went quietly wrong. That is the failure this repo keeps
running into: a wrong reading applied consistently, which no self-consistent
suite can see. So `recover` REFUSES rather than degrades: if a single block is
non-uniform it raises, and the capture path emits nothing.

WHAT A FRAME IS AND IS NOT EVIDENCE ABOUT
-----------------------------------------
It is CrossOver's rasterisation, not a 1999 3D card's.

* **2D is exact.** I2D ends every primitive in an `IDirectDrawSurface::Blt`,
  and a Blt is a memory copy with an optional colour key - there is no
  filtering to differ. Text, the tile-map background, the menus, the HUD and
  every blit the interface makes are the framebuffer the original would have
  produced.
* **3D is not, in the low bits.** Texture filtering, dithering and the fog
  table are the driver's, and Wine's are not a Voodoo's. A 3D frame is
  evidence about GEOMETRY and ORDERING - what is on screen, where, in front of
  what - and not about every pixel's exact value.

Anything built on top of this should say which of the two it is relying on.
"""
import os, struct, sys, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The game's own resolution, and the scale a Retina capture comes back at.
GAME_W, GAME_H = 640, 480


class NotExact(Exception):
    """A capture whose blocks are not uniform: it is not the framebuffer."""


# ---------------------------------------------------------------- PNG in/out
#
# Written here rather than taken from PIL because `verify.py` has no PIL
# dependency and should not grow one for a check that reads one file. The
# encoder is the same shape as `tex3dt.png`; the decoder is its inverse and
# handles what `screencapture` actually writes - 8-bit, non-interlaced,
# greyscale or RGB with or without alpha.

def read_png(path):
    """-> (w, h, rgb) with `rgb` a bytes of 3*w*h. Raises on what it cannot read."""
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s: not a PNG" % path)
    i, idat, w, h, depth, ctype, interlace = 8, [], 0, 0, 0, 0, 0
    while i + 8 <= len(d):
        n, tag = struct.unpack_from(">I4s", d, i)
        body = d[i + 8:i + 8 + n]
        if tag == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif tag == b"IDAT":
            idat.append(body)
        elif tag == b"IEND":
            break
        i += 12 + n
    if depth != 8 or interlace != 0:
        raise ValueError("%s: only 8-bit non-interlaced is read (depth %d, "
                         "interlace %d)" % (path, depth, interlace))
    nch = {0: 1, 2: 3, 4: 2, 6: 4}.get(ctype)
    if nch is None:
        raise ValueError("%s: colour type %d" % (path, ctype))
    raw = zlib.decompress(b"".join(idat))
    stride = w * nch
    out = bytearray(3 * w * h)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        # the five PNG filters, in place
        if f == 1:
            for x in range(nch, stride):
                line[x] = (line[x] + line[x - nch]) & 0xFF
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif f == 3:
            for x in range(stride):
                a = line[x - nch] if x >= nch else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif f == 4:
            for x in range(stride):
                a = line[x - nch] if x >= nch else 0
                b = prev[x]
                c = prev[x - nch] if x >= nch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        elif f != 0:
            raise ValueError("%s: filter %d on row %d" % (path, f, y))
        o = 3 * w * y
        if nch == 3:
            out[o:o + 3 * w] = line
        elif nch == 4:
            for x in range(w):
                out[o + 3 * x:o + 3 * x + 3] = line[4 * x:4 * x + 3]
        elif nch == 1:
            for x in range(w):
                out[o + 3 * x] = out[o + 3 * x + 1] = out[o + 3 * x + 2] = line[x]
        else:  # grey + alpha
            for x in range(w):
                out[o + 3 * x] = out[o + 3 * x + 1] = out[o + 3 * x + 2] = line[2 * x]
        prev = line
    return w, h, bytes(out)


def write_png(path, w, h, rgb):
    """The same encoder shape as `tex3dt.png`, kept local so this file stands alone."""
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
    raw = b"".join(b"\0" + rgb[3 * w * y:3 * w * (y + 1)] for y in range(h))
    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b""))


# ------------------------------------------------------------- the recovery

def recover(w, h, rgb, scale=None, want=(GAME_W, GAME_H)):
    """Recover the framebuffer from a `scale`x capture. Raises `NotExact`.

    `scale` is inferred from the wanted size when not given, so a 1x display
    (scale 1) needs no special case and a future 3x one needs no edit.
    """
    tw, th = want
    if scale is None:
        if tw and w % tw == 0 and h % th == 0 and w // tw == h // th:
            scale = w // tw
        else:
            raise NotExact("captured %dx%d is not an integer multiple of %dx%d"
                           % (w, h, tw, th))
    if scale == 1:
        return w, h, rgb
    bad = 0
    for by in range(h // scale):
        for bx in range(w // scale):
            o = 3 * ((by * scale) * w + bx * scale)
            a = rgb[o:o + 3]
            # one count per BLOCK, not per pixel. Assigning to the loop
            # variables does not break a Python `for` - an earlier version did
            # that and reported "395146 of 307200 blocks", a count larger than
            # the number of blocks, which is how the bug announced itself.
            uniform = True
            for dy in range(scale):
                q = 3 * ((by * scale + dy) * w + bx * scale)
                for dx in range(scale):
                    if rgb[q + 3 * dx:q + 3 * dx + 3] != a:
                        uniform = False
                        break
                if not uniform:
                    break
            if not uniform:
                bad += 1
    if bad:
        raise NotExact(
            "%d of %d %dx%d blocks are not uniform - the display is "
            "INTERPOLATING, so this capture is not the framebuffer and no "
            "frame was written. Capture on a 1x display, or set the Wine "
            "desktop to the game's own size." %
            (bad, (w // scale) * (h // scale), scale, scale))
    ow, oh = w // scale, h // scale
    out = bytearray(3 * ow * oh)
    for y in range(oh):
        src = 3 * (y * scale) * w
        dst = 3 * y * ow
        for x in range(ow):
            out[dst + 3 * x:dst + 3 * x + 3] = rgb[src + 3 * x * scale:
                                                   src + 3 * x * scale + 3]
    return ow, oh, bytes(out)


# ------------------------------------------------------------ using a frame

def luma(rgb, i):
    return (rgb[3 * i] * 299 + rgb[3 * i + 1] * 587 + rgb[3 * i + 2] * 114) // 1000


def ink_mask(w, h, rgb, box, above=40):
    """The 'this pixel is text' mask of a region.

    The interface draws text as a COVERAGE level 0..31 blended into a colour
    ramp over whatever is behind it (docs/UI.md 5), so a captured glyph is not
    a flat colour - it is brighter than its own background by an amount the
    ramp sets. The mask is therefore relative: brighter than the region's
    median by `above`. A fixed threshold would work on the menu's dark tile map
    and fail on a light panel.
    """
    x0, y0, x1, y1 = box
    vals = [luma(rgb, y * w + x) for y in range(y0, y1) for x in range(x0, x1)]
    vals_sorted = sorted(vals)
    med = vals_sorted[len(vals_sorted) // 2]
    mw, mh = x1 - x0, y1 - y0
    return mw, mh, bytearray(1 if v > med + above else 0 for v in vals)


def best_overlap(aw, ah, a, bw, bh, b, search=6):
    """Slide `b` over `a` and return (score, dx, dy) at the best alignment.

    Score is intersection over union of the two masks, which is the right
    measure here: it punishes a render that is too fat as hard as one that is
    too thin, where a plain hit count would reward drawing everything.
    """
    best = (0.0, 0, 0)
    for dy in range(-search, search + 1):
        for dx in range(-search, search + 1):
            inter = union = 0
            for y in range(bh):
                ay = y + dy
                if ay < 0 or ay >= ah:
                    union += sum(b[y * bw:(y + 1) * bw]); continue
                for x in range(bw):
                    ax = x + dx
                    pb = b[y * bw + x]
                    pa = a[ay * aw + ax] if 0 <= ax < aw else 0
                    if pa or pb:
                        union += 1
                        if pa and pb:
                            inter += 1
            if union and inter / union > best[0]:
                best = (inter / union, dx, dy)
    return best


# ----------------------------------------------------- finding text in a frame
#
# The interface draws a glyph as a COVERAGE level 0..31 into a colour ramp
# (docs/UI.md 5), and the menu's ramp is neutral grey to white. The tile-map
# background behind it is strongly coloured - teal and rust - so **saturation
# separates the text from the scene** where brightness alone does not: the
# background has bright patches, and a luma-only rule picks them up as glyphs.
# It also excludes the orange title bitmap, which is what makes the four bands
# come out as exactly four.

def neutral_ink(w, h, rgb, minl=90, maxsat=26, box=None):
    """-> {(y, x)} of pixels bright enough and near-grey enough to be text."""
    x0, y0, x1, y1 = box or (0, 0, w, h)
    out = set()
    for y in range(y0, y1):
        for x in range(x0, x1):
            i = y * w + x
            r, g, b = rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2]
            if max(r, g, b) - min(r, g, b) <= maxsat and luma(rgb, i) > minl:
                out.add((y, x))
    return out


def text_bands(w, h, rgb, minl=90, maxsat=26, minrows=6):
    """-> [(y0, y1)] of the runs of rows that carry text.

    Derived from the frame rather than given: a check that hard-coded the
    label positions would pass even if the engine drew them somewhere else.
    """
    ink = neutral_ink(w, h, rgb, minl, maxsat)
    rows = [0] * h
    for y, _ in ink:
        rows[y] += 1
    bands, y = [], 0
    while y < h:
        if rows[y]:
            s = y
            while y < h and rows[y]:
                y += 1
            if y - s >= minrows:
                bands.append((s, y))
        else:
            y += 1
    return bands


def ramp_peak(w, h, rgb, band, maxsat=26):
    """The brightest near-grey pixel in a band - the top of that item's ramp.

    The menu's focused row peaks at 255 and the other three at 124, so this is
    how the focus highlight reads out of a frame: it is a BRIGHTNESS, and the
    glyphs are otherwise identical.
    """
    y0, y1 = band
    best = 0
    for y in range(y0, y1):
        for x in range(w):
            i = y * w + x
            r, g, b = rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2]
            if max(r, g, b) - min(r, g, b) <= maxsat:
                best = max(best, luma(rgb, i))
    return best


# ---------------------------------------------- the SILHOUETTE metric (3D)
#
# Everything above is for a 2D frame, where the header's first section says the
# pixels are EXACT: a Blt is a memory copy and text can be asserted value for
# value. A 3D frame is the other case, and it needs a different instrument.
#
# `docs/PORTING.md` B5/B6: a captured 3D frame is evidence about GEOMETRY and
# ORDERING and not about a pixel's low bits, because filtering, dithering and
# the fog table are the driver's and Wine's are not a Voodoo's. The capture
# says so about itself - between the two frames where dialog 402's camera is
# PARKED, so the scene is the same scene, **42% of pixels differ by <= 8**.
# Per-pixel comparison is therefore not merely discouraged here, it is refuted
# by the data. What survives is where an edge is, and where there is nothing.
#
# Three properties of this instrument, each of which cost something to learn:
#
# * **It is DIRECTED, render -> capture.** A capture's own strongest gradients
#   are substantially dither and filter noise, which the render cannot and
#   should not reproduce; and the capture carries a character, props and a
#   subtitle that a set-only render has no business drawing. So the question
#   asked is "is every edge the render draws an edge the original also has",
#   never the reverse. The reverse would score a correct render down for the
#   content it is honest about missing.
# * **The density is NORMALISED, not thresholded.** A fixed gradient cut gives
#   the crisp render and the filtered capture edge sets of wildly different
#   size, and the score then measures the threshold. `edge_map` takes the
#   strongest `frac` of pixels from each, so both sides bring the same number
#   of edges to the comparison.
# * **It has a FLOOR, and the floor is reported.** Two dense edge maps overlap
#   by chance, and the amount depends on the density and the tolerance, so a
#   bare score is uninterpretable. `edge_chance` measures the same statistic at
#   large horizontal shifts - the same two images, deliberately misaligned -
#   and a check that quotes the score without it is quoting a number with no
#   scale.


def gradient(w, h, rgb):
    """-> [|dL/dx| + |dL/dy|] per pixel, 0 on the border."""
    L = [luma(rgb, i) for i in range(w * h)]
    g = [0] * (w * h)
    for y in range(1, h - 1):
        base = y * w
        for x in range(1, w - 1):
            i = base + x
            g[i] = abs(L[i + 1] - L[i - 1]) + abs(L[i + w] - L[i - w])
    return g


def edge_map(w, h, rgb, frac=0.05):
    """-> ([edge pixel indices], threshold) - the strongest `frac` of gradients.

    Density-normalised rather than thresholded: see the note above. Ties at
    the threshold are all kept, so the set can be slightly larger than `frac`.
    """
    g = gradient(w, h, rgb)
    order = sorted(g, reverse=True)
    k = int(len(order) * frac)
    thr = max(order[k] if k < len(order) else 0, 1)
    return [i for i, v in enumerate(g) if v >= thr], thr


def _dilated(w, h, edges, radius, margin):
    """`edges` grown by `radius`, into a bytearray padded by `margin` a side."""
    pw = w + 2 * margin
    m = bytearray(pw * (h + 2 * margin))
    for i in edges:
        y, x = divmod(i, w)
        for dy in range(-radius, radius + 1):
            row = (y + margin + dy) * pw + x + margin
            for dx in range(-radius, radius + 1):
                m[row + dx] = 1
    return m, pw


def edge_match(w, h, a, b, radius=2, search=4):
    """-> (score, dx, dy): the fraction of `a`'s edges within `radius` of `b`'s.

    `a` is the RENDER's edge list and `b` the CAPTURE's; the direction is not
    symmetric and matters (see above). The best whole-pixel offset in
    +/-`search` is taken, because a capture is a frame of a running game and
    the parked camera is parked to within a pixel or two, not exactly.
    """
    if not a:
        return 0.0, 0, 0
    margin = radius + search + 1
    m, pw = _dilated(w, h, b, radius, margin)
    idx = [(i // w + margin) * pw + (i % w + margin) for i in a]
    best = (0.0, 0, 0)
    for dy in range(-search, search + 1):
        for dx in range(-search, search + 1):
            off = dy * pw + dx
            hit = 0
            for i in idx:
                hit += m[i + off]
            s = hit / len(a)
            if s > best[0]:
                best = (s, dx, dy)
    return best


def edge_chance(w, h, a, b, radius=2, shifts=(80, 160, 240)):
    """The same statistic at large shifts - what two edge maps score by luck.

    Reported alongside every score. Without it a match of 0.7 could be an
    agreement or could be what any two edge maps of this density give.
    """
    if not a:
        return []
    margin = radius + max(shifts) + 1
    m, pw = _dilated(w, h, b, radius, margin)
    out = []
    for sh in shifts:
        hit = 0
        for i in a:
            hit += m[(i // w + margin) * pw + (i % w + margin) + sh]
        out.append(hit / len(a))
    return out


def lit_mask(w, h, rgb):
    """-> [bool] per pixel: did anything draw here at all?

    The COVERAGE half of the metric. A render of a set alone leaves holes where
    the set has none - a doorway, a window, the gap above a wall - and the
    engine's own frame is BLACK in exactly those places, because the game does
    not clear to a sky either. That makes the holes a shape the two can be
    compared on which owes nothing to a pixel's value.
    """
    return [bool(rgb[3 * i] or rgb[3 * i + 1] or rgb[3 * i + 2])
            for i in range(w * h)]


def hole_darkness(w, h, rgb, holes, cut=32):
    """Of the pixels `holes` names, what fraction of `rgb` is below `cut`.

    Quote it against the same fraction over the WHOLE frame, never alone: a
    dark frame makes any set of pixels look dark, and the claim is that the
    holes are darker than the frame is, not that they are dark.
    """
    if not holes:
        return 0.0
    return sum(luma(rgb, i) < cut for i in holes) / len(holes)


def iou_at_best(mask, rw, rh, rmask, ox, oy, search=2):
    """Slide a rendered mask over a frame mask; -> the best intersection/union.

    IoU rather than a hit count, because a hit count rewards a render that
    draws too much: only IoU punishes fat and thin equally.
    """
    ys = [y for y in range(rh) if any(rmask[y * rw + x] for x in range(rw))]
    xs = [x for x in range(rw) if any(rmask[y * rw + x] for y in range(rh))]
    if not ys or not xs:
        return 0.0
    best = 0.0
    for dy in range(-search, search + 1):
        for dx in range(-search, search + 1):
            inter = union = 0
            for y in range(ys[0], ys[-1] + 1):
                for x in range(xs[0], xs[-1] + 1):
                    a = rmask[y * rw + x]
                    b = (y + oy + dy, x + ox + dx) in mask
                    if a or b:
                        union += 1
                        if a and b:
                            inter += 1
            if union and inter / union > best:
                best = inter / union
    return best


if __name__ == "__main__":
    if len(sys.argv) > 1:
        w, h, rgb = read_png(sys.argv[1])
        print("%s: %dx%d" % (sys.argv[1], w, h))
        try:
            ow, oh, _ = recover(w, h, rgb)
            print("recovers exactly to %dx%d" % (ow, oh))
        except NotExact as e:
            print("NOT EXACT: %s" % e)

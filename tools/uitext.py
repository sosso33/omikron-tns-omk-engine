# SPDX-License-Identifier: GPL-3.0-or-later
r"""Render a line of interface text the way the engine renders it.

Everything here is `Text_LayOutBlock` (0x0043F3E0) and `Text_DrawRun`
(0x0043EA10) followed rather than approximated - the same fonts out of
`gamedata/FONTS`, the same markup, the same 120%-of-line-height spacing, the same
coverage ramp. docs/UI.md 5.

    python3 tools/uitext.py "{fC}Consulter le dossier n{°}1"
    python3 tools/uitext.py --png out.png --font J --width 500 "Nouvelle partie"
"""
import os, sys, struct

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fnt, tex3dt

#: `Text_LayOutBlock`'s switch over the characters 8..27 has nothing to do with
#: this one: `{` opens a DIRECTIVE and a single letter is a command. Several
#: chain inside one brace, and an UNRECOGNISED letter falls through to the line
#: flush - which is how `{P}` works as a paragraph break without being
#: implemented.
ALIGN = {"G": 2, "D": 4, "C": 8, "F": 16}      # 4 = right, 8 = centre, else left
VALIGN = {"H": 1, "L": 2, "M": 3}

_FONTS = {}


def font(letter):
    if letter not in _FONTS:
        rec = next((r for r in fnt.table() if r["letter"] == letter), None)
        if rec is None:
            return None
        _FONTS[letter] = (rec, fnt.load(rec["name"]))
    return _FONTS[letter]


def parse(text, font_letter="J", rgb=(255, 255, 255)):
    """-> [(char, font letter, (r,g,b))], plus the block's alignment or None.

    `{I<9 decimal digits>}` is three 3-digit components, NOT a hex triple;
    `{f<letter>}` names a font by the id letter of the 13-record table.
    """
    out, align = [], None          # None = the string set none; the item's wins
    i, n = 0, len(text)
    cur_f, cur_c = font_letter, rgb
    while i < n:
        c = text[i]
        if c == "{":
            i += 1
            while i < n and text[i] != "}":
                d = text[i]
                if d == "f" and i + 1 < n:
                    cur_f = text[i + 1]; i += 2; continue
                if d == "I" and i + 9 < n and text[i+1:i+10].isdigit():
                    cur_c = (int(text[i+1:i+4]), int(text[i+4:i+7]),
                             int(text[i+7:i+10])); i += 10; continue
                if d == "X" and i + 6 < n and text[i+1:i+7].isdigit():
                    i += 7; continue          # a move; the caller owns the box
                if d in ALIGN:
                    align = ALIGN[d]; i += 1; continue
                if d in VALIGN or d in "BEg":
                    i += 2 if d == "E" else 1; continue
                i += 1                        # anything else: ignored here
            i += 1                            # the closing brace
            continue
        if c in "[]":                         # the counted spans; no styling here
            i += 1; continue
        out.append((c, cur_f, cur_c))
        i += 1
    return out, align


def measure(run):
    """The advance of a run, in pixels."""
    w = 0
    for ch, f, _ in run:
        fo = font(f)
        if not fo:
            continue
        rec, ff = fo
        g = ff.glyph.get(ord(ch))
        w += (g[2] if g else rec["default_advance"]) + rec["kern"]
    return w


def render(text, width=None, font_letter="J", rgb=(255, 255, 255), align=None,
           lit=True):
    """-> (w, h, rgba) with the text drawn as the engine draws it.

    A glyph pixel is a COVERAGE level 0..31 (docs/UI.md 5), so it becomes the
    alpha and the colour comes from the style - which is what lets one glyph
    sheet serve every colour. `lit=False` halves each channel, the shift
    `Ui_ItemTextStyle` applies to every unselected row.
    """
    run, a = parse(text, font_letter, rgb)
    # The markup is read DURING layout, after the parameter block has been
    # applied, so a `{C}` in the string wins over the item's own flags. With
    # no directive the item's alignment stands, and with neither it is 2 -
    # `Text_DrawBlock`'s initialised default, which the renderer reads as left.
    if a is None:
        a = align if align is not None else 2
    if not run:
        return None
    base_rec = font(run[0][1]) or font(font_letter)
    if base_rec is None:
        return None
    line_h = max((font(f)[0]["height"] for _, f, _ in run if font(f)),
                 default=base_rec[0]["height"])
    step = 120 * line_h // 100
    # Word wrap, on the block width. `Text_LayOutBlock`'s character loop skips
    # 13 and ends the line on 10, so an explicit CRLF is one break.
    lines, cur, last_space = [], [], None
    for item in run:
        if item[0] == "\r":
            continue
        if item[0] == "\n":
            lines.append(cur); cur = []; last_space = None
            continue
        cur.append(item)
        if item[0] == " ":
            last_space = len(cur)
        if width and measure(cur) > width:
            if last_space:
                lines.append(cur[:last_space]); cur = cur[last_space:]
            else:
                lines.append(cur[:-1]); cur = cur[-1:]
            last_space = None
    if cur:
        lines.append(cur)
    W = width or max((measure(l) for l in lines), default=1)
    H = max(step * len(lines), step)
    buf = bytearray(W * H * 4)
    for li, line in enumerate(lines):
        lw = measure(line)
        x = 0 if a not in (4, 8) else (W - lw if a == 4 else (W - lw) // 2)
        baseline = li * step + line_h
        for ch, f, col in line:
            fo = font(f)
            if not fo:
                continue
            rec, ff = fo
            g = ff.glyph.get(ord(ch))
            if not g:
                x += rec["default_advance"] + rec["kern"]
                continue
            gw, gh, bottom, rows = ff.pixels(ord(ch))
            top = baseline + bottom - gh
            r, gr, b = col if lit else tuple(v >> 1 for v in col)
            for yy, row in enumerate(rows):
                py = top + yy
                if not (0 <= py < H):
                    continue
                for xx, cov in enumerate(row):
                    if not cov:
                        continue
                    px = x + xx
                    if not (0 <= px < W):
                        continue
                    o = (py * W + px) * 4
                    k = min(cov, fnt.RAMP) / fnt.RAMP
                    buf[o] = int(r * k); buf[o+1] = int(gr * k)
                    buf[o+2] = int(b * k); buf[o+3] = min(255, int(cov * 255 / fnt.RAMP))
            x += gw + rec["kern"]
    return W, H, bytes(buf)


def png(text, **kw):
    r = render(text, **kw)
    if not r:
        return None
    W, H, rgba = r
    return tex3dt.png_rgba(W, H, rgba) if hasattr(tex3dt, "png_rgba") else _png_rgba(W, H, rgba)


def _png_rgba(w, h, rgba):
    """A 32-bit PNG - tex3dt.png writes 24-bit, and the text needs its alpha."""
    import zlib
    raw = b"".join(b"\0" + rgba[y*w*4:(y+1)*w*4] for y in range(h))
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6))
            + chunk(b"IEND", b""))


if __name__ == "__main__":
    args = sys.argv[1:]
    out = None
    if "--png" in args:
        i = args.index("--png"); out = args[i+1]; del args[i:i+2]
    f = "J"
    if "--font" in args:
        i = args.index("--font"); f = args[i+1]; del args[i:i+2]
    w = None
    if "--width" in args:
        i = args.index("--width"); w = int(args[i+1]); del args[i:i+2]
    txt = args[0] if args else "Nouvelle partie"
    if out:
        open(out, "wb").write(png(txt, width=w, font_letter=f))
        print("wrote", out)
    else:
        r = render(txt, width=w, font_letter=f)
        W, H, rgba = r
        shades = " .:-=+*#%@"
        for y in range(H):
            print("".join(shades[min(9, rgba[(y*W+x)*4+3] * 10 // 256)]
                          for x in range(W)))

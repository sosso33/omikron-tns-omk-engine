#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""`FONTS/*.FNT` - the interface's proportional, anti-aliased bitmap fonts.

The engine keeps a 13-entry font table compiled into `gamedata/Runtime 2.exe` at
0x004C7090 (20 bytes a record) and loads each one's glyphs from
`fonts\<NAME>.FNT` - the format string is in the binary, and all 13 names ship
in `gamedata/FONTS`. `Text_DrawBlock` names a font by the letter at +0 of that
record, which is why an item's +36 is 74: 'J', the JOURNAL face.

A `.FNT` is one blob:

    +0      256 glyph records of 8 bytes, indexed by the character code
              +0  u16  the pixel data's offset in EIGHT-BYTE UNITS; 0 = the
                       glyph is absent, and the renderer falls back to the
                       font record's default advance
              +2  i16  the bottom edge, relative to the baseline
              +4  i16  the width - and, plus the font's kerning, the advance
              +6  i16  the height
    +2048   the pixel data: width x height bytes a glyph, row-major, top row
            first, one byte per pixel holding a COVERAGE level 0..31

The coverage is the whole colour model. `sub_43EA10` builds a 32-entry ramp of
the requested colour once (`word_52F5B8`, which IDA itself types `[32]`) and
each non-zero pixel byte indexes it, so a glyph is greyscale antialiasing that
takes the text's colour at draw time. Zero is transparent.

    python3 tools/fnt.py                  # the 13 fonts and their metrics
    python3 tools/fnt.py journal A g E     # draw glyphs as ASCII
    python3 tools/fnt.py --selftest
"""
import omkpaths
import os, sys, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from ui_tables import Exe

FONTS_DIR = omkpaths.data("FONTS")
FONT_TABLE = (0x004C7090, 20)   # walked by Font_Find; a zero +0 ends it
GLYPHS = 256                    # the renderer indexes with a uint8
HEADER = GLYPHS * 8
RAMP = 32                       # word_52F5B8[32]


def table(e=None):
    """The 13 font records, in the order Font_Find scans them.

    +0 the id LETTER, +4 the file stem, +8 the kerning added to every advance,
    +10 the advance used for a character the font has no glyph for, +12 the
    line height (the renderer steps 120% of it between lines), +16 the loaded
    blob at run time.
    """
    e = e or Exe()
    base, stride = FONT_TABLE
    out = []
    for i in range(64):
        r = e.read(base + stride * i, stride)
        if r[0] == 0:
            break
        out.append(dict(index=i, id=r[0], letter=chr(r[0]),
                        name=e.cstr(struct.unpack_from("<I", r, 4)[0]),
                        kern=struct.unpack_from("<h", r, 8)[0],
                        default_advance=struct.unpack_from("<h", r, 10)[0],
                        height=struct.unpack_from("<h", r, 12)[0]))
    return out


class Font:
    """One parsed `.FNT`."""

    def __init__(self, path):
        self.path = path
        self.d = open(path, "rb").read()
        self.glyph = {}
        for c in range(GLYPHS):
            off, bottom, w, h = struct.unpack_from("<Hhhh", self.d, 8 * c)
            if off:
                self.glyph[c] = (off, bottom, w, h)

    def pixels(self, c):
        """(width, height, bottom, rows) - rows of coverage bytes, top first."""
        off, bottom, w, h = self.glyph[c]
        b = 8 * off
        blk = self.d[b:b + w * h]
        return w, h, bottom, [blk[r * w:(r + 1) * w] for r in range(h)]

    def extent(self, c):
        off, bottom, w, h = self.glyph[c]
        return 8 * off, 8 * off + w * h

    def ascii(self, c, shades=" .:-=+*#%@"):
        w, h, bottom, rows = self.pixels(c)
        n = len(shades) - 1
        return ["".join(shades[min(n, p * (n + 1) // RAMP)] for p in r)
                for r in rows]


def load(stem):
    return Font(os.path.join(FONTS_DIR, stem.lower() + ".fnt"))


# ------------------------------------------------------------------ selftest
def selftest():
    """Checks the data can fail: every glyph's pixels inside the file and
    after the header, no block overlapping another, and coverage in range."""
    e = Exe()
    fail = []
    def ck(ok, msg):
        print(("  ok   " if ok else "  FAIL ") + msg)
        if not ok:
            fail.append(msg)

    t = table(e)
    ck(len(t) == 13, "%d records in the font table, want 13" % len(t))
    shipped = {f.lower() for f in os.listdir(FONTS_DIR) if f.lower().endswith(".fnt")}
    named = {r["name"].lower() + ".fnt" for r in t}
    ck(named == shipped, "the %d names ARE the %d shipped .fnt files"
       % (len(named), len(shipped)))
    ck(len({r["letter"] for r in t}) == 13, "13 distinct id letters: %s"
       % "".join(sorted(r["letter"] for r in t)))
    byid = {r["id"]: r["name"] for r in t}
    ck(byid.get(74) == "JOURNAL" and byid.get(76) == "SMALL"
       and byid.get(83) == "SNEAK",
       "the three ids the interface actually names resolve: 74 JOURNAL "
       "(Text_DrawBlock's default and every option row), 76 SMALL (its "
       "sub-640x480 override) and 83 SNEAK (Opt_BindRow's headings)")

    tot = ncount = over = outside = overlap = past = 0
    cover, starts = set(), set()
    for r in t:
        f = load(r["name"])
        ncount += len(f.glyph)
        spans = []
        for c in f.glyph:
            s, en = f.extent(c)
            spans.append((s, en))
            if s < HEADER or en > len(f.d):
                outside += 1
            w, h, bottom, rows = f.pixels(c)
            for row in rows:
                tot += len(row)
                over += sum(1 for p in row if p >= RAMP)
        spans.sort()
        prev = HEADER
        for s, en in spans:
            if s < prev:
                overlap += 1
            prev = max(prev, en)
        past += len(f.d) - prev
        cover.add(frozenset(f.glyph))
        starts.add(spans[0][0])

    ck(ncount == 13 * 223, "%d glyphs, 223 in every font" % ncount)
    ck(cover == {frozenset(range(33, 256))},
       "and every font covers exactly the codes 33..255 - the same set, with "
       "space (32) deliberately absent so it falls to the default advance")
    ck(starts == {HEADER}, "the pixel data starts at byte %d in all 13 (the "
       "glyph table is 256 records however few are used); got %s"
       % (HEADER, sorted(starts)))
    ck(outside == 0, "%d glyph blocks starting before the 2048-byte header or "
       "running past EOF (must be 0 - the test the layout could fail)" % outside)
    ck(overlap == 0, "%d blocks overlapping another (must be 0)" % overlap)
    ck(past <= 200, "at most 200 bytes unaccounted across all 13 files "
       "(alignment padding); got %d" % past)
    ck(tot == 485877, "%d pixel bytes" % tot)
    ck(over == 2, "%d of them index past the 32-entry ramp - and it is "
       "exactly two, both in SMALL's '!' (see docs/UI.md 5)" % over)
    # The markup. sub_43F3E0 reads `{f<id>}` as a raw byte naming a font and
    # `{I<9 decimal digits>}` as an RGB triple; the shipped text has to agree.
    import re, collections
    iam = omkpaths.data("IAM")
    names = ["Menu", "Options", "Save", "Pause", "HScore", "Buy", "Arch",
             "Term", "Morg", "Surv", "Lift", "Fsim", "Multip", "Meca",
             "Sneak", "Den", "Gand", "Shoot", "OBJECT", "DIALOG"]
    ids = {chr(r["id"]) for r in t}
    used, colours = collections.Counter(), 0
    for n in names:
        p = os.path.join(iam, n)
        if not os.path.exists(p):
            continue
        b = open(p, "rb").read()
        for m in re.finditer(rb"\{f(.)\}", b):
            used[chr(m.group(1)[0])] += 1
        colours += len(re.findall(rb"\{I[0-9]{9}\}", b))
    ck(sum(used.values()) == 329 and set(used) <= ids,
       "%d `{f<id>}` directives in the shipped text, every operand one of the "
       "13 font letters (%s) - none names a font that does not exist. (The "
       "loose form `{fCC}` chains a second command inside the same brace, so "
       "only the strict one is counted; OBJECT and DIALOG carry binary too.)"
       % (sum(used.values()), "".join(sorted(used))))
    ck(colours == 111,
       "%d `{I<9 decimal digits>}` colours - NINE DECIMAL digits, RRRGGGBBB, "
       "not a hex triple; the count is every well-formed one, so a mis-read "
       "of the digit grouping would not reach it" % colours)

    print("\n%d failures" % len(fail))
    return len(fail)


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if len(argv) < 2:
        for r in table():
            print("%s  %-9s kern=%-3d default=%-3d line=%-3d"
                  % (r["letter"], r["name"], r["kern"], r["default_advance"],
                     r["height"]))
        return 0
    f = load(argv[1])
    for s in (argv[2:] or ["A"]):
        c = ord(s) if len(s) == 1 else int(s)
        if c not in f.glyph:
            print("%r: absent" % s)
            continue
        off, bottom, w, h = f.glyph[c]
        print("%r code=%d  %dx%d  bottom=%+d  data at %d" % (s, c, w, h, bottom, 8 * off))
        for line in f.ascii(c):
            print("   |" + line + "|")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

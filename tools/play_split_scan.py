#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""How coupled is each section of `play.cpp`'s main to the rest of it?

    python3 tools/play_split_scan.py [engine/backends/sdl/play.cpp]

`play.cpp` is one `main` of ~18800 lines: ~900 locals declared at its top
level, then one `for (;;)` whose body is ~14400 lines, with 95 `[&]` lambdas
reaching into all of it (`todo/play-split.md`). Before a section can become a
function, the question is what it would have to be HANDED - and that is the
set of main's top-level names it reads or writes.

This lists the sections (the file's own `// ---- TITLE` banners at the loop
body's indentation, and the setup's), their size, and how many of main's
top-level names each references - with the names, when asked (`-v`). A
section that touches few names is a cheap extraction; one that touches
hundreds needs the state object first.

A HEURISTIC, and it says so: names are found by a regex over declarations at
main's indentation (4 spaces), and a reference is a whole-word match - a
local in an inner scope that SHADOWS a main local is counted as a reference.
It over-counts, never under-counts, which is the safe side for sizing.
Stdlib only; prints only.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT = HERE.parent / "engine" / "backends" / "sdl" / "play.cpp"

DECL = re.compile(
    r"^    (?!return\b|if\b|for\b|while\b|else\b|switch\b|case\b|break\b|continue\b|do\b)"
    r"(?:static\s+)?(?:const\s+)?(?:constexpr\s+)?"
    r"[A-Za-z_][\w:<>,\s\*&]*?[\s\*&]"
    r"([A-Za-z_]\w*)\s*(?:=|\{|;|\[|\()")
STRUCT = re.compile(r"^    (?:struct|class)\s+([A-Za-z_]\w*)")
BANNER = re.compile(r"^( {4}| {8})// ---- (.*?)\s*-*\s*$")
IDENT = re.compile(r"[A-Za-z_]\w*")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    verbose = "-v" in sys.argv
    path = Path(args[0]) if args else DEFAULT
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()

    start = next(i for i, l in enumerate(lines) if l.startswith("int main("))
    loop = next(i for i in range(start, len(lines)) if lines[i].startswith("    for (;;) {"))

    names = {}
    for i in range(start + 1, loop):
        l = lines[i]
        m = STRUCT.match(l) or DECL.match(l)
        if m and m.group(1) not in names:
            names[m.group(1)] = i + 1
    # The loop body declares its own per-frame locals at 8 spaces; those are
    # the loop's, not main's, and are left out on purpose.

    # Sections: banners at 4 spaces before the loop, 8 spaces inside it.
    marks = []
    for i, l in enumerate(lines):
        if i <= start:
            continue
        m = BANNER.match(l)
        if not m:
            continue
        indent = len(m.group(1))
        if (i < loop and indent == 4) or (i > loop and indent == 8):
            marks.append((i, m.group(2)[:60]))
    marks.append((len(lines), "(end)"))

    print(f"main at line {start + 1}, loop at {loop + 1}, {len(lines)} lines; "
          f"{len(names)} top-level names in main")
    print(f"{'line':>6} {'size':>6} {'names':>6}  section")
    for (a, title), (b, _) in zip(marks, marks[1:]):
        used = set()
        for l in lines[a:b]:
            code = l.split("//", 1)[0]
            for w in IDENT.findall(code):
                if w in names:
                    used.add(w)
        where = "loop" if a > loop else "setup"
        print(f"{a + 1:>6} {b - a:>6} {len(used):>6}  [{where}] {title}")
        if verbose and used:
            print("               " + " ".join(sorted(used)))


if __name__ == "__main__":
    main()

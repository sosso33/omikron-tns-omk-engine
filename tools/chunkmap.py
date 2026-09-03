#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Account for every byte of every `IAM\AREA` / `IAM\SCENE` chunk.

Why this exists. Twice on 2026-08-29 a confident negative result turned out to
be a fact about an *enumeration* rather than about the game: "nothing starts
Impasse's beats" was really "no script I enumerate does", and the driver was
the startup script at `+4` that no record table names. A negative result over a
corpus is only as strong as the walk behind it, so this walks the other way -
it claims every byte that a documented structure explains and reports what is
left.

    python3 tools/chunkmap.py             # the summary
    python3 tools/chunkmap.py --gaps      # every unclaimed run, classified
    python3 tools/chunkmap.py AREA 45     # one chunk

What claims a byte:

* the fixed header;
* the eight tables, each `(pointer, count, stride)` read from the loaders
  themselves - `Area_Load` (0x0040CC90) and `Scene_Load` (0x0040C120) relocate
  the same nine pointer fields and then loop over each with its count, and
  AREA's offsets are SCENE's plus 32 throughout (except `+4`, shared);
* every executable script, marked by **reachability** rather than by a linear
  decode. That matters: a script can jump forward over its own `end`, and
  marking only as far as the first `op3` leaves the tail looking unclaimed -
  which is exactly what AREA 59 did on the first run.

The result is the point: after this, 330 chunks hold **two** unclaimed runs of
bytecode, both of which decode clean and land exactly on their boundary, and
nothing in the corpus references either.
"""
import omkpaths
import os, sys, struct, collections

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "sim"))

import dialog_triggers as T
import dialog_disasm as D

# name, AREA pointer, AREA count, stride, bytes before record 0 (always 0).
# SCENE subtracts 32 from both offsets. Read from the loaders' own loops.
#
# `Area_Load` walks the object and prop tables as `u32(v2, 40) + 8` stepping
# 20 / 24 - the `+ 8` is a field offset INSIDE each record, not a table header.
# The corpus settles it: with the 8 counted the tables overlap their neighbour
# by exactly 8 bytes in 310 of 330 chunks; without it they abut exactly.
TABLES = [("object",     40, 72,  20, 0),
          ("prop",       44, 74,  24, 0),
          ("zone",       48, 76,  68, 0),
          ("propasset",  52, 78,  24, 0),
          ("char",       56, 80, 276, 0),
          ("address",    60, 82,  16, 0),
          ("camera",     64, 84,  44, 0),
          ("subs",       68, 86,   8, 0)]

HEADER = {"AREA": 176, "SCENE": 144}

HDR    = 1
TABLE  = 2
SCRIPT = 3
STRING = 4


def reachable(code, at, limit=None):
    """Every byte a script occupies, following its branches.

    Linear decode stops at the first `end`; a script that jumps over one keeps
    going, so the walk has to as well. Same operand conventions as
    `tools/sim/vm.py`: a branch target is measured from the pc AFTER the 16-bit
    operand, and `case` measures before its label byte.
    """
    n = len(code) if limit is None else limit
    seen, todo, spans = set(), [at], []
    while todo:
        pc = todo.pop()
        while True:
            if pc in seen or pc < 0 or pc >= n: break
            op = code[pc]
            ln = D.oplen(op)
            if ln is None or pc + 1 + ln > n: break
            seen.add(pc)
            spans.append((pc, pc + 1 + ln))
            if op == 3: break                       # end
            if op in (4, 5, 6):
                v = struct.unpack_from("<h", code, pc + 1)[0]
                todo.append(pc + 3 + v)
                if op == 4: break                   # unconditional
            elif op == 42:                          # case label, target
                v = struct.unpack_from("<h", code, pc + 1)[0]
                todo.append(pc + 3 + v)
            pc += 1 + ln
    return spans


def _scripts():
    import vm as V
    out = collections.defaultdict(list)
    for arch, k, rec, f, code, p in V.world_scripts():
        if arch in ("AREA", "SCENE"):
            out[(arch, k)].append((rec, f, p))
    return out


def account(arch, k, b, scripts):
    """-> (claim bytearray, [(name, start, end)])"""
    claim = bytearray(len(b))
    d = 0 if arch == "AREA" else 32
    for i in range(min(HEADER[arch], len(b))): claim[i] = HDR
    marks = []
    for name, po, co, stride, base in TABLES:
        if max(po, co) - d + 4 > len(b): continue
        p = struct.unpack_from("<I", b, po - d)[0]
        n = struct.unpack_from("<h", b, co - d)[0]
        if not p or not (0 < p < len(b)) or n <= 0: continue
        e = min(len(b), p + base + n * stride)
        for i in range(p, e): claim[i] = TABLE
        marks.append((name, p, e))
    # the character records' two strings. `Area_Load`/`Scene_Load` relocate
    # BOTH `+0` and `+4` of every 276-byte character record, and each points at
    # a NUL-terminated string in the chunk - the biography ("Specialiste des
    # armes, entraine au combat rapproche...") and a short line, "Neant." where
    # there is none. 990 of the 1032 shipped records leave both at 0.
    cp = struct.unpack_from("<I", b, 56 - d)[0]
    cn = struct.unpack_from("<h", b, 80 - d)[0]
    if cp and 0 < cp < len(b) and cn > 0:
        for i in range(cn):
            r = cp + 276 * i
            if r + 8 > len(b): break
            for fo in (0, 4):
                v = struct.unpack_from("<I", b, r + fo)[0]
                if not v or not (0 < v < len(b)): continue
                j = b.find(b"\0", v)
                e = (len(b) if j < 0 else j + 1)
                for i2 in range(v, min(len(b), e)): claim[i2] = STRING
                marks.append(("charstr", v, e))

    for rec, f, p in scripts:
        for s, e in reachable(b, p):
            for i in range(max(0, s), min(len(b), e)): claim[i] = SCRIPT
    return claim, marks


def gaps(claim, minrun=8):
    out, i, n = [], 0, len(claim)
    while i < n:
        if claim[i] == 0:
            j = i
            while j < n and claim[j] == 0: j += 1
            if j - i >= minrun: out.append((i, j))
            i = j
        else: i += 1
    return out


def classify(b, s, e):
    """text | padding | bytecode | unknown, from what the bytes are."""
    seg = b[s:e]
    if sum(1 for c in seg if 32 <= c < 127) / len(seg) > 0.55: return "text"
    ins, st = D.disasm(b, s, e)
    # bounding the decode at the run's end makes `disasm` report "ran off the
    # end" for a run that fits exactly, so the terminator test below is the
    # real check and the status is not
    if ins:
        op = ins[-1][1]
        ln = D.oplen(op)
        # a run that decodes clean AND ends exactly on its own boundary, on a
        # terminator - `end`, or an unconditional `jmp` back into claimed code
        if op in (3, 4) and ln is not None and ins[-1][0] + 1 + ln == e:
            return "bytecode"
    if all(c in (0, 0xFF) for c in seg): return "padding"
    return "unknown"


def survey(minrun=8):
    scripts = _scripts()
    rows = []
    for arch in ("AREA", "SCENE"):
        for k, b in sorted(T.archive(omkpaths.data("IAM", arch)).items()):
            claim, _m = account(arch, k, b, scripts[(arch, k)])
            for s, e in gaps(claim, minrun):
                rows.append((arch, k, s, e, classify(b, s, e)))
    return rows


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] in ("AREA", "SCENE"):
        arch, k = a[0], int(a[1])
        b = T.archive(omkpaths.data("IAM", arch))[k]
        claim, marks = account(arch, k, b, _scripts()[(arch, k)])
        print("%s %d: %d bytes" % (arch, k, len(b)))
        for nm, s, e in sorted(marks, key=lambda x: x[1]):
            print("   %-10s [%d, %d)  %d bytes" % (nm, s, e, e - s))
        for s, e in gaps(claim):
            print("   GAP        [%d, %d)  %d bytes  %s"
                  % (s, e, e - s, classify(b, s, e)))
        return 0
    rows = survey()
    by = collections.Counter(r[4] for r in rows)
    tot = sum(len(T.archive(omkpaths.data("IAM", x)))
              for x in ("AREA", "SCENE"))
    print("%d chunks; %d unclaimed runs of >=8 bytes" % (tot, len(rows)))
    for kind, n in by.most_common():
        print("   %-9s %3d runs, %6d bytes"
              % (kind, n, sum(e - s for _a, _k, s, e, c in rows if c == kind)))
    if "--gaps" in a:
        for arch, k, s, e, c in rows:
            print("   %-5s %-4d [%d, %d) %5d B  %s" % (arch, k, s, e, e - s, c))
    else:
        for arch, k, s, e, c in rows:
            if c != "text":
                print("   %-5s %-4d [%d, %d) %5d B  %s"
                      % (arch, k, s, e, e - s, c))
    return 0


if __name__ == "__main__":
    sys.exit(main())

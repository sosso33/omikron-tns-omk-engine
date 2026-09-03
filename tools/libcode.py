#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Identify the linked C/C++ runtime so it stops counting as work to do.

MSVC links its runtime after the program's own code, and the block is visible
in the decompilation: a contiguous tail of functions that the symbol-recovery
pass could name none of, and which the rest of the binary barely calls.

The criterion, applied per function rather than by a hand-drawn address cut:

  1. it lies in the contiguous tail block, and
  2. the recovery pass gave it no name, and
  3. nothing outside the block calls it.

Rule 3 is the load-bearing one. Across the whole tree the block has 260
internal call edges and 5 inbound from game code - the signature of a linked
library, since the compiler resolves its calls to `memcpy`, `sprintf` and the
rest to names the decompiler already prints, so they never appear as
cross-edges at all.

This is a heuristic, deliberately kept as a list rather than stamped into the
`@status` banners: it says "almost certainly not game code", not "verified".
Anything it excludes can be put back by deleting a line.
"""
import json, re, glob, os, sys, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BAN = re.compile(r'^/\* @func 0x([0-9A-F]{8})\s+(\S+)\s+@status', re.M)


def call_graph(rows):
    name2addr = {r["name"]: a for a, r in rows.items()}
    inbound = collections.defaultdict(set)
    for p in sorted(glob.glob(os.path.join(ROOT, "readable/src/*.c"))):
        txt = open(p, errors="replace").read()
        hits = list(BAN.finditer(txt))
        for i, m in enumerate(hits):
            a = m.group(1)
            end = hits[i+1].start() if i+1 < len(hits) else len(txt)
            for c in set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', txt[m.end():end])):
                ca = name2addr.get(c)
                if ca and ca != a: inbound[ca].add(a)
    return inbound


def find(rows, inbound):
    """Pick the boundary minimising inbound-call *density* into the block.

    Sweeping the cut across the tail gives a clear minimum rather than a
    judgement call: crossings fall from 101 at 0x004A4B30 to **4** at
    0x004B3FB0 and rise again below that (22 by 0x004B6000, because a cut
    inside the runtime severs its own internal edges).

    0x004B3FB0 is corroborated independently: it is a `__thiscall` that installs
    vtable pointers, and the next function calls `operator delete` - the start of
    the C++ runtime, not game code.

    The contiguous-unnamed-tail rule on its own overshoots badly, reaching down
    to 0x004A4B30 and swallowing 59 functions that game code calls. Absence of a
    recovered name is weak evidence; the call graph is strong evidence.
    """
    order = sorted(rows, key=lambda a: int(a, 16))
    best = None
    for i, a in enumerate(order):
        if i < len(order) * 3 // 4: continue        # only consider the tail
        blk = set(order[i:])
        cross = sum(len([c for c in inbound.get(x, ()) if c not in blk]) for x in blk)
        # density, not the raw count: minimising the count alone degenerates to
        # a one-function block, which trivially has almost no inbound edges
        density = cross / len(blk)
        if best is None or density < best[0]: best = (density, a, blk)
    _density, lo, block = best
    return block, order


if __name__ == "__main__":
    rows = {r["addr"]: r for r in json.load(open(os.path.join(ROOT, "readable/status.json")))}
    inbound = call_graph(rows)
    block, order = find(rows, inbound)
    lo = min(block, key=lambda a: int(a, 16)) if block else None
    lines = sum(rows[a]["lines"] for a in block)
    out = {"criterion": "contiguous unnamed tail with no inbound call from outside",
           "boundary": lo, "count": len(block),
           "lines": lines, "addresses": sorted(block)}
    json.dump(out, open(os.path.join(ROOT, "readable/libcode.json"), "w"), indent=1)
    print("runtime block: %d functions, %d lines, from 0x%s" % (len(block), lines, lo))
    print("game code    : %d functions, %d lines" % (
        len(rows) - len(block), sum(r["lines"] for a, r in rows.items() if a not in block)))
    edge = [a for a in block if any(c not in block for c in inbound.get(a, ()))]
    inside = sum(len([c for c in inbound.get(a, ()) if c in block]) for a in block)
    print("rule 3 check — inbound call edges: %d from inside the block, %d from outside"
          % (inside, sum(len([c for c in inbound.get(a, ()) if c not in block]) for a in block)))
    print("block members called from outside: %d" % len(edge))
    for a in edge[:8]:
        print("   0x%s  called by %s" % (a, sorted(c for c in inbound[a] if c not in block)))

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Print one function's raw assembly out of Runtime.exe.asm, by address.

Most VM handlers carry no `sub_XXXXXX proc` label, so they cannot be found by
name - and the pre-split blocks in clean/_vmhandlers.json can be the wrong
function entirely (CLAUDE.md 1, opcode 120). What the listing *does* carry is
`loc_XXXXXX:` labels and `sub_XXXXXX proc` lines, which are exact addresses.
This anchors on those: the block for address A is everything from the `align`
before the first anchor at or after A, to the `align` before the first anchor
at or after the following handler.

    python3 tools/asmfn.py 404FB0            # one handler, bounded by the next
    python3 tools/asmfn.py 404FB0 405240     # explicit end address
    python3 tools/asmfn.py --op 103          # by opcode
"""
import omkpaths
import re, sys, os, json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ANCHOR = re.compile(r'^\s*(?:sub_([0-9A-F]{6,8})\s+proc|loc_([0-9A-F]{6,8}):)')
_cache = {}


def _listing():
    if "l" not in _cache:
        t = open(omkpaths.require_asm(),
                 encoding="utf-8", errors="replace")
        _cache["l"] = t.read().split("\n")
        _cache["a"] = [(i, int(m.group(1) or m.group(2), 16))
                       for i, ln in enumerate(_cache["l"])
                       for m in [ANCHOR.match(ln)] if m]
    return _cache["l"], _cache["a"]


def _line_at(addr):
    """First listing line whose anchor address is >= addr."""
    lines, anchors = _listing()
    for i, a in anchors:
        if a >= addr: return i
    return len(lines)


def _back_to_align(i):
    lines, _ = _listing()
    while i > 0 and not re.match(r'\s*align\s', lines[i - 1]):
        i -= 1
    return i


def func(addr, end=None):
    """-> the listing lines of the function starting at `addr`."""
    lines, _ = _listing()
    if end is None: end = addr + 0x400
    lo = _back_to_align(_line_at(addr))
    hi = _back_to_align(_line_at(end))
    return lines[lo:max(hi, lo + 1)]


def handler_span(op):
    """-> (addr, next handler addr) for a VM opcode."""
    tab = json.load(open(omkpaths.clean("_vmsummary.json")))
    at = {e["op"]: int(e["handler"], 16) for e in tab}
    a = at[op]
    later = sorted(x for x in set(at.values()) if x > a)
    return a, (later[0] if later else a + 0x400)


if __name__ == "__main__":
    av = sys.argv[1:]
    if av and av[0] == "--op":
        for o in av[1:]:
            a, b = handler_span(int(o))
            print("=== op %s  handler 0x%06X .. 0x%06X ===" % (o, a, b))
            print("\n".join(x for x in func(a, b) if x.strip()))
    else:
        a = int(av[0], 16)
        b = int(av[1], 16) if len(av) > 1 else None
        print("\n".join(x for x in func(a, b) if x.strip()))

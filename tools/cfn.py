#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Print a function's body out of readable/src/*.c, by address or by name.

    python3 tools/cfn.py 41A350 Actor_Player
"""
import sys, os, re, glob
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BAN = re.compile(r'^/\* @func 0x([0-9A-F]{8})\s+(\S+)\s+@status')

def find(key):
    k = key.upper().replace("0X", "").zfill(8) if re.fullmatch(r'(0x)?[0-9A-Fa-f]{4,8}', key) else None
    for f in sorted(glob.glob(os.path.join(ROOT, "readable/src/*.c"))):
        lines = open(f, encoding="utf-8", errors="replace").read().split("\n")
        starts = [(i, m.group(1), m.group(2)) for i, l in enumerate(lines)
                  for m in [BAN.match(l)] if m]
        for n, (i, addr, name) in enumerate(starts):
            if addr == k or name == key:
                end = starts[n + 1][0] if n + 1 < len(starts) else len(lines)
                return os.path.basename(f), i + 1, lines[i:end]
    return None

if __name__ == "__main__":
    for key in sys.argv[1:]:
        r = find(key)
        if not r: print("!! %s not found" % key); continue
        f, ln, body = r
        while body and not body[-1].strip(): body.pop()
        print("--- %s:%d ---" % (f, ln))
        print("\n".join(body))

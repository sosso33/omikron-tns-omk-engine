#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pull each VM opcode handler out of Runtime.exe.asm.

Hex-Rays decompiled only 4 of the 153 handlers, so the rest have to be read
from the listing. The listing carries no address column, but `sub_XXXXXXX proc`
and `loc_XXXXXX:` labels are exact anchors, and handlers are separated by
`retn` + `align`. Blocks are matched to table entries by anchor, and blocks
without an anchor fall through to the next expected opcode in address order.
"""
import omkpaths
import re, json, sys, collections

tab = json.load(open(omkpaths.clean("_vmtable.json")))
handlers = sorted({e["handler"] for e in tab if e["handler"]})
byhandler = collections.defaultdict(list)
for e in tab: byhandler[e["handler"]].append(e["op"])

t = open(omkpaths.require_asm(), encoding="utf-8", errors="replace").read()
start = t.index("sub_401B00 proc near")
end   = t.index("\noff_4C0140 dd")   # the data definition, not a `call off_4C0140[...]`
lines = t[start:end].split("\n")

ANCHOR = re.compile(r'^(?:(sub_([0-9A-F]{6,8}))\s+proc|loc_([0-9A-F]{6,8}):)')
ALIGN  = re.compile(r'^align\s')
RETN   = re.compile(r'^retn?\b')

blocks, cur, sawret = [], [], False
for ln in lines:
    s = ln.strip()
    if not s or s.startswith(";"): continue
    if ALIGN.match(s):
        if cur: blocks.append(cur)
        cur, sawret = [], False
        continue
    if re.match(r'^\w+\s+endp', s):
        sawret = True; continue
    cur.append(s)
    if RETN.match(s) or s.startswith("jmp "): sawret = True
if cur: blocks.append(cur)

def anchor_of(b):
    for s in b:
        m = ANCHOR.match(s)
        if m: return int(m.group(2) or m.group(3), 16)
    return None

out, expect = {}, 0
for b in blocks:
    a = anchor_of(b)
    if a is not None:
        i = 0
        for k, h in enumerate(handlers):
            if h <= a: i = k
            else: break
        expect = i
    if expect < len(handlers):
        h = handlers[expect]
        out.setdefault(h, []).extend(b)
        expect += 1

print(f"{len(blocks)} code blocks -> {len(out)} of {len(handlers)} handlers", file=sys.stderr)
json.dump({f"{h:X}": out.get(h, []) for h in handlers},
          open(omkpaths.clean("_vmhandlers.json"), "w"))

# ---- summarise each handler ------------------------------------------------
CALL = re.compile(r'^call\s+(\S+)')
STR  = re.compile(r'offset\s+(a\w+)')
strs = json.load(open(omkpaths.clean("_asmstrings.json")))
rows = []
for e in tab:
    b = out.get(e["handler"], [])
    calls = [CALL.match(s).group(1) for s in b if CALL.match(s)]
    lits  = [strs.get(m.group(1), "") for s in b for m in [STR.search(s)] if m]
    rows.append({"op": e["op"], "handler": f"0x{e['handler']:06X}",
                 "operands": e["operands"], "insns": len(b),
                 "calls": calls, "strings": [x for x in lits if x]})
json.dump(rows, open(omkpaths.clean("_vmsummary.json"), "w"), indent=1)
nocode = [r["op"] for r in rows if r["insns"] == 0]
print(f"opcodes with no extracted code: {nocode or 'none'}", file=sys.stderr)

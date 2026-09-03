#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reconstruct the dialogue-script VM opcode table at 0x004C0140.

IDA emitted it as a mix of `dd offset X`, `align N` and raw `db` bytes; this
walks that listing byte by byte, tracking the address, and rebuilds the real
array of { uint32 handler; uint32 operandCount; }.
"""
import omkpaths
import re, json, sys

t = open(omkpaths.require_asm(), encoding="utf-8", errors="replace").read()
i = t.find("\noff_4C0140")
lines = t[i+1:i+200000].split("\n")

BASE = 0x4C0140
addr, mem, syms = BASE, {}, {}
UNRESOLVED = set()

DD_OFF = re.compile(r'^(?:[A-Za-z_?$@][\w?$@]*\s+)?dd\s+offset\s+([A-Za-z_?$@][\w?$@]*)')
DD_NUM = re.compile(r'^(?:[A-Za-z_?$@][\w?$@]*\s+)?dd\s+([0-9A-Fa-f]+h|\d+)\s*$')
DB     = re.compile(r'^(?:[A-Za-z_?$@][\w?$@]*\s+)?db\s+([0-9A-Fa-f]+h|\d+)')
ALIGN  = re.compile(r'^align\s+([0-9A-Fa-f]+h|\d+)')
SYMDEF = re.compile(r'^([A-Za-z_?$@][\w?$@]*)\s+(dd|db|dw|dq)\b')

def num(s):
    return int(s[:-1], 16) if s.endswith(("h", "H")) else int(s)

NAME2ADDR = {f["sub"]: int(f["addr"], 16)
             for f in json.load(open(omkpaths.clean("_funcs.json"))) if f.get("sub")}

def sym_addr(name):
    if name in NAME2ADDR: return NAME2ADDR[name]
    m = re.match(r'^(?:sub_|loc_|off_|unk_|byte_|word_|dword_|flt_|dbl_|stru_|asc_|a)?([0-9A-F]{6,8})$', name)
    if m:
        try: return int(m.group(1), 16)
        except ValueError: return None
    return None

stop = 0
for ln in lines:
    s = ln.strip()
    if not s or s.startswith(";"): continue
    m = SYMDEF.match(s)
    if m and m.group(1) != "off_4C0140" and addr > BASE:
        break                                   # a new named object: table ended
    m = DD_OFF.match(s)
    if m:
        a = sym_addr(m.group(1))
        if a is None:
            a = 0                                # symbol IDA never gave an address
            UNRESOLVED.add(m.group(1))
        syms[addr] = m.group(1)
        for k in range(4): mem[addr+k] = (a >> (8*k)) & 0xFF
        addr += 4; continue
    m = DD_NUM.match(s)
    if m:
        v = num(m.group(1))
        for k in range(4): mem[addr+k] = (v >> (8*k)) & 0xFF
        addr += 4; continue
    m = DB.match(s)
    if m:
        mem[addr] = num(m.group(1)) & 0xFF; addr += 1; continue
    m = ALIGN.match(s)
    if m:
        n = num(m.group(1))
        while addr % n: mem[addr] = 0; addr += 1
        continue
    stop += 1
    if stop > 3: break

end = addr
def u32(a):
    if any((a+k) not in mem for k in range(4)): return None
    return sum(mem[a+k] << (8*k) for k in range(4))

table = []
a = BASE
while a + 8 <= end:
    h, n = u32(a), u32(a+4)
    if h is None or n is None: break
    if n > 16 or (h != 0 and not (0x401000 <= h < 0x4B0000)):
        break
    table.append({"op": len(table), "addr": a, "handler": h, "operands": n,
                  "sym": syms.get(a)})
    a += 8

print("unresolved symbols:", sorted(UNRESOLVED) or "none", file=sys.stderr)
print(f"table spans 0x{BASE:X}..0x{end:X}; {len(table)} opcodes", file=sys.stderr)
json.dump(table, open(omkpaths.clean("_vmtable.json"), "w"), indent=1)
for e in table:
    print(f"  op {e['op']:3}  handler 0x{e['handler']:06X}  operands {e['operands']}"
          + (f"   [{e['sym']}]" if e['sym'] else ""))

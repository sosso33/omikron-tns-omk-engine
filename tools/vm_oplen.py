# SPDX-License-Identifier: GPL-3.0-or-later
"""Recover each VM opcode's operand length from its handler's assembly.

The interpreter keeps the instruction pointer at [ctx+0Ch]. A handler loads it,
walks it forward over its operands, and stores it back. Summing every advance
between a load and a store gives the number of operand bytes the handler
actually consumes - which is the ground truth the table is supposed to state.
"""
import omkpaths
import json, re

L_LOAD  = re.compile(r'^mov\s+(e\w\w),\s*\[(e\w\w)\+0Ch\]')
L_STORE = re.compile(r'^mov\s+\[(e\w\w)\+0Ch\],\s*(e\w\w)')
L_ADD   = re.compile(r'^add\s+(e\w\w),\s*([0-9A-Fa-f]+h?)$')
L_INC   = re.compile(r'^inc\s+(e\w\w)$')
L_ADDM  = re.compile(r'^add\s+dword ptr \[(e\w\w)\+0Ch\],\s*([0-9A-Fa-f]+h?)')
L_LEA   = re.compile(r'^lea\s+(e\w\w),\s*\[(e\w\w)\+([0-9A-Fa-f]+h?)\]')
L_MOVRR = re.compile(r'^mov\s+(e\w\w),\s*(e\w\w)$')

def num(s):
    s = s.strip()
    return int(s[:-1], 16) if s.endswith("h") else int(s)

def operand_len(asm):
    """-> (bytes, confident) or (None, False) if the handler never advances it."""
    acc = {}          # register -> bytes advanced since it was loaded
    total = 0
    saw = False
    VOLATILE = ("eax", "ecx", "edx")
    for raw in asm:
        l = raw.strip()
        # once the handler is done with the pointer it reuses the register for
        # something else; keep counting there and the total runs away
        if l.startswith("call"):
            for r in VOLATILE: acc.pop(r, None)
            continue
        m = re.match(r'^(pop|xor|sub|and|or|imul|shl|sar)\s+(e\w\w)', l)
        if m and m.group(2) in acc and not L_ADD.match(l):
            acc.pop(m.group(2)); continue
        m = L_ADDM.match(l)
        if m:
            v = num(m.group(2))
            # a negative or outsized adjustment is the handler moving the
            # instruction pointer itself - op 47 rewinds it by 7 to loop - not
            # operand bytes being consumed
            if 0 < v <= 32: total += v; saw = True
            continue
        m = L_LOAD.match(l)
        if m:
            acc[m.group(1)] = 0; continue
        m = L_LEA.match(l)
        if m and m.group(2) in acc:
            acc[m.group(1)] = acc[m.group(2)] + num(m.group(3)); continue
        m = L_MOVRR.match(l)
        if m:
            if m.group(2) in acc: acc[m.group(1)] = acc[m.group(2)]
            else: acc.pop(m.group(1), None)
            continue
        m = re.match(r'^(?:mov|movsx|movzx|lea)\s+(e\w\w),', l)
        if m and m.group(1) in acc:
            acc.pop(m.group(1)); continue
        m = L_ADD.match(l)
        if m and m.group(1) in acc:
            v = num(m.group(2))
            if 0 < v <= 32: acc[m.group(1)] += v
            else: acc.pop(m.group(1))
            continue
        m = L_INC.match(l)
        if m and m.group(1) in acc:
            acc[m.group(1)] += 1; continue
        m = L_STORE.match(l)
        if m and m.group(2) in acc:
            # the handler keeps walking the SAME register for its next operand
            # instead of reloading it, so bank what it has advanced and carry
            # on tracking rather than dropping the register
            total += acc[m.group(2)]; acc[m.group(2)] = 0; saw = True; continue
    return (total, True) if saw else (None, False)

if __name__ == "__main__":
    h = json.load(open(omkpaths.clean("_vmhandlers.json")))
    tab = {e["op"]: e for e in json.load(open(omkpaths.clean("_vmsummary.json")))}
    same = diff = missing = 0
    bad = []
    for op, e in sorted(tab.items()):
        asm = h.get(e["handler"][2:])
        if not asm: missing += 1; continue
        n, ok = operand_len(asm)
        if not ok: missing += 1; continue
        if n == e["operands"]: same += 1
        else: diff += 1; bad.append((op, e["operands"], n))
    print("handlers with a recoverable advance: %d" % (same + diff))
    print("   agree with the table : %d" % same)
    print("   differ               : %d" % diff)
    print("   no advance found     : %d" % missing)
    if bad:
        print("\nop : table -> assembly")
        for op, t, n in bad: print("  %3d : %2s -> %s" % (op, t, n))

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Which OPERAND each VM handler announces to the tag logger.

Five times now a handler has been found announcing a field the disassembler's
`SECTION` map did not expect - ops 71, 52, 93, 50 and 51 - and each was found
by accident, from a capture disagreeing with a prediction. This closes the
class: it reads the announce out of the assembly for every opcode that has one.

The shape is always the same. `Dbg_LogTagged(value, section)` is cdecl, so the
handler pushes the section string first and the value second:

    push offset aObjects_2   ; "OBJECTS"
    push ebx                 ; <- the value, and WHICH operand it is is the point
    call sub_40EC70

Operands reach a register two ways: through the shared fetch `sub_401AA0`
(`call` then `mov <reg>, eax`), or inlined - two bytes assembled into a
register and sign-extended with `movsx <reg>, cx`, optionally followed by the
`0x4000` indirect read `movsx <reg>, word ptr [<reg2>+<reg>*2+2]`. Both are
counted in order, so the index of the pushed register in that list is the
field the handler logs.

    python3 tools/vm_announce.py            # every announcing opcode
    python3 tools/vm_announce.py --check    # only where it disagrees with the map
"""
import os, re, sys, json

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import asmfn
import omkpaths
import dialog_disasm as D

CALL_FETCH = re.compile(r"^\s*call\s+sub_401AA0\b")
MOV_EAX    = re.compile(r"^\s*mov\s+(e[a-z][a-z]), eax\b")
MOVSX_CX   = re.compile(r"^\s*movsx\s+(e[a-z][a-z]), (?:cx|ax|dx|bx|si|di|bp)\s*$")
MOVSX_IND  = re.compile(r"^\s*movsx\s+(e[a-z][a-z]), word ptr \[")
PUSH_OFF   = re.compile(r"^\s*push\s+offset\s+(\w+)\s*;\s*\"([A-Z]+)\"")
PUSH_REG   = re.compile(r"^\s*push\s+(e[a-z][a-z])\s*$")
CALL_LOG   = re.compile(r"^\s*call\s+sub_40EC70\b")


def announced(op, handler):
    """-> (domain, field index, register) or (domain, None, register) or None."""
    try:
        a, end = asmfn.handler_span(op)
        lines = asmfn.func(a, end)
    except Exception:
        return None
    order, pending = [], False
    dom = reg = None
    for i, l in enumerate(lines):
        if CALL_FETCH.match(l): pending = True; continue
        m = MOV_EAX.match(l)
        if m and pending:
            order.append(m.group(1)); pending = False; continue
        m = MOVSX_CX.match(l)
        if m:
            order.append(m.group(1)); continue
        m = MOVSX_IND.match(l)
        if m and order and order[-1] == m.group(1):
            continue                       # the indirect read of the same slot
        m = PUSH_OFF.match(l)
        if m:
            # the value push is the next line, the call the one after
            if i + 2 < len(lines):
                r = PUSH_REG.match(lines[i + 1])
                if r and CALL_LOG.match(lines[i + 2]):
                    dom, reg = m.group(2), r.group(1)
                    break
    if dom is None: return None
    idx = order.index(reg) if reg in order else None
    return dom, idx, reg


def audit():
    tab = json.load(open(omkpaths.clean("_vmsummary.json")))
    out = []
    for e in tab:
        if "sub_40EC70" not in e["calls"]: continue
        r = announced(e["op"], e["handler"])
        if r is None: continue
        dom, idx, reg = r
        fs = D.FIELD_SECTION.get(e["op"]) or {}
        want = next((i for i, t in sorted(fs.items()) if t == dom), 0)
        out.append({"op": e["op"], "name": D.NAME.get(e["op"]), "domain": dom,
                    "asm": idx, "map": want, "reg": reg,
                    "operands": e["operands"],
                    "agrees": idx is None or idx == want})
    return out


def main():
    rows = audit()
    bad = [r for r in rows if not r["agrees"]]
    only = "--check" in sys.argv
    print("%d announcing handlers read; %d disagree with the field map"
          % (len(rows), len(bad)))
    for r in (bad if only else rows):
        flag = "" if r["agrees"] else "   <== DISAGREES"
        print("  op%-4d %-24s %-11s asm field %-5s map %-3d (%s)%s"
              % (r["op"], r["name"] or "", r["domain"],
                 r["asm"], r["map"], r["reg"], flag))
    return 0


if __name__ == "__main__":
    sys.exit(main())

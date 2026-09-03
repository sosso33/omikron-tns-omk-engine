#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerate the opcode table in docs/SCRIPT_VM.md from live data.

The table was hand-maintained and drifted: mnemonics landed in
tools/dialog_disasm.py and operand corrections in its LEN_FIX, while the doc
still showed the raw table values and empty names. This rebuilds those rows
from the code that is actually used to decode, so the two cannot disagree.

Columns: the table's own operand count, the corrected one where they differ,
the mnemonic, the .TAG domain and how often the opcode occurs in the 5785
world scripts - which is more useful than the handler's instruction count.

    python3 tools/vm_doc.py            # rewrite the table in place
    python3 tools/vm_doc.py --check    # report drift, change nothing
"""
import omkpaths
import sys, os, json, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import dialog_disasm as D
from dialog_triggers import (archive, area_records, scene_records,
                             _second_table, _scripts_from_records, global_file)

DOC = os.path.join(ROOT, "docs/SCRIPT_VM.md")
HEADER = "| op | handler | table | actual | mnemonic | .TAG domain | uses |"
SEP = "|---|---|---|---|---|---|---|"


def usage():
    use = collections.Counter()
    def scan(b, slots):
        for rec, f, p in slots:
            ops, st = D.disasm(b, p, len(b))
            if st == "ok":
                for pc, op, x in ops: use[op] += 1
    for name, fn in (("AREA", area_records), ("SCENE", scene_records)):
        for k, b in sorted(archive(omkpaths.data("IAM", name)).items()):
            r = fn(b)
            if r: scan(b, list(_scripts_from_records(b, r[0], r[1])) + _second_table(name, b))
    b, slots = global_file(omkpaths.data("IAM/GLOBAL"))
    scan(b, slots)
    return use


def rows():
    tab = {e["op"]: e for e in json.load(open(omkpaths.clean("_vmsummary.json")))}
    use = usage()
    out = [HEADER, SEP]
    for op in sorted(tab):
        e = tab[op]
        real = D.oplen(op)
        actual = "**%s**" % real if real != e["operands"] else ""
        name = D.NAME.get(op)
        dom = D.SECTION.get(op) or ""
        n = use.get(op, 0)
        out.append("| %d | %s | %s | %s | %s | %s | %s |" % (
            op, e["handler"], e["operands"], actual,
            "`%s`" % name if name else "", dom, n or ""))
    return out


if __name__ == "__main__":
    text = open(DOC).read().split("\n")
    # The EXACT header, not a prefix: docs/SCRIPT_VM.md also carries a
    # hand-written `| op | handler | what it does |` table for the held-object
    # opcodes (T14, 2026-09-02), and a prefix match landed on it first - the
    # check reported those 8 lines as "stale" and a rewrite replaced them.
    start = next(i for i, l in enumerate(text) if l == HEADER)
    end = start
    while end < len(text) and text[end].startswith("|"): end += 1
    new = rows()
    if text[start:end] == new:
        print("opcode table already up to date"); sys.exit(0)
    if "--check" in sys.argv:
        print("opcode table is STALE (%d rows would change)" %
              sum(1 for a, b in zip(text[start:end], new) if a != b)); sys.exit(1)
    open(DOC, "w").write("\n".join(text[:start] + new + text[end:]))
    named = sum(1 for l in new if "`" in l)
    print("opcode table rewritten: %d rows, %d with a mnemonic" % (len(new) - 2, named))

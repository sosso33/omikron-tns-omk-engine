#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Build readable/ : a working copy of clean/ in which every function carries a
status banner, so functions can be hand-rewritten one at a time and the
progress tracked.

    /* @func 0x00437E00  o3de_GetObjectByIndex  @status RAW  @lines 10  @callers 4 */

@status RAW   - still the mechanical decompiler output
@status CLEAN - rewritten by hand into idiomatic C
"""
import json, os, re, shutil, collections, sys

SRCDIR, DSTDIR = "clean", "readable"
funcs  = json.load(open(f"{SRCDIR}/_funcs.json"))
bodies = json.load(open(f"{SRCDIR}/_bodies.json"))
index  = json.load(open(f"{SRCDIR}/_index.json"))
segs   = json.load(open(f"{SRCDIR}/_segs.json"))
recov  = json.load(open(f"{SRCDIR}/_recovered.json"))

body   = {f["addr"]: "\n".join(b) for f, b in zip(funcs, bodies)}
name   = {f["addr"]: (f["name"] or f["sub"] or f"sub_{f['addr'][2:]}") for f in funcs}

# ---- call counts over the cleaned source --------------------------------
alltext = "\n".join(body.values())
calls = collections.Counter(re.findall(r'\b([A-Za-z_]\w*)\s*\(', alltext))
known = {name[a]: a for a in name}
callers = {a: calls.get(name[a], 0) for a in name}
# a function's own definition line counts as one occurrence
for a in callers:
    callers[a] = max(0, callers[a] - 1)

os.makedirs(f"{DSTDIR}/src", exist_ok=True)
for h in ("runtime.h", "globals.h", "decls.h"):
    shutil.copy(f"{SRCDIR}/{h}", f"{DSTDIR}/{h}")

rows = []
for (fname, label, lo, hi, n, nrec), seg in zip(index, segs):
    out = [f'/* {fname} - working copy, see ../README.md\n'
           f' *\n * Runtime.exe 0x{lo} - 0x{hi}   ({n} functions)\n'
           f' *\n * Each function below is tagged @status RAW or @status CLEAN.\n'
           f' * RAW   = mechanical decompiler output.\n'
           f' * CLEAN = rewritten by hand; the untouched original is in ../../{SRCDIR}/src/{fname}\n'
           f' */\n#include "../runtime.h"\n#include "../globals.h"\n#include "../decls.h"\n']
    for a in seg["fns"]:
        b  = body[a]
        ln = b.count("\n") + 1
        out.append(f"/* @func 0x{a}  {name[a]}  @status RAW  "
                   f"@lines {ln}  @callers {callers[a]} */\n{b}\n")
        rows.append({"addr": a, "name": name[a], "file": fname, "label": label,
                     "lines": ln, "callers": callers[a],
                     "recovered": a in recov, "status": "RAW"})
    open(f"{DSTDIR}/src/{fname}", "w").write("\n".join(out))

json.dump(rows, open(f"{DSTDIR}/status.json", "w"), indent=1)
print(f"{len(rows)} functions across {len(index)} files", file=sys.stderr)

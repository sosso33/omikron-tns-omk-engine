# SPDX-License-Identifier: GPL-3.0-or-later
import omkpaths
import re, json, sys

SRC = omkpaths.require_decomp()
text = open(SRC, encoding="utf-8", errors="replace").read()
lines = text.split("\n")

# --- locate sections -------------------------------------------------------
def find(pat, start=0):
    for i in range(start, len(lines)):
        if pat in lines[i]:
            return i
    return -1

i_decl = find("// Function declarations")
i_data = find("// Data declarations")
# first function definition marker
FUNC_MARK = re.compile(r'^//----- \(([0-9A-F]{8})\) -+$')
i_code = next(i for i,l in enumerate(lines) if FUNC_MARK.match(l))

print(f"decls  : {i_decl}", file=sys.stderr)
print(f"data   : {i_data}", file=sys.stderr)
print(f"code   : {i_code}", file=sys.stderr)

# --- split functions -------------------------------------------------------
funcs = []          # dicts: addr, sig, body(list of lines), start, end
marks = [(i, FUNC_MARK.match(l).group(1)) for i,l in enumerate(lines) if FUNC_MARK.match(l)]
for n,(ln,addr) in enumerate(marks):
    end = marks[n+1][0] if n+1 < len(marks) else len(lines)
    funcs.append({"addr": addr, "start": ln, "end": end,
                  "lines": lines[ln+1:end]})

print(f"functions: {len(funcs)}", file=sys.stderr)

# signature = first non-empty, non-comment line(s) up to the '{'
SUBNAME = re.compile(r'\b(sub_[0-9A-F]+|nullsub_\d+|start|__\w+)\s*\(')
for f in funcs:
    sig = []
    for l in f["lines"]:
        if l.strip() == "" and not sig: continue
        sig.append(l)
        if l.startswith("{"): break
    f["sig"] = "\n".join(sig[:-1]) if sig and sig[-1].startswith("{") else "\n".join(sig)
    m = SUBNAME.search(f["sig"])
    f["name"] = m.group(1) if m else None

named = sum(1 for f in funcs if f["name"])
print(f"with parsed name: {named}", file=sys.stderr)

json.dump({"i_decl": i_decl, "i_data": i_data, "i_code": i_code,
           "funcs": [{k:v for k,v in f.items() if k!="lines"} for f in funcs]},
          open(omkpaths.clean("_parse.json"),"w"))

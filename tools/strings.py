# SPDX-License-Identifier: GPL-3.0-or-later
import omkpaths
import re, json, sys, collections

lines = open(omkpaths.require_decomp(), encoding="utf-8", errors="replace").read().split("\n")
P = json.load(open(omkpaths.clean("_parse.json")))
i_data, i_code = P["i_data"], P["i_code"]

# --- string globals in the data section ------------------------------------
# char aFoo[] = "...";   /  char aFoo[12] = "...";  / char Format[] = "%d";
STR = re.compile(r'^(?:const\s+)?char\s+([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*=\s*"((?:[^"\\]|\\.)*)"\s*;')
strtab = {}
for l in lines[i_data:i_code]:
    m = STR.match(l.strip())
    if m:
        strtab[m.group(1)] = m.group(2)
print(f"string globals: {len(strtab)}", file=sys.stderr)

# also inline literals appear directly in code
json.dump(strtab, open(omkpaths.clean("_strings.json"),"w"))

# --- candidate function names embedded in those strings --------------------
# patterns seen: "o3de_GetObjectByIndex Error : ...", "Foo(): ...", "Foo Error :",
#                "Async_LoadDuringFrame Error : ...", "Foo(...): ..."
NAMEPAT = [
    re.compile(r'^([A-Za-z_][A-Za-z0-9_]{3,60})\s*\(\s*\)\s*:'),      # Foo(): msg
    re.compile(r'^([A-Za-z_][A-Za-z0-9_]{3,60})\s*\([^)]*\)\s*:'),    # Foo(x): msg
    re.compile(r'^([A-Za-z_][A-Za-z0-9_]{3,60})\s+Error\s*[:!]'),     # Foo Error :
    re.compile(r'^([A-Za-z_][A-Za-z0-9_]{3,60})\s*:\s*[A-Z]'),        # Foo: Msg
    re.compile(r'^([A-Za-z_][A-Za-z0-9_]{3,60})\s*::\s*'),            # C++ Class::
]
cands = {}
for g, s in strtab.items():
    for p in NAMEPAT:
        m = p.match(s)
        if m:
            n = m.group(1)
            if "_" in n or re.search(r'[a-z][A-Z]', n):   # looks like an identifier
                cands[g] = n
            break
print(f"strings that look like they carry a function name: {len(cands)}", file=sys.stderr)
for g,n in list(cands.items())[:40]:
    print(f"  {g:28} -> {n:38} | {strtab[g][:60]}", file=sys.stderr)
json.dump(cands, open(omkpaths.clean("_namecands.json"),"w"))

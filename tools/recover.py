# SPDX-License-Identifier: GPL-3.0-or-later
import omkpaths
import re, json, sys, collections

lines = open(omkpaths.require_decomp(), encoding="utf-8", errors="replace").read().split("\n")
P    = json.load(open(omkpaths.clean("_parse.json")))
asms = json.load(open(omkpaths.clean("_asmstrings.json")))
cs   = json.load(open(omkpaths.clean("_strings.json")))
strtab = dict(asms); strtab.update(cs)          # .c decls win (they're unescaped-ish)

FUNC_MARK = re.compile(r'^//----- \(([0-9A-F]{8})\) -+$')
marks = [(i, FUNC_MARK.match(l).group(1)) for i,l in enumerate(lines) if FUNC_MARK.match(l)]
bodies = {}
for n,(ln,addr) in enumerate(marks):
    end = marks[n+1][0] if n+1 < len(marks) else len(lines)
    bodies[addr] = lines[ln+1:end]
addr2name = {f["addr"]: f["name"] for f in P["funcs"] if f["name"]}

# ---------- candidate extraction -------------------------------------------
NAMEPAT = [
    (re.compile(r'^([A-Za-z_][A-Za-z0-9_]{3,60})\s*\([^)]*\)\s*[:!]'), 10),  # Foo(): / Foo(x):
    (re.compile(r'^([A-Za-z_][A-Za-z0-9_]{3,60})\s+Error\s*[:!]'),      9),  # Foo Error :
    (re.compile(r'^([A-Za-z_][A-Za-z0-9_]{3,60})\s*:\s*\S'),            5),  # Foo: msg
]
BAD = re.compile(r'^(Error|Warning|Info|Note|Fatal|DirectX|Windows|Game|Config\w*|The|You|Sorry|Debug|File|Line|Assert\w*)$', re.I)

def candidates(s):
    out = []
    for p, w in NAMEPAT:
        m = p.match(s.strip())
        if m:
            n = m.group(1)
            if BAD.match(n): continue
            if not ("_" in n or re.search(r'[a-z][A-Z]', n)): continue
            out.append((n, w)); break
    return out

# map each string symbol -> candidate name
sym2cand = {}
for sym, s in strtab.items():
    for n, w in candidates(s):
        sym2cand[sym] = (n, w)

print(f"string symbols carrying a name: {len(sym2cand)}", file=sys.stderr)

# ---------- attribute names to the functions that reference them -----------
IDENT = re.compile(r'\b([A-Za-z_]\w*)\b')
LIT   = re.compile(r'"((?:[^"\\]|\\.)*)"')

votes = collections.defaultdict(collections.Counter)
for addr, body in bodies.items():
    txt = "\n".join(body)
    for sym in set(IDENT.findall(txt)):
        if sym in sym2cand:
            n, w = sym2cand[sym]; votes[addr][n] += w
    for lit in LIT.findall(txt):
        for n, w in candidates(lit.replace('\\n','\n').replace('\\"','"')):
            votes[addr][n] += w

# a name should win only if it dominates within the function AND
# that function is the best claimant for the name
best = {a: c.most_common(1)[0] for a, c in votes.items() if c}
byname = collections.defaultdict(list)
for a,(n,w) in best.items(): byname[n].append((w,a))

recovered = {}
for n, lst in byname.items():
    lst.sort(reverse=True)
    top_w, top_a = lst[0]
    if len(lst) > 1 and lst[1][0] == top_w:
        continue                      # ambiguous: two functions claim it equally
    recovered[top_a] = n

print(f"functions with a recovered name: {len(recovered)}", file=sys.stderr)
json.dump(recovered, open(omkpaths.clean("_recovered.json"),"w"), indent=1, sort_keys=True)
for a in sorted(recovered)[:30]:
    print(f"  {addr2name.get(a,'?'):16} {a} -> {recovered[a]}", file=sys.stderr)

# SPDX-License-Identifier: GPL-3.0-or-later
import json, re, sys, collections
rows = json.load(open("readable/status.json"))
name = {r["addr"]: r["name"] for r in rows}
sub2addr = {r["name"]: r["addr"] for r in rows}
bodies = {}
import glob, os
BAN = re.compile(r'^/\* @func 0x([0-9A-F]{8})\s+(\S+)\s+@status', re.M)
for p in glob.glob("readable/src/*.c"):
    t = open(p).read(); h = list(BAN.finditer(t))
    for i, m in enumerate(h):
        e = h[i+1].start() if i+1 < len(h) else len(t)
        bodies[m.group(1)] = t[m.end():e]
CALL = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
def callees(a):
    out = set()
    for c in CALL.findall(bodies.get(a, "")):
        t = sub2addr.get(c)
        if t and t != a: out.add(t)
    return out
root = sys.argv[1]
depth = {root: 0}; q = collections.deque([root])
while q:
    a = q.popleft()
    for c in callees(a):
        if c not in depth:
            depth[c] = depth[a] + 1; q.append(c)
by = collections.Counter(depth.values())
print("transitive closure of", name[root], "=", len(depth), "functions")
print("by depth:", dict(sorted(by.items())))
lines = {r["addr"]: r["lines"] for r in rows}
print("total lines:", sum(lines[a] for a in depth))
print("\ndepth 1 (direct callees):")
for a in sorted(depth, key=lambda x: (depth[x], x)):
    if depth[a] == 1:
        print(f"  0x{a} {name[a]:22} {lines[a]:5} lines  -> {len(callees(a))} callees")

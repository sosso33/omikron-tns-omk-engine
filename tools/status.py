#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Progress tracker for readable/. The @status banners in readable/src/*.c are
the source of truth; status.json is regenerated from them.

  status.py                 progress per module
  status.py top [N]         most-called RAW functions (best cleanup value)
  status.py big [N]         longest RAW functions
  status.py named [N]       RAW functions that have a recovered real name
                            (a name from a debug string, not yet read - unlike
                             @status NAMED, which means read and named by hand)
  status.py list <module>   every function in a module
  status.py find <pat>      search by name or address
  status.py show <addr|name>  print one function as it stands
  status.py read            functions read and deliberately left alone
"""
import json, re, sys, glob, collections, os

BAN = re.compile(r'^/\* @func 0x([0-9A-F]{8})\s+(\S+)\s+@status (\w+)\s+'
                 r'@lines (\d+)\s+@callers (\d+) \*/$', re.M)

def load():
    rows = []
    for path in sorted(glob.glob("readable/src/*.c")):
        text = open(path).read()
        hits = list(BAN.finditer(text))
        for n, m in enumerate(hits):
            end = hits[n+1].start() if n+1 < len(hits) else len(text)
            rows.append({"addr": m.group(1), "name": m.group(2), "status": m.group(3),
                         "lines": int(m.group(4)), "callers": int(m.group(5)),
                         "file": os.path.basename(path),
                         "module": os.path.basename(path)[3:-2],
                         "body": text[m.end():end].strip()})
    return rows

def sync(rows):
    json.dump([{k: v for k, v in r.items() if k != "body"} for r in rows],
              open("readable/status.json", "w"), indent=1)

def table(rows, n=40):
    print(f"{'address':10} {'status':6} {'ln':>4} {'used':>5}  {'module':8} name")
    for r in rows[:n]:
        print(f"0x{r['addr']} {r['status']:6} {r['lines']:4} {r['callers']:5}  "
              f"{r['module']:8} {r['name']}")
    if len(rows) > n: print(f"... {len(rows)-n} more")

rows = load(); sync(rows)
cmd  = sys.argv[1] if len(sys.argv) > 1 else "progress"
arg  = sys.argv[2] if len(sys.argv) > 2 else None
raw  = [r for r in rows if r["status"] == "RAW"]

LIB = set()
_lp = "readable/libcode.json"
if os.path.exists(_lp):
    LIB = set(json.load(open(_lp))["addresses"])

if cmd == "progress":
    game = [r for r in rows if r["addr"] not in LIB]
    clean = [r for r in rows if r["status"] == "CLEAN"]
    named = [r for r in rows if r["status"] == "NAMED"]
    read  = [r for r in rows if r["status"] == "READ"]
    done = len(clean) + len(named) + len(read)
    print(f"{done} / {len(rows)} functions processed  "
          f"({done*100//max(1,len(rows))}%)")
    print(f"   CLEAN  {len(clean):5}   body rewritten by hand")
    print(f"   NAMED  {len(named):5}   read and named, body left as generated")
    print(f"   READ   {len(read):5}   read, deliberately left alone (banner says why)")
    print(f"   RAW    {len(raw):5}   untouched")
    if LIB:
        print("\nexcluding the linked C/C++ runtime (readable/libcode.json):")
        print("   game code            %5d functions" % len(game))
        print("   processed            %5d  (%d%%)" % (
            done, done*100//max(1,len(game))))
        print("   runtime, not counted %5d" % len(LIB))
    print()
    per = collections.defaultdict(lambda: [0, 0, 0, 0])
    for r in rows:
        per[r["file"]][0] += 1
        if   r["status"] == "CLEAN": per[r["file"]][1] += 1
        elif r["status"] == "NAMED": per[r["file"]][2] += 1
        elif r["status"] == "READ":  per[r["file"]][3] += 1
    print(f"{'file':18} {'clean':>6} {'named':>6} {'read':>6} {'total':>6}")
    for f in sorted(per):
        t, c, n, rd = per[f]
        print(f"{f:18} {c:6} {n:6} {rd:6} {t:6}" +
              ("  <-- in progress" if 0 < c + n + rd < t else ""))
elif cmd == "read":
    table([r for r in rows if r["status"] == "READ"], 200)
elif cmd == "top":
    table(sorted(raw, key=lambda r: -r["callers"]), int(arg or 30))
elif cmd == "big":
    table(sorted(raw, key=lambda r: -r["lines"]), int(arg or 30))
elif cmd == "named":
    named = [r for r in raw if not r["name"].startswith(("sub_", "nullsub"))]
    table(sorted(named, key=lambda r: -r["callers"]), int(arg or 100))
elif cmd == "list":
    table([r for r in rows if arg in r["file"]], 10**6)
elif cmd == "find":
    pat = re.compile(arg, re.I)
    table([r for r in rows if pat.search(r["name"]) or pat.search(r["addr"])], 10**6)
elif cmd == "show":
    for r in rows:
        if r["addr"].lower().endswith(arg.lower().lstrip("0x")) or r["name"] == arg:
            print(f"/* {r['file']}  0x{r['addr']}  {r['status']} */\n{r['body']}")
            break
    else: print("not found")
else:
    print(__doc__)

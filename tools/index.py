#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate readable/INDEX.md - the list of hand-cleaned functions.

Descriptions come from the first sentence of each function's leading comment,
so the index cannot drift from the code: rewrite the comment and re-run.
"""
import json, os, re, sys, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BAN = re.compile(r'^/\* @func 0x([0-9A-F]{8})\s+(\S+)\s+@status (\w+)\s+'
                 r'@lines (\d+)\s+@callers (\d+) \*/$', re.M)

def first_sentence(body):
    """the opening /* ... */ comment of a function, reduced to one sentence"""
    m = re.match(r'\s*/\*(.*?)\*/', body, re.S)
    if not m: return ""
    text = m.group(1)
    text = re.sub(r'^\s*\*?\s?', '', text, flags=re.M)
    text = " ".join(text.split())
    # split on the first sentence end, but not on an abbreviation
    m2 = re.search(r'(?<![A-Z])(?<!\be\.g)(?<!\bi\.e)(?<!\betc)\.\s', text)
    if m2: text = text[:m2.start() + 1]
    if len(text) > 150: text = text[:147].rsplit(" ", 1)[0] + "…"
    return text

rows = []
for path in sorted(glob.glob(os.path.join(ROOT, "readable/src/*.c"))):
    t = open(path).read(); hits = list(BAN.finditer(t))
    for i, m in enumerate(hits):
        if m.group(3) == "RAW": continue
        end = hits[i+1].start() if i+1 < len(hits) else len(t)
        rows.append({"addr": m.group(1), "name": m.group(2),
                     "status": m.group(3),
                     "file": os.path.basename(path), "callers": int(m.group(5)),
                     "desc": first_sentence(t[m.end():end])})
rows.sort(key=lambda r: r["addr"])

renames = {}
rp = os.path.join(ROOT, "tools/renames.json")
if os.path.exists(rp): renames = json.load(open(rp))

total = len(re.findall(r'@func 0x', "".join(
    open(p).read() for p in glob.glob(os.path.join(ROOT, "readable/src/*.c")))))

GROUPS = [
    ("Dialogue", r'^Dialog_'),
    ("Script VM", r'^Script_|^Var_'),
    ("Morph playback and animation", r'^Morph_|^Anim_|^Adpcm_|^Camera_'),
    ("Files and archives", r'^Archive_|^Res_'),
    ("Scene and geometry", r'^o3de_|^Scene_|^Matrix'),
    ("System", r'^Mem_|^Sys_|^Subtitle_|^Text_|^Dbg_'),
]

clean = [r for r in rows if r["status"] == "CLEAN"]
named = [r for r in rows if r["status"] == "NAMED"]
read  = [r for r in rows if r["status"] == "READ"]

out = [f"""# Processed functions

{len(rows)} of {total} functions have been worked out, in three states:

| status | meaning | count |
|---|---|---|
| `CLEAN` | body rewritten by hand | {len(clean)} |
| `NAMED` | read and named from evidence, body left as generated | {len(named)} |
| `READ` | read, then deliberately left alone - the banner says why | {len(read)} |

Everything else is `@status RAW`: untouched decompiler output. A RAW function
may still carry a real name - the automatic recovery pass lifted around 50 out
of the binary's own debug strings - but nobody has read it. `clean/src/` always
holds the untouched original of every function whatever its status.

`READ` is the one that is easy to lose. A function can be read closely, supply
the fact that was wanted, and still be the wrong thing to rename or rewrite -
usually because a name would be a guess. Without a record of that, the next
pass re-reads it from scratch and risks inventing the name that was rejected
the first time. The reason is written into the banner comment, so it travels
with the code.

Renames are applied as **real renames across the whole tree** by
`tools/rename.py`, not as `#define` aliases - a function renamed where it is
defined reads the same at every call site in the other modules, in `decls.h`,
and in its `@func` banner. `tools/renames.json` is the map ({len(renames)} entries).

`tools/rename.py` promotes RAW to NAMED automatically for anything in the map,
so the trace cannot drift out of step with the renames.

Regenerate this file with `python3 tools/index.py`.
"""]

used = set()
for title, pat in GROUPS:
    sel = [r for r in rows if re.match(pat, r["name"])]
    if not sel: continue
    used |= {r["addr"] for r in sel}
    out.append(f"\n## {title}\n")
    out.append("| address | name | | file | used | what it does |")
    out.append("|---|---|---|---|---|---|")
    for r in sel:
        tag = {"CLEAN": "", "NAMED": "named", "READ": "read"}[r["status"]]
        desc = r["desc"] or ("name established, body still as generated"
                             if r["status"] == "NAMED" else "")
        out.append(f"| `0x{r['addr']}` | `{r['name']}` | {tag} | {r['file']} | "
                   f"{r['callers']} | {desc} |")
rest = [r for r in rows if r["addr"] not in used]
if rest:
    out.append("\n## Other\n")
    out.append("| address | name | | file | used | what it does |")
    out.append("|---|---|---|---|---|---|")
    for r in rest:
        tag = {"CLEAN": "", "NAMED": "named", "READ": "read"}[r["status"]]
        desc = r["desc"] or ("name established, body still as generated"
                             if r["status"] == "NAMED" else "")
        out.append(f"| `0x{r['addr']}` | `{r['name']}` | {tag} | {r['file']} | "
                   f"{r['callers']} | {desc} |")

unnamed = [r for r in rows if r["name"].startswith(("sub_", "nullsub"))]
if unnamed:
    out.append("\n## Processed but still carrying their address name\n")
    out.append("Nothing in the code establishes what these are for, and a wrong "
               "name is worse than none. The `status` column separates the ones "
               "whose bodies were rewritten from the ones that were only read.\n")
    out.append("| address | status | file | what it does |")
    out.append("|---|---|---|---|")
    for r in unnamed:
        out.append(f"| `0x{r['addr']}` | {r['status'].lower()} | {r['file']} | "
                   f"{r['desc']} |")

out.append(f"""

## Where the names came from

* **Recovered from the binary's own debug strings** - the game prints messages
  naming its functions, e.g. `o3de_GetObjectByIndex Error : index too big !`.
  See `clean/SYMBOLS.md`.
* **Read out of the code** - `Adpcm_DecodeMono` from the step table and nibble
  handling, `Script_Run` from the opcode dispatch loop, `Var_Set` from the
  array it writes.
* **Inferred from use, and marked as such** in the function's comment where the
  evidence is thin.
""")
dest = os.path.join(ROOT, "readable/INDEX.md")
text = "\n".join(out) + "\n"
if "--check" in sys.argv:
    # report drift without writing, so tools/verify.py can assert freshness
    # without quietly repairing what it is meant to be checking
    cur = open(dest).read() if os.path.exists(dest) else ""
    print("readable/INDEX.md: "
          + ("up to date" if cur == text else "STALE - rerun tools/index.py"))
    sys.exit(0 if cur == text else 1)
open(dest, "w").write(text)
print(f"readable/INDEX.md: {len(rows)} cleaned of {total}")

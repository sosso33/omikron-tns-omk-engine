#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rename symbols across the whole readable/ tree.

The point is that a name has to mean the same thing everywhere: a function
renamed in the file it is defined in must read the same at every call site in
the other 32 modules, in decls.h, and in the @func banner. Doing it with a
`#define` alias keeps the code compiling but leaves the other files still
saying `sub_401800`, which defeats the purpose.

    python3 tools/rename.py                 apply tools/renames.json
    python3 tools/rename.py --check         report what would change
"""
import json, os, re, sys, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP  = os.path.join(ROOT, "tools", "renames.json")
TARGETS = (glob.glob(os.path.join(ROOT, "readable/src/*.c"))
           + [os.path.join(ROOT, "readable", h) for h in
              ("decls.h", "globals.h", "types.h")])

def load_map():
    m = json.load(open(MAP)) if os.path.exists(MAP) else {}
    # longest first so sub_401800 is never clipped by a shorter prefix
    return dict(sorted(m.items(), key=lambda kv: -len(kv[0])))

def apply(check=False):
    m = load_map()
    if not m:
        print("no renames defined"); return
    pat = re.compile(r'\b(' + "|".join(re.escape(k) for k in m) + r')\b')
    total, touched = 0, []
    for path in TARGETS:
        if not os.path.exists(path): continue
        src = open(path).read()
        new, n = pat.subn(lambda x: m[x.group(1)], src)
        if n:
            total += n; touched.append((os.path.basename(path), n))
            if not check: open(path, "w").write(new)
    print(f"{total} occurrences across {len(touched)} files"
          + (" (dry run)" if check else ""))
    for name, n in sorted(touched, key=lambda t: -t[1])[:12]:
        print(f"   {name:16} {n}")
    return total

def promote(check=False):
    """Mark every hand-renamed function @status NAMED.

    A function that has been read closely enough to be given a real name is not
    untouched decompiler output, even when its body is left as generated -
    calling it RAW loses the fact that it was already worked out, and the tree
    is far too big to rediscover that by eye. NAMED is that third state:

        RAW    generated output; the name, if any, came out of a debug string
               during the automatic recovery pass and nobody has read the code
        NAMED  read and named from evidence, body left as generated
        CLEAN  body rewritten by hand

    Membership in renames.json is what distinguishes the first two, so this
    runs off the same map and cannot drift out of step with it.
    """
    names = set(load_map().values())
    ban = re.compile(r'(/\* @func 0x[0-9A-F]{8}  )(\S+)(  @status )RAW')
    n = 0
    for path in TARGETS:
        if not path.endswith(".c") or not os.path.exists(path): continue
        src = open(path).read()
        def fix(m):
            nonlocal n
            if m.group(2) not in names: return m.group(0)
            n += 1
            return m.group(1) + m.group(2) + m.group(3) + "NAMED"
        new = ban.sub(fix, src)
        if n and new != src and not check: open(path, "w").write(new)
    print(f"{n} function(s) promoted RAW -> NAMED" + (" (dry run)" if check else ""))
    return n


if __name__ == "__main__":
    check = "--check" in sys.argv
    apply(check=check)
    promote(check=check)

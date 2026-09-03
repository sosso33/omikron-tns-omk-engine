# SPDX-License-Identifier: GPL-3.0-or-later
import omkpaths
import re,sys,json
lines=open(omkpaths.require_decomp(),encoding="utf-8",errors="replace").read().split("\n")
M=re.compile(r'^//----- \(([0-9A-F]{8})\) -+$')
marks=[(i,M.match(l).group(1)) for i,l in enumerate(lines) if M.match(l)]
want=sys.argv[1:]
for n,(ln,addr) in enumerate(marks):
    end=marks[n+1][0] if n+1<len(marks) else len(lines)
    blk=lines[ln:end]
    head="\n".join(blk[:4])
    for w in want:
        if re.search(r'\b'+w+r'\b', head):
            print("\n".join(blk[:int(sys.argv[0] and 30)]))
            print("...")

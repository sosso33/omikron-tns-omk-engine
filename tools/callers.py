# SPDX-License-Identifier: GPL-3.0-or-later
import omkpaths
import re,collections,json
lines=open(omkpaths.require_decomp(),encoding="utf-8",errors="replace").read().split("\n")
M=re.compile(r'^//----- \(([0-9A-F]{8})\) -+$')
marks=[(i,M.match(l).group(1)) for i,l in enumerate(lines) if M.match(l)]
CALL=re.compile(r'\b(sub_[0-9A-F]+|nullsub_\d+)\s*\(')
cnt=collections.Counter()
owner={}
for n,(ln,addr) in enumerate(marks):
    end=marks[n+1][0] if n+1<len(marks) else len(lines)
    body="\n".join(lines[ln:end])
    m=re.search(r'\b(sub_[0-9A-F]+|nullsub_\d+)\s*\(',body)
    if m: owner[m.group(1)]=addr
    for c in CALL.findall(body): cnt[c]+=1
json.dump({"owner":owner},open(omkpaths.clean("_owner.json"),"w"))
for name,c in cnt.most_common(45):
    print(f"{c:6}  {name}")

# SPDX-License-Identifier: GPL-3.0-or-later
import omkpaths
import re,json,collections,sys
PROC=re.compile(r'^([A-Za-z_?$@][\w?$@]*)\s+proc\s+(near|far)')
ENDP=re.compile(r'^([A-Za-z_?$@][\w?$@]*)\s+endp')
IMP =re.compile(r'\bcall\s+ds:(\w+)')
IMP2=re.compile(r'\bds:__imp_(\w+)')
OFF =re.compile(r'\boffset\s+([A-Za-z_]\w*)')
CALL=re.compile(r'\bcall\s+(sub_[0-9A-F]+|\w+)')

cur=None
imports=collections.defaultdict(collections.Counter)
offs=collections.defaultdict(set)
calls=collections.defaultdict(set)
for line in open(omkpaths.require_asm(),encoding="utf-8",errors="replace"):
    m=PROC.match(line)
    if m: cur=m.group(1); continue
    if ENDP.match(line): cur=None; continue
    if not cur: continue
    for r in (IMP,IMP2):
        for n in r.findall(line): imports[cur][n]+=1
    for n in OFF.findall(line): offs[cur].add(n)
    for n in CALL.findall(line): calls[cur].add(n)

print(f"procs with imports: {len(imports)}",file=sys.stderr)
allimp=collections.Counter()
for c in imports.values(): allimp.update(c)
print("top imports:",allimp.most_common(25),file=sys.stderr)
json.dump({"imports":{k:dict(v) for k,v in imports.items()},
           "offs":{k:sorted(v) for k,v in offs.items()},
           "calls":{k:sorted(v) for k,v in calls.items()}},
          open(omkpaths.clean("_asmev.json"),"w"))

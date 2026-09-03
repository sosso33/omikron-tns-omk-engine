# SPDX-License-Identifier: GPL-3.0-or-later
import omkpaths
import re, json, sys

# IDA asm string defs, possibly continued over several `db` lines:
#   aFoo db 'text ...'
#   db ' more',0Ah,0
LABEL = re.compile(r"^([A-Za-z_?$@][\w?$@]*)\s+db\s+(.*)$")
CONT  = re.compile(r"^\s+db\s+(.*)$")

def decode(operand):
    # operand like:  'abc',0Ah,27h,'def',0
    out, i, n = [], 0, len(operand)
    while i < n:
        c = operand[i]
        if c == "'":
            j = i+1
            buf = []
            while j < n:
                if operand[j] == "'":
                    if j+1 < n and operand[j+1] == "'":
                        buf.append("'"); j += 2; continue
                    break
                buf.append(operand[j]); j += 1
            out.append("".join(buf)); i = j+1
        elif c in " ,":
            i += 1
        elif c == ";":
            break
        else:
            m = re.match(r"([0-9A-Fa-f]+)h", operand[i:])
            if m:
                out.append(chr(int(m.group(1),16))); i += m.end()
            else:
                m = re.match(r"(\d+)", operand[i:])
                if m:
                    out.append(chr(int(m.group(1)) & 0xFF)); i += m.end()
                else:
                    i += 1
    return "".join(out)

strtab, cur = {}, None
for line in open(omkpaths.require_asm(), encoding="utf-8", errors="replace"):
    line = line.rstrip("\n")
    m = LABEL.match(line)
    if m:
        cur = m.group(1)
        strtab[cur] = decode(m.group(2))
        continue
    m = CONT.match(line)
    if m and cur:
        strtab[cur] += decode(m.group(1))
        continue
    if line.strip() and not line.startswith((" ", "\t")):
        cur = None

strtab = {k: v.split("\0")[0] for k, v in strtab.items() if v.strip("\0").strip()}
print(f"asm string symbols: {len(strtab)}", file=sys.stderr)
json.dump(strtab, open(omkpaths.clean("_asmstrings.json"),"w"))

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Rewrite the Hex-Rays dump Runtime.exe.c into a readable, navigable tree.

  * recovers real function names from the binary's own debug strings
  * applies a curated table of hand-verified helper names
  * strips decompiler bookkeeping (register hints, "guessed type" blocks, weak/idb)
  * normalises IDA types to <stdint.h>
  * rewrites  *(_DWORD *)(p + 32)  as  u32(p, 32)
  * drops labels nothing jumps to and gotos that jump to the next statement
  * splits the result into per-subsystem files with a symbol index
"""
import omkpaths
import re, json, os, sys, collections

SRC   = omkpaths.require_decomp()
OUT   = "clean"
MAXFN = 140                      # functions per output file

# ---------------------------------------------------------------- name tables
# Hand-verified from the function bodies (see SYMBOLS.md for the evidence).
CURATED_FUNCS = {
    "sub_411DA0": "Sys_FatalError",      # vsprintf -> MessageBox -> exit(1)
    "sub_411EF0": "Mem_Alloc",           # malloc + allocation accounting
    "sub_411FA0": "Mem_Calloc",          # calloc + allocation accounting
    "sub_412060": "Mem_Free",            # free  + allocation accounting
    "sub_412100": "Sys_GetTimeMs",       # timeGetTime() - base, pausable
    "sub_440320": "Err_SetMessage",      # vsprintf into the global error buffer
    "sub_49EAD0": "Log_Printf",          # guarded formatted logger
    "sub_4345E0": "List_PickRandomByType",
    "nullsub_1":  "Dbg_Printf",          # stripped debug printf
    "nullsub_2":  "Dbg_Trace",
}
CURATED_GLOBALS = {
    "dword_4E77F8": "g_MemBytesAllocated",
    "dword_4E76E8": "g_MemAllocCount",
    "dword_4E76C0": "g_MemPeakBytes",
    "byte_4E77FC":  "g_MemTraceEnabled",
    "dword_4E77F4": "g_TimeBaseMs",
    "dword_4E76D4": "g_TimePaused",
    "byte_530808":  "g_ErrorMessageBuf",
}

# ---------------------------------------------------------------- literal mask
_MASK = re.compile(r'"(?:[^"\\]|\\.)*"' r"|'(?:[^'\\]|\\.)*'")
def mask(line):
    lits = []
    def sub(m):
        lits.append(m.group(0))
        return f"\x00{len(lits)-1}\x00"
    return _MASK.sub(sub, line), lits
def unmask(line, lits):
    return re.sub(r'\x00(\d+)\x00', lambda m: lits[int(m.group(1))], line)

# ---------------------------------------------------------------- transforms
TYPEMAP = [
    (r'\bunsigned __int64\b', 'uint64_t'), (r'\bunsigned __int32\b', 'uint32_t'),
    (r'\bunsigned __int16\b', 'uint16_t'), (r'\bunsigned __int8\b',  'uint8_t'),
    (r'\b__int64\b', 'int64_t'), (r'\b__int32\b', 'int32_t'),
    (r'\b__int16\b', 'int16_t'), (r'\b__int8\b',  'int8_t'),
    (r'\b_QWORD\b', 'uint64_t'), (r'\b_DWORD\b', 'uint32_t'),
    (r'\b_WORD\b',  'uint16_t'), (r'\b_BYTE\b',  'uint8_t'),
    (r'\b_UNKNOWN\b', 'unknown_t'),
]
ACC = {'uint32_t':'u32','int32_t':'i32','int':'i32','unsigned int':'u32',
       'uint16_t':'u16','int16_t':'i16','uint8_t':'u8','int8_t':'i8',
       'char':'i8','unsigned char':'u8','float':'f32','double':'f64',
       'uint64_t':'u64','int64_t':'i64'}

GUESSED = re.compile(r'^\s*//\s*[0-9A-F]{5,8}:\s*using guessed type\b')
REGCMT  = re.compile(
    r'\s*//\s*(?:'
    r'[re]?(?:ax|bx|cx|dx|si|di|bp|sp)\d?|r\d+|st\d*|xmm\d+|'
    r'\[(?:esp|ebp|rsp|rbp)[^\]]*\](?:\s*\[[^\]]*\])*'
    r')(?:\s+BYREF)?(?:\s+OVERLAPPED)?\s*$')
BYREF   = re.compile(r'BYREF')
WEAK    = re.compile(r'\s*//\s*(?:weak|idb)\s*$')
MARK    = re.compile(r'^//----- \(([0-9A-F]{8})\) -+$')

def find_close(s, i):
    """index of the ')' matching the '(' at i, or -1"""
    d = 0
    while i < len(s):
        if s[i] == '(': d += 1
        elif s[i] == ')':
            d -= 1
            if d == 0: return i
        i += 1
    return -1

def split_offset(e):
    """split a balanced 'base + off' at top level; returns (base, off) or None"""
    d = 0
    for i in range(len(e) - 1, 0, -1):
        c = e[i]
        if c == ')': d += 1
        elif c == '(': d -= 1
        elif c == '+' and d == 0 and e[i-1] not in '+-*/%&|^<>=!':
            base, off = e[:i].strip(), e[i+1:].strip()
            if base and off: return base, off
    return None

CASTSTART = re.compile(r'\*\(\s*((?:unsigned\s+)?[A-Za-z_]\w*)\s*\*\s*\)')
ELEMSTART = re.compile(r'\*\(\(\s*((?:unsigned\s+)?[A-Za-z_]\w*)\s*\*\s*\)')
PUNSTART  = re.compile(r'\*\(\s*((?:unsigned\s+)?[A-Za-z_]\w*)\s*\*\s*\)\s*&\s*([A-Za-z_]\w*)')

def split_pm(e):
    """Split a balanced element operand into (base, index).

    Everything additive after the first top-level +/- belongs to the INDEX,
    because the index is scaled by the element size. Splitting at the last one
    instead - as the byte-offset form may - would move a scaled term into the
    base and silently change the address.
    """
    d = 0
    for i in range(len(e)):
        c = e[i]
        if c == '(': d += 1
        elif c == ')': d -= 1
        elif c in '+-' and d == 0 and i > 0 and e[i-1] not in '+-*/%&|^<>=!(':
            base, idx = e[:i].strip(), e[i+1:].strip()
            if base and idx:
                return base, (idx if c == '+' else '-(' + idx + ')')
    return None

def rewrite_punning(s):
    """*(float *)&v12  ->  as_f32(v12)"""
    def sub(m):
        acc = ACC.get(m.group(1).strip())
        return f"as_{acc}({m.group(2)})" if acc else m.group(0)
    return PUNSTART.sub(sub, s)

def rewrite_elems(s):
    """*((uint32_t *)p + 64)  ->  u32i(p, 64)"""
    out, i = [], 0
    while True:
        m = ELEMSTART.search(s, i)
        if not m: break
        acc = ACC.get(m.group(1).strip())
        k = find_close(s, m.start() + 1)          # close of the outer '('
        if not acc or k < 0:
            out.append(s[i:m.end()]); i = m.end(); continue
        inner = s[m.end():k].strip()              # operand after the cast
        sp = split_pm(inner)
        if not sp:
            out.append(s[i:m.end()]); i = m.end(); continue
        base, idx = sp
        out.append(s[i:m.start()])
        out.append(f"{acc}i({base}, {idx})")
        i = k + 1
    out.append(s[i:])
    return "".join(out)

def rewrite_derefs(s):
    """*(uint32_t *)(p + 32)  ->  u32(p, 32);   *(uint8_t *)p -> u8(p, 0)"""
    out, i = [], 0
    while True:
        m = CASTSTART.search(s, i)
        if not m: break
        acc = ACC.get(m.group(1).strip())
        if not acc:
            out.append(s[i:m.end()]); i = m.end(); continue
        j = m.end()
        while j < len(s) and s[j] == ' ': j += 1
        if j < len(s) and s[j] == '(':
            k = find_close(s, j)
            if k < 0: out.append(s[i:m.end()]); i = m.end(); continue
            inner, end = s[j+1:k], k + 1
            sp = split_offset(inner)
            base, off = sp if sp else (inner.strip(), '0')
        else:                                     # identifier, maybe a call
            k = j
            while k < len(s) and (s[k].isalnum() or s[k] == '_'): k += 1
            if k == j: out.append(s[i:m.end()]); i = m.end(); continue
            while k < len(s) and s[k] == '(':     # LODWORD(a2), foo(x)(y)
                c = find_close(s, k)
                if c < 0: break
                k = c + 1
            base, off, end = s[j:k].strip(), '0', k
        out.append(s[i:m.start()])
        out.append(f"{acc}({base}, {off})")
        i = end
    out.append(s[i:])
    return "".join(out)

STRREF = re.compile(r'\b(a[A-Z]\w*|asc_[0-9A-F]+|byte_[0-9A-F]+|Format\w*|Str\d*|lpCaption|Buffer)\b')

def annotate_string(line, masked):
    hits = {t for t in STRREF.findall(masked) if t in STRINGS}
    if len(hits) != 1: return ''
    txt = STRINGS[next(iter(hits))]
    if not txt or len(txt) > 90: return ''
    txt = txt.replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '\\r').replace('\t', '\\t')
    return f'   // "{txt}"'

def clean_line(line, names):
    line = line.rstrip()
    if GUESSED.match(line): return None
    m, lits = mask(line)
    byref = bool(BYREF.search(m))
    m = WEAK.sub('', m)
    m = REGCMT.sub('', m)
    if byref and m.rstrip().endswith(';'):
        m = m.rstrip() + '   // out-param'
    for pat, rep in TYPEMAP:
        m = re.sub(pat, rep, m)
    for _ in range(6):
        prev = m
        m = rewrite_punning(m)
        m = rewrite_elems(m)
        m = rewrite_derefs(m)
        if m == prev: break
    if names:
        m = names.sub(lambda x: names_map[x.group(0)], m)
    out = unmask(m, lits).rstrip()
    if out.strip() and not out.lstrip().startswith("//"):
        out += annotate_string(out, m)
    return out

# ---------------------------------------------------------------- goto/label
LABELDEF = re.compile(r'^\s*(LABEL_\d+):\s*$')
LABELDEF_INLINE = re.compile(r'^(\s*)(LABEL_\d+):\s*(\S.*)$')
GOTO = re.compile(r'\bgoto\s+(LABEL_\d+)\s*;')

def tidy_gotos(body):
    txt = "\n".join(body)
    used = collections.Counter(GOTO.findall(txt))
    out = []
    for ln in body:
        m = LABELDEF.match(ln) or LABELDEF_INLINE.match(ln)
        if m:
            lab = m.group(1) if LABELDEF.match(ln) else m.group(2)
            if not used[lab]:
                if LABELDEF.match(ln):        # bare dead label -> drop the line
                    continue
                mm = LABELDEF_INLINE.match(ln)
                out.append(f"{mm.group(1)}{mm.group(3)}")
                continue
        out.append(ln)
    # drop `goto L;` immediately followed by `L:`
    res, i = [], 0
    while i < len(out):
        g = GOTO.search(out[i])
        if g and out[i].strip() == g.group(0):
            nxt = next((k for k in range(i+1, len(out)) if out[k].strip()), None)
            if nxt is not None:
                mm = LABELDEF.match(out[nxt]) or LABELDEF_INLINE.match(out[nxt])
                if mm and (mm.group(1) if LABELDEF.match(out[nxt]) else mm.group(2)) == g.group(1):
                    i += 1; continue
        res.append(out[i]); i += 1
    return res

# ---------------------------------------------------------------- main
src = open(SRC, encoding="utf-8", errors="replace").read().split("\n")
recovered = json.load(open(f"{OUT}/_recovered.json"))
STRINGS = json.load(open(f"{OUT}/_asmstrings.json"))
STRINGS.update(json.load(open(f"{OUT}/_strings.json")))
parse     = json.load(open(f"{OUT}/_parse.json"))
addr2sub  = {f["addr"]: f["name"] for f in parse["funcs"] if f["name"]}

names_map = dict(CURATED_FUNCS)
for addr, nm in recovered.items():
    sub = addr2sub.get(addr)
    if sub and sub not in names_map: names_map[sub] = nm
names_map.update(CURATED_GLOBALS)
names = re.compile(r'\b(' + "|".join(sorted(map(re.escape, names_map), key=len, reverse=True)) + r')\b')
print(f"applying {len(names_map)} renames", file=sys.stderr)

marks = [(i, MARK.match(l).group(1)) for i, l in enumerate(src) if MARK.match(l)]
i_code = marks[0][0]
i_data = parse["i_data"]

funcs = []
for n, (ln, addr) in enumerate(marks):
    end = marks[n+1][0] if n+1 < len(marks) else len(src)
    raw = src[ln+1:end]
    body = [c for c in (clean_line(l, names) for l in raw) if c is not None]
    while body and not body[-1].strip(): body.pop()
    while body and not body[0].strip(): body.pop(0)
    body = tidy_gotos(body)
    sub  = addr2sub.get(addr)
    funcs.append({"addr": addr, "sub": sub,
                  "name": names_map.get(sub, sub) if sub else None,
                  "body": body})
print(f"cleaned {len(funcs)} functions", file=sys.stderr)
json.dump([{"addr":f["addr"],"sub":f["sub"],"name":f["name"]} for f in funcs],
          open(f"{OUT}/_funcs.json","w"))

# ---- decls + data ---------------------------------------------------------
decls = [c for c in (clean_line(l, names) for l in src[parse["i_decl"]+1:i_data-1]) if c is not None]
data  = [c for c in (clean_line(l, names) for l in src[i_data+1:i_code]) if c is not None]
open(f"{OUT}/_decls.txt","w").write("\n".join(decls))
open(f"{OUT}/_data.txt","w").write("\n".join(data))
json.dump([f["body"] for f in funcs], open(f"{OUT}/_bodies.json","w"))
print("wrote intermediate output", file=sys.stderr)

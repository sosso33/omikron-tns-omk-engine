#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import re, json, os, collections, sys

OUT, MAXFN = "clean", 140
funcs  = json.load(open(f"{OUT}/_funcs.json"))
bodies = json.load(open(f"{OUT}/_bodies.json"))
assert len(bodies) == len(funcs), (len(bodies), len(funcs))
for f, b in zip(funcs, bodies): f["body"] = "\n".join(b)

asmev = json.load(open(f"{OUT}/_asmev.json"))
ev    = json.load(open(f"{OUT}/_evidence.json"))

# ------------------------------------------------------------------ evidence
IMPORT_MOD = [
 ("ddraw", r'^DirectDrawCreate|^DirectDrawEnumerate'),
 ("dsound",r'^DirectSoundCreate|^DirectSoundEnumerate'),
 ("dinput",r'^DirectInputCreate|^joyGet|^joySet'),
 ("wave",  r'^(waveOut|waveIn|mmio|mixer)'),
 ("mci",   r'^mci(SendCommand|GetErrorString)'),
 ("video", r'^(AVI|IC)[A-Z]'),
 ("win32", r'^(RegisterClass|CreateWindowEx|PeekMessage|DispatchMessage|DefWindowProc|'
           r'ShowCursor|AdjustWindowRect|GetActiveWindow|MessageBox|LoadCursor|'
           r'SetWindowPos|MoveWindow|GetWindowRect|CreateDialogParam|EndDialog)'),
 ("reg",   r'^Reg(Open|Query|Set|Create|Close)Key'),
 ("ole",   r'^(CoInitialize|CoCreateInstance|CoUninitialize|CoTaskMem|LoadTypeLib|LoadRegTypeLib)'),
 ("net",   r'^(WSA|socket|recvfrom|sendto|inet_addr|gethostby|closesocket)'),
 ("thread",r'^(EnterCriticalSection|LeaveCriticalSection|InitializeCriticalSection|'
           r'CreateThread|SetEvent|ResetEvent|WaitForSingleObject|Interlocked)'),
 ("file",  r'^(CreateFile|ReadFile|WriteFile|SetFilePointer|FindFirstFile|GetPrivateProfile)'),
]
seed = {}
for a in ev["labeled"]:
    seed[a] = ev["labeled"][a]

sub2addr = {f["sub"]: f["addr"] for f in funcs if f["sub"]}
for proc, imps in asmev["imports"].items():
    a = sub2addr.get(proc)
    if not a: continue
    score = collections.Counter()
    for imp, c in imps.items():
        for mod, pat in IMPORT_MOD:
            if re.search(pat, imp): score[mod] += c
    if score:
        top = score.most_common(1)[0][0]
        if top != "thread" or a not in seed:      # thread is a weak signal
            seed.setdefault(a, top)

NAMEPFX = [("script", r'^Script_'), ("o3de", r'^o3de_'),
           ("sys", r'^(Sys_|Dbg_|Log_|Err_)'), ("mem", r'^Mem_')]
for f in funcs:
    if not f["name"]: continue
    for mod, pat in NAMEPFX:
        if re.match(pat, f["name"]): seed[f["addr"]] = mod
print(f"seeded {len(seed)} / {len(funcs)} functions", file=sys.stderr)

# ------------------------------------------------- contiguous fill + segment
funcs.sort(key=lambda f: f["addr"])
lab = [seed.get(f["addr"]) for f in funcs]
# forward fill, then backward fill for the head
last = None
for i, v in enumerate(lab):
    if v: last = v
    elif last: lab[i] = last
nxt = None
for i in range(len(lab) - 1, -1, -1):
    if lab[i]: nxt = lab[i]
    elif nxt: lab[i] = nxt

segs, cur = [], None
for f, l in zip(funcs, lab):
    if cur is None or cur["label"] != l or len(cur["fn"]) >= MAXFN:
        cur = {"label": l or "core", "fn": []}; segs.append(cur)
    cur["fn"].append(f)
# absorb runs of fewer than 8 functions into the previous segment
merged = []
for s in segs:
    if merged and len(s["fn"]) < 8 and merged[-1]["label"] == s["label"]:
        merged[-1]["fn"] += s["fn"]
    elif merged and len(s["fn"]) < 8:
        merged[-1]["fn"] += s["fn"]
    else:
        merged.append(s)
segs = merged
print(f"{len(segs)} modules", file=sys.stderr)
for s in segs:
    print(f"  {s['label']:8} {s['fn'][0]['addr']}-{s['fn'][-1]['addr']}  {len(s['fn']):4} fns",
          file=sys.stderr)
# (final segments are dumped after the relabel/merge pass below)

# ------------------------------------------------------------------ relabel
# 0x4B4000+ is statically-linked COM/OLE automation support (QueryInterface
# tables, typelib loading, E_NOINTERFACE returns), not game code.
for s in segs:
    if s["fn"][0]["addr"] >= "004B4000": s["label"] = "comlib"
merged=[]
for s in segs:
    if merged and merged[-1]["label"]==s["label"] and len(merged[-1]["fn"])+len(s["fn"])<=MAXFN:
        merged[-1]["fn"] += s["fn"]
    else: merged.append(s)
segs = merged

json.dump([{"label":s["label"],"lo":s["fn"][0]["addr"],"hi":s["fn"][-1]["addr"],
            "n":len(s["fn"]),"fns":[f["addr"] for f in s["fn"]]} for s in segs],
          open(f"{OUT}/_segs.json","w"))

DESC = {
 "file":"file I/O, archive/resource loading, async streaming",
 "win32":"window creation, message pump, dialogs, cursor",
 "sys":"core engine: memory, timing, math, scene and object management",
 "thread":"threading and critical-section wrappers",
 "wave":"WAV / MMIO sound file handling and the mixer",
 "ddraw":"DirectDraw surfaces, blitting, palettes",
 "dsound":"DirectSound buffers and playback",
 "dinput":"DirectInput keyboard / mouse / joystick",
 "d3d":"Direct3D / software rasteriser paths",
 "mci":"MCI (CD audio, video playback)",
 "video":"AVI / codec playback",
 "ole":"COM helpers",
 "comlib":"statically-linked COM/OLE automation support (not game code)",
 "o3de":"o3de 3D engine: scenes, objects, materials, textures",
 "script":"the in-game script VM and its opcode handlers",
 "reg":"registry access", "net":"networking", "mem":"allocator",
 "core":"unclassified engine code",
}

os.makedirs(f"{OUT}/src", exist_ok=True)
names = {f["addr"]: f["name"] for f in funcs}
recov = json.load(open(f"{OUT}/_recovered.json"))

# ---- runtime.h ----
open(f"{OUT}/runtime.h","w").write('''/* runtime.h - support definitions for the cleaned Runtime.exe decompilation.
 *
 * The decompiler emitted raw pointer arithmetic everywhere:
 *     *(_DWORD *)(a1 + 32)        -> u32(a1, 32)      byte offset
 *     *((_DWORD *)a1 + 8)         -> u32i(a1, 8)      element index
 *     *(float *)&v5               -> as_f32(v5)       type pun
 * The macros below are the readable spellings. All of them are lvalues, so
 * assignment through them still works exactly as in the original.
 */
#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>
#include <stddef.h>

typedef int unknown_t;          /* IDA _UNKNOWN: object of unknown size */

/* byte-offset access: u32(p, 32) is the dword 32 bytes into p */
#define BYTEPTR(p)      ((char *)(uintptr_t)(p))
#define u8(p, o)        (*(uint8_t  *)(BYTEPTR(p) + (o)))
#define i8(p, o)        (*(int8_t   *)(BYTEPTR(p) + (o)))
#define u16(p, o)       (*(uint16_t *)(BYTEPTR(p) + (o)))
#define i16(p, o)       (*(int16_t  *)(BYTEPTR(p) + (o)))
#define u32(p, o)       (*(uint32_t *)(BYTEPTR(p) + (o)))
#define i32(p, o)       (*(int32_t  *)(BYTEPTR(p) + (o)))
#define u64(p, o)       (*(uint64_t *)(BYTEPTR(p) + (o)))
#define i64(p, o)       (*(int64_t  *)(BYTEPTR(p) + (o)))
#define f32(p, o)       (*(float    *)(BYTEPTR(p) + (o)))
#define f64(p, o)       (*(double   *)(BYTEPTR(p) + (o)))

/* element-index access: u32i(p, 8) is p[8] treating p as uint32_t * */
#define u8i(p, i)       (((uint8_t  *)(uintptr_t)(p))[i])
#define i8i(p, i)       (((int8_t   *)(uintptr_t)(p))[i])
#define u16i(p, i)      (((uint16_t *)(uintptr_t)(p))[i])
#define i16i(p, i)      (((int16_t  *)(uintptr_t)(p))[i])
#define u32i(p, i)      (((uint32_t *)(uintptr_t)(p))[i])
#define i32i(p, i)      (((int32_t  *)(uintptr_t)(p))[i])
#define u64i(p, i)      (((uint64_t *)(uintptr_t)(p))[i])
#define i64i(p, i)      (((int64_t  *)(uintptr_t)(p))[i])
#define f32i(p, i)      (((float    *)(uintptr_t)(p))[i])
#define f64i(p, i)      (((double   *)(uintptr_t)(p))[i])

/* reinterpret the storage of a variable: as_f32(v5) is *(float *)&v5 */
#define as_u8(v)        (*(uint8_t  *)&(v))
#define as_i8(v)        (*(int8_t   *)&(v))
#define as_u16(v)       (*(uint16_t *)&(v))
#define as_i16(v)       (*(int16_t  *)&(v))
#define as_u32(v)       (*(uint32_t *)&(v))
#define as_i32(v)       (*(int32_t  *)&(v))
#define as_u64(v)       (*(uint64_t *)&(v))
#define as_i64(v)       (*(int64_t  *)&(v))
#define as_f32(v)       (*(float    *)&(v))
#define as_f64(v)       (*(double   *)&(v))

#endif /* RUNTIME_H */
''')

hdr = lambda t: f"/* {'='*74}\n * {t}\n * {'='*74} */\n"
open(f"{OUT}/globals.h","w").write(
    '/* globals.h - static data of Runtime.exe, cleaned.\n'
    ' * Names of the form dword_XXXXXX / byte_XXXXXX are the load address of the\n'
    ' * variable, kept so the listing stays cross-referenceable with IDA.\n */\n'
    '#ifndef GLOBALS_H\n#define GLOBALS_H\n#include "runtime.h"\n\n'
    + open(f"{OUT}/_data.txt").read() + "\n\n#endif\n")
open(f"{OUT}/decls.h","w").write(
    '/* decls.h - prototypes for every function in Runtime.exe. */\n'
    '#ifndef DECLS_H\n#define DECLS_H\n#include "runtime.h"\n\n'
    + open(f"{OUT}/_decls.txt").read() + "\n\n#endif\n")

index = []
for n, s in enumerate(segs, 1):
    fn = f"{n:02d}_{s['label']}.c"
    lo, hi = s["fn"][0]["addr"], s["fn"][-1]["addr"]
    named = [f for f in s["fn"] if f["addr"] in recov]
    with open(f"{OUT}/src/{fn}", "w") as fh:
        fh.write(f'/* {fn} - {DESC.get(s["label"], s["label"])}\n'
                 f' *\n * Runtime.exe 0x{lo} - 0x{hi}   ({len(s["fn"])} functions)\n'
                 f' * {len(named)} of them carry names recovered from the binary\'s debug strings.\n */\n'
                 f'#include "../runtime.h"\n#include "../globals.h"\n#include "../decls.h"\n\n')
        for f in s["fn"]:
            fh.write(f"/* 0x{f['addr']} */\n{f['body']}\n\n")
    index.append((fn, s["label"], lo, hi, len(s["fn"]), len(named)))

with open(f"{OUT}/SYMBOLS.md", "w") as fh:
    fh.write("# Recovered symbols\n\n"
             "Names recovered automatically from error strings the binary prints about\n"
             "itself (`o3de_GetObjectByIndex Error : index too big !` names the function\n"
             "that references it). A name is only accepted when exactly one function is\n"
             "the strongest claimant for it.\n\n"
             "| address | was | now |\n|---|---|---|\n")
    sub = {f["addr"]: f["sub"] for f in funcs}
    for a in sorted(recov):
        fh.write(f"| 0x{a} | `{sub.get(a)}` | `{recov[a]}` |\n")
    fh.write("\n## Hand-verified helpers\n\n"
             "Identified by reading the bodies; these are the most-called functions in\n"
             "the binary, so naming them cleans up thousands of call sites.\n\n"
             "| was | now | evidence |\n|---|---|---|\n")
    HELPERS = [
        ("sub_411DA0","Sys_FatalError","vsprintf -> MessageBoxA -> exit(1)"),
        ("sub_411EF0","Mem_Alloc","malloc + byte/count accounting, retries on failure"),
        ("sub_411FA0","Mem_Calloc","calloc + the same accounting"),
        ("sub_412060","Mem_Free","free + decrements the accounting"),
        ("sub_412100","Sys_GetTimeMs","timeGetTime() - base, honours a pause flag"),
        ("sub_440320","Err_SetMessage","vsprintf into the global error buffer"),
        ("sub_49EAD0","Log_Printf","guarded formatted logger, clamps to 512 bytes"),
        ("sub_4345E0","List_PickRandomByType","counts list nodes of a type, returns a random one"),
        ("nullsub_1","Dbg_Printf","stripped debug printf (\"alloue %d octets\")"),
        ("nullsub_2","Dbg_Trace","stripped debug hook"),
    ]
    for k, v, why in HELPERS:
        fh.write(f"| `{k}` | `{v}` | {why} |\n")
    fh.write("\n## Recovered globals\n\n| was | now |\n|---|---|\n")
    for k, v in [("dword_4E77F8","g_MemBytesAllocated"),("dword_4E76E8","g_MemAllocCount"),
                 ("dword_4E76C0","g_MemPeakBytes"),("byte_4E77FC","g_MemTraceEnabled"),
                 ("dword_4E77F4","g_TimeBaseMs"),("dword_4E76D4","g_TimePaused"),
                 ("byte_530808","g_ErrorMessageBuf")]:
        fh.write(f"| `{k}` | `{v}` |\n")
json.dump(index, open(f"{OUT}/_index.json","w"))
print("emitted", len(segs), "modules", file=sys.stderr)

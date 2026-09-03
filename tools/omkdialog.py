#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""omkdialog - walk an Omikron: The Nomad Soul conversation from the game data.

Reads gamedata/IAM/DIALOG with the structures recovered in docs/FILE_FORMATS.md,
runs the branch scripts on the bytecode VM from docs/SCRIPT_VM.md, and lets you
pick replies. Every variable write and engine call is printed, so if the struct
model were wrong the conversation would fall apart immediately.

    omkdialog.py                 list conversations
    omkdialog.py <n>             play conversation n
    omkdialog.py <n> --no-audio  without voice playback
    omkdialog.py <n> --set 89=1,90=1   pre-set game variables
    omkdialog.py --search jenna
    omkdialog.py --selftest      walk every conversation and report breakages

Voice lines are decoded with tools/adp.py, transcribed from the game's own
ADPCM decoder at 0x00483200, and played through afplay or ffplay.
"""
import omkpaths
import json, os, re, struct, subprocess, sys, shutil, tempfile

ROOT     = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIALOG   = omkpaths.data("IAM/DIALOG")
TAGDIR   = omkpaths.data("IAM")
MORPHDIR = omkpaths.data("MORPH")                       # PC (.3DM)
ADPDIRS  = [os.path.join(ROOT, "MORPH_ANALYSIS/separate"),      # Dreamcast
            os.path.join(ROOT, "MORPH_ANALYSIS/sort")]
VMTABLE  = omkpaths.clean("_vmtable.json")

C = {"hdr":"\033[1;36m", "npc":"\033[1;37m", "you":"\033[1;33m",
     "dbg":"\033[2;37m", "var":"\033[1;32m", "warn":"\033[1;31m",
     "dim":"\033[2m", "off":"\033[0m"}
if (not sys.stdout.isatty() or os.environ.get("NO_COLOR")
        or os.environ.get("OMKWEB_PLAIN")):
    C = {k: "" for k in C}

# ---------------------------------------------------------------- tag tables
def load_tags():
    out = {}
    for fn in os.listdir(TAGDIR):
        if not fn.upper().endswith(".TAG"): continue
        cur = {}
        raw = open(os.path.join(TAGDIR, fn), "rb").read().decode("cp1252", "replace")
        for line in raw.splitlines():
            m = re.match(r"^(\d+)=(.*)$", line)
            if m: cur[int(m.group(1))] = m.group(2).strip()
        out[fn[:-4].upper()] = cur
    return out
TAGS = load_tags()

def tagname(section, i):
    return TAGS.get(section, {}).get(i)

# ---------------------------------------------------------------- archive
def load_chunks():
    d = open(DIALOG, "rb").read(); n = len(d); first = None
    for i in range(n // 8):
        off, size = struct.unpack_from("<II", d, 8 * i)
        if off and size and off + size <= n:
            if first is None or off < first: first = off
        if first is not None and 8 * (i + 1) > first: break
    out = {}
    for i in range(first // 8):
        off, size = struct.unpack_from("<II", d, 8 * i)
        if off and size and off + size <= n and size >= 8:
            out[i] = d[off:off + size]
    return out

def parse(b):
    """-> (speakerObjectId, nodes, cameras) or None"""
    speaker, nn, nc, _ = struct.unpack_from("<4h", b, 0)
    if nn <= 0 or nc <= 0 or 8 + 64 * nn + 44 * nc > len(b): return None
    nodes = []
    for j in range(nn):
        o = 8 + 64 * j
        nodes.append({
            "index": j,
            "ptr":   struct.unpack_from("<9I", b, o),
            "param": struct.unpack_from("<4h", b, o + 36),
            "id":    struct.unpack_from("<h",  b, o + 44)[0],
            "name":  b[o + 46:o + 56].split(b"\0")[0].decode("ascii", "replace"),
        })
    cams = []
    for j in range(nc):
        o = 8 + 64 * nn + 44 * j
        cams.append({"id": struct.unpack_from("<h", b, o + 24)[0],
                     "pos": struct.unpack_from("<6i", b, o),
                     "angle": struct.unpack_from("<2h", b, o + 28),
                     "subject": struct.unpack_from("<2H", b, o + 32)})
    return speaker, nodes, cams

def strings_at(b, at, count=6):
    """The pool walk 0x004011D0 performs: index -1 is the spoken line,
    index k is reply k."""
    out, p = [], at
    for _ in range(count):
        if p >= len(b): break
        e = b.find(b"\0", p)
        if e < 0: break
        out.append(b[p:e].decode("cp1252", "replace"))
        p = e + 1
    return out

# ---------------------------------------------------------------- the VM
# Opcode names, operand lengths and per-field annotation are DELEGATED to
# tools/dialog_disasm.py and tools/script_dump.py, the modules the phase-1
# work and tools/verify.py run on - so this trace cannot drift from them the
# way its own three-entry label map once did (it still said "engine op_52"
# after 52 was established as inventory.remove_all). Both modules read their
# data with repo-relative paths, hence the cwd guard.
_cwd = os.getcwd()
try:
    os.chdir(ROOT)
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import dialog_disasm as _disasm
    import script_dump as _sdump
finally:
    os.chdir(_cwd)

# Same two sources, same order, as dialog_disasm._vm_table(): the listing-
# derived clean/ when present, the committed tables/ otherwise.
TAB = _disasm.TAB
LEN_FIX = {42: 3}
SECTION = {10: "VARIABLES", 12: "VARIABLES", 13: "VARIABLES"}
for _op in range(14, 25): SECTION[_op] = "VARIABLES"
SECTION.update({61: "DIALOGS", 64: "ZONES", 65: "ZONES"})
for _op, _sec in _disasm.SECTION.items():
    if _op not in SECTION: SECTION[_op] = _sec

def oplen(op):
    return _disasm.oplen(op)

class VM:
    """Only the opcodes the shipped dialogue scripts use are implemented.
    Anything else is reported and stepped over - actions are flat sequences of
    engine calls, so skipping one does not desynchronise the stream."""

    def __init__(self, vars_, log):
        self.vars, self.log = vars_, log

    def run(self, code, at, quiet=False):
        st, pc = [], at
        guard = 0
        while True:
            guard += 1
            if guard > 10000: self.log(f"{C['warn']}runaway script{C['off']}"); return 0
            op = code[pc]
            n = oplen(op)
            if n is None:
                self.log(f"{C['warn']}unknown opcode {op}{C['off']}"); return st[-1] if st else 0
            raw = code[pc + 1:pc + 1 + n]
            pc += 1 + n
            if op == 3:
                return st[-1] if st else 0
            pc = self.step(op, raw, st, code, pc, quiet)

    def imm(self, raw):
        if len(raw) == 1: return struct.unpack("<b", raw)[0]
        if len(raw) == 2: return struct.unpack("<h", raw)[0]
        if len(raw) == 4: return struct.unpack("<i", raw)[0]
        return 0

    def vname(self, i):
        nm = tagname("VARIABLES", i)
        return f"{i} {nm!r}" if nm else f"{i}"

    def setvar(self, i, v, quiet):
        old = self.vars.get(i, 0)
        self.vars[i] = v
        if not quiet and old != v:
            self.log(f"  {C['var']}variable {self.vname(i)} set to {v}"
                     f"{C['dim']} (was {old}){C['off']}")

    def step(self, op, raw, st, code, pc, quiet):
        v = self.imm(raw)
        # --- stack / flow ---
        if op == 4:  return pc + v
        if op == 5:  return pc + v if st.pop() else pc
        if op == 6:  return pc if st.pop() else pc + v
        if op in (7, 8, 9): st.append(v); return pc
        if op == 10: st.append(self.vars.get(v, 0)); return pc
        if op == 11: st.pop(); return pc
        # --- variable writes ---
        if op == 12: self.setvar(v, 0, quiet); return pc
        if op == 13: self.setvar(v, 1, quiet); return pc
        if op == 17: self.setvar(v, self.vars.get(self.imm(raw), 0), quiet); return pc
        if op in (19, 20, 21, 22, 23, 24):
            x = st.pop() if st else 0
            cur = self.vars.get(v, 0)
            f = {19: lambda a, b: a + b, 20: lambda a, b: a - b,
                 21: lambda a, b: a * b, 22: lambda a, b: a // b if b else 0,
                 23: lambda a, b: a & b, 24: lambda a, b: a | b}[op]
            self.setvar(v, f(cur, x), quiet); return pc
        # --- comparisons: left is stack[-1], right is stack[-2] ---
        if 25 <= op <= 30:
            a = st.pop(); b = st.pop()
            f = {25: a == b, 26: a < b, 27: a > b,
                 28: a <= b, 29: a >= b, 30: a != b}[op]
            st.append(1 if f else 0); return pc
        if 31 <= op <= 38:
            a = st.pop(); b = st.pop()
            f = {31: a + b, 32: a - b, 33: a * b, 34: (a // b if b else 0),
                 35: a & b, 36: a | b,
                 37: 1 if (a and b) else 0, 38: 1 if (a or b) else 0}[op]
            st.append(f); return pc
        if op == 39: st.append(-st.pop()); return pc
        if op == 40: st.append(0 if st.pop() else 1); return pc
        if op == 41: st.append(~st.pop()); return pc
        if op == 42:
            target = struct.unpack("<h", raw[:2])[0]
            return pc if (st[-1] if st else 0) == raw[2] else pc + target
        # --- engine calls: report, do not touch the stack ---
        if not quiet:
            label = _disasm.NAME.get(op) or f"engine op_{op}"
            val, note = _sdump.operand_text(op, raw)
            what = (val + note).strip() if (val or note) else ""
            self.log(f"  {C['dbg']}{label}{': ' if what else ''}{what}{C['off']}")
        return pc

# ---------------------------------------------------------------- audio
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adp                       # the decoder transcribed from sub_483200
import morph3dm                  # PC .3DM reader

def find_voice(name):
    """Prefer the PC morph file; fall back to the Dreamcast extraction.
    -> (path, kind) or None."""
    p = os.path.join(MORPHDIR, f"{name}.3DM")
    if os.path.exists(p): return p, "3dm"
    p = os.path.join(MORPHDIR, f"{name.upper()}.3DM")
    if os.path.exists(p): return p, "3dm"
    a = find_adp(name)
    return (a, "ddm") if a else None

def find_adp(name):
    for d in ADPDIRS:
        if not os.path.isdir(d): continue
        for cand in (f"{name}.DDM.ADP", f"{name}.ADP", f"{name.upper()}.DDM.ADP"):
            p = os.path.join(d, cand)
            if os.path.exists(p): return p
    return None

_player = None
def play_voice(path, kind):
    """Decode and hand to the system player without blocking."""
    global _player
    note = ""
    try:
        if kind == "3dm":
            pcm, ch, _lay = morph3dm.read(path)
        else:
            pcm, ch = adp.read(path)
            note = ", Dreamcast"
    except Exception:
        return None
    wav = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    wav.write(adp.wav(pcm, ch)); wav.close()
    exe = shutil.which("afplay") or shutil.which("ffplay")
    if exe:
        args = ([exe, wav.name] if exe.endswith("afplay") else
                [exe, "-nodisp", "-autoexit", "-loglevel", "quiet", wav.name])
        try:
            _player = subprocess.Popen(args, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL)
        except Exception:
            pass
    return len(pcm) // (2 * ch) / adp.RATE, note

def stop_audio():
    global _player
    if _player and _player.poll() is None:
        try: _player.terminate()
        except Exception: pass
    _player = None

# ---------------------------------------------------------------- driver
def playable(chunks):
    out = []
    for i, b in sorted(chunks.items()):
        p = parse(b)
        if p: out.append((i, p))
    return out

def cmd_list(chunks, filt=None):
    rows = playable(chunks)
    print(f"{C['hdr']}{len(rows)} conversations in gamedata/IAM/DIALOG{C['off']}\n")
    print(f"{'id':>5}  {'nodes':>5} {'cams':>4}  name (IAM/DIALOGS.TAG)")
    for i, (sp, nodes, cams) in rows:
        nm = tagname("DIALOGS", i) or ""
        if filt and filt.lower() not in nm.lower() and filt != str(i): continue
        print(f"{i:5}  {len(nodes):5} {len(cams):4}  {nm}")
    print(f"\n{C['dim']}play one with:  python3 tools/omkdialog.py <id>{C['off']}")

def cmd_play(chunks, idx, audio=True, preset=None):
    if idx not in chunks: sys.exit(f"no chunk {idx}")
    p = parse(chunks[idx])
    if not p: sys.exit(f"chunk {idx} is not a conversation")
    b = chunks[idx]
    speaker, nodes, cams = p
    title = tagname("DIALOGS", idx) or "(unnamed)"
    print(f"\n{C['hdr']}=== dialogue {idx}: {title} ==={C['off']}")
    # `speaker` is a scene-local object id resolved at load time by
    # Scene_FindObjectIndexById, NOT an index into OBJECTS.TAG - so it is not
    # annotated here.
    print(f"{C['dim']}{len(nodes)} nodes, {len(cams)} cameras, "
          f"speaker object id {speaker}{C['off']}\n")

    variables, log = dict(preset or {}), lambda s: print(s)
    if variables:
        print(f"{C['hdr']}pre-set variables:{C['off']}")
        for i in sorted(variables):
            nm = tagname("VARIABLES", i)
            print(f"  {C['var']}{i} {nm!r} = {variables[i]}{C['off']}" if nm
                  else f"  {C['var']}{i} = {variables[i]}{C['off']}")
        print()
    vm = VM(variables, log)
    node_i, visited = 0, 0

    while True:
        visited += 1
        if visited > 200: print("(too many hops, stopping)"); break
        if not (0 <= node_i < len(nodes)):
            print(f"{C['dim']}-- end of conversation --{C['off']}"); break
        nd = nodes[node_i]
        pool = strings_at(b, nd["ptr"][8]) if nd["ptr"][8] else []
        # string[0] is the spoken line (0x004011D0 with index -1). string[5]
        # has its own accessor (0x00401220) and carries the player's own line;
        # in the shipped file 1072 nodes use [0] alone, 9 use [5] alone and 19
        # use both.
        line   = pool[0].strip() if len(pool) > 0 else ""
        selfline = pool[5].strip() if len(pool) > 5 else ""

        cam = next((c for c in cams if c["id"] == nd["id"]), None)
        print(f"{C['dim']}[node {nd['index']} id={nd['id']} asset={nd['name']}"
              + (f" cam={cam['id']} subject={cam['subject']}" if cam else "")
              + f"]{C['off']}")
        if selfline:
            print(f"{C['you']}YOU: {selfline}{C['off']}")
        if line:
            print(f"{C['npc']}NPC: {line}{C['off']}")
        if not line and not selfline:
            print(f"{C['dim']}(no line){C['off']}")

        if audio and nd["name"]:
            v = find_voice(nd["name"])
            if v:
                r = play_voice(*v)
                if r: print(f"  {C['dbg']}voice {os.path.basename(v[0])} "
                            f"({r[0]:.1f}s{r[1]}){C['off']}")
                else: print(f"  {C['dbg']}voice {os.path.basename(v[0])} "
                            f"could not be decoded{C['off']}")
            else:
                print(f"  {C['dbg']}no voice file for {nd['name']}{C['off']}")


        # replies: string[1+k], gated by the branch condition ptr[k]
        choices = []
        for k in range(4):
            text = pool[1 + k] if len(pool) > 1 + k else ""
            if not text and nd["param"][k] == -1: continue
            ok = True
            if nd["ptr"][k]:
                print(f"  {C['dbg']}evaluating condition for reply {k+1}:{C['off']}")
                ok = bool(vm.run(b, nd["ptr"][k]))
                print(f"  {C['dbg']}-> {'available' if ok else 'hidden'}{C['off']}")
            if ok: choices.append((k, text))

        if not choices:
            print(f"\n{C['dim']}-- end of conversation --{C['off']}"); break

        print()
        for n, (k, text) in enumerate(choices, 1):
            tgt = nd["param"][k]
            print(f"  {C['you']}{n}) {text or '(continue)'}{C['off']}"
                  f"{C['dim']}  -> node {tgt if tgt != -1 else 'end'}{C['off']}")
        try:
            sel = input(f"\n{C['you']}choose [1-{len(choices)}, q]: {C['off']}").strip()
        except (EOFError, KeyboardInterrupt):
            print(); break
        stop_audio()
        if sel.lower().startswith("q"): break
        if not sel.isdigit() or not (1 <= int(sel) <= len(choices)):
            print("?"); continue
        k, _ = choices[int(sel) - 1]

        if nd["ptr"][4 + k]:
            print(f"  {C['dbg']}running action script:{C['off']}")
            vm.run(b, nd["ptr"][4 + k])
        node_i = nd["param"][k]
        print()

    if variables:
        print(f"\n{C['hdr']}variables touched:{C['off']}")
        for i in sorted(variables):
            nm = tagname("VARIABLES", i)
            print(f"  {i:5} = {variables[i]:<6} {nm or ''}")
    stop_audio()

def selftest(chunks):
    """Walk every conversation, always taking the first available reply, and
    report anything that breaks. Exercises the node layout, the string pool
    walk, the branch targets and the script VM across the whole corpus."""
    rows = playable(chunks)
    bad, lines, hops, scripts = [], 0, 0, 0
    for idx, (sp, nodes, cams) in rows:
        b = chunks[idx]
        vars_, msgs = {}, []
        vm = VM(vars_, lambda s: msgs.append(s))
        node_i, seen = 0, 0
        try:
            while 0 <= node_i < len(nodes) and seen < 200:
                seen += 1; hops += 1
                nd = nodes[node_i]
                pool = strings_at(b, nd["ptr"][8]) if nd["ptr"][8] else []
                if (pool and pool[0].strip()) or (len(pool) > 5 and pool[5].strip()):
                    lines += 1
                choices = []
                for k in range(4):
                    text = pool[1 + k] if len(pool) > 1 + k else ""
                    if not text and nd["param"][k] == -1: continue
                    if nd["ptr"][k]:
                        scripts += 1
                        if not vm.run(b, nd["ptr"][k], quiet=True): continue
                    choices.append(k)
                if not choices: break
                k = choices[0]
                if nd["ptr"][4 + k]:
                    scripts += 1
                    vm.run(b, nd["ptr"][4 + k], quiet=True)
                node_i = nd["param"][k]
            if seen >= 200: bad.append((idx, "did not terminate in 200 hops"))
        except Exception as e:
            bad.append((idx, f"{type(e).__name__}: {e}"))
    print(f"walked {len(rows)} conversations: {hops} nodes visited, "
          f"{lines} lines shown, {scripts} scripts run")
    if bad:
        print(f"{C['warn']}{len(bad)} failures:{C['off']}")
        for i, e in bad[:20]: print(f"  chunk {i}: {e}")
    else:
        print(f"{C['var']}no failures{C['off']}")

def main():
    args = [a for a in sys.argv[1:]]
    audio = "--no-audio" not in args
    preset = {}
    rest = []
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("--audio", "--no-audio"): pass
        elif a == "--set" and i + 1 < len(args):
            i += 1
            for pair in args[i].split(","):
                k, _, v = pair.partition("=")
                if k.strip().isdigit(): preset[int(k)] = int(v or 1)
        else: rest.append(a)
        i += 1
    args = rest
    chunks = load_chunks()
    if not args: return cmd_list(chunks)
    if args[0] == "--selftest": return selftest(chunks)
    if args[0] == "--search": return cmd_list(chunks, args[1] if len(args) > 1 else None)
    if args[0].isdigit(): return cmd_play(chunks, int(args[0]), audio, preset)
    print(__doc__)

if __name__ == "__main__":
    main()

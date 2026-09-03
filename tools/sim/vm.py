#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""The script VM, executed rather than disassembled - phase 6 stage 1.

`Script_Execute` (0x00406460) is the real interpreter: it clears the dry-run
flag, then loops while the context's status word is 1, fetching one opcode byte
and dispatching through the 153-entry table at 0x004C0140. This is that loop,
with the ~20 opcodes that carry control flow implemented for real against a
`gamestate.GameState`, and every other opcode **stubbed and traced**.

Stubbing is not a shortcut, it is the design. What we are trying to reproduce
is what the game *decides*, not what it draws: `camera.set` at 3659 sites,
`media.play`, `scx.play*` and `dialog.start` all leave the control flow alone,
so a run that records them is a complete trace of the decisions even though
nothing is rendered. An opcode with no implementation and no stub, by contrast,
fails loudly - which is the point of doing this in Python first.

    python3 tools/sim/vm.py            # run every world script, report
    python3 tools/sim/vm.py --selftest
    python3 tools/sim/vm.py AREA 22    # trace one chunk's scripts

The semantics below are read from the handlers, not inferred from the corpus:

* **every 16-bit operand goes through one fetch.** Read u16 little-endian,
  advance the pc by 2, and unless the value is 0xFFFF, bit `0x4000` means it is
  INDIRECT: clear the bit and take `params[value]` instead, sign-extended,
  from the int16 table at context `+36` offset by one element. Ops 4, 6, 10,
  18 and 42 all do this identically;
* **branch targets are relative to the pc AFTER the operand** -
  `movsx edx, cx / add edx, esi` where esi is that pc;
* **`jmp_if_false` pops** (`dec word [ctx+20]`) and jumps when the popped
  value is zero; `jmp_if_true` is its opposite;
* **`case` peeks, it does not pop**, compares against a signed byte label and
  jumps to the target when they DIFFER - i.e. falls through into the case body
  when they match, leaving the switch value on the stack for the next `case`;
* **op 18 is `set.var.pop`**, not the `set.var.bit` the table used to say: it
  pops and calls `Var_Set(operand, value)`. Corrected here after reading it.
"""
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
ROOT = os.path.dirname(os.path.dirname(HERE))

import dialog_disasm as D
import gamestate
import omkpaths

RUNNING = 1                     # ctx+22; any other value suspends the script


class Trace:
    """What the run decided. The trace is the product, so it is a first-class
    object rather than a print: counts per opcode, and the calls a stub made."""

    def __init__(self, keep=False):
        self.ops = {}
        self.calls = [] if keep else None
        self.keep = keep

    def op(self, name):
        self.ops[name] = self.ops.get(name, 0) + 1

    def call(self, name, fields):
        self.op(name)
        if self.keep is not False and self.calls is not None:
            self.calls.append((name, fields))


class Halt(Exception):
    """A script stopped for a reason that is not `end` - the status word."""
    def __init__(self, status): self.status = status


class VM:
    """One script context. Mirrors the fields Script_Execute uses:
    +12 pc, +16/+20 the stack and its index, +22 status, +36 the params."""

    def __init__(self, state=None, trace=None, params=None, visited=None):
        self.state = state or gamestate.load()
        self.trace = trace or Trace()
        self.params = params or []
        self.stack = []
        self.status = RUNNING
        self.limit = 200000          # a real runaway, not a short loop
        #: Whether `ui.open` parks the script (see `run`). Off by default so
        #: the standalone sweeps keep meaning what they meant; the golden-trace
        #: replay turns it on, because there it is a fidelity question.
        self.ui_open_parks = False
        self.visited = visited       # every pc executed, for the boundary test

    # ------------------------------------------------------------- operands
    def _fetch16(self, code, pc):
        """-> (value, pc). The shared operand fetch, indirection included."""
        v = code[pc] | (code[pc + 1] << 8)
        pc += 2
        if v != 0xFFFF and (v & 0x4000):
            i = v & 0x3FFF
            v = self.params[i] if i < len(self.params) else 0
        elif v & 0x8000:
            v -= 0x10000
        return v, pc

    @staticmethod
    def _imm(raw):
        if len(raw) == 1: return struct.unpack("<b", raw)[0]
        if len(raw) == 2: return struct.unpack("<h", raw)[0]
        if len(raw) == 4: return struct.unpack("<i", raw)[0]
        return 0

    # ------------------------------------------------------------ variables
    def var(self, i):
        try: return self.state.var(i)
        except Exception: return 0

    def set_bit(self, array, i, value):
        """One bit of a game-DB bitmap, read-modify-write. `array` indexes
        gamestate.ARRAYS: 3 objects shown, 4 addresses, 5 zones."""
        try:
            off = self.state.offset(array) + i // 8
            mask = 1 << (i % 8)
            b = self.state.raw[off]
            self.state.raw[off] = (b & ~mask) | (mask if value else 0)
        except Exception:
            pass

    def setvar(self, i, v):
        try:
            off = self.state.offset(0) + 4 * i
            struct.pack_into("<i", self.state.raw, off, v & 0xFFFFFFFF
                             if v >= 0 else v)
        except Exception:
            pass

    # ------------------------------------------------------------- the loop
    def run(self, code, at):
        """Execute from `at`. -> ("end" | "suspended" | reason, pc).

        Returns rather than raises on a stop, because a suspended script is
        normal - `scx.play.wait` writes 4 into the status word and the
        scheduler resumes it later.
        """
        pc, steps = at, 0
        self.status = RUNNING
        while True:
            steps += 1
            if steps > self.limit: return "runaway", pc
            if pc < 0 or pc >= len(code): return "pc out of range", pc
            op = code[pc]
            n = D.oplen(op)
            if n is None: return "unknown opcode %d" % op, pc
            if pc + 1 + n > len(code): return "operands run off the end", pc
            if self.visited is not None: self.visited.add(pc)
            start, raw = pc, code[pc + 1:pc + 1 + n]
            pc += 1 + n
            try:
                pc = self.step(op, raw, code, start, pc)
            except IndexError:
                return "stack underflow at %d (op %d)" % (start, op), start
            except Halt as h:
                self.status = h.status
                return "suspended", pc
            if op == 3: return "end", pc
            # Script_Execute returns outright after dialog.start
            if op == 61: return "dialog", pc
            # `ui.open` PARKS its caller at status 6 and names a result
            # variable; only `Game_HandleEvent` case 5 - a person answering the
            # screen - writes that variable and resumes it. Nothing in the pump
            # can release it.
            #
            # Off by default, because every standalone sweep here predicts one
            # slot with no screens and no scheduler, and stopping would change
            # what those sweeps mean. `engine/` models it always, and porting
            # it is what turned this up: with the park on, `resto-387`'s
            # disagreements went 6 -> 5.
            if op == 70 and self.ui_open_parks: return "ui.open", pc

    # --------------------------------------------------------------- opcodes
    def step(self, op, raw, code, start, pc):
        T = self.trace
        name = D.NAME.get(op) or "op_%d" % op
        st = self.stack

        # ---- flow -------------------------------------------------------
        if op in (0, 1, 2):                       # dbg.dump_ctx/code, nop
            T.op(name); return pc
        if op == 3:
            T.op(name); return pc
        if op in (4, 5, 6):
            v, _ = self._fetch16(code, start + 1)
            T.op(name)
            target = pc + v
            if op == 4: return target
            cond = st.pop()
            if op == 5: return target if cond else pc
            return pc if cond else target
        if op == 42:                              # case label, target
            v, p2 = self._fetch16(code, start + 1)
            label = struct.unpack("<b", code[p2:p2 + 1])[0]
            T.op(name)
            # peek, never pop: the switch value stays for the next case
            top = st[-1] if st else 0
            # The target is measured from the pc after the 16-bit operand -
            # BEFORE the label byte. The handler computes `add esi, ecx` while
            # ecx still points at the label and only then does `inc ecx`, so
            # counting the label in lands one byte past every case: the jump
            # arrives mid-instruction, which showed up as 4 "unknown opcode"
            # slots and 41 `drop`s onto an empty stack - the switch value never
            # pushed because the push had been jumped over.
            return pc if top == label else p2 + v

        # ---- the stack --------------------------------------------------
        if op in (7, 8, 9):
            st.append(self._imm(raw)); T.op(name); return pc
        if op == 10:
            v, _ = self._fetch16(code, start + 1)
            st.append(self.var(v)); T.op(name); return pc
        if op == 11:
            st.pop(); T.op(name); return pc

        # ---- variable writes --------------------------------------------
        if op in (12, 13):
            v, _ = self._fetch16(code, start + 1)
            self.setvar(v, 0 if op == 12 else 1); T.op(name); return pc
        if op in (14, 15, 16):
            v = struct.unpack_from("<h", raw, 0)[0]
            imm = self._imm(raw[2:])
            self.setvar(v, imm); T.op(name); return pc
        if op == 17:
            a, b = struct.unpack_from("<2h", raw, 0)
            self.setvar(a, self.var(b)); T.op(name); return pc
        if op == 18:                              # set.var.pop
            v, _ = self._fetch16(code, start + 1)
            self.setvar(v, st.pop()); T.op(name); return pc
        if 19 <= op <= 24:
            v, _ = self._fetch16(code, start + 1)
            x, cur = st.pop(), self.var(v)
            f = {19: cur + x, 20: cur - x, 21: cur * x,
                 22: (cur // x if x else 0), 23: cur & x, 24: cur | x}[op]
            self.setvar(v, f); T.op(name); return pc

        # ---- arithmetic and comparison ----------------------------------
        if 25 <= op <= 30:
            a, b = st.pop(), st.pop()
            f = {25: a == b, 26: a < b, 27: a > b,
                 28: a <= b, 29: a >= b, 30: a != b}[op]
            st.append(1 if f else 0); T.op(name); return pc
        if 31 <= op <= 38:
            a, b = st.pop(), st.pop()
            f = {31: a + b, 32: a - b, 33: a * b, 34: (a // b if b else 0),
                 35: a & b, 36: a | b, 37: 1 if (a and b) else 0,
                 38: 1 if (a or b) else 0}[op]
            st.append(f); T.op(name); return pc
        if op in (39, 40, 41):
            a = st.pop()
            st.append({39: -a, 40: 0 if a else 1, 41: ~a}[op]); T.op(name)
            return pc

        # ---- the state bitmaps -------------------------------------------
        # The plan's "second small group": these need no subsystem, only a bit,
        # and without them a spent trigger never retires and the lifecycle is
        # a fiction. `zone.disable` in particular is what a launch script uses
        # to retire itself - see the 387 launch.
        #
        # Note Zone_SetStateBit's decompilation is WRONG (a bare OR, which
        # could not clear anything); the assembly read-modify-writes, and that
        # is what is implemented. readable/src/01_file.c carries the rewrite.
        if op in (64, 65):                        # zone.enable / disable
            v, _ = self._fetch16(code, start + 1)
            self.set_bit(5, v & 0x7FFF, 1 if op == 64 else 0)
            T.call(name, (v,)); return pc
        if op in (87, 88):                        # address.enable / disable
            v, _ = self._fetch16(code, start + 1)
            self.set_bit(4, v, 1 if op == 87 else 0)
            T.call(name, (v,)); return pc
        if op == 71:                              # scene.load(area, scene)
            # The handler's last act is Area_SetLoadedScene(area, scene) -
            # `push esi / push edi / call sub_40B120` - so which scene is over
            # an area is GAME STATE, not a property of the area. Area_TickLoad
            # reads it straight back to decide whose startup script to run.
            a, sc = struct.unpack_from("<2h", raw, 0)
            if self.state is not None and 0 <= a < 259:
                self.state.set_scene_of(a, sc)
            T.call(name, (a, sc)); return pc

        # ---- everything else: stubbed, and recorded -----------------------
        # These do not touch the control flow, so the trace stays complete.
        T.call(name, struct.unpack_from("<%dh" % (len(raw) // 2), raw, 0)
               if len(raw) >= 2 else ())
        return pc


# ---------------------------------------------------------------- the corpus
def world_scripts():
    r"""Every executable world script: (arch, chunk, record, field, code, offset).

    Three sources, and the third was missing until 2026-08-29:

    * the zone records' three script fields (enter / activate / leave),
    * the message-subscription table,
    * **the chunk's own startup script at `+4`** - `AREA +4` / `SCENE +4`,
      which `Area_TickLoad` runs when the chunk loads (FILE_FORMATS). It is in
      no record table, so the record walk cannot reach it, and leaving it out
      is what made "no shipped script starts Impasse's beats" look true. It is
      reported as `rec -1 +4` so it stays distinguishable from a slot.
    """
    import dialog_triggers as T
    out = []
    for name in ("AREA", "SCENE"):
        for k, b in sorted(T.archive(omkpaths.data("IAM", name)).items()):
            at = struct.unpack_from("<I", b, 4)[0] if len(b) >= 8 else 0
            if 0 < at < len(b):
                out.append((name, k, -1, 4, b, at))
            r = T.LAYOUT[name](b)
            if not r: continue
            for rec, f, p in (list(T._scripts_from_records(b, r[0], r[1]))
                              + T._second_table(name, b)):
                out.append((name, k, rec, f, b, p))
    b, slots = T.global_file(omkpaths.data("IAM", "GLOBAL"))
    for rec, f, p in slots:
        out.append(("GLOBAL", 0, rec, f, b, p))
    return out


def run_corpus(keep=False):
    r"""Execute every slot and report what the run could fail on.

    The invariant is stage 1's: every slot reaches `end` - no unknown opcode,
    no stack underflow, and every jump landing inside its own script. It is
    deliberately a corpus-invariant run rather than a golden trace, because
    formats have had `verify.py` since day one and behaviour has had nothing:
    this is the first harness that tests what the game *does*.
    """
    state = gamestate.load()
    trace = Trace(keep)
    tot = ended = dialog = strayed = 0
    bad = {}
    for arch, chunk, rec, field, code, p in world_scripts():
        tot += 1
        seen = set()
        vm = VM(state, trace, visited=seen)
        why, _pc = vm.run(code, p)
        if why == "end": ended += 1
        elif why == "dialog": dialog += 1
        else: bad.setdefault(why, []).append((arch, chunk, rec, field))
        # Every pc executed must be an instruction boundary of THIS slot, as
        # the linear decode sees it. "Inside the buffer" is not enough - the
        # chunk holds every script back to back, so a jump one byte out lands
        # in a real instruction of a neighbouring one and runs happily. This is
        # the invariant that catches an arithmetic error in a branch target.
        ops, st = D.disasm(code, p, len(code))
        if st == "ok":
            bounds = {o[0] for o in ops}
            if not seen <= bounds:
                strayed += 1
                bad.setdefault("jumped outside the slot", []).append(
                    (arch, chunk, rec, field))
    return {"slots": tot, "ended": ended, "dialog": dialog, "strayed": strayed,
            "failed": sum(len(v) for v in bad.values()), "why": bad,
            "trace": trace}


def trace_chunk(arch, chunk):
    """Run one chunk's scripts and print what each decided."""
    state = gamestate.load()
    for a, k, rec, field, code, p in world_scripts():
        if a != arch or k != chunk: continue
        t = Trace(keep=True)
        vm = VM(state, t)
        why, _ = vm.run(code, p)
        calls = ", ".join("%s%s" % (n, list(f) if f else "")
                          for n, f in (t.calls or [])) or "-"
        print("  record %-3d slot +%-2d  %-8s %3d instructions"
              % (rec, field, why, sum(t.ops.values())))
        if t.calls: print("     " + calls)
    return 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) >= 2:
        return trace_chunk(args[0].upper(), int(args[1]))
    r = run_corpus(keep=False)
    print("%d world-script slots executed" % r["slots"])
    print("  reached `end` ............. %d" % r["ended"])
    print("  stopped at dialog.start ... %d   (Script_Execute returns there)"
          % r["dialog"])
    print("  jumped outside their slot . %d" % r["strayed"])
    print("  failed .................... %d" % r["failed"])
    for why, where in sorted(r["why"].items(), key=lambda kv: -len(kv[1])):
        print("      %-40s %4d  e.g. %s" % (why, len(where), where[0]))
    ops = r["trace"].ops
    print("  %d distinct opcodes executed, %d instructions"
          % (len(ops), sum(ops.values())))
    top = sorted(ops.items(), key=lambda kv: -kv[1])[:12]
    print("  busiest: " + ", ".join("%s %d" % t for t in top))
    if "--selftest" in sys.argv:
        ok = r["failed"] == 0 and r["strayed"] == 0 and r["slots"] == 5785
        print("selftest: %s" % ("ok" if ok else "FAILED"))
    return 0 if r["failed"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

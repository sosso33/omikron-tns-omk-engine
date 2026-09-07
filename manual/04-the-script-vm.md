# 4. The script VM

← [Contents](README.md) · prev: [The data](03-the-data.md) · next: [The world](05-the-world.md)

---

## In short

The game's behaviour is a program, and the program is in the data files.

Walk into a doorway and a script runs. It is bytecode — one byte per
instruction, operands following — interpreted by a 153-entry table compiled
into the executable. There are 5 785 of these scripts in the shipped game, and
they are what says *this conversation starts here*, *this door opens*, *this
camera watches*, *this variable is now 1*.

The one idea worth carrying out of this chapter is how a script **waits**. It
does not block, and nothing polls. A script that opens a menu, or starts a
cutscene, or asks the player to walk somewhere, simply **parks**: it writes a
number into its own status word and stops, with its program counter and stack
intact. Later something else — a screen closing, an animation finishing, a
camera arriving — writes 1 back and it carries on from the next instruction.

That is why a conversation can interrupt a cutscene, why an area transition can
take three seconds of streaming without the game stopping, and why the whole
engine is one loop with no threads in it.

## In detail

### The interpreter

`sub_4060B0` is the whole of it:

```c
pc = ctx->code;                     /* ctx+4  */
op = *pc++;
while (op != 3 && !(ctx->flags & 0x10)) {
    handlers[op](ctx);              /* table at 0x004C0140 */
    pc = ctx->pc;                   /* handlers advance it themselves */
    op = *pc++;
}
```

An opcode is one byte; **opcode 3 ends the script**; bit `0x10` of `ctx+40`
aborts the run. The context is small:

```
+4   uint8 *code       the script, as passed in
+12  uint8 *pc
+16  int32 *stack
+20  uint16 sp         one past the top
+22  uint16 status     the park word — see below
+36  int16 *fixups     the operand indirection table
+40  uint8  flags      bit 0x10 = abort
```

The table at `0x004C0140` is 153 entries of `{handler, operandBytes}`. It is
lifted to `tables/vm_opcodes.json`, because a replica cannot read it out of any
data file.

### Operands, and a lesson about believing a table

Most operands are literals following the opcode. Some are **indirect**: an
index into the context's fixup table, which is how a script refers to a value
resolved at run time rather than baked in.

The operand *lengths* in the table were checked against the handlers
themselves, and the exercise is worth recording because it is the shape of
mistake this repository is built to catch. Recovering lengths from handler
assembly produced **21 disagreements** with the table. Applying all 21 made the
corpus decode *worse* — 53 failures became 58. Tested one at a time, **6 were
real**.

Then the reverse: opcode 103's assembly reads 6 bytes in a straight line, and
it was recorded as *wrong* — left at the table's 2 — because a corpus test
showed a script breaking at 6. That test had been run while three other lengths
were still wrong, and it was those that were breaking. Corrected, op 103 at 6
decodes 5 785 of 5 785 scripts with **2 056 fewer instructions**: exactly the
four surplus bytes per site, which at 2 had been decoding as phantom
instructions.

So a corpus verdict is only as good as the rest of the table, and where the
assembly is unambiguous a disagreement is a symptom to locate rather than a
verdict to accept.

### The status word — how a script waits

`Script_Execute` runs `while (status == 1)`. A handler that writes anything
else parks its caller, and something else must write 1 back. **Nothing polls**:
every resume is an event or a step of the pump.

| status | what it means | resumed by |
|---|---|---|
| 0 | idle; the action queue may be armed | the pump, when it dequeues an action |
| 1 | running | — |
| 3 | in a fight | the event handler's case 2 — the **first** context at 3 |
| 4 | waiting for a scene object or a player move | case 3, when the object or the move finishes |
| 5 | a transition caller a *second* transition superseded | **nothing found** |
| 6 | waiting for a screen | case 5, from the answer or either close key |
| 7 | waiting for a camera move | case 4, when the move ends |
| 8 | waiting for `area.preload` | the pump's tail, once the load reports |
| 9 | retry the transition next frame | the pump's head, unconditionally |
| 10 | waiting on the area transition | the pump's tail |
| 11 | the transition's last step is done | case 3's `else` arm |

Two details of the plumbing that a reader reconstructs wrongly otherwise:

* **Queued actions do not overtake a parked script.** The dispatcher refuses to
  arm anything while the status is non-zero, so an action waits behind the
  script that is parked, and an action is over only when `end` runs.
* **Status 5 has no resumer anywhere in the image.** A transition caller that a
  second transition supersedes is parked for good. That is the engine's own
  shape, not a gap in the reading.

### What the corpus exercises

Of the 153 opcodes the table defines, the shipped world scripts execute **124**.
**129 are named**, which covers 99.97% of executed instructions — every opcode
the game actually reaches is either named or explicitly recorded as read and
left alone.

Some of the names came out of the game rather than out of a guess. Opcodes 150
and 151 turned out to install and remove the engine's **second render bank**,
which is the same scene walk converting every vertex colour to luma: 14 shipped
installs against 15 restores, and 74 of the 82 instructions between them are
camera and fade opcodes. So the game has **black-and-white cutscenes**, and the
opcodes are `render.grey.on` / `render.grey.off`.

### The accident that makes the original observable

Every handler announces its operand by name, through `GetPrivateProfileStringA`
on the `IAM\*.TAG` tables — and that call sits *before* the debug window's
`if (hWnd)`. So the shipped, unmodified executable narrates what it is doing to
anyone watching the Win32 profile-string API, with no patch, no shim and no
debugger.

That is the whole basis of the golden traces in chapter 12, and it is luck
rather than design.

## Where it lives

| | |
|---|---|
| the finding | `docs/SCRIPT_VM.md` — the table, the status word, the naming |
| the table | `tables/vm_opcodes.json`, `tables/vm_announce.json` |
| the port | `engine/src/script/interp.cpp` (the handlers), `area.cpp` (the pump and the contexts) |
| the readers | `tools/dialog_disasm.py`, `tools/script_dump.py`, and `/world` in the web viewer |
| the checks | `verify.py: vm table sources`, `dialogue scripts`, `startup scripts` |

## What is not settled

* **24 opcodes are unnamed**, identified only by the operand domain they
  announce. They are not reached by any shipped world script, which is why
  nothing has forced the question.
* **Status 5 is parked for ever** by construction. Recorded as the engine's
  behaviour rather than as an open question, but it is the one status with no
  reader.
* The **indirect operand mode** exists in the handlers and the shipped
  conversations never use it: all 1 246 `dialog.start` operands are direct
  literals.

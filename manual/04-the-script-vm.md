# 4. The script VM

← [The data](03-the-data.md) · [Contents](README.md) · next: [The world](05-the-world.md)

---

## In short

The game is driven by a small interpreted machine. Level designers wrote
behaviour, it was compiled to bytecode, and it shipped in the data files. The
engine executes it.

The machine is deliberately simple: one byte of opcode, then its operands, a
tiny stack for arithmetic, and 153 instructions like "enable this trigger
zone", "start this conversation", "play this camera move", "set variable 408".
There are no functions, no loops in the usual sense, and no allocation.

The one idea that makes it feel less simple is **parking**. Some instructions
take time — a camera flying for 160 frames, a conversation, a menu waiting for
the player to choose a save slot. The script does not busy-wait for those. It
writes a number into its own status word and **stops mid-instruction**, keeping
its position and its stack. Something else — a menu answering, a camera
arriving, an area finishing loading — writes the status back to "running", and
the script continues from exactly where it stood.

Nothing polls. Every resume is an event.

That single design decision explains a lot of the engine's shape, and getting
it wrong in a port does not produce a crash — it produces a game that quietly
skips things.

## In detail

The interpreter is `sub_4060B0`. Full account:
[`docs/SCRIPT_VM.md`](../docs/SCRIPT_VM.md).

### The loop

```c
pc = ctx->code;                     /* ctx+4  */
op = *pc++;
while (op != 3 && !(ctx->flags & 0x10)) {
    handlers[op](ctx);              /* table at 0x004C0140 */
    pc = ctx->pc;                   /* handlers advance it themselves */
    op = *pc++;
}
```

* An **opcode is one byte**; operands follow it and the handler consumes them.
* **Opcode 3 ends the script.**
* Bit `0x10` of `ctx+40` aborts the run.

The context:

```
+4   uint8_t *script     the code, as passed in
+12  uint8_t *pc         each handler advances it itself
+16  int32_t *stack
+20  uint16_t sp         one past the top
+22  int16_t  status     1 = running  <-- the parking word
+36  int16_t *params     the indirect-operand table
+40  uint8_t  flags      bit 0x10 aborts
```

### Two entry points, one context

| | | |
|---|---|---|
| `Script_Run` | `0x004060B0` | **evaluates** — sets `g_ScriptDryRun`, so every handler consumes its operands and returns without acting |
| `Script_Execute` | `0x00406460` | **executes** — clears the flag; the real interpreter |

Every handler opens with the same dry-run test. That is why **an operand length
can be wrong without anything visibly failing**: in the evaluate pass, the
handler does nothing *but* consume operands. It is also why the conditions on a
dialogue reply can be evaluated for a value while its actions are not run —
see [chapter 7](07-conversations-and-cutscenes.md).

### Parking: the status word

`Script_Execute` runs `while (status == 1)`. A handler suspends its own script
by writing anything else, with pc and stack intact. Twelve statuses are
established, each with the opcode that writes it and the event that resumes it:

| status | means | written by | resumed by |
|---|---|---|---|
| 0 | idle; the action queue may be armed | op 3 `end`; `Script_NewContext` | `Script_ProcessActions`, dequeuing action 1/2/3 |
| 1 | running | every resume below | — |
| 3 | in a fight | op 62 `fight.begin` | event **2** — returns the **first** context at 3 to 1 |
| 4 | waiting for a scene object or a player move | ops 46, 58, 60, 89 | event **3** |
| 5 | a transition caller a second transition superseded | `Area_Transition` | **nothing found** — see below |
| 6 | waiting for a screen | op 70 `ui.open` | event **5** — `UI_SendAnswer`, or ESC/TAB |
| 7 | waiting for a camera move | op 96 (only when the travel is non-zero), op 126 | event **4** |
| 8 | waiting for `area.preload` | op 45 | the pump's tail, once `Area_TickLoad` reports |
| 9 | retry the transition next frame | a refused `area.goto` | `Script_ProcessActions`' head |
| 10 | waiting on the area transition | `Area_Transition` | the pump's tail |
| 11 | the transition's last step is done | a 60-second watchdog | event **3** |

Two plumbing details a reader will otherwise reconstruct wrongly:

* **Queued actions wait behind a parked script**, they do not overtake it —
  `Script_ProcessActions` refuses to arm anything while the status is non-zero.
  An action is over only when `end` runs.
* **A handler can retry itself.** `area.goto` rewinds the pc by 7 — exactly its
  own size — when `Area_Transition` refuses, so the instruction runs again next
  tick until the transition is accepted.

### An instruction

Operands are 16-bit, assembled from two byte loads. `0xFFFF` means "none". Bit
`0x4000` means the remainder **indexes** the parameter block at `ctx+36` rather
than being the value — and the index is offset by one word, which decides what
the indirection is *for*: `Message_RunHandlers` writes `args[0] = message id`,
`args[1] = sender`, so `push.i16 0x4000` pushes the sender. Every other context
has `ctx+36` zeroed, so an indirect operand anywhere else would dereference
null, which is exactly what the corpus shows.

Binary operators take the **left** operand from `stack[sp-1]` and the right
from `stack[sp-2]`, so the compiler emits the right-hand side first.

### What a real script looks like

Chunk 268, node 0, branch 0 — with ids resolved through the `.TAG` name tables
that ship beside the data:

```
condition:
    push.i8    1
    push.var   89     ; VARIABLES[89] = 'OE Table Corresp'
    cmp.eq
    push.i8    1
    push.var   90     ; VARIABLES[90] = 'OE Livre Astro 4'
    cmp.eq
    and
    end

action:
    zone.disable 2027 ; ZONES[2027] = 'Dialogue Savant'
    zone.enable  2028 ; ZONES[2028] = 'VO Post Dialogue'
    zone.enable  2040 ; ZONES[2040] = 'Test Traductech'
    op_76        464  ; OBJECTS[464] = 'Traductech'
    set.var2     408  ; VARIABLES[408] = 'Traductech'
    end
```

"Offer this reply only if the player has the correspondence table and the
fourth astronomy book. If they take it, close the scholar dialogue, open the
follow-up scene, and record that they now have the Traductech."

That is the whole game, in miniature. The engine knows nothing about scholars
or astronomy books; it knows how to compare two variables and how to enable a
zone.

### The table

153 opcodes, at `0x004C0140`, lifted to `tables/vm_opcodes.json`. **129 are
named**, covering 99.97% of executed instructions — every opcode the shipped
scripts exercise is either named or explicitly recorded as read-and-left.

Naming them was not a matter of reading 153 handlers in order. **21 operand
counts in the shipped table are wrong**, and the way that was established is
the most instructive episode in the repository:

* Recovering operand lengths from handler assembly gave 21 disagreements with
  the table. **Applying all 21 made things worse** — 53 corpus failures became
  58. Tested one at a time, 6 were real.
* Generalising "the handler never advances the pc, so it takes 0 operands" to
  all 16 such opcodes blew up to 275 failures. It was right for the 4 whose
  handlers had actually been read.
* **But a corpus verdict is only as good as the rest of the table.** Opcode 103
  reads 6 bytes in a straight line — no branch, nothing conditional — and was
  recorded as *wrong* because a corpus test showed a script breaking at 6. That
  test ran while ops 57, 58 and 78 still had wrong lengths, and it was *those*
  that were breaking. Corrected, op 103 at 6 decodes 5 785 / 5 785 with the same
  514 sites and **2 056 fewer instructions** — exactly the four surplus bytes
  per site, which at length 2 had been decoding as 1 531 phantom `dbg.dump_ctx`
  and 525 phantom `dbg.dump_code`.

The rule that comes out of it: where the assembly is unambiguous, a corpus
disagreement is a **symptom to locate, not a verdict to accept** — and a
conclusion recorded from a corpus that has since changed is worth re-running
rather than re-reading.

Two named pairs worth singling out, because they change what the game *is*:

* **150 / 151 — `render.grey.on` / `.off`.** They swap the renderer to a
  greyscale bank around a camera sequence. 14 shipped installs against 15
  restores, across eight chunks including the Morgue, Kay'l's apartment and the
  Bowie concert; 74 of the 82 instructions inside those brackets are camera and
  fade opcodes. **The game has black-and-white cutscenes.**
* **61 — `dialog.start`.** The only way into `Dialog_Load`. 1 246 sites, 100%
  valid, all direct literals.

### How the port runs it

`engine/src/script/interp.cpp` is the handler set; `engine/src/script/area.*`
is the Session that owns the contexts, the resident slots and the frame.

The status table above is ported as a table — and the reason it is worth
labouring is that **a port that gets parking wrong produces no error**. One
correction on record: the reading "starting a conversation ends the tick" was
wrong, and a port that ended the frame at opcode 61 skipped every later
context, the area transition and the camera tick with it.

The result that makes the VM more than plausible is behavioural: the port
reaches `ui.open(29, -1, → variable 19)` at pc 1078 of AREA 118's own startup
script, **parks the context the way the handler parks its caller**, walks
screen 29 for an answer, and resumes into `dialog.start 272` — matching the
original engine's own log 42 events of 42, in order.

A separate re-audit against the handlers (2026-09-02) found **18 places where
the rule being run was the port's and not the engine's**, none of them
reachable from the 5 958-slot sweep — which is why each is now checked by
hand-built bytecode rather than by the corpus.

## Where it lives

| | |
|---|---|
| findings | [`docs/SCRIPT_VM.md`](../docs/SCRIPT_VM.md) — the table, the 153 opcodes, parking, how conversations launch |
| the lifted table | `tables/vm_opcodes.json`, `tables/vm_announce.json` (derived from assembly, not hand-written — a hand-written one was wrong three ways in an hour) |
| the port | `engine/src/script/interp.cpp` (the handlers), `area.cpp`/`area.h` (the Session) |
| the reference | `tools/sim/vm.py` — a second, independent implementation in Python |
| listings | `tools/script_dump.py`, and the `/world` page of the web viewer |
| checks | `verify.py: world scripts`, `dialogue scripts`, `vm probe`, `trace agreement` |

## What is not settled

* **Status 5 is read and not resolved.** No event, pump step or handler writes
  that context back to running — it would be parked for good. It needs two
  transitions in flight, which the shipped scripts may never produce, and a
  claim either way needs a trace this rig cannot take.
* **24 opcodes are unnamed**, identified only by the operand domain they
  announce to the `.TAG` logger. They are the tail: 32 uses or fewer each.
* **A sweep built on `tools/script_dump.py` misses every startup script.** Its
  `scripts_of` enumerates a chunk's zone records and its second table, and
  nothing else — a chunk's `+4` startup script is not in it. That exact gap is
  why "no shipped script starts a cutscene's beats" stood as an open question
  for months while being false; see [chapter 13](13-open-questions.md).

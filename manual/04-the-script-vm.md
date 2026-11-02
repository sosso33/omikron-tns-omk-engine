# 4. The script VM

← [Contents](README.md) · prev: [The data](03-the-data.md) · next: [The world](05-the-world.md)

---

## In short

The game's behaviour is a program, and the program is in the data files.

Walk into a doorway and a script runs. It is bytecode — one byte per
instruction, operands following — interpreted through a 153-entry table
compiled into the executable. There are 5 785 world-script slots in the shipped
game, plus 612 small scripts inside the conversations, and they are what says
*this conversation starts here*, *this door opens*, *this camera watches*,
*this variable is now 1*, *this gunman patrols that corridor*.

The one idea worth carrying out of this chapter is how a script **waits**. It
does not block, and nothing polls. A script that opens a menu, or starts a
cutscene, or starts a fight, simply **parks**: it writes a number into its own
status word and stops, with its program counter and stack intact. Later
something else — a screen closing, an animation finishing, a fight ending —
writes 1 back and it carries on from the next instruction.

That is why a conversation can interrupt a cutscene, why an area transition can
take several frames of streaming without the game stopping, and why the whole
engine is one loop with no threads in it. It is also why the VM costs almost
nothing on an ordinary street: the scripts run when something happens, not
every frame.

The second idea cost more to learn: **a script belongs to a place.** When it
names a scene object by number, the number means something only inside the
scene of the slot the script came from — and two places are loaded at once.

The third is a warning about evidence. A script that decodes cleanly to its
`end` instruction is not proof the decoder is right: one wrong operand length
can make a script *look* finished halfway through. The VM's own table was
wrong in 22 places, and the last of them was found in October 2026 by reading
a handler, not by any test failing.

## In detail

### The interpreter

The loop is a dozen lines, `sub_4060B0` — one of the few pieces Hex-Rays
decompiled; 149 of the 153 opcode handlers carry no function label and were
read out of the listing instead (`docs/SCRIPT_VM.md`, its opening and
"Execution model"):

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
stops the scan. The context is small (SCRIPT_VM, "The two entry points"):

```
+4   uint8 *code       the script, as passed in
+12  uint8 *pc         each handler advances it itself
+16  int32 *stack
+20  uint16 sp         one past the top
+22  int16  status     the park word — see below
+36  int16 *params     the indirect-operand block (operand bit 14)
+40  uint8  flags      bit 0x10 stops a Script_Run scan
```

The table at `0x004C0140` is 153 entries of `{handler, operandBytes}`, lifted
to `tables/vm_opcodes.json` because a replica cannot read it out of any data
file (`verify.py: vm table sources`, `opcode table fresh`).

There are **two kinds of entry on one context**. `Script_Execute` (0x00406460)
is the real interpreter. `Script_Run` (0x004060B0) — and its sibling
`Script_RunToOpcode75` — *evaluate*: they set a dry-run flag, and every handler
that tests it consumes its operands and returns without acting or announcing —
111 references across the listing, and 22 of the 153 handlers open with the
identical five-instruction test (SCRIPT_VM §62; `readable/src/01_file.c`). It
is how the engine asks a question of a script without running it, and it is
why an operand length can be wrong without anything visibly failing.

Binary operators take the **left operand from `stack[sp-1]`** and the right
from `stack[sp-2]`, so the compiler emits the right-hand side first
(SCRIPT_VM, "Stack convention").

### Operands, and the one parameter a script can see

Most operands are 16-bit fields following the opcode; `0xFFFF` means "none".
With bit `0x4000` set, the rest indexes the block at `ctx+36` instead — and the
fetch reads it **from its second word**, so operand `0x4000` names `args[1]`.
That block has exactly one filler: `Message_RunHandlers` writes `{message id,
sender}` into it. So the only parameter a script can reach is the **sender** —
and all **59** indirect operands in the world scripts are `push.i16 0x4000`
inside a message handler, comparing the sender with an actor or object id
(SCRIPT_VM, "Operand encoding"; `verify.py: engine: vm probe`). **91**
handlers run that shared fetch. The `scx.play` family's object word does not —
it is read raw, an unsigned scene-object id — and reading it through the
indirect rule once turned Anekbah's whole startup crowd, whose ids carry bit
14, into `param[718]` (same section; `engine: city crowd`).

### The operand lengths, and a lesson about believing a table

The table's operand *lengths* were checked against the handlers themselves,
and the exercise is worth recording because it is the shape of mistake this
repository is built to catch (`CLAUDE.md` §1; `docs/PRACTICE.md` §1).
Recovering lengths from handler assembly produced **21 disagreements** with the
table. Applying all 21 made the corpus decode *worse* — 53 failures became 58.
Tested one at a time, **6 were real**. The table had been checked only against
the conversations, which exercise 25 opcodes; the world scripts exercise far
more, and 53 slots desynchronised onto an opcode above 152 when they were first
read (SCRIPT_VM, "The world scripts use far more of the VM").

Then the reverse: opcode 103's assembly reads 6 bytes in a straight line, and
it was recorded as *wrong* — left at the table's 2 — because a corpus test
showed a script breaking at 6. That test had been run while other lengths were
still wrong, and it was those that were breaking. Corrected, op 103 at 6
decodes 5 785 of 5 785 slots with **2 056 fewer instructions**: exactly the
four surplus bytes per site, which at 2 had been decoding as phantom
instructions (SCRIPT_VM, "The failures a decode test cannot see").

`fight.begin`, opcode 62, takes three 2-byte fields where the table said 4
bytes; at 4 the corpus decodes 216 phantom instructions, two per site over its
108 sites, all inert, so nothing observable broke. Read under the corrected
length, **the third field is the AI level**: every site is a `case` on
`VARIABLES[175] 'Niveau Combat'` with three copies of the call, 36 / 36 / 36
over levels 0, 1 and 2, never 3 (SCRIPT_VM §62). The scripts move that
variable by what the last fight cost the player — **the game's fight
difficulty is adaptive**, and the fourth fight-AI profile can never be
selected (`docs/ASSETS.md`; `todo/fight-mode.md` §2; `verify.py: fight &
become`).

**The last one, op 142, was found on 2026-10-11 by reading its handler for the
port** — and `tools/vm_oplen.py` had printed `142: 4 -> 6` all along (SCRIPT_VM,
"The failures a decode test cannot see"; `d845dec`). Its five sites are all in
AREA 59, the shooting gallery. At 4, the third field `01 00` and `02 00`
decoded as harmless zero-operand opcodes; but at one site it is `03 00`, and
**op 3 is `end`**. The decode stopped there with the slot reading clean, and
every check passed. The jumps were the tell: a `jmp_if_false` landed at an
offset only an instruction one byte-pair longer allows. At 6 the slots still
decode 5 785 of 5 785, with **58 428 → 58 449** instructions — the uncut tail,
a sixth site among it — and the number of distinct opcodes the world scripts
use fell **119 → 117**, because `dbg.dump_code` and `nop` occurred nowhere but
as that phantom. The same correction claimed 32 bytes of AREA 59 that the
chunk accounting had left unreferenced (chapter 3). The lesson the document
draws: *an `end` is only evidence of the slot's end if the instruction before
it had the right length.*

So a corpus verdict is only as good as the rest of the table, and a conclusion
drawn from a table that has since changed is worth re-running rather than
re-reading. **22 operand counts** are corrected in all
(`tools/dialog_disasm.py`'s `LEN_FIX`; `docs/PRACTICE.md` §4).

### The status word — how a script waits

`Script_Execute` runs `while (status == 1)`. A handler that writes anything
else parks its caller, and something else must write 1 back. **Nothing polls**:
every resume is an event or a step of the pump (SCRIPT_VM, "The context status
word", *read from the code*).

| status | what it means | resumed by |
|---|---|---|
| 0 | idle; the action queue may be armed | the pump, when it dequeues an action |
| 1 | running | — |
| 3 | in a fight | event case 2 — the **first** context at 3 |
| 4 | waiting for a scene object or a player move | case 3, when the object or the move finishes |
| 5 | a transition caller a *second* transition superseded | **nothing found** |
| 6 | waiting for a screen | case 5, from the answer or either close key |
| 7 | waiting for a camera move | case 4, when the move ends |
| 8 | waiting for `area.preload` | the pump's tail, once the load reports |
| 9 | retry the transition next frame | the action processor's head, unconditionally |
| 10 | waiting on the area transition | the pump's tail |
| 11 | set by the transition's 60-second watchdog | case 3's `else` arm |

Exactly **ten opcodes** write the word — `end`, `area.preload`, the three
waiting `scx.play` variants, `fight.begin`, `ui.open`, `player.move.wait`,
`camera.set.wait` and `camera.set.at_address` — and the transition machine
writes the rest (same section; `verify.py: engine: parking ops`). Details of
the plumbing that a reader reconstructs wrongly otherwise:

* **Queued actions do not overtake a parked script.** The action processor
  refuses to arm anything while the status is non-zero, so an action waits
  behind the script that is parked, and an action is over only when `end` runs.
* **Starting a conversation stops the world on the next frame, not this one.**
  `Script_Execute` returns after `dialog.start` for that one context; what
  stops the others is the pump's first line on the following frame. A port
  that ended the frame there skipped every later context (same section,
  corrected 2026-09-02).
* **A handler can retry itself.** A refused `area.goto` rewinds its pc by 7 —
  its own size — and parks at 9, so it runs again next tick until the
  transition accepts it.
* **`camera.set.wait` parks only when the travel is non-zero; the camera
  call on an address parks always**, so a 0-frame cut parks under one and not
  the other. Every shipped site of the latter travels 20 frames, so the
  corpus cannot show the difference; it is asserted from the handler.
* **A fight's result is dropped.** Case 2 releases the first context at
  status 3 and ignores the result code it is handed; the scripts learn what
  happened by re-reading the player's life (`todo/fight-mode.md` §5).
* **A screen can answer its own question.** `ui.open` parks its caller at 6 —
  but the videophone's open ends in a call to `UI_SendAnswer`, which hands back
  the −1 the screen was seeded with while the device stays up, so the next
  instruction runs at once and the call looks like it never waited
  (`docs/UI.md` §3i).

### Which pool a script plays objects in

`scx.play` and its variants name a scene object by a small number that is
**scene-local** — matched against the object records of the loaded `.SCX` —
and object numbers are reused in every scene file (SCRIPT_VM, "The four
`scx.play` opcodes"). `scx.play.wait` (0x004031E0) resolves the number through
the object container of **the slot the running context belongs to**, not the
scene loaded last (SCRIPT_VM, "…and a script plays objects in ITS OWN slot's
pool", 2026-09-18).

The shipped case that shows it is a lift door after a phone call. SCENE 45,
loaded over the security centre's shaft, takes the lift over with a zone that
does `ui.open 4`, then `area.goto 181`, `area.arrive -1`, and only then
`scx.play.wait obj 0x0012` — the door. By that line the destination's scene is
resident in the other slot; the script is the shaft's, so `0x12` is the shaft's
door. A replica that hands every `scx.play*` to the newest scene opens an
object of the wrong file, and the door stays shut — `moved 0.0 0.0 0.0`, the
reported bug, is what `verify.py: engine: slot pool` reads without the rule.
Every other ride worked **by order** — the car zone had started the door a
frame before the swap — which is why no ride without the call could see it.

Two more rules from the same family, both learned from play reports:

* **A waiting `scx.play` is not a cutscene.** Opcode 58 leaves its caller
  parked while a door slides, exactly as a cutscene beat does; asking "is some
  context parked on some program" treated the flat's doors as cutscenes, put
  the letterbox up and dropped the player for the length of each. **A beat
  poses a body and a door does not** (chapter 7).
* **`player.move 100` is the stop.** The operand is a group of the player's
  `.CTL` bank, and group 100's default entry is the stand, so a walking player
  stops dead with no root motion left to play out. The scripts put it in front
  of 60 of the 312 `player.move` sites, and it is why a cutscene trigger is not
  re-fired by the player's next step (SCRIPT_VM, "63 / 89"; `verify.py:
  engine: player move`).

### What the corpus exercises

**133 of the 153 opcodes are named.** The world scripts exercise **117** of
them, and **115** of those are named — **99.99%** of the 58 449 executed
instructions. The two that remain, 53 and 101, are read and deliberately left
unnamed: 53 fetches and logs a camera and does nothing else, 101 is a bare
`retn` — 5 instructions between them. The 18 other unnamed opcodes are ones no
shipped script executes (SCRIPT_VM, "Naming the opcodes" and "Read but not
named", recounted 2026-11-02). Names are added only with the handler traced,
and the table in the document is generated from the disassembler's own name
map, so the two cannot drift (`tools/vm_doc.py --check`; `verify.py: opcode
names`).

Some of the names came out of the game rather than out of a guess:

* **150 / 151 — `render.grey.on` / `.off`.** They install and remove the
  engine's second render bank, the same walk converting every vertex colour
  to luma. All 14 installs are closed by a restore, and 74 of the 82
  instructions between them are camera and fade opcodes, in eight chunks
  including the Morgue and the Bowie concert: the game has **black-and-white
  cutscenes** (SCRIPT_VM, "150 / 151"; ported, `engine: grey bank`).
* **134 / 135 — `set.mirror.on` / `.off`**, read 2026-10-11. 135 parks the
  scene's live **mirror** — the plane the renderer reflects the scene through
  — and 134 makes it live again. Their sites are all in Kay'l's apartment:
  zone records park it on camera cuts and restore it after (SCRIPT_VM, "134 /
  135 / 137"; `engine: mirror park`).
* **137 — `ui.close_all`**, read the same day: every open screen gets its own
  close callback. Its seven sites include the **hurt handlers** — `Vie` set, a
  red flash, a shake, then this — so a hit closes the sneak (same section).
* **142 — `ui.highscore`**: the shooting gallery's board, inserting the
  player's time into one of four five-row tables in the save header — lower is
  better — and showing the board, which closes itself after ten seconds
  (SCRIPT_VM, "142"; `engine: ui highscore`).
* **93 — `actor.stat.set`**, renamed from `hud.show_var` on 2026-09-02: it
  writes a variable into an actor's stat — the mirror of 86
  `var.set.actor_stat` — and the HUD flash is a side effect on the player arm
  only. That flash was **read and ported on 2026-10-29**: the call comes
  *before* the write and is refused while `ACTOR_STATE` is 3, shoot mode; health
  shows the gauge, any other stat the gauge and the stat card, eased from the
  old value to the new over 2 500 ms and gone at 3 000 ms, drawn only in
  `ACTOR_STATE` 1, so a flash armed in a fight or a screen waits for it. Every
  fall and run-over handler ends with one, which is why a landing shows the
  gauge (SCRIPT_VM, "93"; `engine: hud flash`). Its stat clamp is
  **unsigned**: a script that subtracts damage past zero heals the actor to 200
  (`engine: world ops`).
* **127 — `player.pos.sync`** and **54 — `camera.follow_player`**, read and
  ported 2026-10-11 (SCRIPT_VM, "The tail"; `engine: follow and sync`): 127
  resets the collision walker's position to the node's and invalidates its
  ground cache; 54 hands the camera back to the player with no blend.

Shoot mode's six opcodes carry most of its content, and the handler of one of
them names the subsystem in its own error string — `"error : perso is not in
shoot mode !"` (SCRIPT_VM, "Shoot mode"; `verify.py: shoot mode`).
`shoot.actor.enter` brings a gunman in and `shoot.actor.action` gives him
something to do; **116 of the action sites are action 1, a patrol** along a
route in the level's `MAP2D` file, ahead of action 3's 113 (`verify.py: shoot
patrol`).

On a steady street almost none of this runs: the port measured about 50
instructions in 150 frames, which is why the VM's per-instruction cost only
matters in cutscenes and transitions (`todo/optimization.md`, item C8).

### The accident that makes the original observable

**51 handlers** announce an operand by domain name through the `IAM\*.TAG`
tables via a profile-string lookup that sits *before* the debug window's own
test, so the shipped, unmodified executable narrates what it is doing to anyone
watching that Win32 API — no patch, no shim, no debugger (`docs/PRACTICE.md`
§6, "Running the original").

Which operand a handler announces is not always its first: that mattered five
times, each found by a golden trace disagreeing with a prediction, so the
announce is now read out of every handler's assembly — 49 read, 0
disagreements (SCRIPT_VM, "Which operand a handler announces"; `verify.py: vm
announce fields`).

That is the whole basis of the golden traces in chapter 12, and it is luck
rather than design. It is also why fights and shoot phases have no such oracle:
`fight.begin` announces nothing (`engine/README.md`, the melee entry).

## Where it lives

| | |
|---|---|
| the finding | `docs/SCRIPT_VM.md` — the table, the operands, the status word, the naming, which pool a script plays in |
| the table | `tables/vm_opcodes.json`, `tables/vm_announce.json`; the names and corrected lengths in `tools/dialog_disasm.py` (`NAME`, `LEN_FIX`); `engine/src/script/opcodes.h` generated from the table by `tools/gen_opcodes.py` |
| the port | `engine/src/script/interp.cpp` (the handlers), `area.cpp` (the pump, the contexts, the slot pools) |
| the readers | `tools/dialog_disasm.py`, `tools/script_dump.py`, `tools/vm_oplen.py`, `tools/vm_doc.py`, and `/world` in the web viewer |
| the checks | `vm table sources`, `opcode table fresh`, `opcode names`, `dialogue scripts`, `world scripts`, `vm announce fields`, `engine: vm probe`, `engine: parking ops`, `engine: slot pool`, `engine: player move`, `fight & become`, `shoot patrol`, `engine: mirror park`, `engine: ui highscore`, `engine: hud flash`, `engine: follow and sync` |

## What is not settled

* **Status 5 is read and not resolved.** A transition caller that a second
  `area.goto` supersedes is parked, and nothing in the reading writes it back
  to 1. The shipped scripts may never reach it — it needs two transitions in
  flight — and a claim either way needs a trace the rig cannot take
  (SCRIPT_VM, "The context status word").
* **Two exercised opcodes are read and not named**, 53 and 101 — both
  no-ops by what their handlers lack, and left unnamed for that reason.
  **Eighteen opcodes have no name at all**, identified only by their table
  entry, because no shipped script executes them.
* **Op 142's screen leaves two questions**: the flag it sets on pages 1..3 has
  no reader found, and its refusal string is reached by nothing — every time
  is "homologated", even one that made no row (SCRIPT_VM, "142").
* **Op 134 after a set change would restore a mirror of the old set** in the
  original, since nothing clears the saved pointer; the port resets both with
  the area — a declared difference, not a reading (SCRIPT_VM, "134 / 135 /
  137").
* **Two names in the table are known wrong** and wait on a rename: 146/147
  `ambience.on` / `.off` are the shoot radar's switch, and 132/133
  `fade.to_black` / `.from_black` are the other way round (SCRIPT_VM, "The
  full table").
* **Site counts depend on the corpus, and three enumerations are not
  reconciled**: the 5 785 slots of `IAM\AREA` + `SCENE` + `GLOBAL` that
  `docs/` counts over, the same without `GLOBAL`, and the port's own sweep of
  5 958 slots, which reports higher figures again (SCRIPT_VM, "A note on site
  counts").

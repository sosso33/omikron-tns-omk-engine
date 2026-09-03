# The Omikron dialogue script VM

Start at [`../CLAUDE.md`](../CLAUDE.md) for the working practice these
findings depend on.

The conversation trees in `IAM\DIALOG` are driven by bytecode. Each
`DialogNode` carries up to eight scripts — a condition and an action for each of
its four reply branches — and those scripts read and write the game's global
variable array. That is how the game remembers which lines you have heard and
which replies you picked.

**Hex-Rays decompiled 4 of the 153 opcode handlers.** The other 149 are present
in `Runtime.exe.asm` but carry no function label, so they never made it into
`Runtime.exe.c` (nor into `clean/` or `readable/`). Everything below was read
out of the listing.

Tools:

    python3 tools/vm_table.py         # rebuild the opcode table from the asm
    python3 tools/vm_extract.py       # pull each handler's code out of the asm
    python3 tools/dialog_disasm.py    # decode every script in the game
    python3 tools/dialog_disasm.py 268

## Validation

The disassembler decodes **all 612 scripts in `gamedata/IAM/DIALOG` cleanly to `end`**.
Since a wrong operand length desynchronises the stream and lands on an invalid
opcode within a few instructions, decoding the whole corpus without a single
failure is a strong check on the opcode table and on every operand length.

## Execution model

The interpreter is `sub_4060B0` (0x004060B0) — one of the few pieces that *was*
decompiled:

```c
pc = ctx->code;                     /* ctx+4  */
op = *pc++;
while (op != 3 && !(ctx->flags & 0x10)) {
    handlers[op](ctx);              /* table at 0x004C0140 */
    pc = ctx->pc;                   /* handlers advance it themselves */
    op = *pc++;
}
```

* An **opcode is one byte**. Operands follow it, consumed by the handler.
* **Opcode 3 ends the script.**
* Bit 0x10 of `ctx+40` aborts the run.

Context layout:

```
+4   uint8 *code       the script, as passed in
+12  uint8 *pc
+16  int32 *stack
+20  uint16 sp         points one past the top
+36  int16 *fixups     operand indirection table
+40  uint8  flags      bit 0x10 = abort
```

### Which operand a handler announces

**verified** — `tools/vm_announce.py`, `verify.py: vm announce fields`.

51 handlers call the tag logger, and the domain they name is not always their
*first* operand. That mattered five times, and every one was found by accident
— a golden trace disagreeing with a prediction, days apart:

| opcode | announces | not |
|---|---|---|
| 71 `scene.load` | field 1, the scene | field 0, the area |
| 52 `inventory.remove_all` | field 1 | field 0, the list selector |
| 93 `actor.stat.set` | field 2 | field 0, the actor |
| 50 `inventory.add` | field 1 | field 0, the list selector |
| 51 `inventory.remove` | field 1 | field 0, the list selector |

That is a *class* of error, and finding them one capture at a time is not a
method. The audit reads the announce out of every handler instead. The shape is
always the same, because `Dbg_LogTagged(value, section)` is cdecl — the section
string is pushed first and the value second:

```asm
push offset aObjects_2   ; "OBJECTS"
push ebx                 ; <- and WHICH operand ebx is, is the point
call sub_40EC70
```

Operands reach a register two ways: the shared fetch `sub_401AA0` (`call` then
`mov <reg>, eax`), or inlined — two bytes assembled and sign-extended with
`movsx <reg>, cx`, optionally re-read through the `0x4000` indirect. Counting
both in order gives every register its field index, and the pushed one is what
the handler logs.

**49 handlers read, 0 disagreements.** The audit rediscovers all five fixes
above independently, and the two maps that were already right (ops 49, 67) —
which is the cross-check that it is reading rather than agreeing. A sixth
instance now fails the check instead of waiting for a capture to contradict it.

### Operand encoding

16-bit operands are assembled from two byte loads. `0xFFFF` means "none". If bit
`0x4000` is set the bit is cleared and what remains indexes the block at
`ctx+36` rather than being the value itself.

**The index is offset by one word, and that decides what the indirection is
for.** Every copy of the fetch — the out-of-line `sub_401AA0` (0x00401AA0) and
the inlined ones — ends

```asm
cmp     ax, 0FFFFh
jz      short skip
test    ah, 40h                 ; or `test e??, 4000h` in the 32-bit copies
jz      short skip
mov     edx, [esi+24h]          ; ctx+36, the parameter block
and     ah, 0BFh
movsx   eax, word ptr [edx+eax*2+2]     ; <- the +2
```

so operand `0x4000` (index 0) names the block's **second** int16, not its
first. That block has exactly one filler: `Message_RunHandlers` (0x00409420)
allocates 8 bytes, stores them at `ctx+36` and writes `args[0] = the message
id`, `args[1] = the sender`. **`push.i16 0x4000` therefore pushes the sender.**
`Script_NewContext` (0x00406290) zeroes `ctx+36` for every other context, so an
indirect operand anywhere else would dereference null — which is exactly what
the corpus shows.

**Not every field runs the fetch - the `scx.play` family's OBJECT word is
read RAW** (corrected 2026-09-03). Ops 46, 57, 58 and 90 read their first
word, and 59/60 their second, as `and ecx, 0FFFFh; mov ebp, ecx`: no 0xFFFF
test, no `test ch, 40h`, an unsigned 16-bit scene-object id (the object's
`handle >> 16`). Only the actor word and the trailing word go through the
shared fetch. Thirty shipped object words carry bit 14 - 26 of them Anekbah's
startup extras, whose ids are `0xC2xx` - and reading them through the indirect
rule turned the whole city's crowd into `param[718]`; `tools/dialog_disasm.py`
`RAW_WORD` names the fields, `verify.py: engine: city crowd` pins it, and
[`docs/STREET_LIFE.md`](STREET_LIFE.md) §1 has the story. The count below is
over the fields that DO run the fetch.

**All 59 indirect operands in the world scripts are `push.i16 0x4000`, and all
59 are in message handlers**: AREA 2 (message 3), 61 (3), 141 (3), 144 (2),
SCENE 56 (3), and two `IAM\GLOBAL` records (4 and 20) — every one of them a
subscription in its chunk's `+68`/`+36` table. Each is the middle of

```
push.i16   173        ; a CHARACTERS id, under message 3
push.i16   0x4000
cmp.eq
jmp_if_false ...
```

with the constant an actor id under messages 2/3 (173, 187 …) and an object id
under 4/20 (3, 100, 385, 775 …). A handler subscribed to message 3 has nothing
to learn from the number 3, which is the data's own refutation of reading
`0x4000` as the message id. `verify.py: engine: vm probe` (T1's port-side
transcription made the same correction independently).

**Which opcodes run the fetch.** Scanning every handler block in
`clean/_vmhandlers.json` for that sequence or a `call sub_401AA0` gives **89**;
op **6** (whose block in `clean/` is empty — read at 0x401CE0 with
`asmfn.py`) and op **120** (whose block is the function *after* its handler;
the real one is at 0x405480) make **91**:

> 4, 5, 6, 8, 10, 12–24, 42–53, 55–67, 69–73, 75–99, 103, 113–115, 118–120,
> 122, 123, 126, 128, 131, 136, 138, 139, 142–144

The fetch is **not** confined to variable indices: `set.var.i16` (15) fetches
its value as well as its index, `set.var.var` (17) and `scene.load` (71) both
of their fields, `area.goto` (47) all three. Only 14's int8 and 16's int32
value are read raw. Two blocks cannot answer for themselves and are excluded
from the count above: op 7's is empty in `clean/` too, and op 2 has no handler
at all (its table entry is 0). `interp.h`'s old note that 4, 6, 10, 18 and 42
were the only fetchers was wrong by 86 opcodes.

An operand's *negative* values are worth noting for the same reason: only
`0xFFFF` is tested for, so any other negative int16 has bit 14 set and would be
taken as an index. No handler is ever handed one in the shipped data.

### Stack convention

Binary operators take the **left operand from `stack[sp-1]`** and the right from
`stack[sp-2]`, so the compiler emits the right-hand side first. That is what
makes `sub` compute `stack[sp-1] - stack[sp-2]` rather than the reverse.

## A real script

Chunk 268, node 0, branch 0 — decoded and with ids resolved through the `.TAG`
tables:

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
    op_52        57081858
    set.var2     408  ; VARIABLES[408] = 'Traductech'
    end
```

"Offer this reply only if the player has the correspondence table and the fourth
astronomy book. If they take it, close the scholar dialogue, open the follow-up
scene, and record that they now have the Traductech."

## Which opcodes the game actually uses

Of 153 defined, 25 appear in the shipped scripts:

| opcode | count |
|---|---|
| `end` 3 | 612 |
| `push.i8` 7 | 355 |
| `push.var` 10 | 323 |
| `cmp.eq` 25 | 279 |
| `set.var2` 13 | 242 |
| `op_92` (media) | 84 |
| `and` 37 | 74 |
| `cmp.ge` 29 | 31 |
| `var.sub` 20 | 31 |
| `op_93` | 30 |
| `zone.disable` 65 | 25 |
| `op_52` | 21 |
| `zone.enable` 64 | 20 |
| `cmp.ne` 30 | 11 |
| `op_50` | 10 |
| `op_76` | 9 |
| `push.i16` 8 | 9 |
| `cmp.lt` 26 | 7 |
| `or` 38 | 4 |
| `op_87` | 4 |
| `op_68` | 4 |
| `set.var` 12 | 3 |
| `jmp_if_false` 6 | 3 |
| `op_71` | 2 |
| `op_138` | 1 |

Conditions are overwhelmingly "variable equals constant", combined with `and`.

## Core instruction set

Decoded from the handler assembly.

| op | mnemonic | effect |
|---|---|---|
| 0 | `dbg.dump_ctx` | debug `printf` |
| 1 | `dbg.dump_code` | debug: hex-dump 64 bytes of bytecode |
| 2 | `nop` | `nullsub_15` |
| 3 | `end` | stop; the dispatch loop tests for this before dispatching |
| 4 | `jmp` | `pc += (int16)operand` |
| 5 | `jmp_if_true` | pop; jump if non-zero |
| 6 | `jmp_if_false` | pop; jump if zero |
| 7 | `push.i8` | push a sign-extended byte |
| 8 | `push.i16` | push a 16-bit immediate |
| 9 | `push.i32` | push a 32-bit immediate |
| 10 | `push.var` | push `Var_Get(n)` |
| 11 | `drop` | `sp--` |
| 12–18 | `set.var*` | write a variable: from the stack, from an immediate of each width, or from another variable |
| 19–24 | `var.add/sub/mul/div/and/or` | `Var_Set(n, Var_Get(n) OP x)` — compound assignment |
| 25–30 | `cmp.eq/lt/gt/le/ge/ne` | `setz/setl/setnle/setle/setnl/setnz` on the two top slots |
| 31–34 | `add` `sub` `mul` `div` | `idiv` for div, so signed |
| 35–36 | `bitand` `bitor` | |
| 37–38 | `and` `or` | logical, result normalised to 0/1 |
| 39–41 | `neg` `not` `bitnot` | unary, `sp` unchanged |
| 42 | `case` | reads a 16-bit target and a 1-byte case value; if the stack top differs, jump. Does not pop — switch dispatch. Its table entry claims 0 operand bytes but it reads 3. |
| 61 | `dialog.start` | calls `Dialog_Load` (0x00401800) |
| 64 / 65 | `zone.enable` / `zone.disable` | identical handlers but for pushing 1 vs 0 |

### Division TRUNCATES

**read from the handlers, 2026-09-02.** `div` (34, `0x402750`) and `var.div`
(22, `0x402390`) are both `cdq; idiv`, which is C's division and not Python's:
**−7/2 is −3**, toward zero. A zero divisor is a `#DE` fault — the engine
crashes, it does not produce a value.

`tools/sim` used floor division, and `engine/` inherited it until 2026-09-02;
the port now truncates and keeps a labelled 0 for the zero divisor, which is
the replica's choice where the engine has no behaviour to copy. **The corpus
could not tell the two apart**: 74 `var.div` sites and 0 `div` sites, and none
of the 74 divides a negative — so this is a rule found in the assembly that no
sweep over the shipped scripts could have caught, and the check that pins it
runs hand-built bytecode instead (`verify.py: engine: vm probe`).

## Variable access

```c
Var_Get(i)     /* 0x0040E530 */  return gameDB->vars[i];
Var_Set(i, v)  /* 0x0040E510 */  gameDB->vars[i] = v;
```

`gameDB` is `dword_4E6D94`; `+8` is the variable array, `+24` a separate bitset.
`IAM\VARIABLES.TAG` names every index, so the game's persistent state is
readable: `'OE Table Corresp'`, `'1 Section 1 Finie'`, `'Inventaire'`, `'Vie'`.

## Opcode domains

Every handler logs through `sub_40EC70(id, section)`, and `section` names the
`.TAG` table its operand indexes. That gives each opcode's domain for free —
`ZONES`, `OBJECTS`, `CHARACTERS`, `CAMERAS`, `SCENES`, `AREAS`, `ADDRESSES`,
`DIALOGS`, `VARIABLES`. `tools/dialog_disasm.py` uses this to annotate operands
automatically.

## The full table

`table` is the count the VM's own table states and `actual` the corrected one
where they differ (see above). `uses` counts occurrences across the 5785 world
scripts — a blank means the shipped scripts never use that opcode.

**Two handler blocks are not bounded correctly** and any
per-handler analysis has to exclude them: opcode 77 (the splitter ran past its
end) and **opcode 152**, which is the last entry in the table and so had no
successor to stop at — its block swallows 6072 lines of unrelated code. That
matters in practice: searching the handlers for a call to `Dialog_Load` returns
61 *and* 152, and the 152 hit is spurious. Its real handler is seven
instructions that set a flag and return. Everything else is bounded correctly.

## The two entry points, and the script context

**from code.** The VM has two entries that share one context:

| | | |
|---|---|---|
| `Script_Run` | 0x004060B0 | **evaluates**: sets `g_ScriptDryRun`, so every handler consumes its operands and returns without acting |
| `Script_Execute` | 0x00406460 | **executes**: clears the flag and is the real interpreter |

Every handler in the table opens with the same `g_ScriptDryRun` test. That is
why an operand length can be wrong without anything visibly failing — in the
evaluate pass the handler does nothing *but* consume operands.

The context fields the handlers use:

```
+4   uint8_t *script
+12  uint8_t *pc        each handler advances it itself
+22  int16_t  status    1 = running
+36  int16_t *params    the indirect-operand table (operand bit 14)
+40  uint8_t  flags     bit 0x10 aborts a Script_Run scan
```

**`status` is how an opcode blocks.** `Script_Execute` loops only while it is 1,
so a handler suspends the script by writing anything else: `camera.set.wait`
writes 7, opcode 58 writes 4, opcode 47 writes 9. That is what separates
`camera.set` from `camera.set.wait` — the same mode-12 request, one of which
stops the script until the move finishes.

`Script_Execute` also returns outright after opcode 61 (`if (v4 == 61)
return;`) — but only for **that one context**: it leaves the status at 1 and
`Script_Pump`'s loop carries on to the next slot. What stops the rest of the
world is the pump's own first line, `if (g_DialogState != 1) return 1;`, on the
*following* frame. (Corrected 2026-09-02: this said "starting a conversation
ends the tick", and a port that ended the frame there skipped every later
context, `finishAreaTransition` and the camera tick with it.)

**A handler can also retry itself.** `area.goto` rewinds the pc by 7 — exactly
its own size, one opcode plus six operand bytes — when `Area_Transition` refuses
the request, so the instruction runs again on the next tick until the
transition is accepted. That is the same "rewind by 7" that had to be excluded
from `tools/vm_oplen.py`: it moves the instruction pointer without consuming
operands.

## The context status word, and what resumes a parked script

**read from the code**, 2026-09-02: `Script_Execute` (0x00406460),
`Script_Pump` (0x00407DC0), `Script_ProcessActions` (0x00408220),
`Script_QueueAction` (0x004063D0), `Area_Transition` (0x00408530) and
`Game_HandleEvent` (0x004067D0), plus a scan of every handler block for a write
to `ctx+22`.

`Script_Execute` runs `while (status == 1)`, so a handler that writes anything
else **parks** its caller with the pc and the stack intact, and something else
has to write 1 back. Nothing polls: every resume is an event or a pump step.

| status | what it means | written by | resumed by |
|---|---|---|---|
| 0 | not running — the context is idle and its action queue may be armed | op 3 `end` (0x401B90: `xor edi,edi; mov [esi+16h], di`); `Script_NewContext` starts here | `Script_ProcessActions`, when it dequeues action 1/2/3 |
| 1 | running | `Script_ProcessActions`, and every resume below | — |
| 3 | in a fight | op 62 `fight.begin` (0x4035D0) | `Game_HandleEvent` case **2**: walks the table and returns the **first** context at 3 to 1 |
| 4 | waiting for a scene object or a player move | ops 46 (0x402C30), 58 (0x4031E0), 60 (0x403430), 89 (0x4043F0) | case **3**, raised when the object or the move finishes |
| 5 | a transition caller that a *second* transition superseded | `Area_Transition` mode 0, state 4 (`u16(dword_4E61E8[a1[1]], 22) = 5`) | **nothing found** — see below |
| 6 | waiting for a screen | op 70 `ui.open` (0x403860) | case **5**, from `UI_SendAnswer` or either ESC/TAB close |
| 7 | waiting for a camera move | op 96 (0x404AF0) — **only when the travel is non-zero** (`test ebp,ebp; jz`) — and op 126 (0x405630), unconditionally | case **4**, when the move ends |
| 8 | waiting for `area.preload`'s load | op 45 (0x402AB0), which also sets `dword_4C0130` to the context's index | `Script_Pump`'s tail, once `Area_TickLoad` reports |
| 9 | retry the transition next frame | `Script_Execute` (a refused `area.goto`) and `Area_Transition` mode 0 states 1/8/default | `Script_ProcessActions`' head: `if (status == 9) status = 1;` |
| 10 | waiting on the area transition | `Area_Transition` modes 0, 2, 3 and 4 at most states | `Script_Pump`'s tail, which hands the context back to `Area_Transition` mode 3 |
| 11 | the transition's last step is done | `Script_ProcessActions`' 60-second watchdog | case **3**, whose `else` arm links the destination decor and writes 1 |

Two details of the plumbing that a reader will otherwise reconstruct wrongly:

* **`Script_ProcessActions` refuses to arm anything while the status is
  non-zero** (`if (status) return;`), so queued actions wait *behind* a parked
  script instead of overtaking it. An action is over only when `end` runs.
* **The pump's tail is the only place statuses 8 and 10 are looked at**:

  ```c
  if (!Area_TickLoad(1 - dword_69BC60) || dword_4C0130 == -1) return 1;
  ctx = dword_4E61E8[dword_4C0130];
  if (u16(ctx, 22) == 10) Area_Transition(&dword_6A0600, dword_69BC60, ctx, 3, -1, -1, -1);
  else                    u16(ctx, 22) = 1;      /* the status-8 preload */
  dword_4C0130 = -1;
  ```

**Status 5 is read and not resolved.** `Area_Transition` writes it into the
*previous* caller when a second `area.goto` arrives at state 4, and no event,
pump step or handler in this reading writes that context back to 1 — it would
be parked for good. It is recorded rather than explained because the shipped
scripts may never reach it (it needs two transitions in flight), and a claim
either way needs a trace this repo cannot take.

### The ten opcodes that write the status word

A scan of all 153 handler blocks for `mov [<reg>+16h], …` finds exactly these,
and no others:

| op | name | writes | note |
|---|---|---|---|
| 3 | `end` | **0** | also `byte_4C012C = 0xFF`, and on `ctx+40 & 8` frees the SCENE block `scene.unload` marked |
| 45 | `area.preload` | **8** | and `dword_4C0130` = the context index — but not at all when a resident slot already holds the area (see the transition section) |
| 46 | `scx.play.player.wait` | **4** | after `ScriptObject_Start`, and `Camera_Request(13)` when the object carries an editing |
| 58 | `scx.play.wait` | **4** | the same shape; also ORs 4 into `ctx+40` |
| 60 | `scx.play.actor.wait` | **4** | the same shape |
| 62 | `fight.begin` | **3** | and `Camera_Request(14)`, the fight camera |
| 70 | `ui.open` | **6** | and `dword_4E6B28` = the variable, `dword_4C0B64` = the parameter; screen 29 also stores the context in `dword_4E6C7C` |
| 89 | `player.move.wait` | **4** | after `Player_GoToMove(addr, ctx+30)` |
| 96 | `camera.set.wait` | **7** | **only if the travel is non-zero**; `dword_930824` = the context index |
| 126 | `camera.set.at_address` | **7** | unconditional — a 0-frame cut parks too |

**Three of the ten are now ported** (2026-09-02). `player.move.wait` (89),
`fight.begin` (62) and `camera.set.at_address` (126) return their own
`RunStatus` from `engine/src/script/interp.cpp` — `MoveWait`, `FightWait`
and the existing `CameraWait` — each carrying the operands the Session needs
to start what it then waits for, and each leaving the pc **after** the
instruction so the resume continues rather than re-running the park.

**89 and 63 are one handler twice.** 0x004043F0 and 0x00403730 are
instruction for instruction the same `Player_GoToMove`, except that 89 passes
`byte [esi+1Eh]` — the context's own table index — where 63 passes −1, and
that 89 ends `mov word ptr [esi+16h], 4`. That index is the resume id:
`Game_HandleEvent` case 3 takes a context INDEX (`dword_4E61E8[a2]`, no
search) and returns it to 1 only if it is still at 4, so the id travels with
the move and comes back with it. 548 sites against 312.

**62's park is not the interesting half — its camera is.** After
`mov word ptr [esi+16h], 3` the handler requests camera mode **14** with
`dword_930818 = max(field 1, 0)`, the clamp being a `test eax,eax; jge`
around `mov dword ptr [esp+18h], 0`. Field 1 is **0 at all 108 shipped
sites**, so the clamp is the handler's word and not the data's — and −1 is
the only negative literal that can reach it at all, because the shared fetch
reads every other negative 16-bit pattern as an INDIRECT index. Release is
case 2, which walks the table for the **first** context at 3 and then reloads
the player's `.CTL` bank list whether or not it found one.

**126 differs from 96 in exactly two places, and both matter.** The subject
is `Address_Find(field 1)` written into *both* `dword_930808` and
`dword_93080C`, where 96 calls `Actor_Player()` twice — so a
subject-relative camera frames an address rather than the player. And the
status-7 write is in a straight line, where 96 reaches its own only through
`test ebp,ebp; jz loc_404CB1`: **a 0-frame travel cuts under 96 and parks
under 126.** The shipped corpus cannot show that — all 84 sites travel
exactly 20 frames — so it is asserted from the handler, in
`verify.py: engine: parking ops`. Both share `Camera_FindWorld`'s guard: a
camera that does not resolve skips the whole handler, request and park alike.

**A note on op 62's operand length.** The table gives it 4 and the handler
makes three 2-byte fetches. Decoding the corpus at 4 produces **216 phantom
instructions** — 144 `dbg.dump_ctx`, 36 `dbg.dump_code` and 36 `nop`, two per
site over 108 sites — which vanish at 6, with 108 sites and 0 failures under
either. They are inert zero-operand opcodes, so the stream resynchronises and
nothing observable differs; the third operand, `Fight_Begin`'s own second
argument, is simply unreachable while the table says 4. This is the op 103
pattern (CLAUDE.md §1) and the table has not been corrected yet, because that
is `tables/vm_opcodes.json` and `dialog_disasm.LEN_FIX` in one step with the
`engine: execute` differential.

Op **152**'s block matches the same pattern and is a false positive: its block
is unbounded (CLAUDE.md §1's trap) and swallows the interpreter that follows
it; the real handler is seven instructions and writes only `g_RestartRequest`.

### What an unfinished action means for the action button

`Script_QueueAction(ctx, 2)` refuses a second activate while one is queued or
current — the FIFO scan plus `if (a2 == u32(a1,32) && a2 == 2) return 0;` — and
`ctx+32`, the *current* action, is cleared in `Script_Execute`'s `LABEL_8` arm
only when an activate has finished and the status is back to 0. So the rule is
**"no second activate while the first has not finished"**, not "one activate
ever": a parked activate (a camera move, a screen, a conversation) holds the
refusal open for as long as it parks, and one that reaches `end` releases it in
the same `Script_Execute` call. Leaning on the action button inside a trigger
therefore runs its activate script once and waits for it — and runs it again on
the next press once it has ended, unless the zone carries the one-shot bit.

**The one-shot bit LATCHES the slot; it does not free the zone early.** Bit 15
of the zone id (37 of the 4558 shipped zones carry it; `Zone_StateBit` masks it
away with `& 0x7FFF` for the save bitmap) is tested by `Script_Pump` as
`test byte ptr [slot+0Bh], 80h` immediately after it queues an activate, and
the prompt slot goes to state **5**. That reads like "the next pump frees it,
whether or not the player is still inside" — and it is not what happens:
`Game_HandleEvent` case 7 maps state 5 back to **4**, the pump's case 4 maps 4
back to **5**, case 7 is raised by `Actor_ScanZones` (0x00467770) on **every**
frame the player is inside the quad and the arc rather than on an edge, and
`Script_Pump(1)` runs *before* `Actors_TickAll` in `Game_Tick`
(`readable/src/05_sys.c:2107` against `:2178`). So the two ping-pong for as
long as the player stands there and the free happens on leaving, through the
ordinary leave-then-free path. What the bit buys is exactly the gap the `ctx+32`
clear opens: an ordinary zone whose activate script has ended can be activated
again by the next press, and a one-shot one cannot. Ported in
`engine/src/script/world.cpp` (`todo/iam-script-engine.md` 5, whose own text
needs this amendment).

**And a correction to the transcription, not to the engine.** `Script_Pump`'s
state 2 — the frame the player presses the action button — runs the context's
current script inline as a **dry run** first (`Script_Run`, or
`Script_RunToOpcode75` while the player holds an object) and then queues action
2. `readable/src/01_file.c`'s CLEAN text had it queueing action 2 from the
held-object branches alone, which would mean **no zone could ever launch a
conversation**. The assembly (`Runtime.exe.asm` around `loc_407F26`
… `loc_407FCC`) sets `edi = 1` on *both* outcomes of the empty-handed
`Script_Run`: the `jz loc_407F93` skips only the `dword_4E66B8 = 0` store, and
the other path falls into the same `loc_407F93`. So the dry run decides only `dword_4E66B8` - and that flag, it turns out,
decides nothing either: its one reader is inside `!dword_4E61E0`, which the
same arm has just incremented (see "The unconsumed press"). The activate is
queued either way. Corrected
2026-09-02 in `readable/` and in `docs/`; the port already modelled the
engine's rule.

## Naming the opcodes (phase 1)

**129 of 153** opcodes now carry a mnemonic, up from 46 when this phase
started (127 until 2026-09-02, when `case.i16` and `case.i32` were added for
43 and 44 — neither is used by any shipped script). The world scripts exercise
**119**, of which **114 are named** — **99.97% of all executed instructions**.
The five that remain are read but deliberately not named (see the table below);
together they are 17 instructions, 0.029% of the corpus. The 24 opcodes with no
mnemonic at all are the ones the shipped scripts never execute.
Names are added only with the handler traced — the table below is generated
from the same `NAME` and `LEN_FIX` the disassembler uses
(`python3 tools/vm_doc.py`), so the doc cannot drift from the decoder.

| op | name | evidence |
|---|---|---|
| 73 | `actor.goto_address` | operand logged as `ADDRESSES`, resolved by `Address_Find`, result handed to `sub_41BF50` with the actor |
| 92 | `media.play` | handler builds `%s.ADP`, `IMAGES\%s`, `{C}%s` on the `OBJECTS` index it logs |
| 95 | `camera.set` | field 0 traced into `edi`, which is what the handler logs as `CAMERAS`; issues `Camera_Request` mode 12 |
| 78 | `character.show` | resolves its `CHARACTERS` operand via `Scene_FindObjectRecord`, calls `Actor_Attach`, sets the record's state bit to **1**; places the actor at the record's own position when field 1 is non-zero |
| 79 | `character.hide` | same resolution, calls `Actor_Detach`, clears the state bit to **0** |

| 50 | `inventory.add` | reads the record from `IAM\OBJECT` and calls `Inventory_Insert`; skips the add when lists 2 or 3 already hold the id |
| 93 | `actor.stat.set` | field 2 is the `VARIABLES` index (traced through `esi`) and `Var_Get` reads it; **both** arms then reach `Actor_SetProperty`, so the opcode writes that variable into an actor's stat — see the section below. Named `hud.show_var` until 2026-09-02, after the `Hud_ShowValue` call on the player arm. Every variable it appears with is a stat — `Vie` ×262, `Carac Attack` ×133, `Carac Dodge` ×131, `Anneaux`, `Mana`, `Argent` |
| 138 / 139 | `character.look_at_player` / `character.look_away` | both call `Actor_SetLookAt` with 1 and 0, setting the actor's `+400` look-at field to the current player or `-1`. The renderer reads it: set, it takes the vector between the two actors' transforms and turns the head; `-1`, it zeroes the angles |
| 48 | `area.arrive` | `Area_Transition` mode 1, which advances the state machine past the pending state; the last instruction before `end` in a transition script, after `area.goto` and `actor.goto_address` |
| 47 | `area.goto` | hands its `AREAS` operand to `Area_Transition`, which loads the area into the **other** of the two scene slots and sets the script status to 10 |
| 96 | `camera.set.wait` | the same mode-12 request as 95, but writes the script status word and a resume id, suspending the script until the move ends |
| 51 | `inventory.remove` | finds the id and `rep movsd`s the array down over it, writing `0xFFFF` into the vacated slot and doing the same to the parallel array |
| 103 | `music.play` | `Music_PlayTrack` builds `TRACKS\%d.ADP` from field 0 and streams it, field 1 being the loop flag; the handler skips it when field 0 already equals `g_MusicTrack`, which it then sets. **518 of 521 sites name a file that exists**; the other 3 are track 0, which the callee refuses to play |
| 118 / 119 | `fade.to_color` / `fade.from_color` | `Screen_FadeToColor` / `Screen_FadeFromColor`, which are `Screen_StartColorFade` modes 1 and 2 — one full-screen quad, alpha ramping 0→255 and holding, or 255→0 and switching itself off. Fields 0–1 are a 24-bit colour, 2 the duration, 3 a delay |
| 131 | `music.volume` | `Music_SetVolumeRamp(target, frames)`; the mixer walks the current value towards the target and hands it to `Music_SetVolume`, which clamps to 0..100 and converts to DirectSound hundredths of a dB. All 52 sites are inside 0..100 |
| 71 / 72 | `scene.load` / `scene.unload` | a scene is loaded *over* a resident area, and `gameDB+12` is an int16 per area saying which. 71 takes `AREAS` in field 0 and `SCENES` in field 1, tears down whatever that area holds and calls `Scene_Load` + `Actors_SpawnFromTables`; 72 takes the area alone and writes `-1` |
| 76 / 77 | `object.show` / `object.hide` | re-parent a prop's node under the **scene root** and set its state bit 1, or back under its own file root and clear it. `Scene_LoadProps` replays the same call for every record whose bit is set, which is what makes it persistent. The character pair 78/79 sits immediately after them in the table |
| 66 / 68 | `object.hold` / `object.release` | 66 hands the prop's instance to `Actor_HoldObject`, which parents it to a bone of the player and stores it at the actor's `+164`; 68 takes **no operand** because it reads that field back, finds the prop record whose runtime slot matches, releases it, hides it and clears the field |
| 75 | `var.set.used_object` | `Var_Set(field0, OBJECTS id of the prop the player is holding)`, or `-1`. It writes `VARIABLES[13] 'ObjetUtilisé'` at **235 of 235** sites |
| 91 | `var.set.player_id` | `Var_Set(field0, word_69BC80[player])` — the `CHARACTERS` id of the body the player currently occupies. All **63** sites write a variable named `'Joueur'` or `'Joueur <place>'` |
| 80 / 81 | `shoot.begin` / `shoot.end` | `Shoot_Enter` allocates the 100 × 192 combat records, sets `g_ShootMode` and puts the player in actor state 3; its operand is the weapon, resolved by `Weapon_SlotForObject`. `Shoot_Leave` clears the flag and, when the operand is non-zero, forgets the weapon slot |
| 82 / 84 | `shoot.actor.enter` / `shoot.actor.action` | 82 puts a character into state 3 and installs its behaviour function, chosen by the character's own type field; 84 issues an action, and **refuses unless 82 has run** — `"error : perso is not in shoot mode !"` |
| 116 / 117 | `shoot.player.suspend` / `shoot.player.resume` | an exact opposite pair on two globals: `g_PlayerBehaviourOff` 1 / 0 and input profile 0 / 2. The per-frame dispatch calls the player's behaviour only while the flag is 0 |
| 67 / 69 | `object.hold.actor` / `object.release.actor` | 66 and 68 for a named character rather than the player, the same way `scx.play.actor` mirrors `scx.play`. 67 is what **arms the enemies**: 157 of the 159 objects it hands out are called `Gun …`. Between them they settle the character record's `+270` — 67 writes the held object's id there, 68 and 69 write `-1` |
| 70 | `ui.open` | opens one of 37 2D interface screens and **suspends the script** (status 6). `UI_LoadScreen` fetches its artwork with `sprintf("I2d\bitmaps\%s")`, and `gamedata/I2D/bitmaps` holds exactly the 11 files the binary names |
| 86 | `var.set.actor_stat` | `Var_Set(field2, Actor_GetProperty(field1, field0))` — field 0 the actor (`-1` = the player at 458/459), field 1 the property, field 2 the variable. The `.TAG` names identify it: **every one of the 18 variables it writes is written by exactly one property**, and each name matches the offset that property reads |

78 and 79 are an opposite-polarity pair on one operand, which is what makes
them safe to name: the only differences are attach/detach and the bit value.
Their `-1` case acts on the current actor through `Actor_SetHidden` instead.

**Field 0 is not always the index.** Ops 49, 50 and 51 take a **list selector**
in field 0 (values 0..3) and the `OBJECTS` id in field 1. There are four such
object lists; `word_69BD60` holds each one's capacity, `word_69BD62` its count,
`Src` the id array and `dword_69BD68` the records. Annotating field 0 as an
object invents one, so `tools/script_dump.py` carries a `DOMAIN_FIELD` map of
the fields actually traced and marks every other annotation with `?`.


**The `.TAG` domain test does not identify which *field* holds the index.**
Small integers are valid in nearly every tag table, so a field of values 1..300
scores 100% against `ZONES`, `OBJECTS` and `ADDRESSES` alike. It confirms a
domain the handler already told you; it cannot pick a field. Only tracing the
register the handler passes to the logger does that.

### Read but not named

The opcode equivalent of `@status READ`: what was established, and why it was
not enough to name. Recorded so the next pass starts here rather than at the
handler again.

| op | uses | what is established | why not named |
|---|---|---|---|
| 137 | 7 | jumps to a function the disassembler mislabelled with a mangled C++ symbol (`?classic@locale@…`) | the target has not been located |
| 134 / 135 | 2 / 3 | save / restore of object-instance slot 48's mesh pointer via `sub_41E1D0` | nothing establishes what slot 48 holds |
| 53 | 3 | fetches a `CAMERAS` operand, logs it, and does nothing else | a no-op — the acting half was cut |
| 101 | 2 | the handler is a bare `retn` | a no-op |

### 150 / 151 — `render.grey.on` / `render.grey.off`

**Named 2026-09-01**, and they left the table above the way the others did:
the handler's callee had a name once the table it indexes was found.

`sub_42FA00(bank)` swaps **six two-entry arrays at `0x004C4910`**, stride 8 —
five function pointers the renderer then calls indirectly, plus an activate
hook it calls once. `Game_Init` installs bank 0; opcode **150** is
`sub_42FA00(1)` and **151** is `sub_42FA00(0)`.

The pointer that matters is `dword_90E09C`, which `Render_Frame` calls once a
frame after the buckets are filled and `D3DRENDERSTATE_SHADEMODE` is set. Its
two implementations are the **same 0x4000-bucket walk of near-identical
length** — `Render_FlushBuckets` **659** lines against `sub_42FF80`'s **660** —
and they differ in one thing: bank 1 converts every vertex colour to luma grey,
`(299 R + 587 G + 114 B) / 1000`, **17 occurrences against 0**.

**The corpus says what it is for, and says it loudly.** All **14** sites of
opcode 150 are closed by a later 151, none left open, and **74 of the 82
instructions between them — 90% — are `camera.set`, `camera.set.wait`,
`fade.to_color` and `fade.from_color`**. Spans run 1 to 14 instructions. The
bank brackets a **cutscene**, in eight chunks:

| | |
|---|---|
| AREA 145 Mahaleel | SCENE 9 `1-13 Morgue` |
| AREA 156 Anekbah Grotte Gandhar Light | SCENE 42 `1-12 Anissa Aka's Bar` |
| AREA 158 Jaunpur Zone 24 | SCENE 57 `1-02 Appart Kayl Rencontre` |
| AREA 177 Lahoreh Konshu | SCENE 60 `1-20 Concert Bowie Bar 02` |

So the game has **black-and-white cutscenes**, and they are ordinary scripted
camera sequences with the render bank flipped around them. The Anekbah one
fades to **white** inside the bracket.

**This corrected two documents.** [`ASSETS.md`](ASSETS.md) §4c and `CLAUDE.md`
§5 both said the luma conversion lives in "the second back end that nothing
installs", concluding "no player ever saw it". The reasoning is recorded in
`sub_42FF80`'s own banner: *"`sub_42FA00` is called exactly once, with 0"* — a
caller count taken out of the **decompilation**, which cannot see a VM table
entry. That is `CLAUDE.md` §1's `Script_StartScript` mistake exactly, one
subsystem over: a caller count used to rule out a search space. The `@callers
0` in the banner was the tell.

**Confirmed in play, 2026-09-01.** Asked whether the game has black-and-white
cutscenes, the user answered yes from having played it. That is worth recording
precisely, because of the direction it ran in: the phenomenon was **predicted
from the bytes** — a luma conversion in one of two otherwise-identical bucket
walks, plus a corpus of 14 brackets that are 90% camera opcodes — and *then*
put to someone who could say. It was derived, not fitted to a known effect, so
the mechanism gets the credit a successful prediction earns.

What it establishes: the effect is real and reaches the player. What it does
**not** establish, and neither does anything else here: that the eight chunks
listed above are exactly the ones, or that any given scene is driven by these
opcodes rather than by something else. Those remain corpus results. The tier is
unchanged at **2**; what changed is that the thing the tier-2 evidence
*predicts* has been seen.

**What is still unread**: the other four swapped pointers. The name rests on
the one difference that is established, and on the corpus use that corroborates
it. `verify.py: render back ends`.

86, 69, 49 and 52 all left this table the same way: the handler's *callee* had
a name, or the `.TAG` names of what it wrote supplied the evidence the handler
could not. 49 is the cautionary one — its third field was recorded here as "a
second object" from the operand shape alone, and reading the handler showed it
to be the **variable the result is stored into**.

Op 93's field mapping matters beyond the name: annotating field 0 as a variable
would have invented one, which is why `tools/script_dump.py` carries the
traced field per opcode.

### The four `scx.play` opcodes, and what "wait" means

**verified.** Ops 57/58/59/60 are the world script's handle on the `.SCX`
scene scripts, and they came out together because they share their machinery.

`Scene_FindScriptObject` (0x0044CD10) scans the loaded scene's object array —
pointer at `+12`, count at `+8`, **stride 100**, matching an int16 at `+26`.
That is exactly the 100-byte script-object record of
[FILE_FORMATS 5c](FILE_FORMATS.md), so the operand is a **scene-local object
id**, not a `.TAG` index — which is why no handler in this group logs a domain.
It also settles what the "handle" int32 at `+24` of that record is: its upper
half is the id scripts address the object by.

| op | uses | what it does |
|---|---|---|
| 57 | 451 | `scx.play` — start object `field0` on the scene |
| 58 | 1697 | `scx.play.wait` — the same, and suspend the script |
| 59 | 472 | `scx.play.actor` — start object `field1` on `CHARACTERS[field0]` |
| 60 | 251 | `scx.play.actor.wait` — the same, and suspend |

The `.wait` halves are the ones that write **4** into `ctx+22`; 58 additionally
passes the script's own slot (`ctx+30`) to `ScriptObject_Start`, which records
the running object in the three parallel arrays at `dword_910500/4/8` so
something can resume it. 57 and 59 pass -1 and register nothing. What resumes
them is in [CUTSCENES.md](CUTSCENES.md): a per-frame scan of that table asking
`ScriptObject_IsBusy`, then event 3.

All four then ask `ScriptObject_HasCamEditing` and request camera mode 13 —
the travel camera, with the last field as its travel time — **only if it says
yes**.

> **Corrected.** That function was named `ScriptObject_IsRunning` here, and
> the four bytes at `+94..97` were described as opaque runtime state. Both are
> wrong. The running flag is the int16 at `+28`; `+94..97` are the object's
> **camera-editing slots**, written at load by `Script_LinkCamEditing`
> (FILE_FORMATS 5c). So the guard is not "did it start" — it is "does starting
> this object hand the camera over", which is why the opcode asks it *before*
> requesting a camera mode at all. The asm is unambiguous: `call sub_41D9C0`,
> `test eax, eax`, `jz` past the `push 0Dh` / `call Camera_Request`.
>
> Two things make the correction safe rather than a swap of one guess for
> another: the bytes are **0 on disk in all 4511 shipped objects**, so nothing
> could read them as loaded state; and only **95 of them** ever receive a
> value, one per object, so the old reading would have made 4416 objects
> permanently "not running" while they demonstrably run.

**What mode 13 actually is, and what the travel field is** — read 2026-09-02
and now DRIVEN by the port. The field is converted on the way in —
`fild dword ptr [esp+10h]` / `fstp dword_930818` after clamping a negative to
0 — so the request's `+24` is a **float travel in frames**, which `sub_414A90`
reads into the move's duration. Mode 13 carries no framing of its own: the
camera tick (`04_sys.c` 3704) copies eye, target, fov and roll out of
`dword_9103D4` **every frame**, and `dword_9103D4` is `Scene_GetActiveCamera`
after `Script_PlayAllScripts` (`05_sys.c` 2136) — the scratch camera
`Script_PlayScript` filled from the linked editing at the object's own clock.
When no object sets one and the mode is still 13, the frame loop requests mode
0 with travel 0 (`05_sys.c` 2140): **the editing's end is a cut back to the
player camera.** In the shipped startup scripts the travel field is 0 at **775
of 786** sites and 1 at the other 11, so in the cutscene chain the "travel" is
in practice a cut. The port drives this through
`SceneRunner::activeEditing` (`verify.py: engine: cam mode 13`); see
[CUTSCENES.md](CUTSCENES.md) §2 for the sampling clock it has to use.

It reads as game logic, which is the check no invariant makes. The script that
launches dialog 402:

```
scx.play.actor.wait 0, 259, 0   ; CHARACTERS[0] plays 259 and the script waits
scx.play.actor      0, 306, 0   ; then starts 306 and runs on
actor.goto_address  678         ; ADDRESSES[678] 'Dialogue Telis Cuisine'
dialog.start        402         ; DIALOGS[402] 'Telis/Appart'
```

Telis performs an entrance that must finish, is left in a loop that need not,
and the conversation begins.

### 63 / 89 — the same shape for the player

`Player_GoToMove` (0x0041B6F0) looks the operand up in the bank at the player
record's `[45]` and hands it to **`SetPersoBank`** — the engine's own name,
from its error string `"SetPersoBank, error on GoToMove"`. 89 passes the
script's slot and writes status 4; 63 passes -1 and runs on. So `player.move`
and `player.move.wait`, where a *move* is a clip in the player's animation
bank.

### 104 / 105 — `player.anim.hold` and `player.anim.release`

Both call `Actor_HoldAnimation` on `Actor_Player()`, 104 with 1 and 105 with 0.
It sets or clears bits **0x80** and **0x01** of the channel's flag word — 0x80
in `Perso_SetInputEnabled`, 0x01 in `sub_45A870` — and **neither call touches a
transform**. What `sub_45A870` also does is write `queue[0] = 0x40000000` and
`n = 1`: those two arrays are the channel's INPUT QUEUE and its LENGTH, which
`Perso_InjectInput` (0x0045A9F0) settles by filling exactly them from its own
`a3`/`a2`. `Cef_TickChannel` (0x004A8160) re-asserts both every tick while
`flags & 0x81`, and the channel's own queue rule then throws it away — **a lone
idle word is dropped** (`if (n == 1 && (queue[0] & 0x40000000)) { queue[0] = 0;
n = 0; }`, ASSETS §"how a press gets INTO the queue").

So a held channel is not stopped and not pinned: it **keeps ticking with nothing
pressed**, and the device is cut off besides. For a gait that is what carries the
state machine to its stand state — the player walks to a halt and stands in the
bank's idle, animated. `Actor_EnterDialogueMode` states the other consequence:
"a held channel never plays the group-400 stance, so an scx scene clip keeps the
body". In the scripts they bracket a staged sequence: 104 before
`fade.to_black`, 105 after the last camera and before `fade.from_black`.

The other consumer of `flags & 0x81` is the CAMERA: `sub_415D10` opens
`if ((u32(a1,356) & 0x81) != 0) { v2 = 0; v13 = 0; }`, so a held channel forces
the follow camera's mode to 0 — which is how the engine knows a script owns the
camera for the bracketed run.

> **Corrected twice on 2026-09-03, and the second time is the instructive one.**
> This first said the update "pins the transform back to rest every frame", and
> the port implemented that literally: a T-pose for the whole of AREA 222's
> tutorial. The correction read `sub_45A870`'s two writes as a BLEND STACK
> collapsed to one entry at full weight and called it a freeze — so the port
> latched the pose, and the player stood frozen mid-stride with one leg forward.
> Also wrong. `Perso_InjectInput`, five lines further down the same file, uses
> those exact two arrays as a queue and a count, which settles what they are.
> The lesson is the one CLAUDE.md §1 keeps making: both wrong readings were
> *consistent with the two writes in front of me*, and only the third call site
> — a function that was already NAMED — could tell them apart. Read the other
> users of a field before naming it. (`todo/omk-play.md` 43.)

> Named from effect plus position, not from a self-description, so it is the
> weakest name in this batch. What is certain is the pair and the bits.

### 120 — `var.set.random`, and a handler block that was not the handler

`Var_Set(field2, Random_NoRepeat(field0, field1))`: pick a number in a range
and remember it, never repeating the previous draw (`dword_4E7E8C`). `field0`
is 1 at all 235 sites and `field1` runs 2..22. All three fields go through the
shared fetch. `Random_NoRepeat` (`0x0041D6B0`) returns `lo` outright when
`lo == hi` and otherwise redraws `lo + rand() % (hi - lo + 1)` until it differs
from the previous result — and `dword_4E7E8C` is **one global for the whole
process**, not one per variable, so consecutive draws of two different
variables constrain each other.

**What the port reproduces is where randomness enters, not which numbers come
out.** `engine/` draws from a seeded xorshift32 with the same no-repeat memory
(`verify.py: engine: vm probe`), which is the only honest standard available:
the engine's `rand()` is the C runtime's, seeded by the host. The measurable
consequence is recorded rather than hidden — with random writes disabled the
5958-slot sweep is unchanged and the DB stays byte-identical to `tools/sim`,
and with them enabled **197 slots diverge**, every one of them a script
containing op 120, 19 of which now reach a `dialog.start`. A replica that
picked a different number there is not wrong; a replica that always picked the
same one would be.

The corpus proves the reading without appeal to the assembly at all:

| check | result |
|---|---|
| sites | 235 |
| where the **next** instruction is `push.var field2` | **235 / 235** |
| where the following `case` labels lie within `[field0, field1]` | 230 / 235 |

and the listing says what it is for:

```
var.set.random 1, 4, 119        ; VARIABLES[119] = 'n° VO passant'
push.var       119
case 7, 1 -> media.play 711     ; 'ZVO P465 Passants Direction'
case 7, 2 -> media.play 782     ; 'ZVO P466 Passants Kiss'
case 7, 3 -> media.play 783     ; 'ZVO P467 Passants Conversation'
```

**The trap.** `clean/_vmhandlers.json`'s block for opcode 120 is not opcode
120's handler. It holds the *following* function — a 14-instruction stub that
reads one int16 and discards it — because the real handler at `0x00405480`
starts with `mov ecx, [esp+4]` rather than a `push` prologue and the extractor
took the boundary from the wrong place. Read from the block, op 120 consumes
**2** bytes; read from `Runtime.exe.asm`, it consumes **6**, matching the table
and the corpus. Two of the earlier operand-length "disagreements" have this
shape, so a disagreement between `tools/vm_oplen.py` and the table is a reason
to open the raw assembly, not to trust either.

### 103 — `music.play`, and the area's own music field

**verified.** `Music_PlayTrack` (0x0041E110) `sprintf`s `TRACKS\%d.ADP` from
field 0, sets the loop pointer from field 1 and streams the file; field 2
selects the stream buffer size through `Audio_SetStreamMode`. The handler skips
the whole thing when field 0 already equals `g_MusicTrack`, which it sets
afterwards — "don't restart the track that is already playing".

The test the data could fail is the filename. `gamedata/TRACKS` holds **145** files
scattered over 0..250, so a field that was really something else would miss:

| check | result |
|---|---|
| sites | 521 |
| field 0 names a file that exists in `gamedata/TRACKS` | **518 / 521** |
| the other three | track **0** — `Music_PlayTrack` returns without playing for anything below 2 |

**The engine does the same thing from the area header.** `Area_Load`'s case 9
reads an int16 at **`AREA +142`** and plays it under the identical
`g_MusicTrack` guard, and `Game_Close` calls `Music_PlayTrack(0, 1)` and sets
`g_MusicTrack = -1`. So `AREA +142` is the area's default music track — and
**189 of 189** non-zero values there name a real file, the other 70 areas being
silent. That is the same field tested twice, once from the script side and once
from the loader's, which is the strongest form the harness takes.

The listing reads as game logic. From the arena in `AREA 149`:

```
var.set.random 1, 3, 126        ; VARIABLES[126] = 'case'
push.var       126
case 11, case 1 -> music.play 96, 1, 0
case 11, case 2 -> music.play 102, 1, 0
case  8, case 3 -> music.play 104, 1, 0
```

Three fight themes, one picked at random, each looping. All three files exist.

### 118 / 119 — `fade.to_color` and `fade.from_color`

**verified.** `Screen_FadeToColor` and `Screen_FadeFromColor` are
`Screen_StartColorFade` modes **1** and **2**: one full-screen quad over a
24-bit colour, with mode 1 ramping alpha `0 → 255` and holding it and mode 2
ramping `255 → 0` and then switching itself off. The renderer picks its blend
from the colour — black takes one path, `0xFFFFFF` another, anything else a
third — which is what makes the colour reading falsifiable.

The eight operand bytes are **a 32-bit colour, then two int16**: duration, then
a delay before the fade starts. Fields 0 and 1 are the halves of the colour, so
the top byte of the assembled value ought to be zero:

| check | result |
|---|---|
| sites (118 and 119 together) | 440 |
| top byte of the assembled colour is zero | **440 / 440** |
| black | 314 |
| white | 124 |
| anything else | 2 |

132/133 are the same idea with the colour and the timing fixed, which is why
both pairs appear in the same scripts.

### 131 — `music.volume`

**verified.** `Music_SetVolumeRamp(target, frames)` stores `target << 16` and
the per-frame step to reach it; the audio update walks the current value
towards the target and hands it to `Music_SetVolume`, which clamps to `0..100`
and converts to the DirectSound hundredths-of-a-dB attenuation
(`-10000 * v / 100`). Anything outside 0..100 would be authored nonsense:

| check | result |
|---|---|
| sites | 52 |
| field 0 inside 0..100 | **52 / 52** |
| field 0 = 0 (fade the music out) | 36 |

and the idiom is unmistakable — `music.play 18, 1, 1` immediately followed by
`music.volume 10, 35` to duck it under dialogue, or `music.volume 100, 100`
swelling it across a cutscene camera move.

### 71 / 72 — `scene.load` and `scene.unload`

**verified.** A **scene is loaded over a resident area**. The engine keeps two
areas live (see FILE_FORMATS, "Two scene slots"); `gameDB+12` is an int16 per
*area id* saying which scene is currently loaded there, or `-1`, and
`Area_GetLoadedScene` / `Area_SetLoadedScene` are its accessors.

* **71** takes `AREAS` in field 0 and `SCENES` in field 1. For whichever
  resident slot holds that area it frees the scene there, then calls
  `Scene_Load` and `Actors_SpawnFromTables` for the new one and records the
  area → scene mapping.
* **72** takes the area alone, tears its scene down and writes `-1`. If the
  slot is the calling script's own (`ctx+31`) it also sets bit 8 of the script
  flags — the script's own scene has just gone away.

Two things have to agree, and the second is the one the data could fail.

| check | result |
|---|---|
| `scene.load` sites | 78 |
| field 0 is a valid `AREAS` id | **78 / 78** — but a valid `SCENES` id at only 55%, which is what separates the two fields |
| field 1 is a valid `SCENES` id | **78 / 78** |
| the two **names** agree on the city | **78 / 78** |
| `scene.unload` sites, field 0 a valid `AREAS` id | **82 / 82** |

The last row is the real check. The game's sections are numbered by city —
1 for Anekbah and Qalisar, 2 for Jaunpur, 3 for Lahoreh — and every scene a
script loads into an area is numbered for that area's own city:

```
scene.unload 49        ; AREAS[49] = 'Jaunpur BE Arene'
scene.load   49, 28    ; AREAS[49] = 'Jaunpur BE Arene' | SCENES[28] = '2-34 BE Yob Retour Prison'
scene.unload 44        ; AREAS[44] = 'Jaunpur BE Jenna'
scene.load   44, 13    ; AREAS[44] = 'Jaunpur BE Jenna' | SCENES[13] = '2-33 BE Jenna Retour Prison'
```

Swapping the two fields, or reading the operand as one wide integer, scrambles
that immediately. It is also what the run of them *is*: a script rewriting the
whole rebel base at once, so that returning from the prison finds every room in
its post-escape state.

### 66 / 68 / 75 / 76 / 77 — the prop rig

**verified.** Four opcodes and a variable writer that only make sense together,
and they came out together because they all address one table: the **prop
table** at `AREA +44` / `SCENE +12` (24-byte records — see
[FILE_FORMATS](FILE_FORMATS.md)). Each handler resolves its `OBJECTS` operand
by scanning that table in the script's own area and then in the scene loaded
over it, which is the same two-table search `Scene_LoadProps` does.

| op | uses | what it does |
|---|---|---|
| 76 | 249 | `object.show` — re-parent the prop's node under the **scene root**, set state bit 1 |
| 77 | 183 | `object.hide` — put it back under its own file root, clear bit 1 |
| 66 | 11 | `object.hold` — `Actor_HoldObject`: parent it to a bone of the player and record it at the actor's `+164` |
| 67 | 159 | `object.hold.actor` — the same for a named character, and it writes the object's id into the character record's `+270` |
| 68 | 226 | `object.release` — **no operand**: read `+164` back, find the record, release, hide, clear |
| 69 | 499 | `object.release.actor` — release what a named character holds, and clear its `+270` |
| 75 | 235 | `var.set.used_object` — write the held prop's id into a variable |

Only the scene root is drawn, so 76/77 are show and hide; and the loader
replays `Object_ShowInScene` for every record whose bit 1 is set, which makes a
prop's presence part of the save. That is the same design as `character.show` /
`character.hide` on the 20-byte object table — and 76, 77, 78, 79 are adjacent
entries in the opcode table.

| check | result |
|---|---|
| operands of 66/76/77 | 443 |
| that name a real prop record | **443 / 443** |
| in the script's **own** chunk (the rest are the scene half of the search) | 410 |
| `object.hold.actor` sites | 159 |
| whose field 1 names a prop record and field 0 a character record | **159 / 159** each |
| whose object is called `Gun …` or `Bâton de pouvoir` | **157 / 159** |
| `var.set.used_object` sites | 235 |
| that write `VARIABLES[13] 'ObjetUtilisé'` | **235 / 235** |

That last row is what identifies opcode 75 outright. Its handler ends in
`Var_Set` with an operand-supplied index, so the `.TAG` name of the variable it
writes is evidence the data could have contradicted flatly — and the scripts
read the value straight back:

```
var.set.used_object 13          ; VARIABLES[13] = 'ObjetUtilisé'
push.i8   61
push.var  13
cmp.eq                          ; "is the thing in your hand the Sneak Den?"
```

Opcode **91** `var.set.player_id` is the same shape: `Var_Set(field0,
word_69BC80[player])`, the `CHARACTERS` id of the body the player currently
occupies — which in this game changes. All **62** of its sites write a variable
named `'Joueur'`, or `'Joueur Lahoreh Bibliothèque'` and `'Joueur Anekbah
Librairie'`.

And the whole rig reads as one gesture. Handing Sork the Sneak Den, from
`SCENE 49`:

```
fade.to_black
actor.goto_address 545        ; ADDRESSES[545] = 'Dialogue Sark'
object.show      490          ; OBJECTS[490] = 'Sneak Den Bis'
object.hold      490          ; into the player's hand
player.move.wait 58           ; the hand-over animation, and wait for it
object.release
object.hide      490
dialog.start     348          ; DIALOGS[348] = 'Sork/Sneak'
```

`'Sneak Den Bis'` is a second object id: 61 `'Sneak Den'` is the inventory
item, 490 the prop that exists only to be held during the animation. The
branch above it, for a player who already carries one, is the same sequence
with `inventory.add 0, 61` where the prop handling would be.

### Shoot mode — 80, 81, 82, 84, 116, 117

**verified.** Six opcodes, one subsystem, and the binary names it: the handler
behind opcode 84 refuses with

```
"Le mode Shoot n'est pas activé!!!"
"error : perso is not in shoot mode !"
```

so `Shoot_Enter`, `Shoot_Leave`, `Shoot_ActorEnter` and `Shoot_ActorAction` are
named from the engine's own word rather than from what the code looks like.

| op | uses | what it does |
|---|---|---|
| 80 | 30 | `shoot.begin` — allocate `g_ShootRecords` (100 × 192 bytes, one per actor), set `g_ShootMode`, put the player in actor state **3**, and select the starting weapon |
| 81 | 74 | `shoot.end` — release what the player holds, clear `g_ShootMode`; a non-zero operand also forgets `g_WeaponSlot` |
| 82 | 311 | `shoot.actor.enter` — put `CHARACTERS[field0]` in state 3 and install its behaviour function, picked by the character record's own type field (`+176`) |
| 84 | 319 | `shoot.actor.action` — issue action `field1` with parameter `field2`; the action selects an animation type out of that actor's `.ani` list (0 idle, 1 a move along a path, 2/3 attacks, 4 a hit) |
| 116 | 108 | `shoot.player.suspend` — `g_PlayerBehaviourOff = 1`, input profile 0, and drop whatever the player is holding |
| 117 | 104 | `shoot.player.resume` — `g_PlayerBehaviourOff = 0`, input profile 2, player state back to 3 |

Two things confirm the grouping, and neither is a range check.

| check | result |
|---|---|
| `shoot.actor.action` sites | 319 |
| preceded, **in the same script**, by a `shoot.actor.enter` on the same character | **315 / 319** |
| chunks (of 328) containing any of the six | **21** |
| scripts using `shoot.player.suspend` | 105 |
| that also use `shoot.player.resume` | 98 |

The first is the handler's own precondition showing up in the authored data.
The second is the areas naming themselves:

```
AREA  59  Anekbah Shooting gallery      AREA 61/77/80/144  Jaunpur Tetra 1..4
AREA  71  Jaunpur Docks                 AREA 75/78/143     ...and their Sas
AREA 175  Ix Astaroth 2                 AREA 63/65/67/69   Anekbah CS Archives
SCENE 56  1-10 Supermarché Shoot        AREA 141           Mayerem Hamestagan
```

`ACTOR_STATE` (int slot 101) therefore takes **3 = shoot mode**, alongside the
4 and 5 already known from the morph loader.

> The arena fight in `AREA 149` uses **none** of these — it is `scx.play.actor`
> and `actor.stat.set` on `'Vie Combattants'`. Shoot mode is the gunfight
> sub-game specifically, not combat in general.

### 86 — `var.set.actor_stat`, and the character record's stat block

**verified.** `Var_Set(field2, Actor_GetProperty(field1, field0))`. Field 0 is
the actor — `-1` meaning the player, at **458 of 459** sites — field 1 selects
one of `Actor_GetProperty`'s cases, and field 2 is the `VARIABLES` index.

`Actor_GetProperty` (0x0040B360) reads a **different offset of the 276-byte
character record per case**, and the check is that the variable names the
scripts choose agree with those offsets:

| property | record offset | variables it writes |
|---|---|---|
| 0 | `+108` (pointer) | `Sexe` |
| 1 | int16 `+170` | `Vie` ×3, `Vie Combat Avant`, `Vie Combat Après`, `Vie Temp`, `Vie Temporaire` |
| 2 | int16 `+156` | `Mana` |
| 3 | int16 `+158` | `Carac Speed` |
| 4 | uint16 `+172` | `Argent` ×2 |
| 5 | int16 `+174` | `Anneaux` |
| 7 | uint32 `+176` | `Type Spectre` |
| 16 | int16 `+160` | `Carac Attack` |
| 17 | int16 `+162` | `Carac Body Shield` |
| 18 | int16 `+164` | `Carac Dodge` |
| 19 | int16 `+166` | `Carac Fight Experience` |

**18 distinct variables are written, and every one is written by exactly one
property value.** If field 1 were not the selector that mapping would be
scrambled; instead property 1 only ever writes something called `Vie`, and
16–19 only ever the four `Carac` characteristics. The offsets are contiguous
and in property order — `+154, +156, … +176` — which is the structural half of
the same check. `readable/types.h` carries the block as `CHAR_*`.

> Properties 0, 6 and 9–15 write the getter's **pointer** slot rather than its
> value slot, and this opcode reads the value slot. Property 0 `Sexe` is used
> at 5 sites and returns whatever was on the stack. The other caller of
> `Actor_GetProperty` — the `.SCX` script service dispatcher, case 44 — is the
> one those cases are for.

### 93 — `actor.stat.set`, the opcode named after its side effect

**verified**, 2026-09-02, and it is a **rename**: the table called this
`hud.show_var` until then, and the name was taken from the first thing the
handler does on one of its two arms.

Read from **0x00404790** (the VM table's own entry; `asmfn.py --op 93`). Three
int16 fields through the shared fetch — field 0 the **actor**, `-1` meaning the
player; field 1 the **property**; field 2 the **variable**, which is also the
field it announces to the tag logger. Then:

```
Var_Get(field 2)                        -> the value  (logged as VALUES)
field 0 == -1 :  Hud_ShowValue(Actor_Player(), field 1, value, playerRec+0xAA)
field 0 != -1 :  Scene_FindObjectRecord(areaSlot, field 0) -> the record, id at +0
both          :  Actor_SetProperty({ +0 property, +8 value, +16 actor })
```

`Actor_SetProperty` (0x0040B8D0) is the writer: it maps the property to the
same offsets of the 276-byte character record that `Actor_GetProperty` reads
(1 → `+170`, 2 → `+156`, 3 → `+158`, 4 → `+172`, 5 → `+174`, 16–20 → `+160`…
`+168`, 35 → the `+260` array), clamping most of them at 200 and property 4 at
65535.

So **93 is the exact mirror of 86 `var.set.actor_stat`** over one field layout:
86 is `Var_Set(field 2, Actor_GetProperty(…))`, 93 is
`Actor_SetProperty(…, Var_Get(field 2))`. `Hud_ShowValue` (0x0041CE50) is a
flash of the changed stat on the HUD and nothing more — it returns 0 for anyone
but the player and for properties 4 and 5, while the property write happens
either way. The old name survived because every variable the opcode touches
*is* displayable; that is a consequence of it being the stat channel, not
evidence about what it does.

This is the pair that moves health, money and every other stat a script changes.
**Ported 2026-09-02 at both ends** (`engine/src/script/interp.cpp`, `props.h`),
with two things the assembly settles that the decompilation only implies:

* **the clamp is unsigned.** Every `Actor_SetProperty` case that caps at 200 is
  `cmp esi, 0C8h ; jbe` (and Argent's `cmp esi, 0FFFFh ; jbe`), so a script that
  subtracts damage past zero and writes the result back heals the actor to 200.
  `verify.py: engine world ops` asserts `-5 → 200`;
* **the player needs no actor table.** `Actor_FindById` returns `g_GameDB + 60`
  before it scans a chunk whenever the id is −1 or the DB record's own `+272`, so
  the player's stat block is the saved block itself — 736 of the 784 sites of 93
  and 459 of the 460 of 86 — and only the other 49 reach a chunk record, through
  `WorldHooks::getActorProperty` / `setActorProperty`.

Properties 0, 6 and 9–15 fill the pointer slot, and 0x15, 0x16 and 0x22–0x24 use
the value slot as an *input* index, so through 86 they store or index by stack
garbage; the port leaves the variable on those (5 shipped sites, all property 0).

### 75 `var.set.used_object`, 67/68/69 and 76 — the held object and the props

**read from the handlers 2026-09-02**, and ported behind `WorldHooks`
(`engine/src/script/hooks.h`): the state they touch is the live actor table and
the 50 object slots, which no DB field holds.

| op | handler | what it does |
|---|---|---|
| 75 | 0x40AC90 | `Actor_HeldObjectSlot(Actor_Player())`; the variable gets `word_4E6CA0[slot]` when the slot is 0..49, **−1 otherwise** — so with nothing held it reads −1, not its old value. It is where `Script_RunToOpcode75` stops when an inventory item is used on a zone. 235 sites |
| 67 | 0x40A9D0 | `object.hold.actor actor, object`: the prop record by id (`+2`), AREA `+44`/`+74` then the SCENE over it `+12`/`+42`; if `Actor_FindById(actor)->+270` already equals the object, nothing; else `Actor_HoldObject(index, rec+0)` when a record with a runtime slot was found, and `+270 = object` either way. No prop-state write. 159 sites |
| 68 | 0x40AAF0 | `object.release`, no operand — the PLAYER drops what it holds. Bails when `Actor_HeldObjectSlot` is −1. Then the record whose **`+0` equals the held SLOT** (`movsx edi, word ptr [edx]`), not the id: found with state bit 0 set → `Actor_ReleaseObject(player, 0)` (dropped where it was), `ObjectState_Set(+22, state & ~2)`, `Object_HideFromScene(slot)`; otherwise `word_4E6CA0[slot] = −1` and `Actor_ReleaseObject(player, 1)` (freed). Both arms end with the player record's `+270 = −1`. 226 sites |
| 69 | 0x40AC20 | `object.release.actor actor`: `Actor_HeldObjectSlot` of that actor, −1 → nothing; else `Actor_ReleaseObject(index, 0)` — a DROP, the same function the review called `Actor_SetState` — and `Actor_FindById(actor)->+270 = −1`. 499 sites |
| 76 | 0x40ACF0 | `object.show object`: the record by id; `ObjectState_Get(+22) & 1` → `ObjectState_Set(+22, state \| 2)` and `Object_ShowInScene(+0)`. Bit 0 clear: nothing. No record: `xor esi, esi` and a read at `[esi+16h]` — the Win9x null page, which the shipped game reads rather than faults on. 251 sites |
| 98 | 0x404DB0 | `object.place_at object, address`: the object's slot in the id table, `Address_Find`, `sub_41CF50(slot, pos)`. World positioning only, no state. 6 sites |

The runtime slot is `Scene_LoadProps`'s (0x00409FC0): for every prop record whose
state has bit 0, the **first free** entry of `word_4E6CA0` takes the id and the
record's `+0` takes the index (−1 when all 50 are taken), AREA's table then the
SCENE's; bit 1 then links it into the scene. That is why 68 can match on `+0`
and why the field is −1 in all 670 shipped records.

### 70 — `ui.open`, and the game's 37 screens

**verified.** The handler writes **6** into the script's status word — so
`Script_Execute` stops until the screen closes — and hands field 0 to
`UI_OpenScreen`, which indexes a 92-byte table of interface screens and calls
`UI_LoadScreen`. That function builds `I2d\bitmaps\%s`, and the game ships an
`gamedata/I2D/bitmaps` directory.

**Field 2 is where the player's answer goes.** The handler stores it in
`dword_4E6B28` (`mov dword_4E6B28, ecx`) before suspending, and the screen
writes the choice into that variable on close — so `ui.open` is a *question*,
and the script branches on the reply. The game's own opening is the clearest
case: AREA 118's startup script calls `ui.open 29, -1, 19` — screen **29 is
`OMK START MENU`** — and then immediately tests `Interface` (variable 19),
taking a short path when it is 0 and the full intro otherwise. Field 1 is
stored in `dword_4C0B64` when it is not -1, and screen 29 alone also parks the
calling context in `dword_4E6C7C` (`cmp edi, 1Dh`).

**What closes the loop is `Game_HandleEvent` case 5.** The screen hands its
result back through event 5, whose argument block carries the waiting
context's index at `+4` and the chosen value at `+8` — `sub_466B60` fills both
from `dword_930744` / `dword_930750`. The handler then does exactly two
things:

```c
if (dword_4E6B28 != -1) { Var_Set(dword_4E6B28, u32(a2, 8)); dword_4E6B28 = -1; }
u16(ctx, 22) = 1;                       /* the caller resumes */
```

so the answer lands in the variable `ui.open` named and the script continues
after it. (Screen 29 is special-cased once more here: the context it parked in
`dword_4E6C7C` is cleared when the answer is non-zero.)

**The corpus agrees, and correcting the enumeration is what makes it agree.**
Of the `ui.open` sites, **48 name an answer variable and 194 pass 0xFFFF** —
the format's "no answer wanted" sentinel, the same one `area.goto` uses. Where
a site names a variable *and* a `push.var` follows within three instructions,
the two are the same variable in **23 of 23**, 0 disagreeing; a wrong field
would have had 23 chances to break.

The site count is **242, not 241**, and the difference is the point. Walking
only the zone records and the message subscriptions — the 5785-slot corpus
every other count on this page uses — finds 241 and **no site for screen 29 at
all**, which contradicts the worked example above. That walk never reaches a
chunk's **startup script at `+4`**, the same gap CLAUDE.md records for the
cutscene beats, and AREA 118's `ui.open 29` is in one. It is the only script
site for the start menu in the game. `verify.py: ui open answer`.

**And the middle of the loop is a 7-slot menu widget.** `UI_OpenScreen` parks
the caller's context index in `dword_930744` and sets the answer
`dword_930750` to **-1**; `UI_GridMenuInput` (0x004B00D0) moves the selection
and, on confirm, writes the chosen slot into it; `UI_SendAnswer` (0x0042B560)
fires event 5 with the pair and clears `dword_930744`, which is what makes it
fire once.

The widget's own arithmetic gives the layout — six slots in a **3-wide, 2-deep
grid plus one standing apart at index 6**:

```
bit 4   up       top row -> 6;  6 -> 4;  else slot - 3
bit 8   down     top row -> slot + 3;  wraps through 6
bit 1/2 left/right   within the row, by `slot % 3`;  6 excluded
bit 16  confirm  dword_930750 = slot - 1  (slot 0 gives 6)
```

The `% 3` establishes the three columns, and slot 6's asymmetric wrapping — up
from the top row reaches it, up from *it* reaches 4 — establishes that it sits
outside the grid rather than in it.

> **Correction, 2026-08-30: this widget is the LIFT, not the start menu.**
> The line that used to stand here said "Screen 29 `OMK START MENU` is exactly
> that shape". It is not. `UI_GridMenuInput` has **exactly one reference in the
> whole image** — a data reference, and it is `0x004E505C`, the `+4` input hook
> of the **LIFT** screen's list, which has **7 items** against the **7** floor
> labels in `IAM\Lift` ("Niveau 1 : Bureau du commandant Gandhar" … "Niveau
> -5"). An elevator's floor panel is what a 3-wide grid plus one apart
> actually is.
>
> Screen 29 is a plain **4-item vertical list** — "Nouvelle partie", "Charger
> une partie", "Options", "Quitter" — whose three lists carry **no** `+4` hook
> at all, so it uses the default `Ui_MoveSelection` walk. Its answer is still
> what variable 19 `Interface` carries; it just does not come from this
> widget. The mis-attribution was found by making the simulator walk the tree
> ([`UI.md`](UI.md) §3c–3d, `verify.py: sim: ui`).

**Where the answer actually comes from.** `UI_SendAnswer` has one call site in
the binary — inside `Ui_OpenSneakFamily` — so it is not the general path
either. A screen writes `dword_930750` when it has an answer and the close
delivers it; there are three read-and-clear sites. For screen 29 the value is
written by the item callback at the end of the navigation: confirm on
"Nouvelle partie" opens a confirm dialog, and **"Confirmer" (0x0047A2B0)
carries `mov dword_930750, 1`** — which is exactly the `Interface` = 1 the
shipped save records. The simulator now derives it that way rather than being
handed the number.

That write is **gated**, and the gate is the callback's first instruction: it
tests the name field's cursor (`dword_657994`) and jumps to its own `ret` when
the field is empty, so it writes neither the answer nor the screen's state
word and the calling script stays suspended at `ui.open`. An empty name — or a
duplicate one, refused a branch later — therefore cannot start a new game.
[`UI.md`](UI.md) §3f has the disassembly; `verify.py: ui confirm gate` pins it
to the bytes.

That makes the whole question/answer path modellable, and `tools/sim`
implements it: the simulator suspends at `ui.open` and resumes on an explicit
answer rather than having the variable set before the run. Which value a person
picks is still player input — for the intro it is **1**, which
`verify.py: save file` reads out of a real save — but it is now one supplied
number at the point the engine asks for it. See `verify.py: sim: area load`.

The screen names sit contiguously in the binary, each with its bitmap and a
short label, and they reconstruct the table in order:

| | | | | | |
|---|---|---|---|---|---|
| 0 `VIDEOPHONE` | 1 *(ELIMINE)* | 2 `MULTIPLAN` | 3 *(ELIMINE)* | 4 `LIFT` | 5 `TERMINAL` |
| 6 *(ELIMINE)* | 7 `SLIDER` | 8 *(ELIMINE)* | 9 `SNEAK` | 10 *(ELIMINE)* | 11 `FIGHT SIM` |
| 12 `GANDHAR DOOR` | 13 *(Den00.bmp)* | 14 `XACHEN` | 15 `SURV ERROR` | 16 `SURV NO KIT` | 17 `SURV KIT` |
| 18 `ARCHIVES` | 19 `MORGUE` | 20 `BANK` | 21 `PHARMACIE` | 22 `ARMURERIE` | 23 `RESTAURANT` |
| 24 *(boutiq.bmp)* | 25 `SORCELLERIE` | 26 `LIBRAIRIE` | 27 `SEX-SHOP` | 28 `DIVERS` | 29 `OMK START MENU` |
| 30 `SAVE GAME` | 31 `PAUSE GAME` | 32 `LIB. LAHOREY` | 33 `SHOOT MECA` | 34 `SHOOT HUMAN` | 35 `OPTIONS` |
| 36 `HIGH-SCORE` | | | | | |

That order is read off the string area, so it is an inference — but three
independent things confirm it, and none is a range check:

| check | result |
|---|---|
| bitmap names in the table vs. the files in `gamedata/I2D/bitmaps` | **11 = 11, exactly** |
| shop-screen sites opening in an area whose **name** says so | **57 / 57** |
| sites using any of the five `(ELIMINE)` indices | **0** |
| `MULTIPLAN` sites reached by an `actor.goto_address` | 73 |
| ...that walk to an `ADDRESSES` entry called `Multiplan N` | **73 / 73** |

The middle row is the strong one. `BANK` is opened in the three areas called
`Banque` and nowhere else; `PHARMACIE` in the four `Pharmacie`; `LIBRAIRIE` in
the four `Librairie`; `SEX-SHOP` in `Qalisar Sexshop 1` and `2`; and
`LIB. LAHOREY` **40 times, every one of them in `Lahoreh Bibliothèque`**. Shift
the index base by one and all 57 break at once.

Two more, from the code rather than the data. The handler **singles out id 29**
and stashes the script context for it alone — 29 is `OMK START MENU`, the one
screen that has to come back to where it was called from. And
`shoot.player.resume` opens **33 or 34** depending on the player's type field:
`SHOOT MECA` if the body is a mecanoid, `SHOOT HUMAN` otherwise.

Indices 13 and 24 carry no name string of their own. They are labelled here for
what the scripts open them in — 13 only ever in `1-16 Appart Den`, whose bitmap
is `Den00.bmp`, and 24 in the bars alongside the other `boutiq.bmp` shops.

> **Confirmed from the code, 2026-08-30, and the inference was right.** The
> screen *definition* table — 37 × 92 bytes at 0x004CB640, the one
> `UI_LoadScreen` scans — carries the id at +4 and the label at +0, and +4 runs
> **0..36 in table order**, so the record index is the `ui.open` operand
> directly. The two unnamed rows are its own strings: **13 is `DEN` and 24 is
> `BAR`**. The same table also explains why the shops share everything but an
> index: +8 is a fixed parameter that overrides the caller's, and it runs 0..9
> across the ten `boutiq.bmp` screens. See [`UI.md`](UI.md) §2.

> Five screens marked `(ELIMINE)` — `TRANSCAN`, `APPARTEMENT`, `JOURNAL`,
> `PUZZLE`, `FIGHT_NOT_SIM` — are cut content the table still has room for,
> which fits the 106 conversations with no launch path.

### 49 / 52 — the inventory query and the mass remove

**verified.** Op 49 is `Var_Set(field2, list field0 contains OBJECTS[field1])`.
Its third field had been recorded as "a second object"; the handler ends in
`Script_StoreVar`, and the corpus is decisive:

| check | result |
|---|---|
| sites | 222 |
| field 2 is a variable named `Inventaire`, `Inventaire 2` or `CDs bowie in inventory` | **215 / 222** |
| the variable is read back by a `push.var` within five instructions | 197 |

Op 52 `inventory.remove_all` removes **every** copy of the id — the same
shift-down as `inventory.remove`, looping, moving the id array and the 56-byte
records at `dword_69BD68` together. Its `-1` form (37 of 210 sites) sweeps the
list of every object whose `IAM\OBJECT` record has **bit 1 of the flags at
`+4`** — set on 245 of the 1002: weapons, ammunition, consumables, keys, but
*not* money or rings, whose flag value (32) lacks the bit. 29 of those 37 sites
sit next to `player.become`: lost on reincarnation.

> This claim was first published as "flag byte at +36, 88 objects", and the
> check written for it **passed** — because its expectation was computed by the
> same wrong expression. The handler reads the 56-byte *list* record at +0x24,
> and that record is `[display name 32][header 24]`, so the byte is the
> original record's `+4`; "+36" landed inside the display name, where bit 1 of
> a letter is noise. A verify expectation derived from the code under test
> tests nothing — the number has to come from an independent reading.

### 56 — `player.become`

**verified.** Reincarnation, the game's signature mechanic. The handler returns
at once if `word_69BC80[player]` already equals the operand — the same "already"
guard as `music.play` — then tears down the old player's attachments, moves the
new body in, hands the old body the vacated object-table record, and copies the
**276-byte character record into the game DB**: 268 bytes to `+68`, the two bio
strings into the buffers its header points at. That copy is why the stats of
whoever you currently are persist in the save. All **43** operands name a
character record, and `var.set.player_id` is its read-back.

And the listing states it outright — from `AREA 22`:

```
player.become        418      ; CHARACTERS[418]
actor.stat.set       -1, 5, 60    ; VARIABLES[60] = 'Anneaux' -> the player
inventory.remove_all 0, -1
media.play           469      ; OBJECTS[469] = 'ZVO P059 Réincarnation Réussie'
```

The voice clip played on success is called *"reincarnation succeeded"*, and the
`-1` sweep of `inventory.remove_all` runs in the same breath — the two
mechanisms confirming each other.

### 62 — `fight.begin`, and the three `.CTL` slots

**verified.** The character record holds three 9-byte `.CTL` name slots, and
the shipped names say what each is for:

| slot | offset | names | which is |
|---|---|---|---|
| 0 | +72 | `H1AVNT`, `F1AVNT`, `MECA`, `SHAM` | **aventure** — walking around |
| 1 | +81 | `H1SHOT`, `F1SHOT` | **shoot** — the gunfight sub-game |
| 2 | +90 | `H1CMBT`, `F1CMBT`, `D1CMBT` | **combat** — melee |

(H1/F1/D1 = homme / femme / démon.) `fight.begin` switches **both** the player
and `CHARACTERS[field0]` to slot 2 via `Actor_CtlSlotName`, then `Fight_Engage`
installs the opponent — `g_FightOpponent`, torn down and replaced if one was
already set — the script suspends with status **3**, and camera mode **14** is
requested. Field 0 names a character record at **108 / 108** sites; field 1 is
0 at all of them.

### 46 / 90 — `scx.play` on the player

**verified.** The sixth and seventh members of the `scx.play` family:
`ScriptObject_StartOnActor(Actor_Player(), …)`, then the same camera mode 13
request, field 1 the travel time. 46 registers the script's slot and writes
status **4** — `scx.play.player.wait`; 90 passes −1 and runs on. The family is
now complete: scene (57/58), named actor (59/60), player (90/46).

### 87 / 88 — `address.enable` / `address.disable`

**verified as mechanism.** An opposite pair on the game DB's **third bitmap**
(`+24`, one bit per `ADDRESSES` entry, written by `Address_SetEnabled`) — the
bitmap the earlier three-array table listed as "not yet traced". Every one of
their 63 operands is a city destination with a postal-style name — `'Anekbah -
Bar Zone 52'`, `"Lahoreh - Place d'Yrmali"`, `'Jaunpur - 46, Temple Square'` —
and never an internal address like `'Dialogue Telis Cuisine'`. That reads as
the slider's unlocked-destination list, but no reader of the bit has been
traced, so the names stay neutral.

### 126 — `camera.set.at_address`, and the world camera table

**verified.** `Camera_FindWorld` scans **44-byte records with the id at +24**
in three places — `AREA +64` (count `+84`), `SCENE +32` (`+52`) and
`GLOBAL +20` (`+30`) — which identifies both the 44-byte "coordinates" array
and `GLOBAL`'s "record array, 44 bytes each" as the **world camera table**,
5381 records. The record the handler consumes: two int32 triples at +0/+12
(eye and aim), int16 parameters from +24. Op 126 loads one, aims it at an
`ADDRESSES` entry resolved by `Address_Find`, issues camera mode 12 and
suspends with status 7 — `camera.set.wait` for a world camera.

| check | result |
|---|---|
| field 0 in the world camera table | **84 / 84** |
| field 1 in `ADDRESSES.TAG` | **84 / 84** |
| field 2 (travel time) | **20 at all 84** |

### The tail — everything at 32 uses or fewer

Twenty-four opcodes named in one pass, closing the exercised set. Grouped by
what they turned out to be:

**The mission timer** — 111 `timer.stop`, 112 `timer.start`, 113 `timer.set`,
114 `timer.mode`, 115 `var.set.timer`. One flag word (`g_TimerFlags`), a value
and a start tick; `Timer_Format` renders minutes:seconds.hundredths for the
HUD. The check that pins field meaning: `timer.set`'s operand is **seconds**
(the handler multiplies by 1000 in place) and it is **900 — fifteen minutes —
at all 12 sites**, which are the Tetra bomb raids; `timer.mode` is 12 at the
same 12. `var.set.timer` reads the elapsed or frozen value back into a script
variable.

> **111 and 112 were named the wrong way round** until 2026-09-02, and the
> table is being corrected with them. Both handlers are a bare tail `jmp` into
> a function with no `proc` label — 0x00405350 → `loc_41E2B0`, 0x00405360 →
> `loc_41E2D0` — so neither is in the decompilation and both have to be read at
> the address the handler names. 111 sets flag bit 0 and 112 requires it, then
> stamps `g_TimerStart = g_ClockTime` and clears it; `Timer_Elapsed` returns
> **0** when the flags are exactly 1, so bit 0 set is a *stopped* timer and the
> opcode that stamps the start is the one that runs it. Three things agree:
> `Timer_SetValue` and `Timer_SetMode` both refuse unless bit 0 is set, so the
> configure pair can only precede the start; all 12 bomb sites read
> `timer.mode 12`, `timer.set 900`, **then 112**; and the shooting range does
> `shoot.end`, **111**, then `var.set.timer`. Full decode, flags and all, in
> [GAME_STATE](GAME_STATE.md) "The script timer".

**The world** — 45 `area.preload` (load an area into the *other* resident slot
without transitioning, status 8; sits before lift and door screens), 123
`set.hide_piece` (clear a 76-byte decor piece's visible bit; the show path is
never scripted), 98 `object.place_at` (move a prop to another object's
position), 127 `player.pos.sync` (reset the collision walker's position pair to
the node transform and invalidate the ground cache), 129/130
`walk.ledges.ignore` / `.obey` (the walker refuses a step down of more than
11.81 units — 30 raw — unless `g_IgnoreLedges` is set; scripts bracket staged
moves with the pair).

**Presentation** — 54 `camera.follow_player` (mode-0 request), 136
`camera.shake` (`Camera_SetShake`: a decaying vertical sine on eye and aim;
duration, then amplitude ÷ 2.54), 94 `image.show` (`IMAGES\%06lx.BMP` — all
four shipped operands name a file that exists), 146/147 `ambience.on` / `.off`
(the flag the `.WRE` ambience updater runs under — `SOUKT.WRE`,
`SMARKET1.WRE`), 144 `morph.play` (a `%06x.3dm` talking head on a named
character — the mechanism is certain, but **all three shipped sites name
morphs that do not exist**: live opcode, cut content).

**Inventory and state** — 128 `inventory.transfer` (move up to
`Var_Get(field)` objects between two lists, records included), 148/149
`inventory.save` / `.restore` (stash list 0's 18 ids in `g_InventoryStash`
plus five character-sheet int16s; restore re-reads each from `IAM\OBJECT` —
they bracket the sub-games), 142 `ui.highscore` (insert `Var_Get(field1)`
under the character's name, open screen 36 `HIGH-SCORE` — the shooting
gallery's exit), 106/107 `shoot.freeze_all` / `.unfreeze_all` (bit 15 of every
combat record's flag word at once).

**And 152 — `game.restart`.** The handler sets `g_RestartRequest`; the game
loop answers by resetting the session, calling **`Game_NewGame`** and fading
from **white**. Three sites: Lahoreh, Tetra 1, and `Ix Astaroth 2` — where
Astaroth captures the player's soul, which in Omikron restarts the game. This
is also the opcode whose handler block was the session's original extraction
trap: the block "with 6072 lines" is the interpreter that happens to follow
it; the real handler is seven instructions.

**Two more operand lengths.** 146 and 150 joined the no-operand set the same
way 147/148/149/151 had: their handlers never touch the instruction pointer,
and at the table's claimed 2 the swallowed bytes decoded as phantom debug ops
(op 0: 187 → 149, op 1: 55 → 37 across the corpus, no failures, site counts
unchanged). That made **12 corrected operand counts** in that pass; the
generated table above now marks **20** in all, the newest being 16, 43 and 44
(2026-09-02, read from their handlers — 0 shipped sites between the three, so
only the table could ever have been wrong about them). The corrections
recovered a handful of previously mis-decoded instructions, which is why
several counts elsewhere in this file ticked up (`music.play` 514 → 521,
shoot pairing 309 → 315 — all ratios held or improved).

## Operands are int16 fields

**verified.** An instruction's operand bytes are a list of **int16 fields**, not
one wide integer. A 4-byte operand is two fields and a 6-byte operand three, and
the opcode's `.TAG` domain applies to whichever field is an index.

Reading them whole is wrong and looks plausible until annotated. Op 50's four
bytes as one int give `OBJECTS[25493507]`; as two int16 they give
`OBJECTS[3], OBJECTS[389]`:

| op | domain | uses | valid as one int | valid as int16 fields |
|---|---|---|---|---|
| 15 | VARIABLES | 9 | 0% | **100%** |
| 50 | OBJECTS | 472 | 0% | **100%** |
| 51 | OBJECTS | 78 | 0% | **100%** |
| 52 | OBJECTS | 210 | 0% | **99%** |
| 67 | OBJECTS | 159 | 0% | **100%** |
| 49 | OBJECTS | 222 | (6-byte) | **100%** |
| 47 | AREAS | 757 | (6-byte) | **99%** |

Ops 93, 95, 96 and 126 do *not* resolve on field 0 (6%, 69%, 48%, 0%) — either
the domain applies to a later field or the tag table is sparse. Not established;
do not assume field 0 is always the index.

This was found by *reading a listing*, not by an invariant: nothing about
`OBJECTS[25493507]` breaks a decode, it just cannot be true. See
`tools/script_dump.py`.

## How a conversation is launched

**verified.** Opcode **61, `dialog.start`**, handler `0x403560`. It is the only
path into a conversation, and it is a direct one — the handler reads its operand
and calls `Dialog_Load` (0x401800) itself:

```
mov     cl, [eax]            ; a 2-byte operand
mov     dh, [eax+1]
test    esi, 4000h           ; bit 14 set -> indirect
mov     eax, [edi+24h]       ;   look the value up in the script's operand table
movsx   esi, word ptr [eax+esi*2+2]
push    offset aDialogs      ; "DIALOGS"
call    sub_40EC70           ; trace it under that domain
push    esi
call    sub_401800           ; Dialog_Load
```

So the operand is normally a literal `DIALOGS` index, but **bit 14 makes it
indirect** — the index is fetched from the running script's own operand table,
which is how a trigger picks between conversations at run time.

Three things this rules out, each checked:

* **Not the conversations themselves.** Opcode 61 appears in **none** of the
  612 scripts in `IAM\DIALOG` — the 25 opcodes they actually use are listed
  above, and 61 is not among them. A conversation never starts another one.
* **Not the scene scripts.** `SCPTDATA/*.SCX` has only 17 script functions and
  none of them touches conversations; see FILE_FORMATS.md section 5c.
* **Not the dialogue driver's command 0**, which also reaches `Dialog_Load`
  (`sub_4067D0(0, index)`) but has no literal call site anywhere in the
  decompilation.

**It is the world scripts** — the other `IAM` archives, which hold bytecode in
the same format:

| archive | what it is | scripts | `dialog.start` sites |
|---|---|---|---|
| `IAM\AREA` | trigger volumes | 5143 | **1001** |
| `IAM\SCENE` | per-scene scripts | 632 | 84 |
| `IAM\GLOBAL` | ambient street conversations | 10 | 161 |

**1246 sites in total, and every single operand names a real conversation
(100%)** — 235 distinct conversations out of the 420 in the archive.

### The record that holds them

**from code** — `Scene_Load` (0x0040C120) and `Area_Load` (0x0040CC90). They
share one header; `AREA`'s is the same layout shifted 32 bytes on by a leading
run of eight `-1`:

| | record array | count |
|---|---|---|
| `SCENE` | int32 at `+16` | **int16** at `+44` |
| `AREA` | int32 at `+48` | **int16** at `+76` |

Both relocate the record's **first three int32** from file offsets to pointers —
that is what makes `+0`, `+4` and `+8` the script slots — and both convert the
rest of the record with the same `* 100/256/2.54 - 1` used on `DialogCamera`
positions, plus `* 360/4096` on the two int16 at `+60`/`+62`. So a record is
**four XYZ points and two angles: a trigger volume**, 68 bytes.

Each archive also carries a **second script table** — 8 bytes per entry with
the offset at `+0`, the shape `GLOBAL` uses for its only one — relocated by a
separate loop after the records:

| | second table | count |
|---|---|---|
| `SCENE` | `+36` | int16 at `+54` |
| `AREA` | `+68` | int16 at `+86` |

`AREA`'s holds **6 `dialog.start` sites the record walk alone never sees**.

The third relocated array in each (`SCENE +24` / `AREA +56`, stride 276) carries
no bytecode — but it is not a dead end. It is the **actor table**: keyed by
actor id at `+272`, naming each character's model at `+144` and its `.CTL` at
`+72`. `Actor_FindById` (0x0040B190) scans exactly those two arrays, and that is
what links a conversation to the character who speaks it — see
[FILE_FORMATS.md](FILE_FORMATS.md) section 5e.

**verified: 259/259 `AREA` chunks and 71/71 `SCENE` chunks**, every script slot
decoding — **5785 of 5785** — and **1246 `dialog.start` sites, every operand a
real conversation (100%)**, launching **235 of the 420**.

> Worth recording how this went wrong first. Guessing the layout from the data
> got `AREA` right by luck and `SCENE` wrong: I read the count as an int32 at
> `+44`, but it is an **int16**, so any chunk with something non-zero at `+46`
> was rejected — 22 of 71. Reading the loader fixed it in one step and more than
> doubled the recovered scripts, 257 → 605. The 18 `AREA` chunks that look
> empty really are: their count is 0, and the loader's own guard is `if (n > 0)`.

### The world scripts use far more of the VM

**verified.** The 612 conversation scripts exercise 25 opcodes. The world
scripts exercise **119** — the great majority of which `IAM\DIALOG` never
touches, including
`jmp`, `case` and `dialog.start`. They are small: median 4 instructions, mean
10, none longer than 372.

That is why the operand table was wrong in nine places. Its counts had only
ever been checked against the conversation corpus, which exercises a fifth of
the instruction set; the rest went untested until the world scripts could be
read, where 53 slots desynchronised onto an opcode above 152.

### Recovering the true operand counts

The interpreter keeps the instruction pointer at `[ctx+0Ch]`. A handler loads
it, walks it forward over its operands, and stores it back — so **summing those
advances is the ground truth**, independent of the table. `tools/vm_oplen.py`
does that over the handler assembly.

Two things have to be excluded, and both bit before they were: a handler often
**reuses the register** once it is done with the pointer (keep counting and the
total runs away), and **op 47 rewinds the pointer by 7** (`add dword ptr
[esi+0Ch], 0FFFFFFF9h`) to loop — control flow, not operands.

The method reproduces the one correction that had been found by hand, op 42 → 3,
without being told about it. Testing each remaining disagreement against the
corpus isolated the ones that are real:

| op | table | actual |
|---|---|---|
| 17 | 2 | **4** |
| 80 | 0 | **2** |
| 84 | 4 | **6** |
| 118, 119 | 6 | **8** |
| 144 | 2 | **6** |
| 147, 148, 149, 151 | 2 | **0** |
| 57, 58 | 4 | **6** |
| 78 | 2 | **4** |
| 103 | 2 | **6** |

The last four take no operands at all — their handlers never touch the
instruction pointer. Opcode 151's is eight instructions long and simply calls
`sub_42FA00(0)`.

### The failures a decode test cannot see

**The first nine corrections were all found by a decode that broke.** Three more
were invisible to that test, and the way they surfaced is the point.

An operand count that is short by an **even number of zero bytes** does not
desynchronise anything: the surplus decodes as opcode 0, a legal zero-operand
instruction, and the script still reaches `end`. Every integrity check passes.

What exposed them was reading a listing and noticing opcode 0 — whose handler
hex-dumps 64 bytes to `printf` — running at **10.9% of all instructions**. No
shipping game executes a debug dump every ninth instruction. Tracing which
opcodes preceded runs of zeros gave the culprits directly: opcode 57 is followed
by exactly two zeros in **97%** of its uses, 78 in 63%, 58 in 60%.

Correcting them drops opcode 0 from **11.2% to 2.9%** with **no new failures**,
and `tools/vm_oplen.py` gives the same three lengths from the handler assembly
independently.

**Op 103 was a fourth, and it had been recorded the wrong way round.** The
assembly reads three int16 in a straight line — no branch, nothing conditional
— but an earlier corpus test, run while 57/58/78 were *still wrong*, showed a
script breaking at 6, so the doc concluded the static reading was the mistake
and left it at 2. It was not: once those three were corrected, 6 decodes with
no failures, the same 514 sites, and **2056 fewer instructions** — exactly four
surplus bytes per site, which at 2 were showing as **1531 phantom
`dbg.dump_ctx` and 525 phantom `dbg.dump_code`**. Opcode 0 drops again, 2.83%
→ 0.32%.

The general lesson is narrower than "trust the corpus". A corpus verdict is
only as good as the rest of the table: while any operand length is wrong, a
test of one *other* length is testing both. Where the assembly is
unambiguous — straight-line reads, no branch — it should be believed and the
corpus disagreement treated as a symptom elsewhere.

Not everything the zero-padding heuristic suggests is real: `1 -> 3` would drop
opcode 0 further, to 0.5%, but introduces **11 decode failures**. The corpus
overrules it, and opcode 1 is left alone.

**Result: every script slot in `AREA`, `SCENE` and `GLOBAL` decodes
cleanly — 5785 of 5785, against 53 failures before**, and the conversation
corpus still decodes 612/612 — the corrections only touch opcodes it never used. The recovered
`dialog.start` count rises from 1066 to **1246 sites, still 100% naming a real
conversation**, launching **235 of the 420**.

Not every disagreement was real: applying all 21 the assembly suggested made
things *worse* (53 → 58 failures), so each was tested on its own and only those
that reduced failures were kept. The rest are left as they are.

    python3 tools/dialog_triggers.py       # every trigger, by conversation

| op | handler | table | actual | mnemonic | .TAG domain | uses |
|---|---|---|---|---|---|---|
| 0 | 0x401B00 | 0 |  | `dbg.dump_ctx` |  | 5 |
| 1 | 0x401B40 | 0 |  | `dbg.dump_code` |  | 1 |
| 2 | 0x000000 | 0 |  | `nop` |  | 3 |
| 3 | 0x401B90 | 0 |  | `end` |  | 5785 |
| 4 | 0x401C50 | 2 |  | `jmp` |  | 2904 |
| 5 | 0x401C90 | 2 |  | `jmp_if_true` |  |  |
| 6 | 0x401CE0 | 2 |  | `jmp_if_false` |  | 2542 |
| 7 | 0x401D30 | 1 |  | `push.i8` |  | 3083 |
| 8 | 0x401D70 | 2 |  | `push.i16` |  | 737 |
| 9 | 0x401DD0 | 4 |  | `push.i32` |  |  |
| 10 | 0x401E30 | 2 |  | `push.var` | VARIABLES | 3388 |
| 11 | 0x401EA0 | 0 |  | `drop` |  | 347 |
| 12 | 0x401EB0 | 2 |  | `set.var` | VARIABLES | 713 |
| 13 | 0x401F10 | 2 |  | `set.var2` | VARIABLES | 705 |
| 14 | 0x401F70 | 3 |  | `set.var.i8` | VARIABLES | 217 |
| 15 | 0x401FE0 | 4 |  | `set.var.i16` | VARIABLES | 9 |
| 16 | 0x402070 | 5 | **6** | `set.var.i32` | VARIABLES |  |
| 17 | 0x402110 | 2 | **4** | `set.var.var` |  | 150 |
| 18 | 0x402190 | 2 |  | `set.var.pop` | VARIABLES | 73 |
| 19 | 0x402210 | 2 |  | `var.add` | VARIABLES | 354 |
| 20 | 0x402290 | 2 |  | `var.sub` | VARIABLES | 403 |
| 21 | 0x402310 | 2 |  | `var.mul` | VARIABLES |  |
| 22 | 0x402390 | 2 |  | `var.div` | VARIABLES | 74 |
| 23 | 0x402410 | 2 |  | `var.and` | VARIABLES |  |
| 24 | 0x402490 | 2 |  | `var.or` | VARIABLES |  |
| 25 | 0x402510 | 0 |  | `cmp.eq` |  | 2417 |
| 26 | 0x402550 | 0 |  | `cmp.lt` |  | 132 |
| 27 | 0x402590 | 0 |  | `cmp.gt` |  | 129 |
| 28 | 0x4025D0 | 0 |  | `cmp.le` |  | 73 |
| 29 | 0x402610 | 0 |  | `cmp.ge` |  | 125 |
| 30 | 0x402650 | 0 |  | `cmp.ne` |  | 60 |
| 31 | 0x402690 | 0 |  | `add` |  | 12 |
| 32 | 0x4026D0 | 0 |  | `sub` |  | 73 |
| 33 | 0x402710 | 0 |  | `mul` |  |  |
| 34 | 0x402750 | 0 |  | `div` |  |  |
| 35 | 0x402790 | 0 |  | `bitand` |  |  |
| 36 | 0x4027D0 | 0 |  | `bitor` |  |  |
| 37 | 0x402810 | 0 |  | `and` |  | 36 |
| 38 | 0x402860 | 0 |  | `or` |  | 358 |
| 39 | 0x4028B0 | 0 |  | `neg` |  |  |
| 40 | 0x4028E0 | 0 |  | `not` |  |  |
| 41 | 0x402910 | 0 |  | `bitnot` |  |  |
| 42 | 0x402940 | 0 | **3** | `case` |  | 2155 |
| 43 | 0x4029A0 | 0 | **4** | `case.i16` |  |  |
| 44 | 0x402A30 | 0 | **6** | `case.i32` |  |  |
| 45 | 0x402AB0 | 2 |  | `area.preload` | AREAS | 12 |
| 46 | 0x402C30 | 4 |  | `scx.play.player.wait` |  | 95 |
| 47 | 0x402D20 | 6 |  | `area.goto` | AREAS | 756 |
| 48 | 0x402E10 | 2 |  | `area.arrive` | AREAS | 262 |
| 49 | 0x40A440 | 6 |  | `var.set.has_object` | OBJECTS | 222 |
| 50 | 0x40A4D0 | 4 |  | `inventory.add` | OBJECTS | 472 |
| 51 | 0x40A5A0 | 4 |  | `inventory.remove` | OBJECTS | 78 |
| 52 | 0x40A6A0 | 4 |  | `inventory.remove_all` | OBJECTS | 210 |
| 53 | 0x402E80 | 2 |  |  | CAMERAS | 3 |
| 54 | 0x402ED0 | 0 |  | `camera.follow_player` |  | 3 |
| 55 | 0x402EF0 | 2 |  |  |  |  |
| 56 | 0x402F60 | 2 |  | `player.become` | CHARACTERS | 43 |
| 57 | 0x4030E0 | 4 | **6** | `scx.play` |  | 451 |
| 58 | 0x4031E0 | 4 | **6** | `scx.play.wait` |  | 1697 |
| 59 | 0x403300 | 6 |  | `scx.play.actor` | CHARACTERS | 472 |
| 60 | 0x403430 | 6 |  | `scx.play.actor.wait` | CHARACTERS | 251 |
| 61 | 0x403560 | 2 |  | `dialog.start` | DIALOGS | 1246 |
| 62 | 0x4035D0 | 4 | **6** | `fight.begin` |  | 108 |
| 63 | 0x403730 | 2 |  | `player.move` |  | 312 |
| 64 | 0x403780 | 2 |  | `zone.enable` | ZONES | 1510 |
| 65 | 0x4037F0 | 2 |  | `zone.disable` | ZONES | 1969 |
| 66 | 0x40A910 | 2 |  | `object.hold` | OBJECTS | 11 |
| 67 | 0x40A9D0 | 4 |  | `object.hold.actor` | OBJECTS | 159 |
| 68 | 0x40AAF0 | 0 |  | `object.release` |  | 226 |
| 69 | 0x40AC20 | 2 |  | `object.release.actor` |  | 499 |
| 70 | 0x403860 | 6 |  | `ui.open` |  | 241 |
| 71 | 0x403950 | 4 |  | `scene.load` | SCENES | 78 |
| 72 | 0x403AF0 | 2 |  | `scene.unload` |  | 82 |
| 73 | 0x403C30 | 2 |  | `actor.goto_address` | ADDRESSES | 1192 |
| 74 | 0x403CA0 | 0 |  |  |  |  |
| 75 | 0x40AC90 | 2 |  | `var.set.used_object` |  | 235 |
| 76 | 0x40ACF0 | 2 |  | `object.show` | OBJECTS | 249 |
| 77 | 0x40ADD0 | 2 |  | `object.hide` | OBJECTS | 183 |
| 78 | 0x403CB0 | 2 | **4** | `character.show` | CHARACTERS | 1256 |
| 79 | 0x403DD0 | 2 |  | `character.hide` | CHARACTERS | 1980 |
| 80 | 0x403E80 | 0 | **2** | `shoot.begin` |  | 30 |
| 81 | 0x403F10 | 2 |  | `shoot.end` |  | 74 |
| 82 | 0x403FB0 | 2 |  | `shoot.actor.enter` | CHARACTERS | 317 |
| 83 | 0x404030 | 2 |  |  |  |  |
| 84 | 0x404090 | 4 | **6** | `shoot.actor.action` | CHARACTERS | 319 |
| 85 | 0x404170 | 4 |  |  |  |  |
| 86 | 0x404230 | 6 |  | `var.set.actor_stat` |  | 459 |
| 87 | 0x404330 | 2 |  | `address.enable` | ADDRESSES | 41 |
| 88 | 0x404390 | 2 |  | `address.disable` | ADDRESSES | 22 |
| 89 | 0x4043F0 | 2 |  | `player.move.wait` |  | 548 |
| 90 | 0x404450 | 4 |  | `scx.play.player` |  | 59 |
| 91 | 0x404530 | 2 |  | `var.set.player_id` |  | 63 |
| 92 | 0x404590 | 2 |  | `media.play` | OBJECTS | 2649 |
| 93 | 0x404790 | 6 |  | `actor.stat.set` | VARIABLES | 781 |
| 94 | 0x4048D0 | 2 |  | `image.show` |  | 4 |
| 95 | 0x404940 | 6 |  | `camera.set` | CAMERAS | 3754 |
| 96 | 0x404AF0 | 6 |  | `camera.set.wait` | CAMERAS | 1019 |
| 97 | 0x404CE0 | 2 |  |  |  |  |
| 98 | 0x404DB0 | 4 |  | `object.place_at` |  | 6 |
| 99 | 0x404EB0 | 2 |  |  |  |  |
| 100 | 0x404F60 | 4 |  |  |  |  |
| 101 | 0x404FA0 | 0 |  |  |  | 2 |
| 102 | 0x405090 | 0 |  |  |  |  |
| 103 | 0x404FB0 | 2 | **6** | `music.play` |  | 521 |
| 104 | 0x4050A0 | 0 |  | `player.anim.hold` |  | 647 |
| 105 | 0x4050C0 | 0 |  | `player.anim.release` |  | 618 |
| 106 | 0x405300 | 0 |  | `shoot.freeze_all` |  | 3 |
| 107 | 0x405310 | 0 |  | `shoot.unfreeze_all` |  | 3 |
| 108 | 0x405320 | 0 |  |  |  |  |
| 109 | 0x405330 | 0 |  |  |  |  |
| 110 | 0x405340 | 0 |  |  |  |  |
| 111 | 0x405350 | 0 |  | `timer.stop` |  | 13 |
| 112 | 0x405360 | 0 |  | `timer.start` |  | 15 |
| 113 | 0x405370 | 2 |  | `timer.set` |  | 12 |
| 114 | 0x4053D0 | 2 |  | `timer.mode` |  | 12 |
| 115 | 0x405420 | 2 |  | `var.set.timer` |  | 5 |
| 116 | 0x4050E0 | 0 |  | `shoot.player.suspend` |  | 108 |
| 117 | 0x405130 | 0 |  | `shoot.player.resume` |  | 104 |
| 118 | 0x405180 | 6 | **8** | `fade.to_color` |  | 184 |
| 119 | 0x405240 | 6 | **8** | `fade.from_color` |  | 256 |
| 120 | 0x405480 | 6 |  | `var.set.random` |  | 255 |
| 121 | 0x405540 | 2 |  |  |  |  |
| 122 | 0x405570 | 2 |  |  |  |  |
| 123 | 0x4055C0 | 2 |  | `set.hide_piece` |  | 32 |
| 124 | 0x405610 | 0 |  |  |  |  |
| 125 | 0x405620 | 0 |  |  |  |  |
| 126 | 0x405630 | 6 |  | `camera.set.at_address` | CAMERAS | 84 |
| 127 | 0x405800 | 0 |  | `player.pos.sync` |  | 14 |
| 128 | 0x405810 | 8 |  | `inventory.transfer` |  | 3 |
| 129 | 0x4059D0 | 0 |  | `walk.ledges.ignore` |  | 28 |
| 130 | 0x4059F0 | 0 |  | `walk.ledges.obey` |  | 29 |
| 131 | 0x405A10 | 4 |  | `music.volume` |  | 52 |
| 132 | 0x405A90 | 0 |  | `fade.to_black` |  | 504 |
| 133 | 0x405AB0 | 0 |  | `fade.from_black` |  | 505 |
| 134 | 0x405AD0 | 0 |  |  |  | 2 |
| 135 | 0x405AF0 | 0 |  |  |  | 3 |
| 136 | 0x405B10 | 4 |  | `camera.shake` |  | 32 |
| 137 | 0x405BA0 | 0 |  |  |  | 7 |
| 138 | 0x405BB0 | 2 |  | `character.look_at_player` | CHARACTERS | 251 |
| 139 | 0x405C30 | 2 |  | `character.look_away` | CHARACTERS | 264 |
| 140 | 0x405CB0 | 0 |  |  |  |  |
| 141 | 0x405CC0 | 0 |  |  |  |  |
| 142 | 0x405CD0 | 4 |  | `ui.highscore` |  | 5 |
| 143 | 0x405DD0 | 2 |  |  | OBJECTS |  |
| 144 | 0x405E30 | 2 | **6** | `morph.play` | CHARACTERS | 3 |
| 145 | 0x405EF0 | 2 |  |  |  |  |
| 146 | 0x405F00 | 2 | **0** | `ambience.on` |  | 11 |
| 147 | 0x405F20 | 2 | **0** | `ambience.off` |  | 9 |
| 148 | 0x405F40 | 2 | **0** | `inventory.save` |  | 14 |
| 149 | 0x405FC0 | 2 | **0** | `inventory.restore` |  | 15 |
| 150 | 0x406050 | 2 | **0** | `render.grey.on` |  | 14 |
| 151 | 0x406070 | 2 | **0** | `render.grey.off` |  | 15 |
| 152 | 0x406090 | 0 |  | `game.restart` | JINGOFF2.ADP | 3 |
## Open

* Opcodes 43–152 are named only by their domain. The four in real use worth
  chasing are 92 (builds `"%s.ADP"` and `"IMAGES\%s"` — plays a voice line and
  shows a picture), 93, 52 and 76.
* Opcode 77's handler needs extracting by hand.
* ~~The `fixups` indirection at `ctx+36` is never exercised by the shipped
  scripts~~ — **true of the dialogue corpus only, and the reading of it was
  wrong.** None of the 754 16-bit operands in `IAM\DIALOG` has bit 0x4000 set,
  which is what this was measured on; the **world** scripts carry 59, every one
  a `push.i16 0x4000` in a message handler, and the block is not a fixup table
  but the message's `{id, sender}` parameters. See "Operand encoding" above.

## The shoot-mode weapon table

**read from the binary** (`Shoot_InitWeapon`, 0x00421FB0). The held weapon
(actor `+164`) is classed by object property 3 (event 46) and looked up in a
compiled-in stats table — one for the player at `0x4C3658`, one for NPCs at
`0x4C36F8`, rows of `{class, ammo slot, f32 A, f32 B, damage}`, `-1`-class
terminated; a weapon whose name carries a `B` suffix maps to the special
`-2` row. The ammo slot indexes the `IAM\GLOBAL +32` ammunition slots the
docs already verify; the per-shot ammo count is queried through character
property 35.

| class | ammo | A (player/npc) | B | damage (player/npc) |
|---|---|---|---|---|
| 1 | 0 | 8.0 / 10.0 | 123.46 | 7 / 7 |
| 2 | 1 | 2.0 / 4.0 | 123.46 | 5 / 5 |
| 3 | 2 | 2.0 / 4.0 | 123.46 | 7 / 7 |
| 4 | 3 | 20.0 | 123.46 | 13 |
| 5 | 4 | 25.0 | 123.46 | 20 |
| 6 | 5 | 5.0 / 10.0 | 104.0 / 31.05 | 6 |

What A and B are physically (rate of fire and range are the candidates) is
not established — the table says only that the player's class-1/2/3 weapons
fire "slower" numbers than the NPCs' and that class 6 is the outlier on B.
Shoot-mode HP lives in the 192-byte shoot record at `+92`
(`Shoot_SyncHudHealth` mirrors it into the HUD).

## Opcode 47 `area.goto` — a transition that carries two scene objects

Decoded 2026-08-29, pinned by `verify.py: area.goto objects`.

Its 6 operand bytes are three int16 fields, and the handler (0x00402D20) hands
them straight to `Area_Transition` as `a5, a6, a7` with `a4 = 0`:

    area.goto  <destination area>  <objA>  <objB>

`Area_Transition` is a state machine over `a1[2]`. It stores `objA`/`objB` in
`a1[5]`/`a1[6]`, loads the destination into the *other* area slot
(`Area_LoadIntoSlot(1 - slot, dest)`), and starts each object with
`ScriptObject_Start` as it advances — `a1[5]` on entering state 5, `a1[6]` at
state 7, and `Script_ProcessActions` starts one more from its 60-second
watchdog if the transition stalls. Meanwhile it writes the **calling script's**
status word (`ctx+22`) to 9 or 10, which is how the transition suspends its
caller until the load finishes.

**Whose scene the ids name is a corpus result, not an assumption.** Resolved
against the *source* area's `.SCX` — the one being left — 416 of 448 land;
against the destination's, 84. So the pair animates the exit, in the scene the
player is still standing in, while the next area loads behind it. Two further
facts from the same sweep: the ids are **always both set or both -1**, never
one (0 of 756), and 247 of 448 are consecutive, which reads as authored pairs.

This matters beyond the opcode: a transition starting a scene object is a route
that is not `scx.play`, and CUTSCENES §5 had ruled out every such route on a
caller count that was wrong (see `verify.py: start-script graph`).

**It is not what starts Impasse's beats.** Exactly one `area.goto` targets area
222 and it carries `-1, -1`. That is a fact about area 222, not about the
mechanism.

`Actor_StartPendingScx` — the deferred start from `Actors_TickAll` — was the
next candidate to read, and **reading it excluded it** (2026-08-29): the actor
`+176` slot it starts from is written only by `Morph_Play`, which *parks* an
already-running scene object when a spoken line interrupts it. It is the resume
half of a suspend/resume pair, so it can never start a beat that was not
already going. CUTSCENES §5 has the trace; `verify.py: actor pending scx`.

**The real answer, found the same day, is none of these routes: it is the
SCENE chunk's own startup script at `+4`**, which `Area_TickLoad` runs on load
and which the 5785-slot inventory never enumerated. SCENE 55's fires all
sixteen of Impasse's beats in authored order. See
[FILE_FORMATS](FILE_FORMATS.md) "`AREA +4` / `SCENE +4`" and
[CUTSCENES](CUTSCENES.md) §5; `verify.py: startup scripts`, `impasse beats`.

## The area transition — `Area_Transition`'s state machine

**read from the code**, 2026-09-02: `Area_Transition` (0x00408530), the
handlers of 45 (0x402AB0), 47 (0x402D20) and 48 (0x402E10),
`Area_LoadIntoSlot` (0x00402B70), `Area_TickLoad` (0x0040C7E0),
`Script_Pump`'s tail and `Game_HandleEvent` case 3. The section above decodes
`area.goto`'s operands; this is what they drive.

### Two resident slots

The engine keeps **two** areas loaded. The table at `0x0069BC40` is two 16-byte
rows of `{AREA block, SCENE block, area id, scene id}` — the four arrays
`dword_69BC40/44/48/4C` index it with `4 * slot` — and `dword_69BC60` names the
active row, `dword_69BC64` the active area. Everything that walks the world
walks **both** rows: `Zones_RegisterAll` (0x00406560) registers four tables (an
AREA's and a SCENE's, in each slot), `Camera_FindWorld` searches both, and
`Message_RunHandlers` is given the slot to search. The outgoing area therefore
stays live — its zones armed and its contexts running — for as long as the
transition takes.

### The block, and the five modes

The state lives in eight dwords at `dword_6A0600`:

```
a1[0] the slot the transition started from      a1[4] the destination area
a1[1] the calling context's table index (+30)   a1[5] the departure object
a1[2] THE STATE                                 a1[6] the arrival object
a1[3] the outgoing AREA block                   a1[7] Sys_GetTimeMs, the watchdog
```

and `a4` selects the caller:

| mode | raised by |
|---|---|
| 0 | op 47 `area.goto` — `Area_Transition(block, ctx+31, ctx, 0, area, f1, f2)` |
| 1 | op 48 `area.arrive` — `Area_Transition(block, dword_69BC60, ctx, 1, area, -1, -1)`, i.e. the *active* slot rather than the caller's, and no objects |
| 2 | `Script_QueueAction` when action **3** (the zone's leave script) is queued on a context already at status 9 or 10 |
| 3 | `Script_Pump`'s tail, once `Area_TickLoad` reports the destination loaded |
| 4 | `Game_HandleEvent` case 3 — a scene object or a move finished |

### The walk

Mode 0 stores `f1`/`f2`, calls `Area_LoadIntoSlot(1 - slot, dest)` — the
destination loads into the **other** row while the player is still in the old
one — parks the caller at status **10**, and enters state **1 when `f1 != -1`,
else 3**. Then:

| from | on | does | to |
|---|---|---|---|
| 1 | mode 3 | `sub_419AF0(dest)` (make the destination decor the active scene), `ScriptObject_Start(f1, outgoing block, …)`, caller stays at 10 | 5 |
| 3 | mode 3 | `sub_419AF0(dest)`, and the caller **resumes** (status 1) | 8 |
| 5 | mode 4 | the departure object finished | 7 |
| 7 | mode 2 | `ScriptObject_Start(f2, …)`, caller at 10 | 9 |
| 5 | mode 2 | — | 6 |
| 6 | mode 4 | `ScriptObject_Start(f2, …)` | 9 |
| 9 | mode 4 | `sub_419A90` (link the decor in), clear the block, caller resumes | 0 |
| 3 | mode 1 | `area.arrive` while the load is still pending | 4 |
| 4 | mode 3 | link, clear, done | 0 |
| 8 | mode 1 | `sub_419A90(area)`, clear the block | 0 |

So `f1` and `f2` are two scene objects of the **outgoing** block — a departure
animation and an arrival animation — each awaited, played either side of the
switch, and `area.arrive` is the script's own way of saying "now". **448 of the
756 `area.goto` sites carry both**; 262 sites of `area.arrive` end a transition.

Two arms of mode 0 are refusals rather than steps: at state 1, and at state 8
with a departure object, the caller is put at status **9** and the handler
returns 0, on which op 47 rewinds its pc by 7 onto its own opcode.
`Script_ProcessActions` turns 9 back into 1 next frame and the instruction runs
again. Mode 2's status-9 arm does the opposite — it sets the status to 1 and
**adds 7 to the pc**, cancelling the rewind, so a queued leave script releases a
transition that was retrying.

### `area.preload` and the deferral

Op 45 loads an area into the other slot *without* transitioning: it parks its
caller at status **8** and sets `dword_4C0130` to that context's index. While
that global is not `-1`, `Script_Execute` refuses to dispatch **any** 45 or 47
— it rewinds the pc onto the opcode and, for 47, writes status 9 — so only one
staged load can be in flight at a time. The pump's tail clears it. Two arms of
the handler skip all of that: if the **active** slot already holds the area it
returns at once, and if the **other** slot does it calls `sub_419AF0` and
returns; either way the script is not parked and no load is staged.

### Returning to an area that is still resident runs **no** startup script

`Area_LoadIntoSlot(slot, area)` opens with

```c
if (dword_69BC48[4 * slot] == area)
    return sub_41D380(area, Area_Block(area) + 144);   /* the fog block, and nothing else */
```

— no `Area_Load`, so `dword_4E6D8C` (the staged-load state) stays 0,
`Area_TickLoad` returns 1 without entering its `while`, and the pump's tail
completes the transition on the spot. **Case 9 — the one that calls
`Script_NewContext` on the AREA block's `+4` and the SCENE block's, queues
action 1 for each, and runs `Zones_RegisterAll` — is never reached.** That is
every A → B → A return, and every `area.goto` into an area a prior
`area.preload` parked in the other slot: the area's startup script, its
cutscene beats, its `character.show`s and its `camera.set`s do **not** run
again. A replica that creates those contexts unconditionally replays them every
time the player walks back. (Ported 2026-09-02 — `todo/iam-script-engine.md`
17.)

### The load takes frames — as many as the SET has 128 KiB slices

`Area_Load` sets `dword_4E6D8C = 1` and calls `Area_TickLoad` once; cases
2–8 each return 0 unless `sub_41EFA0` (0x0041EFA0) passes. That function
is not a load-screen timer: it is the engine's asynchronous file reader —
its own error strings name it `Async_LoadDuringFrame` — reporting that the
queued read has been served (`Offset == 0 && served == queued`, then
`fclose`). `Music_SetFadeMode` (0x0041EFF0) is misnamed and is
**`Async_SetMode`**: 1 sets `ElementSize = 0x20000`, 2 sets `0x10000`, 0
reads synchronously. `Area_LoadIntoSlot` sets mode 1 before `Area_Load`
and case 2 sets mode 0 straight after the set, so **only the `.3DO` set
streams** — `o3de_LoadScene` queues its whole size — and `sub_41F320`,
called at the end of every frame after `I2D_Flush`, reads one slice. Once
the set is served, cases 2..9 (the `.SCX`, the map, the misc model,
`Actors_SpawnFromTables`, `Scene_LoadProps`, the `.ani`, the slider track,
the startup scripts and `Zones_RegisterAll`) run in **one** pump tail. So a
transition takes ceil(bytes / 131072) frames: Anekbah's 2099056-byte set
17, AImpasse's 33392-byte set 1, and the boot (`State_Apply`'s `Area_Load`
in mode 0) 0. The caller of a no-object `area.goto` resumes the frame after
that, and the outgoing area's contexts and zones run throughout. (Ported
2026-09-02, `verify.py: engine: area transition`.)

### Which row is ACTIVE is the player's feet, not the transition

`Area_TickLoad` case 9 sets `dword_69BC60`/`dword_69BC64` to the loaded
slot only around its two `Script_NewContext` calls and restores them. The
switch is `Game_HandleEvent` **case 9** — `dword_69BC64 = area; if the
other row is occupied: dword_69BC60 = 1 - dword_69BC60; Zones_RegisterAll;
play AREA +142 if it differs` — raised by `Walk_ProbeGround` (0x00467030)
and `sub_459AA0` when the decor under the actor changes to a slot in state
2. The transition machine only SHOWS (`sub_419AF0`, state 2,
`o3de_InsertScene`) and HIDES (`sub_419A90`, state 1) decors, and its
completion arms hide "the non-active row's area" — which is the area left
exactly because event 9 has already moved the row under the player.

**And when the script has no `area.arrive`, both decors stay in state 2 and
the player walks from one onto the other with both drawn** (2026-09-03).
The Impasse's airlock is the shipped case: AREA 222 zone 3801 is `area.goto
142 -1 -1; end`, the load lands in one slice (AIMPASAS is small), mode 3
case 3 shows the destination and resumes the caller into its `end`, and the
transition sits at state 8 with AIMPASSE and AIMPASAS both shown until AREA
142's record 10 - `area.arrive -1`, a zone deep in the airlock - hides the
non-active row. Its `-1` operand is one `Dbg_LogTagged` drops, which is why
`traces/impasse-walk.log` shows no AREAS between `142` and the later `222`.
The port models it as `ResidentSlot::shown` (`Session::slotShown`,
`shownCount`), separate from the active row; the feet's decor is
`decorUnder` (actor/walk.h), the nearest floor below the step window over
every shown soup, and it raises `playerOnArea` the way `Walk_ProbeGround`
raises event 9. The viewer draws every shown slot and keeps the player across
the change. `verify.py: engine: airlock walk` — shown to fail under the
one-set model (2 and 2 read 1 and 1).

### The objects are doors

441 of the 448 `area.goto` sites with objects are zone **enter** scripts;
AREA 0's zone 3 is `area.goto 201, 153, 240` and the objects are
`ported43open` and `ported43close`, a linked pair animating set piece
`Ported43`. The walk (`Area_Transition`): the destination streams into the
other slot while the player stands in the door zone; the tail shows its
decor and starts the OPEN on the outgoing scene (state 5); event 3 at its
end (7); the zone's LEAVE — `Script_QueueAction(ctx, 3)` when his feet
cross the quad — starts the CLOSE (9); its end resumes the script. A second
`area.goto X -1 -1` at state 8 — loaded and resumed, no `area.arrive` yet —
is accepted and does nothing (mode 0 case 8, `a6 == -1`).

### The watchdog

`Script_ProcessActions` opens with a 60-second guard on `a1[7]`: a context
stuck on the transition for a minute retries `ScriptObject_Start`, writes
status **11** and clears the whole block, so a load that never reports cannot
wedge the game.

## The 16 prompt slots, as a state machine

`Script_Pump` (0x00407DC0) and `Game_HandleEvent` case 7 (0x004067D0) share a
16-entry table (`unk_4E6B30`, 16 bytes: `+0` context, `+4` the zone record,
`+8` state, `+10` the zone id, `+12` the area slot). The pump reads it, the
actor scan writes it, and **the pump runs first**: `Script_Pump(1)` is
`Game_Tick`'s :2107 and `Actors_TickAll` its :2178, with no dialogue gate on
the actor tick.

| state | set by | what the pump does with it |
|---|---|---|
| 0 | the release, and the prune | nothing - the id at `+10` is -1 |
| 1 | case 7, taking a free slot | make the context, queue action 1 if the zone has an enter script, → 3 |
| 2 | case 7, from 3 | with the press: queue action 2, then → 5 if the zone id's bit 15 is set, else → 3. Without: → 3 |
| 3 | the pump, from 1 and 2 | still 3 means nothing re-armed it - queue actions 3 and 4, release the slot |
| 4 | case 7, from 5 | → 5 |
| 5 | the pump, from 2 (one-shot) and 4 | as 3 |

So an armed zone ping-pongs 3 → 2 → 3 for as long as the player faces into
it, and a **one-shot** zone ping-pongs 5 → 4 → 5 instead: the bit does not
free the zone, it latches the slot out of the press cycle, and the leave
happens on leaving like any other zone. Three of the transitions are
conditional and the conditions are not symmetric: the *enter* script is
queued only if the zone has one (both arms still write 3), the *leave* and
the *free* are queued unconditionally, and the press does nothing at all
unless the zone has an **activate** script - which is also why a one-shot
zone with no activate script is never spent.

An action press with no slot armed never reaches the pump: `Game_HandleEvent`
case 6 is `if (g_DialogState == 3 || a2 != 4 || !dword_4E6B24) return 0;`.

The slot's `+0` is the CONTEXT POINTER and it is the key: case 1 reuses the
context only when `+0` points at one whose zone id matches and whose FIFO
head is the free (`u8(ctx,24) == 4`), and the release (3/5) leaves `+0`
pointing at the freed context - the engine reads it again on the next arm.

### The unconsumed press, and a dry run that decides nothing

`Script_Pump` step 2 (0x00407DC0, after the slot loop):

    if (dword_4E6C90 && !dword_4E61E0) {
        if (Actor_HeldObjectSlot(Actor_Player()) == -1) { if (dword_4E66B8) post message 26 }
        else if (state != 3 && state != 15) { dword_91068C = -1; sub_41C770(); }
    }
    dword_4E6C90 = 0;

`dword_4E61E0` counts the slots in state 2 whose zone HAS an activate script
(`if (u32(ctx,4))`); `dword_4E66B8` is set to 1 at the top of the pump and
cleared when `Script_Run` - the DRY RUN of the activate script, evaluating
without writing - returns 1, i.e. hit an opcode that ORs 0x10 into ctx+40. But
its only reader in the image is the `if` above, inside `!dword_4E61E0`, and its
only clear (asm 11048) is adjacent to `++dword_4E61E0`. So when the reader runs
the flag is always 1: **message 26 - "nothing here" - posts exactly when a
prompt slot was taken (event 6 refuses the press otherwise) and no slot in
state 2 had an activate script**, and the dry run's result is unobservable.
The sender word (`evArgs[1]`) is never written: stack garbage.

Bit 0x10 is set by **24 handlers** - 46, 57, 58, 59, 61, 62, 63, 70, 73, 89,
90, 92, 94, 95, 96, 118, 119, 122, 123, 126, 129, 130, 138, 139 - each ORing it
in before its own dry-run test, and nothing clears it (`Script_NewContext`
zeroes the byte). It is a sticky "this context did something visible", and
`Script_Run`'s `while ((u8(ctx,40) & 0x10) == 0)` never loops again on a
context that has run anything. 91 (`var.set.player_id`) and 93
(`actor.stat.set`) do NOT set it.

In the Impasse - AREA 222's 3790, 3791, 3795, 3799, 3801 and SCENE 55's 3803 -
no zone has an activate script at all, so every press in the alley is a
message 26. Ported: `engine/src/script/area.cpp` `pumpZoneSlots`;
`verify.py: engine live zones`.

### What `end` does besides ending (opcode 3, 0x401B90)

    byte_4C012C = 0xFF;  status = 0;
    if ((ctx+40 & 8) && dword_69BC44[slot]) { Mem_Free(block); block = 0; }
    if (+32 == 1) { if (dword_4E6C7C) dword_4E6C7C = 0; }
    else if (+32 == 2) { --dword_4E6B20; <held-object tail>; +32 = 0; }

`byte_4C012C` is the table index of the running MESSAGE-0 handler
(`Message_RunHandlers` writes it for a message-0 record); `fade.to_color` /
`fade.from_color` compare it with their own context's +30 and force the colour
to 0xFF0000 when they match - a fade issued by the message-0 handler is red.
`dword_4E6C7C` is the BOOT startup context, stored by `Game_NewGame` after
`State_Apply` (the active slot's AREA block +0), cleared when ANY context ends
its enter action or when that context answers a screen with a non-zero value
(event 5); `sub_408410` loads a pending save (`dword_4C09B4`) only once it is
clear. Bit 8 is `scene.unload`'s: the block the caller is running out of is
freed here, not there. And `--dword_4E6B20` retires the activate the pump
counted when it queued action 2. Ported: `Session::execute`'s `End` arm.

### Where the scan runs, and where it does not

`Actor_ScanZones` is reached through `Actors_TickAll`'s ACTOR_STATE dispatch:
from `Actor_TickNpc` (1, 11..14), `Actor_TickDialogue` (16/17 - its last line
is `return Actor_ScanZones(a1)`), `Actor_TickUiHeld` (9) and `sub_466E70`
(10); not from `Actor_TickScxDriven` / `Actor_StartPendingScx` (4/5), the
fight tick (2) or the shoot tick (3/15). So zones are touched and armed DURING
a conversation (the pump, gated on `g_DialogState`, reads the slots once it
closes) and never while a `scx.play.player*` program owns the player. The
Session does the same (`playerDriven_`), and scans only once something feeds
the player's position - the teleport alone does not, which the intro shows the
need for: `actor.goto_address 654` lands inside zones 3799 and 3801 of the
Impasse, and 3801's enter script is `area.goto 142`.

### The arc is converted at load

`Actor_ScanZones` compares the actor's facing (`f32(actor, 420)`, degrees)
against `u16(zone, 48)` / `u16(zone, 50)` — the record's `+60`/`+62` **after**
`Area_Load` has multiplied both by `0.087890625` = 360/4096. The field on disk
is a 4096-per-turn angle (FILE_FORMATS §5b2b); the runtime one is whole
degrees. A zero width — authored, or small enough to truncate to zero —
accepts any facing.

## Messages, and the handler's parameter block

**read from the code.** `Game_HandleEvent` case **43** is "post a message", and
it runs `Message_RunHandlers(msg, areaSlot, sender)` (0x00409420). The engine
posts event 43 from 28 sites — the pump's "nothing here" message 26, the
inventory channel, the actor runtime, the UI, and the script timer's expiry
(message 18).

> **The site count is not settled, and is left disagreeing rather than
> averaged** (2026-09-02). This section says **28**;
> [FILE_FORMATS.md](FILE_FORMATS.md) §5b3 says **27**, which is what
> `grep -c 'Game_RaiseEvent(43'` over `readable/src/*.c` returns today. That
> grep is a **floor, not a count**: the decompilation is missing every function
> IDA never saw called (CLAUDE.md §1), so a raise site inside one of those is
> invisible to it, and the image has 32 `push 2Bh` of which no automatic pass
> here can say how many reach `Game_RaiseEvent`. Neither number is asserted by
> a check. Whoever settles it should count from the image at the call sites and
> fix both files in one edit.

The subscription tables are 8-byte `{script offset, int16 message}` records,
searched **scene, then area, then global** and first match wins: the resident
SCENE's at `+36` (count `+54`), the AREA's at `+68` (`+86`), `IAM\GLOBAL`'s at
`+8` (`+24`). The winner gets a fresh context from `Script_NewContext`, and an
8-byte parameter block at `ctx+36`:

```c
args = Mem_Alloc(8);  ctx[9] = args;
args[0] = msg;
args[1] = Actor_IdBySlot(sender);   /* messages 0..12 except 4 */
       or ObjectSlot_Id(sender);    /* messages 4, 20, 25      */
```

Message **25** is executed inline and the context freed on return; every other
message is queued as actions 1 then 4, so it runs on the next pump and frees
itself. Message **0** additionally stamps `byte_4C012C` with the context index,
which op 3 `end` resets.

**That block is the only thing an indirect operand can read**, and because the
fetch indexes it from its second word, the only parameter a script can reach is
the **sender** — see "Operand encoding". The 154 subscribed scripts are read by
`script.h` and printed by `tools/dump_world_data`; live dispatch was ported
2026-09-02 (`Session::postMessage`, `todo/iam-script-engine.md` 8).

## A note on site counts

Counts in this file are over the **5785 world-script slots** —
`IAM\AREA` + `IAM\SCENE` + `IAM\GLOBAL` — which is what `tools/vm_doc.py`
enumerates for the table's `uses` column and what every other count in `docs/`
uses. `todo/iam-script-engine.md`'s review counts `IAM\AREA` + `IAM\SCENE`
only, so its numbers are lower by exactly `IAM\GLOBAL`'s share: op 93 is
754 + 27 = **781**, op 86 431 + 28 = **459**, op 120 235 + 20 = **255**, op 89
545 + 3 = **548**, op 68 208 + 18 = **226**, and the indirect operands 22 + 37 =
**59**. The port's own sweep enumerates **5958** slots and reports higher
figures again (814 for op 93). None of the three is wrong; a site count is only
meaningful with its corpus named, and the three enumerations have not been
reconciled here.

# IAM script engine — open issues

Reviewed 2026-09-02 against `Script_Execute` (0x00406460), `Script_Pump`
(0x00407DC0), `Script_ProcessActions` (0x00408220), `Script_QueueAction`
(0x004063D0) and the opcode handlers (`clean/_vmhandlers.json`, keyed by
handler address; `asmfn.py` cannot anchor the label-less ones). Port side:
`engine/src/script/interp.cpp`, `area.cpp`, `world.cpp`.

Severity: **A** changes a decision a shipped script can make; **B** changes
timing or ordering within a frame; **C** latent — no shipped script reaches it.

## Fixed in batch 2 (2026-09-02)
3, 14, 15, 16, 18, 19, 21, 36 (the transition machine, T11); 22, 24, 29, 30, 33, 34, 37 (the world-side opcodes, T12 - the Session-side hooks are wave B's); 10 (the zone registry module, T13 - Session wiring wave B). Check: `engine: area transition`, `world ops`, `zone registry`.

## Fixed in batch 1 (2026-09-02)

**18 issues** — 1, 2, 4, 5, 6, 8, 9, 11, 12, 13, 17, 20, 25, 28, 31, 32, 35 and
39 — are fixed, checked and logged. Their sections have moved down to
[Fixed (batch 1, 2026-09-02)](#fixed-batch-1-2026-09-02), each carrying the
check that pins it and the `docs/RECONSTRUCTION.md` row that records it. The
text of each is kept exactly as it was raised, because *what the port had* is
the half a reader cannot reconstruct once the code is right.

Four are fixed only in part and stay in **Open**, each with a note saying which
half landed: **33** (op 93 renamed; the opcode is still unwired) and **29 / 30 /
37** (the `GameState` foundations exist — lists, prop state, timer — and no
opcode reaches them yet). **8** dispatches, but the engine's 28 posting sites
are still unwired; that is issue **7**.

Checks added: `engine: vm probe`, `engine: session rules`, `engine zone pump`,
`engine dialogue line states`, `engine game state`, `engine: cam mode 13`,
`engine: screen close` — 98 checks, 0 failed after integration.

## Open

### 40. `Actors_SpawnFromTables` is not ported: the world's characters never exist — A
Filed 2026-09-03 from a play report - *the characters are not showing in the
Impasse cutscene*. Issue 28 (fixed) made `character.show`/`hide` write the
`ObjectShown` bit; the CONSUMER of that bit was deferred to T11 by T2
("belongs with the transition machine", todo/pending/T2.md "Not done"),
T11 left it labelled, and it sat in the remainders list unnumbered.

**What the engine does** - `Actors_SpawnFromTables` (0x0040BB90,
`readable/src/01_file.c:4815`, NAMED, its layout **verified** in
FILE_FORMATS "the object table"), called once from `Area_TickLoad` case 5
(`01_file.c:5371`, after the set and the misc model, before the props at
6 and the startup scripts at 9), with `(area, scene, 1)`. For every 20-byte
placement record of the AREA table (`+40`, count int16 `+72`) and then the
SCENE table (`+8`, count `+40`): take a runtime slot from the 100-entry
`word_69BC80` (first free, -1 when full) and write it back to record `+0`;
`Actor_FindById(+2)` -> the 276-byte actor record, `+144` + ".3DO" ->
`Actor_LoadModel(slot, name, area)`; `Actor_SetPlacement(slot, {+4, +8,
+12, +16})` - coordinates already converted by `Area_Load` (`* 100/256/2.54
- 1`, the angle `* 360/4096`, the same conversion as ADDRESSES); then
`Actor_Attach(slot)` **only if** the DB `+20` bit at record `+18` is set -
so a character whose bit is clear is LOADED and PLACED but not linked into
the scene ("hidden is not unloaded", the same shape as the decors); and
`Actor_FindById(id) + 270 = -1` - the held-object field cleared. `Actor_Attach`
(0x0041CCA0) links the object to its parent in the o3de tree; `character.show`
(78) is that same call plus the bit, `character.hide` (79) `Actor_Detach` plus
the bit cleared.

**What the port does** - `Session` (`src/script/area.cpp`) keeps
`shown_` as an in-memory list that only opcodes 78/79 and `player.become`
append to; `completeLoad` runs the props (case 5/6) and the startup scripts
(case 9) and skips the spawn between them. So at any area load the world
holds nobody until a script shows someone.

**How established** - the loader (above) and the shipped data: for the
Impasse, AREA 222 places 212/218/219 (`PA1_FN`, bits 800..802 SET in START)
and 216 (`MCG_FN` on the `MECA` bank, bit 803 clear); SCENE 55 places 57
(`DE1_FN`, the Demon, bit 804 SET, at 7605 -80 2980 facing 358), 58
(`MCG_FN`, 805 clear) and 49 (`HO1_FN`, Kay'l, 806 clear - shown by the
intro's `character.show 310` / `player.become 49`). START sets 628 of the
1032 bits. So the port is missing FOUR attached characters in the alley at
load, and the beats `A_2_DemonLook` / `C_1_MecaComes` animate two of the
hidden-until-shown ones (57, 58) through `scx.play.actor`, which the viewer
cannot draw either (todo/omk-play.md 41).

**Severity A** - the runtime slots this table hands out are the ids later
handlers resolve (`Scene_FindObjectRecord` reads the `+0` written here), the
held-object clear is a decision a script reads back through `var.set.used_object`
(75), and what is on screen is the game.

**Fix shape (T19 in the plan)**: a reader for the 20-byte placement records
(`formats/placements.*`; `shownBitOf`'s ad-hoc scan becomes a use of it);
`ResidentSlot::characters` built in `completeLoad` between the props and the
startup scripts - {actor, runtime slot from a 100-entry table, model `+144`,
bank `+72`, pos, facing, bit, attached = bit at load}; 78/79/`player.become`
toggle `attached` and the bit instead of a separate list; `shown()` derived
from both slots' attached characters, carrying pos/facing/bank so a frontend
can stage them; evicted with the slot. Check `engine: spawn from tables`:
7 spawned / 4 attached for 222 + 55, the Demon's placement, `character.hide 57`
detaching and clearing bit 804, a save with 806 set attaching Kay'l at load,
628 of 1032 - SHOWN to fail with the spawn skipped (`shown()` empty at load).

### The labelled remainders
All 39 as originally filed landed in batch 1 or 2 (2026-09-02). Still
labelled inside the Fixed sections' notes and under "Queued for the next
pass": camera mode 14, the viewer's move and fight hooks, the held-object
paths, `Inventory_Insert`'s kind gate, op 96's `+0x1A` travel halving, list 3.
The viewer's one-body staging is todo/omk-play.md 41.

## Fixed (batch 2 wave B, 2026-09-02)
Pinned by `verify.py: engine: world ops`, `live zones`, `parking ops`, `zone registry`; RECONSTRUCTION rows of 2026-09-02 (T12, T13, T15, T16). Text intact, with the labelled remainders.

### 7. The unconsumed action press — B
`Script_Pump` step 2: when the action was pressed and no context ran
(`dword_4E6C90 && !dword_4E61E0`), the engine posts **message 26** through
`Game_HandleEvent(43, ...)` if nothing handled it (`dword_4E66B8`), or calls
`sub_41C770` when an object is held and the player is not in state 3/15. What
clears `dword_4E66B8` is `Script_Run`'s dry evaluation of the activate script
hitting an opcode that sets ctx+40 bit 0x10 (46, 57–59, 61–63, 70, 73,
89–96, 118, 119, 122, 123, 126, 129, 130, 138, 139 — the ones that DO
something visible). Neither `world.cpp` nor `area.cpp` models the press
outcome, so the "nothing here" message and the held-object path are absent.
**Landed (T15):** message 26 posts when a slot was taken and no state-2 slot had an activate script; the dry-run flag is dead code (see SCRIPT_VM). Held-object path labelled.

### 10. Zones run only in the harness, and for one chunk — B
`Zones_RegisterAll` (0x00406560) walks BOTH resident slots, and in each the
AREA's table (+48/+76) and the SCENE's over it (+16/+44) — four tables —
registering `record + 12` for every zone whose save bit is set, then prunes
every context whose zone id no longer resolves (its area was unloaded),
detaching it from the prompt slots. `World` (world.cpp) takes one chunk and
one kind; `Session` (area.cpp) — the thing `omk` and `omk-play` run — has no
zone model, so in the live replica no zone ever arms and every conversation
comes from a startup script. The pruning has no counterpart either.
**Landed (T13/T15):** the registry in `Session::frame`; the scan gated on an outside mover (a teleport alone must not scan - the intro shows why).

### 22. `var.set.used_object` (75) writes nothing — A
The handler (`0x40ACB0`) fetches its variable, reads
`Actor_HeldObjectSlot(Actor_Player())` and writes `word_4E6CA0[slot]` (the
held object's id) when the slot is in 0..49, **−1 otherwise**, then sets
ctx+40 bit 2. It is the opcode `Script_RunToOpcode75` stops at when the
player uses an inventory item on a zone (the pump's held-object branch):
the activate script then branches on the variable. The port records 75 as a
stub, so the variable keeps its previous value — not even the −1 the engine
writes with nothing held. 235 sites in 234 scripts.

### 23. `player.move.wait` (89) parks the script; the port runs on — A
The handler (`0x404410`) is `Player_GoToMove(field, slot)` then
`mov word ptr [ctx+16h], 4` — the same status-4 park the waiting
`scx.play*` variants use, released by event 3 when the move ends. **545
shipped sites**, the third most common parking opcode after 58 and 96. The
port records it as a stub, so "walk there, then the conversation" runs the
conversation on the frame the walk is ordered. (A scan of every handler for
a status-word write finds exactly ten: 3, 45, 46, 58, 60, 62, 70, 89, 96,
126. The port models 46, 58, 60, 70, 96; 45 is issue 3; 62 and 126 are
queued below.)
**Landed (T16 + coordinator):** the interpreter parks (`MoveWait`) and the Session arms it only when a move hook is installed; the viewer installs none yet, so today the opcode runs on - labelled, not a deadlock.

### 24. Four variable-writing opcodes are stubbed, so their variables go stale — A
A scan of every handler for a `Var_Set` call finds 12–24 (ported) and four
more: **86 `var.set.actor_stat`** (431 sites: `Actor_GetProperty(actor,
stat)` into the variable, the actor being the player when the field is −1),
**91 `var.set.player_id`** (63: `Actor_IdBySlot(Actor_Player())` — which
body the player is in), **115 `var.set.timer`** (5: `Timer_Elapsed`) and
**128 `inventory.transfer`** (3: writes the count moved). Each is followed by
a branch on the variable in its script. The port leaves the variable at its
previous value, and `engine: execute`'s "DB byte-identical to tools/sim" is
satisfied because the reference stubs the same four.

### 26. `fight.begin` (62) parks the script until the fight ends — A
The handler (`0x403640`) resolves the opponent (`sub_40D760(slot, id)`),
enters the player and the opponent into fight mode (`Actor_IdBySlot`,
`sub_40B2B0(.., 2)`, `sub_419CB0`, `Fight_Begin` = `sub_41A3B0`), writes
**status 3** into the context and requests camera mode **14** (the fight
camera) with `dword_930818 = max(field, 0)`. Release: `Game_HandleEvent`
case 2 — raised from the fight code at `16_o3de.c:3827` when it ends — walks
the context table and puts the FIRST context at status 3 back to 1. 108
shipped sites. The port records 62 as a stub, so "fight, then what happens
after" runs the after part at once.
**Landed (T16 + coordinator):** `FightWait`, armed only by a fight hook; camera mode 14 unmodelled.

### 27. `camera.set.at_address` (126) is a camera request and a hold — B
Like 96 but with the SUBJECT taken from an address: `Camera_FindWorld(cam)`
fills the request block, `Address_Find(field 1)` becomes both subject
pointers (`dword_930808`, `dword_93080C` — where 96 puts `Actor_Player()`
twice), the travel goes to `dword_930818`, the resume id
(`dword_930824`) is the context's slot and **status 7 is written
unconditionally** — no `test ebp, ebp` guard, so a 0-frame cut holds too,
until event 4 (the move ending) writes status 1. Then `Camera_Request(12)`.
84 shipped sites. The port stubs it: no camera and no hold.
**Landed (T16 + coordinator):** parks unconditionally, the address as the camera subject (`cameraSubjectAddress()`).

### 29. The inventory opcodes are stubbed: nothing a script gives or takes reaches the lists — A

> **Foundations landed in batch 1**: `GameState` carries the object lists with
> the engine's front insert and its per-list duplicate refusal
> (`verify.py: engine game state`; RECONSTRUCTION 2026-09-02, *The game state
> grew the three things a script edits*). The opcodes still do not reach them.

49 `var.set.has_object` (218 sites), 50 `inventory.add` (470), 51
`inventory.remove` (78) and 52 `inventory.remove_all` (209) read and write
the object lists at `word_69BD60` (capacity) / `word_69BD62` (count) /
`Src` (base): 49 scans list `field0` for object `field1` (T6 corrected the
order) and writes 1 or 0
into its variable; 50 appends (refusing a duplicate on lists 2 and 3, the
`cmp edi, 3 / cmp edi, 2` arm); 51 removes one id; 52 empties. Lists 0–2
live in the game DB at +848/+884/+1396 (GAME_STATE 3) — the port's
`GameState` knows the offsets and capacities and the inventory channel
edits them by hand — but `interp.cpp` records all four opcodes as stubs and
`area.cpp` has no arm for them, so a script's pick-up, memo, or "do you
carry X" never changes the state it then branches on (49, and 75 - issue
22). 975 sites between the four; the memo journal (list 2) is fed by
nothing else.
**Landed (T12/T15):** `Inventory_Insert`'s kind gate (seteks, ammunition) and list 3 stay unmodelled.

### 30. Prop state (the 2-bit array) is never written by the VM — A

> **Foundations landed in batch 1**: `GameState` carries the 2-bit prop state,
> sign extension included (`verify.py: engine game state`). The opcodes still
> do not write it.

68 `object.release` (208 sites) and 76 `object.show` (249) find the prop
record by id in the resident AREA's `+8` array (24-byte records, id at
`+2`, state index at `+16h`) and then the SCENE's (`+12`, count `+2A`),
and call `ObjectState_Get`/`ObjectState_Set` on the record's state index —
the 2-bit-per-prop array at DB `+16` the port names
`StateArray::PropState`. 67 `object.hold.actor` (159) and 98
`object.place_at` (6) edit the same records. `Scene_LoadProps` reads the
array on every area load, and it is saved. The port never writes it from a
script, so a prop a script showed, released or handed to an actor is back in
its shipped state after any reload.

### 33. Actor properties: written by 93, read by 86, neither ported — A

> **The naming half landed in batch 1**: op 93 is `actor.stat.set`, not
> `hud.show_var` (`verify.py: opcode table fresh`; RECONSTRUCTION 2026-09-02,
> *Two unused `case` variants, and an opcode named after its side effect*).
> What is still open is the WIRING — neither 93 nor 86 reaches an actor.

Op 93 — named `hud.show_var` in `tables/vm_opcodes.json`, but its handler
(`0x404840`) is `Var_Get(field 0)` then, for the player (field 1 == −1),
`sub_41CE50(playerIndex, ?, field 2, value)` with the player record's
`+0xAA`, and for any other actor `sub_40D6A0(slot, id)` →
`Actor_SetProperty`: it writes a VARIABLE into an actor's property `field 2`.
754 sites over AREA+SCENE (this file's corpus), 781 with GLOBAL (the docs'
convention), 814 with the DIALOG branch scripts too (T8's count). Op 86
`var.set.actor_stat` (issue 24) reads a property back into a variable: 431 /
459 / 460 over the same three corpora. The properties live in the actor records (the
`Carac` fields, FILE_FORMATS §2180ff) and, for the player, in the DB's
player record. The port stubs both ends, so health, money and every other
stat a script moves through a variable stays where the actor table shipped
it. The table name for 93 is wrong and should be corrected with the port.
**Landed (T12/T15):** both ends; the player's through the DB, other actors through `WorldHooks` on the resident blocks.

### 34. `object.release.actor` (69) — the actor drops what it holds — B
The handler (`0x40AC40`): `sub_40D760(slot, id)` → actor index,
`Actor_HeldObjectSlot` (bail on −1), `Actor_SetState(actor, 0)`
(`sub_41A140`), and `Actor_FindById(id)->+0x10E = -1` — the record's held
object cleared. 499 shipped sites, the most common of the object family
after 50. Its twin 68 `object.release` (issue 30) does the same for the
player plus the prop state. The port records both as stubs; nothing in the
session models an actor holding an object, so `var.set.used_object`,
`Script_RunToOpcode75` and this all see the same nothing.

### 37. The script timer (111–115) does not exist in the port — B

> **Foundations landed in batch 1**: `GameState` carries the clock and the
> timer state, and the table's 111/112 names were found INVERTED on the way
> (`verify.py: engine game state`). The opcodes still do not reach them.

`timer.start` (111, 13 sites) records `g_ClockTime` into `g_TimerStart` and
sets `g_TimerFlags`; `timer.stop` (112, 15) clears it; `timer.set` (113,
12) writes `g_TimerValue = field * 1000` (the `lea` triple is ×125 then ×8)
under `Timer_SetValue`, which refuses unless the timer runs; `timer.mode`
(114, 12) `Timer_SetMode(field | 1)`; and `var.set.timer` (115, 5, issue 24)
reads `Timer_Elapsed`: `g_TimerValue` when flag 0x10 is set, 0 when the
flags are exactly 1, else `g_ClockTime - g_TimerStart`. 57 sites, all
stubbed; the port has no timer state and `g_ClockTime` (the game clock
the port keeps in `GameState`) is never read by a script.

### 38. `end` (3) does four things; the port does one — C
The handler (`0x401B90`): `byte_4C012C = 0xFF` (the "message-0 handler is
running" marker `Message_RunHandlers` sets); status 0; **if ctx+40 bit 8 is
set** — `scene.unload` set it when the caller unloaded its own slot (issue
25) — free that slot's SCENE block now (`sub_412060(dword_69BC44[slot])`,
pointer cleared), the deferred half of the unload; then on the current
action: 1 (ENTER) clears `dword_4E6C7C` (the context `ui.open` of screen 29
parked, so the start-menu answer path forgets it), 2 (ACTIVATE) is the
`--dword_4E6B20` / held-object tail also duplicated in `Script_Execute`
(issue 7); and `ctx+32 = 0`. `interp.cpp` returns `RunStatus::End`. Nothing
reachable in the port depends on the three extras today; they matter once
25 and 7 are done.

## Fixed (batch 2, 2026-09-02)
Pinned by `verify.py: engine: area transition`; RECONSTRUCTION row "The area transition is a STATE MACHINE with two resident slots, and the port now runs it". Kept here with their text intact.

### 3. `area.preload` (45) and the transition deferral — A
The 45 handler (`0x402AB0`) loads AREAS[op] into the other resident slot,
writes status **8** into its own context (a park) and sets `dword_4C0130` to
its slot. While that global is not −1, `Script_Execute` refuses ANY 45 or 47
before dispatch: `pc` is rewound onto the opcode and 47 gets status **9**,
which `Script_ProcessActions` turns back to 1 next frame (a retry). The port
records 45 as a stub, never parks on it, and executes 47 unconditionally.
`area.cpp`'s 47 comment ("the caller keeps running this frame") also disagrees
with the handler's own refusal path, which rewinds 7 bytes onto itself.

### 14. `area.goto`'s two object fields, and the transition they drive — A
`area.goto` has THREE operands. The handler (`0x402D20`) hands
`Area_Transition(&dword_6A0600, slot, ctx, 0, area, f1, f2)`, and mode 0
stores `f1` at `a1[5]` and `f2` at `a1[6]`, enters state **1 when f1 != −1,
else 3**, calls `Area_LoadIntoSlot(1 - slot, area)` and parks the caller at
status 10. The rest is a state machine driven from three places: mode 3 from
`Area_TickLoad` when the destination has loaded (state 1: switch the active
area, `ScriptObject_Start(f1, oldBlock, slot, 1)`, state 5; state 3: switch
and RESUME the caller), mode 4 from event 3 when that object finishes (5 →
7), mode 2 from the LEAVE action queued at status 9/10 (7: start `f2` the
same way, state 9), and mode 4 again at state 9 (`sub_419A90`, resume). So
`f1` and `f2` are two scene objects of the OUTGOING block — the departure and
arrival animations — played in sequence around the switch, each awaited.
**448 of the 756 shipped sites carry both** (308 carry −1/−1). The port reads
field 0 only, so every transition is a cut: no departure object, no arrival
object, no wait, and the outgoing scene's programs are dropped before an
object that should run in them can start.

### 15. One resident area, loaded within the frame — B
The engine keeps TWO resident slots (`dword_69BC48`, 16 bytes each) and
`Area_LoadIntoSlot(1 - slot, area)` loads the destination into the OTHER
one while the outgoing area stays live: its zones remain registered
(`Zones_RegisterAll` walks both slots), its contexts keep running, and only
`Area_TickLoad` reporting the load done (mode 3 above) switches the active
slot; `dword_4C0130` holds every further 45/47 meanwhile (issue 3), and
`Zones_RegisterAll` then prunes the contexts whose zone id no longer
resolves. `Session` has one `curArea_`, `finishAreaTransition` completes the
load at the END of the requesting frame, clears `waitingForArea` on EVERY
context, and never frees the outgoing area's contexts. The order of the
incoming startup scripts against the caller is right (the caller resumes
first); the frames the load takes, and what runs in the old area during
them, are not modelled.
**Correction (T11, 2026-09-02):** the ACTIVE row is switched by event 9 from `Walk_ProbeGround` (the decor under the player), not by `Area_TickLoad`; the transition only shows and hides decors.

### 16. Context slots are reused, and the pump runs them in slot order — B
`Script_NewContext` takes the first FREE entry of the 32-slot table
(`while (*v6) ++v6`), `Script_FreeContext` / `Script_ProcessActions` action 4
/ `Zones_RegisterAll`'s prune empty entries, and `Script_Pump` runs the
table in index order every frame. A context created later can therefore run
EARLIER in the frame than one created before it, whenever it lands in a
freed lower slot — which happens as soon as one zone context has been freed.
`Session::ctxs_` is an append-only vector (nothing erases it) run in
creation order, so intra-frame ordering between scripts diverges once any
context has ended. A 33rd live context is silently not registered in the
engine (`Script_NewContext` returns it unlisted, so it never runs); the port
has no cap.

### 18. `camera.set.wait` on a camera that does not resolve holds in the port and not in the engine — B
The 96 handler calls `Camera_FindWorld` first and, on 0, skips everything
(`jz loc_404CCB`): no request and **no status 7**, so the script runs on. The
port's interpreter returns `CameraWait` from the operands alone and
`Session::frame` sets `waitingForCamera = travel` before `applyCamera`
decides the id is unknown. 64 of the 1018 `camera.set.wait` sites (and 87 of
3659 `camera.set`) name a camera that is neither in their own chunk nor in
GLOBAL; `Camera_FindWorld` also searches the OTHER resident slot, so some of
those resolve in the engine when the previous area is still loaded — which
the port cannot reproduce either (issue 15).

### 19. The staged load takes frames; the port's boot and transitions take none — B
`Area_Load` sets `dword_4E6D8C = 1` and `Area_TickLoad` advances one case a
call, each of cases 2–8 gated on `sub_41EFA0()` (the load screen's own
pacing): set, `.SCX`, map, misc model, actors, props, `.ani`, slider track,
then case 9 queues the startup scripts and registers the zones. So an area's
first script instruction is at least nine pump frames after the load began,
during which the outgoing area's contexts and zones keep running (issue
15). `Session::loadArea` and `finishAreaTransition` queue the startup
contexts in the same frame. The golden traces are order-only, which is why
this has never shown.
**Correction (T11, 2026-09-02):** not "nine pump frames" - `sub_41EFA0` is the async file reader and only the `.3DO` set streams, ceil(bytes / 0x20000) frames: Anekbah 17, AImpasse 1, the boot 0.

### 21. No restart path — B
`Script_Pump` phase 1 opens with `if (g_RestartRequest) { Script_Pump(3);
Script_Pump(2); Screen_FadeFromColor(0xFFFFFF, 15, 0); }`. Phase 3 is
`sub_40E260`: stop the music (`Music_PlayTrack(0, 1)`), drop the held object
and its scene record, close a conversation if one is up (`Dialog_Unload`,
`Dialog_ClearSubjectActor`, `g_DialogState = 1`), reset the weapon slot and
the transition block; phase 2 is `Game_NewGame`: clear the object slots and
prompt slots, free every context (`sub_406270`), reset both resident slots,
reload `IAM\START` over a zeroed DB, `State_Apply`, day 52 and time
2000000. The menu's "new game" is this. `Session` has no reset: a second
game in one process keeps the first's contexts, DB and resident scene.

### 36. `area.arrive` (48) — the script's own step in a transition — A
The handler (`0x402E40`) is `Area_Transition(&dword_6A0600, curSlot, ctx,
**1**, area, -1, -1)`: mode 1 moves the state machine of issue 14 from 3 to
4 (the destination has loaded, now switch) or, at state 8, calls
`sub_419A90(area)` — link the destination's decor slot into the scene
(`sub_441200`) and mark it active (`+110 = 1`) — and clears the block. 262
shipped sites, i.e. most transitions with objects (issue 14) end on one.
The port records it as a stub; its transition has no states for it to
advance, and the visible set switches when `finishAreaTransition` decides,
not when the script says.

## Fixed (batch 1, 2026-09-02)

Kept in full. Each section is the issue **as it was raised** — what the engine
does, what the port did and how it was established — with the check and the log
row added on top. Nothing below is a queue item any more.

### 1. Division truncates in the engine, floors in the port — A

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine: vm probe`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *The VM's division, its shared fetch and `var.set.random` corrected — three handler rules the 5958-slot sweep could not see, and one word the corpus had been reading off by one.*

`interp.cpp` `divFloor` (ops 22 and 34) is Python's floor division, after
`tools/sim`. The engine's op 34 handler (`0x402750`) is `cdq; idiv edi`:
truncation toward zero. −7/2 is −3 in the game, −4 in the port. A zero divisor
faults in the engine and yields 0 in the port (a crash, not a decision).
Fix: `a / b` with C++ truncation; keep the zero guard, labelled.

### 2. `dialog.start` ends the frame for every other context — B

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine: session rules`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *Eight of the Session's scheduling rules were the port's, not the engine's - each fixed against its handler and SHOWN wrong by one probe line.*

`area.cpp` `Session::frame`: the `RunStatus::Dialog` arm sets
`dialogState_ = 3` and `return`s, so later contexts do not execute that frame
and `finishAreaTransition` / `tickCamera` are skipped. In the engine only
`Script_Execute` returns for that ONE context (`if (v4 == 61) return;`);
`Script_Pump`'s loop continues to the next slot, `Script_Execute` has no
dialogue gate (only `Script_ProcessActions` tests `g_DialogState == 3`), and
`Game_Tick` still ticks the camera. Visible when the AREA and SCENE startup
scripts are live in the same frame.

### 4. Indirect operands skipped on ops 14–17 and 71 — C

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine: vm probe`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *The VM's division, its shared fetch and `var.set.random` corrected — three handler rules the 5958-slot sweep could not see, and one word the corpus had been reading off by one.*

Their handlers (`0x401F70`, `0x402110`, `0x403950`) all run the shared
fetch (0xFFFF passes through; bit 0x4000 means `params[v & 0x3FFF]`). The
port reads these fields with `i16at(raw, k)`, no indirection. `interp.h`'s
note that only 4, 6, 10, 18 and 42 do the fetch is wrong: 5, 12–24, 45, 47,
61, 64/65, 71 do too. Measured 2026-09-02: 0 of the 199 + 9 + 0 + 138 + 77
shipped sites of 14/15/16/17/71 carry the bit, so this is latent.

### 5. Activate-queue dedupe and the one-shot zone — B  *(corrected by T4, see below)*

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine zone pump`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *The zone pump refuses a repeat activate, and resumes a parked one. — and for the one-shot half, *The one-shot zone LATCHES, it does not free early: a correction to what was being ported.**

`Script_QueueAction(ctx, 2)` refuses a second activate while one is queued
or current (`if (a2 == i8(i + a1, 24) && a2 == 2) return 0`, and the
`u32(a1, 32) == 2` test); `world.cpp` `World::step` queues `ActActivate` on
every frame the action bit is held, up to the FIFO's four. And after queueing
the activate, `Script_Pump` state 2 tests `byte [slot+0Bh] & 0x80` — the high
bit of the u16 zone id at slot +10, i.e. **bit 15 of the ZONES id** (the flag
FILE_FORMATS 5b2b names) — and puts the slot in state 5, which the next pump
frees (leave + free) whether or not the player is still inside. A one-shot
zone. `world.cpp` masks the bit away for registration and never reads it, so
such a zone stays armed and can activate again on the next press.
**Correction (T4, from the assembly):** the one-shot bit does NOT free the
zone while the player still stands in it - `Game_HandleEvent` case 7 maps
slot state 5 back to 4 on every frame `Actor_ScanZones` finds him armed, pump
case 4 maps 4 back to 5, and `Script_Pump(1)` runs before `Actors_TickAll`,
so the two ping-pong until he leaves. The bit LATCHES the slot out of the
press cycle; the free happens on leaving. And `Script_Execute`'s LABEL_8 (and
`end`'s handler) clear ctx+32 when an activate finishes, so the dedupe means
"no second activate while the first is unfinished", not "one ever".

### 6. `scene.load` (71) on the RESIDENT area swaps the scene at once — A

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine: session rules`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *Eight of the Session's scheduling rules were the port's, not the engine's - each fixed against its handler and SHOWN wrong by one probe line.*

The handler (`0x403950`) walks the two resident slots (`dword_69BC48`, 16
bytes each) and, where the slot's area equals the operand, frees the old
SCENE block's context (`dword_4E61E8[ctx+0x1E] = 0`, three `Mem_Free`s),
unloads the old scene (`sub_40BEC0`, `sub_40A200`), loads the new one
(`sub_40C120(scene, slot)`, `sub_40BB90`, `sub_409FC0`), then
**`Script_NewContext(slot, block[+4], 0, 0)` and `Script_QueueAction(ctx, 1)`**
— the new scene's startup script is created and queued in the same frame —
and finally `sub_40B120(area, scene)` (the save field) and
`Zones_RegisterAll`. `interp.cpp` op 71 does only the save field
(`setSceneOfArea`) and `area.cpp` has no 71 arm, so a `scene.load` aimed at
the area the player is standing in changes nothing until that area is
reloaded: no new startup script, no zone re-registration, the old scene's
context still running. For a non-resident area the two agree.

### 8. Message-subscription handlers are read but never dispatched — A

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine: session rules`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *Eight of the Session's scheduling rules were the port's, not the engine's - each fixed against its handler and SHOWN wrong by one probe line.*

`Message_RunHandlers` (0x00409420) is what `Game_HandleEvent` case 43 runs:
it searches the resident SCENE's table (+36, count +54), then the AREA's
(+68, +86), then `IAM\GLOBAL`'s (+8, +24), first match wins, and gives the
handler a fresh context whose parameter block is `{message, sender}` (the
sender resolved to an actor id for messages 0–12 except 4, to an object id
for 4/20/25); message 25 runs inline, every other one is queued as actions
1 then 4. Event 43 is posted from **28 sites** in the engine (the pump's
"nothing here" 26, the inventory channel, the actor runtime, the UI).
`script.h` reads the tables and `tools/dump_world_data` prints them, but no
runtime module posts a message or runs a handler: the 154 subscribed scripts
execute only in the corpus sweep. `Interpreter::setParams` exists for the
`{message, sender}` block and has no caller.

### 9. A zone TOUCH requests its camera — B

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine zone pump`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *A zone TOUCH asks for a camera, and the harness now records it.*

`Actor_ScanZones` (0x004678xx) raises event **8** for every registered zone
the player's point is inside, before the facing test, and `Game_HandleEvent`
case 8 reads the zone's `+54` (record `+66`, the port's `Zone::camera`): when
it is not −1, `Camera_FindWorld` fills the request block at `dword_4E6C40`
and `dword_930800` points the camera at it. 54 of the 4558 shipped zones
carry one (40 distinct cameras - T4's count; 41 was wrong). `world.cpp` decodes the field and never uses
it; `area.cpp` has no zone runtime at all.

### 11. A screen closed WITHOUT an answer still resumes the script — B

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine: session rules` (the script half) and `verify.py: engine: screen close` (the viewer half);
> `docs/RECONSTRUCTION.md` 2026-09-02 — *Eight of the Session's scheduling rules were the port's, not the engine's — and *Leaving a screen is an answer, in the viewer too.**

`UI_OpenScreen` parks the caller's slot in `dword_930744` and sets the answer
`dword_930750` to −1; the widget overwrites it when a slot is chosen. Every
close path — `UI_SendAnswer`, and the two ESC/TAB closes in `21_d3d.c`
(0x4034xx, 0x4035xx) — posts event 5 with whatever `dword_930750` holds, and
`Game_HandleEvent` case 5 does `Var_Set(var, answer)` when a variable was
named and writes status 1 unconditionally. So leaving a screen resumes the
script with the variable at **−1**, which is the branch AREA 118's startup
script takes as "the unseeded opening". The port has two other answers:
`Session::frame` marks the context `done` when `answerScreen` has nothing
(`else c->done = true;  // unanswerable`), and `omk-play` `break`s out of its
frame loop when `walk->closed()`. Operand order is right (screen, parameter →
`dword_4C0B64`, variable → `dword_4E6B28`), checked against the handler.

### 12. The object's camera EDITING (camera mode 13) is never driven — A

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine: cam mode 13`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *Camera mode 13 — the editing an object start hands the camera to — is DRIVEN.*

All five `scx.play*` handlers (46, 57, 58, 59, 60) end the same way: after
`ScriptObject_Start`, `if (ScriptObject_HasCamEditing(obj, slot))` fills the
request block (`dword_930818 = max(field, 0)`, `dword_93081C = 1`,
`dword_930828 = -1`) and calls `Camera_Request(13, &dword_930800)` — the
chunk-10 editing linked to the object takes the camera (docs/SCRIPT_VM.md
449, CUTSCENES). 1668 of the 1697 `scx.play.wait` sites and 445 of 451
`scx.play` sites carry a non-negative field there. The port parses editings
(`o3de/camedit.cpp`) but the only user is `tools/dump_camedit`: neither
`Session` (`applyCamera` knows modes 12 cuts and travels only) nor
`SceneRunner` nor the viewer requests one, so a cutscene keeps whatever
`camera.set` last chose while the game flies the editing.

### 13. The zone harness cannot resume a parked script — B

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine zone pump`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *The zone pump refuses a repeat activate, and resumes a parked one.*

`World::pump` (world.cpp) runs each armed action with a fresh `Interpreter`
and `vm.run()`, sets `c.status = 0` BEFORE the run and records whatever
`RunStatus` comes back as the script's end. In the engine the context keeps
its status word — 4 after a waiting `scx.play*`, 6 after `ui.open`, 7 after a
`camera.set.wait` with a move, 1 after `dialog.start` — and the pump resumes
it on a later frame from the saved pc and stack. So an activate script that
parks before its `dialog.start` never reaches it in the harness, and `World`
is the only zone runner the port has (issue 10). `Session` resumes correctly
but has no zones.

### 17. Returning to the area still resident in the other slot runs NO startup script — A

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine: session rules`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *Eight of the Session's scheduling rules were the port's, not the engine's - each fixed against its handler and SHOWN wrong by one probe line.*

`Area_LoadIntoSlot(slot, area)` (0x00402B70) has two branches. If the slot
already holds `area` it only refreshes the decor's fog block (`sub_41D380`)
and returns — no `Area_Load`, so `dword_4E6D8C` (the staged-load state) stays
0. `Area_TickLoad` then returns 1 at once without ever reaching case 9, and
`Script_Pump`'s tail (`if (!Area_TickLoad(1 - cur) || dword_4C0130 == -1)`)
completes the transition on the spot: the caller resumes, and the AREA and
SCENE startup contexts are NOT created, `Zones_RegisterAll` is NOT run. That
is every A → B → A return, and every `area.goto` into an area a prior
`area.preload` (45) parked in the other slot. `finishAreaTransition` creates
the startup contexts unconditionally, so the port replays an area's startup
script — its cutscene beats, its `character.show`s, its `camera.set`s —
every time the player comes back to it.

### 20. `push.i16` (8) skips the indirect fetch — and every shipped use of it is indirect — A

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine: vm probe`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *The VM's division, its shared fetch and `var.set.random` corrected — three handler rules the 5958-slot sweep could not see, and one word the corpus had been reading off by one.*

The op 8 handler (`0x401D70`) runs the shared fetch: 0xFFFF passes through,
bit 0x4000 means `params[v & 0x3FFF]`. `interp.cpp` pushes `imm(raw)`. The
world corpus carries exactly **22** operands with the bit across every
fetching opcode, and all 22 are `push.i16 0x4000` in MESSAGE-handler
scripts, where `Message_RunHandlers` fills the parameter block as `{message
id, sender}` — and the fetch reads the block from its SECOND word (`movsx
eax, [block + eax*2 + 2]`, T1/T10), so the value pushed is the **sender**,
not the message id: every shipped use is `push <actor-or-object id>;
push.i16 0x4000; eq`. So the engine pushes the sender and the port pushed
the constant 16384. (Issue 8 is why the handlers never run live;
this is why they would branch wrongly when they do, and why the corpus sweep
cannot see it - `tools/sim` decodes the same constant.)

### 25. `scene.unload` (72) — the other half of issue 6 — A

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine: session rules`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *Eight of the Session's scheduling rules were the port's, not the engine's - each fixed against its handler and SHOWN wrong by one probe line.*

The handler (`0x403B30`) walks the resident slots for the operand's area
and, in each: frees the SCENE block's context (`dword_4E61E8[ctx+0x1E] = 0`
and three `Mem_Free`s), clears the block's context pointer, unloads the scene
(`sub_40BEC0`, `sub_40A200`), writes −1 into the slot's scene id
(`dword_69BC4C[slot]`), sets ctx+40 bit 8 when the unloaded slot is the
caller's own, then `Area_SetLoadedScene(area, -1)` and `Zones_RegisterAll`.
82 shipped sites. The port records it as a stub: the DB still names the
scene, so the next load of that area brings it back, and the scene's
startup context keeps running.

### 28. `character.show`/`hide` (78/79) write a SAVE bit the port never sets — B

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine: session rules`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *Eight of the Session's scheduling rules were the port's, not the engine's - each fixed against its handler and SHOWN wrong by one probe line.*

Both handlers resolve the actor (`sub_40D6A0(slot, id)`, giving up on
0xFFFF) and call `State_SetBit(actorIndex, 1 or 0)` on the bitmap at DB
`+20` — the array the port names `StateArray::ObjectShown` and has never
written — then place the model (`sub_41BDF0`) or remove it. The bitmap is
what `Actors_SpawnFromTables` reads on every area load (`01_file.c:4892`,
`4945`), and it travels in the save. `Session` keeps an in-memory `shown_`
list instead, so a character shown by script is absent after a reload or a
load-from-save, and a script that hid one sees it back. 1256 + 1980 sites.

### 31. Opcodes 43 and 44 are `case` variants the table gives no length — C

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: opcode table fresh`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *Two unused `case` variants, and an opcode named after its side effect.*

`0x4029A0` and `0x402A30` fetch a 16-bit target with the shared fetch, peek
the stack top and compare it with a label read after the operand — 16-bit
for 43, 32-bit for 44 (`add edi, 4`) — exactly as 42 does with an 8-bit
label. `tables/vm_opcodes.json` records both as length 0 and unnamed, so a
script containing one would desynchronise the decoder from the label byte
on. No shipped world or dialogue script uses either (0 sites), which is why
the 5785/5785 decode never noticed.

### 32. `player.become` (56) is stubbed: the player never changes body — A

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine: session rules`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *Eight of the Session's scheduling rules were the port's, not the engine's - each fixed against its handler and SHOWN wrong by one probe line.*

The handler (`0x402FA0`): if the operand is not already the player's actor
id, it finds the actor (`sub_40D6A0(slot, id)`), takes the current player's
position and facing (`Actor_GetPosAndFacing`), writes the old id into the
new record's `+2`, calls **`Player_SetActor(-1)`** then places and shows the
new actor at that position (`sub_41BDF0`, `sub_41CCA0`), flags it
(`sub_40D590(actor, 0)`, `sub_41DDB0`), and copies the new character's two
BIO strings (`Actor_FindById(id)` → `[+0]`, `[+4]`) into the game DB's bio
slots through `dword_69BC6C` (`rep movsd` — the +336/+592 strings
`GameState::kBio` names). From then on `Actor_Player()`, `Actor_ScanZones`,
`var.set.player_id`, the dialogue subject and every player-relative camera
follow the new body. 43 shipped sites — the soul-transfer mechanic. The port
has no arm for it: the player is Kay'l for ever, and the bio never changes.
(It is also the opcode `Dbg_LogTagged` filters from the trace, so no capture
can show it.)

### 35. `var.set.random` (120) is stubbed — every "random" branch is stale — A

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine: vm probe`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *The VM's division, its shared fetch and `var.set.random` corrected — three handler rules the 5958-slot sweep could not see, and one word the corpus had been reading off by one.*

The real handler is at `0x405480` (the pre-split block for 120 is the
function after it, CLAUDE.md 1's trap; `asmfn.py --op 120` shows it):
three operands through the shared fetch — `lo`, `hi`, then the VARIABLE —
and `Var_Set(var, Random_NoRepeat(lo, hi))`, where `Random_NoRepeat`
(0x0041D6B0) redraws until the result differs from the last one it gave.
235 shipped sites, and the scripts branch on the variable with `case`
right after — passers-by lines, ambient variations. The port records 120
as a stub, so the variable keeps its previous value and the branch always
takes the same arm. The setter scan in issue 24 missed this one because
the wrong block carries no `Var_Set`.

### 39. Two line assets end differently, and the port treats every line alike — B

> **Fixed in batch 1, 2026-09-02.** Pinned by `verify.py: engine dialogue line states`;
> `docs/RECONSTRUCTION.md` 2026-09-02 — *A LINE'S END RULE IS PER-ASSET, and two shipped lines are not ordinary lines.*

`Dialog_TickUI` case 4 sets the line state to **2** for a normal line, **7**
when the asset is `125338` (the intro's first line: cut by ANY key but
up/down, `(a2 & 0xFFFFFFF3) != 0`), and **8** when it starts `02E19A`
(the 7-byte `memcmp`): a state-8 line ENDS ITSELF when `Morph_IsDone` — no
press — and is also the one line `Morph_Play` cuts without a blend-out. A
state-2 line ends only on confirm; the voice running out leaves it up, which
is the rule `DialogPlayer::tick` models. The port has no per-asset state, so
`02E19A` waits for a press the game never asks for, and `125338` ignores
the keys the game accepts.

## Where the review stands (2026-09-02)
The interpreter (every implemented opcode against its handler), the
scheduler (`Script_Pump`, `Script_ProcessActions`, `Script_QueueAction`,
`Script_Execute`), the zone lifecycle, the message dispatch, the area
transition state machine, the loaders' script hooks, the dialogue's
condition/action events and every opcode that writes the context's
status, the pc, a variable, a save bitmap, an object list or the prop
state have been compared. The conversation scripts use only ported control
opcodes (247 conditions, all leaving a value; the actions' stubs are
93/50/52/76/68/92). What is left is output-class: the voice-overs, the
fades, the look-at and shoot families - listed below, not reviewed.

## Queued for the next pass
* 63 `player.move` (312 sites) is `Player_GoToMove(addr, -1)` without the
  hold of 89 (issue 23); 127 `player.pos.sync` tail-calls `0x41C140`.
* 92 `media.play` (2600 sites - the voice-overs, 10 of 561 shipped) and the
  fades 118/119/132/133 (1435 sites) are output, not decisions; 104/105
  `player.anim.hold/release` (1265) are the pose rule the dialogue staging
  already models; 138/139 look-at (515) and 82/84 shoot (636) belong to
  the actor runtime.
* 81 `shoot.end` and 116 `shoot.player.suspend` free an object slot
  (`ObjectSlot_Free`) and reset the player's shoot state - the shoot-mode
  half of the actor runtime, not modelled by the session.
* The full map of what RESUMES a parked context, for the record: status 3
  by event 2 (fight over), 4 by event 3 (object / move finished), 6 by
  event 5 (screen answered), 7 by event 4 (camera move ended), 8 and 10 by
  `Script_Pump`'s tail once `Area_TickLoad` reports, 9 by
  `Script_ProcessActions` next frame.

## Corrections from batch 1 (2026-09-02)
* Table: ops 111/112 are `timer.stop`/`timer.start` respectively - the names
  were inverted (T6/T8, from the two tail-jumped routines and the reader
  `sub_41E300`; the corpus does `114 -> 113 -> 112` twelve times); op 16
  `set.var.i32` reads 6 operand bytes where the table said 5 (T1/T8; 0
  shipped sites); op 15's VALUE also goes through the shared fetch (T1).
* A status value 5 exists: `Area_Transition` mode 0 state 4 writes it into
  the superseded caller; no resume path found (T10).
* `character.hide -1` hides the PLAYER (`sub_41CED0(Actor_Player(), 1)`);
  the ObjectShown bit index is the 20-byte character record's +18 (T2).

## Notes — differences too small to be issues yet
* Arc units. `Area_Load` converts every zone's `+60`/`+62` from 4096ths of a
  turn to integer DEGREES at load (`* 0.087890625`, truncated), and
  `Actor_ScanZones` compares them against the actor's Euler Y in degrees with
  an explicit 0/360 wrap (`cmp ecx, 168h`). `Zone::faces` keeps the raw
  4096-unit values and a modular difference. Equivalent except at an arc's
  edge, where the engine's truncation to whole degrees moves the boundary by
  up to about 0.1 degree; and `arcWide == 0` is "any facing" in both.
* `camera.set.wait` halves its travel (`sar eax, 1` after `cdq`) when the
  camera's mode word (`+26`) is 4, and takes bit 5 of its THIRD field into the
  request's `dword_93081C`; `camera.set` (95) does neither. One camera in the
  whole corpus has mode 4 (5342 are mode 12, 38 mode 20) and no 96 site
  names it, so the halving is latent. The port ignores the third field.
* The port stops a run after 20,000 steps (`Runaway`); the engine has no
  cap. The longest shipped script is 1526 instructions straight through
  (AREA 149, record 5 slot +0), so only a looping script could differ.
* `Script_ProcessActions` opens with a 60-second watchdog on the pending area
  transition: a context stuck at status 10 for a minute retries
  `ScriptObject_Start` and clears the transition block. The port has no
  equivalent; a load that never reports would wait for ever.

## Resolved while reviewing (docs to fix, not the port)
* `readable/src/01_file.c` `Script_Pump` (CLEAN) misrenders state 2. The
  assembly at `loc_407F26`..`loc_407FCC` sets `edi = 1` on BOTH outcomes of
  the no-object `Script_Run` call (`jz loc_407F93` when it reached `end`;
  otherwise `dword_4E66B8 = 0` and fall through to `loc_407F93`), so action 2
  is queued from that path too — the dry run only decides whether the press
  counts as handled. The CLEAN text queues it from the held-object branches
  alone, which would mean no zone could ever launch a conversation. The
  port's model (queue the activate on the press) is the engine's.

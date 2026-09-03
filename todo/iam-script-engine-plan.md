# IAM script engine — resolution plan (2026-09-02)

Issues are in `iam-script-engine.md`. Ordered by severity (A first), grouped
into work packages so that NO TWO AGENTS OF A BATCH EDIT THE SAME FILE — the
repo is not a git repository, so there are no worktrees to merge. Rules for
every agent:

* edit only the files the package names; a new `engine/tools/*.cpp` probe
  is allowed; `tools/verify.py`, `docs/*.md`, `CLAUDE.md`, `engine/README.md`
  and `engine/Makefile` are OFF LIMITS during a batch;
* build with a private object dir and only the binaries you need:
  `make -s OBJDIR=build/obj-<task> build/<tool>`; do not run `verify.py`
  (it rebuilds the shared tree) — run your own probe;
* deliver the check code, the RECONSTRUCTION log row and any doc text as
  `todo/pending/<task>.md`; the coordinator integrates them after the batch;
* every fix quotes the engine function it transcribes, and every check is
  SHOWN to fail on the unfixed behaviour (PORTING B2).

## Batch 1 (8 tasks)

| task | model | files | issues |
|---|---|---|---|
| T1 interpreter | Fable | `src/script/interp.{h,cpp}` | 1 division, 4 + 20 shared fetch on 8/14–17/71, 35 `var.set.random` (seeded RNG in the Interpreter) |
| T2 session | Fable | `src/script/area.{h,cpp}`, `src/script/script.{h,cpp}` (read) | 2 dialog frame return, 6 + 25 `scene.load`/`unload` on the resident area, 8 message dispatch (with `setParams`), 11 `ui.open` −1 resume (session half), 17 resident-return skip, 28 shown bit, 32 `player.become` (identity + bio copy) |
| T4 zone harness | Opus | `src/script/world.{h,cpp}` | 5 activate dedupe + one-shot bit 15, 9 touch camera (recorded), 13 resume parked contexts |
| T5 dialogue | Opus | `src/script/dialogue.{h,cpp}` | 39 per-asset line states (7: any key cuts, 8: ends on its own) |
| T6 game state | Opus | `src/script/gamestate.{h,cpp}` | foundations for 29/30/37: object-list has/add/remove/clear (GAME_STATE 3 semantics), prop-state get/set (2-bit), timer state + clock read |
| T8 tables + readable | Opus | `tables/vm_opcodes.json`, `tools/dialog_disasm.py`, `readable/src/01_file.c` | 31 lengths/names for 43/44, 33 rename op 93, the CLEAN `Script_Pump` state-2 misrendering |
| T9 camera editing | Fable | `backends/sdl/play.cpp`, `src/script/scenerunner.{h,cpp}`, `src/o3de/camedit.{h,cpp}` | 12 camera mode 13 (editing playback on `scx.play*`), 11 viewer half (leave a screen → answer −1, keep running) |
| T10 docs | Opus | `docs/SCRIPT_VM.md`, `docs/GAME_STATE.md` | the status/resume map, the transition state machine, op 93's real meaning, the message parameter block |

## Batch 2 (2026-09-02, after the Impasse cutscene ran end to end)

The user's case: the Impasse cutscene must hand over to ADVENTURE mode
(`camera.set 0` + `scene.load 237,57` + `player.anim.release`), play its
voices, and its last cameras were wrong (fixed outside the batch: the scene
function's repeat count, `program.cpp`). Two extra tasks (E1, E2) and the
script issues the hand-over depends on. `area.cpp` is the bottleneck: every
Session-side issue lands in it, so wave A gives it to ONE agent and the other
agents deliver modules with their Session wiring as pending text.

### Wave A (6 agents, exclusive files)

| task | model | files | scope |
|---|---|---|---|
| E1 voices | Opus | NEW `src/audio/voiceover.{h,cpp}`, NEW `tools/voice_probe.cpp` | `media.play` (92): resolve the ZVO object's `+14` stem to `VOICEOFF\*.ADP`, decode with the ADPCM decoder, hand PCM to the viewer. The `play.cpp` hook is delivered as text (E2 owns the file) |
| E2 player | Fable | NEW `src/actor/player.{h,cpp}`, `backends/sdl/play.cpp`, NEW `tools/player_probe.cpp` | adventure mode in the viewer: the engine's control scheme -> input word -> the player's `.CTL` channel -> clip root motion -> `Walker` -> position; the mode-0 follow camera; the player drawn and posed. Exposes `pos()`/`facing()` for the zone scan (wave B) |
| T11 transitions | Fable | `src/script/area.{h,cpp}`, NEW probe | 3, 14, 15, 19, 36 (two resident slots, staged load, departure/arrival objects, `area.preload` deferral, `area.arrive`), 16 (32-slot reuse order, 33rd unlisted), 18 (unknown camera: no hold), 21 (restart) |
| T12 world ops | Fable | `src/script/interp.{h,cpp}`, NEW `src/script/hooks.h`, NEW `src/script/props.{h,cpp}`, `tools/run_scripts.cpp` (the sweep switch only), NEW probe | 22, 24, 29, 30, 33, 34, 37: inventory lists, prop state, actor properties, held object, timer - through a hook interface the Session implements (wiring delivered as text); with no hook installed the opcodes stay stubs so `engine: execute` (DB byte-identical to tools/sim) holds |
| T13 zones | Opus | NEW `src/script/zones.{h,cpp}`, NEW probe | 10 (and 9/5 live): `Zones_RegisterAll` over both slots' four tables, `Actor_ScanZones` per frame (touch/event 8 camera, facing arc, arm, press, one-shot latch, leave), the prune. Reuses `world.h`'s `Zone`; Session wiring as text |
| T14 docs | Opus | `engine/README.md`, `docs/CUTSCENES.md`, `docs/FILE_FORMATS.md`, `todo/iam-script-engine.md`, `todo/pending/T*.md` | merge the batch-1 doc text still sitting in `pending/T1,T2,T4,T9`, cross-link the fixed issues with their log rows |

### Wave A landed 2026-09-02 (20 related checks, 0 failed)
E1, E2, T11, T12, T13, T14 integrated; the Session wiring of T12's hooks, T13's registry and E2's position is wave B's.

### Wave B (confirmed 2026-09-02; 3 agents, no shared files)
| task | model | files | scope |
|---|---|---|---|
| T15 session wiring | Fable | `src/script/area.{h,cpp}`, `backends/sdl/play.cpp` (the two feeding lines only), probes | T12's `WorldHooks` (pending/T12.md 6), T13's `ZoneRegistry` into `Session::frame` (pending/T13.md 3; the leave from zones replacing T11's stand-in; the prune hook), E2's `pos()/facing()` and ground-decor event 9 into the scan (pending/E2.md 7, T11.md API list); then 7 (unconsumed press -> message 26, the dry-run marker) and 38 (`end` extras) |
| T16 parking opcodes | Opus | `src/script/interp.{h,cpp}`, probe | 23 (89 `player.move.wait`), 26 (62 `fight.begin`), 27 (126 `camera.set.at_address`): the interpreter side; the Session arms as text |
| T17 channel | Opus | `src/actor/channel.{h,cpp}`, `src/actor/player.cpp`, probe | todo/actor-runtime.md 1: the lone-idle drop, the 0x20000000 truncation, an explicit idle word; drop the controller's shadow; extend `engine: actor states` |
* Coordinator: integrate, run the related checks, replay the chain into adventure mode and LOOK, log, report tokens.

### Wave B landed 2026-09-02
T15, T16, T17 integrated; T16's Session arms landed behind move/fight hooks; op 62's operand length corrected to 6 (21 corrected). 33 related checks, 0 failed. Left labelled: camera mode 14, the move/fight hooks in the viewer, the held-object paths, `Actors_SpawnFromTables`, `Inventory_Insert`'s kind gate, the `+0x1A` travel halving of 96.

## Batch 3 (proposed 2026-09-03) - the missing characters

Issues `iam-script-engine.md` 40 and `omk-play.md` 41. Two tasks, exclusive
files, T20 designed against the API T19's brief names and integrated after it.

| task | model | files | scope |
|---|---|---|---|
| T19 spawn — **LANDED 2026-09-03** (Opus, ~200k tokens; three corrections to issue 40, see pending/T19.md 7) | Opus | `src/script/area.{h,cpp}`, NEW `src/formats/placements.{h,cpp}`, NEW `tools/spawn_probe.cpp` | issue 40: the 20-byte placement reader; `ResidentSlot::characters` built in `completeLoad` between the props and the startup scripts (case 5), runtime slots from a 100-entry table; attached = the `ObjectShown` bit at load; 78/79/`player.become` toggle it; `shown()` derived, carrying actor, model, bank, pos, facing, slot; evicted with the slot; the `+270` clear. Check `engine: spawn from tables` - 7/4 for 222 + 55, the Demon at 7605 -80 2980, hide 57 -> bit 804 clear, 628/1032; SHOWN to fail with the spawn skipped |
| T20 staging — **LANDED 2026-09-03** (Opus, ~308k tokens; two follow-up fixes in the main session — the clip-root anchor and the per-frame placement re-assert — closed its open "no beat frames a body") | Opus | issue 41: `std::vector<Staged>` replacing the `speaker*` set; pose source per actor (its own program / the line / the bank's default state at the placement); per-character texture base; the controller's body for the player's id; a "staged N" summary line. Confirmed by rendering `A_2_DemonLook` |

Reading budget for both: `iam-script-engine.md` 40 / `omk-play.md` 41, the
lines they cite, `area.h` 160-176 and `area.cpp` 1396-1420 (`shownBitOf`,
`modelOfActor`), `completeLoad`; T20 also `play.cpp` 2322-2360 and 2588-2680.
NOT `docs/RECONSTRUCTION.md`, NOT `engine/README.md` whole.

### T18 landed 2026-09-03 (done in the main session, no agent)
* T18 `area.*`, `walk.*`, `player.*`, `play.cpp`, NEW `tools/airlock_probe.cpp`: walking between areas - two shown decors (`ResidentSlot::shown`), the active row from the FEET (`decorUnder` -> event 9), the teleport re-seat (`placementSeq`), `player.anim.hold` (ops 104/105), the `media.play` subtitle, and `engine: airlock walk` against traces/impasse-walk.log's airlock order (tier 4; shown to fail). HANDOFF-adventure.md deleted, as it asked; the record is RECONSTRUCTION's 2026-09-03 row.

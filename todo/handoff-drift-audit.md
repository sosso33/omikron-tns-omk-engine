# Handoff - the DRIFT AUDIT, as of 2026-10-06 (`29849e8`, pushed)

**Read this first to pick up the drift audit.** The audit itself is
`todo/drift-audit.md` (one row per place the port can behave differently from
the original; each row says what was read and what was done). This file says
where the work stands, what waits on the reader, how to reach each thing, and
the traps that cost time. Prose under CC-BY-4.0 like the rest of `todo/`.

The working rules the reader set, which still hold: read the original's code
before changing anything and add no approximation; work in steps - commit,
report, wait for the go; push only on "yes"; Opus agents only (a Fable agent
needs an explicit go); NEVER start the full sweep without asking; run checks
with `--jobs`; write a reader's report into the todo/docs at once; the
reader's testimony outranks a reading.

## 1. Where it stands

**The whole audit is ONE task** for the sweep counter (`todo/sweep-log.md`:
1 since the last full sweep, plus this task once finished). Every step is
recorded in the sweep log's in-progress list with the checks it ran.

**Done** (each row in `todo/drift-audit.md` carries its commit and its
check): S1 the clock, S2 reply actions, S3 `game.restart`, S4 the inventory
checkpoint, S5 the ledge flag, S7 the shoot freeze/suspend, S8 every unposted
message (and Gandhar's whole fight, `todo/gandhar.md`), S9 checked faithful,
S10 the zone box, S11's 123 and 136 (hide piece, camera shake), S13-S16,
M1 hide/show, M2 the rest pose, M2b body steps found BY NAME, M9 the lift
ride, T1 the pause's quit, T4 checked (no exposure), **S6 (2026-10-06): a
fight always begins and its script always parks**, and **L1, the lighting
(2026-10-06), six steps**: characters and props lit by the set from its
ambient grey, the unlit set floored at that grey, the day/night cycle (fog,
clear colour and floor by the game clock), the underwater mode, the per-pixel
enhancement's start colour.

**Open, in the order I would take them** (my ranking, the reader decides):

1. ~~**S11's op 150/151**~~ - **DONE 2026-10-06**, the BLACK-AND-WHITE
   cutscenes on every backend (`engine: grey bank`; the 3DS shaders written
   but not built - no devkitARM on this machine). `todo/play-test.md` 33.
2. **M5 - `object.place_at` (op 98)** queues a `PropEvent` nothing reads; a
   prop a script moves stays at its chunk placement. Gameplay-visible.
3. **M3 - the conversation speaker.** An unresolved speaker is the street's
   reserved pedestrian (`Slider_Init`'s `dword_4C8898`), not "any body
   wearing the model", and never a fresh body at the camera solve. Read, not
   changed.
4. **T2 - the dialogue line clock below 10 fps** (the classic Mac at 6-10
   fps, heavy Vita scenes): faces fall behind the voice.
5. **M4** one actor id in both resident chunks (95 ids, co-residence not
   measured); **T5** randomness (the VM's xorshift never seeded, the crowd
   reseeded to 1 on every load); **T3** the fight AI on game time;
   **M6** the port's 100-unit floor snap; **M7** a scene clip's pitch (3
   beggars); **M8** the texture-cache substitution (the port is "more
   correct" - declared); **S12, T6, T7** small residue.

## 2. What waits on the READER (nothing here has been played)

`todo/play-test.md` item 32 has the routes. In short:

* **The cave (AREA 2)**: the snake rising out of the lava before his shoot
  (`4_D+Pont2`, found by name); his PINK tint (base 63 + the cave's red
  lights - the reader's frame measured green <= 66, which is what confirmed
  the base); after a lava touch, the green glow round the door
  (`grotte.SFX` pieces 0-3, keyed to `Wait2sec`) with the door dark in front
  of it (the clamp). The reader confirmed "his head way above the player".
* **Anekbah at different hours** (`--clock T`, 0..3599999; 1000000 is
  midday, 3000000 night): does the floored daylight look like the original?
  This is the one lighting reading with no reader confirmation yet - at
  midday it floors ~73% of the city's corners (whole-dword compare).
* **Jaunpur's canal** (`--area 1 --stand 10524,-40,10284,270`, walk in): the
  underwater green fog and sway. The port's mode TOGGLES every ~30 frames
  because its (reconstructed) swim camera bobs across the water line - does
  the original's hold steadier?
* **The Morgue** (AREA 35): the bodies on the slabs posed by `Cadavre`.
* Every character in a lit set now LOOKS different (black-plus-lights from
  the ambient, not their baked white): Kay'l on Anekbah's street is dimmer
  and side-lit. A reader's eye on any street is worth more than a check.

## 3. How to reach things, and the instruments added

| what | how |
|---|---|
| the Gandhar fight, real route | `--area 2 --stand 1811,-9,1216,0 --player-at 60:125,-9,401,0` (baton from zone 313, the bridge zone 317 at frame 60) |
| the time of day | `--clock T` (a harness write; `Clock_SetTime`) |
| a melee fight on demand | `--fight <character>` - now attaches a hidden opponent, as `Fight_Engage` does |
| the scripted fight that refuses | `OMK_FIGHT_REFUSE=1` with `--fight-supermarket` (the script must WAIT) |
| lighting off for comparison | `--no-actor-light` (characters and props), `--no-crowd-light`, `OMK_NO_AMBIENT_CLAMP=1` (the set floor) |
| the fog colour override | `--fog-colour r,g,b` (else the day/night colour) |
| a body's trace | `OMK_TRACE_ACTOR=<id>` |

Log lines worth grepping: `LIT by the set (sub_440CA0)`, `resolves BY NAME
to actor`, `day/night (sub_41E7A0)`, `re-floored at grey`, `UNDERWATER
ON/OFF`, `ATTACHED by the fight`, `the script waits for event 2`.

Checks added on 2026-10-06 (all `--slow`, all shown to fail): `engine:
undriven rest pose`, `engine: actor lighting`, `engine: ambient clamp`,
`engine: day night`, `engine: underwater`, `engine: fight park`; extended:
`engine: gandhar play` (the snake found by name), `engine: per-pixel
lighting` (the base in both probes). Re-pinned with dated notes: `engine:
gandhar`, `gandhar head`, `gandhar grab` (his new start point), `engine:
frame hold` (58.3), `engine: back-face cull` (45.8/86.4), `engine: fight
letterbox` (594). `engine: shoot hit` stays red ON PURPOSE - its gunman 240
now reads his placement (`todo/shoot-patrol.md`).

## 4. Traps that cost time on 2026-10-06

1. **A `scx.play` operand is the object's HANDLE >> 16, not its index.**
   Object 0x14 in `Grotte.SCX` is `4_D+Pont2` (handle 20), not
   `1_GandharStand` (index 20) - one wrong conclusion was reported before
   `SceneRunner::start`'s own comment settled it.
2. **`+416` in two structures.** The crowd's port read "every site that sets
   +416 sets it to 0" - those are ACTOR records (the Euler); the SCENE's
   `+416` is the set's ambient grey (`Read3DO_Init`). `docs/FILE_FORMATS.md`
   has the account; the same trap took the fog colour: "no other writer of
   the scene's `+336`" missed `sub_41E7A0`'s `v5[84]`.
3. **A reader's vivid green looked like it refuted the set clamp for an
   hour.** It was the `Wait2sec` set pieces (additive sprites). When the
   original seems to contradict a reading, look for ANOTHER source of the
   pixel before doubting the code - and measure the reader's frame (the
   snake's green channel capped at 66 = the base 63, which is what proved
   the base).
4. **A check whose discriminator never discriminated.** `engine: fight park`
   first waited for the post-fight voice line; the old path never reached it
   either, so the mutation turned the check red only through the NEW code's
   log line. Diff the old path's log against the new before choosing what to
   assert.
5. **An edit that slices a range deletes what sits inside it** - the
   per-pixel edit removed `litCrowd` with the comment above it; the build
   said so. Check `git diff` of every sliced edit.
6. **`engine/backends/n3ds/*` belong to another session** (the 3DS port) and
   were modified in the tree all day - never stage them with this task's
   commits.
7. **Day/night areas move checks**: the Impasse and the docks are flagged
   areas, so anything measured there at the save's clock (2566060, phase 2,
   grey 10) changed with step 4.

## 5. The engine facts this task established (where they live)

* `docs/FILE_FORMATS.md`: "WHICH BODY a body-animation step moves" (param 0
  by name, the actor start's pin); "What the lights are FOR" (characters and
  props through `LightObject`, the ambient start, the clamp, the day/night
  flag).
* `docs/ASSETS.md`: the clamp and the two `+416`s; "The fog" (the colour by
  the clock - the black reading kept as history; the underwater mode proven).
* `engine/src/o3de/daynight.h`: `sub_41E7A0` transcribed.
* `todo/gandhar.md`, `todo/astaroth.md`, `todo/swimming.md`,
  `todo/mesh-lights.md`, `todo/enhancements.md` row 7: corrected in place.

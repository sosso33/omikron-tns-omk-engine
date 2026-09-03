# Street life — the people on the city streets

How *Omikron* populates a city street, read from `Runtime 2.exe` on 2026-09-03
and measured against the shipped data. Written before the port of it was
started; `todo/street-life.md` carries the plan and the progress, and every
"not yet read" below is a claim still waiting on its evidence.

There are **three** mechanisms, and only one of them is what the first survey
found. In order of what a player sees:

| mechanism | what it is | port status (2026-09-03) |
|---|---|---|
| **A. the procedural pedestrians** — the `.OPT` "trajectoires" | anonymous walkers spawned along authored lanes, density from the options menu | format read (`opt tracks` 6/6), spawner and walk read; **not ported** |
| **B. the authored extras** — scene programs on placed characters | couples kissing, beggars, sport, patrolling Mecaguards, walking pairs; one looping `.SCX` program each | ported and running in every city **except Anekbah** (one operand misread) |
| **C. the crowd push** — the spatial index | the player is shoved by nearby bodies | not ported |

Talking to a passer-by is neither: it is an ordinary zone-activate script
(`var.set.random` on "n° VO passant", then one of four `media.play` voice
lines), already ported through the zone registry and the voice-over path.

---

## 0. What an NPC is NOT — the negative results that shaped this

* **A placed NPC has no `.CTL` state machine.** `Actor_LoadBankList`
  (0x00419CB0) has four callers and every one is the player: `Game_HandleEvent`
  (new game), `player.become` (`sub_40D590`), `State_Apply` (save load) and
  `Game_Init`. `Actors_SpawnFromTables` calls `Actor_LoadModel` and
  `Actor_SetPlacement` and nothing else. So a spawned character sits in
  `ACTOR_STATE` **0** (inert — `Actor_LoadModel`'s initial value) until
  something owns his body: a scene program (state 4), the shoot AI (3/15) or a
  fight (2). There is no idle machine and no walking machine to port per NPC.
* **There is no crowd AI.** `sub_4272B0`, the "behaviour selector", and the
  1500-line `sub_424DE0` are both shoot-mode ("error : perso is not in shoot
  mode !"). The character-type table's *Man passer* / *Woman passer* (types 1
  and 2) matter only to the shoot AI's dispatch; **16** of the 830 AREA
  placement records carry them, and Anekbah hides its two at startup
  (`character.hide 394`, `395`).
* **Announcements cannot see any of this.** `scx.play.actor`, `character.show`
  and `character.hide` announce to CHARACTERS, which `Dbg_LogTagged` filters,
  so no golden trace shows a crowd starting; the `.OPT` pedestrians touch no VM
  handler at all. The oracle for everything below is the data and the eye.

## 1. Mechanism B — the authored extras (scene programs)

**Read from the code, confirmed on the data and in the port.**

The area's own startup script (chunk `+4`, `docs/CUTSCENES.md` §5) issues one
`scx.play.actor <CHARACTERS id>, <object word>, 0` per extra. The handler
(0x00403300) resolves the character through the placement tables, then
`ScriptObject_StartOnActor` (0x0041BA80): the actor's state is parked in
`[102]`, `[101]` becomes **4**, and the scene object's program owns the body
until it ends — which, for an extra, is never: every one is `loop -1`.

The programs are `Script_SelectRelativeBodyAnimation` (0x0200002A) chains:
placed once on a `.3DP` path, clip after clip, the couples linked by `sync`
entries (`docs/FILE_FORMATS.md`, "How the two body-animation functions PLACE
the character"). The object names say what each is: `Chemin01F/M` (walkers),
`Kiss0nF/M`, `Indic0nF/H`, `Mendiant0n`, `Sport0n`/`Gym0n`, `Meca0n`,
`Anno`/`Anneaux` (the rings seller), `R_CoupleF/H`, `R_PassantSeul`.

| | count |
|---|---|
| `scx.play.actor` sites in AREA startup scripts | 621 (117 areas carry a startup script) |
| Anekbah (AREA 0) | 26 extras + 6 plain scene programs |
| Jaunpur (1) | 27 |
| Lahoreh (64) | 29 |
| Mahaleel (145) | 34 |

**Port status, measured with a Session probe** (load the area's `.SCX`, load
the area, run 200 frames, list `scene().started()`): Jaunpur starts 27
actor programs and Lahoreh 30, each with actor, clip and path resolved;
**Anekbah starts 0 of 26.**

**Why Anekbah fails — an operand misread shared by three tools.** Anekbah's
scene object ids are `0xC2xx`, so bit 14 is set. The VM's shared operand fetch
treats bit `0x4000` as the indirect-operand flag (`docs/SCRIPT_VM.md`,
"Operand encoding"), and `Interpreter::fetch16`, `tools/dialog_disasm.py` and
`tools/sim/vm.py` all apply it to every 16-bit field. But the handlers of
ops 57 and 59 read the **object word raw** — at 0x403300 the first word after
the actor goes `and ecx, 0FFFFh; mov ebp, ecx` with no `test ch, 40h` — and
only the actor word and the trailing word go through the indirect test. So
the port hands `SceneRunner` object 0 and nothing starts. Over the corpus the
bit is set in 30 of the 3,300 `scx.play*` object words: 26 are Anekbah's
startup extras, 1 more `scx.play` there, 1 `scx.play.actor` in a slot script
and 2 `scx.play.player`. SCRIPT_VM's "all 59 indirect operands are
`push.i16 0x4000`" was counted over the 5785 slot scripts with the same
misread; it is right about `push.i16` and wrong as a statement about the
corpus. **Fixed 2026-09-03** in all three: ops 46, 57, 58 and 90 read the
object word first and 59/60 second, every one raw (`asmfn.py --op N` for each);
`SceneRunner::handle`, `tools/sim/run.py` and `dialog_disasm.RAW_WORD` now
take it unsigned, and `verify.py: engine: city crowd` compares each city's
startup script against what the Session starts - Anekbah 26/26, Jaunpur 27,
Lahoreh 29 (which parks 240 frames behind its fans' `scx.play.wait` first),
Mahaleel 34, every one with actor, clip and path resolved. Shown to fail with
the mask dropped: Anekbah 0/26.

## 2. Mechanism A — the procedural pedestrians (`TRAJECTOIRES\*.OPT`)

**The file format is READ and self-checking (`tools/opt_track.py`,
`verify.py: opt tracks`, 6/6); the spawner and the walk are read; three
helpers of the action phase and the whole vehicle side are not** (listed at
the end). This is what the options menu's density row drives, and it is what
fills a street between the authored extras.

### The option

Row **6** of the 74-row options table (`tables/ui.json`), page "Vidéo":
**"Niveau d'activité dans les rues"**, five choices *Très faible* 0 … *Très
important* 4. Its read hook `Opt_ReadStreetActivity` (0x0048FC70) copies the
byte at `dword_90E724+2` into the row; the apply hook (0x0048FC40, no `proc`
label — CLAUDE.md §1's trap; the assembly is at listing line 227473) stores
the chosen value back. `sub_41F4C0`'s defaults write the dword as
`0x01030000`: **street activity 3 ("Important")**, detail level 1.
`sub_43A7xx` (listing line 92242) zeroes both bytes on a different path,
not yet attributed. The byte has **one consumer**: `Slider_Init`.

### The circuit — `TRAJECTOIRES\<stem>.OPT`

`Area_TickLoad` case 8 appends `.OPT` to the 9-byte name at AREA `+115` and
`Area_LoadSliderTrack` (0x0041B420) hands the file to `Slider_Init`
(0x00453450). Five areas name one and all five ship; `biblio.opt` ships
unnamed. So the procedural crowd exists in **four city streets and the Puits**;
every other area has only mechanism B.

The header is 19 dwords: a magic `V1.0`, the pedestrian lane range `[1]..[2]`
(the vehicle lanes run from `[2]` to the lane count `[5]`), the two spacing
units `[3]` (pedestrians) and `[4]` (vehicles), then (offset, count) pairs for
seven blocks that start exactly where the previous ends and end on the file
size. `Slider_Init` relocates the seven offsets into pointers and every mover
reads through them:

| block | size | fields (from the functions that read them) |
|---|---|---|
| lanes | 24 | `+0` origin `float[3]`; `+12` runtime list head; `+16` first key; `+18` first route; `+20` route count (0 reads as 1); `+21` key count |
| keys | 20 | `+0` runtime list head (the movers on this segment); `+4` delta `float[3]` to the next key; `+16` action point, −1 none |
| actions | 20 | `+0` the point, relative to the mover reaching the key; `+12` the facing to turn to, degrees; `+16` animation **type** (0 = none); `+18` a count 1..100 for the play phase; `+19` = 1 |
| routes | 12 | `+0` runtime list head; `+4` destination lane; `+6` first step; `+8` reservation group, −1 none; `+10` step count |
| steps | 16 | `+0` delta `float[3]`; `+12` reservation group, −1 none |
| groups | 4 | `+0` first list entry; `+2` entry count; `+3` runtime busy count |
| lists | 2 | a group index |

| file | area | lanes (ped / veh) | keys | actions | routes | steps | groups | spacing |
|---|---|---|---|---|---|---|---|---|
| anekbah | Anekbah | 216 / 26 | 2781 | 84 | 344 | 916 | 482 | 15 / 15 |
| souk | Jaunpur | 179 / 22 | 999 | 68 | 246 | 161 | 191 | 30 / 15 |
| qchaud | Qalisar | 226 / 33 | 1744 | 37 | 341 | 500 | 367 | 30 / 30 |
| lahorey | Lahoreh | 162 / 0 | 719 | 33 | 196 | 81 | 128 | 15 / 15 |
| puit | Anekbah CS Puits | 6 / 0 | 84 | 10 | 6 | 2 | 0 | 30 / 15 |
| biblio | (unnamed) | 27 / 0 | 269 | 7 | 37 | 27 | 25 | 50 / 15 |

The self-checks: the layout lands on every block and the file size; every
reference resolves; every runtime field (the three list heads and the busy
byte) is **zero on disk**, which is what makes them runtime; and **no route
crosses** between the pedestrian and the vehicle lanes. A route owes no
geometric closure — its steps are waypoints and the mover then walks a
straight line to the destination lane's origin and snaps onto it — so the
median of that last leg (66–115 units) is reported, not asserted.

### `Slider_Init` — the pools and the spawn

1. Two pools: **240 mover slots of 192 bytes** (`dword_53830C`, shared by
   vehicles and pedestrians) and, only when the area has pedestrian lanes and
   a walk clip exists (`List_PickRandomByType(bank, 9)`), **200 pedestrian
   records of 72 bytes** (`dword_8F5E94`) binding a mover slot to a body.
2. The **models**: `sub_4539B0` walks a 12-byte-stride name table from
   `"PSH_FN"` (men) and one from `"FSH_FN"` (women), loading
   `MESHES\PERSOS\<name>.3do` for every bit the per-area masks
   `sub_40E990(area)` / `sub_40E950(area)` set — so each city dresses its own
   crowd. `sub_453A70` splits each model into up to **4 LOD sub-objects**
   sorted by vertex+face count, `sub_453E80(pool, n, 100)` turns each model's
   weight at `+84` into a **spawn quota** summing to 100 (decremented per
   spawn, a model skipped when exhausted), and `sub_453910` builds each
   model's four-clip list by animation type. `"sli_fn"` and `"moto"` are the
   vehicles, from the same loader.
3. **The spawner, and where density enters** — `sub_453B40(lanes, callback,
   laneTable, spacing)` with

       spacing = (5 - streetActivity) * header[3]

   walks every pedestrian lane's keys accumulating their lengths and, every
   `39 * spacing` units, calls `sub_453ED0` to make one pedestrian: a free
   mover slot (flag 2 = in use), a sex chosen by a coin (`rand() & 1`, forced
   when one pool is empty), a model from that sex's quota, a **name** cycled
   from the tables at `off_4C88A0` (men) / `off_4C8920` (women), a 3D
   instance attached to the area's decor node, a **spatial-index
   registration** (`sub_45E040` → slot `+184`, refreshed every frame by
   `SpatialIndex_Update`), the walk clip of type 9, the two foot markers
   `Piedg`/`Piedd`, and the model's bounding radius. The spawner sets the
   mover's position at the key, its direction along it, a "previous" point
   117 units back, the lane at `+72`, a route from the lane's routes
   round-robin at `+76`, flag `0x8` (= pedestrian) and the key index at
   `+186`. Each then gets a **speed factor** `1 + (5 - rand()%10)/20`
   (0.75 … 1.20) times the clip's stride (`sub_453D80`), so the crowd does
   not walk in step. Level 4 packs walkers `39·h3` apart; level 0 five times
   sparser.
4. `SOUNDS\sliderm01.wav` is loaded for the vehicles (`docs/ASSETS.md` §1).

### `Sliders_Tick` — every frame

For each of the 200 records with a mover: if the player's spatial-index
entry touches this pedestrian's (`sub_45DF30`) and no bump is pending, post
game message **15** (man) or **16** (woman) through event 43 and hold it 100
frames; then either the **walk** (`sub_455830`, flag `0x80` clear) or the
**action phase** (`sub_454F40` + `sub_455E90`); refresh the spatial index;
if the detail byte allows and the instance is visible, place the node on the
instance, orient it, and run the foot markers over the ground
(`sub_467F50`); finally sample the current clip at the record's clock into
the mover's pose (`sub_4348E0`). Separately, `sub_452280` is the **talk**
test: the action press looks for the nearest pedestrian within 117 units in
front of the player and posts message **13** (man) / **14** (woman).
`IAM\GLOBAL` subscribes all four — 13/14 are the "n° VO passant" random
lines — and AREA 86 (the library) overrides 13/14 with its own; no chunk
subscribes 15/16 but GLOBAL. So talking to a walker and bumping one are
already scripted, through the message path the port has.

### The walk — `sub_454F40` (the mover) and `sub_455830` (the body)

The mover holds a position, a direction, a "remaining" length at `+48`
(units × 256) and a speed at `+56`; each frame `speed × dt` is taken off the
remaining length and the position advanced. When it runs out:

* on a lane, `sub_455570` adds the next key's delta and, at the last key,
  raises flag `0x10` (route mode);
* on a route, `sub_4554B0` takes the next step's delta, or — after the last
  step, or at once for a route with none — aims straight at the destination
  lane's origin; when that leg is consumed the mover **snaps** onto the
  origin and `sub_455680` moves it onto the new lane and picks the next route
  round-robin (a global counter modulo the lane's route count);
* `sub_455680` also keeps the **occupancy lists**: every key, lane start and
  route has a linked list of the movers on it (the runtime heads above), and
  the mover ahead in the same list is what the following logic reads.

**Following and blocking.** The mover ahead (`+68`) within the sum of the two
radii (`+60`) sets flag `1` (blocked); `sub_455D10` then decides the gait
from the distance covered this frame: under 2 units → stop, and the body
swaps the walk clip (type 9) for an idle (type **11**) on flag `0x100`;
between the two thresholds at `unk_4C8880` the base speed, beyond them the
speed doubled. A slower mover ahead on the same key is **overtaken**: the two
swap list positions and the follower takes a 39-unit sidestep (flag `0x20`).

**Reservation groups** (`sub_453230`): entering a route or a step that names
a group waits (flag 1) while any group in that group's list is busy, then
increments them all; leaving decrements. This is how vehicles and pedestrians
cross each other's lanes without meeting.

**Action points.** A key naming an action whose type is nonzero sends every
second mover reaching it (the global counter's low bit) into the action
phase: `sub_455830` allocates one of the `[9]`-count 48-byte states
(`dword_539928`), picks a clip of the action's type (`sub_434630`), and the
mover walks to the point (mover position + the action's offset); on arrival
`sub_455E90` snaps it there, turns it to the action's facing, plays the clip
(phase 1 with an intro clip when one exists, else 2), and phases 1..3 run
through `sub_4561B0`/`sub_456250` (unread) back to the lane. Note
`u16(action, 16) = 0` on entry: the action's type is cleared, so **each point
fires once per area load** unless the unread phase restores it.

### Not yet read

`sub_4561B0` / `sub_456250` (the action's play and return, 27 + ? lines),
`sub_4563A0` (per tick when the player exists), `sub_453330` (the facing
smoother), `sub_452490`'s caller (the per-model unload), and the whole
vehicle side — `sub_4543F0` spawns the sliders along lanes `[2]..[5]` with
`sub_4544B0`, `sub_452CC0` / `sub_452570` drive them, `dword_8F5E3C` is a
40-slot ride pool, `Slider_TickRide` is the player's state 7. The pedestrians
never read `[16]`/`[18]` except through the groups.

## 3. Mechanism C — the crowd push (the spatial index)

**Read; not ported.** `Actor_Attach` (0x0041CCA0) registers the actor in a
spatial index (`sub_45DFF0(model, x, y, z)` → slot at record `+649`), nine
callers refresh a slot's position with `SpatialIndex_Update` (0x0045E110,
20-byte entries: owner, flags, x, y, z), and `Actor_TickNpc` — the **player's**
state-1 tick — calls `SpatialIndex_Query(slot, pos, push)` (0x0045E190) and
adds the returned `float[3]` to his position and node. The query sums, over
every registered entry within reach (`max(|dx|,|dy|,|dz|) <= radius + own
radius`, radius from `sub_438040` for flag-1 entries and the model's `+88`
otherwise), a per-entry push from `sub_45E690` (flag 1) or `sub_45E390`
(others), and marks the entry touched (`flags |= 2`). Skipped while the
player is in dialogue (state 16), so a crowd cannot shove a speaker. The two
per-entry tests are unread.

## 4. What the port has today, and what "street life" needs

| piece | where | state |
|---|---|---|
| placement spawn, `ObjectShown` bit, show/hide | `Session::spawnFromTables` | done |
| scene programs on actors, path + clip, loops, sync | `SceneRunner`, `Program` | done, Anekbah included since 2026-09-03 (`engine: city crowd`) |
| drawing every shown actor by its program | `omk-play` (T20) | done for the Impasse's three bodies; a 30-body street never watched |
| zone lines to passers-by | zones + `voiceover` | done, never watched on a street |
| `character.look_at_player` head aim | Session records it | drawn? unverified |
| the `.OPT` pedestrians and the density option | `tools/opt_track.py` | the format and the walk read; nothing ported |
| the crowd push | — | nothing |
| a way to stand in a street without replaying the intro | `omk-play` | nothing |

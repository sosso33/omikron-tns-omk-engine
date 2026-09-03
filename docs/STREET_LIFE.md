# Street life — the people on the city streets

How *Omikron* populates a city street, read from `Runtime 2.exe` on 2026-09-03
and measured against the shipped data. Written before the port of it was
started; `todo/street-life.md` carries the plan and the progress, and every
"not yet read" below is a claim still waiting on its evidence.

There are **three** mechanisms, and only one of them is what the first survey
found. In order of what a player sees:

| mechanism | what it is | port status (2026-09-03) |
|---|---|---|
| **A. the procedural pedestrians** — the `.OPT` "trajectoires" | anonymous walkers spawned along authored lanes, density from the options menu | **ported and drawn** (`engine: pedestrians`, `engine: street frame`); to be watched in play |
| **B. the authored extras** — scene programs on placed characters | couples kissing, beggars, sport, patrolling Mecaguards, walking pairs; one looping `.SCX` program each | ported and running in every city **except Anekbah** (one operand misread) |
| **C. the crowd push** — the spatial index | the player is shoved by nearby bodies | **ported** (`engine: crowd push`), with the bump and talk messages |
| **A2. the road traffic** — the `.OPT` vehicle lanes | the hover-taxis (`sli_fn`) and the motos, spawned on the same circuit and driven by the same mover step | **ported and drawn** 2026-09-04 (`engine: road traffic`, `engine: traffic frame`); seen crossing a street |

Talking to a passer-by is neither: for the authored extras it is an
ordinary zone-activate script (`var.set.random` on "n° VO passant", then one
of four `media.play` voice lines), for the walkers the messages of §3 — both
run through the zone registry, the message path and the voice-over path the
port has. **The voices themselves do not ship**: the `ZVO P*`/`V*`/`VH*`
lines those scripts play are among the 551 of 561 `VOICEOFF` entries absent
from this data set (17 files ship), so the talk and the bump run their
scripts and stay silent here. The camera shake the bump handlers ask for
(`camera.shake 5, 5`) is the visible half.

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
helpers of the action phase are not** (listed at the end). The vehicle half
of the same circuit is §2b. This is what the options menu's density row drives, and it is what
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
| actions | 20 | `+0` the point, relative to the mover's segment start; `+12` the facing to turn to, degrees; `+16` the **clip id** (`slot`) in the sex's `.ani` group, 0 = none — `Assis` 2, the idle 16; `+18` how many times the main clip loops; `+19` = 1 |
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
   spawn, a model skipped when exhausted), and `sub_453910` chains those LOD
   objects with the four distances at `dword_4C8870` — 10, 20, 30, 40 m in
   inches (the vehicles' start at 20). `"sli_fn"` and `"moto"` are the
   vehicles, from the same loader. The **clips** come from the area's `.ani`
   at header `+124` — `PASSANTH` for every city — groups 1 (men) and 2
   (women): the walk is type 9, the idle type 11, and each action is a named
   trio, enter/main/exit as types 14/15/16 (`Assis`, `Attend`, `Appel`,
   `Scato`, `Poteau` for men; only `Assis` for women). The per-city model
   masks are the AREA header's `+164` (men) and `+168` (women) into the
   executable's two 12-byte-row tables — Anekbah `PSH/PSH1` + `FSH/FSH1`,
   Jaunpur the `K` models, Lahoreh `PV/VF`.
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
   round-robin at `+76`, flag `0x8` and the key index at `+186`.
   **Flag `0x8` is not "pedestrian"**, which is what this file said until
   2026-09-04: the spawner itself sets it (listing line 135134, `or edx, 8`
   beside the route assignment it does in the same block), so it marks every
   mover `sub_453B40` places, VEHICLES INCLUDED. That matters twice — it is
   what gates `Sliders_Tick`'s vehicle loop (`flags & 9`) and
   `Slider_Init`'s own speed-factor pass, both of which would be dead code
   under the old reading. Each then gets a **speed factor** `1 + (5 - rand()%10)/20`
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

**Action points.** A key naming an action whose clip id is nonzero sends
every second mover reaching it (the global counter's low bit) into the action
phase: `sub_455830` allocates one of the `[9]`-count 48-byte states
(`dword_539928`), finds the clip by id (`sub_434630`) and its enter and exit
variants by name under types 14 and 16, clears the point's id (so no second
walker takes it) and the body walks to the point (segment start + the
action's offset) with the walk clip; on arrival `sub_455E90` snaps it there,
turns it to the action's facing (`sub_442120`), plays the enter clip (phase 1)
or the main one (2); `sub_4561B0` runs the clock and at each wrap
`sub_456250` moves on — enter → main, main looped `count` times (held while
the player is talking to this walker, `dword_53992C`), exit if there is one,
then a walk clip again, flag `0x80` off and **the point's id restored** from
the state. A point is reusable; the "fires once" this file said before the
phase was read was wrong.

**The body and the root motion.** The body is not the mover: it is placed by
the walk clip's **root motion** — `Anim_RootDelta` (0x004711D0) sums the
root track's keys, each key the motion over one frame with the two end keys
scaled by their fractions, and turns the sum by the instance's 3x3
(`sub_453330` builds it from the direction to the mover: row 2 is minus the
forward, a clip walks along −Z). So a body walks at the clip's own pace,
aimed at the carrot ahead of it, and its speed factor (0.75..1.20) scales the
clock. The mover's base speed is the same clip's xz travel over frames
1..frames−1, ×256, per frame (`sub_453D80`).

### The port (2026-09-03, step 3)

`engine/src/formats/opt.*` reads the file with the Python reader's checks;
`engine/src/actor/pedestrians.*` is the pool — `Pedestrians::load` is
`Slider_Init`'s pedestrian half (models from the masks, quotas, the spawner
with `spacing = (5 − level) × h[3]`, each walker's factor), `tick` is
`Sliders_Tick`'s loop over `moverStep` (`sub_454F40`: keys, routes, the
snap, the occupancy lists, the reservation groups), `bodyStep` (`sub_455830`:
root motion toward the mover, following, overtaking, the gait), and the three
action functions. The Session loads it at `completeLoad` (case 8) for an area
naming a circuit, ticks it beside the scene every frame, evicts it with its
slot, and holds the density as `streetActivity_` — default 3, the engine's,
marked for the options menu. `engine/tools/ped_probe` runs a city;
`verify.py: engine: pedestrians` holds the four cities' counts at every level
to the rule written independently over `opt_track.py`, and over 600 frames
every walker live and moved, no mover further than one frame's advance off
the network, no body further than 500 from its mover, lane changes and action
visits happening, no NaN. Not modelled: the body's radius is the model's root
mesh `+88` (read by the Session), the spatial-index registration and the
bump/talk messages wait for step 5, and the facing convention is the actors'
recipe until step 4 watches it.

### The viewer (step 4)

`omk-play` stages every walker of the pool beside the extras: the model
shared by name, the pose from `PASSANTH`'s clip at the walker's own clock,
the feet on the walker's body point, the body turned to its heading, and
nothing beyond the engine's last LOD distance (40 m) — so Anekbah's 200
walkers are a few dozen on screen. A **street start** stands in a city
without the intro: `--save traces/save-appart.bin --area 0` (the DB player
record comes from the save, since Kay'l's actor record is in no city chunk),
`--address A` or `--stand x,y,z,yaw`, `--density 0..4`, `--no-crowd`; it
requests `Camera Player` (0), the follow preset every hand-over ends on.

**Found by looking (2026-09-03).** The first street frame had a figure in a
T-pose floating over the walkers. A crowd model is **four skeletons in one
file** — `PhBassin`, `PiBassin`, `PjBassin`, `PkBassin`, the LOD sub-objects
`sub_453A70` splits it into, 76 meshes for 19 a skeleton — and the library's
tracks name the first, so posing the model left the other three at rest.
The viewer now cuts the rest geometry to the skeleton the tracks name, for
the walkers and for the authored extras, which wear the same models (all
twenty of Anekbah's couples and beggars had three T-posed skeletons inside
them). How the engine picks an actor's LOD is not read; the crowd's four
distances are `dword_4C8870`. Filed as `todo/omk-play.md` 60.

**What the frames settled**: the walkers are posed mid-stride in the city's
own models, turned along their lanes, feet on the street, two of them a
few metres from Kay'l on Anekbah's main street (`verify.py: engine: street
frame` keeps a crowd-on/crowd-off pair differing by thousands of pixels).
**What only a person can settle**: the walk's pace against the original,
the facing convention over a turn (the actors' recipe is assumed), the
following distance, and whether the action points read as a street.

### The head look (step 6)

`character.look_at_player` (138) writes the actor's look-at slot (+400) and
`Actors_TickAll` aims his head at the target every frame: the head node's
forward against the vector to the target's head node, a pitch and a yaw,
then `Actor_SetHeadLook` (0x00468B50) clamps pitch to ±40 and yaw to ±70 and
eases each an eighth of the way per frame into the head node's matrix at
+324. Ported as `aimHead` (`actor/pose.h`) over the composed pose, applied
in the viewer to every staged actor the Session lists as looking at the
player, with the player's head taken 60 units above his feet (the engine
reads his head node; labelled). `verify.py: engine: head look` measures the
Demon's forward after the aim: 45 turns 45, 120 and behind turn 70, up 60
lifts 40, one frame of ease is 45/8. Six shipped startup scripts ask for it
(AREA 148 once, AREA 155 five times); the intro's beats do too.

**Found by a person, 2026-09-03 (`todo/omk-play.md` 61, 62).** A woman in
a T-pose on the street: the library's bone names carry a skeleton prefix the
women's idle and Jaunpur's models do not share, so name matching failed —
matched by the bone name after the prefix now. And a row of identical
crouching men at one door: the extras' path is a PAIR (param 7 the chunk-0
record, param 8 the path inside it) and the port read only the second, so
Anekbah's couples and beggars all landed on its door paths — resolved as
the pair now, and the extras stand in couples across the city. The frame
with an extra whose head seemed detached was most likely the gym pair with
their arms raised; `OMK_LOOK_ALL=1` makes every extra look at Kay'l so the
head aim can be watched on demand.

**Second play, same day (`todo/omk-play.md` 63, 64).** A kissing couple
intersecting with the woman turned away: the program's Euler (params 4-6,
`Actor_SetEuler` every tick) was never applied to a program-driven body —
now a yaw about the pelvis, and the couple embraces. A man seated with his
shins in the street: the viewer seated the first frame's feet, which for the
seated clip are the folded legs — now the rest feet plus the root's summed
y, the engine's `body.y + footY − radius` up to a constant, and he sits on
the bench.

### Still to be watched by a person

The walkers' pace and facing over a turn, the following distance, the push
against the original, the bump's camera shake, a head turning toward Kay'l.
Everything above that a check can hold is held; these are the class §1's
"verified standing still is not verified moving" names.

### Not yet read

`sub_4563A0` (per tick when the player exists) and `sub_452490`'s caller (the
per-model unload). The pedestrians never read `[16]`/`[18]` except through
the groups. The vehicle side is §2b below, read 2026-09-04; what is left of
it there is the player's RIDE.

## 2b. The road traffic — the sliders and the motos

**Read 2026-09-04 and ported the same day** (`engine/src/actor/vehicles.cpp`,
`engine/tools/veh_probe`, `verify.py: engine: road traffic`;
`todo/road-traffic.md` carries the plan). This is the other population on the
`.OPT` circuit: the hover-taxis the game calls **sliders** and the motos,
moving on lanes `header[2]..header[5]` while the walkers of §2 have
`header[1]..header[2]`. **Not** ported and labelled everywhere it matters:
the player's own RIDE.

### The models — two files, and two masks nothing had read

`Slider_Init`'s vehicle half runs when `header[2] < header[5]`, and loads two
tables with `sub_4539B0` — the crowd's own 12-byte walk (an 8-char name, a
weight of 1) over `MESHES\PERSOS\<name>.3do`:

| table | rows | in the tree |
|---|---|---|
| `aSliFn` (0xC1638) | **2**, both `sli_fn` | `SLI_FN.3DO`, 4 root sub-objects: `SlBassin` 223v/413t with four doors parented to it, `slider_fl` 127/250, `SlBasA` 63/122, `SlBasB` 26/48 |
| `aMoto` (0xC14B8) | **1**, `moto` | `MOTO.3DO`, one root, `GEM0` 265v/511t |

`sub_453A70` splits each model into up to four root sub-objects sorted by
vertex+face count — the LOD ladder — and, only for `sli_fn`, sets `0x400` on
each with `sub_437220(obj, 0x400, 8)` (what that flag does is **unread**).
`sub_453910` then chains them with `dword_4C8860`, the vehicle LOD distances
**20/30/40/50 m** in inches, against the crowd's 10/20/30/40 — and for the
sliders it starts the chain at sub-object **1**, not 0.

The masks are AREA header fields the port had never read: **`+172`** the
sliders (`sub_40EA10`) and **`+174`** the motos (`sub_40E9D0`), both **int16
and sign-extended** where the crowd's `+164`/`+168` are dwords.

| area | circuit | ped / veh lanes | `+172` | `+174` |
|---|---|---|---|---|
| 0 Anekbah | anekbah | 216 / 26 | 3 | 1 |
| 1 Jaunpur | souk | 179 / 22 | 3 | 1 |
| 101 Qalisar | qchaud | 226 / 33 | **1** | 1 |
| 64 Lahoreh | lahorey | 162 / 0 | 0 | 0 |
| 157 Puits | puit | 6 / 0 | 0 | 0 |

Exactly the three areas with vehicle lanes carry a nonzero mask and the two
without carry zero — two independent gates agreeing over the shipped data,
which they need not have.

**Slider row 0 is reserved for the player**, and this is what makes Qalisar's
roads different. `sub_4544B0` treats ride slot 0 specially (`v2 == v3`): it
takes model entry 0, marks the record `+22 = 1`, and `dword_8F5E44` keeps it
across area loads. Ambient sliders are drawn from entries **1..n-1**, so
Qalisar's mask of 1 leaves `dword_539934 - 1 == 0` and `sub_4544B0`'s coin
cannot land on a slider: **all 40 of its vehicles are motos**. The mask is
`| 1` at the call, so row 0 always loads even where the area asks for none.

### The spawn — the same walk, and the density plays no part

`sub_4543F0` hands the walkers' own `sub_453B40` the vehicle lanes, the
callback `sub_4544B0` and `header[4]` **undivided**. So where the crowd is
paced `39 * (5 - level) * header[3]`, the traffic is paced `39 * header[4]`
and the options row thins the walkers only. The callback stops the whole walk
at the ride pool's **40** (`if (a1 >= 40) return 0`, and a 0 return ends
`sub_453B40`).

| circuit | veh spacing | uncapped | placed | of which |
|---|---|---|---|---|
| anekbah | 15 (585 units apart) | 126 | 40 | 19 sliders, 21 motos, on 8 of the 26 lanes |
| souk | 15 | 46 | 40 | 20 / 20, on 15 of 22 |
| qchaud | 30 | 85 | 40 | **0 / 40**, on 9 of 33 |

The walk stops the moment the fortieth is placed, so the tail of the lane
list starts empty and fills only as routes carry vehicles onto it (Anekbah:
8 lanes at frame 0, 294 lane changes in 1800 frames).

The kind is a coin, `rand() & 1` (1 a slider), forced to the other when a
pool is empty. `sub_453E80` — the quota rescaling the crowd gets — is never
called for the vehicle pools, so each entry keeps the table's weight of **1**
as its quota: the first spawn of a kind spends it and every later one falls
through to the first entry (LABEL_29 / LABEL_35). With one ambient row of
each kind that is simply that row.

The ride record is 24 bytes, 40 of them (`Mem_Calloc(0x28, 0x18)`): `+0` the
mover, `+8` the state, `+12` the speed cap **5000**, `+20` the sound handle
(initialised to -1), `+22` = 1 on the reserved slot. The mover is a slot of
the same 240-slot pool the walkers use, spawned with `+52` and `+56` both
**256.0**.

### The drive — `Sliders_Tick` → `sub_456530` → `sub_456C70`

`Sliders_Tick` walks the 40 records and calls `sub_456530(ride)` for each
whose mover has `flags & 9` — flag 8 being the spawner's own mark (§2) and 1
"blocked". `sub_456530` switches on the record's `+8`: **0 is ambient
traffic** and falls to the default; **1..7 are the player mounting and
riding**, and are the part not ported.

Ambient is three calls — `sub_456C70`, the spatial-index update, `sub_456B40`:

* **The mover** is stepped by `sub_454F40`, the walkers' own: the same lanes,
  keys, routes, occupancy lists and reservation groups.
* **The body** chases it. `sub_455D10` — the walkers' gait — is called with
  `unk_4C8888`, the **vehicle thresholds 195 / 390** where theirs are
  19.5 / 58.5. Gait 0 stops the vehicle (flag `0x100`) and resuming resets
  `+52` to 256; otherwise the speed accelerates `+256 * dt` to the record's
  cap of 5000 (19.5 units a frame ≈ 53 km/h) or brakes `-768 * dt` to 0 while
  blocked, and the body advances along the unit vector to the mover by
  `speed * dt / 256`. So a vehicle's `+52` is its live speed, which the gait
  then feeds the carrot's `+56` — for a walker `+52` is the clip's stride and
  never moves.
* **The 3D node** is placed at `(x, y - 30.75, z)` — 30.75 units above the
  body point, y being down.
* **It brakes for the player, and it runs him over.** While `dword_8F5E38` —
  which `Sliders_Tick` sets when the player's ground probe lands on a mesh
  named `X…` or `OP…`, the road — a vehicle within 195 units whose step would
  close the distance takes `768 * dt` off, floored at 256. And above
  **1706.6666** (6.67 units a frame), one whose spatial entry touches him
  raises event 43 with game message **17**, latched by `dword_538E20` for 90
  frames.
* **The sound** is `SOUNDS\sliderm01.wav`, loaded by `Slider_Init` and played
  3D at the body with its velocity, started inside **585** units and stopped
  outside; the handle is the record's `+20`.

### What the data says, and why the port shares one pool

* Keys, routes and steps are **partitioned by class** in all six shipped
  files — 0 overlap between the two lane ranges' — and **no vehicle key names
  an action point**, so a vehicle never enters the action phase the walkers
  have.
* The **reservation groups are shared**. Expanded through the group lists,
  **70** of Anekbah's groups are reachable from both a pedestrian and a
  vehicle route (60 Qalisar, 24 Jaunpur). That is how the two classes cross
  each other's lanes without meeting, and it is the one piece of network
  state a separate vehicle pool could not have. Measured over 1800 frames, a
  vehicle is held at a group a **walker** marked 2197 times in Anekbah, 1228
  in Qalisar and 440 in Jaunpur; give the two classes a busy counter each and
  all three fall to 0 while the frames in which they hold overlapping groups
  rise from 50 to 469 — a slider driving through a crossing.

### The viewer (step 3) — and what a person then saw

`omk-play` stages every vehicle of the pool beside the walkers, and it is a
far simpler body: no clip and no skeleton, so the chosen sub-object is
composed once at rest, re-centred on its own root (the four sit ~200 units
apart in model space) and only turned and translated per frame — to
`(x, y - 30.75, z)`, `sub_437F80`'s own placement, and to the heading
`sub_453330` built from the direction to its mover. The vehicle LOD reach is
`dword_4C8860[3]` = 1968.5, so a slider is still drawn well past the last
walker.

**Watched 2026-09-04**, which is the point of drawing it:
`omk-play --save traces/save-appart.bin --area 0 --stand 5620,0,-2400,270`
puts a **moto** across the street — a blue hoverbike with its rider leaning
onto the handlebars, nose leading, the red tail light behind — and
`--stand 5980,0,-3200,270` a **hover taxi** with its canopy, both riding
above the road rather than sunk into it. `verify.py: engine: traffic frame`.

**And the drawing found a bug that was latent for the CROWD.** The viewer
evicts from `charModels`, every frame, any model no *staged actor* wears, so
the 64 slots a bucket key addresses are not spent on the last area's cast.
The circuit's own bodies are not staged actors. The crowd never showed it:
a city's authored extras wear the same `PERSOS` models and kept them
resident by accident. `sli_fn` and `moto` are worn by nothing else, so the
traffic staged itself for two frames and then vanished while its cached
`CharModel*` went on pointing at a freed map node — `ready` reading false,
which is why it disappeared rather than crashed. Fixed by adding the
circuit's walkers and vehicles to that test.

### Still open

* **Which sub-object an ambient vehicle draws.** `sub_4544B0` hands traffic
  `v16[1]` — sub-object **0**, the heaviest — and the reserved slider
  `v16[2]`, sub-object 1, with the LOD chain over 1..3 either way. Read from
  the two call sites and **not yet judged by eye**; the port draws 0 and says
  so at the field.
* `sub_437220(obj, 0x400, 8)` on the sliders' objects.
* **The player's ride**: `sub_452570` / `sub_452CC0` (the mount),
  `Slider_TickRide` (0x00458150), `ACTOR_STATE` 7 and 8, and the
  `dword_8F5E44` hand-over that preserves his slider across an area load.
  A ride record in state 1..7 is left alone by the port rather than driven
  wrongly.
* **What one person has now seen, and what is still unwatched.** A moto and
  a hover taxi were watched crossing a street on 2026-09-04, which settles
  that they draw, sit on the road and lead with the nose — the last of those
  also held by a number, the models' 1.94 and 2.38 elongation along their
  own Z. What no still frame settles and nobody has watched in motion: the
  PACE against the original, the spacing down a lane, a vehicle taking a
  corner, one braking for the player, and whether a road full of them reads
  as traffic. That is exactly the class §1 of CLAUDE.md calls "verified
  standing still is not verified moving".

## 3. Mechanism C — the crowd push (the spatial index)

**Read and ported (2026-09-03, step 5: `engine/src/actor/spatial.*`,
`Session::crowdPush`/`talkToPedestrian`, `PlayerController::nudge`,
`engine/tools/push_probe`, `verify.py: engine: crowd push`).** `Actor_Attach` (0x0041CCA0) registers the actor in a
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

**The two tests, read in step 5.** `sub_45E390` (an actor entry) is sphere
against sphere: each side's node carries a list of spheres (the model's
`+244`/`+248`, taken as the meshes' bounding volumes of its first skeleton —
the list's writer was not traced), turned by the node's yaw and carried to
its position; an overlap pushes the player's centre along the line between
the two centres, horizontally, and every later pair tests the already-pushed
body. `sub_45E690` (an instance entry — every walker) is an **ellipse** in
the ground plane centred on the walker: two radii long along its heading
and one across, for each radius in its list; a hit pushes the player's
sphere by the whole penetration and the RESULT by a quarter of it, so a
walker shoves softly. Both write y = 0. And the reach box before either —
`max(|dx|,|dy|,|dz|) <= theirs + mine`, the two models' `+88` — clips the
ellipse's long axis at one radius plus the player's, so along a walker's
heading the push starts exactly where it starts across (`push_probe`'s
`shape` line: nothing at 30, 2.5 at x 20, −5 at z 30).

**Bump and talk.** `Sliders_Tick` reads each walker's entry flag 2 (touched
by the last query) and, with no bump pending, posts message 15 (a man) or
16 (a woman) with the player as sender, then holds 100 frames. The action
press runs `sub_452280` from the `.CTL` action state's move callback (cases
4/11 of the dispatcher at 0x46AEE2): the nearest walker within 117 units in
front, **standing at an action point in its main phase**, posts 13/14 and
becomes the talk target whose countdown is suspended. `IAM\GLOBAL`
subscribes all four (§2). The port posts through `Session::postMessage`,
so the "n° VO passant" lines run. Measured: a walker reaching the player on
its lane touches him at frame 57, moves him 103 units and bumps once in 150
frames; a walker at a point takes the talk and holds phase 2 for 200
frames. (Frame 58 and 106 units until 2026-09-04, over 200 index entries
rather than today's 240: the ROAD TRAFFIC registers in the same index, as
`sub_454860`'s `sub_45E040` does, and two corrections to the walk moved the
walker slightly — the `offBefore[2]` overrun in the port's `bodyStep`, and
the reservation groups the vehicles now share with it.)

## 4. What the port has today, and what "street life" needs

| piece | where | state |
|---|---|---|
| placement spawn, `ObjectShown` bit, show/hide | `Session::spawnFromTables` | done |
| scene programs on actors, path + clip, loops, sync | `SceneRunner`, `Program` | done, Anekbah included since 2026-09-03 (`engine: city crowd`) |
| drawing every shown actor by its program | `omk-play` (T20) | done for the Impasse's three bodies; a 30-body street never watched |
| zone lines to passers-by | zones + `voiceover` | done, never watched on a street |
| `character.look_at_player` head aim | `Session::looksAtPlayer`, `aimHead` (pose.h), the viewer | ported (`engine: head look`); to be watched |
| the `.OPT` pedestrians and the density option | `formats/opt.*`, `actor/pedestrians.*`, `Session::loadTraffic` | ported and run headless; drawing is step 4 |
| the crowd push | `actor/spatial.*`, `Session::crowdPush` | ported; the bump and talk messages post |
| the road traffic (§2b) | `actor/vehicles.cpp`, the vehicle masks in `Session::loadTrafficFor`, staged by `omk-play` | ported, drawn and seen (`engine: road traffic`, `engine: traffic frame`); the pace and the corners still unwatched |
| the player riding a slider | - | not ported: `Slider_TickRide`, `ACTOR_STATE` 7/8, `sub_456530` states 1..7 |
| a way to stand in a street without replaying the intro | `omk-play` | nothing |

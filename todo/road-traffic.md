# Road traffic — the plan and the progress

The **vehicle** half of the `.OPT` traffic circuit: the hover-taxis
(`sli_fn`) and the motos that share a city's lane network with the
procedural pedestrians. Opened 2026-09-04 from the request *"work on the
road traffic (sliders and kind of futuristic bicycle)"*.

`docs/STREET_LIFE.md` §2 listed the whole vehicle side under **"Not yet
read"** when the pedestrian work closed on 2026-09-03; the findings go back
into that file as a new §2b, and the port beside `actor/pedestrians.*`.

**Scope**: the AMBIENT traffic only. `Slider_TickRide` (0x00458150) and
`ACTOR_STATE` 7/8 — the player mounting and riding a slider — are the other
half of the ride pool and are explicitly out of scope for this pass; ride
slot 0 is read and described, not driven.

**Coordination**: two other sessions hold `engine/backends/sdl/play.cpp`,
`engine/src/actor/player.*` and `engine/src/script/area.*`. Nothing here
edits them. The Session hook and the viewer's draw are written as patches in
`todo/pending/` and applied once those land.

## Steps

| # | step | status |
|---|---|---|
| 0 | survey; the vehicle functions read out of the engine; this file | **done 2026-09-04** |
| 1 | port: the vehicle pool, the spawner and the drive, sharing the movers' network with the walkers; `engine/tools/veh_probe`; a check shown to fail | |
| 2 | the Session hook (`area.cpp`: AREA `+172`/`+174` into `load`), delivered as a patch | |
| 3 | draw them in `omk-play` (patch), then WATCH a city street | |
| 4 | docs: `STREET_LIFE` §2b, the RECONSTRUCTION row, `engine/README.md`, CLAUDE.md §4 | |

## What the engine does — read 2026-09-04

Every address below is from `Runtime 2.exe`; the decompiled bodies are in
`readable/src/18_d3d.c` and `19_dsound.c`.

* **`Slider_Init` (0x00453450), the vehicle half.** Gated on
  `header[2] < header[5]` — the area's circuit has vehicle lanes. It loads
  the two model tables with the AREA header's own masks, builds the ride
  pool, and calls the spawner.
* **The models are two files.** `sub_4539B0(aSliFn, pool, mask | 1)` and
  `sub_4539B0(aMoto, pool, mask)` walk 12-byte rows (an 8-char name and a
  weight) and load `MESHES\PERSOS\<name>.3do` for every set bit. The
  `sli_fn` table has **2 rows, both `sli_fn`**; the `moto` table has **1**.
  Both files ship. `sub_453A70` splits each into up to 4 root sub-objects
  sorted by vertex+face count, and — only for `sli_fn` — sets `0x400` on
  each with `sub_437220(obj, 0x400, 8)`.
* **The masks are AREA header fields**, `sub_40EA10` reading `+172` and
  `sub_40E9D0` `+174`, both **int16 sign-extended** (the crowd's are the
  dwords at +164/+168). Shipped: Anekbah 3/1, Jaunpur 3/1, Qalisar **1**/1,
  Lahoreh 0/0, the Puits 0/0 — exactly the three areas with vehicle lanes
  carry a nonzero mask, and the two without carry zero.
* **Slider row 0 is reserved for the player.** `sub_4544B0` treats ride slot
  0 specially (`v2 == v3`): it takes model entry 0 and marks the record
  `+22 = 1`, and `dword_8F5E44` keeps it across area loads. Ambient sliders
  are drawn from entries **1..n-1**, so Qalisar's mask of 1 leaves
  `dword_539934 - 1 == 0` and **every ambient vehicle there is a moto**.
* **The spawner** is the walkers' own `sub_453B40`, over lanes
  `header[2]..header[5]` with the callback `sub_4544B0` and
  `spacing = header[4]` — **no density factor**: the options row thins the
  crowd and not the traffic. The walk still multiplies by 39, so vehicles
  are `39 * vehSpacing` apart. At most **40** (`if (a1 >= 40) return 0`,
  which ends the walk).
* **A vehicle is a moto or a slider on a coin**, `rand() & 1` (1 slider),
  forced to the other when a pool is empty. The quota at model `+84` is the
  table's weight, **1**, and is never rescaled for vehicles (`sub_453E80` is
  the pedestrians' call only), so the first spawn of each kind spends it and
  every later one falls back to the first entry.
* **Mover flag `0x8` is not "pedestrian"** — `sub_453B40` sets it at line
  135134 of the listing (`or edx, 8`, beside the route assignment), so every
  mover the spawner places carries it, walkers and vehicles alike. It gates
  `Sliders_Tick`'s vehicle loop (`flags & 9`) and `Slider_Init`'s own
  speed-factor pass. `docs/STREET_LIFE.md` said "= pedestrian"; corrected.
* **The per-frame tick.** `Sliders_Tick` walks the 40 ride records and calls
  `sub_456530(ride)`, which switches on the record's `+8`: **0 is ambient
  traffic** and 1..7 are the player's mount/ride sequence. Ambient falls to
  the default: `sub_456C70` (the drive), the spatial-index update, then
  `sub_456B40` (the sound).
* **`sub_456C70`, the drive.** It calls the walkers' own `sub_454F40` to
  advance the mover — the same lanes, routes, occupancy lists and
  reservation groups — then paces the body toward it with
  `sub_455D10(mover, toMover, dist, unk_4C8888)`, the walkers' gait function
  with the **vehicle thresholds 195 / 390** (theirs are 19.5 / 58.5). Gait 0
  stops it (flag `0x100`); resuming resets the speed to 256. Otherwise the
  speed accelerates `+256 * dt` to the record's cap **5000**, or brakes
  `-768 * dt` to 0 while blocked, and the body advances along the unit
  vector to the mover by `speed * dt / 256`. The 3D node is placed at
  `(x, y - 30.75, z)`.
* **It brakes for the player and it runs him over.** While `dword_8F5E38`
  (the player's ground probe hits a mesh named `X…` or `OP…` — the road) the
  vehicle brakes by `768 * dt` down to 256 when he is within 195 units and
  the step would close the distance. And above **1706.6666** (6.67 units a
  frame), a vehicle whose spatial entry touches him raises event 43 with
  game message **17**, latched by `dword_538E20` for 90 frames.
* **`sub_456B40`, the sound.** `SOUNDS\sliderm01.wav` (loaded by
  `Slider_Init`) played 3D at the body with its velocity, started inside
  **585** units and stopped outside; the handle lives in the ride record's
  `+20`, which `Slider_Init` initialises to -1.
* **The ride record** is 24 bytes, 40 of them (`Mem_Calloc(0x28, 0x18)`):
  `+0` the mover, `+8` the state, `+12` the speed cap (5000), `+20` the
  sound handle, `+22` = 1 on the player's reserved slot.

## What the data says

* Keys, routes and steps are **partitioned by class** in all six shipped
  files — 0 overlap between the pedestrian lanes' and the vehicle lanes' —
  and **no vehicle key names an action point**, so a vehicle never enters
  the action phase the walkers have.
* The **reservation groups are shared**: expanded through the group lists,
  70 groups in Anekbah, 60 in Qalisar and 24 in Jaunpur are reachable from
  both a pedestrian and a vehicle route. That is how the two classes cross
  each other's lanes without meeting, and it is the one piece of network
  state the port cannot give the vehicles their own copy of.
* `SLI_FN.3DO` holds 4 root sub-objects — `SlBassin` (223v/413t, with four
  doors parented to it), `slider_fl` (127/250), `SlBasA` (63/122), `SlBasB`
  (26/48) — which is the LOD ladder `sub_453A70` sorts. `MOTO.3DO` holds
  one, `GEM0` (265/511). The vehicle LOD distances (`dword_4C8860`) are
  20/30/40/50 m against the crowd's 10/20/30/40.

## Open, and labelled as such

* **Which sub-object an ambient vehicle draws.** Traffic takes `v16[1]` =
  sub-object **0** as its base with the chain over 1..3; the player's
  reserved slider takes `v16[2]` = sub-object **1**. Read from the two call
  sites and not yet judged by eye — the port draws sub-object 0 and says so.
* **`sub_437220(obj, 0x400, 8)`** on the slider's objects: what the flag
  does is unread.
* The player's ride: `sub_452570` / `sub_452CC0` (the mount), the
  `dword_8F5E44` hand-over, `Slider_TickRide` (0x00458150) and the states
  1..7 of `sub_456530`.

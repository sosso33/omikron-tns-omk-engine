# Street life — the plan and the progress

The findings are in `docs/STREET_LIFE.md`; this file is the work. Opened
2026-09-03 from the request *"add the npc management on the streets of the
cities (model loading, behaviour, ...)"*, with one standing instruction:
**the crowd density is an option in the original** ("Niveau d'activité dans
les rues", options row 6, 0..4, default 3) — implement the setting in the
crowd code now, behind a default, and mark the default so the options menu
can take it over later. The menu itself is NOT part of this.

Each step ends with a commit and a report; the next starts on confirmation.

## Steps

| # | step | status |
|---|---|---|
| 0 | pull, survey, write `docs/STREET_LIFE.md`, this file | **done 2026-09-03** |
| 1 | the `scx.play` family's object word is RAW: fix `Interpreter::fetch16`'s callers for ops 46/57/58/59/60/90 after reading each handler, `dialog_disasm.py`, `tools/sim/vm.py`; correct SCRIPT_VM "Operand encoding"; a probe `engine/tools/city_crowd.cpp` and `verify.py: engine: city crowd` asserting per city how many extras start with actor, clip and path resolved (shown to fail first: Anekbah 0/26) | **done 2026-09-03** — the fault was signed-vs-raw in the port and the simulator (both already read the word raw, as int16), indirect only in the disassembler; 4 cities 26/27/29/34, all resolved |
| 2 | the `.OPT` format and the pedestrian spawner: read `Slider_Init`'s header use, `sub_453ED0`, and the tick (`Sliders_Tick` + the 200-slot walkers); a Python reader `tools/opt_track.py` with a self-checking walk (relocated offsets land inside the file, key counts land on the next lane, 6/6 files); `docs/STREET_LIFE.md` §2 rewritten from "read so far" to read | **done 2026-09-03** — `tools/opt_track.py` + `verify.py: opt tracks` (6/6, layout exact, refs resolve, runtime fields zero, 0 cross-class routes); the walk, the following, the reservation groups, the action points and the talk/bump messages read; the action's play phase and the vehicles left unread |
| 3 | port it: `engine/src/formats/opt.*` (the reader), `engine/src/actor/pedestrians.*` (the pool, the spawner with `streetActivity`, the per-frame walk), fed from the Session's area load; the density parameter lives in ONE place with the "replace with the options value" comment; checks: spawn count per level against the formula, a walker's path over a transition | **done 2026-09-03** — `formats/opt.*`, `actor/pedestrians.*`, `Session::loadTraffic`/`setStreetActivity` (default `kDefaultStreetActivity` = 3, TODO(options) comment), `tools/ped_probe`, `engine: pedestrians` (4 cities × 5 levels against the Python rule; the 600-frame invariants), shown to fail at 39 → 40 |
| 4 | draw them: `omk-play` stages the pedestrian pool like the extras (shared `CharModel` per model name, the walk clip at each walker's own phase and speed); add `--area N --address A` so a person can stand in a street; **watch Anekbah and Jaunpur**, file what is seen in `todo/omk-play.md` | pending |
| 5 | the crowd push: read `sub_45E390`/`sub_45E690`/`sub_45DFF0`/`sub_438040`, port `SpatialIndex` into the player's tick; invariant over a walk into a body, not a still frame | pending |
| 6 | confirm the interaction path in play (zone lines, head look-at); docs: RECONSTRUCTION log row, CLAUDE.md §4 row, `engine/README.md` coverage row | pending |

## Decisions and notes

* The density lives in the port as one integer `streetActivity` (0..4) with
  default **3**, the engine's own default (`sub_41F4C0` writes `0x01030000`
  into `dword_90E724`: byte 2 = 3). The comment at the definition says the
  options menu (row 6, hooks 0x48FC40/0x48FC70) is to replace it.
* Mechanism B needs no density: the engine starts every authored extra at
  any level. Only the `.OPT` walkers are thinned.
* Only four city streets have an `.OPT` (Anekbah, Jaunpur/souk, Lahoreh,
  Qalisar/qchaud) plus the Puits and the unnamed `biblio`; elsewhere the
  street is the authored extras alone.

## Log

| date | what |
|---|---|
| 2026-09-03 | Step 3. Two faults on the way, both from reading a name instead of the code: the `.ani` root keys are per-frame MOTIONS, not positions, and `Anim_RootDelta` turns their sum by the instance matrix - read as positions in the clip's frame every body ran off the map. The action's `+16` is a clip id, not a type, and the point's id is restored at the end. The one invariant that failed (a mover 1.15 off the network in Qalisar) is the engine's own overshoot at a corner, frozen by an action point. |
| 2026-09-03 | Step 2. The `.OPT` is seven (offset, count) blocks behind a 19-dword header, every one landing on the next; a route's steps are waypoints and the last leg is implicit, so the closure test I wrote first was wrong by design and became a class test (0 cross-class routes in 6 files). Messages 13-16 (talk/bump a walker) are subscribed by `IAM\GLOBAL`, so the port's message path already serves them. |
| 2026-09-03 | Step 1. The object word of every `scx.play*` is raw and unsigned; the port and `tools/sim` compared it signed against `handle >> 16` (so 0xC2xx never matched), and the disassembler ran it through the indirect rule. One mask in `SceneRunner::handle`, one in `run.py`, `RAW_WORD` in `dialog_disasm.py`; `engine: city crowd` (slow) pins four cities; SCRIPT_VM's "all 59 indirect operands" now says which fields it counts. |
| 2026-09-03 | Survey. The first reading ("no crowd AI, it is scene programs") was right and incomplete: the density option led to `Slider_Init`, which spawns a second, procedural population from the `.OPT` lanes. The enumeration trap again — a search for what drives an NPC found the actors and missed the pool that is not an actor at all. |

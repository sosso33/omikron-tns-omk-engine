# Street life — the people on the city streets

How *Omikron* populates a city street, read from `Runtime 2.exe` on 2026-09-03
and measured against the shipped data. Written before the port of it was
started; `todo/street-life.md` carries the plan and the progress, and every
"not yet read" below is a claim still waiting on its evidence.

There are **three** mechanisms, and only one of them is what the first survey
found. In order of what a player sees:

| mechanism | what it is | port status (2026-09-03) |
|---|---|---|
| **A. the procedural pedestrians** — the `.OPT` "trajectoires" | anonymous walkers spawned along authored lanes, density from the options menu | **not ported; format not yet read** |
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

**Located and partly read; the file format and the per-frame tick are NOT yet
read.** This is what the options menu's density row drives, and it is what
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
not yet attributed.

The byte has **one consumer**: `Slider_Init`.

### `Slider_Init` (0x00453450) — the area's traffic

`Area_TickLoad` case 8 appends `.OPT` to the 9-byte name at AREA `+115` and
`Area_LoadSliderTrack` (0x0041B420) hands `TRAJECTOIRES\<stem>.OPT` to
`Slider_Init`. Five areas name one and all five ship — `anekbah` (85 KB),
`souk` (Jaunpur, 33 KB), `lahorey` (23 KB), `qchaud` (Qalisar, 57 KB), `puit`
(Anekbah CS Puits, 2 KB); `biblio.opt` ships unnamed. So the procedural crowd
exists in **exactly four city streets**; every other area has only mechanism B.

What `Slider_Init` does, read so far:

1. `File_LoadWhole` the file; the header's dwords `[6] [8] [10] [12] [14] [16]
   [18]` are file offsets relocated to pointers. `[1]..[2]` is a range of
   lanes, `[3]` a spacing unit, `[5]` another lane bound, `[9]` a count that
   sizes a 48-byte pool (`dword_539928`). The **lane table** at `[6]` has
   24-byte entries: origin `float[3]` at +0, a link at +12, `int16 +16` the
   first key, `int16 +18` a path base, `int8 +20` a lane count, `int8 +21` the
   number of keys. Keys are 20 bytes at `[8]`: `[1..3]` a delta `float[3]`
   (accumulated, so a key is a step along the lane). The block at `[12]` is
   12-byte records the pedestrians pick a **path** from. *The rest of the
   header is unread.*
2. Two pools: 240 slider slots of 192 bytes (`dword_53830C`) with 240 list
   nodes, and — only if the area has pedestrian lanes and a walk clip exists
   — **200 pedestrian slots of 72 bytes** (`dword_8F5E94`, the `14400` loop
   bound).
3. The pedestrian **models** come from the character pool: `sub_4539B0` walks
   a 12-byte-stride name table starting at `"PSH_FN"` (men) and one at
   `"FSH_FN"` (women), loading `MESHES\PERSOS\<name>.3do` for every bit set in
   the mask `sub_40E990(area)` / `sub_40E950(area)` returns — so which
   men and women a city uses is per area, and Jaunpur's `KSH_FN`/`KWH_FN`
   are expected there. `sub_453E80(pool, n, 100)` rescales each model's weight
   at `+84` so they sum to 100 (a spawn percentage), `sub_453910(pool, 0,
   table)` builds each model's four-clip animation list from clip **types**
   (walk is type 9: `List_PickRandomByType(bank, 9)` gates the whole
   pedestrian half). `"sli_fn"` and `"moto"` are the vehicles, from the same
   loader with flag 1024.
4. **The spawner, and where density enters** — `sub_453B40(lanes, callback,
   laneTable, spacing)` with

       spacing = (5 - streetActivity) * header[3]

   walks every lane's keys, accumulating the key lengths, and every
   `39 * spacing` units of lane calls the callback (`sub_453ED0`, 169 lines,
   **unread**) to allocate one pedestrian: position at the key, direction the
   key's unit delta, a "previous" point 117 units back along it, the lane at
   `+72`, a path record from `[12]` chosen round-robin over the lane's count
   at `+76`, flag `0x8`. Level 4 packs walkers 39·h3 apart; level 0 five
   times sparser. Each then gets a **speed factor** `1 + (5 - rand()%10)/20`
   (0.75 … 1.20) times `sub_453D80`'s clip-derived stride, so the crowd does
   not walk in step.
5. `SOUNDS\sliderm01.wav` is loaded for the vehicles (`docs/ASSETS.md` §1).

**Not yet read:** the callback (which pool slot, which model by the
percentages, the instance's node), the per-frame tick — `Sliders_Tick`
(0x00454BB0, 150 lines) and the 18_d3d.c functions around it that walk the
200 slots (`sub_452280`, `sub_452570`, `sub_452CC0`, `sub_454F40`,
`sub_455830`) — how a walker follows its path, turns at a lane's end,
respawns, and whether it is ever in the spatial index. The whole module is
41 functions, 0x004521E0..0x00455E90, all RAW but the two named ones.

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
| the `.OPT` pedestrians and the density option | — | nothing |
| the crowd push | — | nothing |
| a way to stand in a street without replaying the intro | `omk-play` | nothing |

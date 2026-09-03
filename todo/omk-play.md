# omk-play — open issues

The viewer (`engine/backends/sdl/play.cpp`). It is an INSTRUMENT, not a slice
of the port (CLAUDE.md 5), but what it fails to draw is what a person reports,
so its gaps are filed here in the same four-part shape as the script engine's.
Numbering continues from `iam-script-engine.md` so a number names one thing.

**A viewer fix is not done until somebody has WATCHED it** (CLAUDE.md 1: "a value
verified standing still is not verified moving"). 42 and 43 are the case for the
rule rather than an illustration of it: both passed the suite, and 43 shipped
TWICE in two different wrong states - a T-pose, then a frozen mid-stride - each
caught only by a person walking into the alley and looking. So an entry here says
either CONFIRMED IN PLAY or it does not, and one that does not is a claim still
waiting on its evidence.

## Fixed (batch 4, 2026-09-03)

### 42. A scripted camera shaped like the follow camera re-aims the follow camera — A

> **Fixed 2026-09-03, CONFIRMED IN PLAY.** `followCam` now also requires
> `!session.playerAnimHeld()`, so a held player hands the camera to the script and the
> requested shot resolves as a fixed camera. Walked into the alley and watched: the
> three shots hold. `verify.py: held camera bracket`.

Filed 2026-09-03 from a play report — *after the Impasse cutscene, going into
the small way, the text appears but the camera stays in adventure mode.*

**What the engine does** — `camera.set` (95) and `camera.set.wait` (96) issue
`Camera_Request` mode 12 and the request owns the camera. The follow camera
is `sub_415D10`/`sub_415E60`, and `sub_415D10` opens
`if ((u32(a1,356) & 0x81) != 0) { v2 = 0; v13 = 0; }` — while the player's
channel is HELD its camera mode is forced to 0. The scripts bracket every
staged sequence with `player.anim.hold`/`release` (SCRIPT_VM 104/105), so
"held" is the engine's own signal that a script is driving the camera.

**What the port does** — `play.cpp` decided *this is the follow camera* from
the camera's SHAPE alone:

    followCam = hc && !hc->absolute() && hc->eyeSubject == 0 && hc->atSubject == 0;

AREA 222's tutorial (zone 3795, script at 2354) names cameras 4290, 4291 and
4292, and **all three are `eyeSubject 0, atSubject 0` — identical in shape to
camera 0**. So each shot passed that test, was handed to
`player->setCameraOffsets(...)`, and re-aimed the follow camera: it kept
trailing the player with the follow lag instead of standing as a staged shot.
Nothing was ignored, which is why it looked like the camera "never left
adventure mode" rather than like an error.

**How established** — the script disassembles to 16 instructions holding at
pc 2360 and releasing at 2414 with all three `camera.set`s between them; the
chunk's camera table (base +64, count +84, stride 44, subjects at +32/+34)
gives `(0,0)` for 4290/4291/4292 **and** for camera 0. Both halves are
asserted by `verify.py: held camera bracket`, which fails readably when the
decode desyncs.

**Severity A** for the viewer: a whole authored sequence is invisible.

### 43. `player.anim.hold` drew the REST pose, so the player T-posed — A

> **Fixed 2026-09-03 on the third reading, CONFIRMED IN PLAY** - he walks to a halt
> and stands through the held sequence. The controller is now TICKED while
> held, with no input, and the pose comes from the channel exactly as it does
> unheld. Two earlier attempts were wrong in opposite directions and both
> shipped: `composePose(meshes, {}, 0)` (the rest sentinel) drew a T-pose, and a
> latched pose froze him mid-stride with one leg forward - reported from play
> both times.
>
> What settled it: `sub_45A870`'s two writes are `queue[0] = 0x40000000` and
> `n = 1`, and those arrays are the channel's INPUT QUEUE and LENGTH -
> `Perso_InjectInput` (0x0045A9F0), already NAMED, fills exactly them from its
> own arguments. `Cef_TickChannel` re-asserts both every tick while
> `flags & 0x81`, and the queue rule DROPS a lone idle word (ASSETS). So a held
> channel runs on with nothing pressed and the device cut off, which is what
> carries a gait to its stand state: he walks to a halt and stands in the bank's
> idle. `play.cpp` was not ticking the controller at all while held, which is
> why he kept the frame he had.
>
> **The lesson** is CLAUDE.md 1's, one level in: both wrong readings were
> consistent with the two writes in front of me. Only a THIRD user of the same
> fields could separate them, and it was five lines further down the same file.
> Read the other users of a field before naming it.

Filed 2026-09-03 from the same report — *the character is in t pose*.

**What the engine does** — `Actor_HoldAnimation` (0x00468DA0) is two calls and
**neither touches a transform**: `Perso_SetInputEnabled` (bit 0x80) and
`sub_45A870` (bit 0x01). `sub_45A870` sets the channel's blend count to 1 and
weight[0] to `0x40000000`, and `Cef_TickChannel` (0x004A8160) re-asserts
exactly those two every tick while `flags & 0x81`. A blend collapsed to a
single entry at full weight is the pose the body already had — a freeze.
`Actor_EnterDialogueMode`'s own comment says it from the other side: *"a held
channel never plays the group-400 stance, so an scx scene clip keeps the
body"*.

**What the port does** — it composed `composePose(meshes, NodeTracks{}, 0)`,
i.e. empty tracks at frame 0. Frame 0 is the **rest sentinel** (CLAUDE.md 5:
*"animation key 0 is a rest sentinel, not frame 0"*), so the player was drawn
in his bind pose for the whole of the held sequence.

**How established** — the two functions above, read end to end, plus the
0x81 test sites: `sub_415D10` and `sub_415E60` (the follow camera) and
`Cef_TickChannel`. None of the three pins a transform to rest.

**Severity A** for the viewer, and it also **corrects the docs**:
`docs/SCRIPT_VM.md` 104/105 said the update "pins the transform back to rest
every frame", and `engine/src/script/area.h` said `Cef_TickChannel` "skips its
whole input pass". Neither is what the code does — the tick re-pins the blend
and carries on. The SCRIPT_VM entry already labelled itself *"the weakest name
in this batch"*, which is where the next reader should have started.

### Not a bug: the tutorial's one-shot — investigated 2026-09-03

The third symptom of the same report — *this tuto scene could save something,
so it is not triggered each time* — is **already correct**, and is recorded
here so nobody re-opens it.

The mechanism is not a variable, which is what it looks like from outside.
AREA 222 record 5 **is** zone 3795, and its script's last act before `end` is
`zone.disable 3795`: it switches off the zone that triggered it. That sticks
because op 64/65 writes one bit of the persistent game DB
(`state.setBit(ZoneState, operand & 0x7FFF)`) and registration reads the same
bit (`zones.cpp` gates on `state.bit(ZoneState, z.stateBit())`, and `world.h`
defines `stateBit() { return id & 0x7FFF; }`). Both mask 0x7FFF because bit 15
is the record's one-shot flag, which `Zone_StateBit` (0x0040D500) masks away —
3795 does not carry it, so this zone is one-shot by *script*, not by flag.

**How established** — `zone_probe` with camera waits ON shows the script park
twice and reach `end` (`[camera.wait] [camera.wait] [end]`), and its touches
saturate at 273 and never resume at 400, 700 or 1200 settle frames: the
disable takes effect the moment it runs. `walk_zone 3795` reports
`registered=1, 1 scripts ran, save bit 1 -> 0, re-registers=0`.

Pinned by `verify.py: tutorial one-shot`, which asserts the run AND that the
writer's mask, the reader's mask and the registration gate still agree. The
mask half is not redundant: for id 3795 a wrong mask still lands on the same
bit (`3795 & 0x3FFF == 3795`), so a drift would pass the runtime rows and
only bite zones with an id at or above 0x4000. Shown to fail by narrowing the
writer's mask.

## Fixed (batch 3, 2026-09-03)

### 41. One body is staged: every character but the first shown collapses into it — B

> **Fixed in batch 3, 2026-09-03 (T20 + two follow-up fixes).** RENDERED (the Impasse
> arrival, frames 700/900); `docs/RECONSTRUCTION.md` 2026-09-03. Every shown character is
> a `Staged` body posed by its own driver. Two faults T20 delivered open were closed: a
> path-less program snaps the body to its clip ROOT key 0 (not its far placement record),
> and the program's placement is re-asserted each frame so a `fromTable` reset cannot walk
> it 800 units past the camera. Still labelled: the idle is a still frame 0, no per-actor
> `.CTL` tick, no look-at, the lone-program fallback, the 100-unit floor cut-off, and
> `PA1_FN` has no drawable body in the shipped data.
Filed 2026-09-03 from the same report as `iam-script-engine.md` 40. Recorded
on 2026-09-02 (RECONSTRUCTION, "the ~5 second beat") as *no second speaker is
staged* and never filed.

**What the engine does** - every attached actor is an object in the o3de
tree (`Actor_Attach`, 0x0041CCA0) and `Render_Scene` draws them all; a scene
program drives ITS actor (`ScriptObject_StartOnActor`, 0x0041BA80 - the
`Started.actor` the port already records), and an actor no program drives
stands in its `.CTL` bank's default state (`Actor_LoadModel` ->
`Actor_LoadBankList`, state 1 on the default group - the rule
`PlayerController`'s constructor quotes).

**What the port does** - `play.cpp` keeps ONE `speaker*` set: the model is
`session.shown().front()` (or the conversation's speaker), and its pose is the
clip of the LAST running `"actor"` program whichever actor that program
drives (the loop over `sc.started()` near "int sceneClip = -1"). So in the
Impasse only Kay'l draws, and while `A_2_DemonLook` runs his one body can
wear the Demon's clip.

**How established** - the code above, and the run of 2026-09-03: the alley
frames show Kay'l alone against SCENE 55's cast of three programs' actors
(Kay'l 49, the Demon 57, the Meca 58 - `A_1_KaylArrives`, `A_2_DemonLook`,
`C_1_MecaComes`) and AREA 222's three attached passers-by.

**Severity B** for the port (nothing a script decides changes), but it is the
first thing a person sees.

**Fix shape (T20 in the plan)**: a `std::vector<Staged>` keyed by actor id
in place of the `speaker*` set - geometry, textures, meshes, face, and a pose
SOURCE resolved per frame: (a) the running scene program whose
`Started.actor` is this actor (clip + path + offset, the existing code moved
into a per-character function), else (b) the conversation line's `.3DM` when
this actor is its speaker (the face too), else (c) the idle: the bank's
default state clip at frame 0, stood at the placement's pos/facing seated on
the walkable floor. The texture pool becomes `worldTex + each staged
character's textures` with a per-character material base, the same scheme as
`worldTexBase`. In adventure mode the controller's body stands in for the
player's id. Confirmed by RENDERING (a frame during `A_2_DemonLook` with the
Demon at 7605 -80 2980 and Kay'l both drawn) and by the summary line
counting staged bodies; the headless half is 40's check.

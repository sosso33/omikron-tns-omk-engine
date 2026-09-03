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

## Open (batch 5, filed 2026-09-03)

Six filed together from one play report of the Impasse cutscene and the
adventure mode after it. The reporter's framing is worth keeping: *it is a list
of immediately visible issues, and they look like issues that will happen many
times if not fixed* — so each is a CLASS, not a single shot, and the entry says
what the class is.

### 45. Camera ROLL is not applied — A

> **Fixed 2026-09-03. NOT yet confirmed in play.** `RCamera::rollDeg` added and
> applied in `basisOf`, so both backends get it (they share `cameraBasis`).
> The sense is derived from the engine's own `sub_441FF0` through
> `headingMatrix`, whose columns are this basis: 30 of 30 direction/roll pairs
> agree to 1.2e-7. 224 of the 1073 editing cameras carry a roll.
> `verify.py: engine: camera roll`, shown to fail (30 -> 6) with it dropped.

Filed 2026-09-03 from a play report — *when we were working on the web cutscene
viewer, you fixed a bug where rotation around the camera forward vector were
not applied. This bug is still in the c++ engine.*

**What the engine does** — a camera carries a roll (`Cam_PlayEditing` keyframes
`position, target, roll, fov`; a world camera's is `Global_Load`'s
4096-per-turn integer converted by 360/4096). Roll is a rotation about the
FORWARD vector, applied when the view basis is built.

**What the port does** — TO BE ESTABLISHED: whether `o3de/` builds its view
basis with an up vector fixed to world up, which drops the roll silently.

**How it will be established** — the web viewer's fix is the precedent
(CLAUDE.md 5: `W(v) = [x, -y, z]` is a reflection, so the roll's SIGN flips
when leaving the game's space, and `/cutscene` must negate it). The corpus has
the numbers: 2725 of the Bowie title sequence's 4370 frames carry a roll, and
`verify.py: camera roll` already asserts that no camera move rolls past 90°.

**The class** — every rolled shot in the game, not one.

### 46. A shot near the start of the Impasse has a RED FILTER and draws without it — A

> **Fixed 2026-09-03 as the FADE FAMILY, and the premise was half wrong. NOT
> yet confirmed in play.** The Impasse's own opening fade is **white**, not
> red: 118/119 pack their colour as a DWORD out of four operand bytes, so
> `FF FF FF 00 19 00 00 00` is 0x00FFFFFF over 25 frames, and the four-int16
> view `-1, 255, 25, 0` that made it look red is the disassembler's, not the
> engine's. The RED in this family is the engine's own override - a fade
> issued by the message-0 handler is forced to 0xFF0000 - which `area.h`
> already tracked with nothing to consume it. Both fades are now ported with
> their refusal rules, the 60-frame black one, the linear ramp and the "a `to`
> HOLDS" rule; the three I2D quad blends are NOT traced and the mix is
> labelled a model. `verify.py: engine: fades`, shown to fail.
>
> **So whether the reported shot now looks right is still open** - if it is
> red in the original it is a message-0 fade, and which handler issues it has
> not been attributed.
>
> **REGRESSION, found in play and fixed the same day.** The first version had
> the BLACK fade's two states backwards, and a reader saw the consequence at
> once: *I can see the scene for 1 or 2 seconds at the beginning of each
> cutscene and dialog scene, then a fade and the screen becomes black.* Two
> seconds is 60 frames, the black fade's whole duration. `fade.to_black`
> (opcode 132) is the FIRST thing SCENE 55 does, at pc 1228, and its partner
> is at 1395 - past every waiting beat - so a fade that painted black there
> held for the entire cutscene.
>
> It does not paint black: the ticker (0x00452065) draws a grey quad that
> MULTIPLIES the frame, and in state 3 that grey is `clock * 255 / duration`,
> RISING - black to untouched. **State 3 is the fade IN and state 4 the fade
> OUT, the opposite way round from the opcode names**, which is also why 133
> refuses unless 3 is running: you cannot fade out without having faded in.
> `verify.py: engine: fades` now asserts the direction from both ends - the
> fade in starts fully black and ends clear, the fade out the reverse - and
> the inverted reading fails it.
>
> The lesson is the one CLAUDE.md 1 keeps: the ramp, the durations and the
> refusal rules were all read correctly out of the handlers and all pinned by
> a passing check, and the DIRECTION - the one thing no still frame and no
> corpus count can show - was wrong. It took somebody watching.

Filed 2026-09-03 — *in the impasse cutscene, one plan, near the begining, is
with a red filter, but it appears without it.*

**What the engine does** — SCENE 55's startup script runs
`fade.from_color -1, 255, 25, 0` at pc **1229**, immediately after
`fade.to_black` and before the first beat. 255/25/0 is the red. So this is the
fade family (SCRIPT_VM), not a render mode.

**What the port does** — TO BE ESTABLISHED.

**The class** — every `fade.*` site in the game; the opcode family is used
throughout, not just here.

### 47. Environment-triggered particle effects are not drawn — A

> **Fixed 2026-09-03, and the report had TWO answers. NOT yet confirmed in
> play.** The STANDING set pieces (section E keyed `(1, -1)`, which no object
> start can reach) were already bound by `attachSfx`. The missing family is the
> one bound to GEOMETRY: `Sfx_BindAmbientEffects` walks the resident `.3DO`'s
> meshes and, for every one flagged 0x40000000, compares the first FOUR RAW
> BYTES of its name against each section-D tag, registering that binding's
> effect at the mesh's own position. The port had read all three files - the
> flag, the binding, `ParticleField::add` - and nothing walked the set.
> `SceneRunner::bindSetEmitters`, called when a world slot loads its set:
> **319 emitters over 12 sets**. The compare is on the RECORD's bytes, not the
> parsed name, because `readMeshes` truncates at the first null.
> `verify.py: engine: set emitters`.
>
> **CONFIRMED IN PLAY 2026-09-03**: the running game logs
> `world: slot 1 AIMPASSE binds 3 ambient emitters` on entering the Impasse.
>
> **Two caveats, both recorded rather than smoothed over.** The count is 319
> where `docs/ASSETS.md` quotes 321 from `tools/ambientfx.py`, which filters
> harder; the difference is two and unexplained. And the corpus walk pairs a
> `.SFX` with the `.3DO` of the SAME STEM, which is **not** how the game pairs
> them - `attachSfx` takes the resident SCENE's file and `bindSetEmitters` the
> AREA's `+97` set, so the Impasse is `Impasse.sfx` against `AIMPASSE.3DO`, a
> pair no stem sweep produces. This entry first said "the Impasse has 0" on
> the strength of that sweep and the running game said 3. **319 is a floor,
> not a total**, and the check now carries the Impasse's real pairing beside
> it.

Filed 2026-09-03 — *please apply particles effects triggered by the
environnement, and not only the ones triggered by the scene.*

**What the engine does** — two families own the instances (ASSETS 3b):
`Cef_TickEffects` (character effects, from the `.CTL` state records) and
`Sfx_TickAmbient`, the real particle emitter, which is the one bound to
GEOMETRY — mesh flag `0x40000000` → name → `.SFX` section D → section C effect
→ emitter. **321 of 579 flagged meshes bind**, `neon` 102, 153 of them in
Anekbah.

**What the port does** — `o3de/particles.*` and `setpiece.*` exist and are fed
by `SceneRunner`'s object starts (section E, keyed to an object START event).
The ambient/standing family — section E rows keyed `(1, -1)`, which no object
start can fire — is TO BE ESTABLISHED.

**The class** — every set with ambient emitters; 12 sets, 321 emitters.

### 48. Some Impasse particles draw incorrectly — B

> **STILL OPEN 2026-09-03, but narrowed.** Four things ruled out cheaply, so
> the next pass does not repeat them:
> * **the sprites all resolve.** The Impasse's 26 effects name sprite ids
>   13, 114, 139, 140 and 200, and `Impasse.SCX`'s chunk 4 registers all five
>   (12 sprites: 13 14 114 115 116 117 118 121 139 140 143 200). The port
>   already keys by ID rather than index, which is the trap `play.cpp` records
>   ("indexing a library instead lands on the wrong sprite ... which is why the
>   portal came out fire-orange").
> * **every one of the 26 effects is blend mode 4**, so a mode mix-up cannot
>   explain "some are wrong and some are right".
> * **all 15 set pieces are object-keyed** (to 221, 259, 223, 94) and
>   `standingPieces()` is 0, so they only appear during their beats.
> * **the set binds 0 mesh-flag emitters** (issue 47), so nothing in this
>   scene comes from the environment family.
>
> And three more from a second pass over the DRAWING, which is where the first
> pass said the answer had to be:
> * **the quad's size is right.** ASSETS 3b's rule - a particle's size is the
>   sprite quad's own, not the effect's `scale` - is what `particleGeometry`
>   does: `base = sf->extent[frame]` and the effect's scale multiplies it as an
>   instance scale.
> * **the blend is right.** All 26 effects are mode 4; `spriteModeBits`
>   transcribes `Render_SubmitSprites`' switch verbatim (4 -> 0x2100) and
>   `blendOfMode(4)` is `Blend::Add`, no cutout - additive, which is what smoke
>   and neon want.
> * **the frame walk is right**: `(frames - 1) * age / life`, from the age
>   BEFORE the tick's increment.
>
> So seven things are ruled out and nothing cheap is left. What remains is the
> billboard basis, the colour ramp's application, the depth/order against the
> set, or something only a frame can show - and this file's opening rule is
> that a viewer fix is not done until somebody has WATCHED it. This one cannot
> even be diagnosed without that.


Filed 2026-09-03 — *some the particles effects of the impasse cutscene are
visible but does not render correctly.* Needs LOOKING at before it can be
filed properly; the likely axes are the blend mode (additive `0x1000|0x2000`
vs multiply `0x1000|0x4000`, ASSETS 4), the quad's own size (ASSETS 3b: a
particle's size is the sprite quad's, not the effect's `scale`), and the
frame walk.

### 49. The adventure-mode camera sits too low — A

> **Fixed 2026-09-03, and it SETTLES an open item. NOT yet confirmed in play.**
> `player.h` recorded "whether the subject position +244..+252 is the FEET or
> the pelvis is not settled by this read". It is the PELVIS, and the preset
> table is what says so: mode 0 offsets the eye by (0, 0, -118.11) and the
> TARGET by (0, 0, 0), so a third-person camera whose eye and target both sit
> at the subject's own height only makes sense if that subject is a BODY point.
> `pos()` is a floor point - the walker keeps it there - so the camera sat on
> the ground.
>
> `PlayerController::cameraLift()` is the model's hierarchy root (the pelvis,
> `parent < 0`) above its lowest extent: **41.9** for `HO1_FNM`, which is the
> **41.8** the dialogue staging measured from the other side months ago and in
> a different context. The camera's subject is raised by it.
> `verify.py: engine: player walk` now asserts the camera's height above the
> floor point equals that lift, and reads (0, 4189) with the lift removed.

Filed 2026-09-03 — *the camera on adventure mode is set too low, could you
check how the original engine set the adventure mode camera, and reproduce it.*

**What the engine does** — the follow camera is `sub_415D10`/`sub_415E60`, and
the **camera-mode presets** are a compiled table (CLAUDE.md 4, the actor
runtime row; `tables/camera_presets.json` is lifted from the exe). `MDCAMADV`
is the special move that installs adventure mode.

**What the port does** — TO BE ESTABLISHED; `play.cpp` has its own follow
camera and `actor/player.h` a `setCameraOffsets`.

**The class** — the whole of adventure mode, which is most of the game.

### 50. Scene animations that should fire at a point do not — A

> **Fixed 2026-09-03: the timing AND the movement.** NOT yet confirmed in play. `Script_MoveObjectOnPath` addresses a path in TWO parts -
> param 1 the `.3dp` file, param 2 the path inside it - so `ScxPath` now
> carries `file`/`index` and `pathIn(file, index)` resolves it; a flat index
> lands on another file's path in any scene with more than one. Its busy
> window is **param 6**, the playback length in frames - and that was a
> CORRECTION to this entry's own first fix, which took the path's duration and
> gave 146. The handler advances `t` by `duration * dt / param6` and stays busy
> while `t < duration`, so the path's frames are played ACROSS param 6:
> `C_1_BoxMoves` runs **111** (param 6 = 110.01) where a window of 0 gave 40.
> The corpus is unanimous - **4837 of the 4841** sites author a non-zero
> param 6, and the commonest values are 36, 45, 120 and 180: round frame
> counts, 1.2 s / 1.5 s / 4 s / 6 s at 30 Hz. The data names itself twice
> over - the four paths are `CaisseA`..`CaisseD` and the four node names in
> the object's own string table are `Caisse01`, `Caisse1`, `Caisse 13`,
> `Caisse 14`, one per crate. `boxblow` holds 185.
> `verify.py: engine: scene steps`.
>
> **And the MOVEMENT is done too.** `o3de_SetNodePos(node, x, y, z)` places the
> node at the path sample OUTRIGHT - absolute world, not a delta - and param 0
> is an index into the object's own first string table, which is how a scene
> names a mesh of the resident set. `Program::motions()` reports them,
> `SceneRunner::motions()` collects them, and `play.cpp` offsets that mesh's
> corners by (target - its authored position), keyed by `cornerMesh` and
> patched from a kept copy of the ORIGINAL corners so the offset cannot
> accumulate.
>
> The data confirms the shot: **4 nodes move, all 4 resolve to meshes of
> `AImpasse` by name, and 2 of the 4 FALL** - `Caisse01` from y -198.9 to
> -118.7 and `Caisse 13` from -199.2 to -122.3, about 80 units down, while
> `Caisse1` and `Caisse 14` start on the ground and slide. That is crates
> falling. `verify.py: engine: scene steps`, shown to fail (4/4/2 -> 0/0/0).
>
> **Still NOT confirmed in play**, and one thing is deliberately not modelled:
> the path key's QUATERNION. `Path_Sample` returns one and the handler passes
> it on, but nothing here rotates the mesh - a falling crate translates and
> does not tumble.

Filed 2026-09-03 — *inside the cutscene, some animations are supposed to be
triggered at some points, like the crates falling, but nothing happens.*

**What the engine does** — the crates are `Impasse.SCX`'s `C_1_BoxMoves`,
whose program is four `Script_MoveObjectOnPath` (0x03000008) functions and a
`SelectBodyAnimation`, with the editing `boxblow` (185 frames) over it.
`Script_MoveObjectOnPath` moves a scene node along an authored `.3DP` and is
busy for the path's duration.

**What the port does** — `Program::busySpan` returns **0** for
`kFnMoveObjectOnPath` ("a path's duration belongs to the walker and is not
modelled here"), and nothing MOVES the node. So the object runs 40 frames
against its editing's 185 and the crates never move. It is the single largest
remaining gap in the scene interpreter: **4841 uses**, the most-used script
function in the game.

**The class** — every scripted object motion in every scene: doors, lifts,
crates, vehicles.

## Fixed (batch 7, 2026-09-03)

### 53. `MoveObjectOnPath` dropped the ORIENTATION, so anything that turns in place stood still — A

> **Fixed 2026-09-03.** The path sample now carries the key quaternion
> (`pathSampleQuat`), `NodeMotion` carries it, and the viewer rotates a mesh's
> corners about its authored origin before placing them.
> `verify.py: engine env anim` asserts the fan turns and does not move.
> **CONFIRMED IN PLAY** - the Impasse's fan turns.

Filed 2026-09-03 from a play report — *the environnement animations (like the
impasse "fan") are still not working*, after issue 52 kept such programs alive
across a cutscene and the fan still did not turn.

**Where the fan comes from.** `Impasse.SCX` has 36 objects and exactly ONE
with loopCount -1 — object 4, handle **20, `Ventilo`**, whose functions are
`MoveObjectOnPath` plus sounds. SCENE 55's sixteen beats never name it. It is
started by the **AREA's** startup script, whose first instruction is

    2276  scx.play  20, 0, 0

before it even picks the music. That is what "managed by the environment and
not the cutscene" is, mechanically: the AREA `+4` script starts it, and it
loops for ever.

**The port already started it** — object 20 running, 0 missed — so issue 52
was necessary and not sufficient. The node it drives is `Epale20`, a fan
BLADE, and the probe showed the path being sampled correctly (`t` advancing
6.31 → 29.43) with the position **identical every frame**:

    f0   node 'Epale20'  t=  6.31  pos 6678.04 -316.59 3426.18
    f11  node 'Epale20'  t= 29.43  pos 6678.04 -316.59 3426.18

A fan spins in place. Its animation is entirely in the ORIENTATION, and the
port was throwing that away.

**What the engine does.** `Script_MoveObjectOnPath` (0x0046F400) sets position
AND orientation in the same breath. `Path_Sample`'s **sixth parameter is an
out 3x3**, and the handler hands it to `sub_437160(node, m)`:

    result = *a1;  *(uint32_t *)*a1 |= 0x80000u;   /* the node's dirty flag */
    qmemcpy(a1 + 14, a2, 0x24u);                   /* 36 bytes = a 3x3, node +56 */

right beside the `o3de_SetNodePos`. It even composes a further Euler rotation
into it (`Matrix3x3_FromEulerAngles` and two matrix multiplies) on the branch
that asks for one. The port's `pathSample` returned position only.

**The fix.** `pathSampleQuat` interpolates the key quaternions over the same
span the position uses, with the short-arc sign fix and normalisation. A
`Mesh` record carries no orientation of its own — only `pos` — so the node's
matrix starts as identity and the sample's applies directly: a corner is
rotated about the mesh's authored origin and then placed at the sample, which
with an identity quaternion is exactly the offset the old code applied. The
existing crate motion is unchanged, and `engine: scene steps` still passes.

Measured: over twelve frames the fan's position spread is **0.0000** and its
quaternion spread **1.746**, turning steadily about Z. Shown to fail by
sampling position only — `rotated` goes to 0 and the spread to 0.

**Severity A** — every set piece that rotates rather than translates, in every
area.

## Fixed (batch 6, 2026-09-03)

### 52. A cutscene RESTARTS the environment: the `.SCX` belongs to the AREA and must survive `scene.load` — A

> **Fixed 2026-09-03.** `Session::reloadScene` now rebuilds only when the
> AREA changes, and loads by `ChunkKind::Area` so it no longer depends on the
> scene->area map. `verify.py: engine scene survive` runs a program 30 frames
> into the crates' beat, does `scene.load(222, 57)`, and asserts it is still
> running with the same clock and goes on advancing (30 -> 30 -> 40).
> **CONFIRMED IN PLAY** together with 53.
>
> **The first version of that check was worthless and said so only under
> mutation.** It built the Session with `loadScene` alone, which does not make
> an area RESIDENT - and `sceneLoad` walks the two slots and does nothing for
> an area in neither, so the scene load never happened and "the program
> survived" was vacuous. Removing the fix changed nothing and the check still
> passed. It now calls `loadArea(222)` and asserts residency as its first
> field. A second trap on the way: after restoring the source the build linked
> a STALE `area.o`, so one run reported the mutated behaviour from correct
> source - both directions were re-run with a forced rebuild.

Filed 2026-09-03 from a play report — *the animations launched by the
"environnement" (and not by a cutscene) are not stopped during a cutscene*.

**What the engine does.** The `.SCX` is the AREA's, named by its `+97` stem,
and it is loaded exactly once — when the area loads. `Area_LoadScx`
(0x0041B4E0) finds the area's slot, clears the container at `slot+8`, calls
`Scene_LoadSCX` into it, then loads the matching `.sfx` and calls
`Sfx_BindAmbientEffects(slot)`. It has **two callers, `Area_TickLoad` and
`Game_Init`** — and neither is a scene load.

`Scene_LoadSCX` (0x00449750) has **four call sites in the whole binary**:

    Area_LoadScx   -> slot + 8          the AREA's SCX  (the environment)
    Game_Start     -> &stru_930780      the GLOBAL aventure.scx library
    sub_419060     -> &stru_930780
    sub_4193E0     -> &stru_930780

The `scene.load` opcode (71, handler 0x403950) is **not among them**. It calls
`sub_40C120(scene, slot)` — `Scene_Load` — which brings in the SCENE *chunk*:
its startup script, zones and props. The object pool is untouched, so **every
running program survives**, which is exactly the reported behaviour. A
cutscene does not own the objects it animates; it calls `scx.play` on objects
of the area's own `.SCX`, and the ones it never names go on running.

`Script_PlayAllScripts` then ticks that pool every frame, once per resident
slot (`v4 += 33; while (v4 < dword_9104EC)`), so both resident areas animate.

**What the port does.** `Session::reloadScene` builds a whole new
`SceneRunner` and moves it over `scene_` on every scene load. And
`resolveScx` maps a SCENE chunk to its AREA and reads the stem from the **area
chunk** — so area 222 and scene 55 name the *same file*. The port therefore
tears down and rebuilds the identical `.SCX`, resetting every program's
counter, clock and run counts, and re-binding the `.sfx` set pieces. Every
environment animation stops dead at the cutscene and only comes back if some
script happens to start it again.

The comment in `reloadScene` asserts the opposite and is **wrong**:

    // The programs of the outgoing scene go with it. That is the engine's
    // behaviour and not a simplification: `Scene_LoadSCX` rebuilds the object
    // pool, so no program survives the transition.

`Scene_LoadSCX` does rebuild the pool — but only the area load and the global
library ever reach it. The reasoning was sound and the premise was never
checked against the call sites.

**The fix.** Keep the `SceneRunner` while the AREA is unchanged: rebuild it
only when the area changes, and let `scene.load` swap the SCENE chunk's
scripts/zones/props alone. The `.sfx` bind belongs with the area load for the
same reason.

**Ruled out on the way, and worth not repeating.**
`Script_AnimationFromExternalScene` (0x00470060) looked like the mechanism —
its own error string is *"Can't find object \"%s\" in external scenes"* and it
resolves through `sub_4A5040` rather than the running scene. Its function id
is **0x0300001A**, and a census of all 220 shipped `.SCX` (4511 objects) finds
**0 uses**: it is dead code in this game. `engine/tools/extanim_census.cpp`
prints the full per-id table; the workhorses are `MoveObjectOnPath` (4841 uses
in 212 files), `PlaySound` (3797) and `SelectRelativeBodyAnimation` (2398).

**Severity A** — it affects every cutscene in the game, and the environment is
most of what is on screen.

## Fixed (batch 5, 2026-09-03)

### 51. No diagonal movement: holding two directions walked straight — A

> **Fixed 2026-09-03.** The controller applied the standalone turn from the
> state being LEFT instead of from the CANDIDATE that carries it, so the
> turn was zero and holding forward+left walked in a straight line.
> `verify.py: engine player walk` now runs a fifth stream, `k200+203*40`,
> and asserts he covers ground AND turns AND ends in the same state the
> forward-only stream ends in. Awaiting the play test.

Filed 2026-09-03 from a play report — *the new engine doesn't support the
press of two directionals buttons at once (it forces to walk straight, then
turn left without moving, then walk straight again)*. My first reading of the
graph concluded the machine had no walk-and-turn edge and therefore that the
original could not do it either. **That was wrong, and the reader — who has
played the original — corrected it**, with the hypothesis that turned out to
be exactly right: *maybe the animation is still the same and only the
direction of the character change*.

**What the engine does** — the walk-and-turn is not a transition at all.
Every tick, `Cef_TickChannel` (0x004A8160, `29_win32.c:194`) runs a loop
BEFORE the commit:

    while (1) {
      v15 = Cef_FindTransition(v1, v4, v12, 2, 784, 1);
      if (v15) {
        v17 = u32(v15, 8);
        if ((v17 & 0x100) != 0) { sub_45C080(u32(v1,0), (float *)(u32(v15,44)+8));
                                  v60 = 1; v52 &= ~v16[1]; }
        if ((v17 & 0x200) != 0) { Cef_ApplyRootShift(...); v60 = 1; v52 &= ~v16[1]; }
        ...

It searches for a candidate carrying flag `0x100` (turn) or `0x200` (shift),
**applies its effect, masks that candidate's own input bits out of the word,
and loops** — without ever taking the transition. So holding forward and left
leaves `H_WALK` playing and turns the facing underneath it. The animation
never changes; only the direction does.

`H1Avnt` group 0 carries four such aliases, and nothing else in the group
matches `(flags & 2) && (flags & 0x310)`:

    input 0x01  flags 0x00204913  turn dY = +5.0   move MDROT000
    input 0x02  flags 0x00204913  turn dY = -5.0
    input 0x01  flags 0x00201113  turn dY = +3.0
    input 0x02  flags 0x00201113  turn dY = -3.0

Two rates — 5 and 3 degrees a frame. None of them is on `H_WALK` itself,
which is the whole of the bug below.

**And the two turn sites read DIFFERENT records**, which is what the port
collapsed. The on-transition one, `28_script.c:3509`, passes
`&from->turn->dx` to `Cef_ApplyTurn` (0x0045C1B0), which adds it **whole**.
The standalone one passes the found CANDIDATE's block (`u32(v15,44)+8`) to
`sub_45C080` (0x0045C080), which adds `value * flt_4C30D8` — a **rate**.

**What the port did** — `channel.cpp` already ran the loop and already emitted
an event for it, but emitted it as the same `Kind::Turn` the transition site
uses, and `player.cpp` handled that kind by reading `S[e.from]` — the state
being left. In a walk that is `H_WALK`, which carries no turn block, so the
applied angle was **0.0** every tick. The input bits were still masked out of
the word, so the walk edge kept winning and he kept walking straight: the
mechanism was running, and silently applying nothing.

**How established** — read out of `Cef_TickChannel` and the two appliers, then
measured through `tools/player_probe` on `H1AVNT`/`AIMPASSE`. Over 40 frames:

    held      state at end   facing        walked
    UP        H_WALK         0 -> 0        yes
    LEFT      H_SDLROT       0 -> 166      no  (turn in place)
    UP+LEFT   H_WALK         0 -> 190      yes (5 deg/frame)

`LEFT` alone is the negative control: it turns through a different state and
does not move. Shown to fail by restoring the `e.from` read — `UP+LEFT` then
covers the same ground with the facing unchanged, and only that one field of
the check moves.

**Severity A** — half the movement vocabulary, and it is felt on every step.

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

### WITHDRAWN: "the tutorial's one-shot is not a bug" — it was, see iam-script-engine 44

On 2026-09-03 I recorded the third symptom of the play report — *the tutorial
is triggered each time* — as already correct, on the strength of `walk_zone`
and `zone_probe`. **That was wrong, and the player found it again the same
day.** The real fault is `iam-script-engine.md` 44: `zone.enable`/`disable`
set the save bit and nothing rebuilt the live list, so both were inert until
the next area load.

Kept as a heading rather than deleted, because the way it was got wrong is
worth more than the wrong answer. `walk_zone` re-registers explicitly and
`zone_probe` drives `World`, not the Session's `ZoneRegistry` — so **neither
harness could exhibit the fault**, and both reported success. A negative
result is only as strong as the path the test actually drives, and I asserted
one over a path that was not the one that broke. The replacement check drives
a real `Session`.

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

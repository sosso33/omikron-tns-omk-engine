# omk-play — open issues

The viewer (`engine/backends/sdl/play.cpp`). It is an INSTRUMENT, not a slice
of the port (CLAUDE.md 5), but what it fails to draw is what a person reports,
so its gaps are filed here in the same four-part shape as the script engine's.
Numbering continues from `iam-script-engine.md` so a number names one thing.

## Open

### 41. One body is staged: every character but the first shown collapses into it — B
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

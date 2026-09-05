# Reader follow-ups, 2026-09-05

Four items a reader queued after walking the restaurant demo. In their order.
Each ends in a commit and a report; the plan file records what is done.

| # | item | state |
|---|---|---|
| 1 | the OUTGOING pool's bodies freeze during a transition | **DONE** 2026-09-05 |
| 2 | the shoot-mode bodies have no pose (41 of them, 7 areas) | **DONE** 2026-09-05 |
| 3 | ~~21~~ **4** bodies named by nothing in the corpus | **DONE** 2026-09-05 |
| 4 | the adventure follow camera passes through walls | open |

---

## 1. The outgoing pool's bodies freeze during a transition

`omk-play` reads scene MOTIONS from both resident pools -

```cpp
for (const omk::SceneRunner* sr : {&session.sceneOut(), &session.scene()})
```

(`play.cpp`, the tunnel-door fix) - but resolves a body's POSE from
`session.scene()` alone. So the moment `reloadScene` swaps the resident
`.SCX`, every body whose program lives in the outgoing pool falls to the bank
idle, or for a character with no bank to the REST pose.

Measured walking Anekbah -> the restaurant: at exit, **17** of Anekbah's
street bodies (72, 459..464, 50, 61, 465..469) report `the rest pose (no bank
clip)` while the restaurant's eight are on their clips.

It is not new - it predates 2026-09-05 - but `Area_TickLoad` case 2 moved the
swap from the end of the transition to its start, so the freeze now begins
~80 frames earlier, inside the window where BOTH sets are still drawn.

The engine's own position, already recorded in `Session::reloadScene`: a
transition does not call `Scene_LoadSCX`, so every running program survives
it and the outgoing pool goes on being ticked. The pose lookup should
therefore search both pools, exactly as the motion lookup does.

**DONE.** The per-actor lookup walks `{&scene(), &sceneOut()}`, active pool
first so a body both could claim keeps the incoming scene's program, and the
clip, path, clock and frame all come from whichever runner answered. Walking
Anekbah -> the restaurant: **17 frozen bodies -> 0**, the five `MCG_FN` mecas
included. `OMK_ACTIVE_POOL_ONLY` is the mutation (14 frozen), folded into
`verify.py: engine: walk-in scene`.

## 2. The shoot-mode bodies

41 staged bodies over 7 areas (AREA 2 Gandhar's cave, 61, 71, 77, 80, 144,
and the rest) are `shoot.actor.enter` targets: ACTOR_STATE 3, no `.CTL` in
any of the three 9-byte slots, and their clips come from the area's `.ani`
library through `sub_434530(type)` and `List_PickRandomByType`. Opcodes 82
and 84 are not in `interp.cpp` and the shoot AI is not consumed by the
viewer. See `docs/FILE_FORMATS.md` (the `.ani` library at AREA +124) and
`engine/src/actor/shoot.h` for the standard this row can be held to.

**DONE.** Opcodes 82 and 84 now reach the Session (`shootActors_`, actor ->
the last action asked of him), `animGroupClips(ani, group)` generalises
`pedClipsFrom`'s walk to any character type, and the viewer poses such a body
from the area's library ahead of the bank idle. Standing in zone 313, all ten
of its `ZOH_FN` pose from `gandhar.ani` group 11.

**Two things this does NOT do, and they are labelled in the code**: the pick
is the FIRST match rather than a random one, so a still frame is
reproducible; and what runs is the SCRIPT's last action, not the AI -
`Shoot_TickNpc` calling one of four brains every frame is `actor/shoot.h`'s
and is not wired to this.

**And the rest pose before the trigger is the engine's own state.** The
arming is zone 313's and 314's, not the startup script's, and until the
player trips them nothing in the engine can pose these characters either.

## 3. The bodies named by nothing

21 placement records are SHOWN at a new game, carry no `.CTL`, and are named
by no `scx.play.actor` or `shoot.actor.enter` anywhere in the 5785 slots -
16 of them in AREA 218 (Anekbah CS Sas) wearing the crowd models, plus AREA
35 (2), 68 (2) and 230 (1). Either another mechanism drives them or they are
pre-loads; the corpus walk that produced the count is in the 2026-09-05 log
row.

**DONE, and the count was WRONG: it is 4, not 21.** The walk behind it used
the 5785 script slots, which come from the zone records and the message
subscriptions - and `CLAUDE.md` §6 already records that **nothing in that walk
reaches a chunk's `+4`**. 173 chunks carry a startup script and they name half
the cast: `scx.play.actor` covers **166** distinct actors from the slots and
**328** once the `+4` scripts are read. Re-run, the 72 rest-posed bodies split
43 shoot / 25 played / **4** named by nothing.

The four are `MOOBJ_FN` and `M2_FN` in the Morgue and `BRA_FN`/`PSH_FN` in
Anekbah's first supermarket. None carries a `.CTL` in any slot, neither area
names an `.ani` at `+124`, and none is a conversation speaker - so nothing in
the shipped data can pose them, which makes it a property of the data.
`MOOBJ_FN` says so itself: its pelvis is baked at an absolute
(-1565, -114, -8494) where an ordinary character's sits near the origin, so it
is a fixed prop - a body on a slab - and its rest pose is its authored pose.
`verify.py: played actors`.

## 4. The adventure follow camera passes through walls

Reported by a reader: the follow camera "does not respect the wall position
and collider - it is often placed on the other side of the wall". The
requirement is the ORIGINAL's behaviour, so this starts by reading what the
engine's camera does about collision, not by inventing a spring arm.

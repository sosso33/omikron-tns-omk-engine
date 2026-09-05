# Reader follow-ups, 2026-09-05

Four items a reader queued after walking the restaurant demo. In their order.
Each ends in a commit and a report; the plan file records what is done.

| # | item | state |
|---|---|---|
| 1 | the OUTGOING pool's bodies freeze during a transition | **DONE** 2026-09-05 |
| 2 | the shoot-mode bodies have no pose (41 of them, 7 areas) | **open** |
| 3 | 21 placement-record bodies named by nothing in the corpus | open |
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

## 3. The bodies named by nothing

21 placement records are SHOWN at a new game, carry no `.CTL`, and are named
by no `scx.play.actor` or `shoot.actor.enter` anywhere in the 5785 slots -
16 of them in AREA 218 (Anekbah CS Sas) wearing the crowd models, plus AREA
35 (2), 68 (2) and 230 (1). Either another mechanism drives them or they are
pre-loads; the corpus walk that produced the count is in the 2026-09-05 log
row.

## 4. The adventure follow camera passes through walls

Reported by a reader: the follow camera "does not respect the wall position
and collider - it is often placed on the other side of the wall". The
requirement is the ORIGINAL's behaviour, so this starts by reading what the
engine's camera does about collision, not by inventing a spring arm.

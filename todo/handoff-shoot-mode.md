# Handoff — SHOOT MODE (`todo/next-tasks.md` 18)

Written 2026-09-10, at the end of the session that read the mode and wired it.
Everything below is **pushed** on `main`; nothing is left uncommitted.
The plan and the full record are [`shoot-mode.md`](shoot-mode.md); the play
reports are [`omk-play.md`](omk-play.md) 97.

---

## 1. HOW TO REACH A SHOOT PHASE — read this before anything else

```
cd engine && make play
build/omk-play "$OMK_DATA" ../tables --save ../traces/save-appart.bin \
    --area 230 --scene-chunk 56 --vulkan
```

The cutscene plays, the editing ends at frame ~385, and `SHOOT MODE ENTER`
follows two frames later. **~390 frames from a cold start, no save of your
own and no playthrough.**

**`--scene-chunk` is the whole trick and it is easy to miss.** The supermarket
phase is **AREA 230 + SCENE 56 or 62** (both on set `ASM49`), and AREA 230
carries **no `shoot.begin` of its own** — it is in a ZONE SLOT of the scene.
`--save --area 230` is a street start, which *lands* the player in the room
without running the chunk that would have been loaded on the way in, so the
trigger never exists and he stands there for ever.

The other arena, and the one every check uses, is the Shooting gallery:

```
build/omk-play "$OMK_DATA" ../tables --save ../traces/save-appart.bin \
    --area 59 --stand 5000,0,-2900,180 --shoot
```

`--shoot` is a HARNESS — it calls `shootBegin(-1)` directly. It is not how the
game enters the mode, which matters more than it sounds: see §5.

`engine/tools/shoot_trigger.cpp` answers "which chunk starts a shoot phase"
directly, scanning every chunk's zone slots **and** its startup script at `+4`.

## 2. Where the work stands

| step | state |
|---|---|
| 1 the mode read, `MAP2D` found to be the AI's grid | done |
| 2 the grid decoded, ported, drawn | done |
| 3 the mode ported — ops 80/81, weapon slot, HUD, library | done |
| 4 the frontend, the weapon tables, what a shot is | done |
| 5 the brains' decision revisited — range, cone, line of sight | done |
| 6 the sight predicate and line walk ported, docs, checks | done |
| 7a-c the geometry, the turn, **all 16 states of `sub_424DE0`** | done |
| 7d the brain TICKS on real gunmen | done |
| 7f the projectile pool | done, **not connected** |
| 7e the play test | done — see §3 |

**14 checks** cover it: `shoot arenas`, `map2d grid`, `map2d sight`,
`bone names`, `shoot range`, `shoot generic`, `projectile pool`,
`shoot input`, `shoot mode`, `engine: shoot mode`, `engine: shoot brain`,
`engine: shoot AI`, `engine: shoot pose`, `weapon table`.

## 3. What a person has CONFIRMED, and what is only measured

**Confirmed in play** (a reader, 2026-09-09/10): the mode enters; the player's
body is hidden; the first-person camera installs, survives the editing hold
and wins over the area's absolute camera; the mouse turns the view directly;
the eye height is right; ENTER confirms once in menus; the click no longer
validates them.

**Measured only** — every number in the brain: the sixteen states, the three
ranges, the cone, the projectile entry. They come from transcription and
checks, and **no gunman has been watched behaving**, because nothing drives
them into a fight.

## 4. What is NOT done

**Firing (`omk-play.md` 97c) is the one thing left, and it is deliberate.**
The pool is ported and asserted, the trigger reaches the input word, and the
brain reaches outcome 1 — **and nothing joins the three**. The engine raises a
shot through the `.CTL` firing state, not off the trigger bit:

1. the input word drives the channel into a firing state;
2. `loc_45C4DD` sets the latch `dword_53AE3C` — the ONE write that is not a
   clear, and it is in a function with no `proc` label, so it is in the
   listing and not in `readable/src`;
3. `sub_45C680`'s ACTOR_STATE cases 13, 14 and 16 see the latch, raise the
   one-shot request `dword_4E9744`, and clear it;
4. the frame loop calls `Actor_TickProjectiles(player)` ONCE and clears it.

A port that fired on the bit would look right and would have no rate, no state
gate and no channel behind it. **Read the state before wiring it.**

Still parameters rather than readings: `sub_421020` (its success sends a
gunman into the 10/11 pair), `sub_421CD0`, `sub_435900`, and the gate
`actor[+16]` that chooses between `0.7` and `0.0` for the camera lift.

**This port's own numbers, labelled as such** — mouse sensitivity
(0.18°/px yaw, 0.14°/px pitch), the ±70° pitch clamp, and both axis senses.
Nothing shipped governs them: the engine reads mouse motion nowhere in the
binding path.

## 5. Traps that cost time, in the order they bit

1. **A HARNESS THAT DOES SOMETHING THE REAL PATH DOES NOT.** `--shoot`
   installed the camera; the script-driven entry did not, and `--shoot` had
   quietly become the only way the mode was ever entered — so every check,
   render and measurement for six steps went through a path that carried a
   step the game's own path was missing. **Ask what the harness does that the
   thing it stands in for does not.**
2. **A BIT NUMBER IS NOT A MEANING.** `Action / Utiliser` is slot 4 in
   *Aventure* and slot **8** in *Tirer*, where slot 4 is `Tir`. Two separate
   bugs came out of that, and the repeat mask `0x203F` is expressed in
   Aventure's slots too.
3. **THE ROOM WAS THE WRONG ROOM.** Grepping set names for `SMARKET1` found
   AREA 68 — a *different* supermarket — and three fixes were tested against
   it. The reader's own log said `last set ASM49` throughout.
4. **A TOOL GIVEN A TABLE PATH RELATIVE TO THE WRONG DIRECTORY** answered
   "0 slots for 330 chunks" rather than failing. Same family as the handoff's
   own §1 warning about data roots.
5. **`--scene-id` IS NOT A FLAG**, so passing it was silently ignored and two
   different scenes "confirmed" the same set.
6. **A SIGN THAT ONLY MOVING CAN SHOW.** `resolveOffsets` SUBTRACTS its
   offset, so a positive Y raises the eye — the opposite of what "Y grows
   down" suggests. And the acquisition's forward vector is `-sin`, not `+sin`:
   the two conventions agree on the cardinal axes, so every still assertion
   passes either way and only a convergence loop separates them.
7. **THREE WRONG HEIGHTS BEFORE THE RIGHT ONE**, and only the last came from
   the original. The reader's *"instead of guessing, look at the original
   code"* is the lesson, and it was already written down here.

## 6. New instruments, so they are not rebuilt

| | |
|---|---|
| `engine/tools/shoot_trigger.cpp` | which chunk starts a shoot phase — zone slots AND the `+4` startup script |
| `engine/tools/shoot_range.cpp` | the record's geometry, the turn, the brain's states, the engagement, the eye height |
| `engine/tools/projectile_probe.cpp` | the pool, the fire gate, the entry's byte accounting |
| `engine/tools/bone_names.cpp` | bone-name prefixes over every library and model |
| `engine/tools/mousebit.cpp` | what each mouse button does per control group |
| `engine/src/actor/projectile.{h,cpp}` | the pool and the four weapon slots |
| `omk::readBodySpheres` | the `.3DO` sphere table at `desc+244`/`+248` |
| `--invert-x` / `--invert-y` / `--shoot-eye N` | the mouse senses and the eye lift |
| the viewer's own lines | `AIM reached`, `MOUSE dx`, `N/M tracks resolve`, `shoot brain` |

## 7. The sweep

**A full `--slow` run is OWED and has not been done since 2026-09-09's**, which
measured `f92231a` — before any of this. `todo/sweep-log.md` says 3 tasks.

The reader's rule is that the suite catches what nobody can see rather than
measuring code mid-repair, so it waits for their go — but the subsystem has
now settled and been confirmed in play, so the moment has arrived. Things it
should be watched for: the **bone-name fix touched the crowd libraries**
(`passantH.ani` carries 19 `U`-prefixed tracks), the camera changes move a
path every `engine:` check exercises, and `licence headers` moved three times
(402 → 410).

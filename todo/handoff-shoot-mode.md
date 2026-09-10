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

**To FIRE headless** (2026-09-10): `Tir` is scan code **54** (right Shift) in
the *Tirer* scheme, and `--keys` reaches the world's input word, so

```
SDL_VIDEODRIVER=dummy build/omk-play "$OMK_DATA" ../tables \
    --save ../traces/save-appart.bin --area 59 --stand 5000,0,-2900,0 \
    --shoot --frames 300 --nodelay --keys 54,54,54,54,54,54,54,54,54 --keydelay 30
```

gives eight `MDSHOOT0` latches, eight `SHOT` lines and eight `SHOT retired`
lines - `verify.py: engine: shoot fire` is exactly this. (`--hold` feeds the
same word, as `k54*N`.)

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
| 7f the projectile pool | done — and connected by 7h |
| 7e the play test | done — see §3 |
| **7h the SHOT**: `MDSHOOT0`'s latch, `sub_47C2A0`'s gate, the record path to the pool | **done 2026-09-10, `cc3d1f9`** — played, *"Ok"* |
| **7i the FLIGHT**: `Projectiles_Tick`, the world ray, the bolt drawn | **done 2026-09-10, `96fab56`** — played, *"Ok"* |
| **7j the HIT**: the actor sweep, damage, reactions, death | **done 2026-09-10** — three bolts kill a gallery gunman; not yet played |
| **8.0 the FIRST-PERSON ARM and GUN**: the `0x200000` exemption, the Waver on `Maing` | **done 2026-09-10** — reported from the original's frames; **confirmed in play** with the raise |
| **8.0 the RAISE**: `sub_471950`'s aim layer, `actor/shootaim.h` | **done 2026-09-10** — gun low at rest, at the centre to fire, the bolt from it there; **CONFIRMED IN PLAY** in the supermarket |
| **8.5b MOVING in first person**: shoot mode's own mover, `sub_47D4D0`, `actor/shootmove.h` | **done 2026-09-10** — forward, back, strafe, turn keys, crouch speed; `engine: shoot move`; **CONFIRMED IN PLAY** |

**17 checks** cover it (`engine: shoot hit` added with the hit): `shoot arenas`, `map2d grid`, `map2d sight`,
`bone names`, `shoot range`, `shoot generic`, `projectile pool`,
`shoot input`, `shoot mode`, `engine: shoot mode`, `engine: shoot brain`,
`engine: shoot AI`, `engine: shoot pose`, `weapon table`, **`shoot fire`**,
**`engine: shoot fire`** and **`engine: shoot hit`** (the last two `--slow`).

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

**FIRING IS CONNECTED (2026-09-10, `todo/shoot-mode.md` §7h/§7i)** — and the
chain this section used to give was wrong in two places, both found by reading
the addresses it named before wiring anything:

1. the input word drives `H1Avnt` group 200's entry [132], which queues
   `MDSHOOT0`;
2. **`MDSHOOT0` (0x0046B610) sets the latch** `dword_53AE3C`, in ACTOR_STATE
   3. `loc_45C4DD`, named here as the set, is a CLEAR (`mov dword_53AE3C, ebp`
   after `xor ebp, ebp`, in `Actor_PlayClip`);
3. `sub_45C680`'s cases **1, 3, 11, 12, 13, 14 and 16** (not 13/14/16) see
   it - and in shoot mode hand it to the GATE `sub_47C2A0`, which holds the
   rate and the weapon's raise, and whose outcome 2 raises `dword_4E9744`;
4. the frame loop fires `Actor_TickProjectiles(player)` once.

**THE HIT IS PORTED (§7j, 2026-09-10)**: bolts meet gunmen, deal their damage
and kill them, with the death clip their direction picks. ~~What is left is
THE HIT, read and not ported: bolts pass through gunmen, so the phase still
cannot be completed.~~ It also moved every placement-turned body to turn about
its PELVIS instead of its model origin - see §7j, and look at it in play. What
the hit is made of: the two node lists the sweep walks, the per-mesh sphere and
box tests (the engine's box test omits a bound, and the port keeps it), the
damage rules (gunmen cannot hurt each other; the baton's damage 6 reaches only
0x4000 victims), the four hit-direction bands and the death clips they pick,
and the reactions - on per-mesh world transforms the viewer captures at the
draw, which is the engine's own order since the flight runs before the actors
tick.

**And the gunmen's own shots** are still not wired: the brain reaches
outcome 1 and `sub_47C2A0` would fire them directly, but the port's brain
epilogue does not call it yet.

**Reported missing in play (2026-09-10, *"no fire sound effect, no UI"*): the
FIRE SOUND and the shoot HUD.** Both are planned with their leads in
`todo/shoot-mode.md` §8 - the sound probably from `shoot2.sfx` section A's
three effect ids (the Waver's effect 1 carries sound 687), the HUD from
`Hud_Refresh` / `Hud_DrawBar` and the two globals the shot already writes.

Still parameters rather than readings: `sub_421020` (its success sends a
gunman into the 10/11 pair), `sub_421CD0`, `sub_435900`, and the gate
`actor[+16]` that chooses between `0.7` and `0.0` for the camera lift.

**This port's own numbers, labelled as such** — mouse sensitivity
(0.18°/px yaw, 0.14°/px pitch), the ±70° pitch clamp, and both axis senses.
~~Nothing shipped governs them: the engine reads mouse motion nowhere in the
binding path.~~ **Wrong (2026-09-10)**: `sub_47D370` IS the mouse look -
`+420 -= word_90E1AC * 0.01 * dx`, pitch `+= word_90E1AE * 0.01 * dy * dt`,
sign by the invert byte 0x90E1B0, clamped at **±45°**, all from the options
header. The original's numbers are readable and the port's are not them; not
yet acted on.

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
8. **THIS HANDOFF'S OWN ADDRESS WAS A CLEAR** (2026-09-10). §4 named
   `loc_45C4DD` as the latch's one set; the instruction there is `mov
   dword_53AE3C, ebp` straight after `xor ebp, ebp`. A handoff is a reading
   like any other - check the address before building on it.
9. **`asmfn.py` SNAPPED, and a finding was written from the wrong function.**
   `omk-play.md` 97's "MDSHOOT0 is the EQUIP" came from `asmfn.py 0x0046B610`,
   which returns a neighbour because the address has no `proc` label. When an
   address the DATA names (here, `tab_special_move`) has no function,
   disassemble the BYTES - CLAUDE.md §1 says so, and it bit anyway.
10. **`timeout` IS NOT A macOS COMMAND.** `timeout 300 build/omk-play ... | grep`
    printed nothing, which read as "no shot" - the shell's "command not found"
    went into the grep and was filtered out. Save the whole log to a file and
    grep the file.

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
| `engine/tools/shoot_fire.cpp` | the gate, the latch, the aim, the record path, the flight, the weapon types - and, with a data root, every shipped weapon and `shoot2.sfx`'s shot sprites |
| `engine/src/actor/shootfire.{h,cpp}` | the weapon table, `MDSHOOT0`, `sub_47C2A0`'s timing half, the channel tick's shoot branch, `sub_442160` |
| `ProjectilePool::fireFromRecord` / `fly` | `Actor_TickProjectiles`' record path and `Projectiles_Tick` |
| `ctl_find`'s `f12` column | an entry's `+12` whole - what `sub_45AB80` tests |
| the viewer's firing lines | `Shoot_InitWeapon`, `the gun ...`, `MDSHOOT0 - the latch armed`, `SHOT n`, `SHOT retired` |

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

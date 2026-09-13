# The released spectres fire — what makes them

Started 2026-09-13 on the reader's go. The reader, from a video of the
original: *"patroling spectre does not [shoot], but you are supposed to
interact with some doors to release some spectres, and if you released the
wrong ones, they shoot on you"*. The port's patrolling spectres are silent,
and the reader confirmed that in play; the released ones must fire, and by
every path read so far they cannot (`todo/shoot-patrol.md`, "RE-CHECKED
2026-09-13"): the generic brain's fire test is `sub_4348B0(record +20)`, bit 0
of the `.ani` group word `+8` for the character type (property 7, record
`+176`), and group 12 carries 0 in all 11 shipped `.ani` files - the bit is set
for type 3 alone. So the mechanism is somewhere not yet read.

**STATUS 2026-09-13: UNRESOLVED and PARKED** on the reader's word - every
data-side lead is closed (steps 1-4 and the `.CTL`) and none lets a spectre
hurt the player; the question waits on what the attack looks like in the
original. Steps 5 and 6 port what was found on the way.

Working rhythm: a step, a commit, a report, the reader's go. **No porting
until the mechanism is found and reported.**

## The steps

1. ~~**Read every tomb script in full**~~ - **DONE 2026-09-13.** AREA 141
   ("Mayerem Sas Hamest", the catacombs), 45 scripts, 52 actor records: 24
   `SPV_FNM` of type 12 (410, 425-432, 443-450, 589-595), 21 `CHD_FN` of type
   6, no type 3 at all, and five unique characters of type -1 (`SPW_FNM` 407,
   `SPF_FNM` 408, `SPG_FN` 409, `NOU_FNM` 435, `ANOB_FN` 675/676) plus
   `MBK_FNM` 23 of type 0.

   Each "Tombeau N" zone runs one of two outcomes:
   * **a good tomb** - `shoot.player.suspend`, a fade, a camera, the GOOD
     spectre shown (`SPG_FN` 409 at tomb 7, `SPW_FNM` 407, `SPF_FNM` 408) and
     a scene program on him (`scx.play.actor.wait` / `scx.play.actor`),
     `var.add 459 'Good Spectres'`; at 3 the phase ends (`shoot.end 1`);
   * **a wrong tomb** - `set.var2 'Spectre N Vivant'`, `actor.stat.set N, 1,
     469 'Vie Spectre'` (his health), `character.show N`, `shoot.actor.enter
     N` and `shoot.actor.action N, 2, 0` - action **3** for 410 and 425. The
     port's action table puts both 2 and 3 in the HUB, state 6, latched
     (`0x20`), with the `+168` timer from property 31.

   Nothing else touches them: across the 45 scripts no type change, no `.ani`
   load, no weapon given, no property written but health (`actor.stat.set
   ..., 1`). The opcodes used are the ordinary ones (the tally is in the step's
   commit). So **the scripts do not make a released spectre able to fire**; if
   he fires, the engine does it.

2. ~~**Search the executable for every other way to hurt the player in shoot
   mode**~~ - **DONE 2026-09-13, and the result is NEGATIVE for the spectres.**
   Every path that lowers a shoot record's health, enumerated from its callers:

   | path | who reaches it | a released spectre? |
   |---|---|---|
   | the bolt hit `sub_4240E0` | `Actor_TickProjectiles` (fire) and `Projectiles_Tick` (the flight) | only through the fire test - group 12's `+8` is 0 |
   | the splash `sub_424470` | `Projectiles_Tick`, for a bolt of damage over 10 | only through a bolt |
   | the direct strike `sub_423B10` | the generic brain's epilogue **outcome 3** (state 10, the first half of its clip), within **property 21** metres, **property 22** damage, once a clip (flag 4) | **no**: see below |
   | `sub_423B10` again | `sub_4800C0` (the type-13 brain), `sub_47F510` (no callers), and `sub_4691B0` - a CRUSHER (-1, death) | no |
   | message 11 (the area's -45, red flash, shake) | `Walk_GroundResponse` only | no - it is a LANDING |
   | the scripts | AREA 141's `actor.stat.set -1, 1` are medikits and the hurt handler | no |

   **The direct strike, read to the end.** `Shoot_ActorEnter` fills the shoot
   record's `+112..+124` with the SLOTS of the gunman's `.ani` group clips
   whose TYPE is 12 (`sub_4347A0` walks slots, `sub_434860` gives a slot's
   type). `sub_421020` passes each slot to property 21 - which
   `Actor_GetProperty` looks up in the ACTOR RECORD: four int16 ids at
   `+226`, ranges at `+234`, damages at `+242` (property 22), default 2 m and
   1 - keeps the shortest range that still reaches, stores it at `+108` and
   sends him to the 10/11 pair. **`spectre.ani`'s group 12 holds no type-12
   clip at all** (types 1, 5, 9, 11), so a spectre's `+112` stays empty and the
   strike is out of his reach whatever his record says (all twelve fields read
   2412, or 2524 for 443-450 - never consulted).

   **Found on the way, and not spectres:**
   * **the DOGS.** `CHD_FN`, type 6 - "Start Shoot Dogs Entrée", "Activate
     Dogs 3 & 4" - are the catacombs' other enemies, and their group 6 holds
     ONE type-12 clip (slot 1), which their records give **4 m and 5 damage**:
     a BITE. The port has none of it: `found421020` is never set, outcome 3 is
     `NOT READ`, `sub_423B10` is unported. 20 of the 21 dogs carry it (587
     carries zeros).
   * **messages 10 and 11 are the player's LANDINGS**: `Walk_GroundResponse`
     posts 10 with `.CTL` group 4 and 11 with group 5 (and `sub_414DE0(.., 19)`)
     - the bands `todo/player-vertical.md` measured. AREA 141 subscribes to 11:
     a hard landing costs 45 health, with a red flash and a camera shake. This
     is next-tasks 14's fall damage, found from the script side.
   * **the "Apparition spectre" zones** (443-450) only stage a ghost: show him,
     a scene program, hide him.
   * the message table of AREA 141: 3 a kill (by id), 9 the death, 0 the hurt,
     1 a ring spent, 11 the landing.

   So on every path read, **nothing lets a released spectre hurt the player**.
   What is left untested: the phase's REAL start (the reader: *"it start
   elsewhere, then a dialog start when you arrive at your checkpoint"*), which
   may load a SCENE chunk with its own handlers and programs over AREA 141 -
   and the reader's video, which would say what the attack looks like.
3. ~~**Report**~~ - DONE 2026-09-13: the negative result above, reported. The
   reader chose **C**: first the phase's real start, then port what was found.
4. ~~**The phase's REAL start**~~ - **DONE 2026-09-13, NEGATIVE as well.**
   * **The way in** is AREA 140's `area.goto 141, 5, 7` - no scene: `omkdata`'s
     scene map, built from every `scene.load` site in AREA and SCENE, names no
     SCENE for AREA 141, and AREA 141's own 45 scripts load none.
   * **The route inside**: zone 2309 "Start Shoot Dogs Entrée" (record 29 -
     the dogs shown and entered on their routes, `shoot.begin`, the
     "Apparition spectre" zones enabled); zone 2294 "Matamboukous Anneaux"
     (record 14 - Matamboukous, actor 23, appears on scene programs and speaks
     **dialog 100**); zone 2295 "Start Shoot" (record 15 - reads 589's type into
     `Type Spectre`, enables the tombs, the medikits and the appearance zones,
     and enters the patrolling spectres).
   * **Dialog 100 carries no script**: `IAM\DIALOG` chunk 100 is 198 bytes,
     speaker 23, one node-less line - *"Accepte ce modeste présent. Trois
     Anneaux entrelacés : les deux premiers sauveront ta vie, le troisième te
     ramènera ici."* - the three rings the phase's death spends (message 1).
   * **No script outside AREA 141 names a released spectre**: every AREA,
     SCENE and GLOBAL script listed, 0 references to 410 or 425-432.

   So the real route adds nothing that could arm them. The mechanism is not in
   the data this tree reads as scripts; what is left is the NATIVE side not yet
   read for them - their `.CTL` (`SPV_FNM`'s channel: a special move or an
   effect record that damages on contact) - and the reader's video.

   **The `.CTL`, read on the reader's request the same day - there is none.**
   Every one of AREA 141's 52 actor records leaves `+72`, the `.CTL` name,
   EMPTY - all 24 `SPV_FNM` included - and only seven `.CTL` files ship at all
   (`Meca`, `H1Cmbt`, `H1Avnt`, `D1Cmbt`, `Sham`, `f1cmbt`, `F1Avnt`), none a
   spectre's. So a spectre runs on the area's `.ani` clips alone (the viewer's
   `bank none`), with no channel, no special move and no effect record that
   could damage on contact. **Every data-side lead this tree can read is now
   closed**; the one source left is what the attack LOOKS like in the
   original (the reader's video).

   Original step text: the route the game takes into the catacombs
   (AREA 140's `area.goto 141`, and any SCENE loaded over AREA 141), zone 2309
   "Start Shoot Dogs Entrée" (record 29, the dogs), the checkpoint dialog 100
   at zone 2294 (record 14, Matamboukous), then zone 2295 "Start Shoot"
   (record 15, where the harness began). Read what that route loads - scene
   programs and message handlers - for anything that hurts the player.
5. **Port the dogs' BITE** - PORTED 2026-09-13 (commit pending the shoot
   family's re-baselines). What went in, each piece the engine's:
   * `readActorAttack` / `Session::actorAttack` - `Actor_GetProperty` 0x15 and
     0x16 by clip slot, defaults 2 m and 1;
   * `ShootRecord::attacks[4]` (+112) filled at entry from the group's
     type-12 clips in list order, and `attackSlot` (+108) written by `goPair`;
   * `shootPickAttack` - `sub_421020`, the shortest range that reaches the flat
     squared distance, `rand()` drawn only on a tie (the CRT stream the aim
     jitter already shares) - feeding the engage's `found421020`;
   * `sub_4272B0` on a state change, ported ONLY for its first line (flag 4
     cleared, every state) and cases 9 (state 10: the attack clip by slot,
     `sub_434630`) and 10 (state 11: type 23, else 11);
   * the epilogue's OUTCOME 3: `39 *` property 21 of +108 against the 3D
     distance between the root nodes, flag 4 up, property 22 the damage;
   * `shootApplyStrike` - `sub_423B10` whole: the crusher's -1 first, ACTOR_STATE
     3, 0x800, the DIFFICULTY (row 17, `word_90E1A8`: 0 `v + v/-4`, 2
     `v + v/4`) before the Body Shield, never 0, flags `0x1020`, the band
     (`sub_423E20` is `shootHitBand`), then the reaction or the death;
   * the player's consequences shared with the bolt (`applyPlayerDamage`), the
     bolt's log lines byte-identical.

   **Three port faults the replays exposed, all fixed:**
   * the engage runs BEFORE the step here, inside the arm in the engine - so the
     tick `goPair` entered state 10 also ran state 10 on the OLD clip and sent
     him to 11 at once (robber 77, dog 598). The pair-entry tick now has no step;
   * the clip clock (`clipFrame` / `clipFrames`) was filled for STATE 5 ONLY,
     so state 10 read 0 of 0 as a spent clip; now 5, 10 and 11;
   * the engage was called in state 10, which `sub_424DE0` never does, and would
     rewrite a striking gunman's state mid-clip; now it is not.

   **Measured.** The supermarket (the death route, 800 frames): robber 77 - slots
   19 and 20, 2 m, 4 and 3 - rushes at 441, bolts at 442, enters his attack at
   443 and STRIKES at 459 from 55.8 (4, 3 through the shield, 6 -> 3) and at
   491 (the kill); the phase is lost at 552 and left at 553. His strikes on the
   dead player at 508 and 540 are REFUSED (`sub_423B10`: not ACTOR_STATE 3).
   The gallery (the brain route): unchanged, no gunman within 78 before the
   death at 51. The catacombs: dog 598 enters his attack at 446 from a floor
   below and does not strike - 209 in 3D against his 156 - which is the engine's
   rule; a dog biting in the arena needs a route on his own level.
   **The probe** (`verify.py: shoot generic`, `attack arms`): pick near 20 / far
   19 / none 0 with no draw; a tie keeps the new slot on 0 and the first on 2;
   10 through shield 30 is 7 / 6 / 9 at difficulty 1 / 0 / 2; 1 stays 1; 4 on 3
   kills; -1 kills.

   **Re-baselined, each traced first** - the strike reaches past the dogs:
   `braqueur.ani`'s group 3 holds two type-12 clips (slots 19 and 20), and
   the supermarket's robbers and the gallery's gunmen carry them at 2 m, 3 to
   5 damage. A gunman in reach now takes his attack clip BESIDE the player
   instead of walking into him, so the pushes that had been bending the
   walks and the bolts are gone:
   * `shoot death` - 77 kills with his second strike at 491 (the kill pattern
     now takes a strike as well as a bolt); stood down at once at 492, the
     phase lost at 552 and left at 553;
   * `shoot gunfire` - 77 fires once (441) and strikes instead of firing again,
     so one bolt hit on the player, no barrel line, and his y at -125;
   * `shoot fire` - 237 strikes from 52, 240 from 89, 238 from 94, beside the
     player: eight bolts from one muzzle point, all onto the wall at -3161;
   * `shoot move` - 237 and 238 attack at 67 and 73, 240 strikes from 138: the
     last leg runs straight, -92.82;
   * `shoot entrance` - at 440, 76 units away, 77 takes his attack (6 -> 10)
     where he used to rush onto the player's cell (6 -> 8): both legs straight;
   * `shoot hit` stays red for the reason it was red before, its numbers moved
     (240 also struck and killed on its route).

   **Shown to fail** (each restored by editing the line back, the object
   deleted by name, the probe's output compared with the committed one):
   * the pick keeping the LONGEST reaching range - `pick near 19`, `shoot
     generic` red;
   * the difficulty's sign flipped - difficulty 0 gives 9 where it gives 6,
     `shoot generic` red;
   * **NOT shown**: giving the pair-entry tick its step back left `shoot death`
     GREEN. With the clip clock fed to states 10 and 11, running state 10 on the
     tick it is entered only matters when the OLD clip has less than a frame
     left, and on the supermarket's route it has more - 77 still strikes at 459
     and kills at 491. The skip stays because it is the engine's ORDER (the
     engage runs inside an arm, state 10 first runs on the next tick), and it is
     LABELLED as undistinguished by any route measured.
6. **Port the LANDING messages** - `Walk_GroundResponse`'s 10 and 11 - shown on
   AREA 141's -45.

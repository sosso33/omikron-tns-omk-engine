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
3. **Report the mechanism** (or the negative result, with the enumeration it
   rests on) and propose the port.

# GANDHAR - the lava cave's boss fight (shoot type 10, AREA 2)

Started 2026-10-05 from `todo/drift-audit.md` S8: messages **5-8** were filed
as "shoot action 25's tick", never posted. Reading the tick showed it is not a
detail but a whole boss: **Gandhar's twelve actions are not ported at all.**
`omk::ShootAi::tickGandhar` (`engine/src/actor/shoot.cpp`) walks his three
behaviour scripts as a census and nothing else; in the viewer type 10 has no
branch, so **he fights as the generic gunman** (`sub_424DE0`'s port) - the
same drift the Astaroth fight had before `todo/astaroth.md`.

Plan of the same shape as Astaroth's: read, then port in steps, each verified
headlessly, committed and reported.

## How to reach it

AREA 2 is the cave (`Anekbah Grotte`, the zones' names are `Bras Lave`,
`Dead 1-4`, `INIT SHOOT`). Zone **317** `Pont 2 Dial Gandhar` (record 4,
enter, centre 125 -19 401) runs the meeting: the speech cameras 356-410,
`scx.play.actor 17` (the speech body), the leap (`cam Gandhar Saut`), a
`camera.shake 300, 50`, `character.show 187` and `shoot.actor.enter 187` -
**character 187 is Gandhar**. Headless route to be fixed in step 1, from
`--area 2 --shoot --stand 125,-9,401,<yaw>`.

The rest of the chunk around him: twelve ZOH_FN (172..183, type Zombie, the
generic arm) entered by the cave's other zones; the `Bras Lave` zones kill a
player at 5 hp or less (camera 69, `todo/drift-audit.md` S14b).

## His two forms (the reader, 2026-10-06, from playing the original)

*"Gandhar has two forms: one we see in cutscene or in the background (he does
nothing at this point) then after a cutscene, he jumps in the lava and goes out
in a giant snake-shaped form."*

The data agrees: `Grotte.SCX`'s objects animate two models. The `GD` model
(`GDBassin`: `1_Gandhar`, `1_GandharStand`, `2_K+G_Dial`, `3_K+G_dsLave`) is
the first form; the `D3` model (`D3Bassin`, `D3Tete` - character 187, the one
`shoot.actor.enter 187` makes type 10) is the snake. Between `character.show
187, 1` and the shoot entry the zone waits on `scx.play.wait obj 0x0014` =
object HANDLE 20, `4_D+Pont2`, which animates `D3Bassin` - the snake coming out
by the bridge. The port starts that object but binds no body to a plain
`scx.play`, so for those ~200 frames the snake stands in a default frame:
`todo/drift-audit.md` M2b.

## What the engine does (read 2026-10-05, `readable/src/24_sys.c` 6100-7450)

**The brain `sub_47F6F0`** (type 10's callback, `tables/shoot_ai.json`):

1. `Hud_DrawBar(hp, 200, 1, 0)` - his bar, out of 200; `sub_420C10` (the floor
   cell, as the generic arm does).
2. `hp <= 0`: event 43 `{3, index}` - message **3**, AREA 2's handler 0: `Mort
   Gandhar`, `shoot.end 1`, every zombie hidden, the `Bras Lave` zones off,
   then his death scene and the collapse (`area.goto 43`). Returns.
3. Record `+160` bit 8: set bit 0x800, advance his clip (`sub_47EBF0`) and, when
   it has ended, restart the clip at `+12` and put him on his floor
   (`o3de_SetNodePos(x, +60, z)`).
4. **`dword_657A28`** (set by action 25 on a grab - below): cleared, and the
   NEXT action is entered at once from the script.
5. Otherwise the TICK of the current action (`+156`, code 16..27, through the
   tick table at `0x004CFBC8`); a tick returning non-zero means *done*: he is
   put on his floor and the next action is entered.
6. **The next action**: the health band re-read every time (hp <= 50 critical,
   <= 100 wounded; a band change resets `+100`/`+144`), `sub_47FB40` steps
   the band's script (`+144` the step, `+100` its repeats; a `{0,..}` entry
   rewinds) - the healthy script is walked inline, identically. The code goes
   through the ENTER table at `0x004CFB98`, then `+160 &= ~0x2080`.
7. `sub_420B80` - the floor cell bookkeeping (the generic arm's).

**The twelve actions** (code -> table row; enter / tick):

| code | enter | clip | tick | what the tick does |
|---|---|---|---|---|
| 16 | `sub_47E4F0` | none | `sub_47E520` | WAIT `(rand() & 0x1F) + 30` frames at `+168`, stepping toward the player with `sub_47E5F0` |
| 17 | `sub_47E230` | none | `sub_47E870` | RISE: moves up by `+68 * dt` until `y - +64 <= -150`, stepping in x/z |
| 18 | `sub_47E250` | none | `sub_47E960` | SINK: moves down until `y - +64 >= 250` |
| 19 | `sub_47E270` | type 17 | `sub_47EA50` | plays the clip; when `sub_421020` finds an attack, rolls 25/26 (below) |
| 20 | `sub_47E370` | type 11 | `sub_47F0F0` | steps toward the player while the clip plays; then rolls 25/26 |
| 21 | `sub_47E3B0` | type 18 | `sub_47F2B0` | plays the clip to its end |
| 22 | `sub_47E4B0` | type 18 | `sub_47F640` | plays the clip to its end |
| 23 | `sub_47E310` | type 19 | `sub_47EF10` | FIRES (`sub_44CDF0(.., 1, ..)`, mid-clip, `sub_47EBF0 == 2`); then rolls 25/26 |
| 24 | `sub_47E2B0` | type 11 | `sub_47ED10` | steps toward the player and FIRES on a coin flip (`sub_44CDF0(.., 0, ..)`); then rolls 25/26 |
| 25 | `sub_47E3F0` | `sub_434630(list, 1)` | `sub_47F340` | the **GRAB** - below |
| 26 | `sub_47E430` | `sub_434630(list, 9)` | `sub_47F510` | the **STRIKE** - below |
| 27 | `sub_47E470` | `sub_434630(list, 6)` | `sub_47F6C0` | plays the clip to its end |

Enters 17/18/19/21/22/23/25/26/27 SET channel flag 0x800 (`+160 |= 0x800`),
20/24 CLEAR it; 23/24 store the clip length at `+164` (`sub_434890`).

**The roll** (four ticks: 19, 20, 23 and 24): once the clip has
run and `sub_421020` returns an attack (`+108`), `(rand() % 100 <= p) + 25`
with `p` 100 healthy, 70 wounded, 40 critical - so action **26 always when
healthy**, 25 (the grab) 29% of the time wounded and 59% critical.

**The grab, action 25 (`sub_47F340`)**: plays its clip; when it ends without a
touch, `off_4CFBC0` - the ENTER table's row 10, action **27**, the
recovery clip - and `+160 &= ~0x2080`. When `sub_45BC50` finds his body's sphere centre (his
node chain, `desc+76` rotated) inside one of the PLAYER's mesh spheres
(`sub_45BB20` over the player's hierarchy), the side of his floor's box
(`dword_907D00[floor]`: x min/max, z min/max) he is nearest picks the
message - **min x 7, max x 6, min z 8, max z 5** - posted by event 43 with
the player as sender, and `dword_657A28 = 1`. AREA 2's handlers 5-8
(table entries 2-5) are four KILL scenes, one camera each (`cam south`
...): Gandhar's grab clip and the player's death clip, then `Mort Joueur`, a
ring spent, the cave reset - or `Game Over` with none.

**The strike, action 26 (`sub_47F510`)**: mid-clip (`sub_47EBF0 == 2`) and
not yet struck (`+160` bit 7 clear), on the same touch test: event 44 sets
property **22** on him (`{22, index, 11}`), `sub_423B10(player, 11, the push
from him to the player in x/z)` - damage 11 - and `+160 |= 0x80`.

**The clip clock `sub_47EBF0`** (10 callers): `+188` advances by `dt` with
`+192` the previous; at half the clip's frames it returns 2 (the hit frame),
at the end it wraps to `dt + 1` and returns 0 with a zero root move; the
root delta (`Anim_SetFrame`) moves his node. **`sub_47E5F0`**: a step of
`+68` toward the player (through `sub_4368E0`) gated cell by cell by
`sub_421140` - x first, then z, then both - writing `+244/+252` and
`dword_657A20/1C`.

## Steps

1. ✔ **DONE 2026-10-05.** `engine/src/actor/gandhar.*` (the entry, the clock,
   the twelve enters and ticks, the brain, the 25/26 roll) and the viewer's
   type-10 branch (his entry arm instead of the default arm, his brain instead
   of the generic one, his picked clip's restart, his own clip as the pose, the
   boss bar). Route: `--area 2 --stand 125,-9,401,0 --shoot`, zone 317, he
   enters at frame 875. The healthy script runs in order with its repeats and
   rewinds; he SINKS into the lava to y 255 and RISES to -153; at health 40
   the critical script, at 0 message 3 and the cave's ending (`shoot.end`,
   the collapse, `area.goto`). `engine: gandhar` (four mutations shown to
   fail); `--gandhar-health N` is the instrument. Found on the way: `+64` must
   be set before his arm (it was 0 only by the generic path's order - his
   model's is 0 anyway); `dword_657A28` is the GRAB's restart, not "the
   action finished" (`ShootAi`'s comment corrected); action 22's tick runs its
   cone test on a target it never fetched (stack garbage in the engine; the
   player here, LABELLED). Still stubbed for steps 2-4: the step toward the
   player, the fire, the touch - he turns and plays his actions in place.
   **His look is for a person to judge**: his node sits at y -147 as the
   engine puts it, which draws his body high over the lava (a fire spirit
   with a flame tail, rising and sinking).
   Originally: **His brain in the viewer**: a type-10 branch like Astaroth's - the band,
   the script walk, the enter (flags, the clip by type), the clip clock, the
   done test, the floor placement, the HP bar out of 200, death -> message 3.
   The actions as clip players first (21, 22, 27, and 16's wait). Fix
   the headless route. Check: the action sequence of the healthy script, his
   death posting 3.
2. ✔ **DONE 2026-10-05.** The rise and sink landed in step 1; this is
   `sub_47E5F0`, transcribed whole in `gandharStep`: `+68` along his facing as
   the cone test leaves it (`flt_90E0E4`/`flt_90E0F8` = `(-sin yaw, cos yaw)`,
   now `AcquireOut::fwdX/fwdZ`), the wall test of one step from his RECORD
   point (`sub_421140`, the port's `shootWallTest`) cutting it to x alone, z
   alone or nothing, the byte-2 wall's own arm (a `+speed` nudge along x or
   z), and - when his own spot is refused - the nearest standable point on
   floor 1 from his NODE (`sub_4368E0`), at his entry too. He walks from
   (202, 798) toward the player at 6 a frame, meets his floor's edge at z 573
   (answer 1, map byte 0) and slides along x to (96, 573). `engine: gandhar`
   extended (three mutations shown to fail). The step moves the record point
   and the node by the same amount; the clip's own root motion moves only the
   node, and each action's end snaps the node back onto the record
   (`o3de_SetNodePos(+244, +60, +252)`), as the engine does.
   Originally: **His movement**: `sub_47E5F0`'s step and the rise / sink (17, 18), on the
   floor cells. Check: the rise ends at -150, the sink at 250, the steps.
3. ✔ **DONE 2026-10-05.** `sub_44CDF0` is now the viewer's `recordFire`,
   shared with Astaroth (whose `astarothFire` wraps it with his slot-1 wait).
   Action 23 fires SLOT 1 once at its clip's half from `Tire000000` on his
   TAIL (speed 23, damage 20); 24 fires SLOT 0 on its coin flip from
   `Tire000001` at his HEAD (speed 58, damage 10) as often as the reload
   lets it - 19 shots in his first 24. **And the reload was never counted
   down**: `Actors_TickAll` (the loop at 0x468221) takes the frame delta off
   every actor's four slot timers (`actor+148..+160`) each frame; the viewer
   set them and never decreased them, which Astaroth survived only because
   his reload reads 0. His tail bolt kills the save's player (10 health) at
   frame 977. `engine: gandhar` extended (three mutations shown to fail).
   His model, read on the way: `DE3_FN` is a scorpion-like demon - `D3Tete`
   above the pelvis, eight `Pate` legs, a `Queue` (tail) reaching 280 BELOW
   it - so his node at -147 puts the tail's tip just over his floor (198):
   the engine's height fits his shape.
3b. **THE DAMAGE GATE, found in step 3** (not in the first plan): in
   `sub_4240E0` a hit on type 10 with the record's `0x4000` bit (his entry
   sets 0x4020) and source kind 6 goes through `sub_47DF60(him, victim,
   damage, the hit)`: `sub_45E9C0` tests the hit against the PLAYER's body
   with `dword_657A24` - his `D3Tete` node, found by `sub_41E210` at his
   entry - and refuses the damage when it misses; a pass spawns effect 19
   at the meeting point. So he can be hurt only through his HEAD (as
   Astaroth only through his back).
   ✔ **DONE 2026-10-05.** `sub_45E9C0` is the bolt's own body sweep, and its
   fifth argument (`dword_53AA9C`) makes `sub_45ECA0` test that ONE mesh -
   the port's `shootSweepBodies(.., onlyActor, onlyMesh)`, as Astaroth's
   back. The type-10 arm of `HitIn::typeGate` sweeps for his `D3Tete`; a
   meet plays effect 19 at `*(D3Tete)+36` and passes the damage, a miss
   refuses it (`shootApplyHit` called the gate for type 13 only). **Three
   gates in a row, and a reader's testimony behind the second** (*"against
   demons a special weapon is needed, the baton de pouvoir - a Waver does
   nothing"*): `+160 & 0x800` refuses every hit, and nine of his twelve
   actions set it - he can be hurt only while he WALKS (20, 24, and 16 if it
   follows one); `0x4000` refuses all but the baton (damage 6; the Waver's
   is 5 - 20 of 20 refused); then the head. **And a DRAWING fault had made
   him unhittable**: the viewer seated him by his FEET - the tip of a tail
   280 below his pelvis - and added his node's offset on top, so his head
   drew ~330 above the node, above the cave's ceiling (`GGplaf05`, y -257),
   where every bolt struck. His node is his pelvis (the root mesh's world
   point): he is drawn pelvis-on-node now (`s.pelvis`), his head at y -126
   to -200. THE ROUTE IS THE CAVE'S OWN now: zone 313 (`Départ Shoot
   Grotte`: `inventory.save`, `shoot.begin 40` - the BATON - and the twelve
   zombies) then the bridge, zone 317 - `--stand 1811,-9,1216,0 --player-at
   60:125,-9,401,0`; the `--shoot` harness equips the WAVER and is wrong for
   him. `engine: gandhar head` (new, three mutations shown to fail).
4. ✔ **DONE 2026-10-06.** The BODY TOUCH (`omk::shootBodyTouch`,
   `actor/shoothit.h`): his root mesh's sphere (`+76` through the node, `+88`
   = 381 for him) against the box of every non-root node of the player,
   both as posed last frame (inside his own turn `drawn` is cleared, so the
   viewer asks for last frame's meshes explicitly). The STRIKE tests it from
   its clip's half once and deals his property 22 (11, the event-44 struct's
   own default when he has none) through `sub_423B10` - shared with
   Astaroth's slam now (`PlayState::strikePlayer`). The GRAB tests it at its
   clip's end; contact posts the side's message from the PLAYER (`gandharGrabSide`,
   read from the assembly at 0x47F3F0..: the nearer x side or the nearer z
   side of his floor's box, x on a tie; min x 7, max x 6, min z 8, max z 5)
   and AREA 2 runs the kill scene (message 8: `cam south`, camera 383, a ring
   spent, the restart). Contact is real only from the EDGE of the player's
   walkway (z ~379; his floor starts at 414) - both clips lunge him forward
   ~70. `--player-at` takes a second, later placement (the instrument for
   it). `engine: gandhar grab` (new, three mutations shown to fail).
   Originally: **The strike and the grab**: the touch test (`sub_45BC50`/`sub_45BB20`),
   the roll, 26's damage and push, 25's side -> messages 5-8 and the
   `dword_657A28` restart. Check: each side's message, the kill scene runs.
5. ✔ **DONE 2026-10-06 - and the task with it.** From the cave's own start
   (zone 313's baton, the bridge's zone 317) to the cave's end: the player's
   baton bolts meet his head while he walks and take him down (from 12
   health, two hits - `--gandhar-health`, so that a fixed `--aim-at` can do
   it; the gate itself is `engine: gandhar head`'s); his brain posts message
   3; AREA 2 ends the shoot, plays `DEAD.3DA` on the path `D3BassinD1` (he
   sinks into the lava - rendered and looked at), the collapse (camera 365,
   the set pieces hidden), and `area.goto 43`. `engine: gandhar play` (new,
   shown to fail). **What is left, and none of it in this plan**: the reader's
   eye on his height, reach and kill scenes in play (`todo/play-test.md` 31),
   and an aim harness that follows a node, for a full-health play-through.
   Originally: **The play-through**: from zone 317 to message 3 headlessly, renders, the
   play-test entry, the drift-audit row closed.

Not in scope: the twelve zombies (the generic arm, already run).

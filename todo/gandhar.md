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

1. **His brain in the viewer**: a type-10 branch like Astaroth's - the band,
   the script walk, the enter (flags, the clip by type), the clip clock, the
   done test, the floor placement, the HP bar out of 200, death -> message 3.
   The actions as clip players first (21, 22, 27, and 16's wait). Fix
   the headless route. Check: the action sequence of the healthy script, his
   death posting 3.
2. **His movement**: `sub_47E5F0`'s step and the rise / sink (17, 18), on the
   floor cells. Check: the rise ends at -150, the sink at 250, the steps.
3. **His fire**: 23 and 24 through `sub_44CDF0` (ported for Astaroth). Check:
   bolts from his marker, the coin flip of 24.
4. **The strike and the grab**: the touch test (`sub_45BC50`/`sub_45BB20`),
   the roll, 26's damage and push, 25's side -> messages 5-8 and the
   `dword_657A28` restart. Check: each side's message, the kill scene runs.
5. **The play-through**: from zone 317 to message 3 headlessly, renders, the
   play-test entry, the drift-audit row closed.

Not in scope: the twelve zombies (the generic arm, already run).

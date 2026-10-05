# ASTAROTH - the end-game boss fight (shoot type 13)

Asked 2026-10-05 ("Go for the Astaroth fight"), out of the drift audit's S8:
messages 27..32 are his six weak points, and the port had no Astaroth, so the
fight could not be won. **One task**, ported in the steps below; each step
ends in a commit, a check shown to fail, and a report.

## How to reach it, in one command

```bash
cd engine && SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables \
    --save ../traces/save-appart.bin --area 175 --address 526 \
    --zone-enable 2936 --zone-disable 2935 --frames 600
```

Zone 2936 'Restart Shoot' (AREA 175 record 2) is the shipped retry: it
replays the soul actors 653..658 and the set programs, shows 609
(`AST_FNM`), and runs `shoot.begin 40` (the Baton de pouvoir) and
`shoot.actor.enter 609` - the same two calls as zone 2935 'Astaroth', without
dialog 335 in front, which waits for a reply a headless run cannot give.
The save's player has Vie 10; the script's own death handler sets 200.

## What the port did before this task

He entered as a GENERIC gunman: `ShootMode::actorEnter` gives every actor
action 1 (the patrol) - which `Shoot_ActorEnter` never does: it hands the
type to `sub_47DFD0` and runs NO action (type 13 -> `sub_47FF70`, type 10 ->
Gandhar's entry, anything else -> a type-11 clip in `+8` and state 0). So:
no `0x4020`, and the baton's damage 6 was REFUSED by `shootApplyHit` (a
non-0x4000 victim refuses the player's 6 unless type 11) - he could not be
hurt; no world-hit callback, so no soul could be struck and messages 27..32
never fired; `ShootAi::tickAstaroth` (`actor/shoot.cpp`) is a graph walk with
the three wrong labels below, used only by `engine/tools/run_shoot_ai.cpp`.

**The generic entry action is a drift of its own** for every gunman (the
original runs none at `shoot.actor.enter`): recorded in
`todo/drift-audit.md`, not part of this task.

## Steps

**Reaching his back**: `--astaroth-souls 10 --player-at
12:31950,1052,-2504,90 --aim-at 32206,668,-2507` - his `AstDos` is drawn at
(32206, 668, -2507) while he stands on the retry's stand; 200 health is 34
bolts of 6, 45 presses at `--keydelay 20` in 1000 frames.

**Reaching the souls**: from the restart point (address 526) `PAame02`,
`PAame04` and `PAame05` are in line of sight; `PAame01`, `03` and `06` are
behind `PAcylind01`, `PAentrext` and `PAroche01` - the player must move. A
check that shoots Astaroth must hide character 34 (`--hide-show
34,1,100000,0`): the conversation's Astaroth stands where 609 does and the
retry zone does not hide him, record 1 does. `--nodelay` makes a fire run
repeatable; five `54` presses at `--keydelay 40` give four bolts in 220
frames.

| # | step | state |
|---|---|---|
| 1 | **The setup and the souls.** `Shoot_ActorEnter`'s type-13 arm (`sub_47FF70`): state 29, `+88 = 10`, flags `|= 0x4020`, the six `PAame0N` set meshes found and shown at 3 hits each, `AstDos` found, the difficulty factor. The bolts' world ray reports the struck SET MESH and calls the registered callback where `Projectiles_Tick` does (0x44DDDF, only when no body was met); `sub_47FCF0` counts the hit, HIDES the mesh's subtree (`sub_436F20`, flag 2 - the render drops it, the ray does not) and posts message `27+i` with sender `i`. The generic brain no longer runs for him (his own tick is step 3) | **DONE 2026-10-05** (`dce6ca0`): `actor/astaroth.*`; `SweepHit::tri` and `WorldRay`'s mesh; `WorldSlot::meshHidden` honoured by the visible-set walk (and the moving meshes); `shootApplyHit`'s `typeGate`; the `--aim-at` harness. `engine: astaroth souls` (3 mutations shown to fail). Limits, labelled: he HOLDS the stand grid's centre cell and does nothing (step 3); once all six are down his gate still refuses (step 2) |
| 2 | **His body and his death.** `sub_4240E0`'s type-13 gate `sub_47FD90`: only once all six are down, not in 17..19, a bolt travelling WITH his facing, whose segment meets the `AstDos` mesh (the body sweep with a one-mesh filter, the player excluded); the back-hit reaction (a type-4 clip, flags `8 | 0x800`, state 16, `+88 = 10`), his waiting bolts cancelled, effect 20; the `+88` flinch counter on any other hit. The tick's prologue: the picked clip, its `+100` replays, and **message 3** when `+92 <= 0` | **DONE 2026-10-05** (`7431c08`): `astarothGate` / `astarothBand` / `astarothClipOver`; `shootSweepBodies`' one-mesh filter; `ProjectilePool::cancelWaiting`; `FlightEvent::seg`; `GunClip::slot` (clip 14 shares type 0 with five); his picked clip advanced at `flt_6A062C`; a kill takes no generic death path. `engine: astaroth back` (3 mutations). Played headless to the end: 34 back hits, message 3, `shoot.end 1`, the ending runs. Harnesses `--player-at`, `--astaroth-souls`. Limits, labelled: the picked clip is the fixed pick, not `rand()`; effect 20 is its SOUND only (as every shot effect here); every resumed state stands on the stand grid's centre cell (step 3) |
| 3 | **His tick** `sub_4800C0`: the health bands (animation rate, slot-1 wait), the 9-cell stand / walk grids aimed at the player (`sub_4B2F30`, `sub_4B30A0`/`sub_4B3260`, `sub_4B33C0`), slot 0's fire once a cell, the walk's turn; 17 / 18 / 19 the wind-up, the crouch and the LEAP, the SLAM's damage through `sub_423B10`; 21 the big shot, slot 1 at frame 2; the default arm's re-setup. His weapon slots' objects are UNVERIFIED and are read first | |
| 4 | **`camera.shake` / `Camera_SetShake`** (0x414DB0): his footstep (30, 10) and the script's 100/100 and 20/20 (drift audit S11, 32 sites). Read the CONSUMER of `cam+196` / `+204` first - unread | |
| 5 | `ShootAi::tickAstaroth`'s labels replaced; the play-test entry; the drift-audit row | |


---

# Appendix - the reading (2026-10-05, from the assembly)

Read 2026-10-05 from the no-CD `gamedata/Runtime 2.exe` (objdump of the image) and
`readable/src/*.c`. Every claim carries its address. "UNVERIFIED" marks what was
not confirmed. Record = the 192-byte shoot record (`g_ShootRecords` = dword_90E10C,
`rec = g_ShootRecords + 192*idx`); actor = the actor record (`a1` of the brain).
Frame delta `dt` = `flt_4C30D8` (1.0 at 30 fps).

Three corrections to what the port's `engine/src/actor/shoot.cpp` and `docs/ASSETS.md`
say today, each from the assembly:

* the "impulse 3700/2300/1200" is **DAMAGE**: `sub_423B10(player, dmg, dir)` at
  0x4804EB is the direct-damage function (05_sys.c 4361), not a push;
* the "turn 60/40/30" is **not a turn rate**: it is written every tick into
  `+24` of the shot-sprite row of his weapon SLOT 1 (`sub_44F020(dword_657AF0, v)`,
  0x4802FB; row +24 = the frames a bolt WAITS at the muzzle, `formats/sfx.h` 179);
* the "speed 1.0/1.5/2.0" is the **animation rate** `flt_6A062C` that every one of
  his clip advances and root-motion windows uses (only his functions read it:
  all 16 references are in 24_sys.c / 29_win32.c's 0x4B2xxx-0x4B3Exx).
* the sequence 16->17->18->19 is not a grapple and throw: it is a **wind-up, a
  crouched wait, a homing LEAP onto the player's position, and a ground SLAM**
  whose damage falls off with distance. The clip names are all `NULL` in
  `astaroth.ani`, so these are functional names, not the game's.

## Astaroth's clip group (ANIMS\astaroth.ani, group 13)

Read with `tools/anim_ani.py` (node +0 type, +4 id/slot). The brain asks for most
clips BY ID through `sub_434630` (09_ddraw.c 1487: matches node +4, first hit), and
a few BY TYPE through `List_PickRandomByType` (0x4345E0, random among the type).

| id | type | frames | asked by | use |
|---|---|---|---|---|
| 1 | 18 | 449 | `sub_4B2EE0` id 1 | the WALK, a 9-cell aim grid (cell length (449+1)/9 = 50) |
| 13 | 18 | 449 | `sub_4B2E90` id 13 | the STAND, a 9-cell aim grid (state 29) |
| 4 | 0 | 44 | `sub_4B39A0` id 4 | wind-up (state 17) |
| 12 | 0 | 31 | `sub_4B39F0` id 12 | crouched wait, looped (state 18) |
| 5 | 0 | 19 | `sub_4B3B40` id 5 | the leap (state 19) |
| 6 | 0 | 69 | `sub_4B3E10` id 6 | landing, played as a "picked" clip (flag 8) |
| 11 | 0 | 129 | `sub_4B37C0` id 11 | the big shot (state 21; fires slot 1 at frame 2) |
| 14 | 0 | 9 | `sub_47FD90` id 14 | flinch, looped x10 (+100 = 10) |
| 15,16 | 4 | 16 | `sub_47FD90` type 4 (random) | hit IN THE BACK |
| 3 | 9 | 49 | `sub_4B2DB0` type 9 | state 20's timeout (state 20 unreachable, see 3) |
| 2 | 11 | 30 | (Shoot_ActorAction's idle) | not used by his brain |
| 8/9/7 | 30/31/32 | 30/31/45 | `sub_421C00` | turn -90 / +90 / 180 |
| 10 | 17 | 1 | - | stance |

**He has NO death clip**: types 5/6/7/8 are absent, so `sub_4240E0`'s kill branch
(05_sys.c ~4800) picks nothing and only sets flag 8 (see 4).

His actor record (AREA 175, actor 609, `AST_FNM`): property 1 health **200**, +176
type 13, ranges 90/90/90 m, cone 90, property 37 bits 1, property 24 = 0, property
17 = 250 (read with the 276-byte layout of `engine/src/script/props.cpp`).

---

## 1. `sub_44CD90` - what it registers and where it is called

```c
// 0x0044CD90 (17_script.c 907)
fn sub_44CD90(fn a1) { off_4C8444 = nullsub_5 /*0x44CD80*/; if (a1) off_4C8444 = a1; }
```

Three callers (all in the image): `Shoot_Enter` 0x4222F6 `sub_44CD90(0)` (reset),
`sub_47FF70` 0x47FFDC `sub_44CD90(sub_47FCF0)` (Astaroth's setup), `sub_47FCF0`
0x47FD74 `sub_44CD90(0)` (all six souls down).

**The ONLY reader of `off_4C8444`** is one indirect call in `Projectiles_Tick`
(0x0044D930), at **0x44DDDF** (`calll *0x4c8444`; 17_script.c 1658). The path, per
live entry, per step:

```c
// Projectiles_Tick, 17_script.c 1607..1670
sub_45E9C0(seg, owner->node, &entry[1] /*hit node*/, &tri, 0);   // the BODY sweep
if (entry[1]) { ...victim = Actor_FromNode(root of it); if (victim && victim != owner)
                    sub_4240E0(victim, entry+56 /*damage*/, entry, seg); v0 = 1; else entry[1]=0; }
if (entry[1]) { record it } else {
    v0 = 0;
    if (sub_4449E0(seg.a, seg.b, hitPt, -1, &dist, &meshNode /*v51*/, &tri)) {   // the WORLD ray
        off_4C8444(meshNode);                 // 0x44DDDF  <-- the callback
        impact effect (entry+48 row +8) at hitPt; explosion if damage > 10; v0 = 1;
    }
}
if (entry+12 > 1968.5039 || v0) retire the entry;
```

The argument is **the SET MESH NODE the bolt's world ray struck**: `sub_4449E0`'s
6th argument (`*a6 = ctx+272`, 0x444B7A..0x444B8E with ctx = ebp-0x1F0), and
`ctx+272` is copied from `ctx+200` (the mesh node `sub_444460` is visiting, its
`a2`) by `sub_444BB0` when a triangle hit is the nearest so far (16_o3de.c, lines
92..111 of `sub_444BB0`: `u32(a5,272) = u32(a5,200)`). The asm at 0x44DDDA reads
`0x2c(%esp)`, the slot pushed as a6 at 0x44DDB6. Consequences:

* the callback runs only for a bolt that met **no body** this step (a body met first
  - Astaroth, or one of the soul actors 653..658, see 5 - stops the bolt there);
* it runs for **every** bolt of **every** owner, any damage: `sub_47FCF0` does not
  test the owner or the damage (UNVERIFIED in play: whether Astaroth's own bolts
  ever reach a soul mesh);
* `sub_4449E0` is the same set walk as the bolts' world (`o3de_ForEachMeshInBox`
  0x4430A0 + `sub_444460`): meshes with flag 0x800000 skipped; with ctx+444 set
  the 0x800 cutouts too (the port's `SoupKind::Sight` comment says `sub_4449E0`
  sets it; read here only as far as `sub_444460` 0x44448C testing it). Nothing in
  `sub_4430A0` / `sub_444460` tests mesh flag **2** (hidden), so a soul hidden by
  `sub_436F20` still stops a bolt - harmless, since its slot is then 0.

## 2. `sub_47FF70` - the setup, and its two callers

Callers: `sub_47DFD0` 0x47E038 (24_sys.c 6022: `else if (a3 == 13) return
sub_47FF70(idx, actor)`), which `Shoot_ActorEnter` (0x00422C10) calls at 05_sys.c
3815 for EVERY entering character with its type, after `+96 = player`,
`+156 = 0`, the node placed and +112.. filled; and `sub_4800C0`'s default arm
0x48060B (see 3).

```c
// 0x0047FF70 (24_sys.c 7563), a1 = shoot index, a2 = actor record
dword_657AFC = 0;                         // souls destroyed
rec+156 = 29;                             // state: stand
rec+88  = 10;                             // the off-target hit counter (see 5)
rec+160 |= 0x4020;                        // 0x4000: only damage 6 reaches him; 0x20
Actor_GetPosAndFacing(a1, pos);
sub_4B2E90(a2, a1, rec, pos);             // start clip id 13 (the stand grid), see 3
sub_44CD90(sub_47FCF0);                   // the world-hit callback
memset(0x657A30, 0, 0xC0);                // 192 bytes; NO other reader found in readable/ (UNVERIFIED purpose)
dword_657AF4 = o3de_FindMeshByName(a2->node /*actor+8*/, "AstDos");   // sub_41E210 (05_sys.c 631)
for (i = 0; off_4CFCD0[i]; ++i) {         // 6 names, NULL-terminated
    dword_657B00[i] = o3de_FindNodeByName(dword_93076C /*the 3D world root*/, off_4CFCD0[i]);
    sub_436F50(dword_657B00[i]);          // SHOW: clear mesh flag 2 over the subtree (10_dsound.c 1790)
    dword_657B18[i] = 3;                  // three hits each
}
dword_657AF0 = sub_44EEB0(name of actor+88's node);   // slot 1's SHOT-SPRITE row (shoot2.sfx section A)
flt_6A062C = 1.0;
flt_657AF8 = word_90E1A8 == 0 ? 1.0 : word_90E1A8 == 1 ? 1.2 : word_90E1A8 == 2 ? 1.4 : (unchanged);
```

* `off_4CFCD0` (read from the image): `PAame01`..`PAame06` at 0x4CFD98..0x4CFDC0,
  then 0. `dword_4CFCF0` = {27, 28, 29, 30, 31, 32} - the message per soul.
* The six are **set meshes of `MESHES\DECORS\PAstarot.3DO`** (mesh_list: ids 27..32,
  flags 0x3004 / 0x3004 / 0x0004 / 0x3000 / 0x3004 / 0x3004 - additive, the "âmes";
  a seventh `PAame10`, id 35, is never named). They are the only file carrying the
  names besides the exe.
* `AstDos` is mesh 11 of `MESHES\PERSOS\AST_FNM.3DO` (his BACK, parent 13).
* `word_90E1A8` is the shoot difficulty (options row 17; `shoothit.h` 141).
* `sub_41E210` = `*a3 = o3de_FindMeshByName(a2, a1); return 1;` - a mesh pointer.
* `sub_436F20` / `sub_436F50` = traverse the subtree setting / clearing bit 2 of each
  mesh's flags (`*(*node) |= 2` / `&= ~2`), the drawable mask's hidden bit.

`sub_4B2E90` / `sub_4B2EE0` = `sub_4B2F30(actor, idx, rec, pos, sub_434630(rec+20, 13 / 1))`:

```c
// 0x004B2F30 - start a grid clip
rec+184 = 0;  rec+12 = rec+8;             // remember the previous clip
rec+160 = (rec+160 & 0x20000000) ? rec+160 | 0x10000000 : rec+160 & ~0x10000000;
rec+8 = clip;  actor+168 = 0;  sub_434A90(actor, clip->desc, 0, 0);
actor+192 = 0; actor+1280..1286 (4 x u16) = 0; rec+160 &= 0x7FFFFFFF;
actor+188 = 1.0; actor+1272 = actor+1276 = 0;
sub_4725B0(actor->node, desc, 0.0, 1.0, d, actor+1272);       // the root delta 0->1
actor+244 += d.x; actor+252 += d.z;
o3de_SetNodePos(actor->node, actor+244, rec+60 + d.y, actor+252);
Actor_GetPosAndFacing(idx, pos);
sub_4B30A0(actor);                        // aim the grid at the player
sub_4725B0(actor->node, desc, actor+192, actor+188, d, actor+1272);   // pose
rec+160 &= ~0x20000000;
```

`sub_4B30A0` (0x4B30A0) + `sub_4B3260` (0x4B3260, flags read from the asm): yaw
`actor+452` = signed angle between the node's -Z (through `node+156`) and
(player root node - his `actor+24` node), clamped +-45; pitch `actor+456` =
`asin(-dy/dist3d)` in degrees; `actor+1268 = 9`; then the 3x3 blend into
`actor+1272`:

```c
L = (clipFrames /* *(actor+168) */ + 1) / 9;
row = pitch < 0 ? 6 : 0;  pitch = clamp(pitch, -33, 33);
col = yaw   < 0 ? 0 : 2;  yaw   = clamp(yaw,   -45, 45);
blend+8  = 4*L;  blend+14 = (col+3)*L;  blend+10 = (row+1)*L;  blend+12 = (row+col)*L;
blend+0  = clamp((int)(pitch*256 * (pitch > 0 ? 0.030303 : -0.030303)), 0, 256);   // |pitch|*256/33
blend+4  = clamp((int)fabs(yaw*256*0.022222), 0, 256);                             // |yaw|*256/45
```

That is the same four-offset / two-weight blend the port already runs for the
player (`sub_4725B0`, `engine/src/actor/player.cpp` 1458 `gridTracks`).

## 3. `sub_4800C0` - the tick, whole (24_sys.c 7624; asm 0x4800C0..0x480663)

`a1` = actor record, `a2` = shoot index (`Shoot_TickNpc` 0x4279C0 calls the record's
+0 with `(actor, idx)` every frame unless `g_PlayerBehaviourOff`, alive or dead).
`pos` = `Actor_GetPosAndFacing(idx)` (x, y, z, yaw). `tgt` = `rec+96` (the player).

```c
Actor_GetPosAndFacing(idx, pos);
sub_420C10(rec);                     // put his cell's saved byte back (port: the brain prologue)
rec+168 -= dt;                       // the one timer
Hud_DrawBar(rec+92, 200, 1, 0);      // his gauge, slot 1, scale 200
if (actor+420 < 0)    actor+420 += 360;     // yaw wrap: note > 359, not >= 360
if (actor+420 > 359)  actor+420 -= 360;
actor+248 = rootMesh(actor+8)->+40;  // his y re-read from the root node every tick
hp = rec+92;  rate = 1.0; wait = 60.0;
if (hp < 100) { rate = 1.5; wait = 40.0; }  // `<`, 0x480171 / 0x48019D (cmp 0x64 / 0x32; jge)
if (hp < 50)  { rate = 2.0; wait = 30.0; }
flt_6A062C = flt_657AF8 * rate * dt;       // HIS ANIMATION RATE
wait = wait / flt_657AF8;

if (rec+160 & 8) {                   // a picked clip is playing
    if (sub_421770(actor, rec, pos, flt_6A062C)) return;   // advance it (05_sys.c 2922; port has it)
    // the clip is over: sub_421770 has cleared flag 8
    if (--rec+100 > 0) { actor+188 = 1.0; actor+192 = 0; rec+160 |= 8; return; }   // replay it
    sub_420C10(rec);
    if (rec+92 <= 0) { Game_RaiseEvent(43, {3, idx}); return; }     // 0x480242: MESSAGE 3, he is dead
    switch (rec+156) {               // resume the state's own clip (table 0x480664/0x480678)
      case 16: sub_4B2EE0(...);  break;   // walk grid, id 1
      case 17: sub_4B39A0(...);  break;   // id 4, via sub_421A20
      case 21: sub_4B37C0(...);  break;   // id 11, via sub_421A20
      case 29: sub_4B2E90(...);  break;   // stand grid, id 13
      default: rec+160 &= ~8; sub_421A20(actor, rec, rec+12 /*previous clip*/, 0);
    }
    sub_4239D0(idx, Actor_Index(g_PlayerActorRec));   // rec+96 = the player (05_sys.c 4303)
    // FALLS THROUGH into the body below, same tick (0x4802EA -> 0x4802ED)
}
if (dword_657AF0) dword_657AF0->+24 = wait;   // sub_44F020: slot 1's muzzle WAIT, frames
rec+100 = 0;  rec+16 = 0;  rec+160 &= ~0x800;  // 0x480303..0x48031D: the hit-immunity drops here

switch (rec+156) {                   // table 0x480688, states 16..29
case 16:                             // WALK toward the player
    if (sub_4B33C0(actor, idx, rec, pos)) {          // one grid cell (50 frames) completed
        sub_4B2EE0(actor, idx, rec, pos);            // restart the walk grid
        Actor_GetPosAndFacing(tgt, tmp);             // (result unused)
        Camera_SetShake(dword_9307E4, 30.0, 10);     // the footstep: cam+196 = 30, cam+204 = 10*0.3937
    }
    Actor_GetPosAndFacing(tgt, t);
    if (hypot(pos.x - t.x, pos.z - t.z) < 195.0) {   // 0x4BCBCC
        sub_4B39A0(actor, idx, rec);                 // id 4
        rec+156 = 17;
    }
    break;
case 17:                             // WIND-UP
    if (sub_4B39F0(actor, idx, rec, pos)) { rec+156 = 18; sub_420B80(actor, rec); }
    else sub_420B80(actor, rec);
    break;
case 18:                             // CROUCHED WAIT
    if (sub_4B3B40(actor, idx, rec, pos)) { rec+156 = 19; sub_420B80(actor, rec); }
    else sub_420B80(actor, rec);
    break;
case 19:                             // THE LEAP
    if (sub_4B3E10(actor, idx, rec, pos)) {          // the leap clip ended; rec+16 = id 6 queued
        Actor_GetPosAndFacing(tgt, t);
        d = hypot(pos.x - t.x, pos.z - t.z);         // 0x480463..0x480484
        if (d < 273.0) {                             // 0x4BCBD0
            dir = { t.x - pos.x, 0, t.z - pos.z };   // 0x480498..0x4804AE
            dmg = d < 78.0 ? 3700 : d < 156.0 ? 2300 : 1200;   // 0x4BCBD4 / 0x4BCBD8
            sub_423B10(tgt, dmg, dir);               // DIRECT DAMAGE to the player
        }
        rec+168 = 150.0;                             // 0x43160000
        rec+156 = 21;
        sub_4B37C0(actor, idx, rec);                 // id 11 (the tail then overrides with id 6)
    }
    sub_420B80(actor, rec);
    break;
case 20:                             // FACE THE PLAYER (no writer in his own code)
    if (sub_4B3630(actor, idx, rec, pos)) { sub_4B37C0(...); rec+156 = 21; }
    else if (rec+168 <= 0.0) { sub_4B2DB0(...); rec+156 = 16; }   // type-9 clip
    break;
case 21:                             // THE BIG SHOT
    if (sub_4B3800(actor, idx, rec, pos)) {
        rec+168 = 60.0;  rec+156 = 16;  sub_4B2EE0(...);       // back to the walk
    }
    break;
case 27: break;                      // inert
case 29:                             // STAND while souls remain
    if (sub_4B33C0(actor, idx, rec, pos)) {
        if (dword_657AFC >= 6) { sub_4B2EE0(...); rec+156 = 16; }   // all six down: walk
        else sub_4B2E90(...);                                         // restart the stand grid
    }
    break;
default:                             // 22..26, 28: RE-SETUP
    sub_47FF70(idx, actor);          // resets souls to 3 hits each, shows all six, state 29
    sub_4B2DF0(actor, idx, rec, pos);
}
if (rec+16) {                        // a queued picked clip (id 6, or a turn clip)
    keep = rec+184;
    sub_421A20(actor, rec, rec+16, 0);
    rec+160 |= 8;  rec+100 = 0;  rec+184 = keep;
}
```

The helpers (29_win32.c 4794..5525; none of them is in the port):

* **`sub_4B33C0`** (grid cell, states 16 and 29):
  ```c
  if (rec+180 && rec+172 <= 0) rec+172 = *(float*)rec+180;   // the weapon row's rate (nothing here decrements +172)
  actor+192 = actor+188;  actor+188 += flt_6A062C;
  L = (frames(rec+8) + 1) / 9;  last = L - 1;
  wrapped = 0;
  if (actor+188 >= last) {
      o3de_SetNodePos(node, actor+244, rec+60, actor+252);   // re-seat on his floor height
      actor+188 = actor+188 + 1 - last;  actor+192 = 0;  wrapped = 1;
  }
  if ((int)actor+192 < L/2 && L/2 <= (int)actor+188)
      sub_44CDF0(actor, 0, playerPos);                        // FIRE weapon SLOT 0 at mid-cell
  rec+52 = pos.x; rec+56 = pos.z; rec+164 -= flt_6A062C;
  sub_4B30A0(actor);                                          // re-aim the grid
  if (rec+156 != 29) sub_421CD0(actor, rec, 1);               // the grid-field TURN, walk only
  sub_4725B0(node, actor+168, actor+192, actor+188, d, actor+1272);   // blended root delta
  actor+244 += d.x; actor+248 += d.y; actor+252 += d.z;
  o3de_MoveNodeBy(node, d.x, d.y, d.z);                       // NO wall test
  Actor_GetPosAndFacing(idx, pos);  rec+136/+140 = sub_435770(rec+188, pos.x, pos.z);  // his cell
  return wrapped;
  ```
  So in 29 he stands, aims (+-45 / +-33 deg blend), does not turn, and fires slot 0
  once per 50-frame cell; in 16 he also turns along the path field and walks.
* **`sub_4B39F0`** (state 17): `+192 = +188; +188 += rate`; while `< frames`: pose
  (`sub_434C30` with `rec+84` = `unk_4C37E8` for type 13), `rec+52/56 = pos`,
  root delta (`sub_434D30`), move node and +244..252, re-read pos, return 0. At the
  end: `sub_421A20(actor, rec, id 12, 0); rec+168 = 30.0; return 1`.
* **`sub_4B3B40`** (state 18):
  ```c
  Actor_GetPosAndFacing(tgt, p);
  fl = sub_435020(p.x, p.y, p.z, -1);                         // the player's floor
  cell = fl == -1 ? (int)actor /* garbage, as shipped */ : sub_435770(fl, p.x, p.z);
  refused = sub_4353E0(fl, cell & 0xFF, cell >> 16);          // the AI refuses his cell
  los = sub_4359A0(rec+188, {rec+136, rec+140}, {tgtRec+136, tgtRec+140}, 1);
  if (!los || (refused && rec+168 >= 0.0)) {                  // asm 0x4B3BF5..0x4B3C12
      advance id 12 by rate; at its end: +188 = rate + 1, +192 = 0,
          actor+248 += rec+60 - rootMesh+40, SetNodePos(node, pos.x, rec+60, pos.z);
      pose, root delta, move node, re-read pos; return 0;
  }
  sub_421A20(actor, rec, id 5, 0);  n = frames(id 5);          // 19
  if (refused) step = {0, 0};
  else step = {(player+244 - actor+244)/n, (player+252 - actor+252)/n};   // dword_69A75C / dword_69A758
  return 1;
  ```
* **`sub_4B3E10`** (state 19): `+192 = +188; +188 += rate`; while `< frames`: pose;
  `dy` from `sub_434D30`; `actor+244 += stepX*flt_6A062C; actor+248 += dy;
  actor+252 += stepZ*flt_6A062C;` **node moved by (stepX*dt, dy, stepZ*dt)** - the
  record uses the scaled rate and the node the raw dt, as shipped; re-read pos;
  update +136/+140; return 0. At the end: `rec+16 = id 6; rec+184 = 0; return 1`.
* **`sub_4B3800`** (state 21): advance id 11 by rate; at the end return 1. Else:
  `if ((int)+192 < 2 && (int)+188 >= 2) sub_44CDF0(actor, 1, playerPos)` - **FIRE
  SLOT 1 at frame 2**; `if (!sub_420C70(rec, pos, playerPos) || flt_90E114 <
  0.8*sqrt(flt_90E118)) sub_420EB0(actor, 0)` (the port's `shootAcquires` +
  turn, no snap); pose; `rec+52/56 = pos`; root delta; move; return 0.
* **`sub_4B3630`** (state 20): advance the current clip; at its end `+188 = rate+1,
  +192 = 0, SetNodePos(node, pos.x, rec+60, pos.z)`; pose; root delta; move; aimed
  (`sub_420C70` and `flt_90E114 >= 0.8*sqrt(flt_90E118)`) -> return 1; else
  `sub_421C00(actor, rec, sub_420EB0(actor, 0))` (05_sys.c 3118: 90 -> type 31,
  180 -> type 32 + a `rand()`, -90 -> type 30, queued in `rec+16` with
  `rec+184 = angle/frames`; no clip -> `actor+420 += {5,10,-5} * dt`) and return 0.
* **`sub_4B2DB0`**: `sub_421A20(actor, rec, List_PickRandomByType(rec+20, 9), 0)`.
* **`sub_4B37C0` / `sub_4B39A0`**: `sub_421A20(actor, rec, sub_434630(rec+20, 11 / 4), 0)`.
* **`sub_4B2DF0`** (default arm): `sub_421CD0(actor, rec, 1); sub_421370(actor, rec,
  pos, 1.5*flt_6A062C); sub_434C30(...)`; returns a frame test that is discarded.

**Unreachable / dead in his own machine** (searched `sub_4800C0`, `sub_47FD90`,
`sub_47FF70` and the 0x4B2DB0..0x4B3E10 helpers): nothing writes state **20** or
**27**, so state 20 is reachable only through an outside `+156` write
(`Shoot_ActorAction` 0x423170 has no type test; AREA 175 never calls it on 609),
and the two timer writes **150** (19->21) and **60** (21->16) are read only by state
20 (and 18's, which 17 overwrites with **30** first). UNVERIFIED whether any other
writer reaches him. The default arm makes ANY foreign state a full re-setup.

**`sub_44CDF0` - his FIRE** (17_script.c 941), `(actor, slot, targetPos)`; the
port's `Actor_TickProjectiles` four-slot path in function form:

```c
w = actor+84 + 4*slot;  if (!w) return 0;  if (actor+148+4*slot > 0) return 0;   // slot timer
ammo = Event44(prop 35, slot); if (ammo < 0) return 0; Event45(prop 35, slot, ammo-1);
r = Event44(prop 34, slot);  // lo16 = DAMAGE (entry+56), hi16 -> SPEED 39*hi16/10 (entry+8)
actor+148+4*slot = (float)block+12 after the event;   // the slot timer (UNVERIFIED which field)
sub_440C80(actor->node);  free entry e;  e.node = clone(w);  show it;  e+44 = actor;
e+48 = sub_44EEB0(w's root mesh name);        // slot 1's row is dword_657AF0, the wait he rewrites
SetNodePos(e.node, w+44, w+48, w+52);  parent = *(actor+412)+4;
dir = normalize(targetPos - muzzle);  orient the node along dir;  e.vel = speed * dir;
if (e+48) { e+40 = sub_44F180(row); scale = 1; e+52 = sub_44F1A0(row) /* row+24: the WAIT */;
            sub_44EF80(row, ...) /* muzzle effect */; }
```

Which objects fill his slots 0 and 1 (`actor+84`, `actor+88`) and their property-34
values are **UNVERIFIED** (runtime pointers; the record's +84.. are zero on disk).

## 4. How he dies

* Damage reaches him only through `sub_4240E0` -> `sub_47FD90` (5). Each accepted
  hit takes **6** (the baton's damage, entry +56) off `rec+92`; health is 200, so
  34 hits in the back (arithmetic only; UNVERIFIED in play). The "after 6 souls"
  gate is in `sub_47FD90` (`dword_657AFC >= 6`).
* The killing hit: `sub_47FD90` has already started a type-4 back-hit clip with
  flag 8, `+100 = 0`, `+160 |= 0x800`, state 16, and returned 6. `sub_4240E0` then
  takes the health to <= 0 and runs the kill branch (05_sys.c ~4795): `if (!(flags
  & 2)) --dword_4E9764` (the enemy count); a death clip by the hit's direction band
  (types 6/7/5/8, fallback 5) - **none exists in his group**, so none is played;
  `byte_90E0B0 = 2`; `flags |= 8`.
* Next ticks: the prologue's `sub_421770` plays the back-hit clip out (16 frames,
  scaled by his rate); then `--(+100)` gives -1, `sub_420C10`, and **`rec+92 <= 0`
  -> `Game_RaiseEvent(43, {3, idx})`** (0x480245..0x480252; 24_sys.c 7698) and
  return. That is the only message-3 site for him (24_sys.c 7154 is Gandhar's
  `sub_47F6F0`, 5108 is an event-44 property read).
* `Shoot_TickNpc` keeps calling the brain after that: flag 8 is now clear, so later
  ticks skip the prologue (and the death test) and run the state-16 body until the
  script's `shoot.end 1` (table entry 1, below) ends shoot mode. UNVERIFIED how many
  ticks that is (message dispatch timing).
* AREA 175's handler (script_dump "table entry 1", @0x12BF): `if (Mort Joueur == 0)
  { set Mort Astaroth; shoot.end 1; set Astaroth Dead; player.move 100; ... hide
  609; show 34; the ending cameras and scx programs; area.goto 152 }`.
* The player's death (`sub_423B10`/`sub_4240E0` on the player, health <= 0 ->
  `sub_423FC0`) leads to the handler at "table entry 0": `set Mort Joueur;
  actor.stat.set 609, 1, Vie=200` (Astaroth back to 200); if a ring is left: spend
  it, `shoot.end 0`, enable zone 'Restart Shoot', hide 609, player Vie = 200, goto
  address 526; else `shoot.end 1` and 'Game Over'. Zone 'Restart Shoot' (record 2)
  re-plays the soul actors 653..658 and set programs 0x5F..0x69, shows 609 and
  runs `shoot.begin 40; shoot.actor.enter 609` - so the setup runs again (all six
  souls back at 3 hits).

## 5. The baton bolt: souls through the callback, Astaroth through `sub_47FD90`

**The weak points (souls) are set meshes, so a bolt reaches them ONLY through the
callback** - never through `sub_4240E0`, which handles bodies. `sub_47FCF0`:

```c
// 0x0047FCF0 (24_sys.c 7451), a1 = the struck set mesh node
for (i = 5; i >= 0; --i) if (dword_657B00[i] && dword_657B00[i] == a1) break;
if (found) {
    if (--dword_657B18[i] == 0) {
        sub_436F20(a1);                  // HIDE the soul mesh (flag 2 over its subtree)
        ++dword_657AFC;
        dword_657B00[i] = 0;
        Game_RaiseEvent(43, {dword_4CFCF0[i] /*27+i*/, i});   // message 27..32
    }
}
if (dword_657AFC == 6) sub_44CD90(0);    // tested on every call, found or not
```

Three hits per soul, any bolt (see 1). AREA 175's message handlers then `scx.play`
an object, `set.hide_piece` two `.SFX` pieces and `character.hide` 653+i (the
`TUY<i>_FN` additive creatures, health 100, type -1). UNVERIFIED: whether the soul
actors 653..658 are attached bodies placed between the player and a soul mesh -
a bolt meeting one is stopped by the body sweep and refused by `sub_4240E0` (not in
ACTOR_STATE 3) before the world ray runs.

**Astaroth's body**: `sub_4240E0` (05_sys.c 4657, `(victim, damage, entry, seg)`):
refused unless victim or shooter is the player; victim in ACTOR_STATE 3; `+160 &
0x800` refuses; **for a 0x4000 victim any damage but 6 is refused**; then
`if (rec+80 == 13) v13 = sub_47FD90(idx, actor, damage, seg); if (!v13) return -1;`.

```c
// 0x0047FD90 (24_sys.c 7493), a1 = idx, a2 = his actor, a3 = damage, a4 = the bolt segment
s = rec+156;
if ((s < 17 || s > 19) && dword_657AFC >= 6) {
    y = actor+420 deg -> rad;
    dx = seg[3] - seg[0];  dz = seg[5] - seg[2];
    if (sin(y)*dx - cos(y)*dz > 0                       // travelling WITH his facing (-Z at yaw 0): from behind
        && sub_45E9C0(seg, g_PlayerActorRec->node, &hitNode, &tri, dword_657AF4 /*AstDos*/)) {
        // the body sweep, every body but the PLAYER's, box-testing ONLY the AstDos mesh
        // (sub_45ECA0 19_dsound.c ~5747: `if (!dword_53AA9C || dword_53AA9C == mesh)`)
        sub_44DEB0(actor);           // retire his bolts still WAITING at the muzzle (entry+52 > 0)
        sub_44EF00(20, hitNode+36..44, 0, 1);   // effect 20 at the AstDos node's world position
        rec+160 |= 8;  rec+184 = 0;
        sub_421A20(actor, rec, List_PickRandomByType(rec+20, 4), 0);   // id 15 or 16
        rec+160 |= 0x800;            // immune until the tick body clears it
        rec+156 = 16;  rec+100 = 0;  rec+88 = 10;
        return a3;                   // the 6 goes through: health -= 6, then sub_423EF0 or the kill
    }
    if (!(rec+160 & 8)) --rec+88;    // a hit anywhere else, while not reacting
    if (rec+156 != 21 && rec+88 <= 0) {
        sub_44DEB0(actor);
        rec+160 |= 8;  rec+184 = 0;
        sub_421A20(actor, rec, sub_434630(rec+20, 14), 0);   // the 9-frame flinch
        rec+156 = 16;  rec+100 = 10;  rec+88 = 10;           // played 10 times over
    }
}
return 0;                            // refused (the bolt is retired anyway)
```

After an accepted non-lethal hit `sub_4240E0` calls `sub_423EF0`: for a 0x4000
victim no action is requested, but **message 2** `{2, idx}` is posted and
`byte_90E0B0 = 4` if it was 0. `flags |= 0x1020`, `+184 = 0`.

**The BATON's weapon row** (`tables/shoot_weapons.json`, key -2, the type given to
a type-1 weapon whose model name has a `B` 11 characters from its end - only
`BATPOUV`; `engine/src/actor/shootfire.h` 107-112): player row rate 5, speed 104.0,
**damage 6**, no magazine; NPC row rate 10, speed 31.2, damage 6. Damage 6 IS the
baton's identity in every gate: a non-0x4000 victim refuses a player's 6 unless
type 11; a 0x4000 victim refuses anything else. 6 <= 10, so no explosion
(`sub_424470`).

## 6. What the port does for type 13 today

* `omk::ShootAi::tickAstaroth` (`engine/src/actor/shoot.cpp` 586) is a graph walk on
  an `actionDone_` pulse with the wrong labels above (grapple/throw/impulse/turn).
  It is used ONLY by the probe `engine/tools/run_shoot_ai.cpp`; the viewer never
  constructs a `ShootAi`.
* The viewer's per-gunman block (`engine/backends/sdl/playframe_world_staged.cpp`
  ~587-700) builds every shoot record the same way (`initShootRecord`, state 6, the
  scene action) and runs the GENERIC brain for him - no dispatch on type 13
  (only type 12 and the type-11 baton gate are special-cased).
* `sub_47FF70` is not ported: no `0x4020`, no `+88 = 10`, no state 29, no soul
  table, no callback. So in the viewer the baton (damage 6) is REFUSED by
  `shootApplyHit` (`shoothit.cpp` 160: a non-0x4000 victim refuses the player's 6
  unless type 11) - he cannot be hurt - and he fights as a generic gunman.
* `sub_47FD90` is not ported (`shoothit.cpp` 165: "unread").
* The world ray (`projectile.h` 108 `WorldRay`) returns only a point - no mesh - and
  there is no callback, so a soul can never be struck: messages 27..32 never fire.
* No set-mesh hide for `sub_436F20` / `sub_436F50` (no reference to either).
* `Camera_SetShake` (0x414DB0) is not ported; its consumer of cam+196/+204 is
  UNREAD.
* Not ported for him: the clip-by-ID lookup `sub_434630` on his group (the
  pedestrian code has it for slots), the stand/walk 9-cell grid on an NPC
  (`playstate.cpp` 760 explicitly skips type 13's aim layer: "0x4C37E8, not
  lifted"), `sub_44CDF0` as a callable fire, the slot-1 muzzle-wait write, the
  leap, the slam damage call, the flinch loop, message 3.
* Already in the port and reusable: `sub_421770` (picked-clip advance), `sub_421A20`,
  `sub_420C10` / `sub_420B80` (cell restore / stamp), `sub_421CD0` (`shootGridTurn`),
  `sub_420C70` / `sub_420EB0` (`shootAcquires` + turn), `sub_423B10`
  (`shootApplyStrike`), `sub_4240E0` (`shootApplyHit`), the body sweep
  (`shootSweepBodies`, needs a mesh filter), `sub_435020` / `sub_435770` /
  `sub_4353E0` / `sub_4359A0` (map2d), `sub_4725B0` (player grid blend), effects
  (`particles.h` `Sfx_RegisterEmitter`), `Hud_DrawBar` (`ui/hudbar.h`).

---

## PORT PLAN (dependency order)

1. **Clip lookup by ID** for a shoot record's group (`sub_434630`: first node with
   +4 == id) beside the existing type lookup - `engine/src/actor/shoot.h/.cpp`
   (or reuse `animGroupClips` slot field, `actor/pedestrians.cpp`).
2. **Set-mesh hidden bit** (`sub_436F20` / `sub_436F50`, flag 2 over a node subtree)
   honoured by the renderer's drawable mask and NOT by the bolt world ray -
   `engine/src/script/` (the set state) + `o3de/render.*`.
3. **World ray returns the struck mesh**: extend `WorldRay` with the mesh index /
   name (`collisionSoup(..., &meshOf)` already records it) and an
   `onWorldHit(mesh)` hook called exactly where `Projectiles_Tick` calls
   `off_4C8444` (body sweep first, world only when no body met) -
   `engine/src/actor/projectile.h/.cpp`, `backends/sdl/playframe_control_parts.cpp`.
4. **Astaroth state** (new `engine/src/actor/astaroth.h/.cpp`): the globals
   `dword_657AFC`, `dword_657B00[6]` (mesh refs), `dword_657B18[6]`, `dword_657AF4`
   (AstDos), `dword_657AF0` (slot-1 sprite row), `flt_657AF8`, `flt_6A062C`, the
   leap step `dword_69A75C/58`; `astarothSetup` = `sub_47FF70`;
   `astarothSoulHit(mesh)` = `sub_47FCF0` posting messages 27..32 through the
   Session's event-43 path; the callback reset in `Shoot_Enter` and at 6.
5. **Entry dispatch**: in the per-gunman entry block
   (`playframe_world_staged.cpp` ~590-650) call `astarothSetup` when
   `typeOfActor == 13` instead of the generic state-6 init (`Shoot_ActorEnter`
   -> `sub_47DFD0` -> `sub_47FF70`), and `rec+84 = unk_4C37E8` (lift that bone
   table from the image - "not lifted" today).
6. **The back-hit gate** `sub_47FD90` inside `shootApplyHit`
   (`engine/src/actor/shoothit.cpp`): needs the segment, his yaw, the soul count,
   `shootSweepBodies` with an exclude=player and a single-mesh filter (AstDos);
   outputs the reaction (type-4 clip, flags 8|0x800, state 16, +100/+88), the
   cancel of his waiting bolts (`sub_44DEB0`), effect 20; and the +88 flinch
   counter on a miss.
7. **His fire** `sub_44CDF0(actor, slot, target)` as a callable using the existing
   four-slot path (`projectile.h` `WeaponSlot` / `FireIn`), plus the per-tick
   write of slot 1's sprite-row WAIT. Find which objects fill his two slots first
   (UNVERIFIED).
8. **The tick** `sub_4800C0` with its helpers `sub_4B2F30`, `sub_4B30A0`/
   `sub_4B3260` (feed the existing grid blend), `sub_4B33C0`, `sub_4B39F0`,
   `sub_4B3B40`, `sub_4B3E10`, `sub_4B3800`, `sub_4B3630`, `sub_4B2DF0`,
   `sub_421C00` - `actor/astaroth.cpp`, wired into the brain block of
   `playframe_world_staged.cpp` in place of the generic brain for type 13, with
   the HUD bar (slot 1, 200), the slam via `shootApplyStrike` (difficulty and Body
   Shield already modelled), message 3 from the prologue.
9. **Camera_SetShake** (cam+196 = duration, cam+204 = cm*0.3937): read its
   consumer first (UNREAD), then port - `engine/src/o3de/worldcam.*`.
10. Replace `ShootAi::tickAstaroth` / `kAstarothEdges` labels in `shoot.cpp` and
    correct `docs/ASSETS.md` ~3580-3640 (impulse -> damage, turn -> muzzle wait,
    speed -> animation rate, grapple/throw -> wind-up/leap/slam).

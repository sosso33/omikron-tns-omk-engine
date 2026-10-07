# Scene-specific gameplay - what the port does not run (audit, 2026-10-05)

**Re-checked 2026-10-07** at `842bd42` (nothing moved since: no commit after it, and the uncommitted work in the tree is the slider's). **Updated 2026-10-07** at `82a8d54`: rows 1, 2 and 7 are DONE, and row 3 later the same day (`842bd42`; struck below,
with their commits); every other row re-checked against the tree and still
open - no `onCall` arm for ops 54, 127, 134, 135, 137, 142 (94 and 122 are only
classified by `visibleOp`, `area.cpp:3477`), nothing posts messages 23/24,
no `MDGUN`/`MDJP`/`MDHEAD00` consumer, `becomePlayer` still keeps the old
position, the VIDEOPHONE still the last screen in `todo/missing-ui.md` §5.

Read-only audit at `14dcf26`, done beside the drift audit (`todo/drift-audit.md`)
after it found Gandhar running as the generic gunman. Question: which
gameplay that serves PARTICULAR scenes is unimplemented or never played.
Four sweeps: every opcode the shipped scripts use (AREA, SCENE, GLOBAL,
startup scripts, conversation conditions/actions - 117 distinct) against
`interp.cpp` / `Session::onCall` / the viewer; every `player.become` target;
the SCX scene-program functions; `tab_special_move`; and the exe for
hard-coded scene logic.

**Headline: there is NO hard-coded area/actor/zone id in the exe** - no
compare of the active area (`dword_69BC64/48/4C/58/5C`) or an actor id
(`+0x110`) against a constant. Per-scene logic is in the scripts, plus the
shoot AI's character TYPE (`+80`) and a handful of string compares (all
ported: `'125338'`/`'02E19A'`, the radar's `.WRE` scales, Aapkayl's
software-mode glow meshes - not needed, the port is the HAL device).

## Gaps, by what a player loses

| # | gap | scenes | original | port today |
|---|---|---|---|---|
| 1 | ~~Gandhar (shoot type 10): grab (action 25, messages 5-8), strike (26), damage gate `sub_47DF60`~~ | AREA 2 lava cave | `sub_47F6F0` + twelve actions | **DONE 2026-10-06**: the strike and grab (`c81f1bb`), the baton-through-the-head gate (`90125e6`), played through to his death (`ef43de9`); `todo/gandhar.md` closed |
| 2 | ~~`fight.begin` that cannot start runs on (drift S6)~~ | 108 sites: AREA 5, 10, 61, 140, 149 Qalisar Arène, 168, 186, 202, 209, 237, 245; SCENE 10, 25-28, 43 | parks at status 3 until event 2 | **DONE 2026-10-06** (`29849e8`): a fight always begins (`Fight_Engage` attaches a hidden opponent) and its script always parks |
| 3 | ~~`player.become` places the new body at the OLD body's spot - every soul transfer, 45 sites~~ | all reincarnations; most visible on the Sham and the Mecaguard | 0x402F60: `sub_41C270(newIdx,&pos)` reads the NEW body's position/facing, `sub_41BDF0` places it there | **DONE 2026-10-07**: the viewer puts the new controller at the new body's drawn node, else its placement record (`playframe_world.cpp`), and `Session::becomePlayer` moves the Session's player to the record; the harness's `--stand` no longer re-applies at a rebuild. Measured on the Sham mount: the old code put the Sham 29 units off, at the spot Fodo's mount clip left him. `verify.py: engine: become place` (shown to fail) |
| 4 | **The Sham ride** - NEVER PLAYED | AREA 137 Jangir Sas Mayerem: zones 2239/2242 mount -> `player.become 434` (SHU_FN = Sham + Fodo, `Sham.CTL`), ride, 2245 'Pousser Pierre' -> `player.become 406`, `area.arrive 150` | no Sham code at all: the generic channel over `Sham.CTL` (group 100 walk 0x04 / run 0x800 / back 0x08, turns groups 50/51, MDACTION entry 18), follow camera 0 | **the MOUNT now runs headlessly** (`--save traces/save-appart.bin --area 137 --stand 33183,1044,-4412,90 --zone-enable 2239 --keydelay 120 --keys 28,28`): actor 434 becomes the player at frame 182 and stands in `SH_STAND` at its own place; the ride itself, the rearing and the stone are still unplayed. The record's y (985) is 69 above the floor the walker seats it on (1054) - not looked into. Before: loads SHU_FN + SHAM.CTL from the DB record, controller is move-name driven and guards every H1-specific group; tracks bind by index (`pose.cpp:417`; the "N/484 tracks resolve" log line will wrongly say 0). Risks: #3, and the rebuild under `player.anim.hold` at rec 47. Route (unrun): `--area 137 --address 377 --zone-enable 2239` |
| 5 | **The Mecaguard as the player** - never played end to end | SCENE 51 rec 7 (`player.become 216`, MCG_FN/MECA) -> CS Lev-4/5, AREA 183 lifts, AREA 180 shoot vs Meca 191, `player.become 49` back | generic channel + type-5 HUD (screen 33), pitch lock, metal sounds | pieces ported (screen 33, `shootmove.cpp:21`, groups 200/201); metal step sounds 0041/0042.WAV not modelled (`shootmove.h:147`); #3 applies |
| 6 | **Shooting gallery score** `ui.highscore` (op 142) | AREA 59, 5 sites | 0x405CD0: name via event 6, `sub_409370` inserts into the header table at 0x90E454, opens screen 36 at the page; `sub_42B5E0/5F0` "Félicitations" unread | no consumer; screen 36 draws (`playframe_screens_huds.cpp:604`) but nothing is recorded |
| 7 | ~~Black-and-white cutscenes `render.grey.on/off` (150/151)~~ | 14/15: AREA 145, 156, 158, 177; SCENE 9 Morgue, 42, 57 Appart Kayl Rencontre, 60 Concert Bowie | swaps to render bank 1 (luma) | **DONE 2026-10-06** (`e9a97fd`): ops 150/151 on every backend |
| 8 | **City weather** - messages 23/24 never posted | subscribed by AREA 0, 1, 64, 101, 142, 222 (music -> track 119 and back) | `sub_4815D0` (`25_sys.c:303/326`) snow/sand cycle, `Images\snow.bmp`/`sand.bmp`, gated `HIBYTE(dword_90E724) == 2` (detail level? unconfirmed) | no weather, nothing posts 23/24. The drift audit's "20-26 posted" is wrong for these two. (The day/night cycle `sub_41E7A0` was ported 2026-10-06, `o3de/daynight.*` - the weather is a separate function and still absent) |
| 9 | **Shoot mode: weapon change** `MDGUN` | every shoot phase | event 48, `Shoot_InitWeapon`, HUD refresh | not modelled (`projectile.h:167`) |
| 10 | **Shoot mode: jump** `MDJP` | every shoot phase | clears `dword_6A52CC`, `sub_47D2E0` | not modelled (`shootmove.h:82`) |
| 11 | **Adventure first-person look** `MDHEAD00/01` (key L) | everywhere; H1/F1 g0/g27/g28, Sham | actor +1305: head pitch ±40 / yaw ±70, `Camera_Request(3)` | missing - the key only plays the stand loop. Not checked in play |
| 12 | **VIDEOPHONE** callback (screen 0) | `ui.open 0` x10: AREA 41, SCENE 39, 45, 46, 47, 53, 62 | item callback 0x0049DBF0 | partial; last screen in `todo/missing-ui.md:209`. NOTE: 0x0049DBF0 is the SNEAK QUIT tab's "show Oui/Non" (`sub_428FF0(0x004DEBA0, 0x40000001, 0)`), already ported as `kCbSneakQuitShow` (`widgets.cpp:2128`, `:2174`) - so the screen's gap is not that function; re-read what screen 0 lacks before sizing it |
| 13 | **`image.show`** (op 94) | AREA 56 Jaunpur BE Entrée, 76 Jangir (2), 98 Lahoreh Yrmali Place | full-screen `IMAGES\%06lx.BMP` (all ship) | no consumer |
| 14 | **op 122 = SHOW a set piece** (newly read; inverse of 123) | AREA 2 startup (ids 0, 0, 1) | `sub_41BD40(id,1)` -> `SetPiece_Show` (`04_sys.c:6253`) | no case; set pieces default hidden (`setpiece.h:121`), so two effects in Gandhar's cave likely never show |
| 15 | `camera.follow_player` (54) | AREA 24 Anekbah Hall 42 | camera back to follow | no consumer |
| 16 | `player.pos.sync` (127) | 14: AREA 33, 66, 76, 99, 137, 145, 168, 180, 213; SCENE 57, 65 | re-seat walker on the node, clear ground cache | no consumer; impact suspected minor |
| 17 | ops 137 (7: GLOBAL heal handlers, AREA 1, 101, 114, SCENE 2), 134/135 (AREA 237, 81: object slot 48's mesh pointer save/restore) | | 137 unlocated | generic stubs |
| 18 | cosmetic: `MDSTOPR/W` out-of-breath stop after a long run (groups 164/166); `MoveObjectOnPath` X/Z Euler (3 vents, `ACSvent.SCX`, `program.cpp:448`); `SetSpriteDefaultPalette` (3 tunnel doors) | | | not ported |

## Verified complete (so nobody re-checks)

- All 17 SCX script functions the data uses (13887 calls, 220 files) - `program.cpp`; the 8 other names have 0 uses.
- Gandhar (since 2026-10-06), Astaroth, Spectre sight, Zombie damage gate, Tetra-bomb timers, the special screens 11-19/36, swimming, falls, lifts, the slider.
- 43 of 45 `player.become` sites target ordinary H1AVNT/F1AVNT bodies (only #3 applies); none targets a demon (D1) or bankless body.
- Opcodes fully consumed: 3-42, 45-52, 56-73 (62 since `29849e8`), 75-84, 87-93, 95/96, 98, 103-107, 110-120, 123, 126, 128-133, 136, 138/139, 146-152 (150/151 since `e9a97fd`).

## Cut content (nothing to port)

X-Tech (type 7, no record carries it); the six spell recipes (gate 8 never set);
object 980 "Radar activé" (never given); `morph.play` x3 (morphs do not ship);
`Game_HandleEvent` case 0; shoot HEAD mode (`tab_special_move` lists MDHEAD01
twice, first match wins, so row 52 and its flag 0x200 never run - so
`shootmove.h:114`'s note cannot happen); `MDCAMADV/SHT/FGT`, `MDRA/DT/RDP/RDM`
(unused or bare `ret`).

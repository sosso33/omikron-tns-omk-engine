# Drift audit - where the port can behave differently from the original

Asked 2026-10-05: "analyse the current state of the port, compare it to the
original and look if the behaviours could drift or cause any gameplay issue
(script not triggered, 3D model not loaded or not placed correctly, ...)".
A READ-ONLY audit at `1ff56d2`: three agents (the script side, the model
side, timing and save/load), then the heaviest claims re-checked by hand.

**How to read the marks.** ✔ = the PORT side was re-read by hand after the
audit (the line anchors below were current at `1ff56d2`). Every statement
about what the ORIGINAL does is the audit's reading of a doc, a code comment
or the assembly, not re-verified here - read the handler before porting
anything on it (CLAUDE.md §1: a name or a summary is a hypothesis). Site
counts are the audit's own walk: 5870 deduplicated slots (GLOBAL, AREA,
SCENE, the `+4` startup scripts) plus the `IAM\DIALOG` node scripts.

Nothing here has been played. Each row says what a player would meet.

---

## 1. Scripts that do not fire, or whose effect is dropped

| # | the drift | what the player meets | exposure |
|---|---|---|---|
| S1 ✔ **DONE 2026-10-05** (`42c9423`) | **The game clock never advanced.** `GameState::clockTick` (`engine/src/script/gamestate.cpp:343`) and `timerCheckExpiry` (`:387`) are called only by `engine/tools/state_probe.cpp` / `worldops_probe.cpp`, never by the Session or `omk-play`. The original's `Clock_Tick` / `sub_41E600` runs every frame scaled by the delta, and `sub_41E480` raises event 43 / message 18 on expiry (`docs/GAME_STATE.md` §6, "The script timer") | the Tetra-bomb countdowns (`timer.mode 12`, `timer.set 900`) never expire; message-18 handlers in AREA 73, 77, 78, 80, 143, 144 never run; `var.set.timer` (op 115) always stores 0 - the shooting range's time; the calendar stays where the save left it, so every save carries the same date | 12 countdowns, 5 timer reads, every save |
| S2 ✔ **DONE 2026-10-05** (`a72c386`) | **A conversation's reply ACTION ran in a bare VM.** `DialogPlayer::choose` (`engine/src/script/dialogue.cpp:453`) builds `Interpreter act(*state_, *table_)` with no hooks and discards the result, `zonesDirty` and `calls` included. The original executes event 59 through the same handlers as a world script, `Zones_RegisterAll` tail and prop writes with them (that the hooks reach event 59 is inferred, not read). The CONDITIONS (`:412`) use only push / compare / logic and are safe | `zone.disable` writes the bit but the live zone list is not rebuilt, so a conversation that retires its own trigger can be started again from the same spot; `zone.enable` stays inert until the next area load; `object.show` / `object.release` do nothing at all (`interp.cpp` `op == 76 && hooks_`), and the shown bit is not written so the loss SURVIVES A SAVE; `scene.load` touches the DB only; `media.play 534` ("DATA MEMORIZED") is silent | `zone.enable` 20, `zone.disable` 25 (~30 conversations), `object.show` 9, `object.release` 4, `scene.load` 2, `media.play` 84, `character.look_at_player` 1 |
| S3 ✔ **DONE 2026-10-05** (`052ebae`) | **`game.restart` (op 152) had no consumer.** `Session::requestRestart()` (`area.h:1047`) has no caller; op 152 falls to `interp.cpp`'s generic stub. The original sets `g_RestartRequest` and the loop runs `Game_NewGame` with a fade from white (`docs/SCRIPT_VM.md`) | the soul-capture endings end their script and play carries on in an undefined state | 3 (AREA 61, AREA 64, Ix Astaroth 2) |
| S4 ✔ **DONE 2026-10-05** (`e64eb3b`) | **`inventory.save` / `inventory.restore` (ops 148/149) had no handler.** They are the shoot phase's checkpoint: AREA 2 saves on entry, its "Mort Joueur" handlers restore | a retried shoot phase keeps the ammo, medikits and the five character-sheet int16s spent in the failed attempt | 14 / 15 |
| S5 ✔ **DONE 2026-10-05** (`305b1b1`) | **`walk.ledges.ignore` / `.obey` (ops 129/130) were never consumed.** `Walker::ignoreLedges` (`engine/src/actor/walk.h:272`, read at `walk.cpp:239/243`) is never set; the only mention of 129/130 outside the table is `Session::visibleOp`, a classification. They bracket lift and platform rides (AREA 50: `player.move.wait 58` -> ignore -> `scx.play.wait` x2 -> obey) | if the walker runs during a ride, a drop over 11.81 units is refused - the player left behind or stuck. Consequence SUSPECTED: depends on whether the walker steps the player during those waits | 28 / 29 |
| S6 | **A `fight.begin` that cannot start runs on.** `FightWait`: `if (!beginFight(...)) break;`, and `beginMelee` (`engine/backends/sdl/playstate.cpp:968-1001`) returns false with no staged body or a missing combat bank. The original parks at status 3 until event 2 | the script continues as if the fight was won (AREA 5 then computes `Vie Combat Perte = 0`, raises the adaptive difficulty and enables the next zone) | 108 sites; how often `beginMelee` fails is unmeasured |
| S7 ✔ **DONE 2026-10-05** (`5764d57`, `48c0e08`) - and it found a fault the audit missed: MDACTION in ACTOR_STATE 3 is the ZONE PRESS (`sub_467950`, event 6), which the port refused, so no zone could be pressed in a phase (it also TOOK objects there, which the original skips) | **The shoot freezes had no handler**: `shoot.player.suspend` / `.resume` (116/117) - the original sets `g_PlayerBehaviourOff`, installs input profile 0 and drops the held object - and `shoot.freeze_all` / `.unfreeze_all` (106/107) | in a shoot phase's cutscene (AREA 2 "Cinématique Porte") the player and the armed gunmen are not frozen | 108 / 104, 3 / 3 |
| S8 - **PARTLY DONE 2026-10-05** (`606e345`): message **2** posted (a gunman hit and alive, `sub_423EF0`; AREA 144's sentinel zone). Read and NOT ported, because each belongs to a larger piece: **5-8** are shoot action 25's tick (`sub_47F340`) - the action arms; **27-32** are Astaroth's six weak points (`sub_47FF70` / `sub_47FCF0`) - the Astaroth shoot AI, which the port does not have, so the end-game boss fight cannot be won | **Messages subscribed and never posted.** The port posts 0, 1, 3, 4, 9-17, 20-26 and, since S1, 18; nothing posts 2 (AREA 144), 5-8 (AREA 2), 27-32 (AREA 175, whose handlers hold `set.hide_piece`). Where the original raises them is NOT traced | those handlers never run | AREA 2, 144, 175 |
| S9 ✔ **CHECKED 2026-10-05 - FAITHFUL, no change.** The original holds ONE opener too: `ui.open` (0x403860) calls `sub_41DF30(screen, ctx, -1, -1)`, which does `if (a2 != -1) dword_930744 = a2` - one global - and `UI_LoadScreen` returns at once for a screen already open; the close raises event 5 with `dword_930744` (`sub_466B60`), and the answer variable is the one global `dword_4E6B28`. So a second `ui.open` strands the first context in the engine as well. The five lift contexts were S10's band arming every level, which the original's box does not | **One pending `ui.open`.** `pendingUiCtx_` (`area.h:1522`, `area.cpp:3013`) is a single slot; a second context reaching `ui.open` overwrites it and the first stays at status 6 for good, its zone dead. It already happened with five lift contexts and is avoided today only by S10's band. The original is unverified | a zone that stops answering for the rest of the visit | unknown |
| S10 ✔ **DONE 2026-10-05** (`d28b825`) - the `+88` is the ACTOR's root-mesh radius around his pelvis, and the zone's box is the quad plus 19.685 above it; a quad from ~20 below Kay'l's feet to ~84 above arms (`engine: zone box`) | **The zone height band was a reconstruction**: +-39.37 units around the quad's y (`engine/src/script/zones.cpp:297-311`). The engine uses a radius at record `+88`, not read | a zone authored more than 1 m off the floor never arms; a stacked one arms from the wrong storey | not counted |
| S11 ✔ (123, 136, 150) - **123 DONE 2026-10-05** (`ac6717f`): a "piece" is an `.SFX` set piece (an effect row), not decor - `SetPiece_Find` and `+72 &= ~1`, no collision involved; `engine: hide piece` | **Recorded and never consumed**: `set.hide_piece` (123) 32 - the pieces stay drawn, solidity unknown; `camera.shake` (136) 32; `render.grey.on` / `.off` (150/151) 14 / 15 - the black-and-white cutscenes are in colour; `player.pos.sync` (127) 14; `ui.highscore` (142) 5 - screen 36 never opens, no score inserted; `image.show` (94) 4; `camera.follow_player` (54) 3; ops 122 (3, AREA 2 startup), 134 / 135 / 137 (2 / 3 / 7) unread and unconsumed; `morph.play` (144) 3, all cut content | per op | as listed |
| S12 | Minor: `var.set.actor_stat(player, 0, ...)` (op 86, 5 sites, AREA 101) leaves the variable where the original stores pointer garbage (documented in SCRIPT_VM); the transition watchdog counts frames at 30 fps (`area.cpp:144`), so at 15 fps it fires after 120 s, not 60 | | |

**Checked and faithful**: the `0x4000` indirect operand (only `push.i16`, 59
sites, so recording stub fields raw is safe); 7108 branch and case targets,
none backward; `Random_NoRepeat`'s `lo + r % n` and no-repeat rule; the
32-slot context table and its unlisted-when-full; a panel for every screen the
corpus opens (0, 2, 4, 5, 11-27, 29, 30, 32, 36) with answers modelled; all
15 `WorldHooks` overridden by `Session::Hooks`; `player.become` rebuilding the
controller; fight damage written back to the DB; the zone scan after the pump,
as in `Game_Tick`, and running through a conversation.

---

## 2. Models not shown, shown wrongly, placed or posed wrongly

| # | the drift | what the player meets | exposure |
|---|---|---|---|
| M1 ✔ **DONE 2026-10-05** (`953e6e0`) | **Hide then show RESET a body.** `character.show` (`engine/src/script/area.cpp:2722-2736`) calls `showCharacter(id)` and never reads field 1; the viewer ERASES a body that drops out of `shown()` with its `Staged` record - position, `lastPose`, `progRan`, `progYaw` (`engine/backends/sdl/playframe_world.cpp:258-264`) - and rebuilds it at its placement record, re-seated every frame while nothing drives it (`:147-160`). The audit's reading of `0x403CB0`: the record's position and facing are applied ONLY when field 1 is non-zero; with 0, `Actor_Attach` re-links the same node where it last stood, as it last stood | a body a program moved snaps back to its chunk spot; a body with no `.CTL` (896 of 1032 actor records) comes back in the REST POSE - a T-pose. With field 1 = 1 a moved body is NOT re-placed, because `progRan` blocks it. **The likeliest owner of `todo/next-tasks.md` item 6** ("street NPCs stop and T-pose"), whose own unreproduced hypothesis this is | 1256 shows (801 field 0, 455 field 1), 1980 hides |
| M2 | **An undriven NPC with a bank stands on ONE frame.** `idleTracksFor` (`engine/backends/sdl/playstate.cpp:1203-1235`) builds `t.frames = 1` from the default entry's key 1, posed at frame 0 (`playframe_world_staged.cpp:2142-2144`); the comment at `:2115-2118` says the original drives such a body by its channel (`Cef_TickChannel`) | NPCs frozen mid-idle instead of breathing / shifting | 70 records on H1AVNT/F1AVNT, 65 on MECA, 1 on SHAM |
| M3 - **READ 2026-10-05, not yet changed.** The original has ONE subject: `Dialog_Load` (01_file.c 349) resolves DIALOG word 0 with `Scene_FindObjectIndexById(id, the active slot's scene)` and hands it to `Dialog_SetSubjectActor`, whose `-1` fallback is `sub_457030()` = `dword_4C8898` - set ONLY by `Slider_Init` (0x00453450) from event 51's answer: the street's reserved PEDESTRIAN-CONVERSATION actor slot (the passer-by `talkToPedestrian`'s messages 13/14 start a conversation with). So an unresolved speaker is that pedestrian - never "every body wearing the model", and never a fresh body at the camera solve. The port's model fallback and its extra staging (`playframe_world.cpp` 162-170) are both its own. NEXT: read event 51 and how that slot's body is put where the talked-to walker stands, then replace the fallback - pedestrian conversations are a played feature, so this needs the reading first | **The speaker is matched by MODEL** when his actor id is not staged: `isSpeaker = ... (!convOwned && s.model == speakerModel)` (`playframe_world_staged.cpp` ~384-392) is true for every staged body wearing that model; `playframe_world.cpp:162-170` re-uses the first stranger with the model as the speaker's body, which is then `placed`, so the camera-solve placement never applies | every extra in the speaker's model mouths the line and takes his yaw; the speaker stands where the stranger stood. The crowd models (PSH, FSH...) are shared widely | not measured |
| M4 | **One actor id in both resident chunks.** `rebuildShown` (`area.cpp:2148-2170`) pushes both slots' entries, bodies are keyed by actor id (`playframe_world.cpp:91`), so one body takes both records and slot 1's wins; `showCharacter` attaches the first slot's. The original has one runtime slot per record (`word_69BC80`) and resolves through the context (`sub_40D6A0(id, ctx+0x1F)`) | across a transition between two such areas the NPC jumps, or one copy is missing | 95 ids placed by more than one AREA chunk (actor 11 in 35/39/40/87/219/248); 0 name different models; which pairs are ever CO-RESIDENT is not measured |
| M5 ✔ | **Props.** `object.place_at` (op 98) only queues a `PropEvent` (`interp.cpp:957-966`, `area.cpp:3755`) and nothing outside `area.*` reads `propEvents()`; the viewer draws every prop at its chunk placement (`playframe_world_scene.cpp:649-800`). The original moves the node (`sub_41CF50`). `props()` also ignores the 50-slot pool, so a record that got slot -1 is still drawn. SUSPECTED: props and attached characters of a HIDDEN slot (decor state 1) are listed and drawn | a placed prop stays where it was authored; a prop the original could not fit is drawn | 6 `place_at` sites |
| M6 | **Floor re-grounding is the port's.** A record-placed body (`pelvis=false`) is dropped every frame onto the merged walk mesh of both slots, if the drop is under 100 units (`playframe_world_staged.cpp:2478-2503`, labelled as the port's own cut-off). The original's rule for an undriven body's height is not found | an NPC authored on a ledge or furniture less than 100 units above a walkable floor is pulled down to it | not counted |
| M7 | Declared: a scene clip's Euler applies its YAW only (`playframe_world_staged.cpp` ~281-284) | three beggars lose their -7 of pitch | 3 known |
| M9 ✔ **DONE 2026-10-05** (`c2e16e5`) - the cause was NOT the moving floor's collision (eager and lazy placement fail alike), NOT a program end (the platform kept rising), NOT the ledge flag: `Walker::tick` returned at once for a GROUNDED actor, so the floor was answered only on frames with a root delta, and `H_STAND` gives none on the two frames it loops (97-98) - the platform rose 8.4 past him and the next probe passed under it. `Actor_ApplyMotion` runs the ground response every frame; `Walker::settle` now does. AREA 50 rides up to -158.2 and back down to +0.3 (`engine: lift ride`). The other eight rides NOT measured: AREA 137's needs the area's lift state (var 439) and the committed save does not reach it; AREA 145 and SCENE 62 carry the player with `scx.play.player`, which the walker does not own | **AREA 50's lift left the player on the shaft floor.** 'Elevateur Bas' (zone 1040, var 321 = 1): the platform `SA_asens` rises (`motion:` moved by `sawaken0.SCX`), the ground under the player follows it to -37 / -54, then reads the shaft floor, +5.26, and he stays there while the script goes on to enable 'Ascenseur Haut' (zone 1042, y -168). Measured WITH the ledge flag (drops at frame ~105) and WITHOUT it (rides to -54, drops at ~120) - so it is not S5's doing. How the original keeps him on it (the moving floor, the program carrying his node, `player.pos.sync`?) is NOT read | the lift ride fails; the player is left at the bottom with the upper zone enabled | AREA 50; how many other rides, not counted |
| M8 | Declared: the 58-slot texture cache's SUBSTITUTION is not reproduced - `TextureCache` (`engine/src/o3de/texcache.h`) is used only by `engine/tools/dump_render.cpp`, the viewer gives each model its own textures (`playstate.cpp:847-881`). Here the port is "more correct" than the original | Anekbah never shows the substituted atlases (7 with AImpasse resident, 18 with AToit) | 182 names ship with different pixels |

**Checked and clean**: all 17 SCX program function ids that ship are
handled; all 161 distinct actor models resolve under `MESHES/PERSOS` and all
4 `.CTL` banks resolve, so no shipped model is silently dropped; contexts run
before `scene_.tick` (`area.cpp:2445-2452`), so a program's first step lands
on the frame it starts; op 73 is player-only in the original too; the
per-AREA scene pools evict in the same order in the cases traced; the `.SCX`
memory change (`1ff56d2`) reads nothing by the old file offsets - the camera
editing is read before the file is dropped (`scenerunner.cpp:37-39`) and the
sprites are re-read from disk.

---

## 3. Timing, and what survives a load

| # | the drift | what the player meets | when |
|---|---|---|---|
| T1 ✔ **DONE 2026-10-05** (`5e07416`) - the load is NOT reachable with a live controller in the original: screen 30 hides `Charger` (`Ui_BuildLoadPanel`), the pause menu has `Reprendre`/`Quitter` only, and `Quitter le jeu` is `sub_409090` = `g_RestartRequest`, so a mid-session load is quit -> restart -> start menu -> `Charger`. The port ENDED THE RUN on that quit; it now restarts (S3's restart drops the controller), so the load meets none. The load path itself was left as it is - it is the boot load, which is played | **An in-game LOAD keeps the live player controller.** `playframe_modes_parts.cpp:1784` sets `playerReady = false; adventure = false; forceAdventure = true` without `player.reset()`; the hand-over rebuilds the controller only when `!player` (`playframe_control_parts.cpp:150`); `Session::setPlayerPosition` (`area.cpp:603`) does not bump `placementSeq_`, so the controller never gets the saved position (`playframe_control_parts.cpp:396`). `Session::loadArea` (`area.cpp:488`) also leaves `shoot_`, `dialog_`/`dialogState_`, `tr_`, `load_`, `haveCam_`, `rootMotion_`, all of which `restart()` (`:1250`) clears. The slider path records exactly this failure and guards it with `&& !player` (`playframe_modes_parts.cpp:1507-1528`) | the player kept at his old position and state, invisible (drawing needs `playerReady`, `playframe_world_scene.cpp:613`) and his model evicted | IF screen 30's "Charger" is reachable in game - `kCbLoadCharger` (`engine/src/ui/widgets.cpp:2231`) is not gated on the screen. From the boot menu there is no controller, so that path is safe |
| T2 | **The dialogue line clock is not real time below 10 fps.** `playframe_input_parts.cpp:309` clamps dt to 0.1 s (`:311` scales it by `--speed`) and `DialogPlayer::tick` (`dialogue.cpp:387`) adds it to `lineAt_` while the voice plays in real time. The original syncs to an external clock, probably the audio position (`sub_42CC10` / `sub_42BC30`, `docs/BOOT.md` §4) - not ported, only partly read | the face falls behind the voice, more as a line runs; self-ending lines end late; the same gap lets cutscene beats drift from their music | the classic Mac and Tiger builds (6-10 fps), heavy Vita scenes |
| T3 | **The fight AI waits on game time**: `fightRun.ms += frameSec * 1000` (`playframe_control_adventure.cpp:519`), clamped, scaled by `--speed`, 0 under the pause. `Fight_TickAI` waits on `Sys_GetTimeMs` (`todo/sixty-fps.md` §1) | below 10 fps the original attacks more often per game second; after a pause its waits have already run out | low frame rates, pauses |
| T4 | **Script-shown bodies never leave.** `scriptShown_` (bodies shown by op 78 or `player.become` that no placement record names) is cleared only by `restart()` (`area.cpp:1274`), and every `evictSlot` -> `rebuildShown` (`:2152`) re-adds them. In the original "shown" belongs to the actor slot, which an area unload frees | such a body follows the player across areas and reincarnations. Visible effect SUSPECTED | after any op 78 on an unplaced id |
| T5 | **Randomness.** The VM's xorshift (`engine/src/script/interp.h:358`) is never seeded - `seedRandom` has no caller - and is separate from every other consumer; the crowd is reseeded to 1 on every area load (`area.cpp:3837`). The original has one MSVC `rand()` stream; whether it calls `srand` is unverified | the same `var.set.random` sequence every session from boot; the identical crowd on every visit to a street | 235 random sites |
| T6 | Small: the port feeds the raw dt where the original smooths it, `(prev + raw) >> 1` (a hitch recovered in ~3 frames against ~6); it measures before the tick, the original after; at the 30 fps cap the per-frame work (neon emission, the AI's intent-104 re-roll and `Perso_InjectInput`, the shake decay) runs at half the rate of a 1999 machine at 60-85 Hz | | |
| T7 | Port-only residue: `rootAccum` (`playframe_world_crowd.cpp:789-804`) is released only at `H_STAND`, boarding or in the water, so a chain of take / grid states that never reaches the idle keeps a height offset; `nudge()` / `moveBy()` pass `dt = 1.0` (`engine/src/actor/player.cpp:509,514`), an extra gravity tick airborne (already recorded open); ~~during a conversation `Session::frame` ticks neither `tickFades` nor `tickMusicLevel`~~ - **WRONG, corrected 2026-10-05**: both run at the top of `Session::frame`, before the conversation gate (`area.cpp` `tickFades(...)`, `tickMusicLevel()`), so they keep running through a conversation | | |

**Checked and faithful**: the `(-1, -1, -1)` shift on every save and reload
(GAME_STATE §5); the timer is not saved, in the original either; the one-shot
zone latch lasting one visit; the dialogue enter / leave SAVING and restoring
the input-block flag rather than asserting it (`engine/src/actor/state.cpp:279,302`);
the `.CTL` cancel window tested as an interval, so it is rate-safe; camera
travel, fades, the bump cooldown and the fight camera advancing by the delta.

---

## 4. Suggested order

Each step: read the original's handler first, port, add a check SHOWN to
fail, play the scene it names.

1. ~~**S1, the clock**~~ - **DONE 2026-10-05** (`42c9423`).
   `Session::tickClock` at the end of every frame, conversation included
   (`Game_Tick` ends `sub_41E480(); sub_41E7A0(); Clock_Tick();` with no
   gate), and the viewer draws `sub_41E480`'s readout - minutes, seconds,
   hundredths, face 'C', centred at y 20..50. AREA 77's 'bombe 1' starts the
   fifteen minutes and the AREA's message-18 handler is the time-out (back to
   AREA 61). `engine/tools/timer_probe.cpp`, `verify.py: engine: script
   timer`; `engine: save round trip` now reads the save's clock one step on
   (14:14:20). Not yet PLAYED - `todo/play-test.md`.
2. ~~**S2, conversation actions**~~ - **DONE 2026-10-05** (`a72c386`). Case 59
   read: `Script_NewContext` in the ACTIVE slot, pc = the action, status 1,
   one `Script_Execute`, freed - so `Session::runReplyAction` does exactly
   that through `execute`, installed on `DialogPlayer` as its action runner.
   Dialog 197 from its own zone 990 (SCENE 19 over AREA 47) now retires 990
   and enables 991 in the live list. One labelled difference: with all 32
   context entries taken the engine still runs the block unlisted, the port
   says so and runs nothing. What an `object.show` there then DRAWS is M5's
   question. `engine/tools/reply_action_probe.cpp`, `verify.py: engine:
   reply action`. Not yet PLAYED - `todo/play-test.md`.
3. ~~**M1, hide / show**~~ - **DONE 2026-10-05** (`953e6e0`). Read:
   0x403CB0 is `Actor_Attach`, the bit, and `sub_41BDF0(record)` ONLY under
   `test ebx, ebx` (field 1); `Actor_Detach` (0x41CDD0) unlinks and parks the
   node with its transform and pose. Now `Session::actorHeld` keeps a hidden
   body PARKED in the viewer while its slot lives, `placeSeq` counts the
   field-1 re-places, and a re-seat also writes the held heading. Telis in
   the flat, hidden and re-shown while nothing drives her: field 0 brings her
   back on the floor in her last pose (before: at her record 240 units up,
   UNPOSED), field 1 at her record and facing. `omk-play --hide-show`,
   `verify.py: engine: hide show`. Two things left as found: hiding a
   conversation's SPEAKER while the conversation is open does not hide her
   in the viewer (the speaker path stages her regardless of `shown()`); and
   T4's script-shown list is unchanged. Next-tasks item 6 is still not
   reproduced - this closes the mechanism its own hypothesis named, not the
   report. Not yet PLAYED - `todo/play-test.md`.
4. **S3-S5, S7** - restart, the inventory checkpoint, the ledges, the shoot
   freezes: four small handlers with named sites. **S5 DONE 2026-10-05**
   (`305b1b1`): `sub_41C260` is `g_IgnoreLedges = a1`, now carried by the
   Session to the walker; the ride it brackets in AREA 50 failed for a
   reason of its own - **M9 DONE 2026-10-05** (`c2e16e5`): the grounded walker
   never answered the floor on a frame with no root delta. **S4 DONE 2026-10-05** (`e64eb3b`): the
   checkpoint is list 0's ids and the five GUNS' ammunition (record
   +260..+268 - not character-sheet fields), restored reversed. **S7 DONE
   2026-10-05** (`5764d57`, `48c0e08`): the freeze (`dword_4E9760`, bit
   0x8000 kept equal on every record, the four wakes), the player's suspend
   and resume as the player halves of `Shoot_Leave`/`Shoot_Enter`, and the
   zone press in state 3 that the ladders need. **S3 DONE
   2026-10-05** (`052ebae`): op 152 writes the flag, the restart fades in
   from white and the viewer drops the old world; and `Session::restart`
   was found keeping the OUTGOING scene pool, the old area's programs
   running under the new start menu - both pools now go with the slots.
5. ~~**T1**~~ - **DONE 2026-10-05** (`5e07416`): not reachable as written -
   a mid-session load goes through the pause menu's quit, which is a RESTART;
   the port ended the run there and now restarts, dropping the controller
   first. `engine: pause` asserts the restart.
6. Then M2-M5 and S8-S10, each needing a reading before an estimate.
   **S10 DONE 2026-10-05** (`d28b825`): `Zone_Add`'s box (the quad plus
   19.685 above) against the actor's (pelvis +- the root mesh's `+88`), the
   sweep-and-prune's overlap on three axes. **S9 checked faithful.** **S8
   partly done** (`606e345`): message 2 posted; 5-8 (shoot action 25) and
   27-32 (the Astaroth fight) read and left as their own pieces of work.
   **S11's `set.hide_piece` DONE** (`ac6717f`): an effect switched off, not
   a wall - the progression worry it raised does not exist.

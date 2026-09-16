# Fight mode — the plan, and the two questions the reading closed

`todo/next-tasks.md` item 17. Started 2026-09-16 with a reading pass (step 0),
on the reader's instruction to plan before porting anything.

**Melee is the one runtime in this tree that can never have a behavioural
oracle.** `traces/fight.log` was captured on 2026-08-31 to give the actor
runtime one and settled it the other way round: the golden-trace logger sees
only what a VM handler narrates through `Dbg_LogTagged`, combat is two opcodes,
and `fight.begin` announces **nothing**. The capture reached combat — 32 of its
anchored scripts carry `fight.begin` — and is silent about all of it. So the
standard here is `docs/PORTING.md`'s **data-constrained**, as it is for the
`.CTL` channel, plus what a person sees when they play it.

---

## 1. How a fight is ENTERED — op 62, read from the assembly

`tools/asmfn.py --op 62` prints the handler at `0x004035D0`. It makes three
2-byte operand fetches (the table's length was corrected 4 → 6 on 2026-09-02),
and then, in order:

| what the handler does | the call |
|---|---|
| looks the opponent up by character id | `sub_40D760(field0, dword_69BC48[…])` |
| takes the player | `sub_419E00` = `Actor_Player` |
| switches **both** to `.CTL` slot 2 | `sub_40B2B0(actor, 2)` = `Actor_CtlSlotName`, then `sub_419CB0` = `Actor_LoadBankList` |
| enters the fight | `sub_41A3B0(opponent, field2)` = `Fight_Engage` |
| parks the script | `mov word ptr [esi+16h], 3` |
| asks for the fight camera | `Camera_Request(14, …)` with `dword_930818` = field 1, floored at 0 by the `jge` |

**So field 2 is `Fight_Engage`'s second argument, which is the AI LEVEL.** The
operand stack makes it unambiguous: field 1 is stored at `[esp+18h]` and field
2 at `[esp+10h]`, and the `mov edx, [esp+18h]` that feeds the `Fight_Engage`
push happens while `esp` is 8 lower, so it reads field 2. The travel is read
back the same way for `Camera_Request`.

**The `dword_6A05E0` test over the whole body is the DRY RUN, not a gate on
fighting** — and it is worth recording how it looked, because for an hour this
file called it an unexplained guard. `sub_40EC70` is `Dbg_LogTagged`, the
global is set to 1 in three places inside `Script_RunToOpcode75` (`sub_406120`)
and cleared in `Script_Execute`, and it has 111 references: a dry run fetches
its operands, skips its effects and skips the announce. The port already knows
this — `interp.cpp` says so a few lines below its own op-62 block, and never
sets the flag. Nothing to model.

## 2. The difficulty is ADAPTIVE — and that answers ASSETS' open question

`docs/ASSETS.md` recorded which AI profile the shipped scripts select as **not
established**, because deciding it meant trusting the disputed operand length.
The length has been settled since 2026-09-02 and the corpus now answers:

* every one of the **108** `fight.begin` sites sits in a `case` on
  `VARIABLES[175] 'Niveau Combat'`, as three copies of the call;
* over the whole corpus field 2 is **36 / 36 / 36** across {0, 1, 2} and is
  **never 3**; field 1 is 0 at all 108;
* `Fight_FindAiProfile` matches the profile's `+0` against **level + 1**, so
  the shipped fights use profiles **1, 2 and 3** only.

**Profile 4 — the sparring partner, one attack and four walk directions — is
therefore unreachable.** It is in `H1Cmbt` and `f1cmbt` and no shipped call can
select it, which is this repo's usual shape for cut content
(`memory: omikron-cut-content`).

The level moves with how the last fight went. Around the call, from AREA 245:

```
var.set.actor_stat -1, 1, 168      ; 'Vie Combat Avant'   <- life before
   if 'Vie Combat Perte' >= 70 and 'Niveau Combat' > 0:  Niveau -= 1
   if 'Vie Combat Perte' <  30 and 'Niveau Combat' < 2:  Niveau += 1
case 'Niveau Combat' -> fight.begin opp, 0, 0 | 1 | 2
var.set.actor_stat -1, 1, 169      ; 'Vie Combat Après'   <- life after
'Vie Combat Perte' = 'Vie Combat Après' - 'Vie Combat Avant'
```

So the game measures what a fight cost you and picks the next opponent's AI
accordingly. Note the two hooks the port must honour for this to work at all:
the fight has to write the player's life back onto his record (the script reads
it with `var.set.actor_stat`), and the script has to resume where it parked.

*(The `Difficulté des combats` option is a different thing and stays as
documented: `word_90E1A6` becomes a flat +0.5 / +0.25 / +0 on the player's
dodge multiplier inside `Fight_Begin`. It does not choose a profile.)*

## 3. What a fight IS

`Fight_Engage` (`0x0041A3B0`, 102 lines) tears down any previous opponent,
registers the new one, writes ACTOR_STATE **2** to **both** fighters through
the `dword_910834` alias — it is the only writer of that state in the game —
drops whatever scene object drove either body, then calls `Fight_Begin` and
`Fight_SelectAiProfile`.

`Fight_Begin` (`0x004455B0`, 125 lines) builds the two combat contexts
`dword_906F60` (the player) and `dword_907000` (the opponent), 140 bytes each:

* **stats through event 44**: property 1 *Vie* into the hit points, 16 *Carac
  Attack* ×0.005 into `+132`, 18 *Carac Dodge* ×0.005×0.25 into `+136`, 19
  *Carac Fight Experience* ×0.024390243 into the channel rate — an experienced
  fighter literally animates faster;
* the difficulty bonus on the player's `+136`;
* the **separation radius** `flt_906F2C`: the larger of the two models'
  bounding radii at node `+88`;
* the six reaction entries per fighter, cached by role code (3, 4, 5, 9, 18,
  20) through `Cef_FindEntryByCodeGlobal`;
* `Game_Start("fight.SCX")` — melee has its own sprite and sound library, the
  way `shoot2.scx` is shoot mode's, and it ships;
* `Input_InstallScheme(3)` — control group **3 Combat**, already ported;
* both fighters turned to face each other, with flag `0x2` set across the two
  `Fight_FaceOpponent` calls so the turn is unconditional.

**Per frame**, `Actor_TickPlayerAndOpponent` (`0x00466710`, ACTOR_STATE 2's
tick) runs for each fighter: `sub_4451F0`, then the channel tick, then
`sub_4452A0`; the opponent additionally runs `Fight_TickAI` before his channel
and refreshes his spatial-index slot afterwards. `byte_91031F` gates the AI —
and that is the `[Preferences]` key `disable_fight_ai`, a DEBUG switch rather
than a game rule (§8) — while `dword_906F40`, the KO counter, freezes both.

Then `Actors_TickAll`'s tail, under the player's `+404 == 2`:

```
Fight_ResolveBoth()        -> Fight_ResolveHit both ways
Fight_FaceOpponent  x2
Fight_RecordFrame   x2     -> the 60-frame replay ring
Fight_UpdateHealthBars()   -> Hud_DrawBar(player, 200, 0, 2) and (opp, 200, 1, 0)
```

The pieces, with what each is:

| function | lines | what it decides |
|---|---|---|
| `Fight_ResolveHit` 0x0049A960 | 339, **CLEAN** | whether the attacker's move lands: the attack line against the defender's stance, guard and block, the hit window in clip frames, the damage (doubled against the player, scaled by attack, reduced by dodge, floored at 1), the reaction and knockback, the winner/loser globals |
| `Fight_TickAI` 0x00464830 | 407 | picks a move for the opponent and **presses it**: `Perso_InjectInput` into the same queue the player's keys feed. Closes distance past 78.740158 (2 m), else rolls the profile's weights, 25% of the time a special |
| `sub_4452A0` 0x004452A0 | 142 | the per-fighter step after the channel: the current entry and its combat block, the transition mask `sub_49A830`, the fighter's state id, **the KO trigger** (state 6 or 7 → `Screen_Fade(1)`, `++dword_906F40`), and two distance-gated bank switches at 118.11024 (3 m) and 59.055119 (1.5 m) |
| `sub_4451F0` 0x004451F0 | 37 | the step before the channel: writes the life back through event 45, and picks the replay's camera per KO pass |
| `Fight_KeepSeparation` 0x0049A610 | 75 | pushes the fighters apart to the radius, through `Actor_Move` so walls still hold |
| `Fight_FaceOpponent` 0x0049A550 | 17 | writes the facing Euler directly, refused in nine states and under two flags |
| `Fight_TickCamera` 0x00446500 | 211 + ~300 in six helpers | camera mode 14: a small state machine (1 normal, 2 a fighter in state 11, 4 a transition, 7 the KO, 8 close) |

## 4. The KO is a REPLAY, and it plays twice

`Fight_RecordFrame` keeps the last **60** frames of both bodies — position,
entry, clip frame and rate — in the ring at `unk_6A17E0`, sliding when it is
full. When a fighter lands in role state 6 or 7, `sub_4452A0` clears the
effects, fades the screen and raises `dword_906F40`; from then on
`sub_49B2E0` plays the ring back instead of ticking the channel, and
`sub_4451F0` selects a different camera setting for each pass (`dword_906F48`
2, 3, 4). Each playback increments the counter, and once it passes
`dword_9070AC` — set to **2** in `Fight_Begin` — the screen fades and
`Fight_Engage(-1, 1)` tears the fight down. So a knock-out is shown twice, from
two further angles.

## 5. How a fight ENDS

`sub_445AC0` (`0x00445AC0`, 56 lines) restores both channels, reloads
`aventure.scx`, writes the result to the two actors' `+404` (the tick-tail
selector), raises **event 2** with 0 / 1 / 2, clears both contexts and installs
control scheme 0.

`Game_HandleEvent` case 2 (`readable/src/01_file.c`) walks the context table for
the **first** context parked at status 3, returns it to 1 — and reloads the
**player's** slot-0 (`AVNT`) bank list. It ignores the result code it is
handed; the scripts learn what happened by re-reading the player's life.

## 6. What the port already has, and what it has not

Ported: the `.CTL` combat block and the fight-AI profiles as data
(`formats/ctl.h`), the channel and `injectInput`, `ActorRuntime::fightEngage` /
`fightEnd`, control scheme 3, `Session::setFightHook` / `fightEnded` /
`fightingWith` with op 62's park behind it, and `Hud_DrawBar` mode 0
(`ui/hudbar.h`).

**Since step 1 (2026-09-16)** `engine/src/actor/fight.{h,cpp}` adds the two
combat contexts, `Fight_Begin`, the engage and teardown, the profile
selection, both per-fighter steps, `Fight_ResolveHit` including the throw,
`Fight_FaceOpponent`, `Fight_KeepSeparation`, `Fight_TickAI` and the KO
replay, with `engine/tools/run_fight.cpp` running all three combat banks at
all three levels (`verify.py: engine: melee`).

Still not ported: camera mode 14, `Hud_DrawBar` mode 2, and any wiring in
`omk-play` — **no fight hook is installed**, so today every `fight.begin` runs
on instead of parking (the `ObjectWait` rule: a missing subsystem must not
deadlock a script). And one arm inside step 1 is deliberately left, labelled
in the code rather than approximated: the **defensive** branch of
`Fight_TickAI` (intent 9 inside 1.5 m), which reads the opponent's current
combat block to decide whether to guard. It is why `engine: melee` records
`blocks` as 0.

Two stale comments found on the way, both fixed in step 0: `interp.cpp` still
said op 62's table length was uncorrected and its third field unreachable, and
`docs/ASSETS.md` still called the profile choice unestablished.

## 7. The steps

Each step ends in a commit and a report, then waits for the reader
(`memory: stepwise-checkpoints`).

| step | what | state |
|---|---|---|
| **0** | this file, the two doc corrections, the corpus facts in `verify.py: fight & become` | **DONE 2026-09-16** |
| **1** | `engine/src/actor/fight.{h,cpp}`: the contexts, `Fight_Begin`, `Engage`/teardown, `SelectAiProfile`, both per-fighter steps, `ResolveHit`, `FaceOpponent`, `KeepSeparation`, `TickAI`. A probe fights profile against profile on all three `CMBT` files | **DONE 2026-09-16** — `verify.py: engine: melee`, and it found the AI's eight BUILT-IN move tables (`tables/fight_ai_moves.json`) and the per-slot WEIGHT the `.CTL` reader had been skipping. The defensive arm of `Fight_TickAI` (intent 9 inside 1.5 m) is labelled and left for its own commit |
| **2** | Session and viewer: install the fight hook, slot 2 on both bodies, state 2, scheme 3, the park; the end through event 2, the player's slot-0 reload and his life written back so `Vie Combat Perte` is right. Carry field 2 | **DONE 2026-09-16** — `--fight N` / `--fight-level N`, and a whole fight runs in `omk-play`. §10 has what it found and what it left |
| **3** | the KO: the 60-frame ring, the two playbacks, the fade | **DONE except the FADE** — the ring and both playbacks landed with step 1 (`Fight_RecordFrame` / `sub_49B2E0`) and have been seen running since step 2: the KO counter goes 1 then 2 and the fight ends. `Screen_Fade` is not modelled at all in this tree, so the two calls that bracket a knock-out are labelled rather than faked |
| **3b** | **NOT IN THE ORIGINAL PLAN, and worth admitting**: the opponent's BODY — his motion pass and posing him from his fight channel. The commits call this step 3 because it is what the work turned out to need once step 2 ran; this table said nothing about it | **DONE 2026-09-16**, `4eb178b` and `4a9abb3`. §10 |
| **4** | camera mode 14 | **DONE 2026-09-16** — `FightCamera` in `actor/fight.*`, the arm ahead of the follow camera in `omk-play`, and options row 18 consumed. §11 has the three faults the harness log caught, and what is labelled (the collision solve, the throw's swing length) |
| **5** | the HUD: both bars, and mode 2's four-second overlay | |
| **6** | play test with the reader | |

**What step 1's check can assert** (it must be SHOWN to fail, PORTING B2):
damage only ever from a combat block times the two multipliers and floored at
1; every reaction landing on one of the six cached roles; the gap never left
below the radius after the push; hit points monotone down and clamped at 0; and
every run ending in a KO with a winner. None of that is a behavioural oracle —
it is what the shipped data can falsify.

## 10. What step 2 wired, and the three things it found

The hook is installed in `omk-play`, so a script's `fight.begin` now parks and
a fight runs: both fighters onto `.CTL` slot 2, ACTOR_STATE 2, control scheme
3, the contexts from the fighters' own properties, and at the end
`sub_445AC0`'s list — the adventure bank back, scheme 0, both lives written to
the records the scripts read, and `Game_HandleEvent` case 2 releasing the
parked script. A first run played the whole arc: `HGUARD` to `IM_FRONT` and
back, a hit, the player's Vie to 0, state 7, the replay, `FIGHT ENDS after 245
frames`, the script resumed.

Op 62's third field now travels with the opponent, so the profile is the
level's (`Session::setFightHook` takes both), and two Session accessors were
added: `ctlSlotOfActor(actor, slot)` for the `+72/+81/+90` names and
`setActorProperty`, the write half the life needs.

**Three findings, in the order they cost time:**

1. **The first version rooted both fighters to the spot.** `ACTOR_STATE` 2's
   own row runs `Cef_TickChannel` AND `Actor_ApplyMotion` — melee moves a body
   exactly as ordinary play does — and driving the channel directly skipped
   the motion pass. `FightBody::externallyTicked` plus `Fight::setBodyTick`
   now put `PlayerController::tick` in the place the channel tick occupies, so
   the order pre / channel / post is still the engine's. The tell was in the
   harness log: 79 units apart for 245 frames.
2. ~~**THE OPPONENT STILL STALLS, and it is a CHANNEL fault rather than a
   fight one.**~~ **ANSWERED in step 3, and it was neither a `GoToMove` fault
   nor a channel one: it was the missing MOTION PASS feeding back.** With no
   motion the opponent could never close, so `Fight_TickAI` took its APPROACH
   branch every tick for ever, injecting continuously and thrashing between
   `HGUARD`, `HRUN` and the clipless pass-through 228. Applying the clip's own
   root motion to his body (`omk::clipRootMotion`, turned by his facing, the
   wrap frame dropped the way `Anim_RootDelta`'s `ceil(prev) <= cur` guard
   drops it) makes him WALK IN: 79 units apart, then 56, then 44 - which is
   the separation radius, so `Fight_KeepSeparation` holds them chest to chest -
   with his entry now `HFWALK` and his injected moves down from 674 to 58.
   The original text is kept below because the wrong diagnosis is the
   instructive half: every fact in it was true, and the inference from them
   was not.

   ~~**The stall as it was first read.**~~ He reaches `H1Cmbt` entry **228** — `name ''`, flags `0xE0808000`,
   **no clip**, no children, `goto 17005331`, whose own parents include 228 —
   and never leaves, while the AI presses 674 moves into a queue with no
   candidate to match. A clipless entry reports `clipFrames() == 1`, so
   `finished` is true at once and the clip-end path takes the GoTo, which
   walks back into the pair and stops. That is `GoToMove` (0x004A7B80) and its
   junction chase, shared by every bank, so it wants reading in its own right
   before anything is changed in `channel.cpp` — and it may be why
   `run_actor_states` reports 0 aborted chains: it drives with a player's
   words and may never reach group 22.
3. **The priority gate is deliberately NOT modelled.** `Fight_Begin` calls
   `sub_45A4C0(chan, 1/0)`, which sets and clears flag `0x400` — the flag that
   makes `Cef_FindTransition` honour the `+212` threshold — but it writes only
   the flag, nothing read so far writes `+212`, and this port's setter demands
   a threshold. Passing 0 would silently drop every priority-1 and -2
   candidate, so the flag is left off with the reason in `fight.cpp`.

**Step 3 landed the motion half of that** (2026-09-16): the opponent's body now
gets the half of `Actor_ApplyMotion` that matters, his clip's own accumulated
root track turned by his facing, and a second fault came with it — **the AI
fighter must be ticked with the IDLE WORD, not the replica's `kQueueDrives`
sentinel.** `Cef_TickChannel` always polls and `sub_4A7A20` never yields 0
(nothing held is `0x40000000`), so a body with no device searches on the idle
word while its injected queue works through the entry flags and the `0x8001`
clip-end path. Ticking him with 0 skipped the input pass outright and left him
standing in `HFWALK` at the separation radius with his intent stuck at 105.
With both fixed he fights: `HGUARD` → `H_LGUARD` → `HFWALK` → `B2` → `C1` →
`HGUARD`, 73 moves over 500 frames against 674 thrashing, and he beat the
save's 10-hit-point Kay'l down through `KOH_FRONT` to `GROUND` with the KO
replay running (`engine: melee` re-baselined to 6 fights ending in a KO).

**And the pose half landed the same day**, so the opponent now animates as
well as moves: a staged body that is the fight opponent is composed from his
fight channel's own clip at his channel frame, ahead of the idle fallback he
used to land on, and his drawn heading comes from `s.facing` about the pelvis
the way a gunman with a running brain does — `Fight_FaceOpponent` writes that
every frame.

**It needed one engine rule the frontend did not have.** A first version read
`states[state()].clip`, which is −1 whenever the channel sits on an entry
carrying `0x8002` — an alias or pass-through that plays nothing and hands its
clip on through its GoTo — so the opponent flickered between his move and the
bank's default stance (frames 21, 69, 77 of the harness run) and those frames
contributed no root motion either. `CefChannel::clipOwner()` now walks that
chain for any channel, the way `PlayerController::clipOwner` always has for
the player. With it the pose source is reported once at the start of the
fight and never again until it ends.

**What is left, still labelled:** `omk::clipTracks` is public and a root delta is `trans[cur] -
trans[prev]`, so both are reachable. And the harness PLACES him two metres in
front of the player, which the engine never does — a script stages the pair
before op 62 runs, and the log line says so.

## 11. Step 4 — the fight camera, and the three faults the log caught

Camera mode 14 is `Fight_TickCamera` (0x00446500) and six helpers, and the
preset table says it is COMPUTED rather than authored: row 14 is all zeros,
fov 0, both subjects 8. The state machine — 1 orbit, 2 the throw swing, 3 a
steady orbit, 4 a ten-frame hand-over, 7 the KO, 8 close — is ported into
`FightCamera`, stepped inside `Fight::step`, and spent by an arm in `omk-play`
that sits ahead of the follow camera and behind the dialogue and editing
holds, which is the engine's own precedence.

**Options row 18 `Caméra de combat` is `byte_906F20`** (`byte_90E1AA`, save
header `+42`), and "Vue de dos" against "Vue de côté" are two rigs, not a
tweak: at 0 the heading offset is forced to −70°, the extra target lift is
dropped, 1.5 m comes off the eye, and the tail clamps the eye 1.5 m above the
midpoint instead of 2.5 m. That is the second options row melee consumes, and
like row 16 the port had been carrying the value and using it nowhere.

**Three faults, all found in the harness log rather than by re-reading:**

1. **The camera started 2000 units away.** `Fight_Begin`'s own tail calls
   `sub_446000(0.0, …)` — the UNEASED variant, which places the eye outright —
   and the port had skipped it, so the first eased frame blended from a zero
   camera and walked in over the next second.
2. **The KO heading ran away** to −534, −1884, then +2796 degrees. The ±45 and
   ±135 nudges are guarded on `dword_530C24`, the replay pass the camera was
   last placed for, so each fires ONCE when the pass changes; applied per tick
   they simply accumulate. The guard also carries the slow 3°/frame drift
   while a pass is unchanged, which the first version omitted entirely.
3. **The eye sank below the fighters** — 127 units under them, which with Y
   pointing down is the clamp pushing the wrong way. The drop is
   `sqrt(13950.028 − d²)` taken from the eye AS IT STANDS to the midpoint and
   only inside 3 m; computing `d` against a stale midpoint inverted it.

**Labelled, not silently skipped:** the tail's collision solve
(`sub_413450`/`sub_416570`/`sub_413440`) is a different family from the follow
camera's `sub_417070`, the only obstruction rule this tree has read, so it is
not modelled and the height clamp runs unconditionally instead of behind the
solve's "did it return a point" flag. The throw state's swing is spread over a
second because `sub_45ACF0`, the channel's remaining time, has no port.

## 12. The first PLAY TEST, 2026-09-16 — three faults, two of them mine

The reader played the supermarket fight on the real path (the shoot phase, out
through AREA 245's zone, the chunk's own `fight.begin 48` — no harness) and
reported: *"the camera does not follow the fight (doesn't move at all) and the
enemy disappears"*, plus that the demo had dropped him at the start of the
shoot phase rather than at the fight.

1. **The camera was computed and thrown away.** The fight arm sat AFTER the
   editing arms in `omk-play`'s chain, and AREA 245's approach ends with
   "editing over — the camera HOLDS its last frame (mode 13, no active
   camera)". That hold then won every frame of the fight. `fight.begin` ends
   with `Camera_Request(0Eh, …)` and a mode request REPLACES the installed
   mode, so mode 14 outranks the editing and its hold; the arm is now ahead of
   both. **Every number in the log looked right while the screen showed a
   frozen shot** — the camera's own trace moved, and nothing consumed it.
2. **The opponent climbed out of frame.** His motion pass added the clip root
   delta's `y` with no ground response: his height went −29 to −63 while the
   player stood at +9.8, and Y points DOWN, so he rose 73 units above the
   fight and left the view. `Actor_ApplyMotion` is what seats a body after its
   clip has moved it, and this tree has that only for the player, so the
   vertical is now DROPPED and labelled until a non-player body has a ground
   pass.
3. **The demo started in the wrong place**, which was mine to get right:
   `--fight-supermarket` now stands the player in the zone that runs the whole
   sequence, so the fight is one command away instead of a shoot phase away.

The lesson is the repo's own, and it cost a second bad demo: a subsystem whose
own numbers are correct can still be invisible, because something upstream owns
the thing it writes into. The camera trace, the pose source line and the
channel counters all read healthy throughout.

## 13. CLOSED 2026-09-16: the camera never came back — the hold read the ID

*"The engine didn't switch back to adventure mode after the end of the
fight."* The log read as though it did — `FIGHT ENDS`, the script resumes, the
Meditek voice-over plays, a doctor is staged, the medical `scx.play.player`
beat runs, the program ends. Then **"editing over — the camera HOLDS its last
frame (mode 13, no active camera)"**, and the view never moved again while the
body went on ticking `MDSTAND` / `MDHEAD00`.

**The premise this section was written on was wrong.** It said *"AREA 245's
script requests no camera after the fight (only `media.play`)"*, and from
there went looking for a release mechanism that did not need one — the
`Scene_GetActiveCamera` branch, or the `autocameraplayer` arm that ships off.
Record 0 asks for camera **0** (`Camera Player`) three times in the fight's
aftermath:

```
    1480  camera.set   0, 0, 2        ; after the potion block
    1559  camera.set   0, 0, 2        ; the death branch, after the medical beat
    1572  camera.set   0, 0, 2        ; the win branch
```

and the potion block's `jmp_if_false 175` at 1302 lands **exactly on 1480**
(1305 + 175), so the block being skipped skips nothing. `--fight-supermarket`
with `OMK_CAMLOG=1` prints the requests at frames 0, 665 and **1031** — two
frames after the hold began. The earlier reading, *"no camera request at all
after frame 806"*, came from a log the camera log was not enabled on.

**The fault was in the frontend, and it was the id test.** `play.cpp` ended
the hold on

```c
    if (holdEditCam && session.cameraId() != heldUnderCamera) holdEditCam = false;
```

The medical editing had been entered under camera 0 from the same script, so
the request for camera 0 moved no id and released nothing. The engine keys on
the MODE instead: `Camera_RequestChanged` (0x004147F0) opens with
`if (*mode != u32(C, 12)) return 1`, so a `camera.set`'s mode 12 arriving
under an editing's mode 13 **always** changes the camera, and the id
comparison further down is never reached.

The fix counts the event. `Session::cameraRequests()` increments in
`applyCamera` past its `Camera_FindWorld` test — every `camera.set`, every
camera-wait resume, every touched zone carrying a camera, every frontend
`requestCamera` — and the frontend watches that instead of the id. It
**subsumes** the id test (`cameraId()` is `camTo_.id`, written nowhere else),
so the clear-list got one case shorter rather than one longer; the three left
are requests that never reach the Session — a conversation, the take, and
`Shoot_Enter`.

`verify.py: engine: hold release` (SLOW) replays the whole fight and asserts
that no frame after the request still draws the held eye and that the drawn
fov is back to the adventure preset's 75.0 from the editing camera's 74.0.
Shown to fail: restore the id test and it reads `(1080, 3, 1, 48, False,
74.0)` - the held eye for all 48 frames after the request. `docs/CUTSCENES.md` §2 carries the finding.

Two lessons worth keeping. **"The script requests no camera" was a claim about
a log, not about the script** — the listing was there to be dumped and says
the opposite. And **a subsystem can be correct and invisible**: the fight
camera, the walker, the script and the Session's own camera were all healthy
and moving; only the thing drawing over them was wrong.

## 15. THE SECOND PLAY TEST, 2026-09-16 — six issues, triaged

The reader played `--fight-supermarket` after the camera fix and reported it
*"better"*, with six things wrong. What follows is each one with whatever
evidence the played log already carries, so the next session does not start
from the sentence. **The order below is the proposed one**, and items 1 and 2
are first because 1 blocks every fight from finishing and 2 is a single fault
wearing two faces.

### 15.1 "AI stopping responding after some time" — the KO never lands

Not the AI. **The fight does not END when the PLAYER WINS.** From the played
log, 18 s in:

```
fight +540: Vie 5 vs 0, states 32/1, entries 63 'C7' / 0 'HGUARD', hits 12
   fight state: KO counter 0, over 0, player won 1
```

`applyDamage` took its decisive branch — `decided_`, `loser_ = opponent`,
`playerWon_` — and the opponent is on 0 hit points. But `koCounter_` stays 0
and he is in entry **0 `HGUARD`**, his guard idle, not a knock-out. The next
**44 seconds** are both fighters standing 1.10 m apart with the AI's move
counter frozen at 383 and `over 0` throughout, which is exactly what an
unresponsive AI looks like from the outside.

The chain is `applyDamage` → `forceEntry(def, def.koEntry)` → the loser's
state becomes 6 or 7 → `koCounter_` → the two replay passes → `sub_445AC0`.
It breaks at the first link: `koEntry` is `entryByRole(c, 5)`, and at +510 he
was in entry 116 `FRAISE`, at +540 in entry 0 `HGUARD`. So either role 5
resolves to nothing in his bank (`H1AVNT`) and `forceEntry` is a no-op, or the
channel left the forced entry on its next transition. **Check
`entryByRole(H1AVNT, 5)` first, and print what `forceEntry` was handed.**

**Why no check caught it.** A headless run presses no keys, so the player can
never attack: the only fight a probe can reach ends with *Kay'l* dying, which
is the branch that works and the branch `engine: hold release` replays.
`engine: melee`'s 6-of-9 KOs are AI against AI on the `CMBT` banks, a
different bank from an opponent taking a player's hits. **The
opponent-loses path has no headless route at all** — the first job is
probably a harness that gives it one, or `engine: melee` will keep passing
over it.

**INVESTIGATED 2026-09-16, and NOT REPRODUCED — read this before starting.**
The obvious readings were all tested and all refuted, so do not repeat them:

* **A headless route now exists.** `--keys` pushes scan codes unconditionally
  (`play.cpp` ~5458), so the player can be made to attack and the
  opponent-losing branch is reachable without a person:

  ```
  K=$(python3 -c "print(','.join(['0x11','0x1F']*200))")     # W and S, kicks
  SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables \
      --save ../traces/save-appart.bin --fight-supermarket \
      --frames 1000 --keys "$K" --keydelay 3
  ```

  Scheme 3: `0x10`/`0x11` punches, `0x1E`/`0x1F` kicks.

* **Both branches of `applyDamage` work, and so does entry 144.** Four key
  patterns were run; every one ENDED the fight. `0x10` alone produces a
  natural `koEntry` kill — `want entry 144 (flags 0x80009020, w12 0x5) -> ok`,
  and the loser walks 144 -> `I_DEATH`(145) -> `I_DEATHLOOP`(146, state 7),
  the KO counter reaches 3 and `sub_445AC0` runs. Forcing the `koEntry` branch
  from the reader's own `from` entry (116 `FRAISE`) works too.
* **The pass-through chase is faithful.** `GoToMove`'s `0x8000` skip really is
  only in the no-`from` arm (0x004A7B80), and every KO call site in
  `Fight_ResolveHit` passes `SetPersoBank(..., a4 = 0)`, so `from` is non-null
  in the engine as well. The arm that resolves it is the THIRD one: when `to`
  is a pass-through, the engine blends into the clip owner behind its
  `0x8002` chain while `+184` stays on `to`. `CefChannel::clipOwner()` already
  walks exactly that chain.
* **`sub_45ABD0` is not a chase**: it is `chan[+184]`, the raw current entry.
* **`forceEntry` did not fail in the reader's run** — the played log carries
  `badLanding 0, chainAborted 0` throughout, and `transitions` climbed 116 ->
  119 across the killing blow.

**What is left, and it is one fact.** The reader's opponent went from entry
116 `FRAISE` to entry **0 `HGUARD`** — and `FRAISE`'s own GoTo *is* `HGUARD`,
so what that looks like is a forced entry that never happened, leaving FRAISE
to run to its clip end and fall through. The only path that does that while
touching no counter is `forceEntry`'s `entry < 0` early return, i.e.
`def.koEntry == -1`. That cannot be checked from the log the reader has,
because no line printed it.

So the port now PRINTS it, on stdout beside the other fight lines:

```
fight LOSER: from entry N 'NAME' (goto N), branch koEntry|knockdown|crouched,
             want entry N (flags 0x…, w12 0x…) -> ok|FAILED; landed entry N …
```

and `OMK_KOTRACE=1` adds 90 frames of the loser's channel afterwards — the
entry, its clip owner and the state the fight reads. **A fight that will not
end is a loser who never reaches state 6 or 7**, and these two together say
which link broke. Note that `landed` on the call frame can legitimately still
be the `from` entry: a blended transition parks the pending state and `+184`
only moves when the blend lands, which is why the trace matters.

**Next: one more played fight with the log kept.** Nothing else will separate
`koEntry == -1` from a timing-dependent path that a fixed-step run cannot
produce.

### 15.2 "no sound fx" and "no visual effect" — ONE fault: the ids resolve
### against the wrong library

The `.CTL` effect records DO fire. 68 `ctl-effect` lines in the played log,
and every one of them fails to resolve:

```
  20 x  ctl-effect: sound id 419 is not in the global library
  19 x  ctl-effect: sound id 408 is not in the global library
  17 x  ctl-effect: sound id 417 is not in the global library
   2 x  ctl-effect: state 135 'IM_FRONT' spawns sprite 11 on attach 5 ('Bassin')
          for 10 frames from 0, scale 0.50, flags 0x01
   2 x  ctl-effect: sprite 11 is not registered by the library or the scene
   3 x  ctl-effect: sprite 8 is not registered by the library or the scene
```

So the timing, the attach point, the duration and the scale are all being
computed correctly and then thrown away for want of a lookup. Two leads:

* **The sound half is looking in the wrong place, and the port's own comment
  says so.** `channel.h` ~325: *"the engine resolves it with
  `Scene_FindSoundIndex` against the RESIDENT scene's chunk-3 records, so the
  caller does that"* — and the caller (`play.cpp` ~6022) passes only
  `globalRt`, the GLOBAL library out of `aventure.scx`. `Scene_FindSoundIndex`
  (0x0048CC80) walks the scene at `+48`, 26-byte records, count `+24`, and
  returns `+22`. The same id names different sounds in different scenes, so
  the global-only lookup is not a near miss: it is the wrong table.
* **The sprite half checks both and still misses**, so it is a different
  question — and note what record 0 does on its first instruction:
  `scene.unload 230`. The fight runs with `ASm49res.SCX` resident, which
  reports **0 effects, 0 set pieces**. Where a combat bank's sprites are
  registered is the thing to find.

### 15.3 "characters colliders issue"

Not yet reproduced, and the handoff already lists two unported pieces that
could be it: the fight camera's collision solve (`sub_413450` /
`sub_416570` / `sub_413440`, a different family from the follow camera's
`sub_417070`), and `sub_45ACF0`, the throw swing's length, which has no port
at all. `Fight_KeepSeparation` is ported. **Needs the reader to say what it
looked like** — bodies interpenetrating, a body passing through the set, or
the camera going through a wall are three different faults.

### 15.4 "black stripes" — ATTRIBUTED: the fade's two bands are stuck dark

The reader's answer was *bars top and bottom*, and a render settles it. Frame
500 of `--fight-supermarket` at 640x480, well inside the fight:

```
row   0 lit    0 / 640      row 416 lit    0 / 640
row  63 lit    0 / 640      row 479 lit    0 / 640
row  64 lit  640 / 640      row 409 lit  640 / 640
```

Rows 0..63 and 416..479 are pure black and everything between is lit. **That
is not the letterbox** (which is opt-in, is 352 rows of 480, and would give
64-row bars only by coincidence): it is `Session::blackFade`'s two bands, whose
height is exactly `(fb.h * 64) / 480` = 64, drawn at `play.cpp` ~17880 with
`bandGrey` at 0.

So the engine's black fade — which is NOT a full-screen quad but two shaded
letterbox bands, the reading behind `todo/omk-play.md` 56 — is running with
its bands fully dark during the fight. AREA 245 record 0 does
`fade.to_black` at bytecode 1148 and `fade.from_black` at **1175**, long
before `fight.begin` at 1236, so the clear is scripted and something is not
honouring it. Start by logging `blackFade().running()` and both `bandGrey`
values per frame across 1148 -> 1175 -> 1236; the port logs no fade line at
all today, which is why the played log could not attribute this.

### 15.6 "the fight animation stays" — the LOSER's body freezes at the teardown

The reader's second played fight, 2026-09-16, and this one is **located
exactly**. He won: `frame 1006: FIGHT ENDS after 626 frames - the player won,
Vie 10 vs 0`, KO counter 1 then 2, and `OMK_KOTRACE=1` shows the loser walking
his knock-out properly —

```
  koTrace 33: entry 127 'GROUND' clipOwner 127 frame 52.0 state 21
  koTrace 34: entry 110 'GROUND' clipOwner 110 frame  1.0 state 6
```

— so he ends lying in `GROUND`, role state 6, which is right. **Then the
teardown takes his body away from him:**

```
frame  380: actor 48 BBC_FN - pose source: the fight channel's own clip
frame 1006: actor 48 BBC_FN - pose source: the bank's default entry, frame 0
```

and there is **no third line** for the remaining 148 frames of the run. At the
end he is still `actor 48 BBC_FN (bank H1AVNT) ... the bank's default entry,
frame 0`. `sub_445AC0` restores the adventure bank, the port loses the fight
channel that was posing him, and falls back to the adventure bank's default
entry held at **frame 0** for ever - one frozen pose on a body that should be
lying knocked out where he fell.

The PLAYER is fine, which is what narrows it: `player: HO1_FN/H1AVNT ...
ACTOR_STATE 1, .CTL state 0 'H_STAND' clip H_STAND frame 28.0, walked 249.1
over 654 ticks`. So the restore works for the fighter who keeps walking and
strands the one who does not.

`sub_445AC0` writes the result to **both actors' `+404`**, the tick-tail
selector (section 5), and that is the field to read first: a defeated actor
almost certainly gets a different tail from a standing one, and the port's
`fightEnd` is where the loser's down-state has to survive the bank swap.

### 15.7 What the same run CONFIRMED

Worth keeping so nobody re-opens them:

* **The win branch runs end to end**, and nothing had ever reached it: the
  reward fires (`prop 163 SHOWN`, `Anneaux 5`, bytecode 1579), the potion prop
  follows (474), and the script reaches its tail.
* **The camera comes back on the win branch too** - `last camera 0`, the
  follow preset - so 15.4's fix holds on both arms of the `Vie == 0` test.
* **15.1 did NOT reproduce.** This fight ended correctly, through the
  `knockdown` branch (`want entry 158 -> ok`). So the stall is INTERMITTENT,
  not a deterministic `koEntry == -1`, and the `fight LOSER:` line stays in
  the port until it is caught with an instrument attached.

### 15.5 "no UI" — this is step 5, already planned

The reader's own note: *"I think this is the next step"*, and it is. Step 5 is
both gauges (`Hud_DrawBar(player, 200, 0, 2)` and `(opponent, 200, 1, 0)`)
and mode 2's four-second overlay `sub_447000`, 306 lines and unread.
`ui/hudbar.h` has mode 0 already.

## 14. OPEN, same play test: the shoot scheme needs a KEYPAD

*"I don't have a keypad."* The shipped `Tirer` scheme turns with keypad 4/6 and
looks with keypad 8/2 (`tables/key_bindings.json`, the engine's own table), so
a laptop cannot play a shoot phase at all. The fight itself is unaffected —
arrows plus Q/W/A/S.

The original's answer is its options menu: the keyboard pages rebind, and the
result is written into the save header, which is why `SettingsBlock` carries
the three binding tables verbatim. **`omk-play` ignores them**: it builds its
`Input` once from `tables/key_bindings.json` (play.cpp 2163) and never consults
`settings.v.keyboard`, although `resolveSettings` reads it (settings.cpp 163).
So the faithful fix is to feed the save's tables into `ControlSchemes` rather
than to invent a remap flag — and a player who rebinds in the real game would
already have their keys honoured.

## 8. Open questions

* ~~**`dword_6A05E0`**~~ — **closed the same day, and it was never a fight
  question**: it is the VM's dry-run flag (`Script_RunToOpcode75` sets it,
  `Script_Execute` clears it), so a dry run skips op 62's effects the way it
  skips every other handler's. The lesson is the repo's own — a global read
  inside one handler says nothing about that handler; find its writer before
  writing it down as a property of the opcode. **And the tree already held the
  answer twice over**: `docs/ASSETS.md` records the same gate as "ordinary
  execution state, not an off switch" (22 of the 153 handlers open with those
  five instructions, asserted by `verify.py: render backends`), and
  `engine/src/script/interp.cpp` names it the dry-run flag a few lines below
  the very block this task annotated. Grepping the global before describing it
  would have cost one command.
* **The three fight DEBUG switches**, found in the `[Preferences]` reader at
  listing line 23255 and not previously recorded: `disable_fight_damages` →
  `byte_91031E`, `disable_fight_ai` → **`byte_91031F`** — which is the global
  gating `Fight_TickAI` in the melee tick, so that gate is a debug switch and
  not a game rule — and `no_fight_guard` → `byte_910321`. All three default to
  0 and are cleared again at `05_sys.c` 1636.
* **What happens when the player's life reaches 0.** `Fight_ResolveHit` clamps
  it at 0, records winner and loser, and freezes both channels; it does not
  kill anybody, and event 2's result code is dropped by its own handler. Where
  a lost fight leads is unread — and in this game death is a reincarnation
  (`memory: playable-characters-not-only-kayl`), so this wants reading before
  step 2 and not guessing.
* **`sub_447000`** (306 lines) — what `Hud_DrawBar` mode 2 puts on screen for
  four seconds after `dword_531030` is stamped. It draws text blocks and a
  bitmap; nobody has read it.
* **AREA 149, the Qalisar arena.** `docs/SCRIPT_VM.md` says the arena "uses
  **none** of these — it is `scx.play.actor` and `actor.stat.set`", yet the
  chunk carries **15** `fight.begin` sites over five opponents, the most of any
  chunk. Both can be true (the staging is scripted, the fights are op 62), but
  the sentence should be re-read when step 2 reaches it.

## 9. Where the fights ARE

17 distinct opponents over 11 AREA and 6 SCENE chunks; the counts below are
sites, so three per fight (one per difficulty). Chunk names from `AREAS.TAG` /
`SCENES.TAG`; the opponent ids are the CHARACTERS domain, which ships no
`.TAG`.

| chunk | name | sites | opponent(s) |
|---|---|---|---|
| AREA 5 / 10 / 186 / 202 / 209 | Jaunpur Hall 03 / 08, Anekbah Hall 03 / 46 / 63 | 3 each | 572 |
| AREA 61 | Jaunpur Tetra 1 | 12 | 315, 127 and two more |
| AREA 140 | Mayerem Sas Hamest | 3 | 415 |
| AREA 149 | **Qalisar Arene** | 15 | 612, 613 and three more |
| AREA 168 | Anekbah CS Lev 1 | 3 | 57 |
| AREA 237 | Anekbah Appart Kayl | 3 | 331 |
| AREA 245 | Anekbah Sup2 Res | 3 | 48 |
| SCENE 10 | 1-15B Telis Demon Toits | 12 | 14 |
| SCENE 25 / 26 / 27 | 2-14 Yob Retour Pont, 2-01B Yob Base, 2-23 Yob Retour Toits | 9 / 6 / 9 | 25 |
| SCENE 28 | 2-34 BE Yob Retour Prison | 15 | 333 |
| SCENE 43 | 1-16 Appart Den | 3 | 393 |

**The route to try first is AREA 245, Anekbah Sup2 Res**: its fight sits in the
chunk's own record 0, and it is the room the supermarket shoot phase leads to —
a phase `todo/handoff-shoot-mode.md` already reaches in one command
(`--area 230 --scene-chunk 56`). AREA 168 and 237 are the other Anekbah ones,
and AREA 149 is the stress case.

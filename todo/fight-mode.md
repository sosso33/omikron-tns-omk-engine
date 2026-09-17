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
solve's "did it return a point" flag. *(Superseded 2026-09-17: the solve is
ported and the clamp is behind it - 15.8b.)* The throw state's swing is spread over a
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

### 15.1 FIXED — the KNOCKDOWN LATCH was never cleared

Reproduced at last on 2026-09-17, with the instrument in place, and the
`fight LOSER:` line answers it in one line:

```
fight LOSER: from entry 161 'KOH_TOP' (goto 127), branch knockdown,
             want entry 132 (flags 0x00008000, w12 0x0) -> ok;
             landed 132 clipOwner 133 'IH_RIGH' state 0
```

`OMK_KOTRACE=1` then shows `IH_RIGH` playing out over 85 frames and falling
back to `HGUARD`. State 0 throughout: the loser never reaches role state 6 or
7, so the KO never fires and the fight runs on - 46 seconds with the opponent
on 0 hit points, which from outside looks exactly like an AI that has stopped
responding.

**The arm was wrong.** `Fight_ResolveHit`'s three arms for a killing blow are

```c
    if (u32(def, 68) == 6 || u32(def, 68) == 21)  SetPersoBank(..., def+48);  /* crouched */
    else if (u32(def, 128))                       SetPersoBank(..., reaction);
    else                                          SetPersoBank(..., def+44);  /* koEntry  */
```

and `+128` is set, a few lines above, only by
`if (u32(reaction, 12) & 0x10000000) u32(def, 128) = 1;`. Reaction 132's `+12`
is **0**, so the latch should have been down and the blow should have taken
the `koEntry` arm - role 5, `I_DEATH`, `I_DEATHLOOP`, state 7, the KO.

**It was up because nothing ever put it down.** The latch is cleared by
`sub_4463C0` - the HIT SHAKE - which past its guard does two things before any
shake maths:

```c
    Game_RaiseEvent(45, ...);    /* the life back onto the record */
    u32(a1, 128) = 0;            /* the knockdown latch           */
```

The port had transcribed the guard faithfully (`c.frameBefore < c.stateF ||
cam_.shake > 20.0f`) and the sine, and neither of those two lines. So once ANY
knock-down landed, every later killing blow took the `reaction` arm. That is
RIGHT when the final reaction is itself a knock-down (`KOH_FRONT`, `KOL_LEFT`
- they lead to the ground) and wrong when it is a flinch, which is exactly why
it was intermittent and why four headless input patterns never reproduced it.

Fixed by clearing the latch where the engine does. Event 45 stays the
caller's, since `play.cpp`'s teardown owns the DB span.

**The probe had been hiding the same fault in plain sight.** `engine: melee`
recorded **6 of 9** fights ending in a KO and that was baselined as normal;
with the latch cleared it is **9 of 9**. Three of the probe's own fights had
been hanging on this. The check now also carries the runtime's own invariant -
a killing blow taking the knockdown arm with a reaction that does not carry
`0x10000000`, which must be 0 and reads **3** without the clear.

### 15.2 FIXED — one cause: a fight loads `fight.scx` and the port did not

**Found by the reader asking the right question.** He proposed a max distance
from the centre of the fight zone; there is no such thing in `Fight_Begin`,
`Fight_KeepSeparation` (a MINIMUM, not a maximum) or `Fight_TickAI` - but
reading `Fight_Begin` to answer him showed its last lines:

```c
    Game_Start(aFightScx);   // "fight.scx"
    ...
    Input_InstallScheme(3);
```

A second `Game_Start`, exactly like the one that installs `aventure.scx` at
boot, and `gamedata/SCPTDATA/fight.SCX` ships: 1 MB, **21 sounds and 16
sprites** that are nowhere else. The sounds are ids **402..423** -

```
   402 CPOING02      407 ARRETCOUP01   414 PUNCHD       419 ELECMB03
   403 CHUTEF04      408 ELECMB02      415 HAMORT01     420 VOICE03
   405 COUPTETE03    409..413 the cries               421 RECEP01
   406 CPIED03       417 MVT02         418 MVT09        422 STEPSH1
                                                        423 PUNCHG
```

- the punches, the kicks, the head hit, the block, the fall, the cries and the
footsteps of a fight. The three the port reported as missing, 408, 417 and
419, are `ELECMB02`, `MVT02` and `ELECMB03`. The sprites are 8..14, 32..35,
40, 43, 44, 192, 193 - the blow effects the combat states spawn.

So the timing, the attach point, the duration and the scale were all being
computed correctly and thrown away at a lookup against a library that was
never loaded. **Both reported faults, one cause.**

The sound side consults `fight.scx` first while a fight runs and falls back to
the global library. The sprite side goes into the one flat table, loaded after
the global library and BEFORE the scene although the engine's `Game_Start`
comes last: measured, the fight's sixteen ids collide with **none** of
`aventure.SCX`'s twenty and the fight's own set `ASm49res.SCX` registers no
sprites at all, so the order is unobservable here and the conservative one is
taken deliberately.

Measured over a won fight: **0** lookup failures (was every one of them) and
**14** effect sounds played. `verify.py: engine: fight library`.

**Worth keeping**: `Fight_KeepSeparation`'s push also goes through
`Actor_Move(..., 1, 1, 0)` - the collide-and-slide - where the port's
`moveBody` simply adds. That is a third place 15.8a's collision belongs, and
it is not done.

### 15.9 FIXED — the pause menu: *Combat* binds `Action / Utiliser` NOWHERE

The reader: *"the pause menu doesn't work in fight mode (opens, but pressing
enter does nothing)."* The interface reads slot 4 (`kUiConfirm`, 0x10) as its
confirm, and what that control IS depends on the installed group:

| group | slot 4 | `Action / Utiliser` |
|---|---|---|
| 0 *Aventure* | `Action / Utiliser` (key 28, ENTER) | slot 4 |
| 2 *Tirer* | `Tir` (key 54) | moved to slot 8 (key 28) |
| 3 *Combat* | **`Coup de poing 1`** (key 16, Q) | **bound nowhere** |

So during a fight a PUNCH confirms a menu and ENTER reaches no bit whatever -
*Combat* leaves slots 8, 9, 12 and 13 empty, so unlike *Tirer* there is
nothing to promote. `play.cpp` already carried the *Tirer* case as a labelled
reconstruction (`todo/omk-play.md` 97f); this is the same fault one group over
and a degree worse.

Fixed by taking the confirm from the key *Aventure* binds to
`Action / Utiliser` - read from the table so a rebind follows it - and
edge-filtering it locally, since it does not come through `Input::frame`'s own
mask; slot 4 is cleared so a punch no longer confirms. A RECONSTRUCTION on the
same footing as the shoot arm, and for the same reason: nothing traced says
the engine re-maps anything, and the reader's testimony that ENTER validates
outranks a reading that only shows nobody has found the mechanism.

`verify.py: engine: fight pause` opens the screen with ESC at frame 500 and
presses ENTER at 600; the screen must open AND close. Shown to fail: gate the
arm off and the open line appears without the close, which is the report.

### 15.10 PARTLY FIXED — effects fired for ONE fighter

The reader: *"the visual effect happens only when the player is touched."*
Both the sound half and the sprite half read `player->` alone, so the
opponent's own hit reactions - his impacts, his fall, his cry - never fired at
all. `Cef_TickEffects` runs on every channel the engine ticks, and
`Actor_TickPlayerAndOpponent` ticks two.

**The SOUND half is fixed**: the opponent's channel is drained beside the
player's. Measured over one won fight, the mix goes from almost pure swing -
28 `ELECMB02`, 14 `ELECMB03`, 8 `MVT02`, one `PUNCHD`, one cry - to

```
   x14 ELECMB02     x6 PUNCHD     x6 PUNCHG     x6 CHUTEF04     x6 CRIMAL01
```

- the punches landing, the fall and the cry. `engine: fight library` now
asserts the DISTINCT id count, because "some audio happened" is satisfied by
the player's own whoosh alone.

**The SPRITE half is NOT fixed**, and the reason is worth stating: the draw is
bound to the player's specifics - `playerMeshes`, `playerRootXZ`,
`playerFeet`, `lastRootDrop`, his pose and his attach search. No staged actor
gets `.CTL` effect sprites in this tree, so this is a general gap that the
fight merely makes obvious, and closing it means giving the sprite spawn a
BODY rather than assuming the player's. The opponent's own pose is already
kept (`fightRun.foePose`) and his position and yaw are in `fightRun.foe`; what
is missing is the feet/root pair, which is the same vertical question 15.8d
settled for the separation and has not been settled for the draw.

### 15.3 "characters colliders issue"

Not yet reproduced, and the handoff already lists two unported pieces that
could be it: the fight camera's collision solve (`sub_413450` /
`sub_416570` / `sub_413440`, a different family from the follow camera's
`sub_417070`), and `sub_45ACF0`, the throw swing's length, which has no port
at all. `Fight_KeepSeparation` is ported. **Needs the reader to say what it
looked like** — bodies interpenetrating, a body passing through the set, or
the camera going through a wall are three different faults.

### 15.4 FIXED — "black stripes": the LETTERBOX, not the fade

**This section said the fade, and the fade was innocent.** The correction is
worth keeping because the two are genuinely indistinguishable on screen: the
engine's black fade is not a full-screen quad but **two bands of
`(h << 6) / 480` rows** - 64 at 480 - and the letterbox occupies *exactly the
same rows*. A render at frame 500 showed rows 0..63 and 416..479 pure black
and everything between lit, and that was written up as the fade's bands stuck
dark. It was not.

`OMK_FADELOG=1` settles it in one line. The fade runs its whole course early:

```
frame   1: blackFade mode 3 clock  0.0/60.0 inner   0 outer   0   <- script 1148
frame 378: blackFade mode 4 clock  0.0/60.0 inner 127 outer 255   <- script 1175
frame 438: blackFade mode 4 clock 60.0/60.0 inner   0 outer   0
frame 439: blackFade mode 0                 inner 255 outer 255   <- cleared
```

By frame 439 it is **mode 0**, `bandGrey` returns 255 both sides and the band
loop is not entered at all. The bars at frame 500 cannot be the fade.

**They are the letterbox, held on by `holdEditCam`.** The bars are suppressed
only when the player has control, and that test reads

```c
if ((adventure || uiPause) && !holdEditCam &&
    !session.playerAnimHeld() && !session.blackFade().bandsDark())
    view.vh = dispH;
```

`OMK_LBLOG=1` prints every term. From frame 439 to the end of the fight:
`adventure 1, animHeld 0, bandsDark 0` - and `holdEditCam 1`. The approach
cutscene's editing ended at 377 with the camera HOLDING, nothing requested a
camera afterwards, and the hold stood through the whole fight.

So this is **the same fault as §13, in a second consumer**. There the stale
hold froze the view; here it letterboxed a frame the player controls. And the
fix is the same one line the clear-list already has for shoot mode:
`fight.begin` ends with `Camera_Request(0Eh, ...)` - mode 14, the fight camera
- so mode 13's hold is over the moment the fight begins.

```c
if (holdEditCam && fightRun.active) holdEditCam = false;
```

`verify.py: engine: fight letterbox` asserts frame 500 (mid-fight) is
full-frame AND frame 300 (inside the approach editing) still has its bars, so
a fix that stopped letterboxing everything fails; the middle row is quoted so
a black frame cannot pass by having no bars.

**The lesson.** Two different mechanisms paint the same 64 rows black, and the
first attribution was made from a render alone - the right observation, the
wrong cause, written into three files before anything tested it. What settled
it was making each mechanism SAY what it was doing (`OMK_FADELOG`,
`OMK_LBLOG`), and both logs are kept for that reason.

**Still true and still unported**: the engine's real letterbox is **per
camera**, not a mode test. `sub_45FA20` is handed `u16(cam, 408..414)` - the
camera block's own viewport rectangle - so which shots letterbox is DATA the
port does not read yet. The rule above is a labelled reconstruction that
happens to agree with every capture (`play.cpp` ~11090 carries the evidence);
porting `+408..414` would replace it with the engine's own answer.

### 15.8 THE THIRD PLAY TEST, 2026-09-16 — collision, and a camera that cuts

The reader, after the letterbox and pose fixes: *"Ok, no animation error. Main
Issues: colliders missing, leading to the ai being ejected outside the combat
zone and the camera placed inside objects. Camera switched side quickly at
some point, making it difficult to understand what happens."*

So the animation faults (15.6) and the stripes (15.4) are closed by play. What
is left is collision and framing, and the run he played corroborates all three.

**15.8a The opponent has NO collision at all.** Not "the wrong collider" - the
port never tests one. `fight.begin`'s opponent has no `PlayerController`, so
his motion pass is the clip's own accumulated root track turned by his facing
and added to his position (`FightRun::foeRoot` in `play.cpp`), plus
`Fight_KeepSeparation`. Grepping the viewer for any wall, soup, walker or
slide term applied to the foe returns **nothing**. The set has 382 wall
triangles and he is tested against none of them, so a knockback or an approach
that ends inside a wall simply puts him there - which is the reader's "ejected
outside the combat zone". In the played fight he ranged x 14891..15195,
z 1408..1774 (sampled once a second, so excursions between samples are not
even visible).

The player is not in this position: he has the walker, which sweeps his
model's spheres against the wall soup. **The question to read first is whether
the engine gives the opponent the same one** - he is an ordinary actor in
ACTOR_STATE 2, and `Actors_TickAll`'s case 2 is where his step meets the
world. If it does, the port's foe wants the walker rather than a bespoke
collider.

**15.8b The fight camera passes through the set.** Already known and recorded
in §4 of the handoff as unported: the camera tail's collision solve is
`sub_413450` / `sub_416570` / `sub_413440`, a **different family** from the
follow camera's `sub_417070`, and none of them has a port. So the camera has
no reason not to sit inside a crate, and in a room made of crates it will.

**FIXED 2026-09-17, and the "family" is three lines.** `sub_413450` copies the
wanted eye into a local camera's `+52`, `sub_413480` the look-at into `+64`,
`sub_413440` returns `&cam[+52]`. `sub_416570` is the solve: `sub_444810` -
the bolts' own world ray - from the look-at TO the eye, and on a hit the hit
point is written into `+52`. The tail then takes **only X and Z** from it
(`f32(+0)` and `f32(+8)`; `dword_9070A4`, the eye's height, is not written),
eases 0.25 m toward the look-at, and runs the height clamp **only when the
solve hit** (`if (v8)`) - which this tree had been running every frame.

Ported as `Fight::setCameraRay`, wired in `omk-play` to the shown set's
`shotSoup` (the `sub_444460` mesh rule the bolts already use). The one guard
not modelled is `sub_416570`'s `(cam+356 & 0x1000) && (mesh & 0x20000000)`:
the camera is a local struct nothing in this tail gives a `+356`.

Over the real supermarket fight the ray hits on **29 frames**, and at the
sample where it is pulling in the eye stands at x 15134 against 15145 without
it. `verify.py: engine: fight camera collision` compares the run against
`--no-fight-camera-collision` (29 / 0 hits, and the eye samples must differ),
shown to fail by dropping the eye writes. Not judged by eye yet.

**15.8c PARTLY FIXED — the throw swing was divided by a constant.**

First, a correction to this section's own evidence. The headings quoted above
were sampled **once a second**, and at 6 degrees a frame a smooth orbit covers
180 degrees in that time - so most of what looked like cutting was the
sampling rate. Measured per frame over a whole fight (`OMK_CAMLOG=1`, 498
frames): **494 turn less than 5 degrees**, one turns 15-45, one 5-15, and
exactly **one turns 134 degrees** - at the frame the fight ENDS, which is the
legitimate hand-back to the follow camera. The orbit itself is smooth.

What is NOT smooth is the THROW. `sub_446240` swings the camera deliberately
around the pair:

```c
    v2 = (double)(rand() % 0x5Au) - -180.0;   /* the arc: 180..269 degrees */
    flt_530C84 = v2 / v12;                    /* per frame = arc / v12     */
    flt_530C20 = v10 + flt_530C20;            /* and the whole arc AT ONCE */
```

and `v12` is `sub_45ACF0(chan)` of **whichever fighter is in state 11**
(`if (dword_906FA4 == 11)` picks A's channel, else B's). The port divided by a
hard-coded **30**, labelled as a stand-in on a reading that called
`sub_45ACF0` "the channel's remaining time".

**It is not.** `sub_45ACF0(a1)` is `return dword_8F5928[57 * a1]` - the channel
base plus **8**, which `GoToMove`'s own docstring names the current clip's
LENGTH, written from `Actor_ClipFrames`. So the swing is spread over exactly
the clip the throw is playing: a short throw swung a third of the way round
and stopped, a long one crawled. `CefChannel::clipLength()` now exposes it and
`camThrow` uses it.

**MEASURED, NOT YET CHECKED, and that gap is deliberate.** Adding the `CATCH`
input to `run_fight`'s press cycle (entries 18/19/20 of `H1Cmbt`, role 10,
codes 0x410/0x420/0x440, all inside the 0xCFF union) does reach a throw - **2
placements, and `swing * clipLength` inside the 180..270 band both times**,
which is the fix measuring correct. It was NOT adopted, because the same press
surfaces **two damage re-derivations that disagree** (want 1, got 14 and 20),
and they are the probe's fault rather than the runtime's: the throw's base is
`reactE->combat.damage()`, the block of the entry the **attacker** is put into
(`reactIdx`), while the event records the attacker's `CATCH` entry as
`fromEntry` and `followIdx` - the VICTIM's entry - as `reaction`. **The event
does not carry the number the probe would need.** Re-deriving from
`e.reaction` makes the row green by making those two skip as "no combat
block", which is a vacuous pass and worse than the red one.

So the next step is small and named: carry the throw's damage SOURCE on the
event, then adopt the `CATCH` press and assert `swing * clipLength` in
180..270 with a non-zero placement count. Until then `engine: melee` is left
exactly as it was - 204 figures re-derived, 0 mismatches - rather than
baselined around a hole.

**Still open in 15.8c**: whether the engine damps the orbit at all. Both
`case 3` and `case 8` of `Fight_TickCamera` are DEAD in the shipped engine -
`dword_906F50` is only ever written 1, 2, 4 or 7 - so case 8's `flt_530C60 =
0.01` (against the default 0.5) and its divider of 3 never run, and the port
is faithful in not reaching them.

### 15.8d THE ROOT CAUSE — a PHANTOM 41-unit gap holds the fighters apart

**The reader's own hypothesis, and it is right**: *"I think some camera issues
are when Kay'l and the opponent are at the same place at the same time but
this shouldn't happens when collision will be there."* His played fight shows
exactly that, and the cause is not the missing wall collision - it is a units
mismatch between the two fighters.

From the played log, during the camera flips:

```
bodies: player 15059 13 1709 facing 2, opponent 15059 -31 1707 facing 182
```

Same x, 2 units apart in z: they are standing **inside each other**. And the
same line's separation reads `43 apart (1.10 m)`, comfortably outside the
43.4 separation radius, so `Fight_KeepSeparation` pushes nothing.

Both are true because `measureSeparation` is a **3D** distance:

```cpp
    const float dy = b_.body->y - a_.body->y;
    separation_ = std::sqrt(dx * dx + dy * dy + dz * dz);
```

and the two `y` values are **on different origins**. The player's comes from
his walker, whose origin is the FEET; the opponent's is his staged placement,
which names the PELVIS. The gap between them is a constant ~41 units, and
`sqrt(0^2 + 41^2 + 2^2) = 41` - so the separation is satisfied by a vertical
distance that does not exist. The engine has no such problem: both actors
store their position at `+244/+248/+252` in one convention.

**And the camera fault falls out of it.** `camOrbit` takes its heading from
`atan2(b.z - a.z, b.x - a.x)`. With the pair horizontally coincident that
vector is a couple of units long and its direction is noise, flipping ~180
degrees a frame; the eased eye (`eyeK` 0.5) is then pulled between two
antipodal points on the orbit, and the midpoint of two antipodal points is the
CENTRE. Measured over the reader's fight: **31 frames turn more than 90
degrees**, and at each the eye sits **8 to 52 units** from its target where
the orbit radius should be about 131. The camera is not cutting between two
framings - it is sitting on top of the fighters and spinning.

So 15.8c is a SYMPTOM of this, not an independent fault, and the throw-swing
fix (`6af0597`) was real but was never the thing the reader was seeing.

**Why no check caught it.** `engine: melee` asserts `tooCloseAfterPush == 0`
and it is 0 - because `run_fight` builds both `FightBody`s itself, on one
convention, so the phantom offset cannot arise there. It needs a staged
opponent beside a walking player, which only the viewer has.

**FIXED 2026-09-17.** The opponent's placement is the convention to meet,
because it is the one the engine's actor record uses, so the PLAYER is lifted
to his pelvis on the way into the fight step (`player->pos()[1] -
player->cameraLift()`) and dropped back to his feet on the way out, where the
frame's delta is fed to `player->nudge`. Measured over a real fight:

| | before | after |
|---|---|---|
| `\|dy\|` between the two bodies | 41 to 44 | **1** |
| minimum horizontal gap | **2.0** | **43.3** (the radius is 43.4) |
| samples inside the radius | 7 of 15 | 0 |

`verify.py: engine: fight separation` drives `omk-play` rather than
`run_fight`, because the probe builds both bodies itself on one convention and
so cannot see this at all.

**The original note, kept because it is still true of what remains:** putting
both fighters on one origin was upstream of everything else in 15.8 - it is upstream of the camera AND of the collision
work, since a collide-and-slide that starts from overlapping bodies has
nothing sensible to do. Note that `fightRun.foe.y` is also published straight
to the drawn body (`fightRun.body->at[1]`), so the two uses have to be
separated rather than the value simply shifted; and the port's existing
labelled note about the opponent's vertical (`play.cpp`, the root-motion
block) is the same question seen from the other side.

**Order.** 15.8d first: it is upstream of 15.8a: it is a gameplay fault, not a framing one, and it is
the one that can put an opponent somewhere the fight cannot continue.

### 15.8e FIXED 2026-09-17 — the fight began where the approach STARTED

**This is the "upstream question" 15.8a ends on, and the answer is the port's.**
The two fighters were never meant to open 11.9 m apart with `SMbox45` between
them. The played log says so in two lines of the same frame:

```
frame 379: FIGHT BEGINS against CHARACTERS 48 ...
    bodies: player 14995 -32 1856 facing 0, opponent 15132 -31 1415 facing 197
  pose: actor 48 BBC_FN - its program ended; he stays where it left him (14959 -29 1902)
```

The approach program `3DCombat2` walks the robber along `D1BassinP2` from
(15134, 1408) to **(14959, 1902) - 1.5 m from the player**. But `fight.begin`
runs inside the script pump, BEFORE that frame's staged pass notices the program
has ended and carries `drawAt` into `at`, and `beginMelee` read `at` - which
while a program runs is re-asserted to the program's PLACEMENT, the path start.
So the fight began at the start of a walk the player had just watched finish.
The engine's actor record has no such split: `Anim_RootDelta` sums the clip into
`+244` every tick, and that is what `Fight_Begin` reads.

Fixed by the rule `npcBody` already uses, `progRan ? drawAt : at`. Over the same
run the fight now opens **59 apart (1.50 m)**, both on `HGUARD`, with **AI moves
0** - the clipless approach entries 208/228 with intent 104 that the handoff
listed as "the opponent's first second" were the AI closing a distance that
should not have existed, and they are gone with it. `verify.py: engine: fight
separation` asserts the opening gap (50, floored; 460 with the bug).

**What this does to 15.8a.** Every collision attempt failed the same way,
stuck 9.2 m out against a box - and the box was only in the way because of this.
The walker wiring is worth re-trying now, from the patches outside the tree;
the reader's *"the AI being ejected outside the combat zone"* is still a real
fault (nothing stops a knockback at a wall), but the reason not to wire it has
very likely gone.

### 15.8a WIRED 2026-09-17 — the opponent collides

After 15.8e removed the gap, the fifth attempt is the one that stays. The
opponent gets the player's own `omk::Walker` over `playerSoup` /
`playerSteep` (and their grids), seated on his FEET - his `y` is the pelvis
and `foeLift` (42.8 for `BBC_FN`, the player's `camLift_` recipe) below it -
sweeping his own model's four spheres of radius 8.7. **Everything that moved
him in the frame is one try**: `Fight::step`'s separation push and knockback,
then the clip's root motion, are summed against the walker's position and
handed to `step`, which is `Actor_ApplyMotion`'s apply-remember-undo-`Actor_Move`
shape. The vertical is still NOT his (he keeps his placement height, the
labelled note in the root-motion block stands); only the horizontal collides.

**The real supermarket fight does not change, and that is measured, not
assumed**: with and without the walker the bodies lines are identical and the
fight ends on the same frame, with the default keys, with the kick cycle, and
with the player walking forward throughout - Kay'l has 10 health and the fight
never leaves the middle of the room. So the reader's *"ejected outside the
combat zone"* was, as far as this run can tell, 15.8e: the robber crossing
11.9 m and `SMbox45` with nothing to stop him.

What proves the sweep engages is a HARNESS, `--fight-foe-at 15111,1440`, which
starts him behind the box: collision on, he stops against it at z 1510 (74
frames swept into it, 80 blocked, by +180); `--no-foe-collision`, the same
approach walks through to the player. Whether the engine's robber would stand
at that box too is the expected reading (a brawl has no path-finding) and is
NOT verified. `verify.py: engine: fight collision`, shown to fail with the
opponent's sweep radius at 0 (he reaches z 1870).

Still open from 15.8: the fight CAMERA's collision (15.8b), and whether
`Fight_KeepSeparation`'s push goes through `Actor_Move` in the engine - here it
does, because it is summed into the one try; that is this port's choice and
is labelled as such.

### 15.8a ATTEMPTED 2026-09-16 — the reading is settled, the wiring is NOT

**Settled from the engine, and this part is not in doubt.**
`Actors_TickAll`'s `case 2` is `Actor_TickPlayerAndOpponent` (0x00466710) and
it calls `Actor_ApplyMotion` on **both** arms - the player's and
`g_FightOpponentRec`'s - the opponent additionally refreshing his
spatial-index slot. `Actor_ApplyMotion` (0x004672D0) is try-then-ask:

```c
    f32(actor,244) += dx; ...                 /* apply the velocity      */
    wx = f32(actor,244) - f32(actor,232); ...  /* remember the delta      */
    f32(actor,244) -= wx; ...                  /* UNDO it                 */
    Actor_Move(actor, wx, 0, wz, &mout[0], &mout[1], &mout[2], 1, 1, 0);
```

- collide-and-slide, **horizontal only** (the `0` in the `y` slot), then a
ground probe and `Walk_GroundResponse`. So the opponent collides in the engine
with the same machinery the player does, and the port testing him against
nothing is a real gap, not a stylistic one.

**The wiring was tried and REVERTED, and the failure is worth recording
because it rules things out.** Giving the foe an `omk::Walker` on the
player's own `playerSoup`/`playerSteep` leaves him **stuck 9.3 m from the
player for the entire fight**, in the approach entry 228, his AI still
choosing moves (1085 of them in 720 frames) and the separation never closing.
Three configurations, all the same:

| | result |
|---|---|
| walker seated on his `y`, no sphere centres | stuck at 9.29 m |
| walker seated on the FLOOR under him (`floorUnder`), no centres | stuck at 9.30 m |
| walker seated on his `y`, his model's 4 centres as authored | stuck at 9.25 m |
| **blocker sweep disabled (`setBlockers(..., 0.0)`), ground half only** | **`FIGHT ENDS` at 663, byte-identical to no collision at all** |

So the GROUND half is safe and the BLOCKER half is what stops him, and it is
not a radius or a centre convention - all three shapes block identically.
Something in `playerSteep` stands between them that the engine's `Actor_Move`
evidently does not stop him on.

**FOURTH ATTEMPT, 2026-09-17, and the block is FAITHFUL.** Seating the foe's
walker on his FEET with his own derived lift - his `y` is the pelvis and the
floor under him is his feet, so `lift = floorY - y`, the player's own recipe
with the opponent's numbers - leaves him stuck at **9.2 m**, the fourth
configuration to do so within a tenth of a metre. It is not the sphere setup.

**What stops him is a real box, and the engine would stop him too.**
`mesh_list` puts `SMbox45` at (15111, 1554) and he halts at (15110, 1506) -
his 8.7 radius short of it. The box's mesh flags are `0x00000000`, and the
tempting idea that bit `0x4` marks a collidable mesh (23 of this set's 36
carry it) is **refuted by work already in this tree**: `Sweep_MeshTest`
(0x004AD460) opens `if ((flags & 0x20000000) == 0 && (flags & 0x41) == 0)`,
an EXCLUSION, and `collision.cpp`'s own note records that a filter admitting
only those bits "would keep 0-4% of a set's meshes and let the player walk
through the world". A flag-0 mesh is solid. So the port's blocker soup is
right and the walker is right to stop him.

**The real question is therefore upstream: why must he cross a box at all?**
The two approach beats leave the fighters **11.9 m apart** - opponent at
(15134, 1408), player at (14995, 1856) - and `SMbox45` sits between them, so
`Fight_TickAI`'s approach walks him straight into it. A brawl has no
pathfinding, so the engine's robber would walk into it as well *if the
geometry there were the same at that moment*. The next thing to establish is
whether it is: the port's blocker soup is baked from the `.3DO` AT REST, while
the engine sweeps each mesh through its CURRENT matrix (`Sweep_MeshTest` moves
the sweep into it), and `collision.cpp` already carries `meshOf` for exactly
that patching. Whether `ASM49RES`'s boxes are where the rest pose puts them
once record 0 has run - it opens with `scene.unload 230` - is unread.

Both attempts are saved as patches outside the tree. Nothing is committed: the
opponent still has no collision, and adding it as it stands would trade
"walks through crates" for "never reaches the player", which is worse.

**The tail arguments are now READ, and they are NOT a face filter.** That was
the obvious hypothesis - "which faces this actor is allowed through" - and it
is wrong, so it is recorded here rather than left to be found again.

`a8` is a MODE SELECTOR: a `switch (a8)` whose arms install constants into two
locals, and mode **1** (what `Actor_ApplyMotion` passes) sets both to
**786444 = 0xC000C**. Those locals reach `Walk_ClampNormal(mask, 0, ...)`
(0x0046A020), and its first argument IS a bitmask - but of **axis clamps**:

```c
    if ((a1 & 0x10000) && v6 > 0.0)    *a4    = 0.0;   /* normal.x >= 0 */
    if ((a1 & 0x20000) && *a4 < 0.0)   *a4    = 0.0;
    if ((a1 & 0x40000) && v9 > 0.0)    a4[1]  = 0.0;   /* normal.y       */
    if ((a1 & 0x80000) && a4[1] < 0.0) a4[1]  = 0.0;
    if ((a1 & 0x100000) && v10 > 0.0)  a4[2]  = 0.0;   /* normal.z       */
```

`0xC000C` carries `0x40000 | 0x80000`, which clamps the collision NORMAL's y
in both directions - the slide is kept horizontal - plus `0x4 | 0x8`, tested
further down. So the constant governs how a hit is RESOLVED, not which
triangles are tested, and `a10` is stored to `dword_6A52B8` (written once in
the whole image and never read).

**And a coincidence to disbelieve.** 24 of the fight set's 36 meshes carry
flag bit `0x4`, and 10 carry none of `0xC000C` - including `SMbox45` at
(15111, 1554), a metre from where the blocked opponent stopped (15110.5,
1506.9). That looks like the answer and is not: the mask is an axis clamp and
the mesh bit is a mesh bit. The numbers are real and the connection is
invented, which is exactly CLAUDE.md 1's "the data was consistent with the
wrong answer".

**So what stops him is still open**, and the remaining candidates are the
SET rather than the resolution: whether the engine's sweep walks the same
triangles the port's `playerSteep` holds. `playerSteep` is a SLOPE criterion
(faces steeper than 30 degrees, minus `0x20000000|0x41`) and the engine's own
set comes out of `o3de_ForEachMeshInBox` feeding `Sweep_ActorMove`
(0x004AD360), which has no port and is where this goes next.

The attempt is saved as a patch outside the tree; it is 115 lines and all of
it is in `play.cpp`'s `FightRun`. Nothing of it is committed, so the tree
still has no collision on the opponent and the fight still ends at 663.

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

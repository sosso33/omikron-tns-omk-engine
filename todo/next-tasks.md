# The next tasks — a reader's list, with a first pass at ordering

Collected 2026-09-06 from a reader, features and bugs mixed and in no
particular order. This file adds a **quick** assessment per item — what the
repo already knows, what code exists, how hard it looks, and where I would put
it — so the list can be ordered. It is a triage, not a design: every "S/M/L"
here is a guess that the first hour of the actual work is allowed to overturn.

**Sizes**: S = a sitting, M = a slice with a plan file, L = several slices.
**Evidence** says how much of the answer is already in the tree, which is
usually a better predictor of cost than the size of the feature.

---

## Suggested order

The principle: **bugs with a known mechanism first** (they are cheap and each
one removes a wrong impression of the port), then **UI that the lifted tables
already describe**, then **systems that need fresh reverse-engineering**. Two
items are research and can be done any time they are wanted.

| # | task | size | evidence | why here |
|---|---|---|---|---|
| 1 | Enter held ≈ 0.5 s counts as several presses | **S** | strong | mechanism already documented; affects every screen and every conversation |
| 2 | black stripes entering/leaving a building | **S** | strong | the letterbox rule is already written down and this contradicts it |
| 3 | ESC quits instead of opening the pause menu | **S/M** | strong | the screen exists in the lifted table; today ESC loses the session |
| 4 | tuto zone fires repeatedly, player not stopped | **M** | strong | zone lifecycle is read and there is already a check nearby |
| 5 | black frames in the Impasse cutscene | **M** | strong | the port already LOGS the moment it happens |
| 6 | street NPCs stop and T-pose | **M** | good | same family as the scene-facing work of 2026-09-05 |
| 7 | missing animations (lift doors, Kay'l's drawer) | **S/M** | good | may already be fixed - CONFIRM FIRST, it is the cheapest item on the list |
| 8 | save support (save, save menu, load menu) | **M** | very strong | the format is solved end to end; this is plumbing, not research |
| 9 | main menu completed (new game correct, the rest) | **M** | very strong | the widget tree and the answer sites are lifted |
| 10 | sneak: character / info / config pages | **M** | very strong | the panels are already named constants in the port |
| 11 | shop UI | **M** | strong | 7 screens and their jump table are read |
| 12 | multiplan UI | **M** | fair | the tile background is read; the panel is not |
| 13 | any remaining UI screens | **M** | fair | 37 screens are enumerated; what is left is the tail |
| 14 | health: fall damage and vehicle hits | **M/L** | **weak** | nothing in the DB doc or the port; needs reading before estimating |
| 15 | jump / fall animation and physics | **L** | fair | the walker's slope and step rules are ported; the fall tiers are not |
| 16 | slider: call, ride, drive | **L** | good | already MEASURED at ~600 undecompiled lines |
| 17 | fight mode | **L** | good | the AI profiles and combat block are read; nothing is wired |
| 18 | shoot mode | **L** | good | read, and deliberately unwired - a DECISION to revisit, not a gap |
| 19 | does the original filter (anti-aliasing, …)? | **research** | fair | cheap to answer, and the answer may be "no reachable tier" |

---

## The items

### 1. Enter held for ~0.5 s counts as several presses — DIAGNOSED 2026-09-06

**Reproduced, and it is exactly linear in hold time** (`--hold`, headless):

    hold Enter  1 frame  -> 1 action
    hold Enter 15 frames -> 15 actions      (the reader's 0.5 s)
    hold Enter 30 frames -> 30 actions

**It is NOT the repeat mask, which was this file's first guess.** The port
already implements the engine's rule faithfully - `edges = held & (held ^
(repeatMask & last))`, `0x203F` under a screen and `0` in the world - and the
dialogue confirm already takes `edgeBits`. That guess was wrong.

**Where it actually happens.** The `.CTL` channel never changes state: it sits
in `0 'H_STAND'` with the idle clip advancing while the *per-tick* entry path
(`channel.cpp`, flags `0x10 | 0x200000`) emits the `MDACTION` special move
every frame:

    per-tick move 'MDACTION'  from 0 to 24  flags 0xC5F00013
                              inputCode 0x10  word 0x10  lastInput 0x0

The channel does carry the engine's held-button guard - `if (word !=
lastInput_) commit`, transcribed from `Cef_TickChannel` 0x004A853A - but this
entry's flags include **`0x10000000`, whose only job is `lastInput_ = 0`**, so
the guard is cleared every tick and the same held word commits again. The
port's comment claiming "entering a state is once per press by construction"
is false here: no state is ever entered.

**And the engine does the same thing.** Traced rather than assumed:
`Cef_TickChannel` reads the raw input with `sub_43E080` -> `sub_43D920`, which
is a DirectInput STATE read (bit `0x80` = key down, so held), and
`sub_4A7A20(channel, raw, &word)` is a pure bit remapper onto the 14 slots
with a left/right mirror under channel flag 8 - **no edge logic anywhere**.
The port's per-tick loop even passes the same `Cef_FindTransition(.., 2, 784,
1)` filters. So the original ALSO queues `MDACTION` every frame while Enter is
held.

**So the guard is in the HANDLER, and the port does not run it.**
`tab_special_move[3]` = **0x0046AEC0** (no `proc` label - read it with
`asmfn.py`). It switches on `[esi+194h]`, the actor's **`+404` ACTOR_STATE**,
over 11 cases with states 5-10 and 12 falling to a default arm that:

* clears `dword_53AE1C`, then calls `sub_41C810(actor, &obj, &dist,
  dword_53B078)` - the nearest-interactable search;
* returns immediately if it found nothing (`obj == -1`);
* returns if **ACTOR_STATE == 3**;
* compares the returned distance against `flt_4BC918` and returns if it fails;
* branches on the actor's `+164`, and only then reaches
  `sub_465D30(actor, obj, 0)` - the function `play.cpp` already names.

`omk-play` today fires `session.pressAction()` for **every** `MDACTION` name it
sees, with none of that gating. **The fix is to transcribe 0x0046AEC0**, not to
add an edge filter or a timer - both of which would invent a rule the engine
has not got, and the second of which would have papered over this.

**What was DONE 2026-09-06**: the two gates that transcribe exactly — the
ACTOR_STATE switch (`byte_46B2BC[state-4] = {0,4,4,4,4,4,4,1,4,2,3}`, so states
4/11, 13 and 14 take other arms and are now refused with a log line) and the
arm's own `ACTOR_STATE == 3` refusal. Those are safe and correct.

**What is NOT done, and the attempt is recorded because it half-worked.** The
repeat guard is `sub_465D30`'s tail:

    SetPersoBankGroup(channel,
        Cef_FindGroupById(bank, dy <= 27.472441 ? 143 : 41))

— 27.472441 units being 0.6978 m, the low/standing split. It is that STATE
CHANGE that stops the `H_STAND -> 24` per-tick entry matching, **not** the
memset beside it: with the memset alone a 20-frame hold still fired 20 times.

Doing only the switch takes a 20-frame hold from 20 activations to **1** — and
**parks the player in `.CTL` state 54 `H_WAITOB` for ever**, because the engine
leaves that bank again when the action completes and this port has no such
path. A second press then never works, which is worse than the repeat. So it
was measured, backed out, and written down here.

`CefChannel::resetInputLatch()` was added and kept: it is the memset half
transcribed verbatim and will be wanted when the rest lands.

**So the remaining work is `sub_465D30` itself** (190 lines, `21_d3d.c`) — and
reading further shows it is the WORLD TAKE slice, not a bug fix. Decomposed
2026-09-06:

1. **`sub_465D30`'s adjust**, the first two thirds: it takes the actor→object
   vector, the actor's facing (`-Z` through `node+156`), the signed angle
   between them, and walks the actor with `Actor_Move` to stand
   **23.622047 units (0.600 m)** from a low object or **15.748032 (0.400 m)**
   from a standing one, with a 9.8425198 (0.25 m) settle tolerance and a
   49.21259842519685 (1.25 m) give-up. All round metres again.
2. **the bank switch**, its tail — `Cef_FindGroupById(bank, dy <= 27.472441
   ? 143 : 41)`. This is the repeat guard and it is one line.
3. **THE WAY BACK, which is the part that makes 1 and 2 safe**, and the reason
   the switch alone strands the player. Group id 41's default entry lands in
   `.CTL` state **54 `H_WAITOB`** (H1Avnt, group index 4, clip 14) — *wait for
   object*. Its only two exits are states **55 and 56**, flags `0x80000013`,
   **no clip**, both carrying the move bit `0x10`: they are entries waiting on
   the take pipeline's own special move. So the actor leaves `H_WAITOB` when
   the OBJECT is delivered (`MDGETOBJ` and friends), and nothing else does it.
   `Actor_TickScxDriven`'s `actor+1308 = 1` -> `Cef_DefaultGroup` restore is a
   different path, for actors a script was driving.

**CORRECTION, same day, and it is good news.** "The actor leaves `H_WAITOB`
when the object is delivered" was wrong. `take_probe` reads the graph and
`H_WAITOB` (state 54, group 4) exits on **player input**:

| state | group | input | move |
|---|---|---|---|
| 24 | 0 | `0x10` Enter | `MDACTION` |
| 52 | 3 | — | `MDGETOBJ` (automatic) |
| **54 `H_WAITOB`** | 4 | — | the wait pose, clip 14 |
| **55** | 4 | **`0x10` Enter** | `MDPUTSNK` — take and bank it |
| **56** | 4 | **`0x20` Space** | `MDNOTAKE` — decline |

So the original's interaction is: press Enter at an object, he reaches for it
and waits; press Enter again to take it, or Space to decline. Two presses, and
holding the button does not repeat because after the first the machine is in
group 4 where the same bit means *take*, not *reach*.

**And the port already gets most of the way.** With the bank switch applied,
`MDACTION` fires ONCE, `MDGETOBJ` follows automatically, and the actor reaches
`H_WAITOB` correctly. `findTransition(54, 0x10)` **finds state 55**. The chain
is right.

**The blocker is the FRONTEND, not the take.** In `H_WAITOB` the channel is
handed `0x40000000` (the idle word) every tick even with Enter held for 60
frames, and `play.cpp`'s own `bits` is `0000` at the same moment. The suspect
is the repeat-mask gating:

    if (walk) in.setRepeatMask(kUiRepeatMask);
    else if (adventure) in.setRepeatMask(0);

With neither true - which is what a running zone script looks like - the mask
KEEPS ITS LAST VALUE, and after the boot screens that is `0x203F`. The world
then gets EDGES where it should get held bits. That is a one-line-shaped bug
and it is the next thing to test.

So the order is now: **fix the input gating first**, then the bank switch, and
the take completes itself. Doing the bank switch alone is still measurably
worse than the bug (proven above) until the input reaches `H_WAITOB`.

**Also reported (2026-09-06): the same repeat happens in SHOP SELLER
dialogues.** Not yet reproduced. Worth checking whether that path is the
dialogue confirm (which already takes `edgeBits`) or a screen whose mask is
left at the world's 0 - the same gating suspect from the other side.

Size **M -> M/L**, and it is the same work as the "world TAKE" the port's own
comments already reference. The Enter-repeat symptom is a consequence of the
take being unported, not an input bug - which is worth knowing before anyone
spends a day on input code.

### 2. Black stripes entering/leaving a building — S, strong evidence

Almost certainly the **letterbox**. `CLAUDE.md` §5 and `play.cpp` already say
it: the 1.818:1 strip is measured off DIALOGUE captures, so it is evidence
about *camera mode* and not about free roaming — the viewer draws full-frame
when `adventure && followCam`. Stripes on a transition therefore mean the
follow camera is briefly not selected while the areas swap.

Cheap to localise: log the camera mode across a door. The rule is written
down, so this is making the code obey a rule the repo has already established.

### 3. ESC quits instead of opening the pause menu — S/M, strong evidence

The pause screen is one of the 37 in the lifted table and the widget walk can
already open screens. Today ESC ends the process, which also means a reader
cannot pause to look at anything. Two parts: bind ESC to the screen rather
than to quit, and give the screen its items' behaviour.

### 4. Tuto zone fires repeatedly and does not stop the player — M, strong

The zone lifecycle (enter / activate / leave, the 68-byte record, the save
bit) is read and there is a `tuto camera` check already. Two symptoms in one
report: the trigger re-arms, and the player is not halted. **The save bit is
the first thing to look at** — a zone that should fire once is normally made
one-shot by state, and firing several sounds and fades is what an un-cleared
bit looks like.

### 5. Black frames in the Impasse cutscene — M, strong evidence

The port already prints the moment: `editing over - camera falls back to world
camera 2158 (a cut, Camera_Request(0) with travel 0)`. So the question is what
the engine does at the end of an editing that this does not. `docs/CUTSCENES.md`
covers the editings and the fact that they do not wait; the black frame is
likely one frame with no valid camera between the two.

Cheap to reproduce, and the log line is the entry point.

### 6. Street NPCs stop walking and T-pose — M, good evidence

The reader's guess (end of their script) matches what 2026-09-05 found in the
same area: a scene actor is not placed before his first animation step runs,
and every step writes the Euler. A T-pose is the classic "no clip selected"
state, and `verify.py: played actors` already counts the cast.

Worth checking whether these are procedural WALKERS (`.OPT` sliders) or
authored extras (`scx.play.actor`) — the two have different owners and the
answer changes the fix entirely.

### 7. Missing animations — S/M, good evidence, **confirm first**

Lift doors, the drawer in Kay'l's apartment. The reader notes it may already
be fixed. `engine: env anim` exists and covers a scripted environment
animation. **The cheapest item on the list is establishing whether this
reproduces at all** — do that before estimating anything.

### 8. Save support — M, very strong evidence

The one that is nearly free relative to its value. The format is solved end to
end (`GAME_STATE` §8, §8a): the slot geometry, the DB, the clock fields, the
3496-byte settings header, and `savefile.*` already reads slots and settings.
`tools/sim/ui.py` models the LOAD panel and the port walks it.

What is missing is the writing and the two menus, not the knowledge. Ranked
high because a port you cannot save is hard to test anything else in.

### 9. Main menu completed — M, very strong evidence

The start menu already ANSWERS FOR ITSELF in both the simulator and the port
(type a name, walk to Confirmer, the empty-name gate refuses). `ui_widgets`
carries the panels, items and answer sites. New game works but is "not
correct" per the reader — worth pinning what differs before building the rest.

### 10. Sneak: character / info / config pages — M, very strong evidence

`engine/src/ui/widgets.h` already names `kPanelSneakIdentity`,
`kPanelSneakMemory`, `kPanelSneakOptions`, the tab column, the shared row and
status lists and the page builders. The inventory page works. These three are
the same shape with different builders, so the second one should be much
cheaper than the first.

### 11. Shop UI — M, strong evidence

`docs/UI.md`: seven screens through one jump table (`Ui_OpenShop`, with case 4
falling through into 6), the titles naming their own screens 8 of 10, and the
inventory channel's buy/sell at half price already read. The data channel is
ported; the screens are not.

### 12. Multiplan UI — M, fair evidence

The tile-map background is read (and `ui page` asserts that MULTIPLAN's
lit-copy strip is black in the composed background, which is a real detail).
The panel behaviour is not. Nothing in `engine/` references it yet.

### 13. Any remaining UI — M, fair evidence

37 screens are enumerated with their bitmaps and text files resolving; 28 are
walked. This is the tail after 9-12, and should be re-scoped once those are
done because they will have built most of the machinery.

### 14. Health: fall damage and vehicle hits — M/L, **weak evidence**

The honest one: **nothing in `docs/GAME_STATE.md` or `gamestate.h` mentions
health at all**, so this is the item on the list whose size I trust least. The
reader is right that it needs careful reading first — where the value lives
(the 8192-byte DB? the actor record?), who decrements it, and what the fall
tiers and the vehicle collision actually raise.

Do the reading as its own step and re-estimate. Do not start by inventing a
health variable.

### 15. Jump / fall animation and physics — L, fair evidence

The walker is ported with the 30° slope limit and the 30 cm step, and the
"fall tiers" are named in `CLAUDE.md` §4 but not ported. Jumping is a
`tab_special_move` row and an `ACTOR_STATE`. Related to 14 (a fall that hurts
is a fall that landed), so the two may want doing together — read for 14
first, then decide.

### 16. Slider: call one from the sneak, transport, drive — L, good evidence

Already measured, which is why it is trusted: `todo/standing-unknowns.md` §5
puts `Slider_TickRide` at ~600 lines of undecompiled machinery
(`sub_4573E0` 387 lines, plus two more) with its own globals. Three facts are
already pinned — the ride runs at HALF SPEED, camera mode 8's subject is the
slider not the player, and the mount binds the player's `node+156` to the
slider's matrix. The sneak's slider page exists in the widget tree.

### 17. Fight mode — L, good evidence

The `.CTL` combat block is fully decoded (damage, hit window, reaction by id,
knockback), the four AI profiles are read (button combos injected into the
player's own input queue), `tab_special_move`'s 66 rows are lifted, and
`Fight_Begin` installs control group 3. Nothing is wired to a frontend.

Note the oracle problem: `traces/fight.log` proved combat CANNOT be captured
by the trace rig, because `fight.begin` announces nothing. So this will be
data-constrained rather than trace-verified, like the `.CTL` channel.

### 18. Shoot mode — L, good evidence, and a DECISION to revisit

Read: four callbacks by character type, Gandhar's three compiled behaviour
scripts, 302/3/1/0 over 306 sites. `todo/standing-unknowns.md` §2 records that
the brains were **deliberately not wired**, because the generic arm's choice
depends on navigation, line of sight and weapon range — none of which this
tree has — so driving a body from it would draw a deterministic first-edge
walk *as if* it were the game's behaviour.

If shoot mode is wanted, that decision is the thing to revisit first: it may
need the navigation data before it needs any code.

### 19. Does the original filter (anti-aliasing, …)? — research, fair evidence

Cheap to answer and worth answering early because it bounds what any later
render comparison can claim. Two threads already exist: the 1999 spec sheet
(`todo/engine-spec-1999.md`) lists *"Z-mapping (mapping exact)"*, which decodes
to `D3DRENDERSTATE_TEXTUREPERSPECTIVE`, and the renderstate census in that
audit shows which states the engine ever sets — filtering would be among them
if it were set at all.

`docs/PORTING.md` already rules that no pixel's VALUE is checkable against the
captures, so the likely honest outcome is "the engine sets these states, and
their effect has no reachable tier" — which is a real result and should be
recorded as one rather than left as a question.

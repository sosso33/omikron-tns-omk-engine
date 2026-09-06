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

### 1. Enter held for ~0.5 s counts as several presses — **FIXED 2026-09-06**

Was: one action per frame held (15 frames = 15 presses). Now one per
interaction, however long the button is down.

**It was never an input bug.** The port implements the engine's repeat mask
faithfully, the dialogue confirm already took edges, and the mask in the world
is correctly 0 - all three checked and all three innocent. The engine reads
DirectInput STATE and `sub_4A7A20` is a pure bit remap, so the ORIGINAL also
queues `MDACTION` every frame while Enter is held.

**The guard is `sub_465D30`'s tail**, `tab_special_move[3]` = 0x0046AEC0:

    SetPersoBankGroup(channel, Cef_FindGroupById(bank, dy <= 27.472441 ? 143 : 41))

It switches the actor into the take bank, where the same held bit means TAKE
rather than REACH — so the machine walks the interaction instead of repeating
the first step:

    MDACTION -> MDGETOBJ -> [H_WAITOB] -> MDPUTSNK -> MDSTAND -> (a new cycle)

Measured at area 237's zone 4051 with Enter held: 40 frames reaches and waits,
55 completes the take, 70+ completes it and begins a new cycle - which is what
holding SHOULD do. Press-release-press gives two activations. A press with
nothing in reach still retries every frame, which is also the engine (it never
reaches `sub_465D30`, so nothing switches).

Also ported: the handler's ACTOR_STATE switch, `byte_46B2BC[state-4] =
{0,4,4,4,4,4,4,1,4,2,3}` — states 4/11, 13 and 14 take other arms and are
refused with a log line — and the arm's own `ACTOR_STATE == 3` refusal.

**Still unported and labelled in the code**: the LOW arm. `sub_465D30` picks
group 143 when the object is within 27.472441 units (0.6978 m) of the actor's
height, and that needs the object's position which the press does not carry, so
a low object plays the standing take. And `sub_465D30`'s ADJUST - the walk to
0.600 m / 0.400 m with its 0.25 m settle and 1.25 m give-up - is not ported
either; the port acts where the player stands.

**Two false conclusions were published on the way, both from a broken
measurement**, and the tooling lesson is worth more than the fix. `--hold`
takes COMMA-separated runs and uses `+` only to join simultaneous keys within
one run: `k28*5+0*10+k28*60` parses as Enter for **five** frames. Every
"second press" test silently pressed nothing, which produced (a) "the bank
switch parks the player in H_WAITOB for ever" and (b) "the actor leaves
H_WAITOB only when the object is delivered". Both were wrong; the take
completes on the second press exactly as the `.CTL` says.

**Also reported (2026-09-06): the same repeat in SHOP SELLER dialogues.** Not
reproduced yet, and now worth re-testing against this fix - if that path also
runs through `MDACTION` it may already be gone.

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

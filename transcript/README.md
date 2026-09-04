# Session transcripts

The record of *how* each finding in `docs/` was reached, including the wrong
turns. Several of the ground rules in [`../CLAUDE.md`](../CLAUDE.md) §1 are only
convincing with the mistake that produced them attached, and this is where those
mistakes are.

Each session is kept twice:

| suffix | |
|---|---|
| `-raw.jsonl` | the log verbatim — full fidelity, every tool call and result |
| `.md` | readable render: every user message and reply in full, each tool call as one line |

The raw logs are **snapshots**. The live log under
`~/.claude/projects/…/<id>.jsonl` is append-only, so re-copying one later only
adds to it — the `756e33fa` snapshot was taken at 04:18 and its live file has
grown since.

## Sessions

| file | span | what it covers |
|---|---|---|
| `session-2026-08-26_756e33fa` | 2026-08-26 11:16 → 2026-08-27 12:41 | the cleaning pipeline over `Runtime.exe.c`; `IAM\DIALOG` structure; the VM opcode table; the CLI dialogue tester; `.3DM` morph and ADPCM audio; `.3DT` textures and `.3DO` meshes; the WebGL viewer; `.ani` / `.CTL`; the first camera mode |
| `session-2026-08-26_e087fc16` | 2026-08-26 21:47 → 2026-08-27 15:01 | how a conversation is launched (opcode 61); the `AREA` / `SCENE` / `GLOBAL` loaders; `CLAUDE.md` and the reconstruction plan; phases 0 and 1; the letterbox, handedness and speaker-placement fixes; `tools/verify.py`; 9 more opcodes named |
| `session-2026-08-27_347e86c4` | 2026-08-27 15:03 → 2026-08-28 10:55 | phase 1 closed (125 opcodes) and phase 2; **level A closed** — `.3DA`, `.3DP`, SCX chunk 10 camera editings, `.SFX`, `MAP2D`, `.WRE`, the prop-asset catalog, the interface text files; the `.CTL` format read end to end (transitions and the real key bindings, combat, effects, `tab_special_move`); the walker, `ACTOR_STATE` 0..17, the zone-script lifecycle, the event/message system, the dialogue UI machine, melee from setup to KO; the top of the program and the master clock; **phase 6 scoped** — the headless simulator, its reuse policy and the portability constraint. Also the long staging hunt: the scene-clip root found to be the pelvis, and the anchor **measured, implemented and reverted** when it floated the speaker mid-line |
| `session-2026-08-28_5b4133f8` | 2026-08-28 10:58 → 2026-08-28 23:16 | **phase 6 built and closed** — the game-state schema (`GAME_STATE.md`, `tools/gamestate.py`) and all five simulator stages; **the cutscenes**, both families, with `CUTSCENES.md`, the `/cutscene` viewer, its audio, and the transport/scrub sync; then **the golden-trace rig** (`tools/goldentrace.py`): the engine's own operand log captured through `GetPrivateProfileStringA` under CrossOver, with no shim, patch or debugger. Findings from two captures: the intro script in **AREA 118**, which no walk enumerates (recovering conversation 272 from the 106 with no launch path); the **Bowie cutscene agreeing 9/9** with `tools/sim` — the first proof the simulator *decides* what the engine decides; determinism bounded at 55/55 across launches; the save header's 3496 settled by a file the engine wrote; and opcode **47 `area.goto`** decoded as a transition carrying two scene objects. Also three corrections of the repo's own claims — the decompilation is of **`Runtime 2.exe`**, not `Runtime.exe`; `Script_StartScript` has **nine** call sites, not four (which had ruled out the whole non-`scx.play` search space); and the `+64` array is cameras, not scripts. And three matcher bugs that each reported the tool's limits as the game's |
| `session-2026-08-28_06ea1468` | 2026-08-28 23:19 → 2026-08-29 10:15 | **the simulator becomes the game, and the oracle grows to meet it.** Eight new phase-6 stages: the area load (`AREA +4` / `SCENE +4`), the conversation returning control, frame **pacing** through the `.wait` slots, the area transition, the player as a walking **actor**, the **narrow phase**, the area-142 crossing, and conversations **executing**. Findings behind them: the chunk **startup script at `+4`** — the table no walk enumerated, which closes *what starts a cutscene's beats* (SCENE 55's fires all sixteen Impasse beats in authored order); `dialog.start` is **not a suspend**, the whole pump is gated on `g_DialogState`; the `.wait` variants park a caller at status 4 until `ScriptObject_IsBusy` clears; `Walk_ClampNormal` is an **axis clamp**, not a slide; and the UI question/answer loop end to end (`ui.open` → status 6 → `Game_HandleEvent` case 5 → the 7-slot `UI_GridMenuInput`). `tools/chunkmap.py` accounts for **every byte** of all 330 world chunks (97 left, all bytecode). Three new captures — including the first past the opening (286 events) and the first with a **conversation** — took the oracle from 134 events to **475, zero disagreements**; the Telis capture then **broke** the dialogue model and proved the reply *menu* is observable. Also five opcodes found announcing a field the map did not expect, and `tools/vm_announce.py` written to close that class (49 handlers, 0 disagreements) |
| `session-2026-08-29_73fe463b` | 2026-08-29 10:24 → 2026-08-30 | **phase 4 closed: the whole route from a `.3DO` face to a pixel.** The **visible-set walk** and the **texture upload path** turned out to be one 14-bit number — the render bucket key, `meshState | textureSlot` — which answered the **Anekbah panel** question that five earlier sessions had left open: not depth, not draw order, but a **58-slot texture cache keyed on a 19-char filename**, with two decor sets resident at once and **182 names shipping different pixels**. Then the **lighting model** (sets are baked, in **colour**, not the monochrome both viewers drew), the **transparency** correction (additive, not 50% alpha — confirmed four independent ways), the **effect sprites** extracted whole from the `.SCX` stream, and the **ambient effects chain** read end to end — mesh flag → name → `.SFX` section D → section C → emitter → particles → the buckets, **321 emitters bound**, `neon` 102 — then **implemented in `/cutscene`**. `.SFX` section C went from "semantics not established" to fully decoded. **Most of the session's findings came from the user looking at it against the real game**: the ambient clock counts **frames, not seconds** (30x too slow); the camera **roll was mirrored**, because `W(v)=[x,-y,z]` is a reflection and a reflection reverses rotations; particles were wrapping at `n*step` instead of their lifetime; the flames were missing their **acceleration**; the flame **lean** is a per-emitter `rand()` jitter — and `srand(timeGetTime())` means the game's own leans differ every launch; and transparency needed a **global** draw queue, because the engine bins the whole frame into one bucket array. Also two **black screens shipped by me**, both from an edit script that mutated its buffer and died before writing — recorded in `CLAUDE.md` §5, with the rule that prevents it. Ends with the porting policy **refined**: "CLEAN before porting" is a rule about risk, so it does not bind where a corpus or the simulator can prove the port — which moves formats, kernels and the VM into reach and leaves the **no-harness UI** (70 functions, **56 RAW**, 5703 lines) as the real gate |

| `session-2026-08-30_15cf8d45` | 2026-08-30 | **phase 3 closed — the whole UI subsystem, then the interface put under the simulator and under the engine.** The I2D 2D layer, the 37-screen table, the widget tree, the glyph renderer and the 13 `.FNT` fonts, the input path, the per-screen open/close callbacks, `Game_HandleEvent`'s inventory channel, and the fight/shoot AI; then five panels walked by `tools/sim/ui.py` and a browser-driveable `/ui` tester with the game's own artwork, sprites and text. Ends with **menu golden traces**: `goldentrace` learned to press its own keys (`run --keys`), and the captures showed the start menu **never answering** — which reading the confirm path explained, `Confirmer` (0x0047A2B0) opening `test [dword_657994] / je` so an **empty name field** writes neither the answer nor the screen's close word. `sim/ui.py` had answered **1 unconditionally**. **Read this one for how many claims did not survive being run.** The user found four by looking: the hover sprites, the uncentred start-menu buttons (fixed by a *broadcast* the item records do not carry), the missing text, and — after a long wrong diagnosis of mine — *"it is just width/height issue of the img tag"*, one CSS selector too broad. Mine: a backtick inside a comment inside a template literal that emptied the cutscene list; a cache-busting scanner wrong twice; a `ui.open` corpus walk that found **241** sites and no start menu at all, because it missed the `+4` startup scripts — CLAUDE.md's own documented trap, walked into again; a documented walk-through saying **RIGHT** where the engine takes **DOWN**; and, in the menu captures themselves, a "the screen reopens" reading **over-claimed and retracted** — four identical runs gave 1, 2, 3 and 4 preambles, and the capture carries no unique anchor. A log-append theory was tested and **refuted** on the way. Also a real rig bug: a surviving game process keeps writing to a trace *after* `distil` rewrote it (`menu-noinput` distilled at 3 events and later read 39) |
| `session-2026-08-30_72bd3855` | 2026-08-30 15:51 → 2026-08-31 23:59 | **`engine/` goes from nineteen slices to a program that BOOTS.** Ported in one run: `FONTS/*.FNT` and the text LAYOUT, the message subscriptions, `IAM\OBJECT` and the combination table, the fight AI and the `.CTL` combat/transition half, the SCENE OBJECT interpreter *wired into the frame loop*, the save file and the 41-day calendar, the render DECISIONS (drawable mask, the 14-bit bucket key, the two blend modes, the 58-slot texture cache **run** rather than described), the visible-set walk and `sub_48D0D0`'s real frustum, the whole modelled UI surface (widget tree, LIFT grid, options tree, load panel, name field, IAM strings) and the inventory channel. Then **`tools/omk.cpp`**: it parses the command line the way `WinMain` does, steps the three FLIS movies, reads its start area from `IAM\START +1414`, and reproduces `traces/intro.log` **42 of 42** from a cold boot — with the SCRIPT opening its own menu, because `ui.open` now parks the context and the Session answers by walking screen 29. Also `docs/BOOT.md`; all five golden traces replayed in C++ (**1315 events, 112 anchors, 0 disagreements between the two implementations**), which then **closed one of the reference's six long-standing mismatches** and named the cause of two more; a sixth capture, `fight.log`; and `tables/ui_widgets.json`, an eighth compiled table |
| `session-2026-09-01_4f534c75` | 2026-08-31 22:02 → 2026-09-01 08:20 | **the port grows an oracle it did not have, and a standard to be judged against.** Five slices ported first — `ACTOR_STATE` 0..17 as a live machine with the `.CTL` channel under it, the shoot AI, the I2D 2D layer, and a **Makefile** that ended the ~1500-translation-unit rebuild (clean build 6 min → 10.8 s, no-op 0.027 s, which `verify.py` was paying once per engine check). Then the turn the session is named for: asked what to specify before the low-level half, the answer was **do the frame-capture spike first, because it decides everything else** — and it succeeded. `goldentrace.py capture` grabs the engine's own **framebuffer** through the existing CrossOver bottle, and the 2× Retina grab recovers to 640×480 **exactly** (0 of 307200 blocks non-uniform), so it **refuses** rather than degrades if a display ever interpolates. Three frames are committed, and the check needs no CrossOver: the file *is* the oracle. That moved the whole output half from *no anchor at all* to diffable, and [`docs/PORTING.md`](../docs/PORTING.md) was written on top of it — Part A the **target** (the user's call: software backend first, then Vulkan/MoltenVK, playability a goal, RGB565, dependencies only where no reference implementation is being replaced), Part B the six-tier ladder, the three-places declaration rule, and **every check must be SHOWN to fail**. What the frames then settled, none of which reasoning would have: the I2D **blits** 66560/66560 pixels of the menu title; the selection outline **1518/1518** and the connector **138/138**, which pinned the half-open `[left, right)` span the port had transcribed as a wart — *closing* it lights 20 pixels the engine never does; and `Text_DrawRun` at **6132/6132**, where the ramp's division by 31 **truncates** and rounding costs 95 pixels. Driver **mode 2** — the game's own `CONFIG` dialog names it *The Nomad Soul software render*, and its *640 × 480 × 16 bpp* row confirms the 565 framebuffer from a third source — gave the first direct evidence that the two back ends exist and **disagree** (text 1.000 IoU, outline 0.51 with 0/1089 values matching). Then the input path, ported and answering the start menu **by scancode**, and the ten shop titles. **Six documented readings were corrected by being run**: the I2D cache is a **head** cache (a tail cache scores 724/1407, and no screenshot could tell), the untested blit axis is **destination** Y not source Y, a primitive's third component is a **colour** not a z and the third argument a flags word, `UI.md`'s E/R interface defaults are `0x004C65B8`'s **static initialiser** which `Game_Init` overwrites before the first frame (ENTER and SPACE — no player saw E or R), the shoot AI's "no data at all" was about the *dispatch* (Gandhar plays three compiled behaviour scripts), and the connector "line" is a **quad** — the interface vocabulary has no line in it, so I2D's line and triangle can never be exercised by a screen. The shop lift was not a gap but actively **wrong** — all ten bound to string 19, because the linear scan that recovered the other 35 bindings walks through `Ui_OpenShop`'s jump table and keeps the last arm, so nine screens would have shown *Bibliothèque de Lahoreh* with nothing looking amiss. And `fr/SOUNDS/pluie.wav` joins the shipped-and-unreachable list: the image carries exactly two `.wav` path forms and neither can build it, while the name itself is a **particle effect** in `hamesta.Sfx`. **Read this one for falsification as a practice**: every slice was checked by breaking it first, and that found a **circular** dispatch check that passed while testing nothing, a driver bug that ran all three Gandhar bands at hp=200, a 147-miss ordering shape that was real, and a mutation that moved *nothing* because `rm` on an object file is not enough to relink — the worst direction for a falsification test to be wrong in, now `PORTING` B4. Three claims were withdrawn mid-slice for want of a discriminating mutation. Ends with a **macOS Files-and-Folders denial** cutting off the whole repo tree mid-session — every path under `~/Documents/omk` returning `Operation not permitted`, which is why this session's own transcript was first written to a stopgap folder outside it |
| `session-2026-09-01_02d92b54` | 2026-09-01 08:21 → 16:17 | **the port stops emitting numbers and starts drawing.** Six slices: the AUDIO path - and the finding is that there is nothing to port, `Sound_Init` hands a DirectSound primary to the OS and the engine's half is DECISIONS, so B6's "the mix compared offline" was asking for something that never existed; the ANSWER layer, one global with 17 writers, of which only 3 sit in a function IDA labelled; the SDL frontend, presenting the ported framebuffer **byte-identically, 614400 of 614400**, which is A8 rule 3 measured instead of promised; **pl_mpeg vendored, so the intro movies play with sound**; the **software 3D rasterizer**, whose projection agrees with `camshot.py` on 106 of 106 corners at 0.0018 px; and **Anekbah RENDERED** - the repo's oldest 'falsifiable by playing' claim - where the mechanism is confirmed (arriving from `AToit` moves 127919 pixels, 4253 visibly) and the attribution narrowed (`BATITR12`'s own pixels move 0 visibly). Two VM opcodes named on the way, 150/151 `render.grey.on`/`.off`: **the game has black-and-white cutscenes**, predicted from a luma conversion in one of two otherwise-identical bucket walks and then confirmed by the user, who had played them. **Read this one for how often a claim was wrong in the direction of the tooling rather than the data.** "The movies have no audio" was pl_mpeg's default probe window stopping short of the first audio packet at offset 160368 - the user heard otherwise and was right. "The six rasterizers" named nothing: there are two, both 2D, both already ported, and no 3D one at all - which then over-closed a B6 row whose criterion was still live. A projection differential that agreed on 74 of 106 corners was agreeing only where both sides did nothing. A sign mask built on a depth tie reported two DIFFERENT textures moving exactly 531 pixels. And a check asserted `centred`, a count of flags, which stays 4 whether or not anything moved. Every one was caught by a number that could not be true, and several only after the mutation that should have failed did not. **Ends in four hours of lost filesystem access**, which is the other lesson: `~/Documents` went `Operation not permitted` mid-session and I offered three causes - a triggered prompt, a stale process, an auto-update - before reading the system log, which said immediately that TCC had granted Desktop, Downloads, FDA and Automation and never Documents. The decisive fact was in hand the whole time and unnoticed: the PID and start time never changed across every "restart", so the process being asked to restart was self-evidently the one still answering |
| `session-2026-09-04_c8cefe13` | 2026-09-03 22:23 → 2026-09-04 14:02 | **the sneak becomes a device you can use, and the object flow reaches the world.** Kay'l's handheld was built, coloured and navigable and did nothing; by the end a player uses the apartment key at the lift and arrives in the apartment. The chain was four links and the port had two: `Game_HandleEvent` case 35's non-consumable arm puts the object IN THE HAND and out of the bag, and `Script_Pump` case 2 does not queue an activate blind while something is held - `sub_406180` runs the script through `Script_RunToOpcode75` and queues only if it reaches opcode **75**, `var.set.used_object`. The port answered *no to everything*, so a full hand suppressed every activate in the game. Of AREA 229's four activate scripts exactly **two** reach 75, which is what makes the probe mean something. **It is not message 20**: GLOBAL's handler there is a per-object dispatch on POTIONS and no SCENE or AREA subscribes to 20 at all. Then `Utiliser` closing the sneak (`sub_49BEA0` writes the state word to 3), the row window (`sub_42AFF0` is a CENTRED scroll - the cursor moves to the middle widget, then the window moves under it), and `Utiliser sur` turning out to be a MODE rather than a use. **Read this one for how a reading stops at the first plausible place**: `Inventory_Insert` was gated from its first 45 lines, with a green check beside it, and the other hundred say kinds 12/13 skip the ladder entirely and are CONSUMED while 2..11 with no matching row still earn a row - found by a PEER SESSION's play-test, not by the suite. Two dead arms in one table: the six gate-8 recipes cannot fire (already known) and a combine begun with object 330 sets the gate to 1, which matches nothing. And the world's action button moved off the input edge onto `MDACTION`, because `Game_RaiseEvent(6, 4)`'s three sites are all ACTOR STATE handlers - the ENTER that confirms a verb is still held when the sneak closes, so the press at the lift was being swallowed and the player spent a second key on it |
| `session-2026-09-04_99f20e0e` | 2026-09-04 10:50 → 14:46 | **the tail of the session above, after a continuation onto another machine** - the two logs overlap because the first kept being written to. Three things worth the read. A **regression shipped and reported within minutes**: the combine mode was a one-way door, because `beginCombine` set a flag that only a COMPLETED combine cleared, and it lives in the same static record as the selections, so an abandoned `Utiliser sur` survived closing the device and every later row confirm fed the dead mode instead of opening the verbs - the player lost the verb bar entirely. The engine does not have that problem because `sub_49B8A0`, the verb panel's LEAVE hook, cancels it; the port had modelled that hook's four flag writes and stopped one line short of its tail. A **question that dissolved on being read**: 'which list do the bio, statistics and memo pages ask for' - only three panels carry the row list at all, and the memory page's count `dword_4DE708` has seven reads and ZERO writes in the whole image, so that page is built and permanently empty like the options menu's page 12, and the port was already right. And **two player captures** settling the identity page, whose structure turns out to be entirely in the widget tree already. Also the session where the two-session protocol was agreed after `git add -A` swept a peer's work twice: ask per FILE, say when you are done with it, stage explicit paths. Ends in the handover itself: `todo/HANDOFF.md`, both sessions' transcripts archived and indexed, and every branch pushed - including the peer's `take-height`, which was the only one holding unpushed work |
| `session-2026-09-04_9911109c` | 2026-09-04 14:34 → 14:38 | **the TAKE animation — `omk-play` 69, run in parallel with the two sessions above and on its own branch.** *Row written from the session's own ten commit subjects on `take-height` and its reports to the sneak session, not from reading the log; the session that produced it should replace this with its own account.* The reported symptom was that picking an object up 'plays the complete list of grabbing object animations'. Three readings were tried and the first two were withdrawn by their own author — the group pick by object HEIGHT (`sub_465D30`: 41 `H_TAKL` below 27.472441, else 143 `H_TAKH`) is a real fix but not this bug, and an `H_WAITOB` theory was refuted an hour later by a corrected trace showing the channel DOES sit in the wait for 234 ticks. The answer is that **one clip holds every grab variant and the engine plays a frame RANGE**: `len = (clipFrames - n + 1)/n + 1`, with `H_TAKH12` 189 frames = 9 x 21 and `H_TAKL12` 126 = 6 x 21, the count `n` coming from the `.CTL` entry's own TOP NIBBLE — which partitions the bank exactly, nine states above 1 and they are precisely the take/put families plus `H_ADJSTP`. Predicted from the arithmetic and then confirmed INDEPENDENTLY in the data by plotting pose excursion. `n * len` landing on frames+1 rather than frames looks like an off-by-one refutation and is the corroboration, because key 0 is the rest sentinel. Then the blend (`sub_4725B0`, bilinear over the grid) and the second stage: the take is TWO stages, `MDACTION` installing group 600 `H_ADJSTP` to step into position and `MDADJSTP` doing the take at a TIGHTER 120 cm reach, so an object can pass the first test and fail the second. **Read the two withdrawals**: both were called done at a working demo, and both times what caught them was the user playing it |
The first two overlap in time — the second is a continuation of the first after
a context compaction, and the first's log kept being written to afterwards. The
third continues from the second the same way; it was itself compacted once
partway through, so its own early turns are a summary in the live log. The
fourth continues from the third (10:55 → 10:58) and was likewise compacted once.

The fifth continues from the fourth (23:16 → 23:19). Its live log was forked
partway through by a dropped connection, leaving a second id (`6a67890f`)
holding the same session truncated at 06:45; `06ea1468` spans the whole thing
and supersedes it.

The fourth is the one to read for the **method** rather than the findings: it is
mostly a record of claims in this repo being checked against the binary and not
surviving, and of a new instrument being built and then repeatedly reporting its
own defects as the game's. `CLAUDE.md` §1's rule about IDA's `proc` boundaries
bit twice more in it — once in a caller count that had closed off an entire
search space.

The fifth is the one to read for **how a negative result fails**. Three of its
findings exist because something contradicted a confident claim: the `+4`
scripts (the golden trace kept announcing scenes *no slot could emit*), the
reply menu (no walk of the graph reproduced a captured conversation), and the
`OBJECTS/314` residue (a fifth mis-mapped announce field, in a script the corpus
had held all along). It also contains two of its own wrong explanations, both
recorded next to their corrections: a `SCENE 63` "second site" that was a byte
pattern inside a camera table, and a `wide`/`tight` index diagnosis that matched
a documented failure mode perfectly and was false. The lesson each time is the
same one §1 already states — a negative result over a corpus is only as strong
as the enumeration behind it, and a plausible cause is still a hypothesis.

The eighth is the largest, and it is the one to read for **how a claim written
beside a passing test survives being wrong**. Every incorrect statement in it
sat next to green checks: a coverage table with a count that had quietly
dropped the rows it judged unportable, and later a count simply left stale; a
`loadArea(118)` literal sitting under a paragraph asserting nothing was
hand-wired; `aventure.scx` described as the main menu, inferred from the
`Game_Start` **call** without opening the **file** (it is the global effect and
sound library — 20 sprites, 53 sounds, no menu logic at all); and "no world
script opens screen 29", which was really "no script I enumerate", because the
op scan walked the zone records and the second table and not the `+4` startup
scripts — §6's own lesson, in the session that ported those scripts. The menu
is opened at pc 1078 of AREA 118's startup script.

Two things went the other way and are worth the same attention. Porting the
golden-trace diff to C++ made the **port disagree with the reference**, and the
disagreement was the reference's: modelling `ui.open`'s park closes
`AREA 157 rec 60 +4`, which opens the LIFT at instruction 3 of 37 and branches
on the floor, so the old replay predicted both arms. And a capture taken to
give the actor runtime an oracle proved **it cannot have one** — combat's only
two opcodes are `fight.begin`, which announces nothing, and `player.become`,
which announces to `CHARACTERS`, a domain the logger filters out itself. The
capture reached combat (32 of its anchored scripts carry `fight.begin`); the
silence is the mechanism. That check should have been made before anyone was
asked to play.

Three `str.replace` edits also no-opped silently for want of the `assert
s.count(old) == 1` that §5 exists to require — twice leaving a header without
its declaration, once leaving a check registered without its function.

The tenth continues from the ninth, and **forked the same way** — at 14:06,
leaving `4b50966c` with 1162 records where `02d92b54` has 3116. That is now
three sessions in a row that have forked mid-run, so it is a property of the
tooling rather than an accident: always compare record counts before choosing
which id to archive.

It is the one to read for **how a tool's default becomes a claim about the
data**. Four of its corrections are that shape, not one: pl_mpeg reporting no
audio because its probe stops before offset 160368; "the six rasterizers"
naming functions that do not exist; a projection differential agreeing on the
74 corners where both implementations did nothing; and a sign mask built on a
depth tie that had two different textures moving exactly 531 pixels. Each was
caught by a number that could not be true — and in two cases only because a
mutation that *should* have failed did not, which is `PORTING` B4 earning its
place twice in one session.

It also ends with the **first 3D captures** — six frames of dialog 402, taken
with a rig that had to learn not to raise the window, because the game crashes
on a focus change for this player. The shot was picked so the camera is known
rather than reconstructed, and the timing came out of the data: `lineCamera`
cuts, `lineCamera2` is travelled to over 160 frames, and the line is
audio-timed at 11.6 s, so 4555 is parked for about six seconds near the end.
Two of the six landed there.

The other half is a four-hour loss of filesystem access before that, and it is
worth reading as a failure of attention rather than of permissions. I offered
three causes — a prompt my own automation triggered, a stale process, a
mid-session auto-update — before running `log show --predicate 'process ==
"tccd"'`, which said in one line that macOS had granted Desktop, Downloads,
Full Disk Access and Automation, and never Documents. The decisive fact was
visible from the first check and went unremarked for a dozen turns: the PID and
start time never changed, so the process I kept asking the user to restart was,
self-evidently, the one still answering them.

The ninth continues from the eighth. Its live log **forked** at 03:30, the same
way the fifth's did, leaving a second id (`751f0901`) that runs to 07:41;
`4f534c75` spans the whole session to 08:20 and is the snapshot kept. The fork
holds one user turn `4f534c75` does not (`"done, resolution is back to
640x480"`), so it is superseded rather than strictly contained.

It is the one to read for **what changes when a new instrument arrives**. Every
earlier session's ceiling for the output half was transcription; one spike —
proposed, costed at an hour and taken before writing the specification that
depended on it — produced a bit-exact frame oracle, and the specification then
said something it could not have said that morning. Four documented readings
fell to it within the day, and each had been *consistent with everything this
repo could compute*: a tail cache that scores 724/1407, a source-Y test that is
really destination-Y, a rounding division that truncates, and a "wart" whose
tidy-up lights 20 pixels the engine never does. §1's rule that a suite comparing
this repo to itself cannot see a wrong reading applied consistently is the whole
session in one line.

Its counter-lesson is in the falsification record, and it is the sharper half: a
mutation that moves **nothing** is not evidence the check is weak. `rm` on an
object file left `make` relinking nothing — same second, same binary — so the
first attempt to break `engine: input` looked like a check worth deleting. A
forced relink moved it 18/19 → 28/57. That is now `PORTING` B4, because the
tidy response to a harmless-looking mutation is to remove a working test.

## Re-rendering

```bash
python3 tools/transcript.py transcript/<name>-raw.jsonl transcript/<name>.md
```

Add `--thinking` to keep the reasoning blocks or `--full` to keep tool output.

## Naming

`session-<start date>_<short session id>`. The date orders them; the id is what
actually disambiguates, since a session can span midnight and two can run at
once. The id is the name of the live log the snapshot came from.

The seventh continues from the sixth. It is the one to read for **the gap
between reading a thing and running it**: almost every correction in it came
from something being exercised — by the user looking at a rendered page, or by
the engine being asked a question through a scripted keypress — and not from
re-reading the listing that had already been read. Its last finding is the
shape of the whole session in miniature: the simulator had a start-menu answer
transcribed correctly out of the binary, and still wrong, because the
instruction that *gates* it was four bytes above the one that had been read.

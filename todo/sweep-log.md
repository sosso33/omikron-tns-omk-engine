# The full-sweep counter

## 2026-09-22: the RED CHECKS, triaged

The reader asked for the standing reds to be cleared before anything else.
The fast list is **228 checks and ONE failed**; the rest were `engine:` checks,
run by name. What they turned out to be:

| check | verdict |
|---|---|
| `licence headers` | 470 -> **474**: `src/ui/overlay.h`, `tools/body_tie.cpp`, `scripts/vita-vitagl-patch.py`, `scripts/play-golden.sh`, all named in its docstring |
| `play usage` | FIXED: five flags the usage block never named (`--fight-health`, `--rings`, `--water-cam`, and today's `--thread-bodies` / `--no-thread-bodies`) |
| `engine: ui scaling` | **the check was wrong, the code right**: its fourth source test counted the composer's blits (`== 5`) and a correct sixth arrived with the city map and the hint shop. It compares the blits that carry the filter with the blits there are, and says `N of M` rather than `False` |
| `engine: UI`, `ui item bindings` | the merged widget table's two new panels - a census step, re-baselined with the numbers attributed. `resolvable`/`ok` did not move, so every resolvable binding still resolves |
| `engine: UI`'s `disagree` | was **7**, and NOT a census step: the TERMINAL FAMILY (5, 11, 15..19), whose keypad hook the port models and `tools/sim/ui.py` does not. The keypad is eleven items three wide, so DOWN the first column is 0, 3, 6, 9, 10 - what the port reports. Named and asserted by its exact pair, as screens 7 and 9 already were |
| `sneak previews`, `ui geometry`, `engine: player jump` | GREEN on their own; the sweep log's notes about them are history |
| `engine: shoot hit` | **still red, deliberately** (`todo/shoot-patrol.md`: baselining its empty lists would be worse than the red) |

So the only red left is the one that is red on purpose.


`python3 tools/verify.py` (the whole sweep) takes upwards of half an hour —
longer than most tasks it validates. So it is **not** run per task. The rule
(CLAUDE.md §4, set by the reader on 2026-09-07):

* per task, run `verify.py --only "<name>" ...` for the checks the change could
  plausibly break, and nothing else;
* run the full sweep after **5 to 10 finished tasks**;
* record it here, so the count survives between sessions.

Increment `tasks since` when a task is finished; reset it to 0 and add a row
when a full sweep is run.

**A TASK IS ONE THING THE READER ASKED FOR, not a commit and not a step inside
one** (their correction, 2026-09-07: *"I said 5-10 tasks, not 5-10 steps"*).
Porting `Text_LayOutBlock` was one task over three steps and two commits; the
slider is one task over a five-step plan file and four commits so far. Each
adds **one**. Counting commits ran this number to 13 when about nine tasks had
been finished, which would have called for a half-hour sweep long before the
rule intends one.

**tasks since the last full sweep: 11** (2026-09-22: THE VITA CITY - G6 steps 3-4 (one task, the reader's "do G6"), the precompiled shaders (one task; cache made from a console run, the no-compiler start untested), and the city freeze/lag, NOT finished: the music streamed, the tie patches in place, the delta clamp, the body tie replayed, far bodies unskinned, P2 applied - verified with `--only` over `engine: body tie` (new), `tie equivalence`, `pose equivalence` (re-baselined 224), `street frame`, `city crowd`, `character shadow`, `fitted shadows`, `mapped shadows`, `mesh name index`.) (2026-09-18: THE PS VITA PORT, started - `todo/vita-port.md`, one commit: the GLES2 backend and its probe, the device bench, the pad map, the VitaSDK build (`make vita`), and `todo/play-split.md`. Three portability fixes in shared sources (`<cmath>` in `actor/walk.cpp` and `script/area.cpp`; `threads.cpp`'s Vita worker). Three new checks, `engine: vita bench`, `engine: gles backend`, `engine: vita build`, each shown to fail. **`licence headers` was ALREADY red** (452 tracked against 443) and is re-baselined to 458 WITH the attribution in its docstring - 9 earlier files, 6 of this task's. Verified with `--only` over those four checks; `thread_probe` 0 mismatches for the touched `threads.cpp`.) (2026-09-18: FINISHING THE UI - one task of the reader's, five separate slices, and the census notes of the two that regenerated the widget table are BOTH below because BOTH landed. **The merged counts are neither branch's**: `ui geometry` is re-baselined once, to 728/728/727/59, and `tools/exetables.py`'s own self-checks to 29 child panels / 176 lists / 728 items / 105 items naming a child / 25 distinct such records / 10 panels with no tile array / 113 lists with no hook - each re-derived from the regenerated table with both new panels in it, not summed by hand. `engine: UI`, `ui item bindings` and `licence headers` were ALREADY red before any of this and are deliberately left alone; `sneak previews` was a fourth already-red one and is now FIXED - see the commit, it was this repo's own undefined-behaviour regression from `1f3e792`.) (2026-09-18: `Lire plan`, THE SNEAK'S CITY MAP - `docs/UI.md` 3g-bis, one commit. New check `engine: sneak map`, shown to fail. It REGENERATED `tables/ui_widgets.json` (+1 panel, +1 list, +3 items for `0x004DF190`), so `ui geometry` and `engine: UI` both move by exactly that much. **`ui geometry` was GREEN and this made it red, so it IS re-baselined** (718/718/717/58 -> 721/721/720/59, the cause written into its docstring); **`engine: UI` was ALREADY red** for the census drift the notes below describe - it reads 58/170/718 against a baseline of 57/166/698 - so it is deliberately left alone and now reads 59/171/721. `licence headers` was also already red, at 449 against its baseline of 443 (six files from earlier sessions), and the two new `ui/citymap.*` take it to 451; left alone for the same reason. `exe tables` gained `city_maps` in its expected list. **`sneak previews` is a FOURTH already-red one, found on the way and ATTRIBUTED rather than argued**: it reads `0 drawn, slots 0 0 0` where it wants `3, True`, and it reads the same against the PREVIOUS `ui_widgets.json` *and* with the previous composer sources checked out - two independent axes, neither of them this task's - so the three 50x50 3D previews stopped drawing before 2026-09-18. Left red, with the attribution written here so the next owner does not bisect for it; the two cheap tests are running `build/sneak_colour <data> <an older ui_widgets.json> <ui.json>`, and checking the previous `engine/src/ui/screendraw.*` and `engine/src/ui/widgets.*` out before a rebuild. Verified with `--only` over `engine: sneak map`, `engine: sneak memos`, `engine: screen`, `ui page` and `sim: ui coverage`.) (2026-09-17: THE VITA PORT, item 1 of `todo/handoff-vita.md` §2 - `omk::MeshNameIndex`, one commit, `optimization.md` step 13. Verified with `--only` over `engine: mesh name index` (new, shown to fail four ways) and the three shadow families, which cannot move because the index has **no consumer**: `play.cpp` was held by another session in the same tree, so the adoption is handed over in `pending/vita-meshidx-playcpp.md` and the frame is not faster yet.) (2026-09-17: SWIMMING - the reader's own

(2026-09-18: `Indices`, THE HINT SHOP on screen 30 - `todo/pending/ui-remainder-survey.md` 2a, one commit. One new check, `engine: hint shop`, shown to fail. It also touched shared code every screen walks - `ScreenComposer` gained a run-time SECTION index and `ScreenFrame::itemText`, and `tables/ui_widgets.json` was regenerated with a new CODE_NAMED panel - so `ui geometry` and `engine: UI`, which were ALREADY red for census drift, move again by +1 panel / +5 lists / +7 items. **`ui geometry` WAS GREEN and this task broke it, so it IS re-baselined** -
718/718/717/58 -> 725/725/724/59, every one of the four accounted for by that
one panel and its seven widgets, with the map counts and all three behavioural
elements unmoved. **`ui item bindings` was ALREADY red and is NOT**: measured
against the committed table it read (45, 25, 32, 32, 13) against a baseline of
(42, 24, 32, 32, 10) BEFORE this task - three string binds, one tag and three
`nofile` ahead, from the shops', MULTIPLAN's and the memo reader's own
CODE_NAMED additions that nothing re-ran. This task adds exactly **one** of
each: the confirm's copy of the footer item 0x004E2CF0 (string 6) and its copy
of the body box 0x004E2B10 (the tag), so it now reads (46, 26, 32, 32, 14).
Re-baselining it would bake in three bindings this task cannot account for.
**`engine: UI`** was red before this task too and moves by the same census:
+1 panel, +5 lists, +7 items, +1 list with a hook, +4 without. Its last
element - the port-vs-simulator DISAGREEMENT count, which is what the check
exists for - is 0 and does not move. The numbers this task added are recorded
in `tools/exetables.py`'s own comments.) (2026-09-17: THE VITA PORT, item 1 of `todo/handoff-vita.md` §2 - `omk::MeshNameIndex`, one commit, `optimization.md` step 13. Verified with `--only` over `engine: mesh name index` (new, shown to fail four ways) and the three shadow families, which cannot move because the index has **no consumer**: `play.cpp` was held by another session in the same tree, so the adoption is handed over in `pending/vita-meshidx-playcpp.md` and the frame is not faster yet.) (2026-09-17: SWIMMING - the reader's own
item, `todo/swimming.md`, steps 0 to 5 in six commits: the entry, the water
moves, the motion `sub_4A8F30`, the pitch on the drawn body, the breath gauge
(`Hud_DrawBar` mode 1, the port's last unported arm of it) and the swim itself.
One check carries all of it, `engine: water entry`, re-baselined three times and
mutation-shown each time; the play test is not yet done. It also changed code
every DRAWN BODY goes through - `rotateEuler` where the draw used `rotateYaw` -
so the next sweep is the first to run the suite over that.) (2026-09-17: FALLS
AND HEALTH - next-tasks 14+15, one commit, `todo/falls.md`.) (2026-09-17: **CORRECTED BACK TO 3 FROM 8.** A session spent entirely on MELEE counted each fault a play test turned up as its own task - the stuck camera, the frozen pose, the black bars, the throw camera, the fighters standing inside each other - and ran this number from 3 to 8. The reader: *"the task here is implementing the fight mode, so since the start of the session, 0 task is done."* Every one of those was a STEP inside one unfinished task. A fault found inside the feature being built is not a new task, it is the feature not working yet, so the counter does not move until melee is something the reader would call done. The commits are real and are listed in `todo/fight-mode.md` 15; what was wrong was the accounting. **Four checks added in that session are newer than every sweep on record - `engine: bank swap`, `engine: hold release`, `engine: fight letterbox`, `engine: fight separation` - and one row was re-baselined (`fight letterbox`'s row-240 guard, 635 -> 640).**) (2026-09-15: THE SNEAK'S EXAMINER MESSAGE - message 4 on entering the sneak's examine page, one commit; verified with `--only` over the sneak and multiplan families.) (2026-09-15: MULTIPLAN - next-tasks 12, `todo/multiplan.md`, six steps from `42e06a9` to `921fa83`, confirmed in play by the reader. Verified per step with `--only` over the multiplan, interference, I2D, shop, sneak, used-object and screen-close families; it also changed shared code - the composer's examine box now keyed on draw hook `0x004780A0`, the I2D pool flag and census, and `Session::setDataRoot` running without a crowd - so the next sweep is the first to run the whole suite over those.) (2026-09-15: THE SHOPS - next-tasks 11, `todo/shops.md`, six steps and ~20 commits from `41e9bef` to `c30b6e4`, confirmed in play by the reader. Verified per step with `--only` over the shop, UI, sneak, screen-close, pause and simulator families; it also changed code every screen walks - the dispatch's TAB/BACK gates, `UiWidgets::at`'s child merge, the 24-bit sheet loader, `Ui_DrawPanelDim` - so the next sweep is the first to run the whole suite over them.) (reset 2026-09-14 by the `--slow` sweep recorded in the table below, which measured `004d28f`. The twelve it covered are listed next, then the optimization work of 2026-09-13/14 - `todo/optimization.md` steps 2 to 9 and 4b - which this file never counted. History before this reset: ) (2026-09-13: THE RELEASED SPECTRES - `todo/released-spectres.md`: their attack left UNRESOLVED after every data-side lead closed; ported on the way the close attack (dogs bite, robbers strike) and the landing messages, GLOBAL's fall damage; the shoot family 31/32, only `shoot hit` red. Earlier: 2026-09-13: GUNMEN SEEING THROUGH WALLS - `todo/shoot-sight.md`, seven steps: the grid sight, the ray, the spectre's and the watcher's arms, the rays' two mesh skips; the shoot family 30/31 with only `shoot hit` red, as before. Earlier: 2026-09-13: THE FROZEN SHOOT CAMERA - from a real save the phase began on the airlock cutscene's fixed camera; the viewer read a resident absolute camera as newly named. Reproduced on the real path with a new `--scene-load` harness, mutation-shown, confirmed in play. The reader will run the full `--slow` sweep themselves before sleeping; the shoot family runs now.) (2026-09-13: THE SUPERMARKET'S WAY OUT - a reader's stalled exit, looping music and T-posed body after a death; the exit and the music were the `--scene-chunk` harness skipping AREA 231's record 1, proved with a new `--zone-disable` harness walked out to Anekbah; no check moved, since the change is a harness and the Session method it calls.) (2026-09-12: THREE RED SHOOT CHECKS - `shoot brain`, `entrance` and `noise`, red since the patrol and never run by it; bracketed to `5019db7` / `5019db7` / `d8f9eb4`, one real defect fixed (a pending gunman's brain ticked before his entry action), `entrance` kept alive on its route, `noise` narrowed to 240, `brain` re-baselined with record 15's delayed action explained. See todo/shoot-patrol.md 8.) (2026-09-12: THE PAUSE - the gunmen's brain and turn clips ran on a literal 1.0 instead of the frame delta the pause zeroes; verified with a paused supermarket run, 6 shots behind the menu before and 0 after, and `--only` over the shoot family.) (2026-09-12: THE SLIDE - a reader's "I just went through the ground", which was `fc34909`'s own doing meeting the port's two-soup split. `engine: walker falls` gained a fourth column - directions still sliding after ten seconds, asserted at 0 - and `stuck_probe` gained the counters behind it; the three old counts did not move. Verified with `--only` over `engine: walk`, `engine: walker falls`, `engine: airlock walk` and `player vertical`.) (2026-09-12: THE NAV EDGE - five steps, five commits, one task. It renamed the `.mpt`'s "wall segments" to the inter-floor LINKS they are, ported `sub_436BB0` and the engage's cross-floor arm, and re-baselined `engine: shoot patrol` on better numbers. Two of its checks were found WEAK rather than wrong - see `todo/shoot-navedge.md` 5 and 7 - and `patrol walk:` turned out never to have been wired into `shoot fire` at all, so the sweep this counter is waiting for will be the first to run it.) (2026-09-12: THE PATROL - `shoot.actor.action`'s commonest action, seven steps and seven commits, and one task by this file's own rule. It also re-baselined `engine: shoot fire`, `engine: shoot gunfire` and `engine: shoot patrol`, and **left `engine: shoot hit` RED on purpose** - see `todo/shoot-patrol.md`, which says why baselining its empty lists would be worse than the red. A `--slow` sweep was started on 2026-09-12 and STOPPED by the reader within a minute - it was begun on a misread instruction, not on their go, and is still owed.) (unchanged 2026-09-12: the HURT REACTION `sub_47D1F0` is shoot mode's §8 item 1, the same ONE task, and was verified with `--only "shoot fire"` plus a mutation shown to turn its new `hurt shove:` element red) (unchanged 2026-09-11: the gunmen's fire, turn, aim, guns, fall, wall test, WALK and body COLLIDER are all shoot mode's, ~15 more commits, each verified with `--only` over the shoot family, `shoot fire` and `crowd push` - the owed `--slow` sweep grows with them and still waits on the reader's go) (shoot mode is ONE task and has now run to nine steps and ~23 commits - the shot and the flight, `cc3d1f9` and `96fab56`, landed 2026-09-10 and were verified with `--only` over the shoot family and the three SFX checks, so the owed sweep now also carries two checks no sweep has ever run, `shoot fire` and `engine: shoot fire`; a sweep is OWED and is waiting on the reader's go, since their rule is that the suite catches what nobody can see rather than measuring code mid-repair - the shoot camera has now settled and been confirmed in play, so the moment has arrived) (unchanged 2026-09-09 by shoot mode steps 5 and 6 - the SAME task by this file's own rule, now at six steps and seven commits. **Two checks are newer than every sweep on record and have never been swept**: `map2d sight` and, from the shop-door work, the five the handoff already lists.) (shoot mode - next-tasks 18, steps 1 and 2, ONE task by this file's own rule - and the shop doors that would not open, omk-play 94 - it landed WHILE the second sweep ran and is NOT covered by it. That sweep, 384 checks 0 failed, measured `f92231a` and covered the cupboard's stray voice line omk-play 92, the sweep's own triage, and the two red checks that triage left; reset 2026-09-09 by the `--slow` sweep recorded below; the fourteen it covered were the shadows' orphan fix, fitted, mapped, per-pixel lighting, the shimmer, supersampling, the dither, the player's vertical in three steps, the slider-door check's parse, the door read as a cutscene, the turned lift paths, the stale `engine: scene steps` expectation, and the cupboard take), the slider-door check's parse, the door read as a cutscene (omk-play 89), the turned lift paths (omk-play 90), the stale `engine: scene steps` expectation, the cupboard take's flat reach (omk-play 91), and the cupboard's stray voice line (omk-play 92, investigated and left open). **The counter had run to 19 and that was WRONG**: the reader ran a sweep more recently than this file recorded (their word, 2026-09-09). The lesson is this file's own - a counter only survives between sessions if everyone who runs a sweep writes it down, and a session that trusts a stale number spends half an hour proving nothing.

> **ALL SIXTEEN CLOSED, 2026-09-09.** Three were genuine port faults and
> thirteen were checks or probes that had drifted from correct code. The three:
> the sneak SLIDER PAGE's navigation (`UiWidgets::at` took a `current` written
> by another screen's open callback, so the player could not leave the tab
> column), the nine-screen settle rule `tools/sim/ui.py` never got, and the
> door/press work that started the day. **Two that were filed as port faults
> were not**: `fill colour` was the probe composing the fill over the start
> menu's animated cloud, and `engine: renderer`'s A2 violation - a boundary
> apparently moving 83629 pixels - was the probe dithering one side of its own
> comparison. Both were argued from facts that were individually true, which
> is the caution worth keeping: *the port is wrong* is the expensive
> conclusion and needs the same standard of evidence as any other.
>
> Six moved no expectation at all (the dither family, `fill colour`,
> `path form`, `engine fog`, `engine: pose`, `engine: renderer`); five were
> re-baselined against a deliberate change with the cause named; and one -
> `line facing` - moved its WINDOW rather than its assertion, because
> accepting the new value would have discarded what the check was written to
> catch.
>
> **And a fix can turn passing checks red.** Repairing the slider page's entry
> broke four slider harnesses that had been green on the broken navigation:
> every one of them walked that page through the same wrong door, so the
> family agreed with itself and asserted nothing. Only running the neighbours
> caught it.

> **THE 2026-09-09 SWEEP, ATTRIBUTED - and two ways a check can lie about being
> green.** 16 failures, and the useful part is where they came from.
>
> **13 were already red**, none of them ever run: they are all `SLOW`, and the
> two sweeps recorded above were the fast list. `engine fog` (one colour level,
> 24 against 32), `engine: pose`, `engine: walk`, `engine: walker falls`,
> `engine: I2D`, `ui geometry`, `path form` (the flat's greeting beat does not
> start at all, so `Gunbl` never moves), `fill colour`, `sneak page colour`,
> `engine: renderer`, `engine: UI`, and - after the trap below - `engine:
> player program` and `line facing`.
>
> **1 was an UNCOMMITTED local edit.** `engine/tools/mesh_list.cpp` in the
> working tree moves the `id` column to the end of the line, and `engine:
> slider door` parsed that tool's output BY POSITION: zero rows matched, so it
> reported the four door meshes as their own roots instead of `SlBassin`. The
> check's own comment records the same thing happening in the other direction
> at `fc2b8d0`. Fixed by finding the fields by their LABELS - `\bid\s+`,
> `\bparent\s+`, and the token in front of `flags` - so it holds under either
> layout. **A check must not be coupled to a debug tool's printf order.**
>
> **2 were this session's**, both mechanical: `licence headers` (394 -> 395,
> the new `path_turn.cpp`) and `INDEX.md fresh`.
>
> **TRAP 1: the obvious fix for `INDEX.md fresh` destroys information.**
> Running `tools/index.py` would have committed the LOSS of six named
> functions - `Shadow_EmitBoneBlob`, `Actor_DrawShadow`, `Slider_PlaceShadow`,
> `Shadow_CloneNode`, `o3de_Enable/DisableObject` - and three hand-written
> descriptions. `readable/src` is UNTRACKED, so this machine's copy had drifted
> from `tools/renames.json`; the fix is `tools/rename.py` (6 promoted RAW ->
> NAMED, 91 occurrences) and putting the three descriptions back into their
> banners, after which the regenerated file is byte-identical to the committed
> one. This check can go red on any machine whose `readable/src` has not had
> the map applied, and the fast fix is the wrong one.
>
> **TRAP 2: A SKIPPED CHECK PRINTS `ok`.** The attribution above was done by
> running the failures in a throw-away worktree at `dcf99d7` - and `engine:
> player program` and `line facing` came back "ok" there, which read as "this
> session broke them". They had SKIPPED: both need `omk-saves/GAMES`, which is
> gitignored and so absent from a worktree, and their skip path returns
> `("skipped",), ("skipped",)` - got equals want, so the row says ok. What
> settled it was reverting all four of this session's engine changes one at a
> time and getting byte-identical failures each time. **A worktree comparison
> is only valid for checks whose inputs are committed**, and a row that says ok
> may mean "did not run".

> **A RECORDED SWEEP OF "187 checks, 0 failed" DOES NOT INCLUDE THE `--slow`
> CHECKS, and that is how one stayed red for two days.** `engine: scene steps`
> went red at `74f6f8a` (2026-09-07) - the editing-hold reading made a program
> outlive its own steps, and this check's expectation was the stale half of a
> pair that then contradicted each other - and the sweep recorded below as
> `a5b1807`, "187 checks, 0 failed", ran straight past it: it is in `SLOW`, and
> plain `python3 tools/verify.py` runs only the fast list. Pinned 2026-09-09 by
> running the check at `74f6f8a` and `74f6f8a~1` in a throw-away worktree, and
> fixed by moving the expectation onto the engine's own rule.
>
> **So the sweep this counter is counting down to is `--slow`.** A row below
> that does not say `--slow` did not test any `engine:` check, and the count of
> checks tells the two apart: the fast list is 207 checks and the whole thing
> 383 (2026-09-09; both grow).

> **AND IT HAPPENED A THIRD TIME: `engine: text scroll`, found red 2026-09-16.**
> It came up while running the neighbours of a change to the shared text path -
> which is the `--only` rule working - and reported `lists 11 items 12` against
> an expected `10, 11`. **It was not that day's doing**: rebuilt in a throw-away
> worktree at `5bdb50f`, before the memory-page work, the probe already printed
> 11 and 12. The cause is the probe counting LIST ROWS over `w.all()`, which
> walks a panel once per screen that names it - the terminals' `0x004E4108` is
> lifted under eight - so a regeneration that changed how many screens name a
> panel moved the number while the tree it claims to measure did not change at
> all. Fixed by counting DISTINCT addresses (4 lists, 5 items), which is what
> the check's own sentence asserts and is stable against re-lifting; every
> behavioural line was correct throughout (32 / 0 / -24 / 0).
>
> Two things worth keeping. **A red check reads as a finding**, so the first
> instinct is to look for what this session broke - the worktree rebuild is
> what separates "mine" from "already there", and it is cheap. And this is the
> third check whose baseline was a count over LIFTED ROWS rather than over the
> thing it describes (`engine: I2D`'s flag census and `engine: slider door`'s
> row parse were the others): **a census is only as stable as the enumeration
> under it**, which is CLAUDE.md 1's rotting-scan rule one level down.
>
> **`ui geometry` was the FOURTH, found the same afternoon** - and it had been
> red since **2026-09-15**, not since today. Attributed the same way, by
> counting the COMMITTED table instead of arguing: `HEAD` gave 698 items, 57
> panels, 40 tile maps against its baseline of 628 / 51 / 34, so MULTIPLAN's
> and the shops' CODE_NAMED children had moved it a day earlier and nothing
> re-ran it; lifting the memo reader `0x004DEFF0` then added the last +20 / +1
> / +1. Its PROSE had drifted further than its assertion - "all 411 items",
> "23 of the 35 panels" - so the check was quoting three different censuses of
> one table. **When a check's sentence and its numbers disagree, both are
> suspect**, and the cheap way to split "mine" from "already there" is one
> count against `git show HEAD:<table>`.

> **A CHECK ALREADY RED FOR ONE CAUSE MUST STILL BE RUN.** The rule came from
> a peer session on 2026-09-17 and it cost both of us something the same day.
> They skipped `engine: UI` from a neighbour run on the strength of this file's
> note that it was already red for an unrelated reason; the note was accurate
> and the inference was not, and a second cause - their own regression - sat
> hidden behind the first.
>
> Checked immediately against this tree's own deliberate red, `engine: shoot
> hit`, and it had moved too - in the FAVOURABLE direction, which is just as
> invisible. `todo/shoot-patrol.md` records it as killing nobody with four
> empty elements; it now kills TWO gunmen and has one empty element left. The
> work left on it is far smaller than its own note claims.
>
> So: **a deliberate red is a reason to read the row, not to skip it.** Exclude
> a check from a run and you learn nothing about it; run it and a changed
> failure is as informative as a new one.

> **`engine: UI` IS RED AGAIN, and again it was already red — a FIFTH census
> that drifted.** Found 2026-09-17 while running the neighbours of the fight
> work, which is the `--only` rule doing its job. It reads
> `(58, 170, 718, 645, 62, 108, 58, 0)` against a baseline of
> `(57, 166, 698, 627, 60, 106, 57, 0)`: the widget table GREW.
>
> **Not that session's doing**, and the cheap attribution is the one
> `ui geometry` established - count the committed table.
> `tables/ui_widgets.json` last moved at **`1093e9c`** (the sneak memo reader),
> which is before that session's first commit `1518a8f`, and nothing in
> `tables/`, `engine/src/ui/` or `tools/ui_tables.py` was touched in it. So
> the memo reader and the MULTIPLAN/shop CODE_NAMED children lifted more
> widgets and no sweep has run since.
>
> **Deliberately NOT re-baselined.** The invariant the check exists for - the
> two implementations DISAGREEING on a screen - is the last element and it is
> **0**. What moved is the census, and `ui geometry`'s own note is that its
> prose had drifted further than its assertion, so moving these six numbers
> without re-reading what they are meant to count would repeat that. Whoever
> next owns the UI table should re-derive them from the committed table and
> fix the sentence at the same time.

> **`engine: UI` is RED and was already red at `a5b1807`, on a clean tree.**
> Measured 2026-09-07: `disagree` is 9, not 0 - the shops, screens 21..28 and
> 32, whose current list settles on row 0 in the port and row 1 in
> `tools/sim/ui.py`. What moved is `dedd3da` (2026-09-06), which added
> "a remembered selection that is no longer pickable moves off it" to
> `UiWalk::settle()` and not to the reference. So the 2026-09-07 row below
> cannot have covered it, and the next sweep should not read this as new.

| date | what was swept | result |
|---|---|---|
| 2026-09-07 | the full sweep before `74f6f8a` (next-tasks 5) | 187 checks, 0 failed |
| 2026-09-07 | after 5 tasks: the beat hand-over, the frame-by-frame body pass, the city pool swap, the speaker's placement | 187 checks, 0 failed |
| 2026-09-09 | the READER's own run, reported in conversation and not recorded here at the time | (not recorded) |
| 2026-09-09 | a partial `--slow` over the shadows, the shimmer, supersampling and the player's vertical - stopped once the reader said a sweep was not owed | 192 ok, 1 failed (`engine: slider door`) |
| 2026-09-09 | **`--slow`, the first real one since 2026-09-07** - asked for by the reader | **384 checks, 16 failed**, 1h27m. 13 were ALREADY RED before this session, 1 was an uncommitted local edit, and 2 were this session's (both mechanical). See the note below |
| 2026-09-09 | **`--slow` again, after all sixteen were closed** - measured at **`f92231a`**, so the shop-door work (`048cdf6`..`6e76caa`) that landed while it ran is NOT covered by it | **384 checks, 0 failed**, 1h28m (15:47-17:15). The first fully green run of the WHOLE suite - the two sweeps recorded above it as "187 checks, 0 failed" were the fast list, which runs no `engine:` check at all |
| 2026-09-14 | **`--slow`, asked for by the reader** - the engine at **`004d28f`** (the optimization series, steps 2-9 and 4b), in FIVE parts: the system killed the run for low memory three times (after ~222 checks, after 139 more, after 31 more), and each later part ran only the checks without a result yet. Between parts only `tools/verify.py` changed (`4078d16`: each check in its own forked process, and `licence headers` 424 -> 432 for the eight source files the optimization steps added) | **428 entries (427 checks - `engine: airlock walk` is listed twice), 424 ok, 3 failed**, none from the optimization work: `engine: shoot hit` (red on purpose, see above), `play usage` (`--shoot` and `--shoot-health` are missing from the usage text since `862caf0` / `9d7182d`), and `engine: player jump` (1, 1, **9**, -9.0, True, **False, False** against 1, 1, 13, -9.0, True, True, True - identical on a worktree build of `46b43b2`, before the series, and with the ground grid, dirty corners and GPU present each switched off; not bracketed further) |

> **THE 2026-09-14 KILLS, and what the isolation does and does not fix.** The
> runner kept every check's allocations alive to the end of the sweep, so
> memory only grew; `4078d16` runs each check in a forked child that exits.
> But the third kill came AFTER that change, and all three landed on a long
> software-rendered `omk-play` child (`shadow model`, `slider journey
> qalisar`, `cupboard take`, each `--frames 1000`): `cupboard take` alone
> passes, with its child peaking at 783 MB resident, on a machine already
> 3 GB into its 4 GB of swap with other sessions open. So one heavy CHILD
> can still tip a loaded machine over; a sweep killed that way is resumed by
> running the checks that have no result, not by starting again. And a
> `--list`-based resume must take the names from `CHECKS`/`SLOW` directly: the
> listing pads names to 20 columns, a longer name runs straight into its
> source column, and 58 such patterns matched nothing the first time.

The five tasks it covered were `4dffb70` (omk-play 78, the beat hand-over),
`74ed743` (the frame-by-frame pass on the body), `a02930e` (omk-play 79, the
city's pool coming back out of a building) and `fab6610` (omk-play 81, a
speaker's placement through a line), each verified with `--only` over the
checks it touches as it went.

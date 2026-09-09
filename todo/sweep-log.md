# The full-sweep counter

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

**tasks since the last full sweep: 14** (the shadows' orphan fix, fitted, mapped, per-pixel lighting, the shimmer, supersampling, the dither, and the player's vertical - step 1 the walk float, step 2 the jump's impulse, step 3 its state - across two sessions), the slider-door check's parse, the door read as a cutscene (omk-play 89), the turned lift paths (omk-play 90), the stale `engine: scene steps` expectation, the cupboard take's flat reach (omk-play 91), and the cupboard's stray voice line (omk-play 92, investigated and left open). **The counter had run to 19 and that was WRONG**: the reader ran a sweep more recently than this file recorded (their word, 2026-09-09). The lesson is this file's own - a counter only survives between sessions if everyone who runs a sweep writes it down, and a session that trusts a stale number spends half an hour proving nothing.

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

The five tasks it covered were `4dffb70` (omk-play 78, the beat hand-over),
`74ed743` (the frame-by-frame pass on the body), `a02930e` (omk-play 79, the
city's pool coming back out of a building) and `fab6610` (omk-play 81, a
speaker's placement through a line), each verified with `--only` over the
checks it touches as it went.

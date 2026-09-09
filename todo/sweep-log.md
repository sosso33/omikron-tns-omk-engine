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

**tasks since the last full sweep: 8** (the shadows' orphan fix, fitted, mapped, per-pixel lighting, the shimmer, supersampling, the dither, and the player's vertical - step 1 the walk float, step 2 the jump's impulse, step 3 its state - across two sessions). **The counter had run to 19 and that was WRONG**: the reader ran a sweep more recently than this file recorded (their word, 2026-09-09). The lesson is this file's own - a counter only survives between sessions if everyone who runs a sweep writes it down, and a session that trusts a stale number spends half an hour proving nothing.

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

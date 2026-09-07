# The full-sweep counter

`python3 tools/verify.py` (the whole sweep) takes upwards of half an hour —
longer than most tasks it validates. So it is **not** run per task. The rule
(CLAUDE.md §4, set by the reader on 2026-09-07):

* per task, run `verify.py --only "<name>" ...` for the checks the change could
  plausibly break, and nothing else;
* run the full sweep after **5 to 10 finished tasks**;
* record it here, so the count survives between sessions.

Increment `tasks since` when a task is committed; reset it to 0 and add a row
when a full sweep is run.

**tasks since the last full sweep: 2**

| date | what was swept | result |
|---|---|---|
| 2026-09-07 | the full sweep before `74f6f8a` (next-tasks 5) | 187 checks, 0 failed |

The two tasks since are `4dffb70` (omk-play 78, the beat hand-over) and the
frame-by-frame follow-up committed with this file. Both were verified with
`--only` over the checks they touch: `engine: editing hold`,
`engine: frame hold`, `engine: beat handover`, `licence headers`.

# Actor runtime — open issues

Found 2026-09-02 by E2 (the player controller) against `Cef_TickChannel`; `engine/src/actor/channel.cpp` was not that batch's file. Severity B: they change WHEN a transition fires under keyboard input; the AI sweep (`engine: actor states`) injects whole queues and cannot see them.

## Open

*(none)*

---

## Closed

### 1. The channel's commit lacks two of `Cef_TickChannel`'s queue rules, and `tick(dt, 0)` differs by convention — B
**FIXED 2026-09-02 by T17 — `todo/pending/T17.md`.**

1. `LABEL_75` (29_win32.c 306-312, `loc_4A8583`..`loc_4A859A`): `if (n == 1 &&
   (queue[0] & 0x40000000)) { queue[0] = 0; n = 0; }` — a lone idle word is
   dropped before the push. **Ported.** Shown to fail: without it, all 53 banks
   of the new lone-idle sweep park the press behind the idle word (53 → 0
   acting), the whole main sweep falls back to its pre-fix 12063 edges / 34056
   landings, and the player walks 0 units instead of 69.5 and never leaves
   `H_STAND`.
2. Under the CURRENT state's flag 0x20000000 with `n > 1`, the queue is cut
   to 1 (`count = 1`) instead of pushed. **Ported.** It fires 4088 times in
   the main sweep and moves none of its numbers, so it is asserted on the 9
   reachable flag-0x20000000 entries directly (3 cut, 0 not); without it those
   3 become 0.
3. `tick(dt, 0)` substituted the queue front for a zero code. **Settled**: the
   engine's polled word is never 0 (`sub_4A7A20`: `*a3 = 0; if (!a2) *a3 =
   0x40000000;`), so 0 is free and is now the NAMED constant `kQueueDrives`
   with a documented contract — "there is no device this tick, the queue is
   the input", which stands in for the engine's flag-0x80 path where
   `Cef_TickChannel` skips the poll entirely. Every tick that takes it is
   counted in `stats().queueDriven`, so it is visible rather than silent, and
   `tick`'s comment says a keyboard caller must pass `kIdleInput`.

`PlayerController`'s shadow of (1) is **removed** and the measured walk is
unchanged by its removal — the same `H_STAND -> H_SD-WK -> H_WALK -> 258 ->
H_WK-SD -> H_STAND`, 69.47 units in 60 frames, 0 refused, all four streams of
`engine: player walk` byte-identical.

### 2. `GoToMove`'s dynamic return edge was recorded and never read — B
**FIXED 2026-09-02 by T17, found while porting issue 1.** `GoToMove` ends its
entry pass with `if (to->flags & 0x800) to->gotoState = from;` and the engine
writes that into its own loaded copy of the file, so every later read of that
entry's GoTo sees it. `channel.cpp` wrote it into `dynamicReturn_` and **read
it nowhere**: all eight GoTo reads took the authored `gotoIdx`. It cost nothing
while the queue rules were missing and 956 bad landings the moment they landed
(`Sham`'s `SH_STAND` reaches two nameless flag-0x800 aliases, flags
0xC0084813, whose authored GoTo is 0 — a null deref in the engine, so it cannot
be what the engine does). `CefChannel::gotoOf` is now the single read.

---

## Notes for whoever takes the channel next

* **`Perso_SetInputEnabled`'s flag reads backwards.** `Cef_TickChannel` opens
  `if ((v5 & 0x81) != 0) { queue = [idle]; }` and gates the whole input pass on
  `(u8(v1,4) & 0x81) == 0`, so flag **0x80 set means the device is IGNORED**
  and the queue is the only input — which is how the fight AI drives an NPC
  whose channel shares the one global `sub_43E080` poll. `run_actor_states`
  models that path with `kQueueDrives` instead of with the flag; the two agree
  on the queue and differ on what the input pass does with the idle word, and
  nothing has been measured either way. Not a fault, an unexplored branch.

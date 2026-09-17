# Handoff — FIGHT MODE (`todo/next-tasks.md` 17)

Written 2026-09-16, at the end of the session that ported steps 0–4. Read this
with [`todo/fight-mode.md`](fight-mode.md), which is the plan and the reading;
this file is only what a new session needs to pick the work up.

---

## 1. HOW TO REACH A FIGHT — one command

```
cd engine && make play
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
    --fight-supermarket
```

That stands the player in AREA 245's zone whose **record 0** runs the whole
sequence itself: `scene.unload 230`, `character.show 48` ("Gun Waver 3"), a
scripted approach, then `fight.begin 48`. **Nothing is harnessed** — the
chunk's own script does all of it, so this is the real path and the one to
judge a change on. The fight starts about frame 379.

Headless, add `--frames 700` and `SDL_VIDEODRIVER=dummy`.

The other route, the way a player gets there, is the supermarket shoot phase
first (`todo/handoff-shoot-mode.md` §1):

```
build/omk-play ../gamedata ../tables --save ../traces/save-appart.bin \
    --area 230 --scene-chunk 56 --zone-disable 3949
```

`--fight N` / `--fight-level N` also exist and ARE a harness: they call the
hook directly and place the opponent two metres in front of the player. Their
trigger sits inside the adventure-mode branch, so **they do not fire while a
cutscene owns the body** — which is how a demo got handed over that never
reached a fight at all. Prefer `--fight-supermarket`.

**Keys in a fight (scheme 3):** → forward, ← back, ↑ jump, ↓ crouch,
**Q**/**W** punches, **A**/**S** kicks, Del/End sidestep.

## 2. Where the work stands

Steps 0–4 of the plan are done and committed:

| | | |
|---|---|---|
| `86b3b40` | step 0 | the reading; op 62's third field is the AI LEVEL and the difficulty is ADAPTIVE |
| `11b57a3` | 0b | the "unexplained guard" was the VM's DRY RUN — a correction to 86b3b40 |
| `8c7c24e` | 1 | the runtime: contexts, `Fight_Begin`, `ResolveHit`, `TickAI`, the KO ring. Found the AI's eight built-in move tables (`tables/fight_ai_moves.json`) and the per-slot weight |
| `7aad76a` | 2 | the Session and viewer wiring; a fight runs in `omk-play` |
| `4eb178b` | 3 motion | the opponent's motion pass; the entry-228 stall was a feedback loop, not `GoToMove` |
| `4a9abb3` | 3 pose | he animates from his own channel; `CefChannel::clipOwner()` |
| `ae85300` | 4 | camera mode 14, and options row 18 is two camera RIGS |
| `b96f9bc` | 4b | the play test's three faults |
| `351874b` | 4c | the opponent holds his height; the `grep` lesson |

`verify.py: engine: melee` is green (3 banks, 9 profiles, 9 fights, 6 ending in
a KO, 204 damage figures re-derived). Its docstring records every re-baseline
with its cause — including one that was made for a change later withdrawn as an
invention. **Read that docstring before moving the row again.**

What a played fight does today: the script stages the robber, the fight starts,
both fighters move and animate from their own `.CTL` channels, blows land and
are re-derived, the loser goes down, the KO replay runs twice, `sub_445AC0`
restores the adventure bank and scheme 0, and the parked script resumes.

## 3. FIXED 2026-09-16 — the camera comes back (§13 of the plan file)

The reader's *"the engine didn't switch back to adventure mode after the end
of the fight"* is closed. The script **does** request a camera afterwards —
AREA 245 record 0 has three `camera.set 0` at bytecode 1480/1559/1572, and the
potion block's `jmp_if_false 175` lands exactly on 1480 — and the port's
frontend ended the mode-13 hold on the Session's camera **id** changing, which
cannot see a request for the camera already installed. The engine compares the
MODE first (`Camera_RequestChanged`, 0x004147F0), so mode 12 under an
editing's mode 13 always takes the camera. `Session::cameraRequests()` now
counts the request and the frontend watches that; it subsumes the id test, so
the clear-list got shorter. `verify.py: engine: hold release` (SLOW).

**The earlier note that "the run logged no camera request at all after frame
806" was an artefact**: `OMK_CAMLOG=1` had not been set. With it on the
requests print at frames 0, 665 and 1031, two frames after the hold begins.

## 4. Also open

* **§14, the bindings.** The shipped `Tirer` scheme turns with the keypad, and
  a laptop has none. The original rebinds through its options menu and stores
  the result in the save header — `SettingsBlock` carries all three device
  tables verbatim at offsets 52/276/500 and `resolveSettings` reads them — but
  `omk-play` builds its `Input` once from `tables/key_bindings.json`
  (play.cpp ~2163) and **ignores the save's tables**. The faithful fix is to
  feed them through `Input::rebind` (already the port of `Opt_RebindKey`), not
  a remap flag. Note the save we test with carries the stock bindings, so the
  change is unobservable on it: it needs a save with custom keys or a mutation
  to prove. **Shoot mode does not actually need the keypad** — the mouse turns
  the body and pitches the camera (play.cpp ~6856).
* ~~**`Fight_TickAI`'s defensive arm**~~ - **done 2026-09-17** (`fight-mode.md`
  15.11): the guard raise, the two built-in presses, and the latch clear that
  brings the guard down; `engine: melee` blocks 0 -> 24.
* ~~**The priority gate.**~~ - **done 2026-09-17** (`fight-mode.md` 15.14): the
  `+212` writer is `sub_45ACD0` (the channel base is 0x8F5920, not 0x8F5928),
  and the threshold is the player's experience truncated through
  0.024390243 - moves unlock with rank. Not seen changing a decision in any run
  yet.
* ~~**Step 5, the HUD**~~ — **done 2026-09-17**: both gauges, hidden through
  the KO replay, and `sub_447000` read and ported - a four-second STAT CARD of
  the player's properties with `IAM\SNEAK`'s initials and rank (`fight-mode.md`
  15.5, `engine: fight hud`).
* **The camera's unmodelled parts**: ~~the tail's collision solve~~ (ported
  2026-09-17, `fight-mode.md` 15.8b - the bolts' ray from the look-at to the
  eye, X and Z only, the height clamp behind it); the throw swing's length,
  because `sub_45ACF0` has no port.
* ~~**The opponent's first second**~~ on the nameless clipless entries 208/228
  - **gone 2026-09-17**: the 12 m that made it legitimate was the port's own.
  `beginMelee` read the approach program's path START instead of where the
  program left him, 1.5 m from the player (`fight-mode.md` 15.8e). The fight
  now opens on `HGUARD` against `HGUARD`, and the opponent's WALL collision
  (15.8a) is **wired** the same day: his own walker, every move of the frame as
  one try (`engine: fight collision`, via the `--fight-foe-at` harness, since
  the real fight never reaches a wall). His VERTICAL is still his placement's.

## 5. Traps that cost time in this session

1. **`grep` goes binary-quiet on the viewer's log.** It carries an invalid
   UTF-8 byte in a French voice-over line, so `grep` treats the file as binary
   and prints nothing while `sed` and `awk` show the lines. **Four** wrong
   "the log does not print that" conclusions came from this. Use `grep -a`.
2. **Two `Bash` calls in one turn share a shell.** A `cd` in one raced a `cd`
   in the other and a commit failed with *"ne correspond à aucun fichier"*.
   Use `git -C <repo>` and absolute paths.
3. **A windowed run buffers its stdout through a pipe.** `wc -l` reported 0
   lines while 14 KB sat in the file. Check the byte size, not the line count.
4. **A harness that does something the real path does not.** `--fight`'s
   trigger lives in the adventure branch and never fires under a cutscene; two
   demos were handed over that reached no fight. The shoot handoff's trap 1 is
   the same shape.
5. **A subsystem can be correct and invisible.** The fight camera's own trace
   moved the whole time while the editing hold drew over it — every number read
   healthy and the screen was frozen. Only a person watching caught it.
6. **A re-baseline is only as good as the change under it.** `engine: melee`
   moved 205 → 203 for a change (the opponent's input-block flag) that was
   later withdrawn as an invention, then back to 205, then to 204 for a real
   fix. Each move is in the docstring.

## 6. Instruments

`omk-play` prints, once a second of fight: both hit points, the separation in
metres, both `.CTL` states and entry names, the hit/graze/block/AI-move
counters, **both bodies' positions** (a height that walks away from its
placement is how "the enemy disappears" shows up), the camera's state/eye/at/
heading/radius, the KO counter, the AI's clock and intent, and the opponent
channel's tick/transition/landing/clipEnd/badLanding counters.

`engine/tools/run_fight.cpp` is the headless probe behind `engine: melee`: it
fights profile against profile on all three `CMBT` banks and **re-derives every
damage figure** from the attacker's own combat block rather than believing the
runtime.

## 7. 2026-09-17, later: the KO bands, the robber's height, and a build trap

The knock-out's `Screen_Fade(1)`/`(0)` are wired - they are the letterbox bands,
so the replay plays between bars (`fight-mode.md` 15.13). The robber's height
follows his walker's floor. And one thing to know before mutating anything
here: GNU Make 3.81 compares whole seconds, so a mutation restored in the same
second as its compile stays compiled - `touch` and rebuild after every restore
(CLAUDE.md 1).

Also 2026-09-17: the opponent's `.CTL` effect SPRITES (15.10's second half) are
drawn, on his bones through `Staged::meshAt`. Not judged by eye yet.

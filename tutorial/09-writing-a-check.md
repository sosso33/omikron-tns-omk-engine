# Lesson 9 — Writing a check

**Goal:** turn something you watched happen into a check that keeps it
happening: build what it measures, read a record rather than prose, guard
against reading nothing, and show it fail before you trust it.

Lesson 3 found a behaviour nothing tested - the close button `0x0042A990`
that every terminal, shop and save screen shares. This lesson walks through
the check that now covers it, `engine: close button`, one decision at a
time, and then you write its sibling.

## Read

* `tools/verify.py`, the function `c_engine_close_button` (`grep -n` it).
  About fifty lines, half of them its docstring.
* `tools/verify.py`, the helpers above the checks: `_play_built`,
  `_play_out`, `_events`, and `wait` / `press` / `hold`.
* `docs/PORTING.md` Part B, "every check must be SHOWN to fail".
* `docs/PRACTICE.md` §1, the bullets that begin "A CHECK MUST BUILD THE
  BINARY IT MEASURES" and "A LOG LINE PRINTED WHERE A VALUE IS HANDED OVER"
  (`grep -n` them; `CLAUDE.md` §1 keeps each as a one-line trap).

## The decisions, in order

### 1. What, exactly, is the behaviour?

Not "the button works". The behaviour is: **confirming the button closes the
screen, the screen answers, and the script that opened it gets the answer.**
Each part is observable, and each can fail on its own: a walk that closes
without answering, an answer that never reaches the variable.

### 2. Where is it observed - and by whom?

The viewer already printed a line when a screen answers:

```
screen 5 answered 0 -> the script resumes (variable 19 now 0)
```

and that line is printed from what the `Session` STORED, read back after
`Session::answerUi`, not from the answer handed to it. That matters: a line
printed where a value is handed over reports the intention, and a check that
reads it cannot see the write being lost. Always print from the consumer.

But a check that regexes English breaks when the English is reworded. So
beside that line, in `engine/backends/sdl/playframe_modes_parts.cpp`, the
same facts go out as a record (lesson 5):

```cpp
omk::event("ui.answer", {{"screen", openScreen}, {"value", walk->answer()},
                         {"var", uiVar}, {"stored", uiVar >= 0 ? int(state.var(uiVar)) : -1},
                         {"how", walk->log().empty() ? std::string() : walk->log().back()}});
```

`how` is `UiWalk::closeNow`'s own reason - it logs it last - so the record
can tell the button (`close (0x0042A990)`) from a TAB (`close (TAB)`), which
closes the same screen with the same answer. Without `how` the check would
pass for a broken button whenever something else closed the screen.

### 3. Build what you measure

```python
play = _play_built()
if not play:
    return _PLAY_UNBUILT
```

`_play_built` runs `make play` and returns the viewer it built. A check that
built only its probe and then ran `omk-play` measured whatever binary was
lying around - and a mutation of the code it meant to test left it green.

### 4. Drive it

```python
keypad = [press("DOWN", frames=2), wait(4)] * 4           # cell 0 -> 3 -> 6 -> 9 -> 10
steps = [wait(40), press("ENTER", frames=2), wait(300)] + keypad + \
    [press("ENTER", frames=2), wait(40)]
out = _play_out(play, "--area", 179, "--stand", "5468,336,-12118,120", "--frames", 420,
                "--nofmv", "--nodelay", "--no-crowd", "--hold", hold(*steps),
                env={"OMK_EVENTS": "1"})
```

`hold(...)` writes the `--hold` stream from named keys, so the check reads
as what a person does. `_play_out` runs the viewer headless with the save and
the tables, and decodes the log as latin-1 - it carries the game's accented
text.

### 5. Read records, and prove you read any

```python
events = _events(out)
answers = tuple((e["screen"], e["value"], e["stored"], e["how"])
                for name, e in events if name == "ui.answer")
return (len(events) > 0, answers), (True, (("5", "0", "0", "close (0x0042A990)"),)), \
    "the run printed records at all; then the terminal's answers: ..."
```

A check returns three things: what it measured, what it expects, and what
each value means. The first value here is the one people leave out: **did
the run print any record at all?** Without it, a log never read - the switch
not passed, the viewer crashed at frame 0 - reports "no answer" exactly as a
broken button does, and a red check is read as a finding when it is a
harness fault. A check whose input it cannot read must say so.

### 6. Show it fail - three ways

Every check in this repository says in its docstring how it was shown to
fail. This one, three ways, each a different value:

| mutation | result |
|---|---|
| `closeNow` deleted from the button's arm | `(True, ())` - no answer |
| the last ENTER replaced by TAB | `how` is `'close (TAB)'` |
| `OMK_EVENTS` not passed | `(False, ())` - nothing was read |

A mutation that does not turn the check red is not a passed test: it is a
check that cannot see what you changed, or a mutation that did not apply,
or a binary that was not rebuilt. Find out which before you go on.

### 7. Put it where it costs least

The check renders 420 frames, so it goes in `SLOW` (run by `--slow` and by
`--only`), not in the fast list every plain `verify.py` runs. Every check is
a cost every full sweep pays for as long as the repository exists.

## Run

```
python3 tools/verify.py --only "engine: close button"
```

`ok`. Then call it directly to see the values:

```
python3 -c "
import sys; sys.path.insert(0, 'tools'); import verify
print(verify.c_engine_close_button()[0])"
```

```
(True, (('5', '0', '0', 'close (0x0042A990)'),))
```

## Change: the sibling check

The same button closes every shop - *Quitter*, item 3 of the shop panel's
four. The pharmacy in area 39 is a headless route: ENTER four times through
the shopkeeper's conversation opens screen 21, two DOWNs reach *Quitter*:

```
cd engine
SDL_VIDEODRIVER=dummy OMK_EVENTS=1 build/omk-play ../gamedata ../tables --software \
    --save ../traces/save-appart.bin --area 39 --nofmv --no-crowd \
    --keys 28,28,28,28,208,208,28 --keydelay 60 --frames 560 \
    | grep -a "shop header\|closed\|^@evt ui"
```

```
shop header: screen 21 | 'Acheter' | ...
shop header: screen 21 | 'Examiner' | ...
shop header: screen 21 | 'Quitter' | ...
screen 21 closed without an answer -> -1, the script resumes
```

No `ui.answer` record: a shop answers nothing, so the viewer takes its OTHER
branch - "closed without an answer" - which has no event. Your work:

1. In `playframe_modes_parts.cpp`, after the `closed without an answer`
   line, add the same record with `value` -1 and the walk's `how`. Run the
   command again; you should see
   `@evt ui.answer screen=21 value=-1 how="close (0x0042A990)"`.
2. Write `c_engine_close_button_shop` beside `c_engine_close_button`, with
   `--keys` and `--keydelay` instead of `--hold` (the conversation needs its
   presses spaced), expecting `(True, (("21", "-1", "close (0x0042A990)"),))`.
3. Register it in `SLOW`, run it green, and show it fail - at least by
   deleting `closeNow` again.
4. Write its docstring: what it covers, why, and how you showed it fail.

## Traps

* **A scan of another tool's output whose non-match looks like an answer.**
  A check once regexed a tool's lines in an order the tool never printed:
  zero lines matched, and one assertion over the empty result was false
  (loud) while its sibling was true for free (silent). Count what you parsed
  before deriving anything from it.
* **A stale object file passes.** macOS `make` compares times to the whole
  second; a mutation restored in the same second as the build that compiled
  it stays compiled. `touch` the file and rebuild after every restore - and
  if a re-run reproduces the mutation's exact numbers, that is what happened.
* **Revert a mutation by editing it back,** or from the index after `git add
  -A` - never with `git checkout` on a file holding work you have not
  committed. A mutation script whose anchor did not match once made the
  "revert" a delete of two new checks.

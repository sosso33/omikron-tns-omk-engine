# Lesson 6 — The interface walk

**Goal:** know how a screen is described (as data lifted out of the
executable), how a key moves through it (`UiWalk::press`, the port of
`Ui_DispatchInput`), how a button reaches its behaviour (by the callback
ADDRESS the data names), and how a screen answers the script that opened
it.

## Read

* `docs/UI.md`: §2 (the screens), the opening of §3b (the widget tree), §3c
  (input - one word of bits) and §3d-bis (the answer layer). Long; read the
  prose and skip the tables.
* `engine/src/ui/widgets.h`: the comment above `class UiWalk`, and the
  class's public methods `open`, `press`, `answer`, `approximate`.
* `engine/src/ui/widgets.cpp`: `UiWalk::press` (its head), `UiWalk::closeNow`,
  and `UiWalk::confirmTerminalCell`.

## A screen is data

The interface is 37 screens. A screen shows a **panel**; a panel holds up to
ten **lists**; a list holds **items**; and an item carries a position, a
text, and a **callback** - the address of the engine function its confirm
calls. None of it is in `gamedata/`: it is compiled into the executable, and
`tools/exetables.py` lifts it into `tables/ui_widgets.json`. Look at the
ARCHIVES, screen 18:

```
python3 -c "
import json
d = json.load(open('tables/ui_widgets.json'))['rows']
p = next(p for p in d['panels'] if p['screen'] == 18)
print('panel', hex(p['panel']), 'flags', hex(p['flags']), 'lists', len(p['lists']))
for li, l in enumerate(p['lists']):
    for ii, it in enumerate(l['items']):
        print(' list', li, 'item', ii, 'at', it['x'], it['y'], 'callback', hex(it.get('callback', 0)))
"
```

```
panel 0x4e4108 flags 0x20000030 lists 3
 list 0 item 0 at 533 135 callback 0x4af410
 list 0 item 1 at 563 135 callback 0x4af410
 list 0 item 2 at 593 135 callback 0x4af410
 ...
 list 0 item 9 at 563 230 callback 0x4af410
 list 0 item 10 at 528 398 callback 0x42a990
 list 1 item 0 at 40 80 callback 0x0
 ...
```

List 0 is the keypad: ten cells three wide on `0x4AF410`, the terminal
family's cell callback, and item 10, lesson 3's close button. List 1 is the
display, list 2 the bar. The panel's flags carry `0x20` - TAB may close it
- and `0x10` - BACK may close it from the top. The same panel serves seven
terminal screens; what differs is the screen number the callback is handed.

## The walk

`UiWalk` (`src/ui/widgets.*`) holds the screen open and takes the engine's
own **input word** - the same 14-slot word the player's state machine reads
(lesson 1), so the interface needs no keyboard of its own:

* **`UiWalk::press`** is `Ui_DispatchInput`: TAB (`0x2000`) closes if the
  panel's flags allow, BACK (`0x20`) goes to the parent panel or closes, and
  anything else goes to the panel's hook, then the current list's hook, then
  the default - the arrows move the selection, `0x10` confirms;
* **a confirm** finds the selected item's callback ADDRESS and runs the port
  of that function: `UiWalk::confirm` and its helpers are a series of
  `if (it->callback == kCb...)` arms, one per callback, each named for the
  address it ports (`kCbTerminalCell = 0x004AF410u`);
* **an answer** is how the screen talks back to the script parked on
  `ui.open` (lesson 4). A callback either answers AT ONCE (`answer_`, the
  screen then closes) or writes an answer that waits for the screen's close
  (`close_.answer`, handed over by `UiWalk::closeNow`).

And one rule the whole walk keeps: **it does not guess.** A screen whose
callbacks are not ported sets `approx_`, and the `Session` refuses an
approximate answer rather than inventing one (`Session::answerScreen` returns
-1 for it).

## Run

Lesson 4's archives run, read for the interface this time:

```
cd engine
H="k*40,k28*2,k*80,k205*2,k*4,k205*2,k*10,k28*2,k*20"
H="$H,k208*2,k*4,k208*2,k*4,k208*2,k*4,k208*2,k*4,k208*2,k*4,k208*2,k*4,k208*2,k*4"
H="$H,k*20,k57*2,k*20,k15*2,k*150"
SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables --software \
    --save ../traces/save-appart.bin --area 180 --address 565 \
    --frames 460 --nofmv --nodelay --no-crowd --hold "$H" > /tmp/arch.txt
grep -a 'screen 18\|answered' /tmp/arch.txt
```

```
frame 104: screen 18 is asking - arrows move, ENTER confirms, TAB closes
screen 18: its own sound (slot 4, `arc007`)
terminal family: screen 18 cell 0 - the bar says '{fCC}Consulter le dossier "Manek"'
terminal family: screen 18 - the display says '...(string 5, the cover page)
terminal family: screen 18 cell 1 - the bar says '{fCC}Consulter le dossier "KHONSU"'
terminal family: screen 18 cell 2 - the bar says '{fCC}Consulter le dossier "Meurtres"'
terminal family: screen 18 - the display says '...(string 8, the dossier reader, the cell's tag)
terminal family: screen 18 - the display says '...(string 5, the cover page)
screen 18 answered 1 -> the script resumes (variable 19 now 1)
```

Read it against the keys: RIGHT twice to cell 2 (*Meurtres*), ENTER - the
display shows the dossier (string 8) - seven DOWNs scroll it, SPACE (BACK)
returns to the cover page, TAB closes, and only THEN does the script hear
the 1 that cell 2 wrote. `{fCC}` and `{I255120045}` are the game's text
markup - a font, a colour - drawn by `src/ui/text.*`.

## Change

In `UiWalk::confirmTerminalCell`, the ARCHIVES arm holds the answer:

```cpp
if (row == 2) {
    close_.answer = 1;
    log_.push_back("archives: cell 2 - answer 1, held until the close");
}
```

Make it answer at once - `answer_ = 1;` instead of `close_.answer = 1;` -
rebuild (`make play`), and run the same command:

```
terminal family: screen 18 cell 2 - the bar says '{fCC}Consulter le dossier "Meurtres"'
screen 18 answered 1 -> the script resumes (variable 19 now 1)
```

The script still hears 1 and the story still moves on - but the screen
closed the instant the cell was confirmed. The dossier never appeared: the
player is told *data memorised* about a file they never got to read. The
original's callback writes the answer and installs the reader panel, and
does NOT close (`docs/UI.md` §3d); a port that answered at once looked right
in every log line about the story.

## Prove

```
cd ..
python3 tools/verify.py --only "engine: dossier reader"
```

With the change in, the check prints the STORY the walk told, step by step,
against the one it expects:

```
engine: dossier reader FAIL ((
    'display 5 (the cover page)', 'answered 1'), True, ...) != ((
    'display 5 (the cover page)', "display 8 (the dossier reader, the cell's tag)",
    'list 1, scroll 0 of 48', 'list 1, scroll 8 of 48', ... 'list 1, scroll 48 of 48',
    'display 5 (the cover page)', 'answered 1'), True, ...)
```

Two steps against eleven. That shape - a sequence, not a final value - is
what lets a check catch a change whose END STATE is right. Restore
`close_.answer = 1;`, `touch src/ui/widgets.cpp`, rebuild, and run it green.

## Traps

* **The interface reads the input word edge-filtered.** A held key moves
  once. A scripted stream must release between presses (`k208*2,k*4`), or
  the second press is the same edge and does nothing.
* **A callback in a table has no function in the decompilation.** Most of
  the interface's behaviour lives at addresses nothing calls (lesson 3): read
  the executable at the address before trusting a block or a name.
* **A screen answers -1 when it is LEFT.** Every close path posts the answer
  it holds, which starts at -1 - and a script's `push.var 19 / cmp` treats
  -1 as "not 1". When a port's screen closes without answering, the script
  does not hang: it takes the other branch, quietly.

# Lesson 11 — Capstone: port something end to end

**Goal:** take one thing the original draws and the port does not, from the
first sighting to a committed change with its check - every step of lessons
2 to 10, in the order a real port takes them.

The thing is the **outline** the interface draws round some items. It was
ported when this course was written, and the course's author followed the
trail below to do it; you will follow the same trail on a tree that does not
have it, and compare your result with the committed one at the end.

## Start from the tree without it

```
git switch -c capstone
git revert --no-edit $(git log --grep 'item OUTLINE ported' -1 --format=%h)
```

That makes a branch whose last commit takes the port out - the code, its
check, and the two documents that mention it. Build (`make && make play` in
`engine/`). When you are done, `git diff main -- engine/src/ui` shows how
your port differs from the committed one, and `git switch main` takes you
back.

## 1. The sighting

`traces/frames/loadpanel-mode2.png` is the ORIGINAL engine's framebuffer,
captured under CrossOver: the load panel, a save selected, its picture on
the right. Open it, then render the port's:

```
cd engine
cp ../traces/games-resto.bin /tmp/games.bin
SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables --software --res 640x480 \
    --saves /tmp/games.bin --frames 160 --nofmv \
    --hold 'k*20,k208*2,k*10,k28*2,k*40,k208*2,k*60' --dump /tmp/load.bin > /dev/null
python3 ../tutorial/dump2png.py /tmp/load.bin
```

(`--saves` on a COPY: the run must not be able to write the committed file.)
Put the two side by side. In the original, the save picture has a thin grey
frame. In the port it has none.

Make that a number, not an impression - this is the measurement the check
will later make. Save as `/tmp/ring.py`:

```python
import sys; sys.path.insert(0, 'tools'); import frame as F
w, h, cap = F.read_png('traces/frames/loadpanel-mode2.png')
a = open(sys.argv[1], 'rb').read()
def port(x, y):
    p = a[2*(y*640+x)] | a[2*(y*640+x)+1] << 8; return ((p>>11)<<3, ((p>>5)&63)<<2, (p&31)<<3)
def capx(x, y): i = 3*(y*w+x); return tuple(cap[i:i+3])
ring = [(x, 139) for x in range(459, 589)] + [(x, y) for y in (236, 237) for x in range(459, 589)] \
     + [(x, y) for y in range(140, 236) for x in (459, 588)]
grey = lambda c: c[0] > 60 and abs(c[0]-c[1]) < 25 and abs(c[1]-c[2]) < 30
print(len(ring), sum(grey(capx(x, y)) for x, y in ring), sum(grey(port(x, y)) for x, y in ring))
```

```
cd .. && python3 /tmp/ring.py /tmp/load.bin
```

```
582 580 3
```

582 pixels in the ring round the 128x96 picture at (460, 140); 580 of them
grey in the original's frame, 3 in the port's (where the selection's
connector bar crosses the ring). How was the ring's shape known - one row
above, TWO below, a column each side? By sampling the capture's pixels round
the picture, and it is the shape `fillQuad`'s half-open spans give four
mode-1 quads (`src/ui/surface.h`, above `quadMode`).

## 2. Who draws it

An outline round an item. `readable/INDEX.md` has `Ui_DrawItem`
(`0x004764A0`, in `readable/src/24_sys.c`); read it. After the text and the
fill:

```c
if (I2D_TestFlag((uint32_t *)a3, 1073741952))
{
    ...
    v21 = u8(a3, 10) | ((u8(a3, 9) | (v9 << 8)) << 8);
    ...
    I2D_DrawRectOutline(v10, v15, v17, v19, v21, v23);
}
```

`1073741952` is `0x40000080`: bank B, bit `0x80`. The colour is the item's
bytes `+8/+9/+10`; the alpha byte `v9` comes from oscillator 2 under bank B
`0x400000`, and a mode-1 quad does not read it. `I2D_DrawRectOutline` is
the function the load panel's SELECTION BOX already uses, ported by hand in
`ScreenComposer::drawLoadSelection`.

Which items carry the bit? Ask the tree (lesson 6):

```
python3 -c "
import json
d = json.load(open('tables/ui_widgets.json'))['rows']
for p in d['panels']:
    for li, l in enumerate(p['lists']):
        for ii, it in enumerate(l['items']):
            if it['flags'][1] & 0x80:
                print(p['screen'], hex(p['panel']), hex(l['addr']), hex(it['addr']), it['rgb'], it['x'], it['y'], it['w'], it['h'])
" | sort -u
```

Two items: `0x4E4000`, the terminal family's display box, (254, 68, 20),
in seven screens; and `0x4CEAB8` in list `0x4CEB50`, the save picture at
(460, 140, 128, 96) - in **(255, 0, 0)**.

## 3. Port the arm

In `ScreenComposer::drawItem` (`src/ui/screendraw.cpp`), after the call
that draws the fill, add the arm: when bank B carries `0x80`, four mode-1
quads round the item in its colour. Take the quads out of
`drawLoadSelection` into one helper - two copies of `I2D_DrawRectOutline` is
one too many. Build, render the panel again, run `/tmp/ring.py`:

```
582 580 0
```

Zero grey - and the picture now has a RED frame. That is the placeholder:
most of the tree's items ship (255, 0, 0), because a page builder writes the
real colour at run time (`UiWalk::itemColour`, and `docs/UI.md`'s note on
the placeholder).

## 4. Find the colour

The capture says the frame is grey. Back-solve it: a mode-1 quad halves
both colours and adds them, the background there is about (16, 16, 8) and
the ring reads (90, 93, 82) - so the source is near (164, 170, 156), which
is (170, 170, 170) after the 565 rounding. Somebody writes 170 grey over the
picture's list. `sub_4296D0` is the list colour writer (`UiWalk::colourList`
ports it). Search the listing for the list:

```
grep -n -B6 'call    sub_4296D0' Runtime.exe.asm | grep 4CEB50
```

and read round the hit:

```
push    0AAh
push    0AAh
push    0AAh
push    offset word_4CEB50
call    sub_4296D0
```

0xAA is 170. Now WHICH function is that? Walk back up the listing: the code
sits after `sub_479C20 endp` and an `align`, with no `proc` of its own -
lesson 3's trap again. Disassemble the executable where the `align` ends:

```
objdump -d --no-show-raw-insn --x86-asm-syntax=intel \
    --start-address=0x479D10 --stop-address=0x479D20 "gamedata/Runtime 2.exe"
```

It starts at `0x479D10`, and `grep -rn 479D10 engine/src` finds it named in
`src/ui/cloud.h`: **screen 29's open callback** - the START MENU's. The
menu's open builds its children's records too, the slot panel among them.

Port it in `UiWalk::open`: on screen 29, colour every item of list
`0x004CEB50` (170, 170, 170). Careful: `colourList` searches only the
CURRENT panel, and the slot panel is not current when the start menu opens;
`UiWidgets::listAt` finds a list anywhere in the tree. Render and measure:

```
582 580 582
```

Every ring pixel grey. Compare the colours as well as the shape: of the 582,
**515 are within 16 levels of the original's own pixel** - the rest sit over
the tile map, which animates, under a half-blend.

## 5. The other item

The terminal's display box ships the bit SET, and its builders switch it -
lesson 6's dossier reader, commented in `UiWalk::buildPage`:

```
sub_4AF2B0  sub_428FF0(0x004E4000, 0x40000080, 0)   the cover page: off
sub_4AF2D0  sub_428FF0(0x004E4000, 0x40000080, 1)   the reader: on
```

The walk can already SET a bank-B bit at run time (`UiWalk::itemFlagsOn`)
but not CLEAR one the record carries. Add the cleared bits beside it, a
setter that keeps the two consistent, apply them where the composer combines
the record's bits with the runtime's, and make the two builders call it.
Then render AREA 180's archives (lesson 4's keys) at frame 190, on the cover
page, and at 215, in the reader: the 1504-pixel ring round the box should be
orange only in the second.

## 6. The check, and the proof

Write `engine: item outline` (lesson 9): build the viewer, render the panel
and the two archive frames, and return the four ring numbers and the two
orange counts. Then show it fail three ways, one per thing you ported:

| mutation | what the check says |
|---|---|
| the outline arm removed | the panel's ring 3 grey, the reader's 0 orange |
| the start menu's colour removed | the ring drawn red: 0 grey |
| the cover page's clear removed | the box outlined on the cover page too |

Restore each by editing, `touch`, rebuild.

## 7. Finish it

* the guard: `python3 tools/refactor_guard.py record <label>` before and after
  - your change should move no recorded run, because none ends on these
  screens; if one moves, find out why;
* the other targets: the 3DS, Vita, classic Mac and Quest builds compile
  `src/ui` too;
* the documents: `docs/UI.md` (the dossier reader's paragraph) and
  `todo/missing-ui.md` both said the outline was not drawn;
* a commit that says what was ported, the evidence, and the check.

Then compare: `git diff main -- engine/src/ui tools/verify.py`. Differences
in naming and placement are yours to judge; a difference in a NUMBER the
check prints is a question to answer before you go back to `main`.

## Traps

* **The placeholder is not the colour.** 209 of the tree's 222 fill items
  ship (255, 0, 0). A port that draws the record's colour draws red where
  the game draws something a builder wrote.
* **A helper that finds things in the current panel** misses a record the
  original wrote from somewhere else entirely - here, a parent screen's open.
* **Done is usable, not read.** The port is finished when the outline is on
  screen in `omk-play`, measured against the original's frame, held by a
  check that was shown to fail, and the documents say so.

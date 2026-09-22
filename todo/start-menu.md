# The start menu's OPTIONS and QUIT — next-tasks 9

Opened 2026-09-22. The reader scoped item 9 down to these two: *"only OPTIONS
and QUIT are missing, low priority"*. This file is the survey that has been
done, and it stops where a READING has to start.

## 1. What the menu is, from the lifted table

Screen 29 `OMK START MENU`, panel `0x004CF218`, four items in one list, each
with **callback 0** and a CHILD panel - so every one of them DESCENDS and none
of them answers:

| item | string (`IAM\Menu`) | child |
|---|---|---|
| `0x004CE6F0` | 0 `Nouvelle partie` | `0x004CF280` - the name field and its confirm |
| `0x004CE738` | 1 `Charger une partie` | `0x004CF2E8` - the load panel |
| `0x004CE780` | 4 `Options` | **`0x004CF420`** |
| `0x004CE7C8` | 5 `Quitter` | **`0x004CF488`** |

## 2. What the port does today, measured

Driven headlessly - `build/omk-play <data> ../tables --software --nofmv
--keys 0,0xD0,0xD0,0x1C --keydelay 30` (the leading `0` matters: a first key
on frame 0 is lost, which is the `--hold` trap one list over):

* **both items descend correctly** - the frame changes on ENTER;
* **OPTIONS draws its heading and NOTHING else.** That is faithful to the
  table: panel `0x004CF420` has **one item** (the title, bound to string 4)
  and **hook 0**. There is nowhere for the rows to come from;
* **QUIT draws its confirm** - the heading and two options, the selected one
  lit - but **neither button does anything**. Confirming and cancelling both
  leave the menu up and the run goes to its full frame count.

## 3. Where the missing halves actually live - and one wrong turn

The table carries a `callback` per item, so the obvious move is to read the
confirm's. **That was wrong and is worth recording**: `Confirmer`'s callback
`0x0047BC10` is a TEXT function, not an action - it tests event `0x1D`, and
either way `sprintf`s `" %s "` into a caller's buffer (it quotes a save's name
for a confirm's subject line). `Annuler`'s callback is 0 and its CHILD is
`0x004CF218`, the menu root, so cancelling is a plain descent and needs no
code at all.

So **neither half is in the widget records**:

* **QUIT**: nothing in the item records performs the quit. The pause menu's
  own quit is the shape to compare against (`docs/UI.md` 3h): its four items
  are *four instructions each*, none has a `proc` label, and `Oui` sets
  `dword_4E6C9C`, a REQUEST served at the top of the next `Script_Pump`. The
  start menu's is likely the same shape and the same global - but that is a
  guess and the point of the next step is to stop guessing.
* **OPTIONS**: panel `0x004CF420`'s rows are built natively. `docs/UI.md` 3436
  already says it - *"`OPTIONS` (its pages are built by native code)"* - and
  screen 35 `OPTIONS` is a screen of its own (panel `0x004DD438`, hook
  `0x00492AD0`, a 16-item list) that the SNEAK loads with
  `UI_LoadScreen(35, ...)` from its Options tab. Whether the start menu hosts
  35 the same way, or fills `0x004CF420` itself, is the question.

## 4. The next step, in order

1. **Find what fills `0x004CF420`** and what the quit item does. Neither is an
   item callback, so the place to look is the panel's own `+4` builder and
   screen 29's open callback - and both are in the class CLAUDE.md 1 warns
   about: **26 of 30 per-screen callbacks have no `proc` label** because
   nothing CALLS them, so `asmfn.py` will snap to a neighbouring function
   without saying so. Disassemble at the address the DATA names, not by name.
2. If the menu hosts screen 35, the port already has the options page tree
   (`tables/ui_widgets.json`'s `optionPages` / `optionRows`, walked by
   `tools/sim/ui.py` and `engine: options`), so OPTIONS becomes a wiring job
   rather than a build.
3. QUIT, once read, is small: the port already ends its run for the pause
   menu's quit and says so.

**Do not port either from the table alone** - the table is what says these two
panels are empty, and a confirm whose buttons do nothing is exactly what
building from it produces.

# The sneak — what is left

The **sneak** is Kay'l's handheld device: screen 9, opened with TAB, five tab
pages down the left (inventory, slider, identity, memory, options) plus the
verb panel and the examine page it descends into. Opened 2026-09-04 from
*"save what is remaining for the sneak"*, after the device was built,
coloured, navigated, drawn and driven end to end in play.

The findings are in `docs/UI.md`; `docs/RECONSTRUCTION.md`'s 2026-09-04 rows
are the record of what was done. **This file is only what is NOT done.**

Read this first, because it decides what a fix is allowed to look like:
**almost every piece of interface state here is a STATIC DATA-SEGMENT RECORD
with process lifetime** — `list+2` (the selection), the item colour bytes
`+8/+9/+10`, the flag words, `panel+24` (the current list). Builders write
them; nothing resets them. Five separate bugs in this device came from
treating one of them as walk-local state, and one came from clearing a record
a builder should own. A page that "remembers" is not a bug to fix.

---

## 1. What works

The whole spine: TAB through `MDSNEAK0`, the five pages with their own
colours, the tab column, the row list, the cursor highlight, the two text
flashes, the 3D previews, the examine page's two content paths, the verb
panel, `Utiliser` taking an object in hand and reaching a zone script, and
the device closing itself on a successful use. `engine: sneak`,
`sneak page colour`, `cursor highlight`, `sneak previews`, `sneak examine`,
`engine: used object`, and `engine: UI` (0 disagreements with `tools/sim`
across 31 screens).

## 2. What is left

### 2a. Row scrolling — the one that makes the device WRONG, not just thin

`sub_0049C050` (the row list's hook) and `sub_42AFF0` (`Ui_MoveSelection`
over a WINDOW) are both unmodelled. The row binder's window is **hardcoded
0**, so a list longer than the nine row widgets is **truncated, not
scrolled** — carry ten things and the tenth cannot be reached at all.

* `engine/src/ui/widgets.cpp` 980–1003 — the hook, already read as a thin
  wrapper (`if (sub_42AFF0(screen, list) != 1) return 0;`) and falling through
  to the ordinary move
* `engine/backends/sdl/play.cpp` 5754 — the window, and 5865 — the printf that
  has to stop saying *"no scrolling"*

**This is the first thing to do.** Everything else on this list is a gap;
this one is a device that lies to the player about what he is carrying.

### 2b. `Utiliser sur` does the wrong thing, and it is not a gap

The port records the verb and then **runs the same case-35 decision as
`Utiliser`** — so "use X on Y" takes X in hand. `sub_49BF30` does something
else entirely, read from the listing:

    esi = the selected row's object                 // item[+0x3C]
    if (esi == -1) return 0;
    dword_670BE0 = 1;                               // COMBINE MODE
    if (sub_42B520(esi)) { 670BE4 = esi; 670BE8 = -1; }
    else                 { 670BE4 = -1;  670BE8 = esi; }   // two slots
    dword_670BEC = -1;
    sub_4290D0(&word_4DE318, 0x20000004, 1);        // DISABLE the verb list
    sub_4290D0(...,             ...,      0);

So it enters a mode with **two named slots**, puts the first object in one of
them (`sub_42B520` decides which — unread, and it is the whole asymmetry),
disables the verbs and sends you back to the rows for a second object.

The recipe half is already ported: `Inventory::combine(a, b, gate)` over
`GLOBAL +12`'s 11 symmetric recipes. What is missing is the mode, the two
slots, and `sub_42B520`.

Until it is done, the honest behaviour is to REFUSE `Utiliser sur` rather
than run `Utiliser`'s arm under its name.

### 2c. Three of the five pages have empty rows

Only the inventory page is filled. The player's bio, his statistics and the
memos each ask a different list and **which list each one asks has not been
read** — an empty row says so, where showing the carried list would be a
plausible-looking wrong answer. `play.cpp` 5512 states this.

The memory page's row confirm is refused outright and says why
(`widgets.cpp:869`, *"memory row: its arm is not modelled"*).

### 2d. The identity page's character view

`sub_4778E0` builds a camera from the player's own model at
`kCharacterDistance` = `0x42EC3871` = 3.0 / 0.0254, three metres for a
standing man. The constant is lifted and carried in `ui/models.h` — kept
there precisely because it shares `sub_478DE0` with the item previews and
telling the two apart is what stopped the previews rendering two pixels — but
**nothing draws the character**.

### 2e. Four of the five page builders

Only the inventory page's `panel+4` builder is ported. The other four pages'
colour pushes are READ (the icons above are what they name; identity, memory
and options then blacken the clock item `0x004DEC08` with the single-item
setter) and deliberately NOT shipped, because the inventory page's is the only
one whose function boundary is established rather than inferred from the order
of the listing. `docs/RECONSTRUCTION.md` 2026-09-04 states that limit in all
three of PORTING B2's places; shipping the other four means bounding four more
unlabelled functions first.

### 2f. `Text_LayOutBlock`

Not ported. The composer draws a **labelled reconstruction** — greedy word
wrap, clipped at the box height. Invisible on every earlier screen and obvious
on the sneak's 50-pixel captions. `play.cpp:180` carries the note.

### 2g. The object in the hand is invisible

`sub_41C490` writes `player[+0xA4]` and ALSO attaches the model
(`sub_437400` / `sub_4374E0`). The port does the first and not the second, so
`Utiliser` on a key works and shows nothing. A player reported exactly this.

### 2h. `Object_ApplyEffect` (0x00409780)

**NAMED, body as generated** — read enough to name and no further. The
consumable arm of case 35 calls it, so today the port announces the effect and
its actor property and applies nothing. Its sibling gate — `sub_409780`'s
check of whether an object may be used HERE — has not been read at all
(`play.cpp:5596`).

### 2i. Two screens of the family have never been walked

`Ui_OpenSneakFamily` put screens **0 `VIDEOPHONE`** and **7 `SLIDER`** in the
tree alongside 9 `SNEAK` (all three on `sneak.bmp`). Their panels are lifted;
nothing has driven either. Screen 7 belongs to `todo/sliders.md`; the
videophone has no owner.

### 2j. The slider page's confirm

Does nothing. It is the whole of `todo/sliders.md` — cross-referenced here so
this file is not read as if it covered it.

## 3. Steps

Each ends in a commit and a `verify.py` check SHOWN to fail first.

| # | step | status |
|---|---|---|
| 0 | this file | **done 2026-09-04** |
| 1 | **row scrolling**: read `sub_42AFF0` properly, model the window in the binder, make `sub_49C050` move it. Check: a carried list of >9 reaching its last row, which today is unreachable | |
| 2 | `Utiliser sur`: read `sub_42B520`, port the combine mode, the two slots and the second selection onto the already-ported `Inventory::combine`. Until then, refuse it rather than run `Utiliser`'s arm | |
| 3 | which list the bio / statistics / memo pages ask for, and fill them | |
| 4 | `Text_LayOutBlock` — the real wrap, against a caption that today wraps wrong | |
| 5 | the hand attach, so a used object is visible for the frames it is held | |
| 6 | `Object_ApplyEffect` and its context gate | |
| 7 | the identity page's character view | |
| 8 | the other four page builders, each bounded before it is shipped | |

## 4. Cautions

* **The static records.** See the top of this file. `list+2`, the colour
  bytes, the flag words and `panel+24` outlive the walk; a builder writes
  them and nothing resets them. The device REMEMBERING your last verb across
  a close and reopen is correct behaviour, not a leak.
* **`sub_49BC60` is shared** by the inventory, slider and memory rows and
  dispatches on `dword_670CB8` (`UiWalk::rowKind`). One bug already came from
  changing it for one kind (`ba1c335`).
* **The sneak reads `bits`, not edges.** `Ui_BeginScreen`'s repeat mask
  (0x203F) already edge-filters exactly the bits a screen reads. There are
  three input words with three filters — the mask for a screen, the raw word
  for the world, `dword_90E0E0` for a conversation — and a dialogue is not a
  screen. Do not generalise one to the others.
* **Functions with no `proc` label.** 21 of the 23 colour call sites and 26 of
  30 per-screen callbacks are dwords in tables, so `asmfn.py` silently returns
  a NEIGHBOUR. Disassemble at the address the data names.
* **Look before measuring.** Every one of this device's faults that mattered
  was found by a person watching it, not by a check: the placeholder colour,
  the cursor drawn under the icon, the `{B}` flash being red, the previews
  rendering two pixels, the invisible key.

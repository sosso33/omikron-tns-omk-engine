# 10. The interface

← [Contents](README.md) · prev: [Audio](09-audio.md) · next: [The port](11-the-port.md)

---

## In short

The game's menus are data too. There are **37 screens** — the start menu, the
options, the save and load panels, the shops, the terminals, Kay'l's handheld
device — and each is a row in a table compiled into the executable, naming its
background bitmap, its text file and the callbacks that open and close it.

What a screen *contains* is a tree: a screen owns a panel, a panel owns lists,
a list owns items, and an item is 72 bytes saying where it sits, what it draws
and which flags it carries. **51 panels, 145 lists and 628 items** in all,
lifted to JSON because none of it is in any data file.

Text is drawn from the game's own fonts — 13 of them, keyed by a single ASCII
letter, 2 899 glyphs between them — with a small markup language for colour and
alignment. A glyph's pixels are not colours but **coverage**, 0..31, indexed
into a ramp.

And the whole thing is driven by fourteen input bits shared with the actor
runtime, so a menu and a character are reading the same word.

## In detail

### The screen table

92 bytes a row, 37 rows, and the order is confirmed from the code rather than
assumed — the field at `+4` runs 0..36. Eleven screens name a background bitmap
and all eleven resolve; eighteen name a text file and all eighteen resolve.
Each has three slots and an open/run/close state machine.

The per-screen callbacks are the awkward part: 20 opens and 10 closes, of which
**26 of the 30 are absent from the decompilation entirely**. Not because they
are unusual code — because nothing *calls* them. They are dwords in a table, and
IDA's auto-analysis makes a function where it sees a call. The practical
question when an address the data names has no function is therefore not "does
it start with a prologue" but **"does anything call it"**: over the 33 addresses
this table names, predicting a label from *starts with a push* is right 22 times
and predicting it from *has at least one direct caller* is right 29.

What could be recovered from them was: 20 of 20 opens install a static panel and
reach the common entry, 19 of 20 closes reach the generic close, and the shop
titles name their own screens 8 times in 10 — through a jump table that a
linear scan does not merely miss but reads **wrongly**, binding all ten shops to
one string.

### The answer layer

A screen returns a value through **one global**, and it has **17 writers**. Only
three of them sit inside a function the disassembler labelled, for the reason
above. They are enumerated into the lifted table and attributed to the screens
whose tree serves them, and cross-checked against the corpus: of the 242
`ui.open` sites over 25 screens, 15 keep the answer and 10 discard it, and **no
site is attributed only to screens that discard**.

The terminal family turned out to be a second shop dispatch — seven screens on
one panel, one activate callback, a seven-case jump table on the screen's own
parameter, in which **case 4 falls through into case 6**. Reading the arms
independently misses that.

### The text renderer

Thirteen fonts, each keyed by an ASCII letter, all shipping; 2 899 glyphs, none
outside its file and none overlapping. A pixel is coverage 0..31 into a colour
ramp rather than a colour, and the markup is a small vocabulary — `{f}` picks a
font, `{I…}` an RGB colour, `{X}` and `{B}` alignment and boxing.

Laid out at the coordinates the widget tree gives them, the start menu's four
labels come out **6 132 of 6 132 pixels** identical to the engine's own
framebuffer.

### Input

One shared callback serves all 32 live screens — there are no per-screen input
handlers. It reads the same 14-slot binding word the `.CTL` runtime reads,
edge-filtered by mask `0x203F`, and dispatches panel → list → default.

That word is installed by context: four groups × 14 actions × 3 devices, with
adventure installed before the first frame. Which is how a documented error was
found: the interface bits' "defaults" of E and R are a **static initialiser**
that `Game_Init` overwrites before anything runs — no player ever saw them, and
slots 4 and 5 are ENTER and SPACE. The port keeps the initialiser precisely so
that overwrite stays observable.

### The options, and what they do

74 rows over a 13-page tree, every label and caption resolving in the game's own
text archive, with the read and apply hooks paired. **Page 12 is built and
unreachable.**

They are not cosmetic: chapter 8 has the clip distance sizing four things at
once, and the settings live in the **save file's 3 496-byte header** — so
saving a game saves your options, including all three binding tables verbatim.

### The screens the port drives

The start menu **answers for itself**: type a name, walk to *Confirmer*, and the
answer is derived rather than supplied — type nothing and it answers nothing,
because the engine's own callback refuses an empty field.

Beyond it: the sneak (Kay'l's device) with its inventory rows, its verbs and its
examine page; the options page tree; the **load panel** with its slot rows,
its selection box, its connector and its thumbnail; the **save panel**, where
confirming a slot writes the game and charges a ring; and the **pause screen**,
opened by Escape — which, as chapter 2 says, is not an input binding at all.

## Where it lives

| | |
|---|---|
| the findings | `docs/UI.md` — the screens, the widget tree, the fonts, the input path |
| the tables | `tables/ui.json` (37 screens, 45 sounds, 74 option rows, 13 pages, 8 oscillators, 13 fonts), `tables/ui_widgets.json` (51 panels, 145 lists, 628 items) |
| the port | `engine/src/ui/` — `i2d.*`, `widgets.*`, `screendraw.*`, `text.*`, `options.*`, `iamtext.*` |
| the model | `tools/sim/ui.py`, and `/ui` in the web viewer |
| the checks | `ui tables`, `ui widgets`, `ui answers`, `ui input`, `ui page`, `engine: start menu`, `engine: load panel` |

## What is not settled

* **The per-screen callbacks' own bookkeeping.** What each open and close does
  between the common entry and the flag edits is recovered only in part, and 26
  of the 30 are not in the disassembly at all.
* **Fourteen screens share one result variable**, so no per-screen comparison
  test exists for the answer layer.
* **The twelve per-screen sound slots are not fired by the widget walk yet.**
* **The multiplan screen** has its tile background read and its panel
  behaviour not.
* **The line and triangle 2D primitives cannot be raised from the interface at
  all**: the item vocabulary has no line, and exactly one item in the whole
  lifted tree carries the triangle bits — on a child panel no screen reaches.

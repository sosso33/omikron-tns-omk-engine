# 10. The interface

← [Contents](README.md) · prev: [Audio](09-audio.md) · next: [The port](11-the-port.md)

---

## In short

The game's menus are data too. There are **37 screens** — the start menu, the
options, the save and load panels, the shops, the terminals, the lift, a few
one-off puzzles, and Kay'l's handheld device, the *sneak* — and each is a row in
a table compiled into the executable, naming its background bitmap, its text
file and the callbacks that open and close it.

What a screen *contains* is a tree: a screen owns a panel, a panel owns lists,
a list owns items, and an item is 72 bytes saying where it sits, what it draws
and which flags it carries. **60 panels, 176 lists and 728 items** in all,
lifted to JSON because none of it is in any data file.

Most of what makes a screen *work* is not in that tree but in small native
functions it points at — a list's input hook, an item's draw hook, the
callback a press runs. Many of them are called from nowhere but a table, so
the disassembler never recognised them as code, and each screen the port
brought up meant finding them in the raw image first.

Text is drawn from the game's own fonts — 13 of them, 2 899 glyphs — with a
small markup language, laid out by the engine's own block layout. And the whole
thing is driven by fourteen input bits shared with the actor runtime, so a
menu and a character are reading the same word.

## In detail

### The screen table

92 bytes a row, 37 rows, and the order is confirmed from the code rather than
assumed — the field at `+4` runs 0..36. Eleven screens name a background bitmap
and all eleven resolve; eighteen name a text file and all eighteen resolve.
Each has three slots and an open/run/close state machine.

A screen **hides the world** behind it unless its own record says otherwise
(`+112 & 0x40000`, three screens of the 37), and a panel carrying `0x800` is
drawn over a dimmed world by `Ui_DrawPanelDim` — which is why a shop, the
high-score board and a few others show the street behind them.

The per-screen callbacks are the awkward part: of the 30 opens and closes,
**26 are absent from the decompilation entirely**. Not because they are unusual
code — because nothing *calls* them. They are dwords in a table, and IDA's
auto-analysis makes a function where it sees a call. Over the 33 addresses the
table names, predicting a label from *starts with a push* is right 22 times
and from *has at least one direct caller* 29. The same is true one level down,
of list hooks and item callbacks: the terminal's keypad, the lift's text box,
Gandhar's door, Den's locker, the cartridge panel and the high-score board were
each read from the raw bytes because no function was there to read.

### The answer layer

A screen returns a value through **one global**, and it has **17 writers**.
They are enumerated into the lifted table and attributed to the screens whose
tree serves them, and cross-checked against the corpus: of the 242 `ui.open`
sites over 25 screens, 15 keep the answer and 10 discard it, and **no site is
attributed only to screens that discard**.

The terminal family is a second shop-style dispatch — seven screens on one
panel, one activate callback, a seven-case jump table on the screen's own
parameter, in which **case 4 falls through into case 6**. And the terminal
answers on the way **out**, reporting which protected dossiers were read; the
office's script uses that to set a mission and enable an address.

### The text

Thirteen fonts, each keyed by an ASCII letter, all shipping; 2 899 glyphs, none
outside its file and none overlapping. A pixel is coverage 0..31 into a colour
ramp rather than a colour, and the markup is a small vocabulary — `{f}` picks a
font, `{I…}` an RGB colour, `{X}` and `{B}` alignment and boxing.

Laid out at the coordinates the widget tree gives them, the start menu's four
labels come out **6 132 of 6 132 pixels** identical to the engine's own
framebuffer.

What turns a string and a box into positioned lines is `Text_LayOutBlock`
(0x0043F3E0, 577 lines), and it is **ported**: the wrap, a line pitch of 120%
of the font's height, the alignment, the vertical placement and the counted
spans. Before it the layout was this repository's own guess. It also carries
the section extractor that splits an object's description into its **memo**
and its **clue** — the two bracketed sections 37 of the 1 002 object records
carry, which the sneak and the save screen's hint shop each show one of.

### Input

One shared callback serves all 32 live screens. It reads the same 14-slot
binding word the `.CTL` runtime reads, edge-filtered by mask `0x203F`, and
dispatches panel → list → default. Its head gates the two exits on the panel's
own flags: **TAB closes** only a panel carrying `0x20`, and **BACK** pops a
child panel or closes a top one carrying `0x10`. Taking BACK unconditionally
was wrong on the start menu, the pause and the options as well as in the shops.

The binding word is installed by context: four groups × 14 actions × 3
devices, with adventure installed before the first frame. The interface bits'
documented "defaults" of E and R are a **static initialiser** that `Game_Init`
overwrites before anything runs; slots 4 and 5 are ENTER and SPACE.

### The sneak

Kay'l's device is screen 9, opened by a key through the `.CTL` machine — a
special move, not a menu call — and it is most of the interface a player
touches. All of the following runs in the port:

* **the inventory rows and the verbs.** *Utiliser* announces the object to the
  world as a message and decides whether it may be used: a key goes **in hand**,
  a consumable is **applied** (a medkit adds its amount to *Vie*, clamped, and
  leaves the bag). *Utiliser sur* is not a use at all but a **combine** of two
  carried rows — box and key make the open box — with its success sound and
  the verb's flash put out when it closes. *Examiner* shows the examine page and
  posts world message 4, which a global script answers;
* **the echo bar**, which shows the *selected* item's text: the setek and
  anneau counters are read there, from the player record's `+172` and `+174`
  — the first of which is the money the shops charge;
* **the memory page, which is the memo journal**: a list of memos and a reader
  page, each showing the memo section of its description and not the clue.
  It had been recorded as empty *by the code*; a reader who had played the
  original said it was not, and the count it reads turned out to be a list's
  own field rather than a global nothing wrote;
* **the identity page**: the character's details from the save, the
  characteristics as bars with a combat rank word, and his model posed and
  turning — confirmed in play;
* **`Lire plan`, the city map**: a panel nothing in the widget table points at,
  found through the code that installs it; it bounces back unless the resident
  set has a map bitmap, which only the four cities do, and draws the player's
  heading and a marker per enabled destination;
* **the slider page** (chapter 6), and **the call**: the videophone screen that
  opens during a conversation answers its own question the moment it opens,
  and the caller is a real actor parked off-stage.

The options tab hosts screen 35, which the port does not have as a screen, and
the quit tab's page is **built and unreachable** in the original — the same
shape as options page 12.

### The screens the world opens

* **The start menu answers for itself**: type a name, walk to *Confirmer*, and
  the answer is derived rather than supplied — the engine's own callback
  refuses an empty field.
* **The shops** — ten screens, one dispatch. The stock is the area's own `+8`;
  a row buys, sells at half or examines depending on the selected button;
  *Analyser* shows the item in 3D. Confirmed in play.
* **MULTIPLAN**, the most-opened screen of all (82 sites): a storage locker
  shared by every terminal, moving objects between the sneak and it.
  Confirmed in play.
* **The save screen** holds a second page, the **hint shop**: it sells the
  clue section of your memos, and the price is not a constant in the
  executable — the event broadcasts a message, and `IAM\GLOBAL`'s one handler
  sets it to **three anneaux**.
* **The load and save panels**, the thumbnail, the ring a save costs.
* **The pause screen**, opened by Escape, which is not an input binding
  (chapter 2).
* **The security centre's lift**: a 7-slot grid answering `slot − 1` (slot 0,
  the only floor above ground, answers 6) with a box listing the hovered
  level's offices from `IAM\Lift`.
* **The terminals**: a keypad walked by its own hook, a display that names the
  screen's text, and a header showing the label of the cell under the cursor.
* **The one-off screens**: Gandhar's door (a 6 × 6 grid where four cells must be
  marked, in any order), Den's locker (four wheels; the combination **7 2 1 3**
  is compiled in), the cartridge panel before Xendar's door (symbols stepped
  along a ring *out of numerical order*, code 10 14 7 9), and the shooting
  range's high-score board, whose rows live in the save file's settings header.

### The options, and what they do

74 rows over a 13-page tree, every label and caption resolving in the game's
own text archive, with the read and apply hooks paired. **Page 12 is built and
unreachable.**

They are not cosmetic: chapter 8 has the clip distance sizing four things at
once, and the settings live in the **save file's 3 496-byte header** — so
saving a game saves your options, including all three binding tables verbatim.

## Where it lives

| | |
|---|---|
| the findings | `docs/UI.md` — the screens, the widget tree, the sneak, the shops, the special screens, the fonts, the input path |
| the per-task records | `todo/missing-ui.md`, `todo/sneak.md`, `todo/shops.md`, `todo/multiplan.md`, `todo/text-layout.md` |
| the tables | `tables/ui.json` (37 screens, 45 sounds, 74 option rows, 13 pages, 8 oscillators, 13 fonts), `tables/ui_widgets.json` (60 panels, 176 lists, 728 items), `tables/city_maps.json` |
| the port | `engine/src/ui/` — `i2d.*`, `widgets.*`, `screendraw.*`, `text.*`, `options.*`, `citymap.*`, `iamtext.*` |
| the model | `tools/sim/ui.py`, and `/ui` in the web viewer |
| the checks | `ui answers`, `ui input`, `ui geometry`, `ui page`, `engine: sneak verbs`, `engine: sneak memos`, `engine: sneak map`, `engine: shop open`, `engine: multiplan transfers`, `engine: hint shop`, `engine: lift`, `engine: terminal family`, `engine: gandhar door`, `engine: den locker`, `engine: xachen`, `engine: high score` |

## What is not settled

* **The per-screen callbacks' own bookkeeping** is recovered only in part, and
  26 of the 30 are not in the disassembly at all.
* **Fourteen screens share one result variable**, so no per-screen comparison
  test exists for the answer layer.
* **Screen 35**, the options screen the sneak's tab hosts, is not ported; the
  port has the options page tree as a model instead.
* **Not yet played**: the newest screens — the lift's box, the terminals'
  header, Gandhar's door, Den's locker, the cartridge panel, the high-score
  board, the hint shop, the echo bar and the city map — are committed, checked
  and listed in `todo/play-test.md` as awaiting a person.
* **Filed and not done**: a tab strip should switch pages on the move rather
  than the confirm; the overwrite dialog should name the slot; the start menu's
  *Quitter* is not the real exit yet; the combine's failure text on the echo
  bar.
* **The line and triangle 2D primitives cannot be raised from the interface at
  all**: the item vocabulary has no line, and exactly one item in the whole
  lifted tree carries the triangle bits — on a child panel no screen reaches.

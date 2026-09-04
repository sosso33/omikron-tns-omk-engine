# The interface — I2D, the screens, and the options menu

The UI is the one subsystem with **no shipped file that describes it**. There
is no `.SCX` for a menu and no `.CTL` for a widget: everything about the
interface — which screens exist, what each row of the options menu says, which
sound a confirm makes — is compiled into `gamedata/Runtime 2.exe`. So the standard
here is *read and explained*, and the checks have to be built out of the
cross-references between the compiled tables and the shipped tree.

There turn out to be plenty. Three tables carry the whole subsystem, each is
walked by a loop in the binary that names its own base and bound, and each
points into files that ship:

| table | where | records | the check it has to pass |
|---|---|---|---|
| the **screens** | 0x004CB640, stride 92 | **37** | its 11 bitmap names are exactly the 11 files in `gamedata/I2D/bitmaps`; its 18 text-file names are all in `gamedata/IAM` |
| the **interface sounds** | 0x004D0990, stride 20 | **45** | ids 0..44 contiguous, and all 45 named `.wav` files ship |
| the **options items** | 0x004DA574, stride 140 | **74** | every label and every choice caption resolves inside `gamedata/IAM/Options` |

`tools/ui_tables.py` reads all three (`screens`, `options`, `sounds`,
`--selftest`) and also the artwork — `bitmap()` for a sheet and
`panel_background()` for what `Ui_DrawPanelBack` actually composes out of it;
`verify.py: ui tables` runs the checks. `tools/fnt.py` reads the fonts,
`tools/uitext.py` renders a line with them, and `tools/sim/ui.py` walks the
widget tree.

---

## 1. I2D — the 2D layer

`LIBI2D` and `libpoly2d/gereaff.c` are the engine's own names for it (both
appear in `C:\Omikron\Sources\...` strings sprintf'd into DirectX error
messages). It is **not** part of the 3D path: every I2D primitive ends in an
`IDirectDrawSurface::Blt` onto the back buffer.

### The display list

`I2D_Enqueue(drawfn, payload, layer)` (0x004284B0) links a 16-byte node
— `[0]` the draw callback, `[1]` its payload, `[2]` next, `[3]` the layer —
into a single list kept sorted by layer, with a per-layer cache so an insert is
O(1) once a layer has been used. **16 layers, 4862 nodes.**

> **Correction, 2026-09-01: it is a per-layer HEAD cache, not a tail cache**,
> and the difference is visible on screen. The assembly writes
> `dword_4E97B8[layer]` on both exits of the list walk (`loc_42850C`,
> `loc_428524`) and **not** on the cache-hit path (`loc_428534`), which only
> does `new->next = cached->next; cached->next = new`. So the cache holds the
> *first* node of a layer for ever and every later node is spliced in
> immediately after it: **within one layer the first primitive submitted draws
> first and the rest draw in reverse submission order.** Across layers the list
> is still sorted, which is the property the layer exists for; inside a layer,
> submission order is not draw order.
>
> One more quirk falls out of it. The frame's very first node takes
> `I2D_Enqueue`'s `if (!count)` arm, which sets the head and does *not* set the
> cache — so the second node on that layer goes through the walk, is inserted
> **before** it, and becomes the cache. That layer's order is
> `s1, s(n-1), …, s2, s0`, where every other layer's is `s0, s(n-1), …, s1`.
> Both are asserted in `verify.py: engine I2D`; a real tail cache scores 724 of
> 1407 there instead of 1407.

**And the 4862 is derived, not arbitrary**: it is exactly the sum of the seven
pool capacities below — 4096 + 200 + 100 + 220 + 220 + 16 + **10** — so the
list can never fill before the pools do. Reaching that identity needs the last
one, which belongs to a primitive nothing calls (see below).

Each primitive pushes its payload into its own fixed pool and enqueues the
matching drawer. A point is three ints, so a line is 2 of them, a triangle 3, a
quad 4 — and a blit's "rectangle" is two corner points, which is why the
readers take a1[0], a1[1], a1[3], a1[4] and step over [2] and [5]:

| primitive | payload | pool cap | drawer |
|---|---|---|---|
| `I2D_DrawLine` 0x00428430 | 28 B — 2 points + flags | **4096** | sub_4822F0 |
| `I2D_DrawTriangle` 0x00428560 | 40 B — 3 points + flags | 200 | sub_4806C0 |
| `I2D_SubmitQuad` 0x004285E0 | 52 B — 4 points + flags | 100 | sub_480BD0 |
| `I2D_BlitSurface` 0x00428850 | 52 B — 2 rects + surface | 220 | sub_480F60 |
| `I2D_BlitBitmap` 0x004287A0 | 56 B — 2 rects + cache entry + flags | 220 | sub_4810D0 |
| `I2D_Submit3DView` 0x00428900 | 84 B — rect + scene + camera | **16** | sub_4812E0 |
| `I2D_BlitFullScreen` 0x00428780 | none | — | sub_481170 |

**Two more primitives ship and nothing calls them**: `sub_428660`
(0x00428660, a 24-byte payload, pool cap **10**, drawer `sub_481000`) and
`sub_4286F0`. The second carries a latent overflow — it checks the *bitmap*
counter (`dword_4E97B0`, cap 220) and then writes into the 10-entry pool at
`dword_4E9784`, incrementing that instead. It has no callers, so the bug never
fires; the port keeps it rather than quietly correcting it.

Each blit submitter rejects a degenerate rectangle before enqueuing, so a
mis-built payload is dropped rather than drawn — but it is **three** tests, not
four: `dst.x0 >= dst.x1`, `dst.y0 >= dst.y1` and `src.x0 >= src.x1`. **There is
no test on the source Y**, identically in `I2D_BlitBitmap` and
`I2D_BlitSurface`, so a source rectangle inverted only vertically is accepted
and submitted. Asserted rather than tidied away (`verify.py: engine I2D`):
adding the missing fourth test takes that count from 2 to 0. The flags word is DirectDraw's: bit 0 becomes `DDBLT_KEYSRC` and
bit 1 `DDBLT_KEYDEST` on top of the constant `DDBLT_WAIT` — **colour-key
transparency, not alpha**. `I2D_Submit3DView` is the odd one: it calls
`Scene_SetActiveCamera` and renders a 3D scene into a 2D rectangle, layered
against everything else, which is how the videophone and the terminals put a
live view inside a panel.

> **Correction, 2026-09-01: a point's third component is the COLOUR, and the
> third argument is FLAGS.** This table read `{x, y, z}` and "N points +
> colour" until the drawers were ported. Both are wrong:
>
> * the submitter stores its third argument one dword **past** the points — at
>   6, 9 and 12 for the line, triangle and quad — and neither back end treats
>   it as a colour. Direct3D takes **SHADEMODE** from bit 3, **FILLMODE** from
>   bit 4 and **ALPHABLENDENABLE** from bits 0..2; the software back end takes
>   its blend mode from the same bits;
> * the colour is **per vertex**, in each point's third dword. `sub_480BD0`'s
>   software branch builds four `D3DTLVERTEX`-shaped records
>   `{x, y, 1.0, 1.0, colour, 0, 0, 0}` from exactly that field, and the
>   software line rasterizer reads point 0's.
>
> So a "z" was never stored: these are 2D primitives and the slot carries their
> colour. `verify.py: engine I2D prims`.

### The primitives have TWO back ends, and the second is software

`sub_4822F0`, `sub_4806C0` and `sub_480BD0` each open with
`if (sub_45EF50() == 2)`. The selector is a **display-driver index**:
`sub_43A6D0` hands a real DirectDraw GUID to mode 0 and the last two synthetic
entries of the device list to modes 1 and 2, so **mode 2 is the engine's own
software rasterizer** and is user-selectable rather than dead.

What it does is not what the D3D path does:

* a **triangle is a wireframe** — three calls to the line rasterizer
  `sub_48C4C0`, no fill;
* a **quad is an axis-aligned rectangle fill** of the four points' bounding
  box (`sub_48C060`), with the colour taken from **vertex 0 alone** — no
  gouraud, whatever SHADEMODE the other path sets. Four blend loops: opaque,
  50% (`((0xF7DE & c) >> 1) + ((0xF7DE & dst) >> 1)`), per-channel saturating
  add, and per-channel saturating subtract. **Confirmed against the engine's
  own framebuffer**: the load panel's selection box is four mode-1 quads of
  thickness `I2D_ScaleX(1)` = 1, and the ported fill reproduces it at 1518 of
  1518 pixels — which also pins the half-open x span, since that single 1
  becomes **2-pixel horizontal bars and 1-pixel verticals**. The "connector"
  to the save thumbnail is another such quad, not a line: 2 rows by 69
  columns, reproduced 138 of 138;
* the line is a clipped integer Bresenham, whose **clip is asymmetric** — it
  processes endpoint 0 first using running values, so a clipped segment drawn
  the other way round is not always the same pixels, while an unclipped one
  always is.

The three 565 conversion tables are built by `sub_440A20` from the surface's
own channel **masks**, so the conversion follows the pixel format; 565 is the
instance the shipped path produced. The masks the blends use are those tables'
own last and middle entries.

`I2D_Flush` (0x00428B00) is the whole 2D pass and it is one line of the frame:
clear render state **14** (`D3DRENDERSTATE_ZWRITEENABLE`), walk the list front
to back calling each node, set it again, then zero every pool counter. The
state is cached in `dword_8F56D8` so the pair costs one call each way. `Game_Tick` calls it once
per frame with the argument that also draws the HUD (suppressed while the
player actor is in state 9 or 17 — a screen is up, or he is dead).

### Bitmaps

`I2D_LoadBitmap(name)` (0x00428A20) keeps a 264-byte record per file —
`char[256]` name, the DirectDraw surface at +256, the next record at +260 — on
one global list. `I2D_CreateSurfaceFromBmp` does the `LoadImageA` /
`CreateSurface` / blit, and `I2D_ReloadBitmaps` redoes every one of them after
a device loss. Nothing reference-counts: two screens naming `boutiq.bmp` load
it twice.

### The four flag families

A flag constant is `bank | bit`, and the bank picks which word it lives in —
which is why one helper can serve structures of different shapes:

| helper | banks | word | the structure |
|---|---|---|---|
| `I2D_TestFlag` / `I2D_SetFlag` | 0x20000000/0x40000000/0x80000000 | +48/+52/+56 | a **row widget** (72 bytes) |
| `sub_429080` / `I2D_SetFlag16` | 0x20000000/0x40000000 | +16/+20 | the **live page** |
| `sub_4291E0` / `sub_429230` | 0x20000000/0x40000000 | +72/+76 | |
| `UI_TestScreenFlag` / `UI_SetScreenFlag` | 0x20000000/0x40000000 | +112/+116 | a **screen slot** (124 bytes) |

*(The first two banks were written `0x20` and `0x40` here until 2026-09-01;
`I2D_FlagBank` tests `0x20000000` and `0x40000000`.)*

A constant naming **no** bank is silently dropped — `I2D_FlagBank` returns -1
and `I2D_SetFlag`'s three-way `if` simply falls through — so "every shipped
constant resolves" is a property the data could fail. It does not: the widget
tree carries **139** of them, 87 applied by an item and 52 broadcast by a list,
and all 139 resolve, splitting **42 / 95 / 2** across the three words. All
three words are also in use in the shipped item records (24 / 357 / 190), so no
bank is theoretical.

`I2D_FlagBank` returns which of the three a constant addresses, and
`I2D_SetFlagOnAllRows` broadcasts one over every row of a page.

---

## 2. The screens

**37 records of 92 bytes at 0x004CB640**, and the walk ends exactly on the
string `aNoOne_45`: `UI_LoadScreen` (0x00429BB0) scans it comparing **+4**
against the id it was handed, and +4 runs **0..36 in table order**, so the
record index *is* the `ui.open` operand.

That settles something [`SCRIPT_VM.md`](SCRIPT_VM.md) §70 had to leave as an
inference. The screen names were read off the contiguous string area and
confirmed three ways from the *data* (11 bitmaps, 57/57 shop sites, 0 uses of
the five `(ELIMINE)` indices); the table says the same thing from the *code*
side, and fills in the two rows that carried no name string of their own —
**13 is `DEN` and 24 is `BAR`**, exactly what the script sites had implied.

| off | field |
|---|---|
| +0 | the screen's label (`VIDEOPHONE`, `OMK START MENU`, `TRANSCAN (ELIMINE)` …) |
| +4 | the id — its own index |
| +8 | a **fixed parameter** that overrides the caller's, or -1 |
| +12 | the artwork, opened as `I2d\bitmaps\%s` |
| +16 | the text file, opened as `IAM\%s` by `UI_LoadScreenText` |
| +20 / +24 / +28 / +32 | open / tick / input / close callbacks |
| +36 | **twelve sound ids** (§3), copied to the instance by `UI_BindScreenSounds` |
| +84 / +88 | the two flag banks |

**+8 is what lets ten shops share one screen.** `BANK`, `PHARMACIE`,
`ARMURERIE`, `RESTAURANT`, `BAR`, `SORCELLERIE`, `LIBRAIRIE`, `SEX-SHOP`,
`DIVERS` and `LIB. LAHOREY` all name `boutiq.bmp`, the text file `Buy` and the
same four callbacks; they differ **only** in +8, which runs 0..9. The same
trick runs `SNEAK`/`SLIDER`/`VIDEOPHONE` (0/1/2 over `Sneak`) and the three
`SURV` screens (4/5/6 over `Surv`) — which is why one 44-string `IAM\SNEAK`
serves three interfaces, the point [`FILE_FORMATS.md`](FILE_FORMATS.md) §5b4
makes from the other end.

**Only three screens can be open at once.** The instances are 124 bytes at
0x004E9818 and there are exactly three slots; every loop over them in the
binary is bounded at 0x004E998C.

### The state machine

`UI_TickScreens` (0x00429F40) does nothing at all while no slot is in use.
Otherwise it dispatches on the instance's **state word at +8**:

```
1  opening   call the open callback (+12)
2  running   if the flag bank wants input, latch it at +108 and call +16;
             if that left the state at 3, fall straight into the close
3  closing   call the close callback (+20)
```

`UI_CloseScreen` finds a slot by id and calls its close callback; `UI_FocusScreen`
moves the input focus, parking the previously-focused slot at the new one's
+44 so it can be handed back.

Two flags in +84 have visible consequences:

* **0x20040000 — do not suspend the player.** Without it `UI_LoadScreen` puts
  the player actor into `ACTOR_STATE` 9 for as long as the screen is up,
  saving the old state at +408. Exactly three screens set it: `PAUSE GAME`
  and the two `SHOOT` screens — the ones you are still playing during.
* **0x20000400 — tell `PAUSE GAME` about it.** Set on `OMK START MENU` and
  `SAVE GAME`; opening either fires screen 31's open callback.

---

## 3. The interface sounds

**45 records of 20 bytes at 0x004D0990** — `{i32 id, char[16] stem}` — and the
walk lands exactly on `aNoOne_0` (900 bytes, 45 × 20). Ids are 0..44 with no
gaps, and **every one of the 45 stems ships** in `gamedata/I2D/sounds`.

`UI_CacheScreenSounds` (0x00482F30) runs the screen's twelve slots, resolves
each id here and loads `i2d\sounds\%s.wav` into a 32-entry cache.

The slots are positional, and reading them across the 37 screens shows it:
**slot 0, 1 and 2 are always the `002`, `003` and `001` of one family** —
`SNK002/003/001` for the sneak screens, `arc002/003/001` for the terminals and
shops, `men002/003/001` for the menus, `gan`, `den`, `asc` for the ones with
their own set. The later slots are per-screen: the shops add `achat` at slot 7,
`SAVE GAME` adds `savgrd`, `PAUSE GAME` adds `pause`.

**WHICH slot is which is `sub_482FE0` (0x00482FE0), and this paragraph used to
get all three wrong.** It said *"So slot 0 is the selection move, slot 1 the
confirm and slot 2 the screen opening"* — but the family pattern above
establishes only that the three slots hold one family's `002/003/001`, and the
word "So" carried an inference the evidence never supported. The dispatcher
reads the live input word at screen `+108` and picks the slot by BIT:

| input bit | §3c's meaning | slot played |
|---|---|---|
| `0x10` | confirm | **slot 0** |
| `0x20` | back | **slot 1** |
| `0x0F` | the four directions — a selection move | **slot 2** |
| `0x2000` | close | **slot 3** |

So the order is **confirm, back, move, close**, and there is **no screen-
opening sound at all**. For `OMK START MENU`, whose slots are `1, 2, 0`, that
makes `men002` the confirm, `men003` the back and `men001` the selection move.

**Corrected 2026-09-01, by PLAYING.** The replica wired the slots as this
paragraph described and a reader reported that moving the selection played the
validation sound while confirming played what sounded like a refusal — which is
exactly what confirm-at-slot-0 and back-at-slot-1 produce when read as move and
confirm. No check here could have caught it: the names, the counts and the
cache all agree with each other whichever meaning is attached.

Sixteen `.wav` files ship that no record names — `arc004..006`, `asc004/005`,
`FS004..006`, `MUL001..006`, `SNK010`, `cptrebour01/02`. Note `MULTIPLAN` uses
the `arc*` set despite `MUL*` shipping: a revision the table was not updated
for, not a decode gap.

---

## 3b. The widget tree — how a screen actually draws

> **The records' `x`, `y` and `h` are NOT what a screen shows** (found
> 2026-09-01). Each screen's open callback walks its lists through three
> helpers and overwrites them:
>
> | helper | writes | meaning |
> |---|---|---|
> | `sub_4295C0(list, x)` | every item's `+0` | the X |
> | `sub_429650(list, h)` | every item's `+6` | the height |
> | `sub_429680(list, firstY, step)` | every item's `+2` | the Y, stepping |
>
> Screen 29's callback lays out **five** lists. Its start menu goes to
> **y 120 step 80** where the records say 150 step 60 — and the engine's own
> capture has the four labels at **127/208/290/370**, which is the callback's
> spacing, not the record's. Its confirm dialog's `Confirmer` and `Annuler`
> both **ship at y=330** and are separated to **260 and 320**; drawn from the
> records alone they land on top of each other.
>
> This is the same shape as the flag edits below — the callback is where a
> screen is finished — and it was missed for the same reason: the records
> parse cleanly and nothing contradicted them. A reader reported the two
> buttons overlapping. `tables/ui_widgets.json` now carries a `layout` per
> list, and `verify.py: menu layout` compares the COMPOSER against the
> capture, which is the comparison that did not exist: `engine: screen` pits
> the reference against the live window (both ours) and `engine: frame` tests
> `uitext.py` rather than the composer, so neither could fail. Without the
> layout pass **0 of 4** rows land near the capture's; with it, 4 of 4.

> **THE MENU'S ANIMATED BACKGROUND** (found 2026-09-01). `gfxint.bmp` is the
> title on palette index **255** — rgb(4,4,4) — and nothing else: every sampled
> pixel outside the title glyphs is that one index, and it is the I2D colour
> key. So the sheet is TRANSPARENT and a lower layer shows through.
>
> Screen 29's open callback calls `sub_4B19C0`, which loads
> `IMAGES\CLOUD.BMP` (256×256 greyscale), mallocs `0x20000` = 256·256·2 and
> makes a 640×480 offscreen surface. Per frame `sub_4B1B00` runs **two passes**:
>
> 1. **Emboss the cloud at 256×256** into that work buffer. A light at
>    `(cos t·64 + 128, sin t·64 + 128)`, `t` stepping 0.0785 a frame — an
>    80-frame turn — gives two weights that decrement, one per pixel and one
>    per row, from `255 − light`. Over 256 they stay small, which is why it
>    never saturates. Each pixel dots the cloud's x and y gradients — taken as
>    **signed bytes** — with those weights, `>> 5`, `+ 32`, clamps to 0..63 and
>    looks up the ramp.
> 2. **Resample that buffer to 640×480 through the warp.** `sub_4B1F40` fills a
>    480-entry ROW table and a 640-entry COLUMN table of cosine sums, and they
>    are used **crossed**:
>
>    ```
>    al = rowTab[479 - y]        then `inc al` per pixel   -> the X source
>    ah = colTab[639 - x] + y    `bl` being a per-row inc  -> the Y source
>    out = buf[ah * 256 + al]
>    ```
>
>    This is the wave. The buffer is 256 wide against a 640-wide screen so it
>    repeats — but each row is displaced horizontally and each column
>    vertically, which breaks the repeats up instead of lining them into
>    squares. `cloud.bmp` is itself seamless (its step across the wrap, 0.6, is
>    a normal interior step, 0.7), so nothing else introduces an edge.
>
> > **Four attempts, three of them written into this document as fact.** The
> > tables were first read as per-pixel offsets into the CLOUD on their own
> > axes — a smooth flow. Then, when a window of the decompiled renderer showed
> > no read of them, they were dropped and this section said *"there is no
> > warp"* — flat tiling. Then applied crossed but still to the cloud, in one
> > pass — hard 256-pixel seams from the light weights wrapping. A reader
> > watching the original rejected each in turn: *"deformed squares"*, *"it
> > should create a wave feeling"*, *"normal squares are not supposed to be
> > recognizable"*.
> >
> > What settled it was reading the **raw assembly of the whole 382-line
> > function** rather than a decompiled excerpt. The second pass is there in
> > plain `mov ah, byte_6A0630[edx*4]` — and the decompiler had named those
> > reads off *neighbouring* addresses, so every grep for the tables' own
> > symbols came back empty and produced the confident, wrong "it never reads
> > them". **"I could not find a read" is a fact about the search.**
>
> The ramp is built from constants in the image, not from the .bmp — whose own
> palette is greyscale. Two halves of 32 from `0x004B22D0`, and the packing
> settles a reading the disassembly alone does not: `Color_Sum` takes a
> **COLORREF**, so the low byte is RED. Read as `0xRRGGBB` the ramp is
> blue-to-olive and matches nothing; read correctly it runs **rust → dark →
> teal**, and **97%** of the captured menu's background pixels land within 24
> of one of its entries.
>
> **The light weights wrap as signed bytes**; left as full ints they reach
> −449 across a row and drive the emboss into its clamps, and the capture's
> own statistics are what settle it:
>
> | | median luma | p90 | at the rust extreme |
> |---|---|---|---|
> | the engine's capture | 17 | 29 | **0.0%** |
> | full-int weights | 29 | 57 | 7.1% |
> | byte-wrapped | **17** | 26 | **0.0%** |
>
> **Open**: in the 640×480 captures rows ~0–150 are a static dark band (luma
> 12.9, identical across all three frames while the middle animates) even
> though the sheet is transparent there — and a reader's own 800×600
> screenshot of the game has the cloud running behind the title. Something
> confines the effect vertically at this resolution and it has not been traced.
> `verify.py: menu cloud`.

**Four levels**, and the reason there are four *different* flag-helper pairs
(§1) is that each level has its own flag words. The bank in a flag constant
picks the word, so the same constant means different things at different
levels — which is exactly why `0x40008000` is "animating" on a panel and
something else on an item.

```
screen   124 B, three slots at 0x004E9818        flags +112 / +116
  panel  one being drawn (+28), one leaving (+32)      +72 / +76
    list up to ten, at panel+32                        +16 / +20
      item 72 B                                  +48 / +52 / +56
```

`Ui_DrawScreen` (0x00475A50) is the definition table's `+32` callback for **34
of the 37 screens** — the two `SHOOT` screens and `HIGH-SCORE` bring their
own — so it is the generic renderer, and walking it is what establishes the
tree. The full field map is in `readable/types.h`.

### The panel slides, the items don't

A panel carries a start offset at `+88/+90`, a delta at `+92/+94`, a duration
at `+96` and an elapsed at `+100`. Each frame `Ui_DrawScreen` advances the
elapsed by the frame delta and sets the **current** offset at `+84/+86` to
`start + delta × elapsed/duration`, clamping at the end. `Ui_ItemScreenX/Y` —
two four-line functions — then add that offset to every item's own coordinate.

So a whole page flies in from **one pair of fields**, and `Ui_SlidePanelFrom`
picks the direction from a small enum: 1 from above (−480), 2 from below
(+480), 3 from the right (+640), 4 from the left (−640), 0 no move. Items have
their own motion too (`+64/+66` target, `+68` speed, stepped straight-line or
per-axis) but the menus barely use it.

A screen keeps a **second** panel at `+32` while a transition runs, and
`Ui_DrawScreen`'s body is written out twice for it: incoming first, outgoing
after.

### The background is a tile map

The screen's artwork is one 640×480 sheet. With flag `0x40004000`
`Ui_DrawPanelBack` blits the whole thing full-screen; **without it,
`panel+20` is 80 tile ids** — a 10-wide by 8-deep grid of 64×64 tiles, each id
selecting the source cell `(id % 10, id / 10)`.

The arithmetic is the check. 10 × 64 is exactly 640, and seven rows of 64
leave 32 — which is precisely the case the function hard-codes, row 7 drawing
at half height from source y 448..480. **All eleven shipped bitmaps are
640×480**, so the grid tiles every one of them exactly. That is what lets
`boutiq.bmp` serve ten different shops and `gfxint.bmp` all the menus: one
sheet, a different 80-byte map per panel.

### Focus, and why only one item has it

`Ui_DrawList` (0x00476340) rewrites two bits on every item every frame.
`UIF_SELECTED` goes on whichever item is that list's own `+2`; `UIF_FOCUSED`
additionally requires the list to be the panel's **current** one (`panel+32`
indexed by `panel+24`). So however many lists a panel carries, exactly one item
on screen is focused — and it is cached at `screen+40` for the input code.

### An item is a bag of bits, not a type

There is no widget class. `Ui_DrawItem` (0x004764A0) draws the text and then
tests one flag per decoration, each its own primitive:

| flag | what | drawn as |
|---|---|---|
| `0x40000010`, `0x44000000` | `Ui_DrawItemFill` | a quad at layer `+11 − 2` — see below |
| `0x40000080` | `I2D_DrawRectOutline` | four 1-pixel quads |
| `0x40000200` | `Ui_DrawItemCursor` | only while focused |
| `0x403C0000` | `Ui_DrawItemArrows` | up to four triangles, 20 px outside the box |
| `0x40000400` | `Ui_DrawItemMarker` | the ▶ at the left of the focused row |
| `0x40000100` | `Ui_DrawItemSprite` | a lit/unlit pair off the sheet |

The arrows guard is `0x403C0000`, which is **exactly the OR** of the four bits
`Ui_DrawItemArrows` then tests one at a time — the kind of agreement that says
the bit assignment is read right rather than fitted to what looked plausible.

### The fill's blend is the INVERSE of source-over

`Ui_DrawItemFill` (0x00476FE0) puts a flat quad over the item's own scaled
rect — `sub_480BD0` copies vertex 0's colour to all four when the record's
flag bit 3 is clear, so one colour covers it. The colour is the item's
`+8/+9/+10`, or (255, 50, 50) on the `0x42000000` arm, and the alpha is
**200** on the plain `0x40000010` arm.

The blend is the part that matters and it is not the obvious one.
`sub_480AC0`'s mode-4 arm sets D3D render states 19 and 20 to **6 and 5** —
`SRCBLEND = INVSRCALPHA`, `DESTBLEND = SRCALPHA` — so

```
result = src * (1 − a) + dst * a
```

and a **large** alpha makes the quad FAINT, not solid. Drawn the usual way
round every fill in the game is roughly four times too bright.

**Checked against a picture of the original.** 209 of the tree's 222 fill
items ship the colour (255, 0, 0), which is a placeholder rather than a
colour — so most screens cannot test the rule. The **LIFT**'s description
panel can: item 0x004E5078 at (15, 360) 475×105 carries (80, 122, 118) over
artwork that is black there.

| | |
|---|---|
| the rule predicts | (17.3, 26.3, 25.5) |
| a screenshot of the running game measures | **(15, 25, 25)** |
| `engine/` composes | (16, 24, 24) — the RGB565 quantisation |

`verify.py: fill colour`. Drawn as source-over the same item gives
(63, 96, 92).

### What the placeholder means: a page paints itself in its TAB ICON's colour

209 of the 222 fills ship (255, 0, 0), so the record is a placeholder and the
colour is written at run time. **What writes it is a pair of setters two lines
long**, and they had never been found because of where their callers live:

| | |
|---|---|
| `sub_4296B0(item, r, g, b)` | `item[8]`, `item[9]`, `item[10]` — 3 sites |
| `sub_4296D0(list, r, g, b)` | the same three on **every** item of the list — count at `+0`, item array at `+12`, the pair `sub_42AAE0` also walks — 23 sites |

**21 of those 23 sites sit in a function with no `proc` label**, because every
one of them is a panel or list callback: a dword in the widget tree, which is
exactly the class CLAUDE.md §1 records IDA does not recognise as code. A
search through named functions finds nothing, and that is what happened.

**What they are handed is a tab icon's own colour.** The sneak's five pages
are five panels sharing one icon column — list 0x004DE210, every item at
x = 15 — and those five records carry the five page colours:

| icon | y | colour | page |
|---|---|---|---|
| 0x004DDFB0 | 60 | (20, 165, 250) | Identity |
| 0x004DDFF8 | 129 | (25, 240, 115) | Slider |
| 0x004DE040 | 207 | (240, 135, 15) | Inventory |
| 0x004DE088 | 281 | (255, 240, 95) | Memory |
| 0x004DE0D0 | 356 | (255, 100, 70) | Options |

Each page's **`panel+4` builder** — a callback slot the walker did not read
until now; it lifts `+16`, the input hook — pushes the bytes of *its own* icon
by address. For the inventory page the builder is **0x0049B710**, named by
panel 0x004DEE50 `+4` at file offset 0xDDA54, and it ends:

```
sub_4296D0(0x004DE6F0, byte_4DE048, byte_4DE049, byte_4DE04A)   the 9 rows
sub_4296D0(0x004DE318, ...)                                     the 3 verbs
sub_4296D0(0x004DEC58, ...)                                     echo + clock
```

`byte_4DE048` is icon 0x004DE040 `+8`. The other four pages do the same from
their own icons, and identity, memory and options then blacken the clock item
0x004DEC08 with the single-item setter, which the inventory page does not.

**Confirmed against five captures of the original**, 2026-09-04: the inventory
page draws amber, the slider page green, the identity page blue — each
matching the icon beside it. Fitted over 15 channel samples from 3 hues and 3
pages, `measured = 0.19 × source + 11`, where the fill rule predicts a slope
of 55/255 = **0.216** and source-over **0.784**; the offset is the capture's
lifted black. The port composes (49, 28, 0) for the row bar over black and
(57, 32, 0) over the page's own window tile, against (56, 34, 8) measured.

**All six builders, attributed by ADDRESS** (2026-09-04, after a play report).
Each site's nearest preceding `loc_` label gives its address, and every panel
callback slot is a function start, so each call lands in a named builder:

| builder | panel | page | icon |
|---|---|---|---|
| `0x0049B710` | 0x004DEE50 | Inventory | 0x004DE040 |
| `0x0049C100` | 0x004DED80 | Identity | 0x004DDFB0 |
| `0x0049D170` | 0x004DEDE8 | Slider | 0x004DDFF8 |
| `0x0049D750` | 0x004DEF88 | Memory | 0x004DE088 |
| `0x0049D8F0` | 0x004DF058 | Options | 0x004DE0D0 |
| `0x0049D980` | 0x004DF0C0 | Quit | 0x004DE118 |

**And the shipped data checks it.** Three lists are shared between pages — the
nine rows 0x004DE6F0, the three verbs 0x004DE318 and the echo bar 0x004DEC58 —
and every builder colours *precisely* the shared lists its own panel carries:
Identity echo; Slider rows + echo; Inventory verbs + rows + echo; Memory rows +
echo; Options echo; Quit echo. **Six of six.** The membership is lifted from
the tree and the calls are read from the image, so the two could have
disagreed. That is what makes this a rule rather than a table of addresses —
and it is how the port states it: `UiWalk::buildPage` finds the page's icon as
*the tab-column item whose `child` is this panel*, with no address list at all.

Three of the six then blacken the clock (item 0x004DEC08) with the single-item
setter — Memory, Options and Quit — and Inventory and Slider do not, which is
why a capture of the original shows the date on those two pages.

**The builder runs on every panel CHANGE, not only on `ui.open`.** The tab
column's items carry a `child` panel, so confirming a tab *descends*: the walk
changes panel with no second open. Building only on open leaves the new page
wearing the old page's colour — a player reported reaching the slider page
"with everything in amber", and that was it. `verify.py: sneak page colour`
walks RIGHT, UP, CONFIRM into panel 0x004DEDE8 and asserts its rows turn green.


**The cursor is ANIMATED, and it is the glow a player sees.** `0x40000200` is
by far the commonest decoration — every one of the sneak's tab icons, its
three model buttons and its verbs carry it — and `sub_479920` is not a box:
it takes a 220-dword pool, centres it on the item (`x + w/2`, `y + h/2`), and
advances **sixteen** per-element angles by the frame delta, wrapping each at
360°. So the focused item wears a ring of sixteen turning pieces, which is
what "bleeds light" around the selected verb in a capture of the original and
reads, in a still frame, like a bright fill. It is drawn only when the item
carries `UIF_FOCUSED` **and the screen does too**, so exactly one is on screen.

Not ported: the sixteen elements' geometry and art are not read, and a
sixteen-piece animation invented from one still frame would be decoration
this repo cannot defend.

### `Ui_DrawItem` NEVER READS `+28` — and that is most of what an item shows

The line above says "draws the text", and *which* text is the part that
matters, because it is not the field this document has pointed at everywhere
else. The function reads **`+24`**, a resolved `char *`, and when that is null
calls **`+32`**, a callback that fills a 2048-byte buffer. `+28` is never
touched. So:

> **An item whose `+24` and `+32` are both zero draws no text at all**,
> whatever string id it carries.

**111 of the tree's 572 items are exactly that** (2026-09-04). The sneak's six
tab icons and its three 50×50 buttons are among them, and a composer keyed on
`+28` prints five labels the game has never shown — which is what a player
reported as *"the menus are essentially icons"* and *"some of the texts are
parts of sub-menu and should not be displayed at anytime"*. They are neither:
they are **strings belonging to a widget that does not draw text**. §3g shows
where those particular strings really surface.

The callbacks that appear on the shipped items, and there are fifteen:

| `+32` | what it puts in the buffer |
|---|---|
| `0x00476860` | the generic one: the item's own `+28`, with `+30` as a printf argument when it is not −1 |
| `0x0042AA00` | an inventory row — reads the item's `+60` tag and asks `Game_RaiseEvent(33)` for the object's name |
| `0x0049DC20` | the sneak's echo bar (§3g) |
| `0x0049E090` | the sneak's clock (§3g) |
| eleven more | per-screen native text |

`0x00476860` is gated on bank C `0x80000200` only for *which* of its two arms
runs; both walk the screen's text blob to string `+28`, and the second also
passes `+30` through `sub_43FEA0` as a format argument.

### The SPRITE is how an icon gets on screen

`Ui_DrawItemSprite` (0x00476E60) blits the item's own `w × h` from the
screen's artwork surface: the **lit** source at `+12/+14`, the **unlit** at
`+16/+18`. **233 items carry the flag.** Decoding `sneak.bmp` and looking at
it shows the arrangement plainly — the five left-hand icons appear twice in
the middle of the black panel, a lit row and an unlit row, and the tile map
punches that area out with cell 42 so the atlas never shows. It also explains
a measurement `verify.py: ui sprites` had been making since 2026-09-01 without
an account of it: an unlit source often **equals its own destination**,
because the unlit icon *is* the background there.

### The two lit ladders are NOT one ladder

This document used to say `Ui_DrawItemSprite` "repeats the same ladder" as
`Ui_ItemTextStyle`. Read side by side they agree on two rungs of four:

| | `0x40000008` | `0x40000004` | `0x40000002` | otherwise |
|---|---|---|---|---|
| **sprite** | lit | oscillator 1 | ¬sel: unlit · sel ∧ ¬focus: lit · sel ∧ focus: **oscillator 1** | SELECTED |
| **text** | lit | oscillator 1 | ¬sel: unlit · sel: **oscillator 1** | SELECTED **and** FOCUSED |

(`sub_476E60` LABEL_4/10/11 against `sub_4769A0` LABEL_6/12/13.) A drawer that
shares one of them lights the wrong things the moment two lists both have a
selection.

**Oscillator 1 is the flash.** Its record ships period 500, flags 3, and its
completion `sub_42B7B0` does `osc[6] = (osc[6] == 0)` — a square wave toggling
0/1 every 500 ms, on a MILLISECOND clock and not a frame count. That is a
player's *"flashing icon to indicate the selection"*. Oscillator 2 does the
same job for `I2D_DrawRectOutline`'s colour, through `0x40400000`.

### Text colour is the item's own, halved when unlit

`Ui_ItemTextStyle` takes `+8/+9/+10` as the colour — unless bank C carries
`0x80000001`, which forces white — and **halves all three when the row is not
lit**. That halving is the one shift this document already described; what it
halves is the item's colour, not a constant. **233 of the 275 text items carry
the white flag**, so a composer hard-coding white-and-grey is right for those
by luck; nine items are genuinely coloured (eight at (254, 68, 20), one at
(255, 100, 70), on the terminal and SURV screens).

`item+36` is the FONT and is the same story: identified in §5 and never
lifted, so a port draws every screen in one face until it is. The sneak names
74 `J` JOURNAL for its text and 67 `C` for its clock, where the start menu's
buttons name 73 `I` MENUINTR — and MENUINTR has no Latin glyphs, so a device
drawn in the menu's face renders the right strings through the wrong alphabet.

### Two things that explain how the menus look

**Unselected rows are dimmed by a shift.** `Ui_ItemTextStyle` decides whether
an item is *lit* — always with `0x40000008`, pulsed by oscillator 1 with
`0x40000004`, conditional on the selection with `0x40000002`, otherwise
selected-**and**-focused — and if it is not, it halves all three colour
channels (`>>= 1`). One shift, and it is the dimming of every unselected row in
the game. `Ui_DrawItemSprite` repeats the same ladder to pick between the lit
and unlit source rectangles.

**`item+36` is a font, not a character.** It goes into `params[2]`, which
`Text_DrawBlock` copies into `dword_907A10` — a global whose default is **74**,
stepping to **76** below 640×480. So `Opt_BindRow` writing 83 for a heading and
74 for a value row is choosing a typeface, and 74 is the engine's own default.
(Read as ASCII they are 'S' and 'J', which is a coincidence worth naming
because it is an inviting one.)

### The oscillators

**8 records of 40 bytes at 0x004C3EA0**, ending exactly where the string block
begins. `Ui_TickScreens` advances every one whose flag bit 0 is set by the
frame delta; the drawers read `+24`.

| # | period | output | used for |
|---|---|---|---|
| 1 | 500 ms | — | the lit/unlit blink |
| 2 | 1000 ms | **45..200** | the alpha the arrows, the ▶ marker and the pulsing fill throb with |
| 3 | 500 ms | 230..235 | a small wobble |

### What is generic, and what is per-screen

Everything above is the *shared* renderer. The rest of that address range is
per-screen setup, and it reaches the same primitives: `sub_47A6D0`, for
instance, is `SAVE GAME`'s — it counts the save directory with
`SaveDir_NameAt` / `SaveDir_CountByName` and, at exactly **256**, marks the
"new save" row unselectable and reveals a second one. That is the UI
independently agreeing with the 256-slot `IAM\GAMES` directory in
[`FILE_FORMATS.md`](FILE_FORMATS.md) §5b4, from the other end of the engine.
Thirteen such functions (652 lines) are still unread.

`Ui_Oscillator` is one of the places the decompiler is wrong and the assembly
is not: Hex-Rays renders the base as `4996768` (0x004C3EE0), but
`lea eax,[eax+eax*4]` / `lea eax, ds:4C3EA0h[eax*8]` is `0x004C3EA0 + 40*k`.
Trusting the C would have put every record 24 bytes out.

---

## 3c. Input — one callback, and one word of bits

**There are no per-screen input handlers.** The definition table's `+24` is
`0x0042A0F0` for **all 32 live screens** (the five `(ELIMINE)` entries carry
nulls), so `Ui_ScreenInput` is shared; what differs per screen is the `+20`
**open** callback that builds the panel, and the hooks that panel and its lists
carry. Of the four slots, only two really vary: **20** distinct open callbacks
and **10** close (over 20 pairs), against 1 input and 2 draw.

### The bits are the game's, not the menu's

`Game_Frame` polls once and hands one word to the whole interface:

```c
Input_Poll(&held, 0);
edges = held & (held ^ (repeatMask & lastFrame));
lastFrame = held;
uiBits = edges;                       /* -> screen+108, via UI_TickScreens */
```

A bit **in** `repeatMask` fires only on the press; a bit outside it repeats
every frame while held. `Ui_BeginScreen` sets the mask to **0x203F** — every
button the interface uses — so menus are edge-triggered and holding a
direction does not scroll. Closing the last screen sets it back to 0.

The bits themselves are the engine's **14 key-binding slots**, the same ones
`.CTL` transitions match on ([`ASSETS.md`](ASSETS.md) — `Input_Poll` maps
binding *k* to bit `1 << k`). **The defaults are not at `0x004C65B8`**,
which this section said until 2026-09-01. That address is the **live**
keyboard table, written by `Input_SetUiKeyBinding` (0x0043E830), and the
`{203, 205, 200, 208, 18, 19, 32, 33, 29, 57, 34, 35, 42, 15}` it ships with is
only its static initialiser — arrows, then **E R D F**, `LCTRL`, `SPACE`,
**G H**, `LSHIFT`, `TAB`. A dense fourteen with no holes is the tell: the real
Aventure scheme leaves four slots unbound. The defaults are the three compiled
tables at `0x004C8F90` / `0x004C9070` / `0x004C9150`, and `Game_Init`
(0x0041FA00) copies group 0 over the initialiser with `Input_InstallScheme(0)`
before the first frame — so **slots 4 and 5 are ENTER and SPACE, and no player
ever saw E or R**. The table below is corrected accordingly.
`verify.py: engine input` installs the scheme and asserts the transition. The menus and the
fighting read one word; the interface just gives three of the bits a second
job:

| bit | default | in combat | in the interface |
|---|---|---|---|
| `0x0001`/`0x0002` | ← → | | previous / next, in a list or between lists |
| `0x0004`/`0x0008` | ↑ ↓ | walk | previous / next |
| `0x0010` | ENTER | attack row `A*` | **confirm** |
| `0x0020` | SPACE | attack row `B*` | **back** — to the parent panel, or close |
| `0x2000` | TAB | holster | **close**, where the panel's flag `0x20` allows |

`Input_Poll` also folds the joystick in: the stick's two axes become the same
1/2/4/8 against a deadzone, and its first ten buttons land on `0x10` upward —
so gamepad button 0 is confirm and button 1 is back, matching E and R.

### The dispatch chain

`Ui_DispatchInput` delegates in a fixed order, and the first to return 1
consumes the frame (and clicks the confirm sound):

```
the back / close bits          handled here
the panel's own hook           panel+16
the CURRENT list's hook        list+4      (panel+32 indexed by panel+24)
Ui_MoveSelection               the default
```

`Ui_MoveSelection(screen, list, prev, next)` steps `list+2` and **keeps
stepping over any item flagged `UIF_UNSELECTABLE`**, so a caption or a disabled
row is skipped rather than landed on; it wraps unless the list's flag `0x80000`
pins it at the ends. `Ui_MoveBetweenLists` is the same idea one level up, over
`panel+24`, skipping a hidden list or one with nothing selectable in it. There
are two prebuilt bindings of each — 4/8 for a vertical list, 1/2 for a
horizontal one.

If neither bit was down the selection cannot have moved, so
`Ui_MoveSelection` falls straight through to `Ui_ConfirmSelection` — which is
why one function serves both.

### Activating an item

`Ui_ConfirmSelection` wants bit `0x10`, then takes the selected item and:

* calls its **`+40` callback** if it has one; otherwise
* enters the **panel at `+44`** — the old panel's leave hook (`+8`) runs, the
  screen's `+28`/`+32`/`+36` are rethreaded, and the new panel's builder
  (`+4`) runs.

**That `+44` is exactly what `Opt_BindRow` writes for a submenu row**, and its
`+0`/`+4`/`+8` are the options page record's parent / builder / leave. The
options menu's "page" and the interface's "panel" are the same structure —
§4's page tree is a panel tree, and this is the code that walks it.

One variant worth naming: an item whose `+44` also carries flag `0x20000040`
is entered **on the move rather than on the confirm** — a tab strip, where
sliding onto the heading switches the page under it.

### Rebinding

`Input_ReadOneControl(out, device)` returns the first control held down, in one
code space: **0..255** a keyboard scan code, **48..57** a joystick button (its
index plus 48 — the offset of `rgbButtons` in `DIJOYSTATE`), **12/13/14** a
mouse button. `Opt_RebindKey` calls it with the device for the slot it is
filling — keyboard, joystick, mouse — and stores the code in the option
record's `+96`, `+100` or `+104`.

So a **type-3 row's `+92` array is not the choice values it is for every other
type**: it is three device bindings. All three ship zeroed and are filled at
run time from a scheme, `Input_InstallScheme` pushing 14 slots at a time into
the tables `Input_Poll` reads.

---

## 3d. The per-screen open and close callbacks

The `+20` and `+28` slots are the only per-screen code there is: **20 distinct
open callbacks and 10 close**, over 20 pairs. They are also nearly invisible —
**26 of the 30 addresses have no function in `Runtime.exe.c`, and no `proc`
label in `Runtime.exe.asm` either**, so IDA folded each into whatever precedes
it. `tools/asmfn.py` anchors on `loc_`/`proc` labels and so silently returns the
*wrong block* for most of them, which is the trap CLAUDE.md §1 already records
for opcode 120, one address range further on.

**The reason is not the prologue, which is what this said until 2026-09-01.**
Measured over the 33 distinct callback addresses: predicting "has a `proc`
label" from *starts with a push* is right 22 of 33, and **11 of them open with a
push and still have no label**; predicting it from *has a direct `E8` caller* is
right **29 of 33**. Thirty-two of the 33 have **no direct caller at all** — they
are reached only as a dword in the screen table, and IDA's auto-analysis makes
functions from calls, not from data references. The prologue was a correlate,
not the cause. They have to be disassembled from the image at
their own address. Their names live in `tools/ui_tables.py: CALLBACKS`,
because `readable/src` has nowhere to put them.

### They all have one shape

Read that way the family is uniform, and the uniformity is the check:

* **20 of 20 opens** write the screen's `+28` with a **static panel address**
  and reach `Ui_BeginScreen`, which sets the state to 2 and installs the
  edge-trigger mask.
* **19 of 20 closes** are, or reach, `Ui_CloseScreenDefault`.

Between those two lines each open does only bookkeeping: bind string ids and
tags onto the panel's items, and set the flags that hide or show a row. Seven
screens share a **single panel** (`0x004E4108` — the terminal family:
`TERMINAL`, `FIGHT SIM`, `MORGUE`, `ARCHIVES` and the three `SURV` screens),
which is the `+8` parameter story from §2 seen from the code side.

`TERMINAL` is the clearest example. Its open writes five items' `+28` with the
string ids **5,6,7,8,9** and their `+60` tags with **0,1,2,3,4**, and a sixth
with string **10**. *(Recovered mechanically since 2026-09-01 and lifted into
`tables/ui_widgets.json` as each item's `bind` — 35 string bindings and 22
tags across the tree, and every one of the 29 that sits on a screen with a
text file names a non-empty string in it. The other six are on child panels,
which have none. Two encodings: `+28` is an int16 and uses the operand-size
prefix `66 C7 05`, `+60` is a dword and uses `C7 05` — decoding both with one
pattern reads TERMINAL's string 5 as 3345350661.)* In the shipped `IAM\Term` those are
"Consulter le dossier n°1" … "n°5" and "Quitter le terminal", against dossier
texts in strings 0..4 — so the tag is the dossier the row opens. The items it
writes to also carry an activate callback at `+40`, which is exactly what
`Ui_ConfirmSelection` calls.

### The open callbacks' FLAG edits — and the second branch

Between `Ui_BeginScreen` and the end, an open callback also sets flags that
hide or show a row. Three helpers do it, all with the same
`(target, flag, on)` shape and differing only in the record they write:

| helper | writes | what |
|---|---|---|
| `I2D_SetFlag` `0x00428FF0` | an **item**'s +48/+52/+56 | one row |
| `I2D_SetListFlag` `0x004290D0` | a **list**'s +16/+20 | the list itself |
| `I2D_SetFlagOnAllRows` `0x00429140` | every row of a list | a broadcast |

**The middle one was missing from the lift until 2026-09-01**, and because all
three look alike at the call site nothing indicated it: **41 edits across
eleven screens** — the ten shops (four each) and `MULTIPLAN` — simply were not
in the table.

**And `Ui_OpenShop` branches a second time.** The parameter that picks the
title also picks the flag arm: `test eax, eax` at `0x004AE5DA` jumps to
`0x004AE66A` when the parameter is 0, which is `BANK`. The two arms are exact
mirrors, so a linear scan recorded both — **20 items (2 rows × 10 screens)
carried `[f, False]` and `[f, True]` together, and they were the only
contradictions in the whole tree.** Following the branch resolves all 20.

`0x20000004` is `UIF_UNSELECTABLE`, so **setting** it greys the row:

| | `0x4E3240` "Acheter" | `0x4E3288` "Vente" |
|---|---|---|
| `BANK` | greyed | **live** |
| the other nine | **live** | greyed |

That is the same fact `IAM\Buy`'s titles state from the other side — "Banque -
**vente**" against nine "… - achat" — and it is **the only asymmetric thing
about the two arms**. Every count over them is symmetric, so swapping the arms
leaves the constant totals, the selectable count and the sim/port agreement all
unchanged; only the text of the live row moves. `verify.py: ui shop titles`
asserts that row, which is what actually pins the assignment.

Two consequences worth naming. The port carried a deliberate **`conditional`
guard** — refuse any flag recorded both ways, leaving the static record
standing — and `tools/sim/ui.py` did the cruder equivalent by reading `+48`
raw. Both were right while the edits were unresolvable and both are now
removed, so the ten shops each grey the right row: the tree's selectable count
falls **387 → 377**, and the widget tree's flag-constant total falls
**139 → 99** (20 items × 2 duplicate entries), splitting 22/75/2 instead of
42/95/2.

### The shops name themselves

`Ui_OpenShop` serves ten screens and switches on the fixed parameter, through a
jump table at `0x004AE7AC` whose ten targets are each one
`mov word ptr [0x004E37CC], imm16` — the title string in `IAM\Buy`:

| param | screen | string | | param | screen | string |
|---|---|---|---|---|---|---|
| 0 | `BANK` | "Banque - **vente**" | | 5 | `SORCELLERIE` | "Sorcellerie - achat" |
| 1 | `PHARMACIE` | "Pharmacie - achat" | | 6 | `LIBRAIRIE` | "Librairie - achat" |
| 2 | `ARMURERIE` | "Armurerie - achat" | | 7 | `SEX-SHOP` | "Sex shop - achat" |
| 3 | `RESTAURANT` | "Achat" | | 8 | `DIVERS` | "Divers - achat" |
| 4 | `BAR` | "Achat" | | 9 | `LIB. LAHOREY` | "Bibliothèque de Lahoreh — Emprunter livre" |

**Eight of the ten titles name the screen that uses them**, which is a *third*
independent confirmation of the screen-table order, and the strongest: it is
the engine mapping a screen's own parameter to a French title, and shifting the
index by one breaks all eight at once. (`RESTAURANT` and `BAR` share the
generic "Achat" — a bar and a restaurant just sell things.) `BANK` alone says
**vente**, and its open takes the one branch the other nine skip: at the bank
you sell.

**And this is the binding a linear scan cannot get — it gets it *wrong*.** The
ten shops share one panel, one item and one callback, so the scan that
recovered the other 35 bindings (§3d) walks straight through the ten arms and
keeps whichever `mov` it saw last: every shop came back bound to string **19**,
and nine screens would have shown "Bibliothèque de Lahoreh". Nothing about that
looks wrong in the lift — the id resolves, the text is real, the item is right.
`tools/sim/ui.py: shop_titles()` follows the table and `exetables.py` overrides
the scan with it, so `tables/ui_widgets.json` now carries each shop's own
title.

`verify.py: ui shop titles` — **not** `ui tables`, which this section cited
until 2026-09-01 and which never touched the shops; the table above was a
documented number with no test behind it. The check asserts the ten ids in
screen order, that all ten resolve to non-empty text in `IAM\Buy`, that the ten
are **distinct** (a linear scan gives one id ten times), and the eight that
name their own screen. Shifted one place, that last count goes 8 → **0**: every
shop takes its neighbour's title and not one of them lands.

### What OPENS the sneak — a key, an animation and a table

Screen 9 is not opened by `ui.open`. **No script opens it**: the chain runs
through the player's own animation channel, and every link is in the shipped
data or in a table lifted from the image.

```
TAB                      tables/key_bindings.json group 0 "Aventure",
                         action 13 "Ouvrir sneak", bit 0x2000, keyboard 15
-> H1Avnt / F1Avnt.CTL   a group-0 entry whose +4 is exactly 0x00002000 and
                         whose flags carry 2, the ALIAS bit: not a state to
                         sit in, a redirect through its GoTo
-> group 6               H_SNKON, that group's flag-0x20 default entry
-> its child             flags 0x25000013 - bit 0x10 names a move
-> tab_special_move[0]   "MDSNEAK0" -> sub_0046ADF0
```

and `sub_0046ADF0` is short enough to quote whole — the binary names it
itself, through its own failure string:

```c
v1 = Actor_Index(a1);
if (sub_41A350(v1) != -1)            // something pending at actor+164
    return sub_41C720(g_Player);     // ...use THAT instead, event 10
Game_RaiseEvent(25, 0);              // open object list 0 - the carried items
sub_41E040(byte_53B084);
if (!UI_OpenScreen(9, -1, -1, -1)) { // SNEAK
    Game_RaiseEvent(26, 0);
    return Dbg_Printf("cant start sneak");
}
```

Three things follow, and they are why the sneak behaves unlike every other
screen:

* **it has no waiting script.** The `-1` is `UI_OpenScreen`'s waiting-context
  argument, so `dword_930744` is never written and nothing is parked at status
  6. Closing it answers nobody — where leaving a `ui.open` screen IS an answer
  of −1 (§3d-bis).
* **event 25 comes first**, opening object list 0, because the inventory page
  reads its nine rows back out of the channel (§3e) rather than out of
  `IAM\Sneak`. The matching 26 is raised by the close, and also by this
  function's own failure arm.
* **only the two ADVENTURE banks carry `MDSNEAK0`.** `H1Avnt` and `F1Avnt` do
  and the five combat/creature ones do not, so the sneak opens in adventure
  mode and nowhere else — which is the game's behaviour, arrived at from the
  data rather than from playing.

`verify.py: sneak chain` asserts all of it; `engine: sneak` runs it in
`omk-play`, where TAB in Anekbah opens the device.

### The background scales its DESTINATION, not its source

`Ui_DrawPanelBack` (0x00476040), the tile-map arm:

```
dst = (col * I2D_ScaleX(64), row * I2D_ScaleY(64), ...)   row 7 at half height
src = ((id % 10) << 6, (id / 10) << 6, ...)               raw 64-pixel cells
```

Ten columns of `ScaleX(64)` cover the display whatever its width, while the
source keeps reading 64-pixel cells out of a 640×480 sheet. A port using a
literal 64 for BOTH covers the top-left 640×480 and leaves the rest of the
display showing whatever was under it — while every widget, which does go
through `I2D_ScaleX/Y`, sits somewhere else entirely.

**A 640×480 test cannot see this**, because there `ScaleX(64) == 64`. It has
to be composed at a second size, which `verify.py: engine screen scale` does.

Two more things the same function does before the tiles: with flag
`0x40002000` it draws no background at all, and **without** `0x40001800` it
CLEARS the whole display first.

### 3g. The sneak's own two rows — the echo bar and the clock

Neither `sub_0049DC20` nor `sub_0049E090` has a `proc` label: nothing CALLS
them, they are dwords in the widget table, so `tools/asmfn.py` anchors on a
neighbour and returns the wrong function. The byte range has to be counted out
from the last labelled function before them, which is `sub_49DB80`.

**The echo bar shows what is SELECTED**, not what is hovered — a still frame
cannot tell the two apart, and the screenshot that prompted this reads as the
latter. `sub_0049DC20` takes the panel's current item and dispatches on its
ADDRESS:

| item | what the bar shows |
|---|---|
| `0x004DE338` | `"%s %d"` of its string and `sub_42B1C0(4)` |
| `0x004DE380` | `"%s %d"` of its string and `sub_42B1C0(5)` |
| `0x004DE3C8` | its string alone |
| `0x004DE230` | its string, with `+30` forced to 1 |

**And that settles what the three 50×50 items are.** They are the **setek**
and **anneau** counters and the **map reader** — the three `.3DO` models the
screen's parameter-0 open loads (`setek`, `anneau`, `imager`) — and their
strings 8, 9 and 41 are rendered *here*, on the bar, when one of them is
selected. "Seteks en votre possession :" was never a caption beside an icon,
which is why those items have no `+24` and no `+32`.

It also answers what `imager` counts: **nothing**. Its arm carries no
`sub_42B1C0` and no format string, just the bare "Lire plan". It is a map
reader, not the ammunition a player guessed at.

The two counts come from `Game_RaiseEvent(44, {4|5})` → `sub_40B360`, whose
cases 4 and 5 read a character record's **`+172`** (unsigned) and **`+174`**
(signed). `+172` is corroborated from the other end and was already in this
document without the connection being made: §3e has case 38 refusing a
purchase whose price "exceeds the player's money at `+172`". Two subsystems
reading one field is what makes it the money rather than a plausible int16 at
a plausible offset; `+174` is new. The shipped fixture opens with **0 seteks
and 2 anneaux**. `verify.py: player counters`.

**The clock** (`sub_0049E090`, item `0x004DE160` at (350, 430)) is the
engine's own date and time formatters, `sub_0041E690`'s integer division —
"12 Nadim 7216 - 13:01:15". Only the `" - "` joining the halves is read off a
screenshot rather than out of the code.

The clock is also where a bug outside the interface surfaced.
[`GAME_STATE.md`](GAME_STATE.md) records that the clock is an engine global
and **not part of the 8192-byte image**, so restoring a save restores
everything except *when* it happens: the day and time live in the slot header
that `SaveDir_Build` writes. A loader that restores only the DB leaves the
game at day 0 — invisible for as long as nothing draws a clock, which in
`engine/` was until this row existed. `verify.py: save clock`.

### The rows are a WINDOW, and the gate is per row

`sub_42AAE0(list, window)` binds the nine row widgets:

```
for each widget k:
    if (k + window >= list+24)      // past the end of the real list
        item+60 = -1;               // no tag
        set 0x40000001;             // and NOT DRAWN
        set 0x20000004;             // and unselectable
    else
        item+60 = k + window;       // the row it shows
        clear both;
```

so the engine draws only the rows that HOLD something — two of nine in a
capture with two objects carried — and `list+24` is the count the channel
reports through case 29. `sub_42AFF0` moves the window on the up and down
bits, skipping rows whose tag is −1, and raises **event 30** for the newly
selected row, which is the 3D preview (§3e).

### The one close that can refuse

`Ui_CloseSneakFamily` serves `VIDEOPHONE`, `SLIDER` and `SNEAK` by the `+4`
parameter. **`SNEAK`** frees three o3de scenes and closes screen 35, which its
own open had loaded hidden underneath; both closing paths raise **event 26**
and fall into the generic close. But **`VIDEOPHONE`**, with no `SHOOT HUMAN`
up, calls `Ui_StartOscillator(5, 100)` **instead** of closing — oscillator 5
ships with period 0 and gets 100 ms here, and while its flag bit 0 is set the
close is refused. That interface has a closing animation; every other screen
closes on the frame it is told to. `SLIDER` takes neither arm and goes
straight to the generic close.

**This paragraph had `SNEAK` and `VIDEOPHONE` the wrong way round until
2026-09-04**, and the branch is what decides it — the parameters are 0 SNEAK,
1 SLIDER, 2 VIDEOPHONE (§2's table), and `sub_49B610` opens:

```
mov  eax, [esi+4]
sub  eax, 0
jz   loc_49B6A5      ; 0 SNEAK      -> close screen 35, free the three scenes
sub  eax, 2
jnz  loc_49B6EA      ; 1 SLIDER     -> the generic close
                     ; 2 VIDEOPHONE -> falls through: the SHOOT HUMAN test
                     ;                 and the oscillator refusal
```

The OPEN says the same thing from the other side and is the corroboration:
its parameter-0 arm is the one that `Read3DO`s `setek`, `anneau` and `imager`
and calls `UI_LoadScreen(35, …)`, and 35 is `OPTIONS` — which the sneak
device needs because **`Options` is one of its own five tabs**. So the screen
loaded hidden underneath is the options screen, the three models are the
inventory page's object previews, and the arm that frees them is the arm that
loaded them. Asserted in `verify.py: sneak chain`.

### And what the generic close does for the script layer

`Ui_CloseScreenDefault` releases the bitmap and the text buffer, hands focus
back to the slot recorded at `+44`, and memsets the 124-byte slot with its id
set to `-1` — which is what frees it, since `UI_LoadScreen` looks for a slot
whose `+8` is 0. When the **last** screen closes it clears the repeat mask and
the input word, so the release of the button that closed it cannot leak into
the game, and calls `sub_466B60` to resume the player — the same function that
fills the event-5 argument block with the waiting context and the answer.
`ui.open` suspends a script at status 6 ([`SCRIPT_VM.md`](SCRIPT_VM.md) §70)
and **this is where it comes back**.

### 3d-bis. The ANSWER layer — what a screen hands back

`ui.open` suspends its script at status 6, and **one global carries the
reply**: `UI_LoadScreen` sets `dword_930750` to **−1**, a widget callback
writes the chosen value, and `Ui_CloseScreenDefault` → `sub_466B60` hands it to
`Game_HandleEvent` case 5, which stores it in the variable the `ui.open` site
named. So "what did the player choose" is **one global with 17 writers**, and
they are enumerated in `tables/ui_widgets.json` as `answerSites`.

**They had to come out of the image.** Only **3 of the 17** sit inside a
function IDA gave a `proc` label — the −1 reset in `sub_41DF30` and the LIFT's
two in `UI_GridMenuInput`. The other **14** are in unlabelled regions following
an `endp`, because a widget callback is reached only as a **dword in the widget
tree** and never by a direct `E8` call — the class of function §1's corrected
trap describes. Nine of the eleven answer-writing functions are in neither
`Runtime.exe.c` nor `readable/`.

Nine writers store an immediate, and the distinct values are only **0, 1 and
6**; the seven register writers are where the interesting answers are computed.

#### The terminal family — a second `Ui_OpenShop`

Seven screens share panel `0x004E4108` — `TERMINAL`, `FIGHT SIM`, `MORGUE`,
`ARCHIVES` and the three `SURV` screens — and **one activate callback** at
`0x004AF410`, which switches on the screen instance's `+4` (the fixed
parameter from §2) through a jump table at `0x004AF578`. That is exactly the
shop-title shape, and it carries exactly the same hazard: a linear scan over
the bytes attributes all four of its answer writes to one screen.

The mapping is one the data could refute and does not — the seven screens
sharing that panel carry parameters **0..6 with no gap and no repeat**, and the
table has exactly **seven** targets, every one inside the function. So the case
index *is* the parameter:

| case | screen | what it answers |
|---|---|---|
| 0 | `TERMINAL` | nothing here — its answer comes from `0x004AF0E0` |
| 1 | `FIGHT SIM` | `row + 1`, for rows 0..2; also plays interface sound **26** |
| 2 | `MORGUE` | `row + 1`, for rows 0..4 |
| 3 | `ARCHIVES` | **1**, and only when the row is 2 |
| 4 | `SURV ERROR` | — it **falls through into case 6** |
| 5 | `SURV NO KIT` | no answer at all |
| 6 | `SURV KIT` | **1**, and only when the row is 0 |

**Case 4 falling through to case 6** is what a table read as seven independent
arms would miss: `loc_4AF52F` ends in a call with no jump, so `SURV ERROR`
runs `SURV KIT`'s test.

`TERMINAL` answers from its own `+24` region instead, and it answers a *pair*
of booleans: `2 + (dword_68A5FC != 0)` on one branch and `(dword_68A5FC != 0)`
on the other — **0, 1, 2 or 3**. That global is set by case 0 when the selected
row is 4, with a sibling for row 3, so the terminal remembers which dossiers
have been read and answers differently afterwards.

The two already-read writers re-derive from the same enumeration: the **LIFT**
(`UI_GridMenuInput`) answers `slot − 1` with slot 0 giving **6**, and the start
menu's `Confirmer` answers **1**, gated on a non-empty name field (§3f).

#### Which screens the writers serve, and the one cross-check that works

A site belongs to the callback it sits in, and the widget tree names the
callback — so the mapping is tree → screen. Two things it has to get right:

* a **child panel records `screen: -1`**, and the whole menu tree hangs off
  `0x004CF218`, whose parent is 0 — so resolving by the `parent` chain drops
  screen 29, the one screen whose answer was already read. It is recovered the
  way the data states it: **screen 29's open callback references
  `0x004CF218`**, so a child panel is attributed to whichever screen's open
  mentions it. That tree serves **29 `OMK START MENU`, 30 `SAVE GAME` and
  31 `PAUSE GAME`** — the same family §2's `0x20000400` flag describes.
* `0x004AF410` serves seven screens, so its four sites are attributed to all
  seven. No hand-written constant is doing that work: the widget tree already
  names that address as an item `+40` callback **80 times**, which is exactly
  how `Ui_ConfirmSelection` reaches it.

`ui.open`'s third field is the variable the reply is stored in, or **−1** to
discard it. Over the whole corpus — and it has to be the *whole* corpus, since
the slot walk finds **241** sites and no start menu, screen 29 being opened
from AREA 118's `+4` **startup** script — there are **242 sites over 25
screens**. Fifteen keep the answer, ten discard it.

**The invariant is that no answer site is attributed exclusively to screens
that discard it** — 0 of 16. A wrong attribution shows up there, because a
writer whose only screens throw the value away would be a writer for nothing.
It is stated per *site* and not per screen on purpose: screen 30 discards its
answer while sharing a tree with two screens that keep theirs, so the
per-screen form would fail on that alone.

**What cannot be checked, and why.** Fourteen of the 25 screens store the
answer in the **same variable, 19** — the LIFT's 496 is the only private one,
and ten screens pass −1. So "which constants does a script compare this
screen's answer against" is not answerable by any static walk: attributing a
comparison to a screen would be counting collisions, the trap §1 records for
scene-local ids. Only the LIFT's is attributable, and its scripts compare
`Etage` against 4.

Three screens keep the answer with no writer: **0 `VIDEOPHONE`**, whose panel
the widget lift already lists as unresolved (its open installs three
candidates), and **2 `MULTIPLAN`** and **23 `RESTAURANT`**, which resolve to a
panel and have no writer among the 17 — so their scripts can only ever read
back the **−1** `UI_LoadScreen` set, which is a real outcome ("closed without
choosing") and not a missing decode.

`verify.py: ui answers` — **tier 2**, corpus-constrained. What it establishes
is the set of values each screen can write, not that any screen behaves
correctly.

---

## 3e. The inventory screen's data channel — `Game_HandleEvent` 25..42

The interface never touches an object list directly. It asks, through
`Game_RaiseEvent`, and the answer comes back in a small argument block:

| off | |
|---|---|
| `+0` | the value, in and out — usually an item index |
| `+4` | the **result code**; and for case 36, the *request* code on the way in |
| `+8` | a caller-supplied buffer, for the cases that return text |

Result codes are consistent across the family: **1** done, **2** refused,
**3** no list open or the index is past the end. `dword_4C0B64` holds the open
list — case 25 sets it, and it is the same global `ui.open`'s field 1 writes.

| case | |
|---|---|
| 25 / 26 | open / close. Opening list **3** also sizes it to 16 and `ObjectList_Load`s it — list 3 is a shop's stock, loaded per area |
| 29 | the item count |
| 30 | the **3D preview**: load the selected item's model into an object slot and cache its description. Skipped for list 2, which has no models |
| 33 | the display name, plus `" - N"` when the record's flag `0x20` says it carries a quantity — from the **player** record for kinds 2..6, from the item's own `+12` for kinds 7..11 |
| 34 | the item's price |
| 35 | **use**: record flag bit 1 means consumable (`Object_ApplyEffect`, then drop it from the list); otherwise load its model and hand back the slot — "use" on anything else means **drop it into the world** |
| 36 | a sub-dispatch on the *incoming* `+4`: 6 can-drop, 7 move to list 1, 8 move back to list 0, 9 and 10 pure queries |
| 37 | **combine** — below |
| 38 | **buy**: refuse if list 0 is full or the price exceeds the player's money at `+172`; for list 2 it is a flat charge against `+174` instead |
| 39 | **sell**: credit **half** the price, clamped at 0xFFFF, and remove |
| 40 | **examine**: kind 15 returns the preview slot and the description, kind 16 a different payload; anything else is refused |
| 41 | may this shop item be bought? Refused when its kind is 2..6 and the player already holds one |
| 42 | raise message 25 and return a global variable — the script layer's veto |

An inventory slot is **56 bytes**: `char[32]` display name, then a 24-byte
header copied from the `IAM\OBJECT` record's `+0`. So the header fields these
cases read are object-record fields: `+0` the id, `+2` the kind, `+4` the
flags, `+10` the price, `+12` a per-item count, `+14` the model stem.

### Case 37 — the combination table, and six recipes that cannot fire

`sub_409650` walks a table at **`GLOBAL +12`**, count `int16` at `+26`:
eleven 8-byte records of `[a][b][product][gate]`, matched **symmetrically**, so
the order the player picks the two items in does not matter. All 33 ids name a
real object, and they read as recipes:

```
Petite boîte      + Petite clé            -> Petite boîte ouverte        gate 0
Explosifs KR100   + Détonateur pour KR100 -> Charge WCM                  gate 0
Tasse de koil     + Somnifère             -> Tasse de koil droguée       gate 0
"L'entrée d'Hamestaga'n" + Traductech     -> Traduction du texte …       gate 0
Langue de mort    + Wiki                  -> Sort de vérité              gate 8
Plume de jimpan   + Gouttes d'Ombre       -> Sort de résurrection        gate 8
Corne de sham     + Rosée de lumière      -> Sort démasquer un démon     gate 8
```

A recipe fires only when its `+6` **equals** `dword_4E6C70`. That global has
exactly **four references in the whole binary**, all inside this case: set to
**1** when the first item picked is `GLOBAL +64` — object 330,
*"Beshe'm sanctifié"* — to **0** otherwise, and to **−1** after a combine. It
is never 8.

So the **six** spell recipes cannot fire. And nothing else grants their
products: across **472** world-script and **10** conversation-script
`inventory.add` sites, **not one** names any of the eleven products, so
combining is their only source.

> One survives anyway, and checking is what caught it: **"Sort de
> résurrection" is placed once as a world prop**, so it can be picked up.
> The other five spell items — "Sort de vérité" in its four variants and
> "Sort démasquer un démon" — are **unobtainable in the shipped build**.
> Sixteen of the eighteen *ingredients* are placed as props, so the
> ingredients are gettable; it is the combination that is dead.
> `verify.py: object combine`.

This is the same shape as the dead options page (§4) and the five `(ELIMINE)`
screens (§2): shipped data with no path to it.

---

## 3f. The widget tree under the simulator

`tools/sim/ui.py` walks the tree with the engine's own input words, so
`ui.open`'s answer is **derived** rather than supplied. Until 2026-08-30 the
simulator answered with a literal — which tested the suspend/resume mechanism
and left the whole widget layer outside the harness.

What is data and what is code is kept apart, the way the rest of the simulator
does it:

* the **navigation is data** — the screen record names an open callback, the
  callback installs a panel with one `mov [reg+0x1C], imm32` (28 of the 32
  live screens give exactly one), and the panel's lists and items are static
  records;
* the **callbacks are native code and are not run**. An unmodelled hook is
  logged and the answer falls back, rather than being invented.

**The sneak family gives three, and following its branch is what put screens
0, 7 and 9 into the tree** (2026-09-04). `Ui_OpenSneakFamily` writes a
different panel on each arm of a `+4` branch, so a linear scan finds all three
and cannot say which is whose — the same shape as `Ui_OpenShop`'s titles, and
the same fix. `sim/ui.py: SNEAK_ARM` records the three byte ranges read out of
the image and `panel_of` CHECKS the scan against them rather than trusting
either alone:

| param | screen | panel | the tab column `0x004DE210` |
|---|---|---|---|
| 0 | `SNEAK` | `0x004DEE50` | shown, selected on row 2 "Inventaire" |
| 1 | `SLIDER` | `0x004DEDE8` | hidden |
| 2 | `VIDEOPHONE` | `0x004DF128` | hidden |

Three more things came out of doing it, and each is general rather than about
the sneak:

* **`panel+24` and `list+2` are recoverable after all.** They are the panel's
  CURRENT LIST and that list's SELECTED ROW — runtime state, which is why this
  document and both walkers said the disk image cannot supply them and fell
  back on `Ui_MoveBetweenLists`'s rule. But an OPEN CALLBACK can write them,
  and going looking found **15 panels** and **8 lists** that do:
  `Ui_OpenSneakFamily` sets all three of its pages, `Ui_OpenShop`'s first
  instruction is `mov dword_4E3988, 0` (its shared panel's `+24`), and
  `FIGHT SIM`, `SAVE GAME`, `PAUSE GAME` and `SHOOT HUMAN` each set one.
  Lifted into `tables/ui_widgets.json` as `current` and `select`, `-1` where
  nothing wrote them.
* **Two of the "unmodelled hooks" were not screen-specific at all.**
  `sub_42A710` is `Ui_MoveBetweenLists` bound to LEFT/RIGHT (a PANEL hook) and
  `sub_42A930` is `Ui_MoveSelection` bound to LEFT/RIGHT (a LIST hook) —
  `sub_42A5C0` and `sub_42A7E0` both take their two direction bits as
  PARAMETERS, and the default dispatch is just `sub_42A7E0(…, 4, 8)`. The
  simulator had transcribed the first as `_move_lists` and **never called it**,
  because until the sneak entered the tree no panel in it named the hook.
* **What still refuses is the sneak's own scrolling.** `sub_0049C050` windows
  a list longer than its nine row widgets — it shifts each row's `+60` tag and
  raises the two arrow flags at the ends — and it is not modelled, so
  `SLIDER` and `SNEAK` still count as approximate the moment those rows are
  driven. `VIDEOPHONE` needs nothing and walks exactly.

`verify.py: sneak chain` asserts the family end to end and `engine: sneak`
runs it.

The path for the game's own opening, every step an input word:

```
screen 29   selection on "Nouvelle partie"       (first selectable item)
CONFIRM  -> item +40 is null, so item +44 -> panel 0x004CF280
            and that panel's BUILDER runs: 0x0047A050 clears the
            name buffer at 0x0069BDA0 and the cursor at 0x00657994
type     -> the name. NOT optional: see below
DOWN     -> focus list 1                          "Confirmer" / "Annuler"
CONFIRM  -> item +40 = 0x0047A2B0  -> answer 1
```

Two steps there were wrong until they were tested against the engine, and both
are worth keeping visible. The move between lists is **DOWN, not RIGHT** — the
panel's own `+16` hook takes priority and moves on up/down, so RIGHT falls
through to the list and never reaches the buttons at all (see *What running it
cost, and caught*, below). And the **typing is required**: `Confirmer` opens by
testing the name cursor and returns without writing anything when it is empty,
so the walk answers `None` — which is what a golden capture ran into before the
callback was read (*Against the engine*, below). `ui_answer` therefore types a
name by default, and passing `name=""` walks the refusal.

and 0x0047A2B0 is the callback carrying `mov dword_930750, 1` — **1** being
what the shipped save records for `Interface`. `verify.py: sim: ui` runs that
with a deliberately **wrong** fallback, so the value has to come from the walk.

Two refusals are asserted alongside it, because a model that answers
everything is worse than one that knows its limits: `LIFT` (its list has a
native `+4` hook) and `OPTIONS` (its pages are built by native code) both
decline and return the fallback.

One thing the data does not decide: **which list a panel focuses on entry**.
`panel+24` on disk is runtime state the native builder sets. The model applies
the engine's own rule instead — `Ui_MoveBetweenLists`' predicate, the first
list that is neither hidden nor empty of selectable items.

### A sprite item's two rectangles — the hover artwork

`Ui_DrawItemSprite` picks its source by the same *is this item lit* ladder the
text style uses: **`+12/+14` while lit, `+16/+18` while not**. So every sprite
item names two rectangles in the screen's own 640×480 sheet, and switching
between them is what a player sees as a hover state.

Two things follow, both visible in the artwork:

* **For 146 of the 164 sprite items the unlit source IS the item's own
  destination.** The resting look is already painted into the background, so
  drawing the item "unlit" blits the background back over itself — it erases
  the highlight rather than drawing anything.
* The lit copy is therefore a **second rendering of the same button somewhere
  else on the sheet**, laid out to suit the sheet rather than the screen.
  `Multipla.bmp` is the clearest case: the four destinations are a **vertical
  column** at x 554/563, and their lit sources are a **horizontal row** at
  y = 192 with x stepping 64 — which is exactly what the sheet looks like.
  The LIFT's seven instead mirror its own 3-2-1 keypad, shifted to another
  corner.

Whether the lit copy is *brighter* is art direction, not a rule. Measured over
the non-key pixels — the blit is `DDBLT_KEYSRC` and the key is **black** —
`MULTIPLAN` is 84 against 67, the shops 112 against 49 and `GANDHAR DOOR` 104
against 41, every item in each case; `DEN`'s highlight is *darker* instead.
`verify.py: ui sprites` checks the three that are decisively brighter, since a
swapped `+12`/`+16` reading would invert them.

`/ui` draws sprite items from the sheet rather than as boxes, and hovering one
shows its lit copy — which works on every screen, including the 29 whose
navigation hooks are native and cannot be walked.

> **And this is why a viewer must compose the background rather than show the
> sheet.** The lit copies live in their own strip — on `Multipla.bmp` at
> (64,192), 256×64 — and the tile map **never places those cells**, so they are
> the one part of the sheet a player never sees. Draw the sheet and they appear
> on screen beside the buttons they belong to, which is wrong and was exactly
> the first thing a reader noticed about `/ui`. Composed, that region is black.
> `verify.py: ui page` asserts it.

### The LIFT — the one bespoke widget

`UI_GridMenuInput` is the only list-level input hook the game has, and it is
the elevator's floor panel. The **item coordinates confirm the arithmetic
independently**:

```
   (278,194)  (321,194)  (370,194)      slots 0 1 2   "Niveau 1 / 0 / -1"
   (284,241)  (325,242)  (371,242)      slots 3 4 5   "Niveau -2 / -3 / -4"
              (325,288)                 slot  6       "Niveau -5"
```

Three columns and two rows, then slot 6 alone on a third, centred under the
middle column — which is exactly what the handler's `% 3` and slot 6's
asymmetric wrapping say, arrived at from the other side.

Modelled and checked: **every slot is reachable** from slot 0 through the four
directions; **slot 6 has no horizontal move**, being alone on its row; and
confirm answers **`slot − 1`, with slot 0 giving 6** — so slot 1, "Niveau 0",
the entrance, answers **0**, and slot 0, "Niveau 1", the only floor above
ground, answers **6**. All **18** `ui.open 4` sites store that in variable
**496, `Etage`**.

### The options screen

Screen 35 is **never opened by a script** — 0 of 241 `ui.open` sites name it;
it is reached from the start menu and the pause screen. So what there is to
model is its navigation and its value changing, not an answer.

It is also not shaped like the other screens: **thirteen page records share
one set of sixteen row widgets**, so what a page shows is not in the row, it is
in the calls its builder makes to `Opt_BindRow(row, item, page)`.
`OptionsUi` recovers those from the builder's own bytes — `push page; push
item; push row; call 0x00490F90` — rather than hard-coding them, so a wrong
page tree shows up as a wrong walk.

The value rules are **`Opt_RowInput` (`sub_492DA0`)**, the live page's `+4`
hook and the last of this module's big unread bodies:

| item type | |
|---|---|
| 0 choice, 4 device | LEFT steps the choice back, **RIGHT and CONFIRM** step it forward, both wrapping |
| 1 slider | LEFT −10 with a floor of 0, RIGHT +10 |
| 3 keybind | reads a control; `dword_660B84` latches the rebind so the selection cannot move while it waits |
| 5 defaults | CONFIRM copies one of **three compiled 224-byte tables** over the live bindings, chosen by `dword_4DD628` (1 keyboard/mouse, 2 gamepad) |
| 6 back | CONFIRM at the **root** returns to screen 29 |

**There is no `case 2`**, and that is the neat part: the hook runs
`Ui_MoveSelectionVertical` first, which — when no direction was pressed —
falls through to `Ui_ConfirmSelection`, so a header's page at `row+44` is
followed generically and needs no code here.

Walked and checked (`verify.py: sim: options`): the root's four selectable
rows are Vidéo / Audio / Options / Contrôles, each opens the page it names,
each page's Retour comes home, and **"Distance de clipping" cycles
Très proche 25 → Proche 50 → Intermédiaire 100 → Loin 150 → Très loin 200 →
wrap**, and back the other way on LEFT — caption and value both, against the
shipped `IAM/Options`.

> **One transcription the walk forced.** Every sub-page's "Retour" binds page
> **0**, not the root — and page 0 has no rows at all. Its builder bounces
> straight to page 1 unless `dword_9103C8`, the dirty latch, raises a prompt
> instead. Without that the walk lands on an empty page, which is exactly how
> it was found.

### The load panel

One panel serves **both** loading and saving — the start menu's "Charger une
partie" (screen 29) and `SAVE GAME` (screen 30) — told apart by the screen id,
which `Ui_BuildLoadPanel` records in `word_4CEA9A`. Its whole shape comes from
one number, how many **distinct profiles** the save directory holds:

| profiles | |
|---|---|
| > 0 | focus the slot list (`panel+24 = 0`), show it, leave "Charger une partie" and "Détruire" selectable |
| 0 | focus the **buttons** (`panel+24 = 1`), hide the slot list, disable both, and set `word_4CEA9A = 3` |

"Nouvelle partie" is hidden either way on screen 29 — it belongs to the save
panel.

The directory comes from `SaveDir_Build`, which lifts four fields out of each
32808-byte slot of `IAM\GAMES` into a 256 × 72 table
([`GAME_STATE.md`](GAME_STATE.md) §8, where that record had been left unread).
And the shipped `gamedata/IAM/GAMES` is a file **the engine itself created** under
the golden-trace rig and never saved into: all 256 slots are empty.

So the model has a real answer to give, and a sharp one — **against the
shipped save directory the load panel comes up offering exactly one thing,
"Annuler"**. `verify.py: sim: load panel` asserts that, and runs a synthetic
one-profile directory beside it so the model is shown to *branch* rather than
always saying the same thing.

### The name field — the last refusal

`sub_47A390` is the only list hook besides the LIFT's grid, and it answers a
**different channel**: `sub_4397B0` hands it one typed character, not the input
bits. That is why a direction does nothing inside the field — and why the hook
*existing* stops `Ui_MoveSelection` being reached, so up and down there are
inert and getting out of it is the panel hook's DOWN.

Its switch is a compact jump table over the characters 8..27:

| | |
|---|---|
| 8 BACKSPACE | delete before the cursor, shift the tail left |
| 9 TAB, 27 ESC | ignored |
| 13 RETURN | focus the buttons — **only when the buffer is not empty** |
| anything else | insert at the cursor |

The cap is the buffer itself: `0x0069BDA0`..`0x0069BDB4` is 0x14, so **20
characters**, refused by a `strlen` test before the insert. The save slot that
receives the name has room for **32**, so the field is the tighter of the two —
and the one real save the engine wrote carries a name of exactly 20, which fits
the cap without exceeding it.

With this the whole new-game path walks with `approx` false:

```
screen 29   CONFIRM on "Nouvelle partie"     -> the confirm dialog
            type "Kay'l 669"                  the character channel
            DOWN                              the panel hook -> the buttons
            CONFIRM on "Confirmer"            -> answer 1
```

### Rendering the text: `tools/uitext.py`

The interface's own fonts and its own markup, followed rather than
approximated — the shipped `.FNT` glyphs (§5), the same 120%-of-line-height
spacing, and a glyph pixel taken as a **coverage level 0..31** that becomes the
alpha, which is what lets one glyph sheet serve every colour.

```
python3 tools/uitext.py "{fC}Quitter le terminal"        # as ASCII art
python3 tools/uitext.py --png out.png --font J "Nouvelle partie"
```

The markup cases the shipped text actually uses all work: `{fC}` names the
COMPUTER font by its id letter, `{I255120045}` is three **3-digit decimals**,
the two chain in one brace as `{fCI255120045}`, `{C}` centres, and a font can
change mid-string — one terminal dossier is 647 characters of COMPUTER with
**20 of SNEAK** inside it, which is `{fS}Anekbah{fC}`. CR is skipped and LF
ends a line, so `IAM\Lift`'s two-line floor labels come out two lines tall.

An item's label is drawn with the item's own font (`+36`), its own colour
(`+8..+10`) and its own alignment (bank C → `Text_DrawBlock`'s 2/4/8/16, where
**4 is right and 8 centred**), and an unlit row has **every channel halved** —
`Ui_ItemTextStyle`'s one shift, which shows up as ink 247 lit against 123
unlit. `verify.py: ui text render`.

> **But the alignment is often not in the record.** The start menu's four
> buttons store bank C as **zero**, which reads as the default — left, against
> the edge of a 640-wide row. They are centred in the game, and what centres
> them is one line of `Ui_OpenStartMenu`:
> `I2D_SetFlagOnAllRows(0x004CE820, 0x80000010, 1)` — a **broadcast** over
> every item of list 0, which `Ui_ItemTextStyle` turns into `TEXTP_ALIGN_8`.
> So a screen's open callback sets flags its items do not carry, and reading
> the record alone gets them wrong. `Ui.open_flags` recovers those calls from
> the callback's bytes the way `Opt_BindRow`'s bindings are recovered, and
> `item_flags` applies them; `verify.py: ui open flags` measures the result
> where it shows, as equal margins either side of the ink.
>
> Precedence, from `Text_LayOutBlock`: the markup is read **during** layout,
> after the parameter block, so a `{C}` in the string beats the item's flags;
> with no directive the item's alignment stands; with neither it is 2, left.

### Driving it: `/ui`

`python3 tools/omkweb.py` then `http://127.0.0.1:8752/ui`. The screen list, the
panel's items drawn as rectangles at their **real coordinates** over the
shipped artwork, and the fourteen input bits on the arrow keys, Enter and
Escape — plus a text box for the name field, which answers the character
channel rather than the bits.

The page is **stateless**: it sends the whole key history and the server
replays it, since the model is deterministic. Undo is dropping the last key.
`verify.py: ui page` runs the same data path over all 37 screens and six key
sequences without a server, and asserts the two shapes that were bugs when the
sweep first ran — a dead `(ELIMINE)` screen must come back as an error rather
than raising, and a walk that *closes* a screen must come back as `closed`
rather than dereferencing a null panel.

### How much of the interface this actually covers

Five panels walk exactly — the start menu, its confirm dialog, the name field,
the LIFT's grid and the OPTIONS page tree, plus the load panel's branch. The
**other 29 screens keep their own native hooks** and are not modelled: the ten
shops share one panel hook (`0x004AEE00`), the terminal family one list hook
(`0x004AF300`), and `SAVE GAME`, `PAUSE GAME` and `HIGH-SCORE` have their own.
Driven directly they log an unmodelled hook and stop.

That is the honest number, and `verify.py: sim: ui coverage` keeps it honest by
counting it. The half that matters is the second: **no screen may produce an
answer through an unmodelled path**. A walk that steps over a hook it does not
have sets `approx`, `ui_answer` then refuses, and the simulator falls back to a
supplied number rather than a plausible wrong one.

### What running it cost, and caught

The first model moved between lists on left/right and reached the right answer
for the **wrong reason**. The engine does not do that: `Ui_DispatchInput` gives
the **panel's** `+16` hook priority, and the start menu's confirm dialog
(`sub_0047A230`) moves between its name field and its buttons with **up and
down**. Left/right on that panel fall through to the list instead. The model
now carries that hook transcribed, and marks a walk `approx` when it steps over
a panel hook it does not have — `ui_answer` then refuses and falls back, rather
than reporting a number it reached by luck.

> **This is what caught the `UI_GridMenuInput` mis-attribution.** Walking the
> start menu showed a 4-item vertical list where [`SCRIPT_VM.md`](SCRIPT_VM.md)
> §70 claimed a 7-slot grid. The grid is the **LIFT**'s — one reference in the
> whole image, to that screen's list, whose 7 items match the 7 floor labels
> in `IAM\Lift`. Reading alone had let a plausible sentence stand for two
> sessions; running it did not.

### Against the engine: the menu captures

`tools/goldentrace.py run --keys return,down,return` plays a scripted key
sequence into the running game, which is what a menu capture needs. What it
can and cannot show is fixed by the mechanism: the engine announces **nothing**
for `ui.open` — its `.TAG` domain is `None`, and `vm_announce` reads that off
the handler — so **no keystroke can ever appear in a trace**. A menu is
visible only through what the *script* does around it.

**The suspension.** The handler writes 6 into the script's status word, so
`AREA 118 +4` stops dead at its `ui.open`. That script is the boot area's —
`GRID` is not a place — and it is the only one in the game that opens `OMK
START MENU` (`ui.open 29, -1, 19`). Left alone, the engine emits the three
operands before it and then nothing for a minute:

    VARIABLES  175    set.var2      Niveau Combat
    VARIABLES  170    set.var.i8    Vie Combat Perte
    OBJECTS    997    media.play    ZVO P660

`traces/menu-noinput.log` is exactly that. *Which* three is not a guess:
`goldentrace.loggable` predicts them from the decode by applying the logger's
own three filters, and the capture matches it operand for operand. Predicting
them from `dialog_disasm.SECTION` instead gives **39**, because the preamble
contains two of the three things the logger drops — `player.become`, a
CHARACTERS operand, and `character.hide -1`.

**The refusal.** `traces/menu-keys.log` plays `return,down,return` through
`run --keys`, and the one thing it says flatly is that **the screen was never
answered**: `Interface` is never read, no camera ever fires, and the capture
holds nothing but the same three-operand block over again. The script never
resumes past its `ui.open`.

Reading the confirm path explains exactly why, and it corrects the model.
`Ui_ConfirmSelection` (0x0042A750) runs the item's `+40` callback if it has
one; for "Nouvelle partie" that is null, so it enters the `+44` sub-panel and
runs **that panel's builder at `+4`** — 0x0047A050, which clears the name
buffer at `0x0069BDA0` and zeroes the cursor at `0x0065 7994`. The player is
then expected to type. `Confirmer` (0x0047A2B0) opens:

    0047A2B0  mov  eax, [dword_657994]   ; the name field's cursor
    0047A2B6  test eax, eax
    0047A2B9  je   0x0047A35E            ; empty name -> straight to the ret

On that path it writes **nothing** — not `dword_930750`, and not the screen's
state word (`mov [esi+8], 3` at 0x0047A34D, which is what closes the screen
and lets `UI_SendAnswer` fire). So confirming with an empty field leaves the
screen open and the script suspended for ever, which is precisely the capture.
A scripted `return,down,return` never types, so it never could have answered.

There is a second refusal one branch later: `sub_408CE0` looks the typed name
up in the save directory and, when it is already there, 0x0047A328 shows a
message and 0x0047A336 forces `eax = -1`, which the `cmp eax, -1` at
0x0047A348 turns into the same silent no-answer. A duplicate name cannot start
a new game either.

> `tools/sim/ui.py` answered **1 unconditionally** until this was read —
> `ANSWER = {0x0047A2B0: 1}` with no gate — so the walker would have started a
> new game from an empty field that the engine refuses. It now carries
> `ANSWER_NEEDS_NAME`, `Ui.name` and `Ui.type_name`, and `ui_answer` types a
> name because typing is as much the player's half as the presses are.
> `verify.py: sim: ui`, `sim: name field` and `ui page` each now assert both
> arms — 1 with a name, nothing without one.

**What the capture does NOT establish** is the *number* of repetitions. Four
runs with identical scripted input gave one, two, three and four copies of the
block, and no key-by-key account of that survives scrutiny: the capture carries
no unique anchor, so even attributing the block to `AREA 118 +4` rests on its
matching that script's predicted prefix rather than on attribution. Something
re-runs the script and how often varies; that is written down as an open
observation, not a reading.

**The branch.** Once something *does* answer, the script reads `Interface`
(variable 19, the variable `ui.open`'s field 2 names) and takes one of two
arms. Both are in the decode:

| `Interface` | arm | what it emits | capture |
|---|---|---|---|
| `== 0` | falls through | `CAMERAS 2152, 2153`, `OBJECTS 753`, `2154, 2158`, then `end` | **none** |
| `!= 0` | `jmp_if_false` | `CAMERAS 2172, 2148`, **`dialog.start 272`**, then `area.goto 222` / `scene.load 55` into the Impasse | `intro.log` |

`verify.py: golden: menu` reads both arms out of the decode rather than
hard-coding them, so a changed operand length or jump convention breaks the
check instead of silently re-pointing it. The jump convention corroborates
itself on the way: `jmp_if_false 59` lands **exactly** on the second arm's
first instruction.

The `== 0` arm is **still uncaptured**, and that is the standing gap: §4.6
records that replaying this script from `IAM\START` "takes the `Interface == 0`
arm the capture did not", one of the reasons window attribution stays opt-in.
It was seen twice in the session that wrote this section — both times in runs
whose trace file was then destroyed by the surviving-process bug
([`RECONSTRUCTION.md`](RECONSTRUCTION.md) §4.6), and neither reproducible
afterwards by scripted input alone.

> **What these captures do NOT establish, and why.** Key delivery through
> System Events is **intermittent**: one run reported "3 of 3 keys played" and
> the game acted on none, the next reported the same and acted on all three.
> `send_keys` now re-focuses the game before *every* key, which is the only
> cause found, but the flakiness is not fully closed. So a capture showing no
> answer is not evidence that the keys did nothing — a dropped key and a key
> with no effect are identical in a trace. Runs playing RETURN alone, and DOWN
> then RETURN, both came back at 3 events, and that is recorded as **not
> established** rather than as the confirmation of the
> `+40`-callback-else-`+44`-panel rule it would otherwise look like. Any null
> result here needs a positive control in the same run — which is exactly what
> `menu-keys.log` supplies for the reopen, since its preamble count proves the
> keys arrived.

---

## 4. The options menu

**74 items of 140 bytes at 0x004DA574**, bounded by `dword_4DCE48` — the
sentinel the conflict scan in `Opt_RebindKey` stops at, and it is exactly
74 × 140 past the value array. `+136` counts **1..73 and then 0**, so the
table terminates itself.

| off | field |
|---|---|
| +0 | the **type** — `Opt_BindRow`'s switch: 0 choice, 1 slider, 2 header, 3 keybind, 4 device, 5 defaults, 6 back |
| +4 / +8 | how many choices, and which one is current |
| +12 / +16 | the **apply** and **read-back** hooks |
| +24 | the row's label, an index into `IAM\Options` |
| +52.. | the choice captions, same indexing |
| +92.. | the values those captions stand for |
| +132 | a dirty bit — set when a value changed and no apply hook ran |
| +136 | the record's own index plus one; 0 in the last |

Every one of the 74 labels and 35 of the 37 choice captions resolve inside the
shipped `gamedata/IAM/Options`, and they read as the menu:

```
 3 choice   Distance de clipping    Très proche=25  Proche=50  Intermédiaire=100
                                    Loin=150  Très loin=200
 6 choice   Niveau d'activité dans les rues   Très faible=0 … Très important=4
 7 choice   Niveau de détail        Faible=0  Intermédiaire=1  Important=2
16 choice   Difficulté des combats  Facile=0  Intermédiaire=1  Difficile=2
18 choice   Caméra de combat        Vue de dos=0  Vue de côté=1
```

The two that do not are rows 23 and 24, `Sensibilité horizontale` and
`verticale`: their caption array is -1 throughout because a slider shows a
number, and `Opt_ApplyMouseSensitivity` reads +92 indexed by +8 directly.

**+16 is the read-back, +12 the apply**, and the pairing is provable rather
than guessed. `Opt_ReadResolution` writes 0x004DA690 and 0x004DA694, which are
item 2's +4 and +8 — it fills in the mode list and the current mode. And
`Opt_ApplyAccel3D`, after switching renderer, calls `off_4DA728`, `off_4DA7B4`,
`off_4DA840`, `off_4DA8CC` and `off_4DA958` with the arguments 3, 4, 5, 6, 7:
those five addresses are the **+16 fields of items 3 to 7**, called with their
own ids. Changing the renderer re-reads the five video settings.

### The pages

**13 page records of 104 bytes at 0x004DD3D0** — `+0` the parent page, `+4` the
builder, `+8` the on-leave hook that saves the selection into `+80`, `+32` the
live page struct it fills. Every parent pointer resolves to another record in
the table; two are roots. A builder calls `Opt_BindRow` sixteen times, once per
row widget, and `Opt_LayOutPage` then spaces the non-empty ones down the
screen.

```
Options (page 1, Opt_PageRoot 0x00491200)
├── Vidéo         Résolution · Distance de clipping · Ciel · Ombres ·
│                 Activité dans les rues · Niveau de détail · Accélération 3D
├── Audio         Volume dialogues / musiques / effets · Son 3D
├── Options       Difficulté des combats · du shoot · Caméra de combat
└── Contrôles
    ├── Configuration du clavier et de la souris ─┐
    ├── Configuration de la manette ──────────────┤  (both, device 1 vs 2)
    │   └── Contrôles Aventure · Nager · Tirer · Combat   — 44 keybinding rows
    ├── Configuration de la souris   Sensibilité H/V · Souris inversée
    └── ⟨page 12⟩  Configuration de la manette · Force FeedBack   ← unreachable
```

Two findings out of that tree, and the second is the kind that needs the code:

* **The gamepad shares the keybinding pages.** Item 21
  (`Configuration de la manette`) and item 20 (`… clavier et de la souris`)
  point at the *same* page. The keybinding builders pick their own title row
  from `dword_4DD628` — item 20 when it is 1, item 21 when it is 2, no title
  otherwise — so one set of four pages serves both devices, with the heading
  swapped. Reading only the page links would have said the gamepad entry was
  broken.
* **Page 12 is built and unreachable.** It has a well-formed record whose
  parent is `Contrôles`, a real builder at 0x00492890 binding items 26 and 27
  (`Configuration de la manette`, `Force FeedBack`) and a `Retour`, and
  **nothing in the binary references its address** — IDA does not even label
  it. Force Feedback ships as a setting no menu can reach. Item **15,
  `Sous-titres`,** is the same story one level down: a complete choice row with
  both hooks, on no page.

### An extraction trap in this range

Five functions in the options module are **absent from `Runtime.exe.c`
altogether**, the root page builder among them. `Opt_PageRoot` (0x00491200),
0x00492890, 0x00492A70, 0x00492AA0 and 0x00492AD0 have no `sub_` label in the
listing either; `python3 tools/asmfn.py 491200` prints them. Anything that
counts call sites in `readable/src` under-counts here, and "no builder binds
item 0" was wrong for exactly that reason — the builder was one of the missing
five.

That is also why `Opt_BindRow` was called `I2D_SetState` until now. The
2026-08-28 hub sweep named it from a caller count of 176 and read that as a
hub; all 176 are in this one module, and what it actually does is bind a row
of the options menu.


---

## 5. The text — 13 fonts, and the markup

**solved.** `Text_DrawBlock` (already CLEAN) only unpacks its parameter block;
the work is `Text_LayOutBlock` (0x0043F3E0), which parses the markup, wraps the
lines and emits styled runs, and `Text_DrawRun` (0x0043EA10), which rasterises
one run. `tools/fnt.py` reads the fonts; `verify.py: ui fonts` runs the checks.

### The font table

**13 records of 20 bytes at 0x004C7090**, walked by `Font_Find` and ended by a
zero id. `+0` is a **letter**, `+4` the file stem, `+8` the kerning added to
every advance, `+10` the advance used for a character the font has no glyph
for, `+12` the line height, `+16` the blob once `Font_LoadAll` has read
`fonts\<NAME>.FNT`. **All 13 named files ship in `gamedata/FONTS`, and there are
exactly 13 there.**

| id | font | kern | default | line | | id | font | kern | default | line |
|---|---|---|---|---|---|---|---|---|---|---|
| `I` | MENUINTR | 2 | 15 | 36 | | `V` | VOIXOFF | 1 | 6 | 23 |
| `M` | MENUSAVE | 1 | 8 | 17 | | `1` | GENERIC1 | 2 | 6 | 12 |
| `D` | DIALOGUE | 1 | 6 | 17 | | `2` | GENERIC2 | 3 | 6 | 18 |
| `R` | DIALSELE | −1 | 6 | 17 | | `3` | GENERIC3 | 3 | 6 | 24 |
| `P` | PARCHEMI | 1 | 6 | 17 | | `L` | SMALL | 0 | 6 | 12 |
| `C` | COMPUTER | 0 | 6 | 14 | | `S` | SNEAK | 1 | 6 | 20 |
| `J` | JOURNAL | 1 | 6 | 17 | | | | | | |

**This is what `item+36` has been all along.** 74 is `J`, JOURNAL — the
default every option row carries and `Text_DrawBlock`'s own default; 83 is `S`,
SNEAK — what `Opt_BindRow` gives a heading, and it is two pixels taller; 76 is
`L`, SMALL — the sub-640×480 override, and it *is* smaller (line 12 against
17). The ids read as ASCII because they are meant to be typed: `{fS}` in a
string picks SNEAK.

### `FONTS/*.FNT`

**solved, 13/13.** One blob, no header:

```
+0      256 glyph records of 8 bytes, indexed by the character code
          +0  u16  the pixel data's offset in EIGHT-BYTE UNITS; 0 = absent
          +2  i16  the bottom edge, relative to the baseline
          +4  i16  the width (the advance is this plus the font's kerning)
          +6  i16  the height
+2048   the pixels: width x height bytes a glyph, row-major, top row first
```

The checks it has to pass, and does: every glyph's block starts at or after
2048 and ends inside the file (**0 failures over 2899 glyphs**), no two blocks
overlap (**0**), the pixel data starts at exactly 2048 in all 13, and each font
covers **exactly the codes 33..255** — the same 223 in every one, with space
deliberately absent so it falls to the `+10` default advance.

And it draws. `python3 tools/fnt.py journal A g É ?` prints recognisable
letters with their accents, which random bytes do not.

**A pixel byte is a coverage level 0..31, not a colour.** `Text_DrawRun`
rebuilds a 32-entry ramp of the requested colour whenever it changes —
`word_52F5B8`, which IDA itself types `__int16[32]` — and each non-zero byte
indexes it. So the fonts are greyscale antialiasing that takes the text's
colour at draw time, and zero is transparent. Over all 13 files, **485 875 of
485 877 pixel bytes are in 0..31**.

> **The other two are a shipped defect.** Both are in `SMALL`'s `!` (2×8, at
> byte 2048): 125 and 133, which index 186 and 202 bytes past the end of the
> 32-entry ramp and would draw whatever globals follow it. One glyph, one font,
> two bytes out of 485 877 — asserted at exactly 2 in `verify.py: ui fonts`, so
> it stays a known quantity rather than drifting into "close enough".

Text is composited into a scratch surface and then pushed through
`I2D_BlitSurface`, which is why it layers against the widgets instead of being
painted over them.

### The markup

`{` opens a directive and one letter is one command. Several **chain inside a
single brace** — `{fCC}` is font `C` then command `C` — and an **unrecognised**
letter falls through to the line flush, which is how `{P}` works as a paragraph
break without being implemented.

| directive | effect |
|---|---|
| `f<letter>` | the font, by id letter. **Ignored below 640×480** |
| `I<9 digits>` | the colour, as three 3-digit **decimals** |
| `X<6 digits>` | move to (xxx, yyy) as **percentages** of the screen |
| `B` | blink — white on the frames oscillator 1 is high |
| `C` `D` `F` `G` | the four horizontal alignments |
| `H` `L` `M` | top / bottom / middle, off the font's line height |
| `E<byte>` | sets `dword_907A28` |

> **Correction.** [`FILE_FORMATS.md`](FILE_FORMATS.md) §5b4 recorded these as
> "`{f...}` format, `{C}`/`{fC}` centering, `{I RRGGBB}` color triplets". The
> first two are right; the colour is **nine decimal digits**, `RRRGGGBBB`, and
> the shipped text says so — `{I255120045}`, `{I255255255}`. `{fC}` is not
> centring either: it is the COMPUTER font.

The shipped text agrees with the decode from the other side: **329
`{f<letter>}` directives across the interface files, and every operand is one
of the 13 ids** — none names a font that does not exist — alongside **111**
well-formed `{I}` colours.

`[` and `]` bracket spans and are **counted**: when the span index reaches
`params[11]` the alternate style is swapped in and restored at the close. That
is how one string carries a label and a value in different colours, and it is
what an item's `+30` selects.

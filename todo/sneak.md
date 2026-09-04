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

**`sub_42B520` is read now (2026-09-04)** and so is the rest of the mode. It
raises **event 37** with `obj | 0xFFFF0000` and answers whether the result is
1, and case 37's first arm is:

    if (objectId == u16(GLOBAL, 64))  { result 1; dword_4E6C70 = 1; }
    else                              { result 2; dword_4E6C70 = 0; }

`GLOBAL +64` is **object 330**, and `dword_4E6C70` IS THE RECIPE GATE — which
the port already carries as `globalSpellItem`, so this confirms a reading
rather than adding one. Case 37's second arm completes it:

    recipe = sub_409650(second, first)
    if (!recipe || dword_4E6C70 != recipe+6) result 2       // no recipe, or the gate
    else { remove both; ObjectList_InsertFront(list, recipe+4); dword_4E6C70 = -1; }

and `sub_49BC60`'s combine branch (`loc_49BDD6`) drives it: if slot `670BE8`
is empty it fills it and returns; otherwise it calls the combine, plays
interface sound **12** on success or shows text **35** on failure, and either
way reinstalls the INVENTORY page (`sub_42A370(screen, unk_4DEE50)`).

**And the shipped gates make one of the two paths fruitless.** The 11 recipes
carry gate **0 five times and gate 8 six times — never 1**:

    0 :  18+7->33,  108+156->38,  26+20->99,  607+464->286,  710+20->709
    8 :  379+159->367, 525+391->378, 387+383->382,
         358+379->721, 379+359->722, 360+379->723

`dword_4E6C70` can only be 0, 1 or -1, so the six gate-8 recipes can never
fire — which this repo already knew — and, newly, **starting a combine with
object 330 sets the gate to 1, which matches nothing**, so that arm can never
produce anything either. The reachable combine is: first object NOT 330 (gate
0), second object, five recipes. Implement that path and record the other.

What is missing is only the UI mode: `dword_670BE0`, the two slots, disabling
the verb list, and the second row confirm going to the combine instead of to
the verb panel.

Until it is done, the honest behaviour is to REFUSE `Utiliser sur` rather
than run `Utiliser`'s arm under its name.

### 2c. ~~Three of the five pages have empty rows~~ — CLOSED, and the question was wrong

**Read 2026-09-04, and there is nothing to implement.** This entry said "the
player's bio, his statistics and the memos each ask a different list and which
list each one asks has not been read". Both halves were wrong.

**Only THREE panels carry the row list at all**: `slider`, `inventory` and
`memory` (plus the verb panel, which borrows it). `identity` and `options` have
no `0x004DE6F0` in them, so their rows were never the gap — they show a
character view and an option tree, and there is no "bio" or "statistics" row
list anywhere.

**And the memory page is empty by the code.** Its `panel+4` builder sets
`word_4DE6F0 = 5` (five widgets, not nine) and `dword_670CB8 = 2`, then reads
its count from **`dword_4DE708`** — which is **never written anywhere in the
image**: a static `dd 0` with seven reads, no store, no `offset`, no `lea`. So
the count is permanently 0 and the selected memo `dword_4DEAD4` permanently -1.
The two other sites reading it always take their zero arm.

That is the same shape as the options menu's **page 12, built and unreachable**
— a page the interface constructs and the game never fills. The port leaving
those rows empty is CORRECT, and correct for the reason the code gives.

`verify.py: sneak memory page` asserts the three/two split, the nine shipped
widgets, and the zero writes — with a POSITIVE CONTROL on `dword_670CB8`
(three writes) so that zero cannot be a broken scanner.

Still not modelled, and now known to be invisible: the builder shrinking the
list to **five** widgets on that page. With no rows it cannot be seen.
The row confirm is still refused (`widgets.cpp:869`), which is right.

### 2d. The identity page — TWO sub-sections, and it is the biggest thing left

**Read 2026-09-04 from two captures of the original a player supplied.** The
page has two tabs across the top, `Identity` and `Characteristics`, sharing
one character view on the left:

* **Identity** — Name (`KAYL 669`), Age, Sex, Blood Type, Height, Weight,
  Eyes, Job (`Investigating Agent`), and two prose lines, `Signs` and
  `Interests`. All of it per-character: the text changes with whose body the
  player is in.
* **Characteristics** — Energy, Attack, Fight Experience (`Initiate`, a WORD
  not a number), Body Resistance, Speed, Dodge, Mana, each with a filled BAR
  behind the value.

The widget tree already has the whole structure, in list **`0x004DE900`**
(hook `0x0049C160`), and it matches the captures item for item:

| item | rect | what |
|---|---|---|
| `0x004DE780` | (187, 30) 202x22 | the **Identity** tab, string 10 |
| `0x004DE7C8` | (389, 30) 202x22 | the **Characteristics** tab, string 11 |
| `0x004DE810` | (250, 100) 300x270 | the Identity content |
| `0x004DE858` | (250, 100) 300x270 | the Characteristics content — **same rect**, so an ALTERNATIVE |
| `0x004DE8A0` | (0, 50) 360x300 | the **character view** |

The switch is read and is small. The page's `panel+4` builder does

    sub_428FF0(&item_4DE810, 0x40000001, 0);   // Identity content DRAWN
    word_4DE902 = 0;                            // the tab list's selection
    sub_428FF0(&item_4DE858, 0x40000001, 1);   // Characteristics HIDDEN
    sub_4296D0(&list_4DEC58, r, g, b);          // the echo bar, in the page's blue

and `sub_49C160` — the list's own hook — is `sub_42A930` (the LEFT/RIGHT
mover, which the port already has) followed by a two-case swap of that same
`0x40000001` flag on the two content items. Nothing harder than the slider
page's mover.

**What is NOT established is where the text comes from.** Both content items
ship `string -1`, `text 0` (item `+24`) and `textFn 0` (item `+32`) — no
string id, no pointer, no callback — so something outside the item draws into
that 300x270 box, and the per-character bio has to be found in the data
(the actor table's 276-byte record is the obvious first place, and
`player.become` announcing to CHARACTERS the second).

> **A trap on the way, and it is CLAUDE.md 1's exactly.** The listing shows
> `off_4DE810 dd offset unk_6400FA`, which reads as a pointer to a shared
> text buffer and is not one: item `+0` is the X and `+2` the Y, both int16,
> so the dword is `0x006400FA` = **y 100, x 250** — the item's own
> coordinates. IDA saw an address-shaped dword and invented `unk_6400FA`.
> Ten minutes went into "what fills that buffer" before the field map
> settled it. There is no buffer.

And the character view itself: `sub_4778E0` builds a camera from the player's
own model at `kCharacterDistance` = `0x42EC3871` = 3.0 / 0.0254, three metres
for a standing man. The constant is lifted and carried in `ui/models.h` —
kept there precisely because it shares `sub_478DE0` with the item previews,
and telling the two apart is what stopped the previews rendering two pixels —
but nothing draws the character yet.

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
| 1 | **row scrolling**: read `sub_42AFF0` properly, model the window in the binder, make `sub_49C050` move it. Check: a carried list of >9 reaching its last row, which today is unreachable | **done 2026-09-04** — a CENTRED window (the cursor moves to the middle widget, then the window moves under it); `bindRows`'s second argument is the WINDOW, the tag lives in widget 0's `+0x3C`, and the two end marks are `0x100000`/`0x200000`. `verify.py: engine row window` drives 12 rows through 9 widgets, reaching row 11 of 11; shown to fail at row 8 with the window pinned. **The event-30 raise is RECORDED, not raised** — the walk has no channel, so a caller must ask for the preview off `rowOf(selected())` |
| 2 | `Utiliser sur`: read `sub_42B520`, port the combine mode, the two slots and the second selection onto the already-ported `Inventory::combine`. Until then, refuse it rather than run `Utiliser`'s arm | **done 2026-09-04** — the mode, the two slots, the disabled verb list and the second row confirm; the slots hold ROW INDICES (`item+0x3C`), which the caller resolves. `verify.py: engine combine` asserts the gate histogram (5 at 0, 6 at 8, **0 at 1**), a real recipe through the mode (18 + 7 -> 33) and the spell arm answering -1; shown to fail with the gate ignored |
| 3 | which list the bio / statistics / memo pages ask for, and fill them | **done 2026-09-04 — NOTHING TO FILL.** Only slider/inventory/memory carry the row list; identity and options carry none. And the memory page's count `dword_4DE708` has seven reads and ZERO writes in the image, so that page is built and permanently empty — the port is already right. `verify.py: sneak memory page`, with a positive control so the zero means something |
| 4 | `Text_LayOutBlock` — the real wrap, against a caption that today wraps wrong | |
| 5 | the hand attach, so a used object is visible for the frames it is held | |
| 6 | `Object_ApplyEffect` and its context gate | |
| 7 | the identity page: the two-tab switch (small - `sub_42A930` plus a flag swap, both already ported in pieces), then the character view, then the per-character TEXT, whose source is not yet found | |
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

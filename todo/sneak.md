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

**And since 2026-09-18, the ECHO BAR and the ROW MARK** — the two native
callbacks on the Inventaire page that `screendraw.cpp` had listed among "the
other thirteen [that] draw nothing". `sub_0049DC20` composes the device's
status line (`ScreenComposer::echoBarText`), which is the only place the
player's seteks and anneaux are shown anywhere in the interface, and
`sub_0049C090` puts a second `Ui_DrawItemFill` behind the row a verb will act
on. `docs/UI.md` 3b has both; `verify.py: engine: sneak echo bar` walks one
route through five of the bar's seven arms and both branches of the mark's
gate.

Two things that came out of porting them, both recorded where they belong:
the rows already fill once from their own bank-B `0x10`, so the hook's fill
is a SECOND quad and not the only one a row gets; and the tile previews pass
distance **0** to `sub_478DE0` and take its bounding-box fit — `0x42EC3871`,
which `engine/src/ui/models.h`'s header called the sneak's, is pushed at one
site in the listing and it is the identity page's character view.

## 2. What is left

### 2z. What the echo bar and the row mark did NOT close

* **The bar's TRANSIENT arm** (`byte_6A4CA0`, oscillator 0, 5000 ms) is
  ported and has no writer in the port. Its two writers in the image are both
  inside `sub_49BC60`: `loc_49BD23` flashes screen string **42** when the
  slider call `sub_452570` refuses, and `loc_49BE30` string **35** when a
  combination finds no recipe. `ScreenComposer::setEchoMessage` is where
  either would arrive.
* **The mark's COMBINE branch** is ported and unexercised, and the reason is
  §2b, which is less closed than that section now reads: the mode itself IS
  ported (`beginCombine`, the two slots, the verb list off), but a single
  press still runs `sub_49BEA0`'s arm as well - measured 2026-09-18, the log
  reads `Utiliser sur ... combine opened, gate 0` and then
  `'Notice MK400' -> IN HAND`, and the device closes on the use. So there is
  no frame in which the walk stands on the verb panel with the mode open, and
  nothing can mark from the slots. A verb-dispatch fault, not a drawing one.
* **The dead call at `0x0049DCA6`** — `sub_42AA00(screen, row, B)` into a
  buffer nothing reads. Left out; its only effect is one extra event-33 raise
  per draw of the bar.

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

### 2c. Three of the five pages have empty rows — HALF CLOSED, and the other half was WRONG

> **CORRECTED 2026-09-16, by a reader playing it.** They reported *"info page
> contains important info you may have heard in dialog (and maybe read from
> document but i am not sure for this last point)"* — and they are right. The
> paragraph below says the memory page is "empty by the code" because
> `dword_4DE708`, the count its builder reads, "is never written anywhere in
> the image". **`0x004DE708` is `0x004DE6F0 + 0x18` — the ROW LIST's own
> `+24`**, exactly where the shops' count (`0x004E3640 + 0x18`) and
> MULTIPLAN's (`0x004E5670 + 0x18`) live, and `sub_42ADD0` writes it at
> `0x0042AF70` (`mov [esi + 0x18], eax`) from the channel's event 29. The scan
> was looking for an absolute store to a named symbol and could not see a
> write through a pointer, and "no absolute store" was read as "nothing fills
> it". The page binds **object list 2, the memo journal** (`sub_42ADD0(rows,
> 0, 2)` → event 25 on list 2), and 76 `inventory.add` sites in the world
> scripts put `Memo NNN …` objects there. What the PORT does — leave those
> rows empty — is therefore a GAP, not fidelity. `verify.py: sneak memory
> page` is rewritten around the arithmetic; the fill is the open work, with
> the memo READER page `0x004DEFF0` (which the row confirm installs, and which
> is not lifted into `tables/ui_widgets.json` at all) behind it.
>
> **FILLED 2026-09-16** (`7e56033`): the viewer binds the page's rows from
> object list 2 and names them through the channel, and a render shows the two
> given memos as *Moi :* and *Panneau Bibliothèque :* under the page's own
> yellow. `verify.py: engine: sneak memos`, red under both mutations with its
> own output: the row source switched to the carried list (`object list 0,
> 1 rows: 171`) and the fill gated back to kind 0 (no line at all).
> **And the check's first version could not have caught the first of those**:
> the log line named the list with a LITERAL 2, so a page reading list 0 still
> printed "object list 2" and only the ids differed. The line now takes the
> number from the same enum the rows were read with - the same weakness, and
> the same fix, as the character line's hard-coded bank name a day earlier.
>
> **THE BODY BOX** (2026-09-16, `5bdb50f`): the page's own box `0x004DEA98`
> (hook `0x00477F60`, its tag `dword_4DEAD4`) shows the SELECTED memo's
> DESCRIPTION - the row's name is the heading, the record's description is the
> text - so the memory page is complete without the reader page behind it:
> `0x004DEFF0` carries the same four lists with the box current, and needs no
> lift after all. The composer reaches it through the examine-text branch,
> which now keys on that hook as well as the examine page's `0x004780A0`.
>
> **And its log line was the THIRD check in two days to report an INTENTION**
> (fixed in `1c7b9ea`). `sneak: memo body` was printed beside the FILL, out of
> the description the page had just been handed, so
> `comp.setExamineText(nullptr)` - the box cut off from the composer entirely -
> left `engine: sneak memos` GREEN. The composer already counts what it laid
> out (`ScreenFrame::textLines`, incremented only in that branch) and the
> viewer's main `comp.draw(...)` was throwing away the frame that carries it;
> the line is now printed AFTER the draw with that count in it. Both mutations
> are red with their own output: the row pinned to 1 (`row 1 id 915 'Panneau
> Bibliotheque :', 192 chars, 4 lines drawn`) and the text cut off (`row 0 id
> 913 'Moi :', 308 chars, 0 lines drawn` - the fill's half of the line
> UNCHANGED, which is exactly what the weak version could not see). The
> general form is now in CLAUDE.md §1: a line printed where a value is handed
> over reports the intent, so print it from what the consumer produced.
>
> **THE PAGE WAS NOT USABLE, and the rows were the half nobody had read**
> (2026-09-16, the reader again: *"I can't select anything in the memo list (I
> can just select the page)"*). Two functions settle it, and the port had
> opened neither:
>
> * the panel's `+16` hook `0x0049D8B0` is a WRAPPER - it shows the body box
>   when the current list is the rows, hides it otherwise, and tail-calls
>   `sub_42A710`, the generic mover. `press` matched the mover by ADDRESS
>   (`panel_->hook == moveListsHook()`), so this page fell to "unmodelled panel
>   hook" and LEFT/RIGHT did nothing: the rows could never become current. The
>   identity page worked only because its wrapper was hand-modelled in an
>   earlier step. **Key on the behaviour, not on the caller.**
> * the builder `0x0049D750` writes `word_4DE6F0 = 5` (five row widgets here,
>   nine on the inventory and slider pages - all three writes in the image are
>   those builders) and ENDS by HIDING the body box, after setting its tag from
>   the selected row only when the rows are already current. So the body is not
>   drawn when the page opens; it lights on the first UP or DOWN inside the
>   list, because the panel hook runs on every press and sets the flag from the
>   list current BEFORE the move. The port drew it at open - its own invention,
>   and the opposite error to the one above it.
>
> Ported as read: `kHookSneakMemoryPanel` gets its own `press` arm,
> `state_->rowWidgets` carries the per-page widget count (9 restored in the
> inventory and slider arms), and `play.cpp` fills the body only while
> `walk->memoBodyShown()`. Played headlessly: opening the page draws rows and
> NO body; RIGHT into the rows then DOWN draws memo 915 at 4 lines.
> `verify.py: engine: sneak memos` walks that route and asserts the ordered
> pair, so a body drawn at open (which would print an earlier `row 0` line)
> fails it. **NOT covered**: the five widgets - two memos bind the same two
> rows at 5 or at 9.
>
> **THE BODY DREW A CLUE THAT IS NOT IN THE ORIGINAL** (2026-09-16, the reader
> comparing the two side by side: *"you added some texts to the memo ... the
> added texts kinda look like the clues that can be bought on the save
> screen"*). They are clues. A memo record's description holds TWO bracketed
> sections, the memo and a clue, and 37 of 1002 records carry one - 342 and 338
> share theirs, 336 has its own, which is exactly the "same for two, different
> for the third" that was reported. The WIDGET picks the section:
> `sub_477F60`'s `mov ax, [ebx+1Eh]` is the item's `+30` (already lifted as
> `textArg`), and unless it is -1 the text goes through
> `sub_43FEA0(index, src, out)`, a depth-aware extractor of the index-th
> top-level `[...]` group with the two `{TEXT ERROR}` literals for its failure
> arms. The memo body carries 0; the sneak's examine box `0x004DE710`,
> MULTIPLAN's `0x004E57E0` and `0x004E2B10` carry -1, which is why a long
> notice still draws whole. Ported as `omk::extractTextSection`, and the body
> line now reports the characters the COMPOSER laid out (`ScreenFrame::
> textChars`), not the field's length: memo 915 is 192 bytes and its section 0
> is 102.
>
> Mutation red with its own output (`96a0f48`): with the section index ignored
> (`const std::string body = *examine_;`) memo 915 goes back to `192 chars,
> 4 lines drawn` from `102 chars, 2 lines` - the clue returned to the box,
> which is precisely what the reader saw in the port and not in the original.
>
> **THE PREVIEW NOW APPEARS ON SELECTION** (done): the original shows it as
> soon as the line is selected, so a panel hook is the panel's PER-FRAME TICK
> and not an input handler - given an input word matching neither of the
> mover's bits it returns 0, and the flag is re-evaluated every frame. The port
> stored it on a press, from the list current BEFORE the move, and lit the box
> a press late. `UiWalk::memoBodyShown()` derives it from the current list now.
> Played headlessly on both saves: RIGHT onto the list draws memo 913 (185
> chars) / 342 (74), and DOWN follows to 915 (102) / 338 (229).
>
> **THE MEMO READER IS PORTED** (done): ENTER on a memo runs `sub_49BC60`'s
> kind-2 arm - `push offset off_4DEFF0`, then the shared tail
> `sub_42A370(screen, panel)` - which INSTALLS the reader page. It was not in
> `tables/ui_widgets.json` at all, so `exetables.py`'s `CODE_NAMED` gained
> `0x004DEF88: [0x004DEFF0]`, the way the verb panel and examine page are
> lifted; the lifter's seven counts moved with it (child panels 26->27, lists
> 166->170, items 698->718, items naming a child 96->103, non-default hooks
> 60->62, tiled panels 27->28, default-walk lists 106->108) and every delta is
> the page's own four lists, while the two DISTINCT counts did not move at all
> because it reuses the same records.
>
> The page is those four lists with a different CURRENT: `+24 = 2` is
> `0x004DEAE8`, the body box, whose hook is the scroller `0x0042A9A0`. Nothing
> writes that `+24`, so the walk supplies the shipped value as it does for the
> shops' and MULTIPLAN's children. `sub_49D870` zeroes the scroll offset;
> `+16` is 0, so BACK is the way out. Played headlessly: ENTER draws
> `list 2, scroll 0` and three DOWNs give `scroll 3` - the clamp against the
> laid-out height, not a lost press. **Not modelled**: the `0x40400080` the
> builder sets and the leave clears, whose two sites in the image are those.
>
> Both mutations red with their own output (`1093e9c`), and each kills a
> different half: refusing the kind-2 arm again
> (`false && ... rowKind == 2`) makes BOTH `memo reader` lines vanish while
> the four memory-page lines stand - the confirm turns away and the page never
> opens; dropping `curFromBuilder_ = 2` leaves the page open but reports
> `list 0, scroll 0` with the second line gone - it settles onto the TAB
> COLUMN by the move rule, and a scroll that is not standing in the box can
> never move. That second one is the whole reason the shipped `+24` has to be
> supplied by hand: the lift records `current` only where a callback writes
> it, and nothing writes this one.
>
> Both mutations red with their own output (`9bf0faf`), and each reproduces one
> of the two faults exactly: with the hook arm dead
> (`false && panel_->hook == kHookSneakMemoryPanel`) the body line DISAPPEARS -
> the list is unreachable, which is the reader's report, and the check as it
> stood this morning would have passed it because it never walked in; with the
> builder showing the box (`memoBodyShown = true`) an EARLIER line appears,
> `row 0 id 913 'Moi :', 308 chars, 8 lines drawn`, the body drawn at open that
> the engine does not draw.
>
> **WHERE MEMOS COME FROM** (2026-09-16, the reader asked whether they are
> unlocked only by dialogue or also by reading a document - the answer is
> BOTH). Three routes fill list 2: **76** `inventory.add` sites in the world
> scripts; **7** in DIALOGUE ACTION scripts (a conversation node's `ptr[4..7]`,
> across 6 conversations) - the ones "heard in dialog"; and GLOBAL's
> **message-4** handler, subscription script offset 168, which is what the
> sneak's EXAMINER posts (`sub_42B420(tag, 4)`, ported 2026-09-15). That
> handler tests the examined object's id - 89 *Note dossiers archives* (kind
> 16) and 97 *Note Anissa* (kind 15), both documents - plays
> `ZVO P315 DATA MEMORIZED` and adds *Memo 004 Aller aux Archives* /
> *Memo 022 Code Gandhar*. So examining a document in the sneak unlocks memos,
> and the port already posts that message. `docs/GAME_STATE.md`'s list-2 row
> carries the same three counts.
>
> **THE BODY, and the reader page needs no lift after all** (2026-09-16). The
> empty panel at the bottom right of the page is item `0x004DEA98` (400x110 at
> (190, 250), list `0x004DEAE8`), and it is on the MEMORY PAGE itself. Its
> draw hook is `0x00477F60` - the same text drawer every arm of the examine
> page ends in - and it raises event 40 on its own `+0x3C`, which is
> `0x004DEA98 + 60 = 0x004DEAD4`, the very global the row hook writes with the
> selected row's tag when the kind is 2. So the box shows the SELECTED MEMO's
> description, and the reader page `0x004DEFF0` turns out to be the same four
> lists with the BOX as the current one (record `+24 = 2`): its builder
> `0x0049D870` sets `0x40400080` on the box and zeroes the scroll, its leave
> `0x0049D890` clears that, and the memory page's own panel hook `0x0049D8B0`
> flips the box's `0x40000001` by which list is current. Nothing to lift: the
> composer now draws that hook's text and the ported scroller already moves
> it. Rendered: *Moi :* selected, its text wrapping in the box.



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

**Where the text comes from - SETTLED 2026-09-15, see §5b: three draw hooks
on the items, reading the player's properties through event 44.** What
follows is the reasoning as it stood. **What is NOT established is where the
text comes from.** Both content items
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
| 4 | `Text_LayOutBlock` — the real wrap, against a caption that today wraps wrong | **done** — `todo/text-layout.md` (all three steps) |
| 5 | the hand attach, so a used object is visible for the frames it is held | |
| 6 | `Object_ApplyEffect` and its context gate | |
| 7 | the identity page: the two-tab switch (small - `sub_42A930` plus a flag swap, both already ported in pieces), then the character view, then the per-character TEXT, whose source is not yet found | |
| 8 | the other four page builders, each bounded before it is shipped | |

## 5. next-tasks 10 — the IDENTITY, OPTIONS and QUIT pages (opened 2026-09-15)

Picked up after MULTIPLAN (`todo/multiplan.md`), on the reader's go. The
MEMORY page is not in scope: it is empty by the code (§2c, `sneak memory
page`). Every address below was read from the raw image; none of these
functions has a `proc` label.

### 5a. What the port shows today

The headless route (Anekbah, TAB held, RIGHT onto the tab column, UP twice to
the blue identity icon, ENTER):

```
SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables --software --nofmv --no-crowd \
    --save ../traces/save-appart.bin --area 0 --stand 1804,0,-6890,336 --frames 240 \
    --hold "0*40,k15*3,0*30,k205*3,0*15,k200*3,0*15,k200*3,0*15,k28*3,0*60"
```

The identity page opens with its two tab labels, *Identité* and
*Caractéristiques*, and the caption bar - and an EMPTY 300x270 box: no text,
no character. Options and Quit have no page behaviour at all.

### 5b. The identity page (panel `0x004DED80`)

* **Builder `0x0049C100`**: the Identity content `0x004DE810` drawn, the
  Characteristics content `0x004DE858` hidden (flag `0x40000001`), the tab
  list's selection `word_4DE902 = 0`, the echo bar in the page's blue.
* **The tab list's hook `0x0049C160`**: `sub_42A930` (LEFT/RIGHT, already
  ported), then the two content items swap the same flag by the new
  selection.
* **The panel hook `0x0049C1D0`**: while the tab row is current, LEFT/RIGHT
  move `word_4DE902` by the input's `0x1`/`0x2` bits and swap the contents;
  otherwise `sub_42A710`, the generic list mover.
* **The content is three DRAW HOOKS** on the items, which is why §2d found no
  string on them:

  | item | draw hook | what |
  |---|---|---|
  | `0x004DE810` | `0x0049C2B0` | **Identité**: ten lines of label + value |
  | `0x004DE858` | `0x0049CA30` | **Caractéristiques**: seven labels, values, BARS (`sub_49CE60`) |
  | `0x004DE8A0` | `0x004779C0` | the **character**: `sub_4778E0`'s model on a 3 m camera, `I2D_Submit3DView` |

* **Where the text comes from - SETTLED.** Every value is `Game_RaiseEvent(44)`
  (`Actor_GetProperty`) on `Actor_Player()`, through `sub_42B1C0` (an int) or
  `sub_42B1F0` (a pointer); every label is `sub_4767E0` on the screen's own
  `IAM\Sneak`. The pairs, in the hook's order:

  | label (`IAM\Sneak`) | property | record field |
  |---|---|---|
  | 15 Nom | 6 | the string at `+8` |
  | 16 Age | 8 | int16 `+154` |
  | 17 Sexe | 0 | the string at `+108` |
  | 18 Groupe sanguin | 13 | the string at `+124` |
  | 19 Taille | 9 | the string at `+128` |
  | 20 Poids | 10 | the string at `+136` |
  | 21 Yeux | 12 | the string at `+116` |
  | 22 Profession | 11 | the string at `+40` |
  | 23 Signes particuliers | 15 | the string POINTED TO by `+0` |
  | 24 Centres d'intérêt | 14 | the string pointed to by `+4` |
  | 25 Energie | 1 | int16 `+170` |
  | 26 Attaque | 16 | `+160` |
  | 27 Maîtrise du combat | 19 | `+166` - drawn as a RANK WORD, strings 36-39 *Novice* ... |
  | 28 Résistance corporelle | 17 | `+162` |
  | 29 Vitesse | 3 | `+158` |
  | 30 Esquive | 18 | `+164` |
  | 31 Mana | 2 | `+156` |

  The label/property pairing is read off the call order and has to be
  confirmed line by line when each hook is ported (the first label's push is
  not a literal). The port's `readActorProperty` covers every INT here and
  none of the string slots (`props.cpp`'s "pointer slot").

### 5c. The options page (panel `0x004DF058`)

The page has no content list of its own: it HOSTS screen 35, `OPTIONS`, which
the sneak's open loaded hidden underneath (`docs/UI.md`, `sneak chain`).
Builder `0x0049D8F0`: the echo bar's colour, the clock black, and
`UI_SetScreenFlag(UI_FindScreen(35), 0x40000001, 0)` - screen 35 SHOWN; its
leave hook `0x0049D940` sets the flag back. The panel hook `0x0049D960`: on
the input's `0x3` bits, `UI_FocusScreen(35)` - the options menu takes the
keys. The port already walks screen 35's page tree (`sim: options`).

### 5d. The quit page (panel `0x004DF0C0`)

The quit icon `0x004DE118` (string 32 *Quitter le jeu*, bottom right) has its
own callback `0x0049DBF0`: show the Oui/Non list `0x004DEBA0`, select *Non*
(`word_4DEBA2 = 1`), make it the current list. *Non* `0x0049DBC0` hides it and
returns the focus to the tabs. *Oui* `0x0049DBA0` is `sub_409090` - `mov
dword_4E6C9C, 1`, the QUIT REQUEST the pause screen's *Oui* already sets in the
port (a new game at the next pump, not an exit) - and `screen+8 = 3`, the close.
The builder `0x0049D980` hides the pair and resets the list's selection and
the panel's `+24`.

### 5e. Steps

Each ends in a commit, a `verify.py` check shown to fail, and a report.

| # | step | status |
|---|---|---|
| 0 | this section: the survey, the headless route, the steps | **done 2026-09-15** |
| 1 | **the identity page's WALK**: the builder's flag and selection, the tab hook's swap, the panel hook. Check: LEFT/RIGHT on the tab row flips which content is drawn | **done 2026-09-15** — the builder (tab 0, Characteristics off), the row's hook (the LR mover, then the swap by selection) and the panel hook as read: on the row, LEFT off tab 0 / RIGHT off tab 1 go to the list mover, anything else falls to the row; off it, arriving with a LEFT lands on tab 1, with a RIGHT on tab 0. Nothing is DRAWN in either content yet (steps 2-4), so this is invisible in play until then. `verify.py: engine: sneak identity`, red under both mutations with its own output (`608b624`): the row's swap dropped (Characteristics never comes on), and the arrival tab reversed (a RIGHT into the row lands on tab 1 and the next RIGHT leaves) |
| 2 | **Identité**: `0x0049C2B0` - read it whole, the string property slots in the port, the ten lines laid out through `Text_LayOutBlock`. Check: the lines for Kay'l from `save-appart.bin` | **done 2026-09-15** — the hook transcribed call by call into the composer (`ScreenComposer::PlayerSheet`): ten white labels, the values in the identity icon's blue, the pen moved by individually scaled literals, the two prose lines as ONE block built with the listing's two formats `%s : {I%03d%03d%03d} %s` / `%s\n{I255255255}%s : {I%03d%03d%03d} %s`; the box's height is `I2D_ScaleX(h)`, X not Y, as the listing has it. The viewer fills the sheet from the DB's character record at `+60`. **The bio pointers**: the save stores the original process's absolute addresses (`0x37240D0` / `0x37241D0`), which `State_Apply` replaces with the image's 336 / 592; the viewer's copy is not relocated, so it reads those two blocks. Kay'l from `save-appart.bin`: KAY'L 669, 30, M, K-, 178, 80, Vert, Agent-Enquêteur, and both prose lines. `verify.py: engine: sneak identity sheet`, red under both mutations with its own output (`ab47b5f`): the two bio blocks swapped (the prose lines trade places), the name read from `+40` (the job twice). **Not covered by the check**: the composer's LAYOUT - the pen offsets, the colours, the markup - which is seen in a render (labels white, values blue, the two prose lines wrapping inside the box) and not asserted |
| 3 | **Caractéristiques**: `0x0049CA30` and `sub_49CE60`'s bars, the rank word. Check: the seven values and their bar lengths | **done 2026-09-15** — `0x0049CA30` and `sub_49CE60` transcribed into the composer: seven labels 30 apart in the item's own text colour (nothing rewrites it here, unlike 0x0049C2B0), and at half the box's width either a BAR - the value "%d" in face 'J' in a 100-wide box 5 in, four `I2D_DrawLine`s (flags 0x14, layer 6) in `PackColour(100, 50, 50, 50)` outlining 200 x (4 + 2), a 5-wide knob quad (flags 0xC) and the fill quad (flags 4) as wide as the value, both in the item's colour - or, for Maitrise du combat, the RANK WORD `value / 41` -> strings 36..40 (*Novice*, *Initie*, *Disciple Taar*, *Maitre de la Voix Interieure*, *Grand Maitre Taar*; none past 204). The software back end's rules apply (`surface.h`): a line's colour is point 0's third dword, opaque; a quad is its bounding box in vertex 0's colour, flags 4 and 0xC both mode 1. Drawn in submission order - the head cache's within-layer order is not modelled, as nowhere else in the composer. Kay'l: Energie 10, Attaque 70, Maitrise 50 (*Initie*), Resistance 30, Vitesse 70, Esquive 60, Mana 10. `verify.py: engine: sneak characteristics`, red under both mutations with its own output (`aa7874d`): Maitrise read from `+168` (0, and the rank drops to 36 *Novice*), Mana from `+158` (70). **Not covered by the check**: the bars' GEOMETRY and colours, seen in a render (the value over a 250-pixel grey outline at 800x600, the fill as long as the value, *Initie* in place of a bar) and not asserted |
| 4 | **the character view** `0x004779C0` / `sub_4778E0`: the player's model and clip in the item's rect. Check: a 3D view is submitted with the player's model; a render to look at | **done 2026-09-16** — `sub_4778E0` read whole: the player's model (`sub_41E200` on the player node's `+0x30`), the LITERAL bank `ANIMS\F1AVNT.CTL` whatever the player (the open pushes `0x004DF5EC`), `Cef_DefaultGroup` (first group flagged 1) and `Cef_DefaultClip` (its flag-0x20 entry's clip, NO goto followed - unlike the viewer's `charBankFor`), `Anim_BindToHierarchy` and `Anim_SetFrame(node, clip, 0.0, 1.0)`: a STILL pose at frame 1 (key 2), which nothing advances. `0x004779C0`: `sub_478DE0` at 118.11, oscillator 4's turn, `I2D_Submit3DView` into the item's rect at `layer - 1`, so the page text lands OVER him (the 360-wide box overlaps the text column). Ported as `UiModels::setCharacter`/`drawCharacter` and a layer-5 pass in the composer; the viewer poses the player once per model. **The binding is by BONE**: `Anim_BindNodeTrack` compares the track's `+0` with the node record's `+12`, which the files do not hold as equal integers (the track starts with its NAME, `ShBassin`, the mesh's `+12` is its slot) - the loader's conversion is untraced; what `docs/ASSETS.md` established is that every consumer finds a bone by `strstr`, and the port's rule for it (the bone begins at the second uppercase letter) binds F1AVNT's `Sh` tracks to `HO1_FN`'s `U` meshes 19 of 19. The first render, with an equality match, was a T-pose. Not applied: `Anim_RootDelta`'s 0 -> 1 step, which moves the node and not the camera's target. Kay'l in `H_STAND`, standing. `verify.py: engine: sneak character`, red under both mutations with its own output (`1bc95b4`): the bone fallback off (0 tracks bound), and the player's own `H1AVNT` in place of the literal bank (53 frames, first track `UAvantd`). **Its first version PASSED that second mutation** (`b7e3179`): it printed the bank as a literal string, and H1AVNT also opens on entry 0 `H_STAND`, clip 0, binding 19 by exact name - so the line now names the bank from its variable and carries the clip's length and first track. **Not covered by the check**: the picture - the pose, the framing, the layer under the text - seen in a render and not asserted |
| 5 | **Options**: screen 35 shown under the page and focused by the hook. Check: the options page tree reachable from the sneak's Options tab | **BLOCKED 2026-09-16 - the port has no options SCREEN to host.** Read: the page's builder `0x0049D8F0` shows screen 35 (`UI_SetScreenFlag(UI_FindScreen(35), 0x40000001, 0)`), its leave hook `0x0049D940` hides it again, and the panel hook `0x0049D960` calls `UI_FocusScreen(35)` on the input's `0x3` bits - which clears every other resident screen's focus bit, records the previous one in the target's `+0x2C`, and focuses 35; `Ui_CloseScreenDefault` later hands focus back through that `+0x2C`. But the port only MODELS screen 35 (`OptionsWalk`, the page tree walked against `tools/sim`): the composer has no options page drawing and `omk-play ... 35` boots into screen 29. Hosting the options menu under the tab therefore needs the options screen itself ported first - next-tasks 9, which the reader scoped as low priority on 2026-09-13. Waiting for the reader's call; the survey's line "the port already walks screen 35's page tree" was about the MODEL and must not be read as a drawn screen |
| 6 | **Quit**: the icon's callback, Oui/Non, the quit request. Check: *Non* returns to the tabs, *Oui* raises the request | **done 2026-09-16 - and the PAGE IS UNREACHABLE.** The icon `0x004DE118` carries a callback AND a `+44` child, and `Ui_ConfirmSelection` prefers the callback, so `0x004DF0C0` is never installed; nothing else installs it either (the value appears once in the image, as that item's `+44`, and the only two `sub_42A370` sites reading an item's `+44` are the load panel's `0x0047B83B` / `0x0047BB24`). Its Oui/Non list `0x004DEBA0` is that page's alone and `Ui_DrawScreen` draws the current panel's lists, so the callback shows a list nothing draws - options page 12's shape. Ported as read: `0x0049DBF0` shows the list, selects *Non* (`word_4DEBA2 = 1`) and writes `1` into the CURRENT panel's `+24` (on the inventory page, its previews); `0x0049DBC0` hides it and puts `+24` back to 0; `0x0049DBA0` sets the quit request `dword_4E6C9C` (the pause's, already served by the viewer between pumps) and closes the screen. Before this the walk answered "unmodelled item callback" and went approximate. `verify.py: engine: sneak quit`, red under both mutations with its own output (`a3aa792`): the current list not written (the confirm leaves it at 0), and *Oui* selected in place of *Non* |
| 7 | **PLAY** - a reader's pass; `todo/play-test.md` gets the recipe | **steps 1-4 CONFIRMED IN PLAY 2026-09-16** - the reader: *"the identity page is good"*. Open in the same pass: the QUIT tab (they did not say whether they pressed it), and *"there is nothing on info and option pages"* - Options is step 5, blocked; the MEMORY page is empty BY THE CODE (§2c) and the reader is being asked whether the original's shows anything |

## 3b. Notes worth not rediscovering

* **`traces/save-appart.bin`'s three object lists read EMPTY through
  `GameState::fromFile`**, and that is not a bug in either. The DB image and
  the save FILE are different formats read by different paths — `omk-play`
  loads the save through its own reader, which is why it can print the slot
  name and the clock. Anything wanting the save's inventory must go that way.
* **`--give` takes a LIST** (`--give 18,7,26,20`), because a new game ships
  exactly two objects and neither row scrolling nor `Utiliser sur` can be
  driven with two. A harness write, not `inventory.add`.

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

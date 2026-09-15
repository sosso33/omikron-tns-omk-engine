# MULTIPLAN — screen 2, the storage kiosk

Opened 2026-09-15 from `todo/next-tasks.md` 12, after the shops (`todo/shops.md`)
closed. MULTIPLAN is the most-opened screen in the game - **82 `ui.open 2`
sites** - and until now nothing in `engine/` handled it. This file is the plan
and the status; findings go into `docs/UI.md` as each step lands.

**It is not a map: it is a LOCKER.** The kiosk moves objects between the
player's sneak (object list 0) and the kiosk's own storage (list 1) - which is
why `docs/GAME_STATE.md`'s list 1 holds "Notice Multiplan". **Confirmed by the
reader, 2026-09-15**: *"it allows the player to transfer objects to some
virtual 'cloud' and get them back from any terminal in the game"* - one
storage shared by every kiosk, which is what a single DB list 1 opened by all
82 sites means. Its strings,
`IAM\Multip`:

| id | string |
|---|---|
| 0 | Transférer vers le sneak |
| 1 | Transférer vers le multiplan |
| 2 | Examiner cet objet |
| 3 | Détruire définitivement cet objet |
| 4 | Impossible de détruire cet objet |
| 5 | Inventaire vide ! ! |
| 6 | Transfert impossible : sneak plein ! ! |
| 7 | Transfert non autorisé ! ! |
| 8 | Objet Transféré ! |
| 9 / 10 | Oui / Non |

## 0. Where it is opened, and what the port shows today

All 82 sites are a trigger zone's ACTIVATE slot (`+4`), every one passing
parameter **1**; 69 discard the answer and 13 store it in variable 19. The
zones are named `Multiplan` in `ZONES.TAG`, one per building almost
everywhere (AREA 178 and 179 have five each), and `docs/SCRIPT_VM.md` records
that the player walks to an ADDRESS called `Multiplan N` before 73 of them.

The pharmacy has one - AREA 39, record 4, zone 884 - and it is the headless
route in:

```
SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables \
    --save ../traces/save-appart.bin --area 39 --stand 14591,-251,11771,0 \
    --nofmv --no-crowd --software --keys 28,28 --keydelay 60 --frames 160
```

-> `screen 2 is asking` at frame 93 (`--keys` presses its first key at frame 0,
before the zone arms, so the second press is the one that lands).

**Drawn today**: `Multipla.bmp`'s tile map, the four button icons down the
right, nine rows and a header box in the (255, 0, 0) placeholder, nothing in
them. Nothing responds.

## 1. The widget tree (lifted, `tables/ui_widgets.json`)

Screen record: open `0x004B01F0`, input `0x0042A0F0` (generic), close
`0x004B02D0`, draw `0x00475A50`; bitmap `Multipla.bmp`, text `Multip`; panel
bank B 0 (tile background, no world, no dim).

Panel `0x004E5930`, flags `0x20000030` (TAB and BACK close), input hook
`0x004B0B00`:

* list `0x004E53A0`, hook **`0x004B09A0`** - the four BUTTONS, sprite items:
  `0x004E5270` string 1 "vers le multiplan" (554,140) cb `0x004B0760`;
  `0x004E52B8` string 0 "vers le sneak" (554,210) cb `0x004B07E0`;
  `0x004E5300` string 2 Examiner (563,286) cb `0x004B0860`;
  `0x004E5348` string 3 Détruire (563,366) cb `0x004B0890`
* list `0x004E5670`, hook `0x0042AFF0` (the row window) - NINE rows 400x20
  from (90,140), text hook `0x0042AA00` (case 33), row callback `0x004B05C0`
* list `0x004E5910` - one header item (60,60) 400x20, text hook `0x004B0B60`
* list `0x004E58A0` - one 450x260 box (60,130), layer 7, draw hook
  `0x00477ED0`, bank A `0x20000004`

## 2. What is read (exact disassembly - none of these has a `proc` label)

**Open `0x004B01F0`**: `panel+24 = 0`, both lists' selections 0, row 0's tag
-1; the rows and the header coloured from the SELECTED BUTTON (`sub_4296D0`,
the shops' trick); `dword_68A610 = 0` - **the source list** - rows flags
`0x20008000` off and `0x20010000` on; `sub_42ADD0(rows, 0, dword_68A610)`,
which raises event 25 on list 0; broadcast `0x40300000` off; the generic open.

**Close `0x004B02D0`**: event 26, then the generic close (the shops' shape).

**The button list's hook `0x004B09A0`**: `sub_42A910` moves the selection;
on a move it re-colours rows and header from the new button and chooses the
source - "vers le multiplan" (`0x004E5270`) sets `dword_68A610 = 0` (flags
0/1), "Détruire" (`0x004E5348`) sets 1 (flags 1/0), the other two set 1
(flags 0/0) - and re-binds the rows on it. **So the rows list what the chosen
button acts ON**: the sneak before a deposit, the kiosk for everything else.

**The buttons' callbacks**: "vers le multiplan" and "vers le sneak" set the
source themselves (0 and 1) and re-bind; all four hand the focus to the rows
(`panel+24 = 1`) when `dword_4E5688` - the rows' bound count - is positive.

**The row callback `0x004B05C0`**, a jump table on the button selection:

| button | the arm |
|---|---|
| 0 vers le multiplan | event 36 request **7** (list 0 -> list 1); 1 -> re-bind, clear `panel+24` when empty, message **8**; else message **7** |
| 1 vers le sneak | event 36 request **8** (list 1 -> list 0); 1 -> the same; else message **6** |
| 2 Examiner | `dword_4E581C = tag`; `sub_42A370(screen, 0x004E5998)`; `sub_42B420(tag, 4)` |
| 3 Détruire | `sub_42A370(screen, 0x004E5A00)` - a confirm |

The destroy confirm's `Oui` (in `0x004B08D0`..`0x004B097F`) runs event 36
request **6** and posts message **4** on refusal; both answers reinstall the
panel. Messages go through `sub_42B820`, oscillator 0, as in the shops.

**Not yet read**: the panel hook `0x004B0B00` (tests the input's `0x3` pair
and the list it is on), the header text hook `0x004B0B60`, the box's
primitive `sub_4286F0` (not in `i2d.h`'s list), `sub_42B420`, the two child
panels `0x004E5998` / `0x004E5A00` (neither is lifted - they need
`CODE_NAMED`), and whether the port's inventory channel carries case 36's
requests 6/7/8.

## 3. Steps

Each ends in a commit, a `verify.py` check shown to fail first, and a report.

| # | step | status |
|---|---|---|
| 0 | this file, the survey and the headless route | **done 2026-09-15** |
| 1 | **the OPEN and the SOURCE**: the colour push, `dword_68A610`, the rows' flags; the button list's hook re-colouring and switching the source; the four button callbacks; lift the two child panels. Check: opening shows the sneak's objects in the button's colour, and moving to "vers le sneak" switches to list 1 | **done 2026-09-15** — the walk carries the source (`UiListState::multiplanSource`), the open's reset and colour push, the button list's hook (move, repaint, source 0 on "vers le multiplan" / 1 on the rest, rows rebound at 0), the panel hook's LEFT/RIGHT between buttons and rows, and the four button callbacks; the two children are `CODE_NAMED` (57 panels, nine self-check counts moved, no existing record) and come up on their record's list 1. The button colours: yellow (255,240,0) for a deposit, (255,100,70) "vers le sneak", blue Examiner, green Détruire. The rows' list flags `0x20008000`/`0x20010000` the hook toggles are **recorded, not modelled** - their drawing effect is unread. Rows are still EMPTY: filling them is step 2. `verify.py: engine: multiplan open`, red under both mutations with its own output (`e9bedfd`): the source never set back to the sneak (the wrap to "vers le multiplan" reads source 1), no panel hook (LEFT stays on the buttons) |
| 2 | **the ROWS and the HEADER**: case 33 names over the window from the chosen list; the header hook `0x004B0B60`; the panel hook `0x004B0B00`; the box `0x00477ED0` / `sub_4286F0`. Check: both lists' names, the header text | **done 2026-09-15** — the viewer binds the rows to the walk's source (event 25 on a switch, `objectList` Carried or Second, case-33 names through the window, the rest hidden) and fills the header with the selected button's label (the posted-message arm comes with the messages in step 3). The box is **the interference** `sub_432940`, reached through `sub_4286F0` - a primitive this repo had recorded as having NO callers; both of the 24-byte pool's submitters are called from table-dword draw hooks (`0x00477E00` the save thumbnail, `0x00477ED0` this box on the sneak, the terminal pages and MULTIPLAN), so `i2d.cpp`, `i2d.h`, `docs/UI.md` and `engine: I2D` were corrected (unreferenced 1 -> 0). Ported as `ui/interference.{h,cpp}`: rule exact, `rand` stream a reconstruction. `engine: I2D`'s flag census was also stale since `e9bedfd` regenerated the table (117 -> 133 flags, 57 -> 69 words) and is re-baselined. `verify.py: engine: multiplan rows` (source 0 -> 171; DOWN -> source 1 -> 176 163), `engine: interference` (3000 black frames: nothing outside the box, band only on its eight rows, every other lit pixel a scatter grey). Mutations, each red with its own output (`bbcb2ee`): the band rows three apart (7038 writes outside the box, 579078 stray pixels); source 0 reading the storage list (the open's rows 176 163 instead of 171); `rect24` flagged unreferenced again (`engine: I2D` 0 -> 1) |
| 3 | **the TRANSFERS**: case 36 requests 7 and 8 in the channel (read them whole first), messages 6/7/8, the re-bind and the focus rule. Check: an object crosses each way and the refusals post their message | **done 2026-09-15** — the row callback `0x004B05C0` is recorded by the walk (`takeMultiplan`: button 0 -> request 7, 1 -> 8, a row with no tag refuses) and carried out by the viewer as case 36 does: 7 refuses kind 1 and a full storage, else front-inserts into list 1 and removes from 0; 8 refuses a full sneak, else runs the session's `Inventory_Insert` ladder (`insertArm`: kinds 12/13 apply their effect and take no row, 2..11 merge into a related row) and removes from list 1 whatever it answered. Message 8 / 7 / 6 through the header for 5000 ms; an emptied list hands the focus to the buttons. **The header hook re-read whole**: on the Examiner page or with the rows focused it fetches the selected row's name and never prints it (`"%s"` at `0x004E5AF8`, the label its only argument). **A latent viewer bug found**: `Session::dataRoot_` came only from `loadTraffic`, which `--no-crowd` skips, so every object kind read -1 and a withdrawn ring took a sneak row - `setDataRoot` now runs unconditionally. Not modelled: `Inventory_Insert`'s gun arm that also swallows loose ammunition of kind + 5 (the session's ladder answers Row there, as it did before). `verify.py: engine: multiplan transfers` - deposit 171 + withdraw 163 (rings 2 -> 7, no row); a Waver refused (message 7); an 18-slot sneak refused (message 6) |
| 4 | **EXAMINER and DÉTRUIRE**: the examine page `0x004E5998` (`sub_42B420`, and what it draws), the destroy confirm `0x004E5A00` (request 6, message 4). Check: the page shows the object, a destroyable object goes, a protected one refuses | |
| 5 | **the CLOSE and the answer**: event 26, variable 19 at the 13 sites that keep it | |
| 6 | **PLAY** - a reader's pass; `todo/play-test.md` gets the recipe | |

## 4. Cautions (carried from the shops)

* **Read the original before shaping anything**, at the exact address: these
  are table dwords and `asmfn.py` snaps to a neighbour (it did, twice, in step
  0 - `0x004B01F0` came back as a row arm and `0x00477ED0` as `sub_477F60`).
* **A field can be read where no address is**: the shops' Analyser page drew
  through the box item's own `+0x3C`, which no instruction names.
* **Children are lifted once**; `UiWidgets::at` merges them with their
  screen's shared lists, which MULTIPLAN's two children will need.
* **A reader's memory of the original outranks a reading** - ask what the
  kiosk looks like before calling any of it finished.

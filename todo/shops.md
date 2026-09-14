# The shops — screens 20..28 and 32

Opened 2026-09-14 from `todo/next-tasks.md` 11, after a survey of what the
world scripts actually open: **the ten shop screens are 83 of the 242
`ui.open` sites** (LIB. LAHOREY 40, the nine others 43), second only to
MULTIPLAN's 82. One open, one input callback, one panel and one row callback
serve all ten; they differ only in the screen table's fixed parameter `+8`
(0..9). This file is the plan and the status; the findings go into
`docs/UI.md` as each step lands.

## 0. How a shop is reached, and what the port shows today

Every site is a trigger zone's **ACTIVATE** slot (`+4`), and every script has
the same shape: `dialog.start` the seller, a branch on the reply, `ui.open
<shop>`, `dialog.start` the goodbye. So a shop is *a conversation, then the
screen, then a second conversation* — and which reply is chosen decides
whether the screen opens at all.

The pharmacy in AREA 39 (`SPHARMA1`, zone 881) is the headless route —
address 146 stands inside the zone and ENTER is the action:

```
SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables \
    --save ../traces/save-appart.bin --area 39 --nofmv --no-crowd --software \
    --keys 28,28,28,28 --keydelay 60 --frames 260 --dump shop.bin
```

-> the conversation from frame 61 to 181, `screen 21 is asking` at 182.
**Trap**: with three presses instead of four the run is still on the reply
menu (*Je voudrais acheter quelque chose* / *Je jette juste un œil*) at frame
330, and a fifth ENTER inside the shop confirms and closes it (-1). Time the
presses against the log, not by count.

**What is drawn at frame 260, 2026-09-14:**

* the title, *Pharmacie - achat*, right — the title switch is ported;
* the nine stock rows and the two header rows as EMPTY boxes in the
  placeholder (255, 0, 0) fill — `Ui_OpenShop`'s colour pushes are not
  ported, and nothing fills the rows;
* no money line, no price line;
* no artwork. The panel's bank-B word is `0x40003800`, whose `0x2000` arm
  draws no background, so this part may be RIGHT — confirm against a capture
  or a reader before touching it;
* a blurred cyan blob at the top right, where the three 63x63 sprite items
  (buy, sell, preview) sit at (554,140), (554,140), (563,286). Unexplained;
* the world hidden behind the screen. The screen's flag banks are `[0, 0]`,
  which by `docs/UI.md` "Which screens stop the world" means hidden — again
  probably right, to confirm.

No stock, no buy, no sell: the screen is a title over empty boxes.

## 1. What is already in the tree

* `tables/ui.json`: the ten screens, callbacks open `0x004AE540`, tick
  `0x0042A0F0` (generic), input `0x004AE7E0`, close `0x00475A50` (generic).
* `tables/ui_widgets.json`: the panel `0x004E3A70` (hook `0x004AEE00`) and its
  three lists —
  * `0x004E3370` the four BUTTONS: string 0 (callback `0x004AED00`), string 1
    (`0x004AED30`), string 2 at (563,286) (`0x004AED60`), string 3 the back
    button (`0x0042A990`, generic);
  * `0x004E3640` the NINE STOCK ROWS, 400x20 from (90,140) at 30 px, font 74,
    text hook `0x0042AA00`, row callback `0x004AEAA0`, list hook `0x00429AF0`;
  * `0x004E38E0` the HEADER: (90,30) text hook `0x004768E0`, (90,60)
    `0x004AEE30`, (30,440) string 4 `0x004AEF10`, (340,440) string 5
    `0x004AEF60` — font 83 above, 74 below.
* `docs/UI.md` "The shops name themselves": the title switch and the
  BANK-sells / nine-buy flag arm. `verify.py: ui shop titles`.
* `docs/UI.md` 3b's inventory channel and `engine/src/script/inventory.*`:
  cases 33 name, 34 price, 38 buy, 39 sell at half, 41 the gun refusal —
  ported as functions, NOT wired to any screen. List 3 (a shop's stock,
  `ObjectList_Load`ed per area by case 25) is NOT ported.

## 2. What is read in this session (to go into `docs/UI.md` with step 1)

`Ui_OpenShop` (`0x004AE540`), past the title switch and the flag arms:

    dword_4E3988 = 0;  word_4E3642 = 0;  screen+28 = &unk_4E3970
    word_4E3372 = (param == 0)           // 1 = SELL (the bank), 0 = BUY
    sub_4290D0(rows, 0x2000C000, bank)   // the arm's list flags, mirrored
    sub_4290D0(rows, 0x20020000, !bank)
    sub_42ADD0(rows, 0, bank ? 0 : -1)   // bind the rows
    colour = off_4E337C[word_4E3372] +8/+9/+10    // the LIVE button's colour
    sub_4296D0(rows,   colour)           // the placeholder fill, replaced
    sub_4296D0(header, colour)
    sub_429140(rows, 0x40000010, 1); (0x40000200, 1); (0x40000008, 1);
    sub_429140(rows, 0x40300000, 0)
    sub_42A050(screen)                   // the generic open

The two money arms, from the raw image (no `proc` labels):

* `0x004AE8A0` **BUY**: refuse with sound 18 if `sub_42B3E0(obj)` fails;
  raise **event 38**; on result 2 pick message 9 or 8 by comparing
  `sub_42B300(obj)` against `sub_42B1C0(4)` and post it through
  `sub_4767E0` / `sub_42B820`, sound 18; otherwise re-bind the rows
  (`sub_42ADD0(rows, -1, -1)`), post message 21, sound 16.
* `0x004AE990` **SELL**: `sub_42B360(obj)` and `sub_42B300(obj) > 0` gate it;
  raise **event 39**; on 1 re-bind the rows, clear `dword_4E3988` when
  `dword_4E3658` is 0, sound 17; otherwise message 7.
* `0x004AE7E0` (the INPUT slot) raises **event 26** — close the open list —
  then `sub_42A150`. Which of input/close it really is must be settled
  against `UI_TickScreens` before it is ported.
* `0x004AE800` is a SECOND copy of the title switch, bare. Who calls it is
  not yet known.

## 3. Steps

Each ends in a commit, a `verify.py` check shown to fail first, and a report.

| # | step | status |
|---|---|---|
| 0 | this file, the survey and the headless route | **done 2026-09-14** |
| 1 | **the OPEN**: the rest of `Ui_OpenShop` — the buy/sell word, the rows' flags, the colour pushes off the live button, the broadcasts, the generic open. Diagnose the cyan blob on the way. Check: the composed shop's row fill is the button colour, not (255,0,0), and the bank's differs from the pharmacy's | |
| 2 | **the STOCK**: event 25 on list 3 — find which file `ObjectList_Load` reads per area and port it — then the row text hook `0x0042AA00` (case 33, the name) over the nine rows, with the window the sneak's rows already use. Check: the pharmacy lists its real stock, by name | |
| 3 | **the HEADER and the PREVIEW**: the four header text hooks (title, the selected item, money string 4, price string 5) and the preview button's 3D model (case 30, the sneak's preview path). Check: money and price read out of the save and the object record | |
| 4 | **BUY and SELL**: the row callback `0x004AEAA0`'s dispatch on `word_4E3372`, the two arms above, their messages and sounds, the money write, the row re-bind; the two arm buttons `0x004AED00`/`0x004AED30`. Check: a buy moves the price out of `+172` and the object into list 0; a sell at the bank credits half | |
| 5 | **the CLOSE and the answer**: event 26, the input/close slot settled, the goodbye conversation resuming; RESTAURANT (23) has no answer writer and must answer -1. Check: the zone script reaches its second `dialog.start` | |
| 6 | **PLAY**: the pharmacy (buy), a bank (AREA 83/223/257, sell), the Lahoreh library (AREA 86/114, param 9, *Emprunter* — does case 38's list-2 flat charge apply?). A reader's pass; `todo/play-test.md` gets the recipe | |

## 4. Cautions

* **Read the original before shaping any of it** (the standing rule): every
  callback here is a table dword with no `proc` label, so `asmfn.py` snaps to
  a neighbour. Disassemble the image at the address (`objdump -d
  --start-address=...` on `omkpaths.exe_path()`), as §2 was.
* **The static records** (`todo/sneak.md` §0): `list+2`, the colour bytes and
  the flag words outlive the screen. A shop that remembers its last row is
  correct.
* **One panel, ten screens**: anything a step writes into the panel is shared,
  so a check must open at least two shops with different parameters — the
  pharmacy and the bank at minimum — or it cannot tell a per-screen value from
  a leftover one.

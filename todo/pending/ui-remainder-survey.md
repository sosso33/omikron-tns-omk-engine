# ui-remainder-survey — the hooks the widget tree names that the port does not

Integrated: no

A **triage**, not a port. Every address below was read at the address
`tables/ui_widgets.json` names, out of `Runtime.exe.asm`, and every panel/item
attribution is from the table's own records. Nothing in `engine/` or `tools/`
was edited.

Scope: the addresses the brief supplied — the ones named in `ui_widgets.json`
that appear in neither `engine/src/ui/widgets.{h,cpp}`, `screendraw.{h,cpp}`
nor `engine/backends/sdl/play.cpp`. Screen 34 (SHOOT HUMAN) and screen 35
(OPTIONS) are owned elsewhere and are one line each in §4.

**A note on `asmfn.py`, because three of these needed it.** The tool anchors on
`loc_`/`proc` labels and snaps FORWARD, so `asmfn.py 47BC10` silently prints
the function at `0x0047BC30` and `asmfn.py 49D960` prints a block whose first
line happens to be the right one. Where a target sits between two `align 10h`
directives with no label, the block was located by counting instruction
lengths back from the nearest `loc_` and checking the 16-byte alignment. The
three so pinned are **0x0047BC10**, **0x0047BE50** and **0x0049BC30/40**; each
is noted where it matters.

---

## 1. The table

`reached from` follows the panel's `parent` and the item `child` that installs
it. `screen -1` in the lift means "a child panel no screen names directly".

| address | panel / item | reached from | what it plainly is | port already does it | player can reach it |
|---|---|---|---|---|---|
| **0x0042A710** | panel hook of `0x004DEE50` (sneak *Inventaire*) | **9 SNEAK** (screen 9's own panel; the same family serves 0 and 7) | `sub_42A5C0(screen, panel, 1, 2)` — `Ui_MoveBetweenLists`, LEFT back / RIGHT on. This *is* the table's `moveListsHook` | **YES**, by name — `widgets.cpp:2498`, the `w_->moveListsHook()` arm | yes, constantly |
| **0x0049D960** | panel hook of `0x004DF058` (sneak *Options*, tab string 4) | **9 SNEAK**, tab column item `0x004DE0D0` | `if (screen+0x6C & 3) { UI_FocusScreen(35); return 1 }` — LEFT/RIGHT hands focus to screen 35 | no — and **already read and owned** by `todo/sneak.md` §5 step 5, marked BLOCKED | yes, but the port has no options SCREEN to focus |
| **0x0047A230** | panel hook of `0x004CF280` (*Nouvelle partie*) | **29 START MENU**, button string 0 | moves focus between the name list `0x004CE890` and the button list `0x004CE948`: DOWN off the field, UP off `Confirmer` | **YES**, by name — `startConfirmHook`, `widgets.cpp:2364` | yes |
| **0x0047A390** | list hook of `0x004CE890` (the name field) | **29** | the 20-character name field: 8 backspace, 13 return, else insert while len < 0x14 | **YES**, by name — `nameHook` + `nameSwitch`/`nameMax` | yes |
| **0x0047A2B0** | item `0x004CE8B0` `Confirmer` | **29** | empty name ⇒ do nothing; else read the save directory and, **if that name already has saves, install `0x004CF3B8` (`Ecraser ce fichier ?`)**; otherwise `screen+8 = 3` and `dword_930750 = 1` — the new game | **PARTLY**: it is the table's one `answers` row (value 1, `needsName`) and the empty-field gate is modelled; the **already-exists ⇒ overwrite confirm** arm is not | yes |
| **0x0047BC10** | item `0x004CEE78` `Oui` on `0x004CF488` (the *Quitter* confirm) | **29**, button string 5 | `screen+8 = 3; PostQuitMessage(0)` — six instructions. **The only real application exit in the whole widget tree** | **NO** | yes: start menu → *Quitter* → *Oui* |
| **0x004AE220** | the five hint rows of `0x004E3018` (*Indices*) | **30 SAVE GAME**, button string 1 | rings (`Actor_GetProperty(5)`) vs `dword_6A17C0`, the price; too poor ⇒ rewrite the footer item's string id to 8, "Je n'ai pas assez d'Anneaux pour faire ça !"; rich enough ⇒ install `0x004E3080`, the purchase confirm | **NO** | yes — any save point |
| **0x004AE260** | item `0x004E2B10`, the 440×120 body box (shared by `0x004E3018` and `0x004E3080`) | **30** | install `0x004E3018` and `sub_42ADD0(list 0x004E2AF0, 0, 2)` — back to the row list, re-opening object list 2 | **NO** | yes |
| **0x004AE290** | textFn of item `0x004E2CF0`, the hints footer at 100,400 | **30** | rows present ⇒ `{C}` + string 6 + the ring count; none ⇒ string 5, "Aucun indice disponible" | **NO** — one of the "other thirteen [textFns that] are native and draw nothing" (`screendraw.cpp:1253`) | yes |
| **0x0049BC30** | item `0x004DE380`, the ANNEAUX tile | **9 SNEAK**, list 1 | `mov eax, 1; retn`. Six bytes. It consumes the confirm and does nothing | nothing to port | yes — and it does nothing in the original either |
| **0x0049BC40** | item `0x004DE3C8`, the `Lire plan` tile | **9 SNEAK**, list 1 | `sub_42A370(screen, 0x004DF190)` — install the **MAP** panel | **NO** | yes |
| **0x0047BC30** | textFn of item `0x004CEE08`, shared by `0x004CF350` (*Détruire*) and `0x004CF3B8` (*Ecraser ce fichier ?*) | **29** and **30** | the confirm's SUBJECT line: on screen 29 under `0x004CF3B8` it quotes the typed name `byte_69BDA0`; otherwise it resolves `dword_4CEBAC` (the selected row) through `sub_47BCB0` and quotes that slot. Format `" %s "` | **NO** — so both confirms come up in the port with the question and no subject | yes |
| **0x0047BE50** | textFn of item `0x004CF138`, the start menu's 640×40 footer at y=440 | **29** | `buf[0] = byte_65799C; return 1` — one byte out of a BSS global | **NO** | drawn, but see §3 — there is nothing to draw |
| **0x0047B470** | drawFn of item `0x004CEB00`, the 70×300 strip at 390,140 on the load panel | **29** and **30** | the **CONNECTOR**: three 4-point quads (`sub_4285E0`) from the highlighted save row across to the 128×96 thumbnail, its Y solved from the 9-slot row window | **APPROXIMATELY**, under another construction — `screendraw.cpp:455` draws **one** bar, reconstructed off `traces/frames/loadpanel-*.png`, inside the `kDrawLoadRows` branch. The hook itself is not transcribed | yes |

---

## 2. The two real features behind these addresses

### 2a. `Indices` — the HINT SHOP on the save screen (0x004AE220 / 260 / 290)

Screen 30, `SAVE GAME`, is not one page. Its panel `0x004E2ED8` carries three
buttons out of `IAM\Save`:

| item | string | goes to |
|---|---|---|
| `0x004E2868` | 0 `Sauvegarde` | child `0x004CF2E8` — the slot panel, which the port has |
| `0x004E28B0` | 1 **`Indices`** | child `0x004E3018` — **not modelled at all** |
| `0x004E28F8` | 3 `Annuler` | `0x0042A990` |

`0x004E3018` is a shop for **clues, bought with Anneaux**, and every string it
needs is already in `IAM\Save`: 1 `Indices`, 2 `Cet indice te coûtera :`,
3 `Annuler`, 4 `Acheter`, 5 `Aucun indice disponible`, 6 `Anneaux en votre
possession :`, 8 `Je n'ai pas assez d'Anneaux pour faire ça !`, 9 `Indice
acheté !`.

Its builder is `sub_4AE120` (the panel record's `+4`), and it reads end to end:

* `dword_6A17C0 = Game_HandleEvent(42)` — the price;
* `word_4E2D0C = 6` — the footer item `0x004E2CF0`'s string id, which is
  exactly the `cmp ax, 6` branch in **0x004AE290**, and matches the item's
  `bind {"string": 6}` in the lift;
* `word_4E2AF0 = 5` then `sub_42ADD0(&word_4E2AF0, 0, 2)` — **five rows, from
  object list 2**;
* `dword_4E2B08` — the row count. It looks unwritten (six reads, no store
  anywhere in the image), and **that is a trap**: `0x004E2B08` is
  `list 0x004E2AF0 + 0x18`, and `sub_42ADD0` ends
  `... sub_4083F0(0x1D, &count); mov [esi+18h], eax`. So the count comes from
  `Game_HandleEvent(29)` after the list is opened, and the hint page is live
  data, not dead code. (I nearly filed it as dead on the grep alone.)
* with no rows: `dword_4E2B4C = -1` and `screen+0x18 = 1`, so the focus skips
  the row list; the footer then prints "Aucun indice disponible".

**The hint rows and the sneak's `Mémoire` rows are the same list.** The sneak
memory panel does `word_4DE6F0 = 5; dword_670CB8 = 2; sub_42ADD0(&word_4DE6F0,
0, 2)`; the hints panel does the identical call on its own list. Object list 2,
five rows, both. The port already fills the memory page's rows through the
inventory channel, so the same machinery serves this page — the missing parts
are the price, the purchase and the footer, i.e. the three addresses above.

`0x004E3080` (the purchase confirm, builder `sub_4AE3A0`) is **not in the lift**
— only a callback installs it — but its record reads cleanly: parent
`0x004E3018`, 5 lists `0x004E2C18 / 0x004E2B60 / 0x004E2C88 / 0x004E2D40 /
0x004E2EB8`, of which `0x004E2B60` and `0x004E2D40` are the hint page's own
body and footer lists re-used at different Y (`word_4E2B12` 250 → 180,
`word_4E2CF2` 400 → 290). Whoever ports this needs to read `0x004E2C18` and
`0x004E2C88` out of the image; they are where `Annuler` / `Acheter` live.

Also already ported and worth knowing: `kPanelSaveNoRings = 0x004E2FB0` is the
*save*'s "not enough Anneaux" panel — a different refusal from **0x004AE220**'s,
which shows string 8 in the footer line rather than installing a panel.

### 2b. `Lire plan` — the CITY MAP in the sneak (0x0049BC40)

The sneak's Inventaire page carries three 50×50 tiles down its left edge
(list `0x004DE420`, already named `kListSneakPreviews` in the port, and
`play.cpp:17914` already reads all three correctly as the setek counter, the
anneau counter and "a map reader, not ammunition"). What the port does not have
is what **confirming the third one does**.

`0x0049BC40` installs panel **`0x004DF190`**, which no screen and no item
`child` names, so it is absent from `ui_widgets.json` entirely. Its record:
parent `0x004DEE50` (back to the Inventaire tab), open `sub_49D9E0`, close
`sub_49DB80`, one list `0x004DED60`.

`sub_49D9E0`:

1. takes `dword_93076C + 0x30` — the current location's file path — keeps the
   basename after the last `\` and drops the last four characters;
2. `sprintf("Images\\%s.bmp")`, tests it with `fopen`/`fclose`, then loads it
   with `sub_428A20` into `dword_4DECB4`;
3. `_strupr`s the name and matches it against a 52-byte-stride table at
   `dword_4DF1F8` (`aAnekbah`, bounded by `0x004DF2CC`) — **four entries:
   ANEKBAH, QALISAR, JAUNPUR, LAHOREH** — storing that record's dword in
   `dword_4DECFC`;
4. **if the bitmap is not there, it immediately re-installs `0x004DEE50`** —
   so in any location without a map the button silently bounces back.

`sub_49DB80` frees the bitmap on close.

All four bitmaps ship: `gamedata/IMAGES/anekbah.bmp`, `qalisar.bmp`,
`jaunpur.bmp`, `lahoreh.bmp`. So this is a complete, data-backed, reachable
feature that the port has none of. What `dword_4DECFC` is for (a marker? a
scale? the player's pin?) is **not** settled here — it would need the list
`0x004DED60`'s draw hook read, which I did not do.

---

## 3. Three smaller things, each with what the evidence does and does not settle

**0x0047BE50 — the start menu's footer prints one uninitialised byte.**
The whole function is `mov eax, [esp+0Ch]; mov cl, byte_65799C; mov [eax], cl;
mov eax, 1; retn` (18 bytes; pinned by counting back from `loc_47BECC` and by
the 16-byte alignment of the next block at `0x0047BE70`). `byte_65799C` has
**two references in the 54 MB listing**: this read and its `db ?` definition.
Nothing writes it. So the item at 0,440 renders a single byte of BSS.
I am not going to call it "dead" outright — a write through a struct base that
IDA did not name would not show in that grep — but there is no visible feature
here and nothing for the port to reproduce. Leave it unported and say why.

**0x0047B470 — the connector is three quads, the port draws one bar.**
The port's version is honest about being a reconstruction from a capture, and
it lands in the right place. The engine's own hook computes the item's screen
x/y *and* the slot list `0x004CEB70`'s, clamps the row into the 9-slot window
(the `>9`, `<4`, `>= count-5` arithmetic the port's `moreAbove`/`moreBelow`
already carries) and emits **three** `sub_4285E0(points, 4, layer)` calls, with
a colour byte chosen by `sub_428F90(item, 3.0f)` → `sub_42B5E0(2) + 0x18`.
So the shape is an elbow/bracket, not a single horizontal bar, and one of its
two colour states is conditional on something this survey did not chase.
**A cosmetic difference on a screen that is otherwise tier-4 against a real
capture** — worth transcribing if anyone touches the load panel, not worth a
task of its own.

**0x0047BC30 — both confirms lose their subject line.**
`0x004CF350` (title string 2, `Détruire`) and `0x004CF3B8` (title string 10,
`Ecraser ce fichier ?`) *share* item `0x004CEE08` at 95,120 450×40, whose only
content is this textFn. The port models both panels and both `Oui` callbacks
(`kCbDestroyYes`, `kCbConfirmYes`) but draws no subject, so a player is asked
"Ecraser ce fichier ?" without being told which. Small, cheap, and it needs
only `dword_4CEBAC` (the selected row, which `LoadPanel::row` already is) plus
the typed-name case for screen 29.

**0x0049BC30 is a genuine no-op, and that is the finding.** Two instructions,
`mov eax, 1; retn`. It sits on the ANNEAUX tile and exists only so that
`Ui_ConfirmSelection` sees a callback and does not descend into the item's
`+44`. Do not "implement" it.

---

## 4. The skipped addresses, one line each

* **Screen 34 (SHOOT HUMAN)** — the drawFns `0x0042EA10`, `0x0042E980`,
  `0x0042E9E0`, `0x0042E870` and the textFns `0x0042EB20`, `0x0042EB60`, all on
  panel `0x004C4618`, are the shoot HUD. Owned by
  `todo/handoff-shoot-mode.md`.
* **Screen 35 (OPTIONS)** — `0x00492AD0` (panel hook), `0x00492DA0` (list
  hook), `0x00492F80`/`0x00493380` (draw). Owned by `todo/sneak.md` §5 /
  `todo/next-tasks.md` item 10, and blocked on the options screen being drawn
  at all.
* **`0x004ADA80`** — screen 36, HIGH-SCORE; already named `kHookHighScorePage`
  in `widgets.h:157` and handled in `widgets.cpp:2492`. It was on the brief's
  "already being done" list and it is in fact already done.
* **`0x0049DBF0` / `0x0049DEE0` / `0x0049DF30` / `0x0049DF80` / `0x0049C090`**
  — skipped as "the VIDEOPHONE". **Two corrections for whoever owns that
  agent's work**, because the attribution looks wrong: (a) `0x0049DBF0` is
  already ported, as `kCbSneakQuitShow` (`widgets.h:215`), and it is the
  *Quitter le jeu* tile on the sneak's tab column, not the videophone;
  (b) `0x0049DEE0`, `0x0049DF30` and `0x0049DF80` are the draw hooks of the
  three 50×50 tiles on the sneak's **Inventaire** panel `0x004DEE50`, and
  `0x0049C090` is the draw hook of that panel's nine object ROWS. Screen 0
  (VIDEOPHONE) shares the sneak's tab-bar item records, which is almost
  certainly how they were picked up — but the panel they hang on is the
  inventory's.

---

## 5. What is actually left to finish the UI

Ranked. "Notice" means a player using the port would see a page that is blank,
a button that does nothing, or a question with no subject.

### (a) Real gaps a player would notice

1. **`Indices`, the hint shop (screen 30).** The largest of these by far, and
   the only *page* that is wholly missing rather than a detail. The port
   already walks into it — `widgets.cpp:2292` descends on `child` — so the
   player reaches an **empty panel** today: five rows with no text, a blank
   body box, a blank footer. Needs `0x004AE220`, `0x004AE260`, `0x004AE290`,
   plus `0x004E3080`'s two unread lists. The row data is object list 2, which
   the port's inventory channel already serves for the sneak's `Mémoire`.
2. **`Lire plan`, the city map (screen 9).** Confirming the third inventory
   tile does nothing in the port; in the engine it opens a whole panel showing
   `IMAGES/<AREA>.bmp`. Four shipped maps, four cities. Needs `0x0049BC40`
   and the panel `0x004DF190`, which is not in `ui_widgets.json` and so has to
   be read out of the image by hand (its record is transcribed in §2b).
3. **`Quitter` on the start menu (screen 29).** `Oui` does nothing. The engine
   calls `PostQuitMessage(0)`. Two instructions to port, and it is the one
   place in the UI that ends the process — note that it is *not* the same as
   the pause/sneak "Quitter le jeu", which the port already has and which
   only sets `dword_4E6C9C` (quit the game, start a new one).
4. **The confirm subject line** (`0x0047BC30`). "Ecraser ce fichier ?" with no
   file named, on both screens.
5. **`Confirmer` with a name that already exists** (`0x0047A2B0`'s second
   arm). The port answers 1 and starts the game; the engine first asks
   `Ecraser ce fichier ?`. This is the same panel as 4, so the two are one
   change.

### (b) Already covered under another name

* `0x0042A710` → `moveListsHook`. `0x0047A230` → `startConfirmHook`.
  `0x0047A390` → `nameHook` + `nameSwitch`/`nameMax`. `0x0047A2B0` → the
  `answers` row (its *main* arm; see (a) 5 for the rest). All four are covered
  because the port consumes the table's symbolic rows rather than the
  addresses, which is why a hex-literal grep of `engine/` reported them
  missing. **The grep that produced the brief's list under-reports coverage
  by exactly this much** — worth recording, since the same grep will be run
  again.
* `0x0047B470` → the connector inside `screendraw.cpp`'s `kDrawLoadRows`
  branch. Present, approximate, labelled as a reconstruction; see §3.
* `0x004ADA80` → `kHookHighScorePage`; `0x0049DBF0` → `kCbSneakQuitShow`.

### (c) Dead, inert, or with nothing to reproduce

* `0x0049BC30` — `mov eax, 1; retn`. Inert by construction.
* `0x0047BE50` — one byte from a global nothing writes. No content.
* Not on the brief's list but found on the way and worth one line: the start
  menu's **`Options`** button (`0x004CF420`) is a panel with **one list
  holding only the title item** — no buttons, no child, no hook. Whatever
  fills it is a callback, and CLAUDE.md §1 already records that the options
  module's root page builder is one of the five functions IDA gave no label.
  If a player selects `Options` on the title screen of the original, the
  evidence in the tree says they get a heading and nothing else; I did not
  chase the builder, so treat that as unfinished rather than settled.

### What I could not settle

* `dword_4DECFC` in the map panel — the per-city dword. Needs list
  `0x004DED60`'s draw hook.
* Whether `dword_4E2B08` is ever non-zero in a shipped playthrough. The
  mechanism is live (`Game_HandleEvent(29)` on object list 2); whether the
  game DB ever puts anything in that list is a question for the state side,
  not the UI side. Until someone answers it, the honest port of the hints page
  shows "Aucun indice disponible" — which is at least the right page.
* `0x0047B470`'s `sub_428F90(item, 3.0f)` colour condition.

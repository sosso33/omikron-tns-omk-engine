Integrated: no

# The SNEAK's Inventaire page draws, and the quit confirm — four native callbacks read out of the raw image

**The filename is wrong and is kept only so links do not break.** This started
as "four videophone callbacks" and none of them is one. Read the corrections
before anything else.

Read 2026-09-18 from `Runtime 2.exe` and `Runtime.exe.asm`. Nothing in
`engine/` or `tools/` was edited. Every address was checked against the
image's own bytes (`exetables.Exe.read`), not only against the listing.

---

## 0. Corrections, up front

1. **Nothing here is the videophone's.** Screen 0's own panel is
   `0x004DF128`, whose items are the 3D viewport `0x004DEA28`
   (drawFn `0x004782B0` = the port's `kDrawViewport`, where Telis renders)
   and `0x004DE1A8` (drawFn `0x00477ED0`). Screen 0 shares the **tab column**
   list `0x004DE210` with screens 7 and 9, and screen 0's `Inventaire` icon
   `0x004DE040` has the SNEAK's inventory page `0x004DEE50` as its `+44`
   child — which is the only reason these addresses showed up under screen 0.
   All four functions below live on the sneak's **Inventaire** page
   (`0x004DEE50`) and its verb child (`0x004DEEB8`).

2. **`asmfn.py` snapped.** `python3 tools/asmfn.py 0x0049DBF0` prints the
   function at **`0x0049DC20`**, not the one asked for — neither has a label.
   The image settles it:
   ```
   0049DBF0: 6a 00 68 01 00 00 40 68 a0 eb 4d 00 e8 ...   push 0 / push 40000001h / push offset 4DEBA0
   0049DC20: 81 ec 20 02 00 00 53 56 57 33 db ...         sub esp,220h / push ebx,esi,edi / xor ebx,ebx
   ```
   `0x0049DBF0` is **eleven instructions**. `0x0049DC20` is the sneak's
   **echo bar**, and it is transcribed here too because it is the piece the
   three tile hooks feed.

3. **`0x0049DBF0` is already ported as `kCbSneakQuitShow`** — §1 gives the
   verdict. Its three arms match; **one surrounding claim in
   `engine/src/ui/widgets.h` is wrong**, and it is not a small one.

4. `sub_42B5E0` / `sub_42B5F0` **are** read in this tree:
   `Ui_Oscillator(n) = 0x004C3EA0 + 40*n`, `Ui_OscillatorFlags(osc, mask) =
   mask & osc->+12`. Nothing below is unported for want of them.

### The oscillator table, needed by three of the four

`0x004C3EA0`, 8 records x 40 bytes: `+0` id, `+4` elapsed ms, `+8` duration,
`+12` flags (**bit 0 = RUNNING**, bit 1 = looping), `+16`/`+20` lo/hi, `+24`
value, `+28` start fn, `+32` tick fn, `+36` expiry fn.

* **osc 0** — duration 5000, start fn `sub_42B660`, which is the function that
  does `strncpy(byte_6A4CA0, msg, 0x7F)`. The **transient message**, 5 s.
* **osc 4** — duration 5000, tick `sub_42B770`:
  `value = elapsed * 360 / duration` (`lea eax,[eax+eax*4]`,
  `lea eax,[eax+eax*8]`, `shl eax,3` = x360). **One turn every 5 s.**

---

## 1. `0x0049DBF0` vs the port's `kCbSneakQuitShow` — the verdict

**The three arms match, instruction for instruction.** What the port does at
`engine/src/ui/widgets.cpp:1973..1992` is what the image does:

| engine | `widgets.cpp` |
|---|---|
| `0x0049DBF0`: `I2D_SetFlagOnAllRows(0x004DEBA0, 0x40000001, 0)`; `word_4DEBA2 = 1`; `screen->[0x1C]->[0x18] = 1`; return 1 | `setListOff(kListSneakQuit, false); selMap()[kListSneakQuit] = 1; cur_ = 1;` |
| `0x0049DBC0`: `screen->[0x1C]->[0x18] = 0`; `I2D_SetFlagOnAllRows(..., 1)` | `setListOff(kListSneakQuit, true); cur_ = 0;` |
| `0x0049DBA0`: `sub_409090()` then `screen[+8] = 3` | `quitRequest_ = true; panel_ = nullptr;` |

`sub_409090` (`0x00409090`) really is two instructions,
`mov dword ptr ds:4E6C9C, 1` / `retn`, verified in the image; `dword_4E6C9C`
is read at `0x00407E2C`, a `case 1` arm of the game's state jump table, which
on seeing it nonzero calls `sub_407DC0(3, 0)` then `sub_407DC0(2, 0)` and
clears it. The port's split into a quit request plus a screen close is right.

**Neither `0x0049DBF0` nor either sibling writes `dword_930750`.** The ANSWER
global is not touched anywhere in this family.

### The disagreement, and it is load-bearing

`engine/src/ui/widgets.h:208-214` and the comment at `widgets.cpp:1966-1972`
both say:

> the page `0x004DF0C0` is **never installed**: … `Ui_ConfirmSelection`
> prefers a callback over a `+44` child, and nothing else installs it … The
> page is **BUILT AND UNREACHABLE**, the shape options page 12 already has -
> and its Oui/Non list belongs to it alone, so what the callback shows is
> drawn by nothing.

**That is wrong.** `Ui_ConfirmSelection` is not the only descent.
`Ui_MoveSelection` (`0x0042A7E0`) has a tail that enters an item's `+44`
child **on the MOVE**, and the raw listing is unambiguous
(`loc_42A897` onward, read from `Runtime.exe.asm`, not the decompiler):

```
loc_42A897:
  ecx = list->+2                      ; the new selection
  if (old == ecx) -> loc_42A8FB:  return Ui_ConfirmSelection(screen, list)
  eax = list->+0Ch                    ; the item array
  if (!eax) return 1
  eax = items[ecx]
  esi = item->+2Ch                    ; the CHILD panel
  if (!esi) return 1
  eax = item->+30h ; and eax, 40h     ; bank A bit 0x40
  if (!eax) return 1
  if (screen->+1Ch == esi) return 1
  screen->+24h = esi
  if (old panel && old->+8) old->+8(screen, old)     ; the LEAVE hook
  eax = newPanel->+4                                 ; the BUILDER
  screen->+20h = screen->+1Ch
  screen->+1Ch = esi
  if (eax) eax(screen, newPanel)
  return 1
```

And `Ui_ConfirmSelection`'s own child path carries the complement,
`if ((v3[12] & 0x40) != 0) return 0;` — it refuses a `+44` child that carries
`0x40`, precisely because the move already entered it.

The item records, read out of the image:

| item | string | `+2C` child | `+30` bank A | `0x40` |
|---|---|---|---|---|
| `0x004DDFB0` | 0 `Identité` | `0x004DED80` | `0x20000040` | yes |
| `0x004DDFF8` | 1 `Appel du slider` | `0x004DEDE8` | `0x20000040` | yes |
| `0x004DE040` | 2 `Inventaire` | `0x004DEE50` | `0x20000040` | yes |
| `0x004DE088` | 3 `Mémoire` | `0x004DEF88` | `0x20000040` | yes |
| `0x004DE0D0` | 4 `Options` | `0x004DF058` | `0x20000040` | yes |
| **`0x004DE118`** | **32 `Quitter le jeu`** | **`0x004DF0C0`** | `0x20000040` | **yes** |

So the whole tab column is a **move-driven tab strip**: the highlight landing
on an icon installs that page and runs its builder, before any confirm. Panel
`0x004DF0C0`'s builder field is `0x0049D980` in the image, and that function
is the exact inverse of `0x0049DBF0` — which is only sensible if it runs:

```c
/* 0x0049D980 - panel 0x004DF0C0's +4 builder */
I2D_SetFlagOnAllRows(0x004DEBA0, 0x40000001, 1);    // hide Oui/Non
sub_4296D0(0x004DEC58, u8(0x4DE118,8), u8(0x4DE118,9), u8(0x4DE118,10));
                                                    // tint the ECHO BAR with the
                                                    // Quit icon's own rgb (255,255,255)
sub_4296B0(0x004DEC08, 0, 0, 0);                    // the clock item -> black
u32(0x004DF0C0, 0x18) = 0;                          // dword_4DF0D8: current list := 0
i16(0x004DEBA0, 2)    = 0;                          // word_4DEBA2:  selection := 0
```

`word_4DEBA2` has exactly two writers in the whole listing — this builder
(`= 0`) and `0x0049DBF0` (`= 1`). `dword_4DF0D8` has one, this builder.

**Consequences for the port.** `UiWalk::move` (`widgets.cpp:1276`) implements
the selection step and the fall-through to confirm and **stops there** — it
has no `+44`-on-move descent at all (`it->child` appears only inside
`confirm()`, at line 2292). So two things follow, and they are the same
omission:

* the quit page is unreachable in the replica **because the replica made it
  so**, not because the engine does;
* every sneak page switch, and every other `0x20000040` tab strip in the
  tree, happens on CONFIRM in the replica and on MOVE in the engine — a
  one-keypress difference the whole way down.

The fix is `Ui_MoveSelection`'s tail, which is nine lines. This report does
not make it.

---

## 2. What the four hooks draw on the Inventaire page, and what the port is missing

The page is panel **`0x004DEE50`** (screen 9; reached from screen 0 through
icon `0x004DE040`) and its verb child **`0x004DEEB8`**. Its lists, in order:
`0x004DE210` tabs, `0x004DE420` the three tiles, `0x004DE318` the three verbs,
`0x004DE6F0` the nine rows, `0x004DEC58` the echo bar + clock.

`IAM\Sneak` (535 bytes, 44 strings; `IAM\FRENCH\Sneak` is the same file):
0..4 `Identité` / `Appel du slider` / `Inventaire` / `Mémoire` / `Options`;
5,6,7 `Utiliser` / `Utiliser sur` / `Examiner`;
8,9,41 `Seteks en votre possession :` / `Anneaux en votre possession :` /
`Lire plan`; 32,33,34 `Quitter le jeu` / `Oui` / `Non`.

| hook | draws | in the port |
|---|---|---|
| `0x0049DF30` on item `0x004DE338` (str 8) | a turning **setek** | ported (`UiModels` slot 0) — **camera distance wrong**, §2a |
| `0x0049DEE0` on item `0x004DE380` (str 9) | a turning **anneau** | ported (slot 1) — same |
| `0x0049DF80` on item `0x004DE3C8` (str 41) | a turning **imager** | ported (slot 2) — same |
| `0x0049C090` on the nine rows `0x004DE440`..`0x004DE680` | a **filled highlight bar** behind the marked row | **NOT ported** — §2b |
| (`0x0049DC20`, the echo bar's `textFn`) | the page's **status line** | **NOT ported** — §2c |

**None of the three is a decoration the sheet supplies.** The tiles are real
3D submissions (`I2D_Submit3DView`), the highlight is a computed quad, and the
echo bar is composed text.

### 2a. The three tiles — ported, with one wrong number

Each hook is 0x50 bytes and they differ only in one pair of globals.
`Ui_DrawItem` runs a `+20` hook as `v3(screen, panel, item)`; the hooks read
`[esp+44h]` with one push live, i.e. arg3 = the item, and pass
`(screen, panel, item, OBJECT, &view)` to `sub_478360` — which matches
`sub_478360`'s own body (`Ui_ItemScreenX(a1, a2, a3)` with
`Ui_ItemScreenX(screen, panel, item) = item->x + panel->+84`).

```c
void Sneak_DrawTile(int screen, int panel, int item)
{
    char view[0x34];
    sub_478DE0(NODE, 0.0f, view);            // 6a 00 - the second argument is ZERO
    sub_478EC0(NODE);                        // spin about Y from oscillator 4
    sub_478360(screen, panel, item, OBJECT, view);
}
```

| hook | NODE | OBJECT | loaded from |
|---|---|---|---|
| `0x0049DF30` | `dword_670BFC` | `dword_670BF8` | `meshes\objets\setek.3do` |
| `0x0049DEE0` | `dword_670C5C` | `dword_670C58` | `meshes\objets\anneau.3do` |
| `0x0049DF80` | `dword_670CC4` | `dword_670CC0` | `meshes\objets\imager.3do` |

Loaded by `sub_49B400`'s param-0 arm (`loc_49B4C4`, the arm that installs
`0x004DEE50`) with `sub_41E200(path)` = `Scene_Load3DO` and
`sub_41E230(0, obj, &node)` = `o3de_FindNodeByName(obj, NULL)`, and released
by the close hook (`loc_49B6A5`) with `sub_441A00`. **The port's slot order
is right**: list order is setek, anneau, imager and load order is the same.

**The disagreement.** `engine/src/ui/models.h` says

> the distance the sneak passes is `0x42EC3871` = 118.110, which is
> 3.0 / 0.0254 - THREE METRES … a positive distance uses the literal offset
> and SKIPS the bounding-box fit.

`0x42EC3871` is pushed at **one** site in the whole listing, and it is not one
of these. `sub_478DE0` has nine callers; the ones that matter here:

| site | distance | object |
|---|---|---|
| `0x0049DF30` / `0x0049DEE0` / `0x0049DF80` | `push 0` (`6a 00`) | the three tiles |
| `~0x004779C0` | `push 42EC3871h` = 118.110 = **3 m** | `dword_6A4748` |
| `~0x0042E5F0` | `push 43C4D9B3h` = 393.700 = **10 m**, and it also writes `41A00000h` (20.0) into `view+0x30`, the FOV | `dword_6A4748` |
| `~0x00477B20` | `push 0` | `sub_41CFF0(sub_42B2C0(tag))->+4`, i.e. an inventory object's own model |

`dword_6A4748` is set by `sub_4778E0(animName)`, which loads
`sub_41E200(dword_930724->[0] + 0x30)` plus an `ANIMS\%s` clip — the
**character** model, and the sneak's open calls it with `"F1AVNT.CTL"`. So the
3 m literal belongs to the **identity page's character view**, not to the
three inventory tiles.

What the tiles actually get, with `a2 = 0`, is the auto-fit arm of
`sub_478DE0`:

```
d    = u32(node, 0);
view[+20..+28] = view[+32..+40] = (f32(d,36), f32(d,40), f32(d,44));   // the centre
view[+44] = 0;  view[+48] = 50.0f;                                     // roll, FOV in DEGREES
sub_437D60(node, &lo, &hi);                                            // bounding box
size = max(hi.x-lo.x, hi.y-lo.y, hi.z-lo.z);
view[+28] += size + size / tan(view[+48] * pi/180);                    // push the eye back in Z
```

i.e. `d = size * (1 + 1/tan(50 deg)) = 1.839 * size` — **fitted per model**,
not a fixed 3 m. Note that `tan` takes the whole 50 degrees, not a half-angle;
transcribe it as written. The spin the port has is right: `sub_478EC0(node)` is
`Matrix3x3_FromEulerAngles(0, osc4.value * pi/180, 0, node + 56)`, the same
`sub_441EB0(0, angle, 0, node+0x38)` `models.h` describes, one turn per 5 s.

**Whether it is visible is for the viewer to say** — three small props at 3 m
against a per-model fit will differ in apparent size, and the effect is
per model. This report only establishes which number the engine uses.

One thing not established: `sub_478DE0` writes only `view[+20..+48]` of a
0x34-byte stack local, so `view[+0..+19]` reach `I2D_Submit3DView`
(`0x00428900`) as uninitialised stack unless that function fills them. It was
not read in this pass.

### 2b. `0x0049C090` — the row HIGHLIGHT, missing from the port

The `+20` hook of the nine row items of list `0x004DE6F0` — `0x004DE440`,
`0x4DE488`, `0x4DE4D0`, `0x4DE518`, `0x4DE560`, `0x4DE5A8`, `0x4DE5F0`,
`0x4DE638`, `0x4DE680`, each 400x20 at x=190, y=100..340 step 30, `textFn`
`0x0042AA00` (the inventory-name one), callback `0x0049BC60`, and **flag bank
A = 0**, so nothing in the record asks for a fill.

```c
void Sneak_DrawRowHighlight(int screen, int panel, int item)
{
    int tag = u32(item, 0x3C);
    if (tag == -1) return;
    if (u32(screen, 0x1C) != 0x004DEEB8) return;        // ONLY while the VERB panel is up
    int hit = 0;
    if (dword_670BE0 == 0) {
        if (Ui_ListSelectedItem(0x004DE6F0) == item) hit = 1;
    } else {
        if (tag == dword_670BE4 || tag == dword_670BE8 || tag == dword_670BEC) hit = 1;
    }
    if (hit) Ui_DrawItemFill(screen, panel, item);      // sub_476FE0
}
```

* `0x004DEEB8` is the **verb panel** (`tools/exetables.py` already names it;
  `+0` parent `0x004DEE50`, installed by `loc_49BE7B`'s
  `sub_42A370(screen, off_4DEEB8)`), so the bar appears **only while a verb is
  being chosen**.
* `dword_670BE0` = 0 ordinary, 1 a **`Utiliser sur` combination is pending**
  (set at `0x0049BAC2`, cleared at `0x0049B8A0`'s tail).
  `dword_670BE4`/`BE8` are that combination's two object tags, ordered by
  `sub_42B520`; `dword_670BEC` is a third, written again at `0x0049B607`. All
  three are reset to -1 together at `0x0049B930`.
* `sub_476FE0` is `Ui_DrawItemFill`, the same primitive `Ui_DrawItem` runs for
  `UIF_FILL / 0x44000000`. The rows carry none of those bits, so **this hook
  is the only fill a row ever gets**.

**Missing from the port.** `screendraw.cpp` dispatches a fixed set of draw
hooks (`kDrawViewport`, `kDrawSneakCharacter`, `kDrawLoadRows`,
`kDrawNameField`, `0x004780A0`, `0x00477F60`, `kDrawShopPreview`,
`kDrawInterference`, `kDrawSneakIdentity`, `kDrawSneakCharacteristics`,
`kDrawHighScore`); `0x0049C090` is not among them. So while a verb is being
chosen, the replica shows no bar behind the row the verb will act on, and
during a `Utiliser sur` no mark on the one or two rows already chosen — which
is the only feedback the page gives for that operation.

The colour is not a placeholder: the rows ship rgb (255, 0, 0), and the page
builder overwrites it with the tab icon's colour via `sub_4296D0` — amber
(240, 135, 15) from `0x004DE040` for the Inventaire page. The port already
models that builder colouring (`widgets.cpp:1212`), so the colour is in hand;
only the fill is not.

### 2c. `0x0049DC20` — the ECHO BAR, also missing

The `+32` text callback of item **`0x004DEBC0`**, 411x24 at (180, 398), font
74 (`J`, JOURNAL), the only selectable item of list `0x004DEC58` — which is
the **last list of every sneak page**. Signature `(screen, item, out)`, from
`Ui_DrawItem`'s `v5(a1, a3, Destination)`; the frame arithmetic agrees
(`sub esp,220h` + 3 pushes, so `[esp+230h]` is arg1 and `[esp+238h]` arg3).

Two 0x100-byte stack buffers at `esp_f+0x2C` (`A`) and `esp_f+0x12C` (`B`),
each initialised from `byte_670D24` / `byte_670D28` — BSS bytes read once in
the whole listing and never written, i.e. the compiler's source of a zero.

```c
int Sneak_EchoBar(int screen, int item /* unused */, char *out)
{
    /* 0x0049DC20 */
    if (Ui_OscillatorFlags(Ui_Oscillator(0), 1)) {   // a 5 s message is up
        sub_478D60(out);                             // strcpy(out, byte_6A4CA0)
        return 1;
    }
    /* 0x0049DC62 */
    char A[0x100] = "", B[0x100] = "";
    int panel = u32(screen, 0x1C);
    if (!panel) return 1;
    int sel = Ui_PanelSelectedItem(panel);           // sub_428F50
    if (!sel) return 0;

    /* 0x0049DCA6 - see "not established" #1 */
    int row = Ui_ListSelectedItem(0x004DE6F0);
    if (row) sub_42AA00(screen, row, B);             // B is never read again

    /* 0x0049DCC9 */ if (sel == 0x004DE338) { Ui_ItemStringDefault(screen, sel, A);
                       sprintf(out, "%s %d", A, sub_42B1C0(4)); return 1; }   // Seteks
    /* 0x0049DD14 */ if (sel == 0x004DE380) { Ui_ItemStringDefault(screen, sel, A);
                       sprintf(out, "%s %d", A, sub_42B1C0(5)); return 1; }   // Anneaux
    /* 0x0049DD5F */ if (sel == 0x004DE3C8) { Ui_ItemStringDefault(screen, sel, out);
                       return 1; }                                            // Lire plan
    /* 0x0049DD88 */ if (sel == 0x004DE230) { i16(sel,0x1E)=1;                // Utiliser
                       Ui_ItemStringDefault(screen, sel, out); i16(sel,0x1E)=0; return 1; }
    /* 0x0049DDBB */ if (sel == 0x004DE278) { i16(sel,0x1E)=1;                // Utiliser sur
                       Ui_ItemStringDefault(screen, sel, out); i16(sel,0x1E)=0; return 1; }
    /* 0x0049DDEE */ if (sel == 0x004DE2C0 || sel == 0x004DE710) {            // Examiner, or
                       word_4DE2DE = 1;              /* 0x0049DEAD: an ABSOLUTE   the examine box
                                                        store to 0x004DE2C0+0x1E,
                                                        on BOTH arms */
                       Ui_ItemStringDefault(screen, sel, out);
                       word_4DE2DE = 0; return 1; }
    /* 0x0049DE06 */ int tab = Ui_ListSelectedItem(0x004DE210);
                     if (!tab) return 1;
    /* 0x0049DE1B */ if (tab == 0x004DE040) {                                  // the Inventaire tab
                       Ui_ItemStringDefault(screen, tab, out);
                       sprintf(tmp, "  (%d / 18)", dword_4DE708);
                       strcat(out, tmp);             // inlined repne scasb / rep movsd
                       return 1; }
    /* 0x0049DE85 */ Ui_ItemStringDefault(screen, tab, out);
                     return 1;
}
```

Format strings: `aSD_2`, `aSD_3` both `"%s %d"`; `aD18` is `"  (%d / 18)"`.

* **`sub_42B1C0(n)`** builds `{n, ...}`, calls `Actor_Player()`, raises
  **`Game_HandleEvent(44)`** and returns slot `[2]`. Property **4 = seteks**,
  **5 = anneaux** — corroborated independently by `docs/UI.md`'s note that
  `sub_4AE060` refuses to save when **property 5** reads zero, and a save
  costs one *anneau*. The port already uses property 5 that way
  (`widgets.cpp:1860`).
* **`dword_4DE708` is list `0x004DE6F0 + 0x18`.** The list stride is 0x20:
  list `0x4DE6F0`'s `+0x20`/`+0x24` are `0x00640096` / `0x01040190` =
  (150, 100, 400, 260), exactly item `0x004DE710`'s x/y/w/h — the records
  abut. Printed as `(n / 18)`, it is the **inventory occupancy against an
  18-slot capacity**; one of its seven readers, at `loc_49BA9A`, clears panel
  `0x004DEE50`'s current list when it is zero.
* **`sub_478D60(dst)` / `byte_6A4CA0`**: `strcpy(dst, byte_6A4CA0)`, a
  128-byte global written only by `sub_42B660(timer, ms, src)`, which is
  oscillator 0's start function. So **while a transient message is up, the
  bar shows it and nothing else, for 5 s.**

So the bar reads, in turn: `Seteks en votre possession : 27`,
`Anneaux en votre possession : 3`, `Lire plan`, `Utiliser`, `Utiliser sur`,
`Examiner`, or the current tab's own label — `Inventaire  (5 / 18)` on this
page.

**Missing from the port.** `screendraw.cpp:1250` states it plainly: "Of the
callbacks, two are modelled: `0x00476860` … and `0x0042AA00` … The other
thirteen are native and draw nothing." `0x0049DC20` is one of the thirteen,
and item `0x004DEBC0` has no `+24` literal and no string id, so the composer
`continue`s past it. **The sneak's status line is blank in the replica** — no
money or ring counts, no verb name, no `(n / 18)`, no transient message.
That is the largest visible gap on the page, because those two counts are the
only place the player's seteks and anneaux are shown at all.

### 2d. The `{TEXT ERROR!}` prefix — do not "fix" it away

`Ui_ItemStringDefault` (`0x00476860`): if `+28 < 0` it writes nothing; if
`I2D_TestFlag(item, 0x80000200)` it `strcpy`s the string; **otherwise, if
`+30 != -1`, it calls `sub_43FEA0(+30, string, out)`** — the
bracketed-section extractor the terminal pages use (`IAM\Term`, `Arch`,
`Morg`, `Fsim`, `Surv` together hold 19 `[`).

Read from the raw listing: `[` at depth 1 with `index == 0` opens the span,
`]` at depth 0 with `index == 0` closes it cleanly, and **reaching the NUL
sets the error flag**, on which the output is prefixed with the inline literal
at `0x004C724C` = `"{TEXT ERROR!}"` (the null-source arm uses `0x004C723C` =
`"{TEXT ERROR !}"`, one space different).

`IAM\Sneak` contains **no brackets**, and the three verb items ship `+30 = 0`
(verified byte for byte: `0x4DE230` `+1C = 05 00`, `+1E = 00 00`), as do
`Oui`/`Non` and eighteen other items across the tree. So every draw of the
verb row composes `"{TEXT ERROR!}Utiliser"`. It is invisible because the text
scanner treats `{` as opening a markup command and swallows everything to `}`
with `default: break` for an unknown one — so the echo bar's `+30 = 1` writes
change nothing visible either (index 1 fails the same way). **A port must not
print the prefix, and must not "repair" the bar by dropping the `+30` path.**

That last step — the swallowing — is read from `engine/src/ui/text.cpp`'s
transcription of the scanner, **not** re-derived from `Runtime.exe.asm` here;
see "not established" #5.

---

## 3. What could NOT be established

1. **The dead call at `0x0049DCA6`.** `sub_42AA00(screen, row, B)` fills the
   second buffer with the selected row's object name (either
   `sub_40E540(tag)` or `Game_RaiseEvent(33)`), and **`B` is never read
   again** — the only references to `esp+12Ch` are the `B[0] = 0` and the
   `lea` that passes it. A port that skips it changes no text, but also stops
   raising event 33 once per echo-bar draw. Which was intended is not
   decidable from the listing.
2. **The absolute store at `0x0049DEAD`** (`66 c7 05 de e2 4d 00 01 00`,
   verified) writes `0x004DE2C0 + 0x1E` on an arm shared by
   `sel == 0x4DE2C0` and `sel == 0x4DE710`, so for the latter it pokes a
   different item's `+30`. Compiler fold or authoring slip — cannot be told
   apart. Visible consequence: none, by §2d.
3. **The `0x004DE710` arm produces an EMPTY bar** in the shipped data: that
   item (the examine page's 400x260 box, panel `0x004DEF20`, drawFn
   `0x004780A0`) ships `+28 = -1`, so `Ui_ItemStringDefault` writes nothing
   and `Ui_DrawItem` has pre-cleared the buffer. Nothing found in this pass
   binds a string into its `+28`.
4. **`view[+0..+19]`** of the tile camera block is never written by
   `sub_478DE0`; `I2D_Submit3DView` (`0x00428900`) was not read here.
5. **The `{TEXT ERROR!}` swallowing** is one step removed (port transcription,
   not the listing). The prefix's existence and its two literals are certain.
   If it is checked directly: inside `{TEXT ERROR!}` the two `E`s are real
   commands that each consume the following character, so the scanner ends up
   setting `eValue` to `'X'` then `'R'`.
6. **`sub_42B520`** (the ordering test behind `dword_670BE4`/`BE8`) and
   `sub_429B40` / `sub_429F10` / `sub_42B560` were not read.
7. **Event 44's property table** was not read; 4 = seteks and 5 = anneaux is
   corroboration, not a transcription.
8. Panels carry **two** hook fields: `0x004DED80` has `0x0049C1D0` at `+12`
   and 0 at `+16`, `0x004DEE50` has 0 at `+12` and `0x0042A710` at `+16`.
   `tools/exetables.py` lifts only `+16`. Not needed above, but a port of the
   sneak's input will meet it.

---

## 4. If someone picks this up

Three separable pieces, in the order their absence shows:

1. **The echo bar** (§2c) — item `0x004DEBC0`, list `0x004DEC58`, on all six
   sneak pages. Needs oscillator 0 + `byte_6A4CA0`, `Ui_PanelSelectedItem`,
   the six special items, the tab list, `dword_4DE708`, and event-44
   properties 4 and 5. The page builders' tint of `0x004DEC58` is already
   ported.
2. **`Ui_MoveSelection`'s tail** (§1) — nine lines, and it is what makes the
   whole tree's tab strips behave like the engine's. It also un-breaks the
   quit page, which the port's own comment currently calls unreachable.
3. **The row highlight** (§2b) — draw hook `0x0049C090` on list
   `0x004DE6F0`, gated on panel `0x004DEEB8`, plus the three combination
   globals.

And one number to correct rather than build: the tile camera distance in
`engine/src/ui/models.h` (§2a).

A free cross-check for whoever does 1 and 2 together: **the three tiles have
no text of their own** (no `+24` literal, no `textFn`), so if a port ever
draws a label under those icons, either the tiles or the echo bar is wrong.

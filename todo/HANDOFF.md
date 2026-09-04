# Handoff — 2026-09-04, the sneak and the object flow

Written at the end of a long session so another can pick it up. **The durable
findings are in `todo/sneak.md`, `todo/sliders.md` and `docs/UI.md`; this file
is the session state around them** — what landed, what broke on the way, what
is open, and how to drive it.

Tree state at handoff: **`main` = `c1d0681`, clean, in sync with `origin`,
`verify.py` 164 checks / 0 failed.**

---

## 1. What landed (13 commits, all pushed)

Every one has a `verify.py` check that was SHOWN to fail first.

| commit | what |
|---|---|
| `6805974` | **using an object on the world**: `Session::useObject` (case 35's non-consumable arm), the pump's dry run (`Script_RunToOpcode75`), and `Utiliser` closing the sneak |
| `c3f13da` | the world's **action button comes from `MDACTION`**, not the input edge |
| `11cd32a` | the key at the lift **confirmed in play**, plus the demo recipe |
| `ce1a0cc` | **taking an object reaches the inventory** (case 10) and clears the right prop bit |
| `d1f5e28` `958d286` | `todo/sliders.md` and `todo/sneak.md` — what is left in each |
| `a538ce3` | **`Inventory_Insert` read in full**: kinds 12/13 are CONSUMED, not merged |
| `3c462a1` | **the rows SCROLL** — `sub_42AFF0` is a centred window |
| `644b51a` `7e1c58d` | **`Utiliser sur` is a MODE**, and its gate is dead from both ends |
| `4c8d8cf` | the **memory page is empty by the code** — step 3 was a non-task |
| `f415e6c` | the **identity page has two sub-sections**, from two player captures |
| `c1d0681` | the combine mode was a **one-way door** — a player lost the verb bar |

**The chain that now works end to end, in play:** open the sneak → use the
apartment key → the sneak closes itself → press action at the lift → zone 3889
→ camera 4354 → `actor.goto_address 663` → `area.goto 237` → standing in
AAPKAYL. Confirmed by the user, not just headlessly.

## 2. What is NOT working, or is knowingly absent

* **The object in the hand is invisible.** `sub_41C490` writes `player[+0xA4]`
  AND attaches the model (`sub_437400` / `sub_4374E0`); the port does the first
  only. A used key works and shows nothing. `todo/sneak.md` step 5.
* **`Object_ApplyEffect` (0x00409780) is NAMED, body as generated.** The
  consumable arm and the `Consumed`/`Merged` bank arms announce an effect they
  do not apply, and say so where it happens. Its sibling — the context gate,
  "may this object be used HERE" — has not been read at all.
* **The walker does not block on walls.** AHALL27's walls are in the STEEP
  soup, which the walker SLIDES off rather than stopping at, so
  `StepResult::Blocked` has never fired there and walking east past x 4840
  leaves the geometry entirely (`floorUnder` NONE at 4964) and falls. A player
  hit this twice. The narrow phase against `SoupKind::All` is unported. **Not
  a sneak problem — it is the walker**, and it is the thing most likely to
  interrupt the next play-test.
* **A held button re-enters the `.CTL` action state**, so two frames of ENTER
  give two activations (two voice lines at the lift). Cosmetic, not chased.
* **The identity page draws nothing.** Structure fully read (see below), no
  code written.
* **`Text_LayOutBlock` is not ported** — the composer's wrap is a labelled
  reconstruction, visible on the sneak's short captions.

### The build system, and it cost real time

**`make` does not always rebuild after an edit.** Six times this session a
measurement flipped with no source change, and twice `touch` was not enough —
only `rm -f build/obj/<path>.o` settled it. It has twice made a falsification
look like it passed. **When a number changes and the source did not, distrust
the binary before the reading.** Worth fixing in the Makefile's `-MMD -MP`
dependency generation before it causes a wrong conclusion.

## 3. What is left

`todo/sneak.md` §3 is the queue; steps 1-3 are done, **4-8 open**:

| # | step |
|---|---|
| 4 | `Text_LayOutBlock` — the real wrap |
| 5 | the hand attach, so a used object is visible while held |
| 6 | `Object_ApplyEffect` and its context gate |
| 7 | **the identity page** — the two-tab switch is small and read; then the character view; then the per-character TEXT, whose source is NOT found |
| 8 | the other four page builders, each bounded before it is shipped |

**Step 7 is the biggest and best-specified.** From two captures a player
supplied: the page is `Identity` (Name, Age, Sex, Blood Type, Height, Weight,
Eyes, Job, and the prose lines Signs / Interests) and `Characteristics`
(Energy, Attack, Fight Experience — **a WORD, "Initiate", not a number** —
Body Resistance, Speed, Dodge, Mana, each with a **filled bar**). The widget
tree already carries it: list `0x004DE900`, two tabs at (187,30)/(389,30)
strings 10/11, **two content items at the same rect (250,100) 300x270** so
alternatives, and the character view at (0,50) 360x300. The switch is
`sub_42A930` plus a two-case swap of `0x40000001` — both already ported in
pieces. **What is missing is where the text comes from**: both content items
ship `string -1`, `text 0` (+24) and `textFn 0` (+32), so something outside
the item draws that box. The actor table's 276-byte record is the first place
to look; `player.become` announcing to CHARACTERS the second.

`todo/sliders.md` is a separate, untouched queue — the player's RIDE (call,
mount, choose, fly, arrive) is not ported at all; step 1 there is reading
`sub_452570`.

## 4. How to drive it

```bash
cd engine && make play
build/omk-play <gamedata> ../tables --vulkan --res 1024x768 --nofmv \
    --save ../traces/save-appart.bin --newgame-world \
    --area 237 --address 677 --give 18,7,26,20,108,156,44,45,46,47,48
```

13 carried objects in Kay'l's apartment — enough for **row scrolling** (more
than the nine widgets) and for **`Utiliser sur`** (18 `Petite boîte` + 7
`Petite clé` → 33 `Petite boîte ouverte`; 26 `Tasse de koil` + 20 `Somnifère`
→ 99 `Tasse de koil droguée`).

The lift, which is the object-use chain end to end:

```bash
build/omk-play <gamedata> ../tables --save ../traces/save-appart.bin \
    --newgame-world --area 229 --stand 4460,-25,-742,0
```

**No `--give`** there: a new game already carries object 6. Two gotchas —
`--stand 4482,...` (the zone centre) is walkable but the hall is small, and
the user found `4460,-25,-742,0` better; and this is **zsh**, so a flag string
in a variable needs `${=VAR}`.

## 5. Coordination — there is a SECOND session in this checkout

`omikron-tns-omk-engine-58`, addressable at `uds:/tmp/cc-socks/78950.sock`.
It is on branch **`take-height`** (a git worktree), which its user has said is
**not to be merged into main for now**. Its two commits `c318a57` / `55d7125`
sit off main.

**The protocol we agreed, and it should continue:**

* **Ask per FILE before editing, and say when you are done with it.** Not per
  slice — they explicitly preferred answering four times over having it
  inferred once.
* **Never `git add -A`.** Stage explicit paths. My `git add -A` swept their
  uncommitted work into a commit earlier in the day (`d6a7999`) and reverted
  their `play.cpp` fix in another (`8f9de78`/`323f1ff`). That is what the
  whole protocol exists for.
* **Commit and push promptly** so main is never far ahead of what they would
  rebase onto.
* Their region of `play.cpp` is the **flag cluster in the first ~1800 lines**
  plus the **take arms at 3650-3710**. Mine has been 5590-5860 (the sneak).
  `docs/UI.md`'s **"UI input path"** section is theirs to write — they found
  that the engine has THREE input words with three filters (`Ui_BeginScreen`'s
  0x203F mask for a screen, the raw word for the world, `dword_90E0E0` for a
  conversation) and a dialogue is NOT a screen. Do not generalise one to the
  others; the sneak must keep reading `bits`, not edges.

**Their in-flight work, for context:** the take is two stages and the port did
only the second — `MDACTION` installs group 600 `H_ADJSTP` (the character
steps into position) and the take comes from `MDADJSTP`, whose reach is 120 cm
against MDACTION's 150. And a take clip is a GRID of 21-frame variants
(`H_TAKH12` 189 frames = 9 x 21, `H_TAKL12` 126 = 6 x 21), the count coming
from the `.CTL` entry's own top nibble, which partitions the bank exactly.
The port plays frame 0 to the end, which is every grab in a row.

## 6. Two traps this session, both worth not repeating

* **IDA invents pointers out of coordinate pairs.** The listing shows
  `off_4DE810 dd offset unk_6400FA`, which reads as a pointer to a shared text
  buffer. It is not one: item `+0` is the X and `+2` the Y, both int16, so the
  dword is `0x006400FA` = y 100, x 250 — the item's own coordinates. Ten
  minutes went into "what fills that buffer". There is no buffer.
* **Reading a function to the first plausible stopping point.** `ce1a0cc`
  shipped a gate over `Inventory_Insert`'s kinds "2..13" from the first 45
  lines, with a green check beside it. The other 100 lines say 12 and 13 skip
  the ladder entirely and 2..11 with no matching row still earn a row. A
  player's play-test found it, not the suite. **A check written beside a
  half-read function tests the half you read.**

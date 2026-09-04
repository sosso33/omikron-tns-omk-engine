# The sliders — what is left

The **slider** is Omikron's flying taxi: you call one from the sneak's slider
page, it comes to you, you get in, you pick a destination, and it flies you
there. Opened 2026-09-04 from *"save what is remaining for the sliders"*.

Three separate things share the word and only one of them is done, so the
first job of this file is to keep them apart:

| | state |
|---|---|
| **the AMBIENT traffic** — sliders and motos on a city's vehicle lanes | **ported** (STREET_LIFE §2b): spawned by the walkers' own `sub_453B40`, driven by the walkers' mover, drawn. `engine: road traffic`, `engine: traffic frame` |
| **the sneak's SLIDER PAGE** — the 39 destinations | **partly**: the page is built, coloured and navigable, and the destination list is read out of `GLOBAL +16`. Confirming a row does **nothing** |
| **the PLAYER'S RIDE** — call, mount, choose, fly, arrive | **not ported at all**. This file is about that |

Everything below is read from the binary; nothing here is implemented.

---

## 1. What the data already gives us

`GLOBAL +16` (array) / `+28` (count): **39 destination records**, 36 bytes
each — `+0` an int16 **save-bit index**, `+4` a 32-byte inline name. Read and
ported (`engine/src/script/globaldata.*`, `globalDestinations()`), and the
walk fills the slider page's rows from it. Destination **0** is
`Anekbah - Appartement de Kay'l`; the list spans all four cities and the bit
index is what gates whether a destination is offered yet.

The `.OPT` traffic circuit is read (`engine/src/formats/opt.*`, 6/6 files
exact) and it is the road the slider flies. The reservation groups, lanes and
action points are all already modelled for the ambient traffic.

## 2. The chain, as the engine runs it

### 2a. Confirming a destination — `sub_49BC60`'s slider arm

The sneak's shared row-confirm callback (`0x0049BC60`, no `proc` label — it is
a dword in the widget tree, CLAUDE.md §1's trap) branches on the row kind. Its
slider arm, read from the listing at `loc_49BCFA`:

    if (row address == -1)                     // "call it to me"
        point = player[+0xF4], [+0xF8], [+0xFC]        // the player's position
    else
        point = (float) Address_Find(addr)[0], [4], [8]  // int -> float

    if (sub_452570(&point)) {                  // can a slider reach this point?
        dword_6A17CC = item[+0x3C];            // the chosen DESTINATION
        screen[+8]   = 3;                      // state 3 = closing (UI.md)
        return 0;                              // the sneak shuts
    }
    sub_4767E0(screen, buf, 42, -1);           // else: interface text 42
    sub_42B820(0, -1, ...);                    //       and show it

So the confirm answers a **reachability question** and, if it passes, leaves a
pending destination in a global and closes the device. Nothing travels yet.

* **`sub_452570`** (`readable/src/18_d3d.c:3047`, **RAW**, 209 lines) is the
  reachability test: it walks the resident circuit's node array
  (`dword_8F5E48`, 24-byte records, count from `+5 - +2`) looking for a node
  the slider can use near the point, and gates on `dword_8F5E3C` and on the
  current mode `u32(dword_8F5E44, 8) == 4`. **Not read properly, not ported.**
* **`dword_6A17CC`** is the pending destination: initialised to -1 inside
  `sub_49B400` (asm 244879), written here and at asm 245559, read at asm
  139556 and 247593. Five sites in the whole image, so it is a small thing to
  port and the question is entirely what writes and reads it.
* **Text 42** is the refusal a player sees when no slider can come — worth
  resolving in `IAM\Sneak` before implementing, because it is the only
  feedback the failing arm gives.

### 2b. Mounting — `MDSLIDIN`, and a screen nobody has walked

`tab_special_move` row for **`MDSLIDIN` (0x0046B7F0)** — *"mount the slider,
and open screen 7"*. **Screen 7 is `SLIDER`**, the third member of the sneak
family (0 `VIDEOPHONE`, 7 `SLIDER`, 9 `SNEAK`), and all three share
`sneak.bmp`. Its panel is in the lifted widget tree because
`Ui_OpenSneakFamily` was followed; **nothing has ever walked it**, so what its
lists hold is unknown.

The state machine around it is read and recorded in
`engine/src/actor/state.cpp`, and it is unusually strict:

| | |
|---|---|
| `MDSLIDIN` 0x0046B7F0 | writes ACTOR_STATE **7**, opens screen 7 |
| `sub_457040` 0x00457040 | the slider system's own hold — **the prior state parks in a GLOBAL, not in `[102]`** like every other park |
| `sub_4570F0` 0x004570F0 | and is restored from it |
| `sub_468FA0` 0x00468FA0 | the riding pose: `.CTL` **group 61**, `[101]` and `[102]` both 8 |
| `MDSLIDOU` 0x0046B890 | dismount, and it **REFUSES unless the state is 8** — the binary's own string is `"bad mode getting out of the slider !"` |

**State 7 has no case in `Actors_TickAll` at all** — already recorded in
`engine/src/actor/state.h`: the slider drives that actor from `Sliders_Tick`
instead. So a ported ride must move the player from the vehicle, not from the
actor tick.

### 2c. The travel and the arrival

`dword_6A17CC`'s reader at asm 139556 sits in the slider system around
`0x00456886` and builds a **camera**: `Address_Find(dword_6A17CC)` into
`dword_930808`, `dword_93080C = esi`, `dword_93081C = 1`,
`dword_930828 = -1`, then `sub_414BF0(10, &dword_930800)` — a mode-10 camera
request. Just above it, `sub_413360(...) == 8` selects between
`dword_930818 = 42700000h` — **60.0f exactly**, checked — and 0.

So the arrival is a camera sequence framing the destination address, and the
destination is an **ADDRESSES id**, not an area id. That is the join to find:
39 destination records with save-bit indices on one side, `Address_Find` on
the other, and nothing yet says how a row becomes the id in `dword_6A17CC`
(`item[+0x3C]` is the row widget's own field — what the builder wrote there is
the thing to read next).

The second reader (asm 247593) is in the slider UI module right after
`sub_457040`, and repeats the row-confirm's shape — the player position or a
named address. That is screen 7's own confirm, i.e. choosing where to go once
you are already aboard.

## 3. What the port has, and the one field that is a stub

`engine/src/actor/sliders.h`'s `Vehicle` already carries the ride:

    int state = 0;      // +8: 0 ambient traffic, 1..7 the player's ride
    bool reserved;      // +22 == 1: slot 0, the player's own slider

`reserved` is honoured — row 0 of the pool is kept for the player and ambient
traffic draws from row 1 up, which is why Qalisar (slider mask 1) puts nothing
but motos on its roads. But **`state` is only ever tested, never set**:
`engine/src/actor/vehicles.cpp:225` does `if (v.state != 0) continue;` and no
line anywhere assigns 1..7. The gate exists and nothing drives it.

## 4. Steps

Each ends in a commit and a `verify.py` check that has been SHOWN to fail.

| # | step | status |
|---|---|---|
| 0 | this file | **done 2026-09-04** |
| 1 | read `sub_452570` properly (promote it from RAW; it is the reachability test and the whole confirm turns on it) and resolve interface text 42 in `IAM\Sneak`. Deliverable: a Python or C++ probe answering, for each of the 39 destinations and a given resident circuit, whether a slider can reach the player — with the count asserted | |
| 2 | the join: what `item[+0x3C]` holds on a slider row, and how a destination record becomes the ADDRESSES id `Address_Find` is given. Read the slider page's builder the way the inventory page's was. Assert all 39 resolve, or say exactly how many do not and why | |
| 3 | walk screen 7. Its panel is already in the tree and has never been driven; `tools/sim/ui.py` and the port must agree on it the way they agree on the other 31 (`engine: UI`'s "screens the two disagree on" must stay 0) | |
| 4 | the mount: `MDSLIDIN` -> ACTOR_STATE 7, `sub_457040`'s **global** park (not `[102]`), the group-61 riding pose, and `MDSLIDOU`'s refusal outside state 8. The refusal is the test the data can fail | |
| 5 | drive `Vehicle::state` 1..7 and fly the reserved slider along the circuit to the destination; the mode-10 arrival camera | |
| 6 | watch it, in `omk-play`, from Kay'l's apartment to another destination | |

## 5. Cautions

* **State 7 is not a tick.** Nothing in `Actors_TickAll` handles it; the
  vehicle moves the player. A port that adds a case there is inventing one.
* **The park is a global.** `sub_457040` does not use `[102]`, which every
  other park in this engine does. Copying the dialogue-mode park would be
  wrong.
* **`sub_49BC60` is shared.** The same callback serves the inventory rows, the
  slider rows and the memory rows and dispatches on `dword_670CB8`
  (`UiWalk::rowKind`) — this already cost one bug (`ba1c335`), so any change
  to the slider arm must leave the other two alone.
* **Three of the four cities' circuits carry vehicle lanes** and Qalisar's
  slider mask is 1. Whatever is implemented has to be tried somewhere other
  than Anekbah before it is called done.

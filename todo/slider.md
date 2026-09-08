# The SLIDER — call one from the sneak, ride it, drive it

`todo/next-tasks.md` item 16, **L**, good evidence. The road traffic is already
ported (`docs/STREET_LIFE.md` §2b — the sliders and motos on the `.OPT`
circuit's vehicle lanes, spawned and driven by the walkers' own mover) and the
PLAYER's half is not: `ACTOR_STATE` 7 and 8 are the mount and the ride, and
`todo/standing-unknowns.md` §5 records the decision not to port it, with the
size measured rather than guessed.

Three separable things, and the numbering below is the order they are worth
doing in, not the order the game does them.

## THE WHOLE FLOW, corrected 2026-09-08

A reader described what the original actually does, and every part of it is in
the code — including the part this plan had backwards. The sequence:

> call a slider → a cutscene of it on its road (**not every time**) → it stops
> near you, always **on the road** → press ENTER close enough and **on the
> right side** → a small animation of opening the door, getting in, closing it
> → another potential cutscene near the destination → it stops, the character
> gets out, the slider drives away.

| what the player sees | what it is |
|---|---|
| the cutscene of it on its road | `sub_456530` state **2** requesting **camera mode 8**, whose two subjects are the SLIDER and not the player — and it is *"not every time"* because the request is guarded `if (sub_413360(C) != 8)`, so it is skipped when the camera is already there |
| it stops **on the road**, near you | the **117-unit** arrival test, measured against the nearest **lane point** and not against the player. That is exactly why it stops on the road and you have to walk to it |
| press ENTER close enough | `MDSLIDIN` (`tab_special_move[12]`), gated on ACTOR_STATE **6**, an active slider, and its mode **3** — its three refusals are its own debug strings |
| the door animation | the `.CTL`'s own clips: **`A_SliderIn`** and **`A_SliderOut`**, with **`H_Slider`** the riding state, in `H1Avnt.CTL` and `F1Avnt.CTL` beside the two move names |
| the cutscene near the destination | state **6**'s arrival: **camera mode 10**, framed between `sub_40E630(dword_6A17CC)` — the remembered destination's own address record — and the vehicle |
| he gets out, it drives away | `MDSLIDOU` (8 → 1), `sub_4570F0` (camera **17**), then state **7**, which releases the slider once he is **300** units clear AND in front of it |

### ...and the SNEAK'S PAGE NEVER TRAVELS. It CALLS.

This is the correction, and it is one field. `MDSLIDIN`'s last act is
`UI_OpenScreen(7, -1, -1, -1)` — *"cant find slider interface !"* if it fails —
so **boarding opens SCREEN 7**, which is the same slider page seen from inside
the vehicle. `UI_LoadScreen` writes the slot's `+4` from the screen record's
own `param` whenever that is not −1:

    screen 7  SLIDER   param = 1
    screen 9  SNEAK    param = 0

and `sub_49BC60`'s kind-4 arm is `if (slot[+4] == 1) … else …`:

* **from the SNEAK (screen 9, param 0)** the point is the PLAYER'S own
  position — it **calls one to where he stands**, and remembers which row he
  picked in `dword_6A17CC`;
* **from inside the slider (screen 7, param 1)** the point is
  `sub_40E630(tag)` — which loads the destination's AREA and returns its
  address — and that is the journey.

So the two modes are not two buttons on one page: they are **the same page
opened by two different screens**, and the panel appears twice in the widget
lift for exactly that reason (which is also why `UiWidgets::at` had to learn to
prefer the record carrying a `current`).

**What this port does today is wrong at the root**: the sneak's page teleports
on a destination row. It should call a slider; the journey belongs to screen 7,
after boarding. The reader met it as *"calling a slider with the sneak
teleports me"*.

### The header's three buttons, and the hook — read 2026-09-08 from the reader's memory

The reader: *"if you called it by selecting a destination, it transports you
directly; if you called it by 'Appel du slider', the slider menu opens - and I
think there is an extra button, which lets you drive it manually."* All of it
is in the widget table. List 0x004DEA08 has three items, and which show is
`sub_49D170`'s two-state arm on the live slot's `+4`:

| item | string | callback | what it does |
|---|---|---|---|
| 0x004DE920 | 12 *Appel du slider* | **0x0049D400** | `sub_452570(player's position)`, close the screen. `dword_6A17CC` is NOT touched: a call with no destination remembered |
| 0x004DE968 | 13 *Automatique* | **0x0049D480** | five instructions - `panel+24 = 2`, the destination rows get the focus |
| 0x004DE9B0 | 14 *Manuelle* | **0x0049D4A0** | `sub_457040(slider, player)`: save the prior ACTOR_STATE, write **7**, slider mode 3, and `Slider_TickRide` owns the body - **drive it yourself**. The extra button the reader remembered |

From the sneak (screen 9, param 0) the bar shows *Appel du slider*; from inside
the vehicle (screen 7, param 1) it shows *Automatique* beside *Manuelle*. The
port had recorded that second state as "reachable only through a message code
the port does not deliver" - the message is the screen.

**The page's own hook, 0x0049D4D0** (both records of panel 0x004DEDE8 name
it), on the frame screen 7 opens:

```
if (dword_6A17CC != -1) {           ; a destination was remembered
    point = sub_40E630(tag)         ; ...resolve it (and load its area)
    if (sub_452570(&point)) screen[+8] = 3   ; state 6, the journey, close
    else text 42
} else {
    ordinary navigation: header <-> rows on the input word
}
```

So *called by a destination → transported directly* is the hook firing the
journey before the menu is ever used, and *called by the header → the menu
opens* is `dword_6A17CC == -1` leaving the page up for Automatique/Manuelle.
Both arms of `sub_49BC60` write `dword_6A17CC`; its only reset is in the
new-game path (`sub_49B400`, beside `F1AVNT.CTL` and `UI_LoadScreen(35)`), so
the engine remembers the last destination across calls.

**Still not found**: the *correct side* test. `MDSLIDIN` itself has no side
check in it, so the constraint is somewhere else — the `.CTL` entry's own
conditions, or a proximity test at the action button. Recorded as open rather
than invented.

## What the original does — read 2026-09-07, before any code

### The sneak's slider page, and where a destination's COORDINATES live

`sub_49BC60` is the sneak's row confirm, and its kind-4 arm (`dword_670CB8 == 4`,
the slider page) is:

```
tag = item[+0x3C]                        ; the ROW TAG, not the selection
if (screen[+4] == 1) {
    rec   = sub_40E630(tag)              ; resolve - AND TRANSPORT
    point = (float)rec[0], rec[4], rec[8]
} else {
    point = player[+0xF4], [+0xF8], [+0xFC]     ; call one to WHERE I AM
}
if (sub_452570(&point)) { screen[+8] = 3; dword_6A17CC = tag; }
else                    { text 42, "no" }
```

so the page has **two modes** — travel to the chosen destination, or call a
slider to the player's own position — and the switch is the screen record's
`+4`, which is the same word `sub_49D170`'s two-state header reads
(`docs/UI.md` §3b: "Appel du slider" against the two-label state).

**`sub_40E630(row)` is not a lookup, it is the transport.** It

1. walks GLOBAL `+16`'s **36-byte** records (`v3 += 18` int16s), counting only
   those whose `+0` bit is set in the game DB's `+24` array, until it has
   skipped `row` of them — so the argument is the ENABLED index, which is
   exactly the row tag the binder wrote;
2. compares the record's **`+2`, the AREA**, against the resident one and, if
   they differ, frees both slots' contexts, `Area_Load`s the new area,
   re-attaches the player, rebinds his facing matrix and raises event 9;
3. then walks the now-resident chunk's ADDRESS table (`AREA +60`, count `+82`,
   16-byte records) for the entry whose **`+14` equals the record's `+0`**, and
   returns it. Its `+0/+4/+8` are the int32 coordinates.

**So a destination's position is not in GLOBAL at all — it is an ADDRESS in the
destination's own area, keyed by the same bit that enables it.** Measured over
the shipped data: **39 of 39 destinations resolve**, across 4 areas (0, 1, 64
and 101, holding 34, 34, 7 and 3 addresses). The port already reads that table
(`engine/src/formats/addresses.h`, 791/791 against `ADDRESSES.TAG`) — what it
was missing is the `+2` area field, since `globalDestinations` lifted only the
bit and the name.

`sub_40E8E0()` is the count of enabled destinations, which the port already
computes its own way.

### `sub_4570F0` — how a ride ENDS

The brake (`0x20` under 10 units of speed) reaches it, and it is the mirror of
the mount: probe the ground and drop the slider onto it, restore the player's
ACTOR_STATE from `dword_53999C` and the slider's mode from `dword_539970`
(both saved on the way in), set `dword_539988` — the flag `Slider_TickRide`
gates its three helpers on — place the player at the seat height
(`y − 33.149605`), then `sub_4521E0(slider)`, `sub_468FA0(player)`, the
slider's mode to **7**, and

    Camera_Request(17, ...)   with the PLAYER as both subjects,
                              dword_930818 = 56.0, dword_93081C = 1

so a ride hands back at **camera mode 17**, not mode 0 — which is exactly why
`sub_452570`'s arrive arm guards its own `Camera_Request(0, ...)` on the mode
not already being 17.

### `sub_452570` — arm the ride, or arrive

One function, two arms, and which one runs is whether a slider POOL exists
(`dword_8F5E3C`, the `.OPT` circuit's 40 ride slots):

* **arm**: search the circuit's lanes for the point nearest `a1`
  (`sub_452A80` per lane point, best kept in `dword_8F5E8C`), record the block
  and point index, take a free slot out of the 40 (`slot[11] == 1` and the
  actor's `+180 & 8`), reserve it with `sub_452CC0`, set its state to 2 (or 6
  when one was already assigned), then **`Screen_Fade(1)` and
  `Actor_HoldAnimation(player, 1)`** — the fade and the hold are the whole of
  what the player sees while the slider comes.
* **arrive**: clear the assignment, restore camera mode 0, `Screen_Fade(0)`,
  release the hold, and **place the player at `a1`** — `+232/236/240` and
  `+244/248/252`, the node position, velocities zeroed, the facing matrix
  rebuilt from the actor's own Euler at `+416/420/424` (so the address's
  heading is NOT used by this path), `Walk_ProbeGround`, `ACTOR_STATE` 1.

### How a ride is ENTERED, from the binary's own debug strings

`MDSLIDIN` (`tab_special_move[12]`, 0x0046B7F0) names its three refusals:

```
player[+404] != 6        -> "bad mode getting in slider !"
sub_438240() == 0        -> "no active slider !"
sub_438410(slider) != 3  -> "slider is not in open mode !"
else: sub_438420(slider, 4); player[+404] = 7; node flag 8;
      sub_41DF30(7, -1, -1, -1) or "cant find slider interface !"
```

and `MDSLIDOU` (0x0046B890) refuses unless `player[+404]` is **8**, leaving to
1. So the gate is **ACTOR_STATE 6 plus a slider standing OPEN (mode 3)** —
CLAUDE.md §4's "7 and 8 are the mount and the ride of one slider" names the two
ride states and says nothing about the way in, which is 6. 7 is what
`Slider_TickRide` binds the body in; 8 is what `MDSLIDOU` will leave from.

### `Slider_TickRide` (0x00458150) — the ride

82 lines, and the three helpers under it are the machinery: `sub_4573E0`
(**387 lines**, the flight model), `sub_458600` (75) and `sub_457F50` (66).
What the 82 say:

* `flt_4C30D8 *= 0.5` at the top and restored at the end — **everything ticked
  inside the ride advances at half a frame per frame**;
* the MOUNT is `player[+404] == 7` (`ACTOR_STATE` 7): bind the player's node
  under the slider's, `sub_438420(slider, 3)`, and
  `sub_437140(playerNode, sub_438450(slider))` — the same field
  `Anim_RootDelta` turns a clip's root motion by, so a mounted player's root
  motion is rotated into the vehicle's frame by machinery the port already has;
* then `Camera_Request(8, ...)` with **both subjects the slider**, not the
  player;
* after the three helpers, the player is dropped onto the surface under him
  (`World_ProbePoint`, `+248 += hit - 1`), his yaw is taken from
  `-dword_8F5DD8` (less 180 when `dword_8F5DBC < 0`), and `Actor_ScanZones`
  runs — so **riding still triggers zones**.

### `sub_4573E0` — the flight model (387 lines, transcribed)

`dword_8F5DE0` is the frame's input word, copied from `dword_4E9718`, and its
low four bits are the same four the interface uses.

* **`0x20`** with `|speed| < 10` zeroes the speed and calls `sub_4570F0` — the
  only way out of the model that is not a dismount.
* **`0x1` / `0x2` STEER** at ±5 degrees a frame, negated when the slider is
  reversing so the stick still turns the nose the way it points. While
  steering, the bank ramps at **1.75 degrees a frame** to a hard **11**
  (349 being −11 in the wrapped angle); released, it unwinds at the same rate.
* **`0x4` / `0x8` THRUST**, from a six-value ladder that is 0.615, three times
  it and six times it, chosen by the SIGN OF THE SPEED: forward, up is
  **+1.845** and down **−3.690**; reversing, they mirror. Gated on a
  frame counter (`dword_8F5E08 < 0x50`).
* **The drag is quadratic** — `speed² × 0.1538 × 0.0078125` off the thrust —
  and a dead band snaps a coasting slider to a stop.
* **A SKID test** takes the angle between the nose and the velocity: inside
  **37 degrees** it grips and the simple arm runs (one speed along the nose);
  past that and inside 170 it is sliding, and either the velocity is snapped
  back toward the nose (over 16 units of speed) or the hard arm runs, where the
  two components take their own quadratic drag and the yaw is pulled by their
  cross product.
* **The position integrates with a MINUS**: `x -= vx·dt`, `z -= vz·dt`.
* **The pitch** is `asin` of the difference between two ground probes a
  **metre** fore and aft — 39.370079 units is 1.00 m at the world's 39.3701 to
  the metre, and the 0.0127 it multiplies by is 1/78.74, the reciprocal of that
  2 m span.

`sub_458600`'s hover has **two arms**, and which runs is a comparison the
decompilation lost (`v7`, "possibly undefined" at 458775). The listing keeps
it — `fld dword_8F5DBC / fcomp flt_4BC5E0 / test ah, 40h`, and `flt_4BC5E0` is
**0.0** — so a **stationary** slider bobs (8.43 degrees a frame, one cycle in
42.7 frames, amplitude a sixteenth of the 30.75 hover height) and a **moving**
one eases toward its height at a third of the gap a frame. A surface whose mesh
name begins `OP` damps the ride by a quarter a frame.

## The steps

Each ends in a commit and a report.

1. **The TRANSPORT** — the destination record's `+2`, the address lookup, the
   area change and the placement, wired to the slider page's row confirm.
   Checkable headlessly and needs no ride at all, because with no slider pool
   the engine's own arrive arm is a teleport. — **DONE 2026-09-07**, not yet
   confirmed in play. `verify.py: slider addresses` (the join, 39 of 39) and
   `engine: slider travel` (the walk: TAB, RIGHT, UP, confirm, DOWN, confirm,
   and the player lands on the destination's address).

   Two things fell out of it. The slider page comes up on its two-state
   HEADER, not on the rows — `panel+24` is 1 — so a player moves DOWN to the
   destinations, which is only true because `UiWidgets::at` now prefers the
   record carrying a `current`. And the page's OTHER mode is real: when the
   screen record's `+4` is not 1 the point is the PLAYER'S OWN position, not a
   destination's — "call one to where I am" — which is recorded in
   `UiListState::travelToDestination` rather than invented, since nothing in
   the port reaches it yet.
2. **The FLIGHT MODEL** — `sub_4573E0` and `sub_458600` transcribed into
   `engine/src/actor/slider.*` and RUN: the six-value thrust ladder, the
   5-degrees-a-frame steer, the 11-degree bank, the quadratic drag, the skid
   test and its recovery arm, the two-probe pitch, and the hover with both of
   its arms. — **DONE 2026-09-07**. `verify.py: engine: slider fly`, shown to
   fail by bobbing always.
3. **The RIDE, FLOWN** — the model wired into `omk-play` behind `--ride`,
   which mounts it where the player stands: the halved delta, the same input
   word the walker takes, `sub_457F50`'s placement of the rider, camera mode 8
   resolved against the SLIDER, and `sub_4570F0`'s stop handing back at camera
   mode 17. The walker does not tick beside it, because ACTOR_STATE 7 and 8
   have `walks` false and the ride writes the body outright — ticking both
   made them fight and the walker won. — **DONE 2026-09-07**, and it is
   watchable: `--ride` in Anekbah flies. `verify.py: engine: slider ride`,
   shown to fail by ticking the walker as well.

   **What it is not**: `--ride` is a harness and its own log line says so. The
   engine's way in is `MDSLIDIN`, which wants ACTOR_STATE 6 and a slider
   standing OPEN in mode 3, and there is no vehicle under him — he flies
   standing up.

4. **WHERE a called slider comes to** — `sub_452570`'s lane search and
   `sub_452A80`, and the round-robin route off the chosen lane. — **DONE
   2026-09-07**, `verify.py: engine: slider call`, shown to fail by searching
   the pedestrian lanes as well (which gives a nearer and entirely plausible
   6.0 m median, because a pavement is closer than a road).

   The search itself is short — the point-to-segment distance behind a
   3900-unit box reject, over the lanes AFTER the pedestrian range — and what
   it produced is a statement about the AUTHORING the data could have refused:
   **every destination the game offers in a city is within 46 m of a road** in
   Anekbah and within 13 m in Qchaud. Two rows are worth keeping:

   * **LAHOREY has no vehicle lanes at all** (`pedEnd == laneCount == 162`),
     so none of its 7 addresses finds one and a call there can only fail —
     `sub_452570` returns 0 and the page shows text 42. Which squares with
     step 1: with no pool the ARRIVE arm runs instead and the transport is a
     teleport, which is what the port does there.
   * **Two of the Souk's 34 addresses find nothing**, which is the box reject
     doing its job: they are more than 99 m from any road on at least one
     axis.

5. **What `sub_452CC0` DECIDES** — where the chosen slider is put, and how
   fast. — **DONE 2026-09-07**, `verify.py: engine: slider call` extended and
   shown to fail by dropping the hover.

   The mover goes to the chosen lane's **ORIGIN**, set back **39 units** along
   that lane's own direction in x and z (the y is the origin's, untouched),
   with the node **30.75** under it — `SliderRide::kHover`, the ride's own
   hover height, turning up for the third time and out of a different
   function. `mover+56 = 256.0`, `mover+52 = 0`, `mover+186 = 1` (the priority
   the swap test compares against) and the node's flag word takes `| 8`. So a
   called slider does not appear beside you: it starts at the top of the road
   and drives down it.

   The **set-back is a flat 39**, not the 39.370079 that is a metre elsewhere
   in the same file. It looks like a typo and is not — worth keeping written
   down for exactly that reason.

   **What is deliberately NOT transcribed** is the rest of that function: the
   mover is unlinked from whatever lane list it is on and relinked onto the
   chosen one, through the engine's own linked lists, and this port's traffic
   keeps its own occupancy structures (`docs/STREET_LIFE.md` §2b). Its most
   interesting arm is recorded anyway: **if another vehicle is already on the
   target lane** with a priority no higher than the call's (`+186 <= a1[4]`),
   the two are **swapped outright**, node blocks and all — the vehicle that
   was in the way BECOMES the player's slider. Cheap, and not what a reader
   would guess.

6. **THE RIDE STATE MACHINE** — `sub_456530`'s switch on the slot's `+8`,
   ported and driven. — **DONE 2026-09-07**, `verify.py: engine: slider call`
   extended and shown to fail by doubling the arrival radius.

   Every arm of that switch sets `flt_536C28 = 90.0`, the field of view a ride
   is watched at against the 75 of every other camera, and the states are:

   | state | what |
   |---|---|
   | 0 | ambient traffic — not in the switch; the default arm is the ordinary drive |
   | 1 | IDLE, a **600**-frame countdown; a slider you called and did not board gives up exactly then |
   | 2 | COMING — camera 8 on the SLIDER, and within **117 units** (2.97 m) of the pickup point the camera hands back to the player at mode 0, `Screen_Fade(0)` fades in and the hold is released |
   | 3 | OPEN — what `MDSLIDIN` demands ("slider is not in open mode !") |
   | 4, 5 | aboard; `Slider_TickRide` owns the body |
   | 6 | FETCHING — the same arrival test, ending at state **4** and camera **mode 10**, framed between the destination's own address record and the vehicle |
   | 7 | LEAVING — it drives off once the player is **300** units clear AND in front of it |

   So a ride passes through four camera modes and none of them is guessed:
   **8** while it comes, **0** when it arrives, **10** when it leaves with
   you, and **17** when you get off.

### `MDACTION`'s SLIDER ARM — how you get in, read 2026-09-08

This is the piece three of the "what is left" entries below were all really
asking for, and it is one arm of one handler. **Boarding does not begin at
`MDSLIDIN`.** It begins at `MDACTION` (0x0046AEC0), the action button's own
`tab_special_move[3]` handler, whose *first* branch — `loc_46AFF8`, before the
world-object take everything else in this port uses it for — is the slider.
`MDSLIDIN` is at the far END of it.

The arm, in order, with the constants as the listing has them:

| step | the code | what it is |
|---|---|---|
| the slider | `edi = sub_438240()` — `dword_8F5E44` | the active slider; nothing → the ordinary take |
| not shooting | `[esi+194h] == 3` refuses | ACTOR_STATE 3 |
| a flag | `sub_438290(slider)` = node+180 & 4 | must be set |
| its position | `sub_438310(slider, v)` = node +36/+40/+44 | |
| the vector | `d = slider − actor`, and **`d.y = (sliderY + 33.149605) − actorY`** | `flt_4BC91C` is −33.149605 and is *subtracted*, so the seat drop is folded in before the test |
| its matrix | `sub_438450(slider)` = node+140 | |
| **the side** | `R = Matrix3x3_RotateVector(1,0,0, M)` — the matrix's row 0, the local **+X** — then `dot(d, R) < 0` **refuses** | THE CORRECT SIDE. `d` points from the man to the slider, so the man must stand on the slider's **−X** side |
| **the reach** | `|d| >= 157.48032` refuses | **4.000 m** exactly, at 0.0254 m to the unit |
| open it | `if (mode == 7) sub_438200(slider, 0); sub_438420(slider, 3)` | the arm **sets** mode 3; it does not require it |
| the group | `ebx = Cef_FindGroupById(actor+0B4h, 60)` | **group 60**, `H_SLDIN` |
| **the snap** | `off = root0(Cef_DefaultClip(g60)) − root0(dword_90EF28)`, rotated by M; `actor = slider + off`, `y −= 33.149605` | `root0` is `sub_471100` (the first position key of the first track with any) and `dword_90EF28` is `ANIMS\slf_112.3da`, which `Game_Init` loads for exactly this (05_sys.c 1842) |
| the move | the delta written to +0E8..+0F0, `o3de_MoveNodeBy`, `+104h = FLT_MAX` | the fall clock reset |
| **the root frame** | `sub_437140(actor+8, M)` | node+156 becomes the **SLIDER's** matrix, so `H_SLDIN`'s authored step runs in the vehicle's frame however the man is turned. This is the `Anim_RootDelta` 3x3 CLAUDE.md §6 lists — a second confirmed writer of it |
| the model | `sub_4521E0(slider)` | swaps `dword_538E30` ↔ `dword_538E2C` — the open-door mesh against the closed one. **Not ported**; the two globals are unidentified |
| the state | `[esi+194h] = 6` | ACTOR_STATE 6 |
| the clip | `SetPersoBankGroup(actor[18Ch], g60)` | the door opens, he steps in, the door shuts |
| the camera | `Camera_Request(9, {slider, slider, 60.0f, 1, …, −1})` | **preset 9**, 60 frames, on the slider |

Then the **channel** finishes it. `H1Avnt.CTL` group 60 holds `[158] H_SLDIN`
(clip 53, 72 frames), `[159] H_SLIDER` (clip 54, the riding pose) and `[160]`,
a child of `H_SLDIN` with **no input at all**, flags 0x13, move `MDSLIDIN`,
GoTo `H_SLIDER`. So when the door clip ends the channel takes [160] by itself,
fires `MDSLIDIN` (0x0046B7F0 — `sub_438420(slider, 4)`, ACTOR_STATE 7,
`UI_OpenScreen(7)`) and settles on the riding pose. Group 61 is the mirror on
the way out: `[161] H_SLDOUT` (clip 55, 51 frames), `[162]` `MDSLIDOU`,
`[163]` GoTo `H_STAND`.

**Three independent things agree on which side the door is**, which is the
self-check this needed and is why it can be believed without a screenshot:

* the gate wants the man on the slider's **−X**;
* the placement offset is `(−62.503, −8.782, +3.697)` — 1.59 m along **−X**,
  22 cm and 9 cm — measured by `engine/tools/slider_door` off the shipped
  clips (`slf_112.3da` root0 `(−538.195, −162.587, 7.884)`, `H_SLDIN` root0
  `(−600.698, −171.369, 11.581)`);
* camera preset 9's eye is `(157.4803, 59.0551, 0)` and the engine SUBTRACTS a
  preset's eye offset, so the camera stands 4.00 m along **−X** and 1.50 m up
  — over the man's shoulder, watching him get in.

Two round metres (4.00 and 1.50) and the reach matching the camera distance
exactly are the kind of agreement a wrong unit or a flipped sign does not
produce.

**One thing this arm does NOT do**, and it matters for the port: it never
writes the actor's euler. He keeps whatever way he was facing and the clip
turns him. It is also where the port's own yaw conventions had to be kept
apart — `Sliders::calledYaw` returns `atan2(dir.x, dir.z)` (forward
`(sin t, 0, cos t)`) while `PlayerController`'s euler runs the other way
(`rotateYaw` makes forward `(−sin y, 0, cos y)`), so the two are **mirror
conventions** except along ±Z. The door geometry is therefore carried as the
matrix's ROWS end to end (`calledFrame`, `setRootFrame`) and never as an
angle. Whether the SEATED placement's `rideAt(seat, calledYaw())` should be
negated is a separate question this did not touch, and is now listed below.

### Played, and six things were wrong — 2026-09-08, the same day

The reader played the boarding and reported, in order: *"I had to go to the
front of the slider to enter it"*, *"the animation didn't work"*, *"I was
teleported instead of just leaving the slider where it arrives"*, then *"the
door has still no animation"* (with a screenshot of the original: a gull-wing
door swung up, Kay'l beside it on the road), *"kay'l animation is played with
the wrong transform"*, and *"the slider doesn't move once I leave it, it
blocks all the vehicles on the road"* — with the standing instruction **"look
carefully at the code, there are many approximations right now"**. Every one
of them was a real fault, and only one was the sign question the previous
section had flagged. Recorded here with the cause, because each looked like a
different bug and several were the same kind of mistake.

| the report | the cause | the fix |
|---|---|---|
| had to go to the **front** | `calledAt` returned the mover's `pos`, the lane **carrot**, which `spawnVehicle` puts `kCarrotBehind = 117` units (2.97 m) AHEAD of the vehicle. `sub_438310` reads the NODE's +36/+40/+44 — the drawn body. So 117 of `MDACTION`'s 157.48 reach was spent before he took a step and the only ground satisfying it was off the nose | `calledAt` → `m.body` |
| ...and the sign, before that | `calledFrame` derived the door axis from `Matrix3x3_FromEulerAngles`; the vehicle's node matrix is `sub_4427D0(mover+24, 0, mover+32)` **transposed** by `sub_4423C0` (18_d3d.c 3938). On flat ground node+140 row 0 = `(−f.z, 0, +f.x)`, row 2 = `(−f.x, 0, −f.z)` — the exact negative of both derived rows. Gate, snap and camera 9 all read row 0, so all three moved to the far flank TOGETHER and every self-consistency check passed | rows transcribed from the builder |
| Kay'l **floats** at the vehicle's waistline (the "wrong transform", half of it) | the engine writes +244..+252, his ORIGIN = the **pelvis**; `PlayerController`'s position is the walker's, at the **feet**. The pelvis-space y went straight to `rideAt`, one pelvis-height (41.9) too high. **Found by RENDERING the boarding beside the screenshot**, not by reading — the listing cannot say which convention a class uses | `+ cameraLift()` on both placements |
| ...the other half | `sub_437140(node, M_slider)` installs the slider's matrix at node+156, and +156 is read as an ORIENTATION (21_d3d.c 2854); the port used it only for the root delta and drew him on his walk-up facing. The first fix wrote the yaw into `Session::setPlayerPosition` — the logical record — and moved NOTHING on screen; the model is posed from `player->facing()` at the draw | the draw's yaw, states 6/8 |
| **teleported** at the destination | `sub_4570F0` writes only the actor's y and copies +244/+252 through UNCHANGED, then `sub_468FA0` places him from group 61's clip against `slf_113.3da` (`dword_9103D8`, NOT the entry's 112) and plays `H_SLDOUT`. `placeActorAt(address)` had no basis in the code | out where it stopped, `H_SLDOUT`, `MDSLIDOU` → `H_STAND` |
| **no door animation** | not a model swap. `SLF_112.3DA` is **72 frames** = `H_SLDIN`, `SLF_113.3DA` **51** = `H_SLDOUT`: the slider's OWN clips, played on its sub-node by `Cef_TickChannel`'s ACTOR_STATE switch, cases 6 and 8 (19_dsound.c 4145-4200), on the SAME clock as the character's. Of five tracks one moves — **`SlPorteG`, 71.4°**, the gull-wing swing. `build/slider_doorclip` | the called vehicle composed from the clip at `poseFrame()` |
| **blocks the road** after he gets out | THREE causes in a row: (1) `RideMachine` went to 0 but `Vehicle::state` stayed 7 and `vehicleDrive` skips `state != 0`; (2) once written, the hand-back was undone one line later by the "stopped where it arrived" arm (`was != state`, 0 is neither 2 nor 6 → back to OPEN); (3) `callRide_.tick(dt, d, 0.0f, false)` — `toPlayer` and `ahead` were HARD-CODED, so `case 7`'s `> 300 && ahead` could never be true | tested first; `sub_456530` case 7's own test `dx·sin y − dz·cos y > 0`, fed by `setRider` |
| ends **beside** the slider, not in it | the door snap is right and so is the rotated root delta (traced: per-tick direction `(−0.76, +1.10)` is the seat's) — but he moved `(+44.8, −4.7)` net, 25 units of it in the first five frames. **The crowd push**: `MDACTION` snaps him INSIDE the vehicle's body sphere and `Actor_TickNpc`'s push shoved him out every frame faster than the clip walked him in. ACTOR_STATE 6 and 8 never run `Actor_TickNpc` | no push while boarding or leaving |

**And the side — settled by the door's NAME, after two wrong readings.**
The door clip's five tracks carry a mesh index at `+0` (`0,3,1,4,2`) and,
what the port's `.3DA` reader had never kept, **the bone's name at `+4`**:
`SlBassin`, `SlPorteZD`, `SlPorteD`, `SlPorteZG`, `SlPorteG`. The one that
moves is **`SlPorteZG`** — the parent copy of the door on the G side, the
file's −X. Read by index it landed on `SlPorteG` alone (the child), so the
parent copy stayed shut over the hole — the *"face of the slider hiding
him"* — and read by id it landed on `SlPorteD`, the far door, which made the
man look mirrored and cost an hour with a mirror in `calledFrame` that is
gone again. Bound by name, the parent swings and its coincident child
follows, the well opens, and the man is on the side these rows always gave:
`-row0`, the file's −X. `clipTracks` now keeps the names. Each door is two
coincident meshes (72 corners each, the pivot at the hinge); every track is
identity again at the last frame — the door opens, he steps in, it shuts.

**Played again (the pushed `94aa317`), three more — 2026-09-08.** The
reader: *"kay'l is really entering the slider, he is just a bit too low"*;
*"when the slider arrives and kay'l gets off, it looks like two sliders are
showing at the same place and at the same time (part of the model
flickering)"*; *"transportation from anekbah to qalisar is broken (same
effect as the teleportation from earlier, with the character disappearing)"*.

* **too low**: the clip's root y (+12.5, the step down into the seat) was
  applied TWICE — once to his position by the channel-only tick
  (`Actor_MoveBy`), and again by the viewer's `rootDrop`, the drawn drop it
  accumulates from the pose's root translation for the take. Zeroed while
  he boards or leaves;
* **two sliders**: the journey and the arrive arm put the called vehicle on
  the destination lane's set-back point — where AMBIENT vehicles spawn — on
  top of whatever already stood there, and the two bodies' coincident faces
  flickered. `sub_452CC0` never adds a vehicle: it TAKES OVER the occupant
  ("the one in the way becomes the player's slider"). `Sliders::takeOverAt`:
  ambient vehicles on that lane within 400 of the place die first;
* **Qalisar**: its slider mask is row 0 alone, so every ambient vehicle is a
  moto and `spawnVehicle`'s ambient coin could not produce a slider for the
  call — `arriveAt` failed and the old bare placement ran, dropping
  `playerReady` on a fresh load. `sub_452CC0` binds `dword_538E28` ROW 0's
  model into the reserved slot whatever the city: a call now spawns row 0,
  a slider, regardless of the mask (`forCall_`). The sneak page also lists
  its enabled rows now (name, area, address bit) — row 2 in this save is
  `Qalisar - Sas vers Anekbah`, area 101. `engine: slider journey qalisar`
  (--slow) replays it from aboard.
  **And it was still broken when played**, for a viewer reason the headless
  log could not see: the other-area arm dropped `playerReady` after the load
  while the controller was alive, the hand-over gate that sets it back runs
  only on `!player`, and the model eviction keeps Kay'l's model only while
  `playerReady` — in a city where no staged actor wears `HO1_FN` he was
  thrown away on the first frame. He was ticking and walking in the log and
  invisible on screen. The arm now keeps a live controller across the load
  (the walk-through path's own rule: "he keeps walking"), the viewer says so
  when it ever evicts the player's model, and the Qalisar check asserts it
  never does. Open beside it: whether an existing controller's collision
  soup follows the new set — the walk-through path lives with the same
  question.
  **And a third layer under it**: with him drawn again the SLIDER was still
  missing at Qalisar's kerb. Qalisar's pool is forty motos and full, so the
  call's spawn found no slot and the fallback relinked an ambient vehicle -
  a moto - as "the slider"; the viewer's new line said it: *"the called
  vehicle (slot 0, model 'moto') is NOT staged: its model did not load"*.
  `sub_452CC0` takes the occupant's slot AND REBINDS ITS MODEL to row 0
  (`sub_437F20(v7 + 20, dword_538E30)`) - the one in the way becomes the
  player's slider, body included - and the fallback does that now. Left
  open, and found on the way: Qalisar's ambient motos report *"its model did
  not load"* in this viewer (`charModelFor` resolves under `MESHES/PERSOS/`,
  the moto ships in `MESHES/MISC/`), so that city's traffic may be drawn as
  nothing at all - a street-life item, not a slider one.
  **Played (dd90c2b)**: Kay'l stays now; the reader saw no slider after the
  travel, and *"a fade effect that shouldn't be here"*. The fade was the old
  bare placement's sixty-frame `startColourFade(4, ...)`, still on the aboard
  path; the engine's exit calls `Screen_Fade(0)` = `fade.from_black` every
  tick of `H_SLDOUT`, which CLEARS the load's black and is not a dip. Gone
  from the aboard path. The slider's whereabouts after the arrival are now
  printed as numbers every 45 frames while he leaves.
  **And the numbers said where the slider went**: one frame after ARRIVED,
  *"the called vehicle is GONE at 0 0 0, ride state 0"* — RELEASED had
  fired at once. `case 7`'s test ("300 clear and in front") was fed the
  rider's position from the OLD city, nine kilometres away, for the one
  tick between `dismountCalled` and the first frame that re-read him; the
  engine's ride writes +244 before mode 7 is set. The exit placement now
  sets the release's rider itself.
  **And the slider that vanished at Qalisar's kerb, for real this time**:
  `Sliders::clear()` — what a load calls — rebuilt the whole pool but kept
  `called_` as the OLD city's slot number, so `callSlider` in the new city
  answered "one call at a time" and never spawned, relinked or rebound
  anything: the player's slider there was whatever Qalisar's pool had put in
  slot 0, a moto, and the log's *"relinked … with him aboard"* was printed
  over nothing. Found by the staging line — *"the called vehicle (slot 0,
  model 'moto') is NOT staged: its model did not load"* — in a run that two
  earlier runs of the same code had not shown, which is the kind of thing a
  player meets and a headless replay meets one time in three. `clear()`
  forgets the call now (`called_`, the ride machine, the rider).

**The seat, from the clips alone.** From the door (`−62.5, −8.8, +3.7`)
`H_SLDIN`'s root travels `(+43.5, +12.5, −0.9)` and ends at
`(−19.0, +3.7, +2.8)`; `H_SLDOUT` **starts** at `(−19.0, −8.8, +2.8)`. The
entry ends where the exit begins, to 0.1: a driver's seat 48 cm off the
centreline on the door side, stepped down into. `build/slider_door` prints
both travels.

**Also found, and deliberately left**: `Sliders::setPlayer` has NO CALLER in
the tree, so `playerKnown_` is always false and the class's player-aware arms
(the run-over latch, the on-road test) have never run. Waking it changes crowd
behaviour outside this task; the release takes its own `setRider`. And
`make -s build/omk-play` is a silent no-op (no rule, file exists) — the target
is `play`; two eight-minute runs were evidence about the previous binary.

**The origin residual, measured and CLOSED** (`build/slider_body`): every
one of `SLI_FN.3DO`'s four root sub-objects is centred on its own `pos` to
0.0 — four copies of one 83.7 x 60 x 162.5 body (2.1 x 1.5 x 4.1 m) laid out
at different places in the file. So bassin-relative IS body-centre-relative
and the man placed from the clips lands where the drawn body's seat is. The
19.7 in z was where the copies sit in the file, not an offset on the body.

**Camera 17 wired**: `sub_4570F0`'s last act, preset 17 on the player (eye
1.00 m behind and 2.00 m up) over 60 frames, through the take camera's blend
generalised to a preset per request; `MDSLIDOU` sends it back the way the
take's `MDPUTSNK` does.

**The model swap, read and PORTED** (the reader: *"the current model when
Kay'l enters the slider has no modelised interior"*). `dword_538E28` is the
slider model TABLE (88-byte rows); `sub_453A70` fills row `+4..+16` with the
model's four root sub-objects SORTED heaviest first by vertices + faces -
`SlBassin` 1527 corners, `slider_fl` 750, `SlBasA` 366, `SlBasB` 144 - and
`dword_538E2C`/`dword_538E30` are row 0's `+4`/`+8`. The reserved slider is
created on `+8`, `slider_fl`, a SHELL: no interior, and - `mesh_list`'s ids
say - **no door**: `SlPorteZG` (id 1, the hinge) is `SlBassin`'s child and
`SlPorteG` (id 2, the panel `SLF_112` turns) is the hinge's; the three shells
carry none. `sub_4521E0` toggles the sub-node to `+4`, the COCKPIT, at
`MDACTION`'s snap - before `H_SLDIN` - which is what puts a door under the
clip, and `sub_4570F0` toggles it back before the exit clip. Ported as the
staged root for the called vehicle while boarding or aboard; rendered, the
red well opens along the flank and he sits in it. `engine: slider door`
asserts the door parentage.

## What is left — 2026-09-08, evening: PLAYED AND CONFIRMED

The reader played the whole thing three times over the day, reporting each
round (the two "played" sections above), and the last word was *"ok, it
works"*. `next-tasks` 16 is DONE. What remains is below, and none of it is
needed to use the slider.

### The list as it stood before the play rounds

The reader, 2026-09-07: *"So, slider task is not finished if it is not usable,
don't you think?"* — and they were right. Since then the whole flow they
described has been built and run headlessly (`verify.py: engine: slider
journey`): a destination row on the sneak calls a slider, it comes on camera,
you board it, screen 7 opens and fires the journey, it drives you there, you
get out, it leaves. "Appel du slider" → the menu → *Manuelle* → the controls
are yours. **`next-tasks` 16 stays OPEN until a person has played it.**

What is still not there, so the labelling is not mistaken for done:

* ~~a journey to a destination in ANOTHER area~~ — **done 2026-09-08**, and
  the engine does not drive it either. `sub_40E630` loads the area, then
  `sub_452570` runs against the NEW pool: the lane nearest the destination
  (`sub_452A80`), a vehicle relinked THERE by `sub_452CC0` — at the lane
  point, not at the lane's top the way a call is — and state 6, which
  `sub_456530` case 6 finds within its 117 at once. So the arrival, the exit
  clip, camera 17 and the release all run in the new city; only the drive is
  skipped, under the load's fade. `Sliders::arriveAt` is that arm: the call's
  plan and slot,   `placeCalled` at the lane point, `mountCalled`, `sendCalledTo`.
  The port had dropped him at the address bare. **Run**: boarded in
  Jaunpur at address 34's kerb, the Anekbah row from aboard - loaded,
  relinked at the lane nearest address 0, out at 5445 -580, camera 17,
  `MDSLIDOU`, `RELEASED`. `engine: slider journey area` (--slow, and both
  journey checks board through the `--board` harness flag);
* ~~the correct SIDE to board from~~ — **done 2026-09-08**: it is
  `MDACTION`'s, not `MDSLIDIN`'s, and it is a dot product against the slider's
  local +X plus a 4.00 m reach. See the section above;
* ~~the door animation~~ — **done 2026-09-08**: group 60's `H_SLDIN`, 72
  frames, entered by `MDACTION` with the man snapped to the door and the root
  frame swapped to the slider's, and left by the channel's own no-input child
  `[160]`, which is what fires `MDSLIDIN`. What is still missing from it is
  `sub_4521E0`'s **model swap** — the open-door mesh against the closed one,
  two unidentified globals — so the door does not visibly open;
* ~~ACTOR_STATE 7 on the mount~~ — **done 2026-09-08**: `MDACTION` writes 6,
  the channel's `MDSLIDIN` takes it to 7, and the state table's `6 -> 7` row
  is exercised by that path rather than asserted on its own;
* **the SEATED facing** — `rideAt(seat, calledYaw())` uses the pool's yaw
  convention where the player's euler is the mirror of it, so the rider may
  be turned the wrong way except along ±Z. Found while reading the door
  geometry; not touched, because changing it without a picture is exactly the
  sign guess CLAUDE.md warns about;
* **the optional CUTSCENE** of the slider on its road — a longer thing than
  the camera cut, *not every time*; unfound. State 2's `+180 & 0x10` /
  `0x400` pair is the right shape and nothing read ties it to an editing;
* **the 600-frame idle** (state 1) is in the machine and not driven;
* the engine's **swap** of an occupying vehicle (`sub_452CC0`) — this port
  relinks an ambient one instead; same effect, one of the engine's two
  mechanisms.

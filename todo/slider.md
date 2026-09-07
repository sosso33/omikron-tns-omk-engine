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

## What is left — 2026-09-08, after the journey landed

The reader, 2026-09-07: *"So, slider task is not finished if it is not usable,
don't you think?"* — and they were right. Since then the whole flow they
described has been built and run headlessly (`verify.py: engine: slider
journey`): a destination row on the sneak calls a slider, it comes on camera,
you board it, screen 7 opens and fires the journey, it drives you there, you
get out, it leaves. "Appel du slider" → the menu → *Manuelle* → the controls
are yours. **`next-tasks` 16 stays OPEN until a person has played it.**

What is still not there, so the labelling is not mistaken for done:

* **a journey to a destination in ANOTHER area** loads the area and places
  him — the arrive arm — instead of driving. `sub_40E630` loads the area
  first and only then looks for a lane; the circuit changes under the
  vehicle, and driving across that is not ported;
* **the correct SIDE to board from** — not in `MDSLIDIN`; unfound;
* **the door animation** — `A_SliderIn` / `A_SliderOut` / `H_Slider` exist in
  `H1Avnt.CTL` and `F1Avnt.CTL` and nothing plays them; he keeps his walking
  pose aboard;
* **ACTOR_STATE 7 on the mount** — the engine reaches it from 6 and nothing
  in the port puts him in 6; the state table refuses `1 -> 7` correctly;
* **the optional CUTSCENE** of the slider on its road — a longer thing than
  the camera cut, *not every time*; unfound. State 2's `+180 & 0x10` /
  `0x400` pair is the right shape and nothing read ties it to an editing;
* **the 600-frame idle** (state 1) is in the machine and not driven;
* the engine's **swap** of an occupying vehicle (`sub_452CC0`) — this port
  relinks an ambient one instead; same effect, one of the engine's two
  mechanisms.

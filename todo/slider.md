# The SLIDER — call one from the sneak, ride it, drive it

`todo/next-tasks.md` item 16, **L**, good evidence. The road traffic is already
ported (`docs/STREET_LIFE.md` §2b — the sliders and motos on the `.OPT`
circuit's vehicle lanes, spawned and driven by the walkers' own mover) and the
PLAYER's half is not: `ACTOR_STATE` 7 and 8 are the mount and the ride, and
`todo/standing-unknowns.md` §5 records the decision not to port it, with the
size measured rather than guessed.

Three separable things, and the numbering below is the order they are worth
doing in, not the order the game does them.

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
3. **The MOUNT, in the world** — nothing calls the model yet. It needs
   `sub_452570`'s ARM arm (reserve a slider out of the 40, fade, hold the
   player), the slider arriving in mode 3, `MDSLIDIN`'s gate, camera mode 8
   with the slider as both subjects, `sub_457F50`'s placement of the rider and
   the node, and `Actor_ScanZones` under him. — open

## What is already there, and must not be re-done

* the `.OPT` circuit, its lanes, routes, reservation groups and action points;
* the 40-slot ride pool and the vehicles ON it, spawned by `sub_453B40` at
  `39 x h[4]` with no density factor and driven by the walkers' own mover with
  the VEHICLE thresholds 195/390 (`docs/STREET_LIFE.md` §2b);
* `readAddresses`, 791/791;
* the sneak's slider PAGE — the panel, the two-state header, the destination
  list filtered by the DB's `AddressEnabled` bits, and the row binder.

The three facts `verify.py: slider ride` pins (half speed, camera mode 8's
subjects, the node bind) come from the same read and are already asserted.

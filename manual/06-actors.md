# 6. Actors

← [Contents](README.md) · prev: [The world](05-the-world.md) · next: [Conversations and cutscenes](07-conversations-and-cutscenes.md)

---

## In short

Every character in the game — the one you steer and the ones you do not — is
driven by the same machine: a **state graph that shipped on the disc**.

A `.CTL` file is a list of states, each naming an animation, and a list of
transitions between them, each naming the buttons that take it. Press forward
and the graph moves from standing to walking, because an author drew that edge.
Press the action button next to an object and it walks a chain of four states —
reach, wait, put away, stand — for the same reason.

So "the player draws a gun" and "the player takes a step" are the same kind of
event, and the fight and shoot modes are not separate engines: they are
different **input context groups** feeding the same graph.

Around that sits a walker that decides whether a step is possible — how steep
is too steep, how high a ledge you can climb, how far you can fall — and, in
the cities, a crowd: pedestrians and vehicles following a circuit authored as
lanes and routes, keeping out of each other's way and out of yours.

## In detail

### The `.CTL` channel

Seven files, and all seven parse to the byte: the walk lands exactly on the
file size, 398 clips, and **every one of 2 044 graph edges resolves** — the
loader refuses to start otherwise, which is what makes that a test the data
could fail.

A state's entry carries flags, and every flag-gated block now has its traced
consumer:

| block | what it is |
|---|---|
| the combat block | damage, the hit window, the reaction keyed by the low 16 bits of an id, and knockback |
| turn and root-shift | each with an over-the-window mode and an on-transition mode — the two bits of `0x140` / `0x280` |
| the move name | resolves into the executable's own 66-row `tab_special_move[]` of engine callbacks; all 209 shipped sites resolve |
| bit `0x20` | this is the group's default entry — 202 of 202 |
| bit 2 | redirect through a GoTo rather than entering |

The `+28` sub-records are the states' **effect records**: bone-attached sprites
and frame-triggered sounds, footsteps among them. All 590 decode. Nothing in
the format is unread.

Transitions are matched against the current input bitfield, with cancel
windows, priorities and group-global edges. The port re-derives all **12 063
edges** from the file. Its standard is *data-constrained*, not
engine-verified, and the reason is worth stating: the trace rig in chapter 12
cannot reach this code at all, so no oracle exists for it.

### `ACTOR_STATE`

Eighteen states, 0..17, mapped **and run**. Two of them repay the reading:

* **14 is the water state** — two special moves write it.
* **7 and 8 are the mount and the ride of a slider**, and the dismount move
  refuses to leave anything but 8. State 7 has **no case at all** in the
  per-frame actor tick, which is the engine's shape rather than a gap in the
  reading.

### The walker

A step is refused for two reasons and only two: the face is steeper than **30°**,
or the ledge is higher than **30 cm** (`dword_910340`, 11.811023622 world
units). Falls are graded in tiers. The narrow phase sweeps a capsule built from
the model's own sphere list and stops **one unit short of the contact**, which
is why a body of radius 12 rests 13 from a wall; a penetrating contact is
pushed out along the clamped normal and re-swept.

The world unit is an **inch** — measured, not assumed — and the studio still
authored in metres, which is why the constants above look like round numbers
in one system and not the other.

### The two AIs

**Fight** is four profiles of button combinations injected into the player's own
input queue: the harder the profile, the shorter the wait between them, over an
input-bit union of `0xCFF`. It is the same channel the player drives, fed by a
table instead of a keyboard.

**Shoot** picks one of four callbacks by character *type*, from the binary's own
14-name type table — and it does have data: Gandhar plays three compiled
behaviour scripts (healthy, wounded, critical) through two 12-entry handler
tables. Over the 306 resolved sites the split is **302 generic, 3 Astaroth, 1
Gandhar, 0 X-Tech**, and **no character is type 7 at all**, so one of the four
callbacks is unreachable in the shipped game.

Neither is wired to a frontend, and the shoot AI's silence is a **decision**
rather than a gap — see chapter 13.

### The street

Three mechanisms, all ported and all drawn:

**The `.OPT` traffic circuit.** Seven blocks, six of six exact. Pedestrians are
spawned by `Slider_Init` at `39 × (5 − density) × h[3]` and walked by
`Sliders_Tick` over lanes and routes, with following, overtaking, reservation
groups and action points.

**The road traffic** rides the same circuit's vehicle lanes, behind two masks in
the AREA chunk that are non-zero in exactly the three areas that have such
lanes. Vehicles spawn at `39 × h[4]` with **no** density factor, capped by the
40-slot ride pool, driven by the walkers' own mover with vehicle thresholds. One
city's mask is the reserved row alone, so all 40 of its vehicles are motos.

They share a pool, because they share the **reservation groups**: 70 of one
city's groups are reached by both classes, and a vehicle waits on a walker
2 197 times in 1 800 frames. Counting them separately gives zero.

**The authored extras** are ordinary scene programs — 621 `scx.play.actor`
sites — plus the spatial index's push (spheres for actors, an ellipse for
walkers), the bump and talk messages, and the head look that turns an NPC's
head toward you.

<p align="center">
  <img src="images/anekbah-street.png" width="560" alt="Anekbah's street with its crowd">
  <br><em>The crowd, the traffic, the ambient fire and the set's baked lights,<br>drawn by the port in adventure mode.</em>
</p>

### A misnamed function, and why it is in this chapter

`tools/renames.json` is a map of hypotheses, and one of them had its sense
backwards. `Perso_SetInputEnabled` reads as an enable and is a **block**: flag
`0x80` makes the channel tick *skip* the input search, argument 1 sets it and
argument 0 clears it. The port trusted the name, asserted the flag on the way
out of every conversation, and the player's action button was dead from the
first conversation onward — a change that was right in outline made the game
worse.

The rule that follows is the chapter's, not an aside: **before a rename decides
a behaviour, read the handler rather than the map.** A boolean argument is
where this bites hardest, because a wrong name inverts it silently and both
values look plausible at the call site.

## Where it lives

| | |
|---|---|
| the findings | `docs/ASSETS.md` (the `.CTL` format), `docs/STREET_LIFE.md` |
| the port | `engine/src/actor/` — `channel.*` (the state machine), `walk.*`, `player.*`, `shoot.*`, `pedestrians.*`, `spatial.*`, `pose.*` |
| the tables | `tables/special_moves.json`, `tables/shoot_ai.json`, `tables/key_bindings.json` |
| the checks | `engine: actor states`, `engine player walk`, `engine: narrow phase`, `engine: pedestrians`, `engine: city crowd`, `crowd push`, `head look`, `opt tracks` |

## What is not settled

* **The `.CTL` runtime has no oracle and cannot have one from this rig.** The
  trace logger sees only what a VM handler narrates, and combat has two
  opcodes: one announces nothing, the other announces to a domain the logger
  filters. A capture *did* reach combat — 32 of its anchored scripts carry the
  fight opcode — so the silence is the mechanism, not the play.
* **Fight mode and shoot mode are not wired to a frontend.**
* **The player's ride** — calling a slider, mounting, driving — is read and
  measured at ~600 undecompiled lines, and deliberately not ported.
* **The jump and the fall** have no consumer yet: the walker reports a fall and
  nothing acts on it, and the five jump moves are lifted but unwired.

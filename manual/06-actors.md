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
reach, wait, put away, stand — for the same reason. Press it beside a parked
slider and a different arm of the same handler opens the door and seats you.

So "the player draws a gun", "the player dives" and "the player takes a step"
are the same kind of event, and the fight and shoot modes are not separate
engines. They are different **input context groups** feeding the same graph —
and different things pressing the buttons: in a fight, the opponent's AI
presses combinations into his own input queue exactly as your keyboard presses
into yours; in a shoot phase, each gunman has a small brain that walks the
level's navigation grid.

Around that sits a walker that decides whether a step is possible — how steep
is too steep, how high a ledge you can climb, what happens when you jump or
fall, when the floor is water — and, in the cities, a crowd: pedestrians and
vehicles following a circuit authored as lanes and routes.

## In detail

### The `.CTL` channel

Seven files, and all seven parse to the byte: the walk lands exactly on the
file size, 398 clips, and **every one of 2 044 graph edges resolves** — the
loader refuses to start otherwise, which is what makes that a test the data
could fail.

A state's entry carries flags, and every flag-gated block has its traced
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

Some of the special moves are the **most consequential code in the actor
runtime**, because they are where a data file hands control to the engine:
`MDACTION` is the action button (the take, the slider door, a talk), the
`MDJUMP` family is the jump, and `MDDIVEND`, `MDSW2SD`, `RSTAVNT` and `RSTNAGE`
move a swimmer between states.

### `ACTOR_STATE`

Eighteen states, 0..17, mapped **and run**. Several repay the reading:

* **11 is entering the water** — it was read as "the ladder" from the
  decompiler's own comment, and the floor that triggers it is the canal's bed;
  **14 is underwater**. Special moves write both.
* **7 and 8 are the mount and the ride of a slider**, and the dismount move
  refuses to leave anything but 8. State 7 has **no case at all** in the
  per-frame actor tick, and the way into a ride is really state 6.
* **2 is melee**, and `Fight_Engage` is its only writer — through an alias a
  search for the obvious store misses.

### The walker

A step is refused when the face is steeper than **30°** or the ledge higher
than **30 cm** (`dword_910340`, 11.811023622 world units). The narrow phase
sweeps the model's own collision spheres and stops **one unit short of the
contact**, which is why a body of radius 12 rests 13 from a wall; a penetrating
contact is pushed out along the clamped normal and re-swept. The sweep starts a
step-height above the feet, because the ground probe already claims everything
inside that window — without it the lowest sphere met every riser and a player
stopped one step short of a bank's door.

The world unit is an **inch** — measured, not assumed — and the studio still
authored in metres, which is why the constants look like round numbers in one
system and not the other.

**The body sweep meets edges — a reconstruction.** The engine's narrow phase
(`Sweep_ActorMove` over a 930-line kernel) is not transcribed. The port's
stand-in tested a triangle's *interior* only, and on the security centre's
handrails — 8-unit bars at waist height — a sphere meeting the bar's edge
passed straight through, and a player ran down the shaft: *"not possible in
the original game"*. It now meets corners and edges as well, **only from
outside**: a first version that also counted an edge the sphere already
overlapped threw a player standing among a bar stool's edges round the stool.
What the engine does at an edge is not read; this is the geometry that makes
the rails hold, and it says so.

**The jump.** `Actor_MoveBy` applies X and Z only, so the vertical is the
walker's alone, and a walk that floated turned out to be the port's own mix of
two origins rather than anything in the clips. The jump is `MDJUMP0A` and
`MDJUMP01`: 2.5 m forward over a number of frames the entry carries, with a
vertical impulse that brings him back down exactly at the end — measured at 14
frames and 22.9 cm of lift, a flat leap rather than a hop. A global turns the
ground probe's absorption off for its duration.

**The fall.** Stepping off a ledge is banded by the clearance below — 1.5, 3 and
5 m — into the falling group and a camera request; the landing by the distance
fallen: back to walking, a stumble, the stumble and **message 10**, or lying
flat with **message 11** and an overhead camera. The "fall states" a first
reading found were camera requests. And every script that costs health outside
a fight or a shoot phase floors it at 5, so **nothing in adventure mode kills
the player** — there is no adventure death to port.

### The water

`Actor_ApplyMotion` hands the water states to `sub_4A8F30` instead of gravity.
The surface mesh holds a swimmer just under it; underwater he drifts up over
the bed with his pitch turning and clamped, and a **40-second breath** runs from
the first underwater tick, drawn as the horizontal gauge of `Hud_DrawBar` —
the one mode of it the port had never needed — ending in **message 12**, which
the canal's own script answers with a drowning and a rescue to the bank.

Underwater the **dive** key swims: the graph's edge from treading to swimming
matches the input bit that, in the swimming control scheme, is *Plonger*. The
body is drawn through the actor's whole Euler rather than its yaw, so the swim
pitch lies him down. Played the same evening it was built: nine reports, each a
port fault that a green check had passed.

### The slider

Kay'l's sneak can **call a slider** — a hover taxi — and the whole ride is
ported and was played to the end:

* the call finds the nearest point on a **vehicle lane** of the city's traffic
  circuit, and the slider starts at that lane's origin, 39 units back, at the
  hover height of 30.75 that turns up in three unrelated functions;
* it drives in on its own camera and, within 117 units (three metres) of the
  pickup, hands the camera back to the player;
* **boarding** is the action button's slider arm: two geometric gates on the
  door side and a four-metre reach, then the door clips — bound by name to the
  cockpit's doors — and he is seated;
* the ride's **flight model** steers at 5° a frame on the interface's own input
  bits, with a six-step thrust ladder, the delta halved; every state of the
  ride's machine watches at a field of view of **90** against 75 everywhere
  else, through four camera modes — 8 while it comes, 0 when it arrives, 10
  when it leaves with you, 17 when you get out.

The destination is an **address** in another area, so a journey can take you to
another city.

### Melee

A fight is `fight.begin` from the chunk's own script. `Fight_Begin` builds two
combat contexts from the fighters' properties — life, attack, dodge, and the
options menu's difficulty bonus on the player's defence alone — and every frame
runs both fighters' steps, the knock-out trigger, two distance-gated bank
switches at 3 m and 1.5 m, and the hit resolution both ways, throws included.

**The AI presses buttons.** Four profiles of combinations live in each `.CTL`,
and each slot also carries a **percentage** that every choice rolls against;
the AI also presses **eight moves compiled into the executable**, which is why
porting it found content no data file holds. The harder the level, the shorter
the wait. Which level a fight runs at is **adaptive** (chapter 4): the scripts
raise and lower it by how much life your last fight cost.

The camera is **mode 14**, computed rather than authored, and the options
menu's fight-camera row turns out to choose between two camera rigs. A
knock-out is a **60-frame replay shown twice** from two further angles. At the
end the engine releases the parked script and drops the result; the scripts
read the player's life to learn who won. All of it runs in the port and was
played through a lost fight and two won ones.

### Shoot mode

A first-person mode: `Shoot_Enter` hides the player's own body, and the eye
sits at **0.7 × the model's height** from its body-sphere table — the crown,
not the pelvis the camera preset's zero offset would give.

**The gunmen think with the map.** Each gunman's brain converts his position
into a cell of his floor's `MAP2D` grid, and the generic brain — all sixteen
states of `sub_424DE0`, each computing an outcome that one shared epilogue
acts on — navigates, engages, patrols and retreats over it. An ordinary gunman
*sees* by walking a line across the grid; a spectre and a cross-floor watcher
see by casting a ray. How far he can shoot is a **character property**, not a
weapon field. A patrol is a route written into the map file, and the way to
another floor is one hop to the nearest **link** — a staircase written once per
direction; there is no path search anywhere.

The brain is picked by character type from the executable's own 14-name type
table: besides the generic one there is Astaroth's, Gandhar's — three compiled
behaviour scripts, healthy, wounded and critical — and X-Tech's, which does
nothing; a fifth callback serves a type no character has. Over the 306
resolved entry sites the split is **302 generic, 3 Astaroth, 1 Gandhar, 0
X-Tech**.

**A shot is a projectile.** Each actor has four weapon slots with their own
timers and ammunition, drawing on one projectile pool; a bolt flies, hits a
wall or a body, and damages in a fixed order. The player's own shot, the
gunmen's aim, their fall when killed, the hurt reaction's four-frame shove, the
HUD and its wireframe radar, and the **player's death** are all ported. Much of
it has been confirmed in play; the patrol, the floor links and the death are
recorded in `todo/handoff-shoot-mode.md` as done and not yet judged by a
person.

### The street

Three mechanisms, all ported and all drawn:

**The `.OPT` traffic circuit.** Seven blocks, six of six exact. Pedestrians are
spawned by `Slider_Init` at `39 × (5 − density) × h[3]` and walked by
`Sliders_Tick` over lanes and routes, with following, overtaking, reservation
groups and action points.

**The road traffic** rides the same circuit's vehicle lanes, behind two masks in
the AREA chunk that are non-zero in exactly the three areas that have such
lanes. Vehicles spawn at `39 × h[4]` with **no** density factor, capped by the
40-slot ride pool. They share a pool with the walkers because they share the
**reservation groups**: 70 of one city's groups are reached by both classes,
and a vehicle waits on a walker 2 197 times in 1 800 frames. A vehicle closing
on a player in the road brakes, and one still fast when it touches him raises
message 17 — Anekbah's script knocks him flat.

**The authored extras** are ordinary scene programs — 621 `scx.play.actor`
sites — plus the spatial index's push, the bump and talk messages, and the head
look that turns an NPC's head toward you.

The moving population is **lit** by the `.3DO` light table the set supplies and
casts the engine's blob **shadows** (chapter 8).

<p align="center">
  <img src="images/anekbah-street.png" width="560" alt="Anekbah's street with its crowd">
  <br><em>The crowd, the traffic, the ambient fire, the set's baked lights and<br>the characters' shadows, drawn by the port in adventure mode.</em>
</p>

### A misnamed function, and why it is in this chapter

`tools/renames.json` is a map of hypotheses, and one of them had its sense
backwards. `Perso_SetInputEnabled` reads as an enable and is a **block**: flag
`0x80` makes the channel tick *skip* the input search, argument 1 sets it and
argument 0 clears it. The port trusted the name, asserted the flag on the way
out of every conversation, and the player's action button was dead from the
first conversation onward.

The rule that follows is the chapter's, not an aside: **before a rename decides
a behaviour, read the handler rather than the map.** A boolean argument is
where this bites hardest, because a wrong name inverts it silently and both
values look plausible at the call site.

## Where it lives

| | |
|---|---|
| the findings | `docs/ASSETS.md` §7 (the `.CTL` format, the walker, the water, the fall, the fight and shoot AI), `docs/STREET_LIFE.md`, `docs/FILE_FORMATS.md` §5b5 (`MAP2D`) |
| the plans and records | `todo/slider.md`, `todo/fight-mode.md`, `todo/handoff-shoot-mode.md`, `todo/swimming.md`, `todo/falls.md`, `todo/player-vertical.md` |
| the port | `engine/src/actor/` — `channel.*`, `walk.*`, `player.*`, `fight.*`, `shoot*.*`, `projectile.*`, `slider.*`, `vehicles.*`, `pedestrians.*`, `spatial.*`, `pose.*` |
| the tables | `tables/special_moves.json`, `shoot_ai.json`, `shoot_weapons.json`, `fight_ai_moves.json`, `key_bindings.json` |
| the checks | `engine: actor states`, `engine: narrow phase`, `engine: security rail`, `engine: player jump`, `engine: fall reaction`, `engine: water entry`, `engine: slider ride`, `engine: melee`, `engine: shoot brain`, `engine: shoot death`, `engine: pedestrians`, `engine: crowd push` |

## What is not settled

* **The actor runtime has no oracle and cannot have one from this rig.** The
  trace logger sees only what a VM handler narrates; `fight.begin` announces
  nothing, and the shoot phase's opcodes are no better. A capture *did* reach
  combat, so the silence is the mechanism, not the play. Melee and shoot mode
  are therefore **data-constrained**, and a fight's outcome has no oracle.
* **`engine: shoot hit` is red on purpose.** Its empty lists are a behavioural
  result rather than a parse artefact, and baselining them would be worse than
  the red.
* **The released spectres' attack is unresolved** after every data-side lead
  closed; the close attack (dogs bite, robbers strike) is ported beside it.
* **The body sweep's edges** are a reconstruction, and so is the stair start
  height; reading `Sweep_ActorMove` would settle both.
* **Water state 13, the surface, has not been reached by a run**, and whether
  the engine keeps a placed body standing on a water surface is unread.
* **The fight AI's priority gate** has never been *seen* refusing a move.

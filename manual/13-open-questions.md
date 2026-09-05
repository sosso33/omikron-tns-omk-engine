# 13. Open questions

← [Evidence](12-evidence.md) · [Contents](README.md)

---

## In short

What is still unknown, listed **with what has already been ruled out**, so
nobody repeats a search that has already been done.

That framing is deliberate. A negative result is a finding: *"`IAM\OBJECT`
contains one `dialog.start` site in 1 002 records"* saves the next person a day.
And a negative result over a corpus is **only as strong as the enumeration
behind it** — which is exactly how the largest question on this page was
answered, after standing open for months while being false.

## In detail

### The big one: 105 conversations with no launch path

The game ships **321 conversations. 105 of them cannot be started by anything
found in the data.**

Not for want of looking. What has been ruled out:

| ruled out | how |
|---|---|
| another opcode starts conversations | opcode 61 is the **only** way into `Dialog_Load` |
| indirect operands hide targets | all **1 246** of its operands are direct literals; the handler's indirect mode is never used |
| conversations start conversations | the conversation scripts contain no opcode 61 |
| a missed pointer array | every relocated pointer array in `AREA` / `SCENE` is accounted for |
| the object archive | `IAM\OBJECT` — 1 002 slots of 2 048 — holds **one** site |
| a hidden event | `Game_HandleEvent` case 0 calls `Dialog_Load` directly, but **nothing in the binary raises event 0**. A dead entry, not a launcher |
| the message subscriptions | they are inside the already-scanned 5 785 slots |
| a second corpus | `gamedata/IAM/FRENCH/` is a **byte-identical duplicate** of `gamedata/IAM/`, same MD5 |
| the startup scripts | all 173 decode, and the only `dialog.start` in them not already reachable is **272** itself — one, not a hundred |

**Either the mechanism is outside the data, or the content is cut.**

### Closed, and why they are worth reading anyway

Three of these were open long enough to teach something, and the lesson is in
*why* they closed rather than in the answer.

**What starts a cutscene's beats.** Closed 2026-08-29: the SCENE chunk's own
`+4` startup script. Every ruled-out route had been correctly ruled out — the
**inventory** was incomplete. The 5 785 script slots come from zone records and
message subscriptions, and nothing in that walk reaches `+4`, so "no shipped
script starts them" was really "no script I enumerate". The golden trace had
been flagging the gap all along, announcing scenes that no slot could emit.

**What reaches the intro's code.** The script was found at the offset held in
chunk 118's `+68`, and `+68` was read as a script pointer. It is the
message-subscription table. The chunk declares 0 zones and 0 subscriptions, so
the empty table's base coincides with the start of the code — `+4` and `+68` are
the same number, 1040, for that chunk alone. It looked like a coincidence
because it was one.

**One Anekbah panel shows the wrong texture.** Closed by reading the renderer,
not by a depth rule or a draw order — it is the global texture cache plus two
resident sets. [Chapter 8](08-rendering.md) has it. Note which side that put in
the wrong: the viewers are right and the game is the odd one out.

**The scene clip's root orientation.** Closed by *looking*. Three statistics
leaned one way without deciding, and each convention appeared to win on some
shots — but the two decisive cases both read correct in the viewer, so the
apparent split was the metric's fault. "Faces the camera filming them" is a bad
prior: a guard in a corridor is not looking at the lens. Kept as a caution — **a
weak corpus signal measured through a wrong prior can look like structure in the
data.**

### Formats

* **`.3DM`'s `float[3]` track.** The parser's integration of it is read and
  confirmed against the assembly, but the corpus **refutes** "root-motion
  deltas" as playable semantics: near-constant and near-unit in 57 of 60 files,
  which means universal drift. Whatever neutralises the integral in the engine
  is untraced.
* **`.3DM` node slots 0 and 1.** Narrowed rather than solved: uploaded with
  preamble ids 0/1, bound by no drawn mesh, not rotations, and **not the voice
  envelope** either (|r| < 0.2 against per-frame RMS in four files). Slot 0 stays
  in [0,1]⁴ and varies smoothly; slot 1 is a signed low-magnitude 4-vector with
  one dominant component. Eye-direction or blink channels are the surviving
  shapes.
* **97 bytes** across 330 `AREA` / `SCENE` chunks that no documented structure
  explains.
* **One 32-byte field** of the save directory's 72-byte record.
* **`Anim_RootDelta`'s optional 3×3**, on the scene path. Answered for the actor
  path (it is the character's facing matrix).

### The VM

* **24 of 153 opcodes are unnamed**, identified only by the operand domain they
  announce. They are the tail — 32 uses or fewer each. The world scripts
  exercise 124 opcodes, so there is a large corpus to test any guess against.
* **Context status 5 is read and not resolved.** `Area_Transition` writes it into
  a superseded caller, and no event, pump step or handler in this reading writes
  it back to running — that context would be parked for good. It needs two
  transitions in flight, which the shipped scripts may never produce, and a claim
  either way needs a trace this rig cannot take.

### Rendering

* **The flicker on two Anekbah panels.** Best account: a 7-vertex prism of three
  quads with identical UVs, `D3DCULL_NONE` and no depth bias. Note the
  neon-flicker half of that story is **withdrawn** — 148 of the set's 153
  emitters have period 0, which with a one-frame lifetime is a steady glow.
* **Four of the six swapped render-bank pointers** are unread, and have no
  oracle here.
* **Pixel values keep no reachable tier** — filtering, dither, fog and blend
  arithmetic are the driver's.
* **Two parts of the mirror stay reconstruction**, and are labelled as such: how
  the engine confines the reflection to the mirror's area, and the plane's
  normal.
* **A set-piece ring on a one-record anchor is a reconstruction**: the engine's
  heading there is stack garbage by construction, and "not drawn" is what a
  reader's frames of the original showed, not a value computed. The portal's
  red rim has not been re-measured since (`todo/omk-play.md` 76).

### Audio

* **The attenuation and pan law** is DirectSound's, described nowhere in the
  image, and no rig here records sound. **No reachable tier.**

### The port

* **`Actors_SpawnFromTables` is not ported** — the largest single gap. The
  world's own ambient characters never spawn.
* **The 7 partly-ported rows**, each with its missing half named in
  `engine/README.md`.
* **The per-screen native callbacks** (26 of 30 absent from the decompilation);
  the answers they write are not ported.
* **The player's ride**, the LOD selection among an actor's four skeletons, the
  bump's camera shake, and the joystick axes.
* **Two parts of `Actor_Move`**: the mesh-flag filter and the accumulated
  blocked-direction mask; and the corner of the Impasse airlock where the port
  holds the player under a hanging crate, which only a play comparison in the
  original can settle (`todo/collision-scenes-transitions.md` 3e).
* **Ported and checked but not seen in play**: the seventeen scene functions'
  new arms, the arrival's wait, the portal's rings, the door closing behind the
  player in the tunnel.
* Claimed: the opening, the sneak, the take of an object, the door-carrying
  transitions — each confirmed by a reader. Nothing past those.

### Not a gap, but a property of the data

Kept here so nobody spends a day treating one as a decode failure:

* **10 of 561** named voice-over files ship.
* **6 spell recipes cannot fire** and 5 spell items are unobtainable: the
  combination table's gate is never 8. One survives as a world prop.
* **No character in the game is type 7**, so one shoot-AI callback is
  unreachable.
* **13 of the 45 interface sounds can never be resident** — the cache is 32
  slots and the loader returns silently when full.
* **Options page 12 is built and unreachable.**
* **One item in the whole widget tree** carries the arrows-or-marker bits, on a
  child panel no screen reaches — so no reachable screen draws an I2D triangle.

## Where it lives

| | |
|---|---|
| the live list | `CLAUDE.md` §6 — this chapter is its retelling, and `CLAUDE.md` is the authority |
| per-subsystem | the "What this does not settle" section at the end of each `docs/` file |
| the port's open items | `todo/iam-script-engine.md` §Open, `todo/omk-play.md`, `todo/street-life.md` |
| the roadmap | `docs/RECONSTRUCTION.md` — grep it, never read it whole |

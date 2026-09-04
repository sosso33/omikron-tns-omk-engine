# The take animation — session state, 2026-09-04

Handoff for `omk-play` issue **69**, the take's animation. The findings live in
`todo/omk-play.md` 69; this file is the **state of the work**: where it is, what
runs, what does not, and what is untested rather than wrong.

## Where the code is

**Branch `take-height`, in the worktree `.claude/worktrees/take-height`.**
Based on `ce1a0cc`; **not merged into `main`, deliberately** — the reader's
instruction was "stay in branch, it is too broken now to merge into main",
which was about *this branch*, not about main. (I first read it the other way
round and told the sneak session main was suspect; that was my error and it
cost them a held commit.)

Eight commits, tree clean:

| commit | what |
|---|---|
| `c318a57` | two take animations, chosen by height; `scanTakeable` gains `dyOut` |
| `55d7125` | confirmed in play; `--scene-chunk` added |
| `01531ec` | a take clip is a GRID of variants |
| `0e9ba51` | the spec — the variant count is the `.CTL`'s own nibble |
| `908a81d` | `sub_4725B0` read — the blend is bilinear |
| `159d91e` | the grid IMPLEMENTED |
| `9d66724` | the anchor is the FLOOR; stop reading the rest sentinel |
| `0c281e2` | the crouch — root motion carried, the variant SEAM excluded |
| `7c12eb1` | the take is TWO STAGES — the adjust step and its direction |

## What the take does now

Press action →
**`MDACTION`** installs group **600** (`H_ADJSTP`) and the character steps into
position →
**`MDADJSTP`** re-measures the geometry and installs group **41** (`H_TAKL`,
floor) or **143** (`H_TAKH`, table) →
`H_TAK*12` reaches, the clipless **`MDGETOBJ`** hands the object over,
`H_TAK*22` stands up →
**`H_WAITOB`** *waits* →
your press picks **`MDPUTSNK`** (into the sack) or **`MDNOTAKE`** (cancel).

Each state plays **one 21-frame cell** of its clip, blended from the four cells
nearest the approach, with the pelvis dropping through root motion.

## Confirmed in play

* the height rule — `dy −1.3 → group 41`, the sign settled by the running game
  and matching `prop_probe`'s prediction (ring y −80.0, seated feet −78.7)
* the chain walks `H_TAKL12 → H_TAKL22 → H_WAITOB → H_GETOBJ`, one clip each,
  which is the reported "it plays the complete list" gone
* the wait really waits: 234 ticks held, left only when input `0x10` arrived
* the timing: 20 ticks for a 21-frame window at 30 fps, 0.7 s for a grab
* no T-pose at the start (that was the rest sentinel, key 0)
* the crouch tracks: feet lifted and root dropped stay within ~1.5 units all
  the way down, and the take returns to standing

## NOT confirmed — the honest list

1. **The adjust step (`7c12eb1`) has never been driven into the world.** The
   build after it was launched twice; the first run sat on the start menu and
   was killed, the second was never reported on. **Nothing about the step is
   play-tested**, including whether the character now visibly squares up and
   whether the `MDADJSTP` line shows the angle *shrinking* across the step. If
   it still reads 66.4 → 66.5, the displacement is not reaching the walker and
   that is a different fault from the one `7c12eb1` fixes.
2. **The HIGH take (`n = 9`, group 143) has never been entered at all.** No
   object at table height was ever taken. Its `second` axis is a guess: the
   high arm of `sub_465D30` sets no `var_2C`, so what reaches `+0x1C8` there is
   unread, and the code says so.
3. **The blend fractions are the weakest link.** 256/51, 256/53, 256/50 each
   match their own clamp, which is one hypothesis fitted three times, not three
   confirmations. What would settle them is a rendered pose at a mid-cell angle
   checked to sit *between* two variants.

## Broken / owed

* **`verify.py: engine actor states` is RED**, deliberately not re-baselined.
  Six counts move — channel ticks 55182 → 57330, edges 10367 → 11599 and four
  more — because a grid state now lasts 21 frames instead of 125, so more
  transitions fit the harness's tick budget. That is the expected consequence
  of a real fix, but it is a broad behavioural oracle and re-baselining it
  silently is exactly the "green beside a wrong reading" CLAUDE.md 1 warns
  about. **Someone has to decide**, and the decision needs the animation
  confirmed by eye first.
* **No check covers any of this.** Every fault today was found by a reader
  playing the build; none by anything in `verify.py`. A check should assert the
  FOUR OFFSETS for a known geometry (`n * len == keys` is a second constraint
  the data can fail) or count DISTINCT ENTRIES per press — never sample a clip
  name, which wasted two traces.
* **A debug printf is still in** `play.cpp` (`crouch: …`, one line per frame of
  a take). It is the instrument that found the seam; keep or gate it, but do
  not merge it without deciding.

## The one unresolved contradiction

`Anim_RootDelta` (`0x004711D0`) indexes the position keys by the **raw frame**,
no cell offset — traced, not assumed — so the engine reads **cell 0's** root
motion for a grid clip. But cell 0 is a step to the *right*, and the rotations
alone leave the body floating. So the port reads the **blended** cells instead,
for both the crouch and the step's direction. Both divergences are labelled in
the source.

Either the engine starts a grid state's frame **inside its own cell** — which
would change how both the root reads and `sub_4725B0`'s offsets are modelled
here, and is the reading I would test first — or the displacement reaches the
actor by a path not yet found. Settling this is the next real piece of
reverse-engineering, and it may simplify everything above.

## Suggested order for whoever picks this up

1. Play the current build and answer §"NOT confirmed" 1 — does he square up?
2. Take something off a table, to exercise group 143 for the first time.
3. Settle the frame-origin question above; it is upstream of the two labelled
   divergences.
4. Then decide `engine: actor states`, and write the check §"Broken / owed"
   describes.

## How to run it

```bash
cd .claude/worktrees/take-height/engine && make -s play
./build/omk-play <gamedata> ../tables --speed 3 --nofmv
```

Play to the Impasse and take the rings after the tutorial cutscene. A street
start needs `--scene-chunk 55` with `--area 222` or there are **no props to
take** — the chunk that would have loaded on the way in never does, and nothing
in the viewer says so.

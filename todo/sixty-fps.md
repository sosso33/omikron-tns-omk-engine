# 60 fps - the frame-rate cap lifted, and the bodies smoothed between keys

Asked 2026-10-01: "how difficult would real 60 fps support be, with
interpolation?" Built as an ENHANCEMENT on the reader's instruction - off by
default, under `[Enhancements]`, in `todo/enhancements.md` rows 11 and 12 -
although half of it turns out to be closer to fidelity than to invention (§1).

## 1. What the ORIGINAL does - read 2026-10-01, all read-only

* **It is variable-step.** `Game_Frame` (0x0041F740) sets
  `flt_4C30D8 = 30 / smoothed_fps`, clamped at 3.0 (`docs/BOOT.md` 4). Every
  clock downstream is in "frames at 30 Hz" and advances 0.5 a frame at 60.
* **It has NO frame cap.** `Game_RunLoop` calls `Game_Frame` from the message
  pump's idle path with no `Sleep` and no timer. The fullscreen present
  (`sub_433860`, 0x00433860) is `Flip(NULL, 1)` - `DDFLIP_WAIT` without
  `DDFLIP_NOVSYNC`, so the monitor's refresh paces it (60-85 Hz on a 1999
  CRT); the windowed arm is a `Blt` with `DDBLT_WAIT` (0x1000000), unpaced.
  So **the port's 30 Hz pacer is the port's own**: on a fast machine the
  original ran above 30, with the delta at 0.5 or less.
* **It never interpolates between two keys of one clip.**
  `Anim_ApplyNodeFrame` (0x00471690), called on every node from
  `Anim_SetFrame` (0x004715B0, 9 callers), stores the float frame at node
  `+160`, `call _ftol` - MSVC's truncating conversion - and builds the node's
  matrix from that ONE quaternion (`shl eax, 4` / `sub_442A00`). Read in the
  assembly. The one slerp beside it, `sub_471820` (0x00471820), mixes two
  DIFFERENT clips, each at a truncated frame, by a weight `f32(a4,8) * 255` -
  the clip-change cross-fade the port already has (`blendTracks`/`qslerpK`).
  ROOT MOTION is fractional: `Anim_SetFrame` hands both float frames to
  `Anim_RootDelta`. So at 60 the original moved a body smoothly while its
  POSE stepped at 30. Smoothing the pose is therefore the enhancement (row 12);
  lifting the cap (row 11) is what the game itself did on a fast machine.
* **What the original itself does differently at 60** (an agent's read,
  tagged; none of it is to be "fixed" - the port reproduces it):
  * ambient emission is once per FRAME - `Sound_TickEmitters` (0x0044FC30):
    `if (phase >= period) { phase = 0; fire } else phase += flt_4C30D8`. A
    period-0 `neon` emits twice as often at 60 and its particles live three
    frames instead of two, under an ADDITIVE blend: steady neon is brighter.
    The phase resets rather than subtracting, so a period-3 `cacC` fires every
    ~133 ms at 30 and ~117 ms at 60. [decompilation]
  * `Fight_TickAI` (0x00464830): every WAIT is on the millisecond clock
    (`Sys_GetTimeMs`, five calls, no reference to the delta - read in the
    assembly), but intent 104's re-roll and the approach branch's
    `Perso_InjectInput` run every frame. [decompilation]
  * probable, unconfirmed: `Cef_TickChannel` (0x004A8160) counts `actor+214`
    once per input-loop pass against the entry's `+82`. Read `sub_43E080` /
    `sub_43D920` before relying on either reading.
  * everything else read is delta-scaled: the `.CTL` frame clock
    (`actor+188 += flt_4C30D8`), `Actors_TickAll`'s timers and head look, the
    shoot AI's countdowns, turns and Gandhar's waits, the player's shoot death
    timer, projectiles, the crowd, `Clock_Tick`, the idle-movie timer, the
    texture scroll. The input edge filter is a ONE-frame lookback plus a mask
    (`dword_4E9720` = 0x203F while a screen is open), so a press is one edge
    at any rate.
  * `Game_Frame` measures the delta AFTER `Game_Tick`: a tick runs on the
    previous frame's delta. The port measures first - one frame of difference,
    invisible at a steady rate.

## 2. Where the PORT is frame-locked - audited 2026-10-01

Counting PRESENTED frames where the engine counts the delta. These are bugs at
ANY rate other than 30 - a slow machine included - and are fixed first:

| site | what | at 60 |
|---|---|---|
| `area.cpp` `tickFades()` | both screen fades, default `dt = 1.0` | 2x fast |
| `play.cpp` `shimmerClock += 2.0f`, and `sceneShimmer` in `--scene` | the engine does `2 * frameDelta` | 2x fast |
| `play.cpp` `playerDeathCountdown -= 1.0f` | the engine's `dword_4E975C -= flt_4C30D8` | respawn in half the time |
| `play.cpp` `--mediaTextFrames` | the interaction subtitle's hold | off at half its audio |
| `play.cpp` `--boardCam` | the slider-boarding camera, 60 frames | 1 s, not 2 |
| `play.cpp` gunman death: `n - s.deathStart` (pose and root walk) | the presented counter as a clip clock | falls 2x fast |
| `player.cpp` `walker_.step(dx, dz, 1.0)` in `nudge()` / `moveBy()` | only airborne or sliding | subtle |

Per-tick in the port because the port reads the engine that way - each to be
CHECKED against the original before it is touched:

| site | question |
|---|---|
| `fight.cpp` camera (swing, settle, shake decay, eye pull) | is `sub_445E30` / `sub_4463C0` per frame or per delta? |
| `fight.cpp` KO replay ring, 60 frames | does the engine record per frame? |
| the port's fight AI waits | the engine's are in MILLISECONDS - are the port's? |
| `slider.cpp` braking `v -= v*0.25`, the no-thrust countdown | per frame in the engine? |
| `area.cpp` `bumpCooldown_` (`dword_538318`, a float) | per delta? |
| the UI cloud (`sub_4B1B00`), driven by `n` | per frame or per ms? |
| `input/bindings.h` auto-repeat, the dialogue scroll | UI-only; per frame in the engine? |

And outside this task: `GameState::clockTick` has NO caller in `engine/`, so
the calendar never advances; and `Sliders_Tick`'s `++dword_539938` has no
reader found.

## 3. Steps

1. **The frame-locked sites of §2's first table**, each onto the delta. At 30
   nothing moves (the delta is 1.0), so the existing checks stay as they are.
2. **Row 11, `framerate = N` / `--framerate N`**: the pacer's period, 30 by default;
   `all = max` asks 60. The simulation is untouched - it already steps on the
   measured delta - and a `--frames` run never reaches the pacer, so headless
   checks are unaffected. A check that the DURATION of a fade, a shimmer cycle
   and a death countdown holds at 60 while the frame count doubles, the shape
   of `engine intro beat`; shown to fail on step 1's sites.
3. **Row 12, `animation = smooth`**: `blendTracks` takes a fraction and
   slerps key `k` to `k+1` (and lerps the root translation), never across a
   clip change - that is the engine's own cross-fade - and never past the
   last key. Off: the truncation, which is `_ftol`.
4. **§2's second table**, one read each; port only what the engine scales.
5. **Played at 60** on the Vulkan / GLES viewers: a street, a conversation, a
   fight, a shoot phase. The software reference may not hold 60 and does not
   have to.

## 4. State

| step | state |
|---|---|
| 1 | **done**: fades, both shimmers, the shoot death countdown, the media subtitle, the boarding camera, the gunman's death clip (pose, message 3, fall) - the last on a new `gameClock`, the sum of the deltas. At 30 every one reduces to the old arithmetic (the delta snaps to 1.0): `engine: shimmer`, `fades`, `shoot death`, `slider door` green, and `shoot hit` (red on purpose) byte-identical to the unmodified commit run in a worktree. Behaviour that MOVES even at 30: the shimmer and the death clip now stand still under the pause, as the engine's zero delta stops them. `nudge()`/`moveBy()`'s `dt = 1.0` left for step 4 - an extra gravity tick while airborne at ANY rate, not a frame-rate fault |
| 2 | **done**: `framerate = N` (30..240, or `game`) under `[Enhancements]`, `--framerate N`, `all = max` asks 60 (`kMaxFrameRate`); the pacer's period is `1 / N`, nothing else reads it. NOT `--fps`, which already exists and SHOWS the rate. `engine: frame rate` - `framerate_probe` runs `Session::frame()` at 1/30 and 1/60: the black fade 61 -> 121 frames and a 25-frame colour fade 26 -> 51, the time held to a frame; shown red (61, 61, 26, 26) with `tickFades()` back on its default. `enhance all` (shown red with the `take` removed), `config template`, `play usage` green |
| 3 | **done**: `animation = game|smooth` / `--smooth-anim`, `all = max` turns it on. `composePose` gained FLOAT-frame overloads: off they floor to the integer key bit for bit; on they `qslerp` key `f` to `f+1` (the engine's 1/256 weight steps), holding at the last key and never blending an integer caller. Smoothed: the PLAYER (`PlayerController::poseFrameF`, zero fraction wherever `poseFrame` clamped or the next key is past a variant window's seam), the SCENE-PROGRAM bodies (`sceneFrame`) and the CROWD (`PedJob::frame` a float). NOT smoothed, still the truncated key: the gunmen's clips and death clip, the melee foe, a dialogue line's `.3DM`, the shoot aim layer's bent row, the slider door, and the player's `rootDrop` - an accumulated per-key delta with its own seam guard, where fractions would double-sum. `engine: frame rate` grew a pose half on a synthetic one-bone clip (0.5 -> 44.6 deg, 0.25 -> 22.1, the last key held, an int caller never blended; shown red with the fraction forced to 0). Off is the default and is identical: `clip root`, `pose`, `pose blend`, `pose equivalence`, `street frame`, `scene facing` green, and two Anekbah street dumps at `--speed 0.5` byte-identical off, 26890 bytes different on |
| - | a 60 fps SMOKE RUN of the supermarket shoot phase (`--speed 0.5`, 1600 frames, step 2's binary): clean, shoot mode entered at presented frame 784; the player was NOT killed within the run, so the death countdown is unexercised there - the gunmen's half-size steps are not step-for-step the 30 fps run's |
| 4-5 | not started |

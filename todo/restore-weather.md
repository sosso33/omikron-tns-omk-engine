# Restore the weather - the showers the original planned and never shipped

PLANNED 2026-10-27, and built the same week - see "Progress" below (steps
1-4 and 7 done). The reader's "do a restore weather plan
and save it", after the reading in `docs/ASSETS.md` 4e ("The shower's
music, and two cities that never get it", "Type 3's draw is complete code
behind the wrong test") - the evidence is there; this file is the work.

## What is missing, and how sure each row is

| | the original | evidence | tier |
|---|---|---|---|
| **Qalisar** (AREA 101) | weather type 0: the cycle never runs, its shower music (track 120, then 56 back) never plays | the 23/24 subscription with a track no other script names; the reader heard 120 as Qalisar's music under the rain sound (2026-10-27) | the WISH is read; the TYPE (rain) is inferred from the track |
| **Lahoreh** (AREA 64) | the same, track 121 then 27 | the same; 121 heard as rain too | the same |
| **the sand** - type 3: Jaunpur (1), Boz's interior (25), the docks (71), the rooftop streets (249, 250) | the cycle runs (`test al, 0Ah`) and Jaunpur's shower track 122 plays, but nothing draws: the caller reaches `sub_481D90` on `test al, 4` alone | read from the code, all three halves (cycle, caller, arm) | the arm's PARAMETERS are read; how `sand.bmp` was meant to look is NOT - see step 1 |

The reader's hypothesis, which the reading supports and cannot settle: the
sand was planned and half-built, the rain sound in 122 a placeholder, and
the draw gated off for lack of time (`sand.bmp` is a 128x128 FIELD of ~70
grains where the arm blits one bitmap per drop, sized from the 64x64 snow
flake). Whether the gate was closed on purpose, nothing records.

## The rules this follows

* **Off by default.** The 1999 game never shows any of it, and the port's
  default stays the original byte for byte (`todo/enhancements.md`'s rule).
  The switch is ONE key and ONE flag - proposed `[Restored] weather = 1` /
  `--restore-weather`: neither `[Enhancements]` (the original planned these)
  nor `[Remaster]` (they override no art). **The reader's to name** - a new
  category, so it is decided once.
* **Its own files** (`feature-code-in-own-files`): `engine/src/o3de/
  weather_restored.{h,cpp}`; the shared files gain a call at each seam and an
  `_off` stub if a build can leave it out.
* **The *Niveau de détail* 2 gate stays** (`cmp byte ptr [0x90E727], 2`):
  restored weather is still the original's weather, shown where it shows.
* **Label every invented part.** Steps 2 and 3's types and step 4's look are
  OMK's reconstruction, not readings; say so in the code, the docs and the
  option's help text.

## Progress

* **2026-10-27, the reader's answers**: the switch is `[Restored] weather` /
  `--restore-weather 0|1` - off by default, ON under the remaster preset
  (its own key or flag still turns it off); the sand AS THE ASSET SUGGESTS
  (one grain a drop, following the shower's ramp - OMK's design, labelled);
  with `[Remaster] wet` the restored rain wets its floors too.
* **Step 1 done** (`docs/ASSETS.md` 4e): the arm would blit the sand's
  top-left 64x64 into every drop, all 200 for the whole shower.
* **Step 2 done**: Qalisar (101) and Lahoreh (64) take type 1 at the seam in
  `weatherFrame` (`weather.restored`); their shower tracks 120 and 121 play.
  Qalisar's neon street (15300, -10050) is under a roof, so under the
  original's rule its shower never begins there. Of its three start
  addresses, 335 (10341, -260, -9507) and 337 are open; facing 90 at 335 is
  a lamp-lit street with its signs, and it rains there. (A first test used
  1804, -6890 - Anekbah's street start, OUTSIDE Qalisar's city: the reader
  found it in play, 2026-10-27; the check and these notes moved to 335.)
  With the 3D weather the neon street rains too,
  the streaks pink by the neon, the wet street reflecting the signs.
  `engine: weather`'s restored legs.
* **Steps 3-4 done, as the asset suggests**: `Weather::step` gains the sand
  arm (bit 8, the caller passes it only with the switch); the 2D draw cuts
  `sand.bmp` into its 63 grains and blits one a drop (capped at ~3 px - a
  near grain blown up to 40 px read as a block); the remaster draws them in
  the scene (1.5 units - 4 read as glowing blobs), opaque with the colour
  key, lit. Jaunpur's open addresses 49, 54, 59 (its start address 34 has a
  roof over the frame-1 camera, so under the original's rule its shower
  never begins there). `engine: weather` (2D) and `engine: rain 3d` part 10.
* **The remaster's sand redone** (the reader in play, 2026-10-27: "more like
  yellow/brown snow than sand"): in the scene each grain is now a short soft
  streak along the arm's wind (x back 0.866, y down 0.3 - mostly sideways),
  half a frame of it long (15 units), the rain's streak texture tinted with
  that grain's own colour in the field, additive, lit; and the shower has a
  DUSTY SKY - the grade toward an ochre of each pixel's luma (0.40 at the
  storm's height), flattened and lifted a little (0.12), in 16 steps as the
  shower ramps (`remaster.grade` overcast -1..-16). The original look keeps
  its specks.
* **...and redone again, the reader's idea** (in play: the streaks "look a
  bit like rain"; "a mix of a moving fog effect and small particles using
  the sand texture" - remaster only, since the fog is far from the original
  arm, which blits 200 pictures from `sand.bmp` and nothing else): a DUST
  FOG in both scene shaders (`View::dust`, `dustAmount`) - toward an ochre
  lit by the set's ambient, as thick as the storm (`1 - exp(-s n (d + 300) / 1100)` - the 300
  of dust in front of everything, so it drifts by the player too: "make the
  moving fog go closer"),
  its density `n` a 3D value noise at the fragment and the ray's middle,
  drifting with the wind on the weather's clock (held while paused), on the
  batches the game fogs (or marked -1 / -1) - and the grains back to small
  soft dots of their own picture (0.8 units and at most 1.5 px each side at
  480 lines - 1.8 units read as "really too big"; a texel of black round
  each so the filter softens it), additive at 0.45 (0.9 too solid), lit; the grade's ochre halved (0.20).
  MORE VISIBLE MOVEMENT (the reader's question): the clouds stretched along
  the wind (noise scale 0.0012 along x, 0.004 across), contrasted (`g^2 *
  2.2`), sampled near the eye too (250 units out), drifting at 700 units a
  second (300 read as still) - 10 frames apart, 453092 of total colour change
  where it was 269683.
  Jaunpur's square: 213462 pixels the fog's alone on Vulkan, the same on
  GLES; `OMK_NO_DUST=1` (an instrument) leaves it out. `engine: rain 3d`
  part 11.
* **Step 5**: the 2D grains go into `fb` like the rain and snow, which every
  backend composites; the 3D sand is the remaster's (Vulkan, GLES).
* **Step 7 done**: `docs/ASSETS.md` 4e, `engine/README.md`'s coverage, the
  row "Meteo restauree" on OMK's first page (`restored.weather`, next start).

## Steps

**1. Read before building (no code).**
* `sub_481D90`'s loop bound on the sand arm: all 200, as the snow's, or the
  cycle's count (`dword_4CFE1C`), as the rain's? The sand is a timed shower,
  so a fixed 200 would ignore its own ramp and fade.
* What `I2D_GetBitmapSize(v60, snow)` fills, and whether `v60` is the
  source rectangle of the packet `sub_4810D0` hands `Blt` (`v63` points at
  it; the decompilation's indices do not line up on sight - read the
  assembly). That says what the original's sand arm WOULD have drawn: the
  whole 128x128 field squeezed into a 15-unit square, or its top-left 64x64.
* Write both into `docs/ASSETS.md` 4e, with the dead ends.

**2. Qalisar and Lahoreh rain** (the small half, ~half a day).
* At the seam where `weatherFrame` reads AREA `+176`
  (`backends/sdl/playframe_world_weather.cpp`, the `type` lookup): with the
  switch on, a table `{101 -> 1, 64 -> 1}` overrides a type of 0. Nothing
  else changes - the cycle, the roof ray, message 23 -> 120/121 and 24 ->
  56/27 are the original's own code and data.
* Walk both cities' open air with the clock skipped (`OMK_WEATHER_SKIP_MS`)
  and LOOK: the roof ray under Qalisar's walkways and arcades decides where
  it rains, and a set authored dry may have covered spots nobody saw.
* The remaster's wet floors (`[Remaster] wet`) already follow "the area's
  weather is the rain", so Qalisar's streets would get wet with no more work.
  Decide with the reader whether that is wanted when both switches are on;
  `wetFloors` then needs Qalisar's and Lahoreh's sets measured (~100 ms a
  set at load, Anekbah's figure).

**3. Open the sand's gate** (~half a day).
* The caller's dispatch, with the switch on: `bits & 2` -> the lines, else
  `bits & 0xC` (was `& 4`) -> the sprite arm. `Weather::tick` already treats
  bit 8 as the original does (`bits & 0xAu`, box 300 at the scatter) - check
  it, do not rewrite it.
* `Weather::step` gains the sand arm from `sub_481D90`'s else branch: box
  300, speed 30, size 15, the snow's direction (x back 0.866, y down 0.3, z
  only wrapped), with the loop bound step 1 found.
* `weatherDraw` loads `IMAGES/sand.bmp` beside `snow.bmp` (key 0, the
  loader's) and blits the sand's drops from it.

**4. How the sand looks - the only design decision.** Two ways, the reader
picks:
* **As coded**: exactly what step 1 says the arm would have drawn, the
  original's unfinished state and its oddity with it.
* **As the asset suggests** (OMK's design, labelled): the 128x128 field is a
  sheet of grains - cut it into its grains at load (the colour key separates
  them) and give each drop one grain, or draw the sheet as a drifting
  screen-space layer at the cycle's intensity. The first stays closest to
  the arm's per-drop blit.
* Both from the same particle stepping, so only the blit differs. Decide by
  LOOKING at Jaunpur's souk and the docks in a shower, side by side.

**5. Every backend** (`fix-all-backends`). The drops are drawn into the
shared 2D surface (`fb`, under the interface layers), which every backend
composites - so software, Vulkan, GLES/Vita, GL1 and citro3d should all show
it unchanged. Confirm on each: a capture of Jaunpur in a shower per backend.
`sand.bmp` is 16 KB at 8 bpp: nothing on the 3DS's or the iBook's budget.

**6. The checks** (`verify.py: engine: weather`, new legs - `--slow`, they
render). Each shown to fail.
* **Off, unchanged**: Qalisar at detail 2 past the first wait - view bits 0,
  no message 23; Jaunpur - message 23 posted, 0 flakes drawn. The same lines
  the check reads today.
* **On, Qalisar**: type 1 from the override, message 23 to its handler,
  `music.play 120` run, streaks drawn. Lahoreh the same with 121.
* **On, Jaunpur**: sand drops stepped and N drawn, from `sand.bmp`.
* Read them as `@evt` events (CLAUDE.md 4), not printf English: the weather
  prints lines today, so the restored path should raise events.

**7. Docs and the menu.**
* `docs/ASSETS.md` 4e: the switch, what it restores, what is invented.
* `engine/README.md` coverage: a row for the restored weather, marked as
  beyond the original.
* The OMK options page (beside the Remaster page's "Sols mouillés"): one row
  "Météo restaurée" / "Restored weather", its help saying it shows what the
  1999 game planned and never shipped.
* `todo/sweep-log.md`: one task.

## Not in scope

* Khonsu (177) and the rooftops (42) rain with no 23/24 subscription, so
  their music never changes: that is the original's data, not a gap.
* The snow's static in Mahaleel's cave after it has snowed (ASSETS 4e): the
  original's quirk, kept.
* Making Jaunpur rain instead of sand, to match track 122's sound: it would
  overrule the code. If the reader wants the music to match the sand, a
  separate, later option.
* The boot default *Niveau de détail* 1 that hides every weather: the
  original's default.

## Open questions for the reader

1. The switch's name and section (`[Restored] weather` proposed).
2. Step 4: as coded, or as the asset suggests?
3. With `[Remaster] wet` on too, should Qalisar's and Lahoreh's restored rain
   wet their floors?

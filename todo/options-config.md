# Graphical options, first as a config file

A reader's ask: skybox/fog and the graphical options (detail, crowd density,
...), "first with a config file, options menu is for later".

The two turned out to be one topic. Rows **3, 4, 5, 6 and 7** of the game's own
options table are *Distance de clipping*, *Affichage du ciel*, *Affichage des
ombres*, *Niveau d'activité dans les rues* and *Niveau de détail* - so the sky
and the clip/fog distance ARE option rows, and building either without the
option plumbing means building its toggle twice.

## The schema is the game's, not ours

**`GetPrivateProfileStringA` is not only the `.TAG` logger.** The engine reads
a `[Preferences]` section - 65 keys - and also `[Debug]` and `[User]` (with an
`id`, and per-developer names ANTOINE, CHRISTOPHE, FABIEN, FRANCOIS, GUEST,
MANU, OLIVIER). No `.ini` ships in `gamedata/`, so there is nothing to read
from a real install; what the binary gives us is the **key names**, which is
what a config file should use rather than anything invented here.

The 65 are in `verify.py: preferences keys`. The ones this topic wants:

| key | what |
|---|---|
| `clipdistance` | option row 3, choices 25 / 50 / 100 / 150 / 200 |
| `displaysky` | option row 4, Non/Oui |
| `displayshadows` | option row 5, Non/Oui - **read and drawn 2026-09-08**, `ASSETS` 4d |
| `SoftwareMode`, `GFXCard`, `screen_x`, `screen_y`, `window` | the device rows 2 and 8 |
| `music`, `DialogAttenuation`, `FxAttenuation`, `AmbientAttenuation` | the audio rows 10-12 |
| `displayframerate`, `debug_zones`, `display_path`, `viewer`, ... | the debug set |

**Two settings have no ini key**: the crowd density (row 6) and the level of
detail (row 7). `displaypassersclock` is a DEBUG display, not the density - the
labels mislead, and the exact strings had to come out of the data section
rather than IDA's truncated names (`aDisplaypassers` is really
`displaypassersclock`, `aAmbientattenua` is `AmbientAttenuation`). Their
apply-hooks write globals; density's is the one `Slider_Init` reads as
`39 * (5 - density) * h[3]`.

**They are not menu-only, though - this file said so and was wrong.** Having no
ini key is not the same as not persisting. The **save file's 3496-byte header
is the settings block**, and the density and the level of detail are two bytes
of it (GAME_STATE 8a, `verify.py: settings block`). So the game keeps its
options in **two** places, not one, and a port needs to know which is which:

| | the `.ini` | the save header |
|---|---|---|
| what | 65 `[Preferences]` keys, `[Debug]`, `[User]` | 21 fields + the three binding tables |
| written by | the installer / by hand | `Game_WriteSave`, on **every** slot save |
| read by | `GetPrivateProfileStringA` at boot | `SaveDir_Load`, behind the `OMK_SAVE` magic |
| holds | boot and device settings, and 3 of the 5 graphical rows | all 74 option rows' state |

They overlap: `clipdistance`, `displaysky` and `displayshadows` are in both.
The ini is what the game boots with and the header is what the options menu
edits, so the header is the LATER of the two and wins for anything a player has
touched. There is one header for all 256 slots.

Which is why the two shipped saves are evidence and not just fixtures: they
differ in exactly two of those 3496 bytes, and both are settings a player
changed - clip distance 200 against 150, crowd density 4 against 3.

## Steps

| # | step | state |
|---|---|---|
| 1 | the reader: `[Preferences]` with the game's own key names | **DONE** 2026-09-05 |
| 2 | wire what the port already has - density, clipdistance | **DONE** 2026-09-05 |
| 2b | read the settings out of a save header too, and let it win | **DONE**, with step 2 |
| 3 | the sky: establish whether one exists in the data at all | **DONE** 2026-09-05 - and drawn |
| 4 | fog | **DONE** 2026-09-05 - drawn, both backends |
| 5 | the SHADOWS, row 5 - and row 7's level of detail, which says how many | **DONE** 2026-09-08 - read (`Actor_DrawShadow` 0x00467E20) and drawn; `docs/ASSETS.md` 4d |

Step 3 is research and may end in "narrowed": `PORTING` records that fog has
**no reachable evidence tier**, because the captures cannot validate pixel
values. The one hard datum so far points at no sky in an interior - the
dialog-402 captures are black where the render has holes, "because the game
does not clear to a sky either" - but that is an apartment and says nothing
about a street.

**That sentence about fog needs splitting in two, and step 2 is what split
it.** PORTING's ruling is about a pixel's VALUE, and it stands. The fog's
EXISTENCE and its parameters are a different question and they are now read
out of the code, at the same decision level as the blend modes already ported:
the game draws **linear** fog (`D3DRENDERSTATE_FOGTABLEMODE` = 3) at density
1.0, from `clipdistance x 39.37 x 0.25` to `clipdistance x 39.37`, coloured
from the scene's `+336` through `FOGCOLOR`, skipped for key bits `0x2080` and
doubled for `0x800`. So step 4 is not research any more - it is a renderer
change with a written spec, and only its APPEARANCE stays untestable.


## Step 4, done - the list is finished

The fog is drawn, in both backends, and the reading turned up the thing the
step actually hinged on: **the fog colour is BLACK**.

`Scene_Load3DO` memsets the scene object and `sub_44E830` then writes
`a1[84] = 0` - which is `+336`, the fog colour - explicitly, and nothing else
in the decompilation writes that field on a scene. (Three greps hit `+336`;
all three are other structs at the same offset, the collision CLAUDE.md 1
warns about.) So the shipped fog DARKENS toward the horizon rather than hazing
it - which is what a domed city at night wants, and what hides the hard edge
step 2's clip distance would otherwise leave. The two go together, and that is
why they were one piece of plumbing.

One mode colours it: `dword_93082C == 1` gives R 40, G 80, B 64 (a murky
green) with a 15.0 m clip distance, entered when a camera with flag 0x800
passes a player-derived height and flagging the player's own node. Consistent
with underwater; recorded as a reading, not a finding.

Implementation: the fog is a field of the renderer boundary's `View`, and the
two bucket-key exclusions (0x2080 unfogged, 0x800 doubled) are applied where
the key is - `renderer.cpp` and `vkrender.cpp`, the same rule in both. On
Anekbah at a 25 m clip it moves 42.9% of the Vulkan frame and 46.1% of the
software one. `--fog 0|1`, `--fog-colour r,g,b`.

`verify.py: engine fog` measures a white quad at ten known depths through the
boundary, so no set's geometry is in the answer; shown to fail three ways -
the end read from the 0.95 split, the 0x800 doubling dropped, and the 0x2080
exclusion forgotten.

**A note on how long this took to find.** All four steps' screenshots were
rendered through VULKAN without my noticing - the viewer picks it up by
default on this machine - so the software fog measured 0 changed pixels for
several rounds while being perfectly correct. The lesson is CLAUDE.md 5's,
one level out: when a render does not change, check WHICH renderer ran before
checking the code.

## Step 3, done - and the answer is yes

A sky exists, ships, and is now drawn. It is **not** a skybox or a dome: it is
a flat painted CEILING, 864 corners in a 12x12 grid with every vertex at
Y = 401.09, scaled 12.5x and hung 2250 units up, following the camera in x and
z and never in y. Which is what a domed city has.

The AREA chunk's `+133` names it and 17 of the 259 areas do; 242 name none,
which is what interiors should look like. Six models ship, plus a seventh
(`jansky`) that nothing names. The single texture on each is called `ciel1` or
`ciel2` and one mesh is `TOITCIEL`, so the files name themselves and no
inference was needed.

The mechanism is in `docs/ASSETS.md`, "The sky". One detail matters beyond this
step: `Area_LoadMiscModel` sets mesh flag `0x10000`, whose line in
`Render_SubmitMesh` is an ASSIGNMENT (`state = 0x800`) rather than an OR — so
the sky's bucket state is exactly 0x800, which keeps it out of the far bucket
AND doubles its fog range. Step 4 needs that.

Drawn in `omk-play`, off with `--sky 0`; turning row 4 off moves 6.3% of the
frame on Anekbah's street start. `verify.py: the sky`, shown to fail by reading
`+134` instead of `+133` and by making the probe report a non-flat plane.

## Step 2, done

**One option turned out to size four things.** The clip distance is in METRES
and the world unit is an inch, so the engine converts with 39.37007874015748
and hands `D` to `sub_440BE0(scene, D, 1)`, which writes `+340 = D`,
`+328 = D*0.25` and `+332 = D*0.95`. Those are, in order: `sub_48D3B0`'s
visible-set radius **and** the fog END; `bucketKey`'s nearSplit **and** the fog
START; and `bucketKey`'s farSplit. (The fog ends at `D`, not at the 0.95 split
- two different numbers, and reading the wrong one is a silent 5%.) The full
chain is in `docs/ASSETS.md`, "Where the two splits come from".

`engine/src/platform/settings.h` resolves the three sources in the engine's own
order - defaults from `sub_41F4C0`, then `[Preferences]`, then the save
header - and records which one supplied each field, because a setting that
silently came from the wrong place shows up only as "the config file does
nothing". The two rows with no ini key keep this port's own `[Options]`
section, at the ini's precedence level, so a save still wins.

**And a third section, `[Enhancements]`, for what the original never had**
(2026-09-08, the reader's rule: anything not in the original stays optional,
behind a launch flag or a config category of its own). Its first key is
`antialiasing = N` — MSAA on the Vulkan backend, `--aa N` on the command
line, OFF by default, which the software reference ignores because the
original's ANTIALIAS state is explicitly off (`docs/ASSETS.md` 4). Nothing
in the save header can carry it. `verify.py: engine: anti-aliasing`.
The second key is `texturefiltering = nearest|bilinear|trilinear`
(`--filter M`) with `anisotropy = N` (`--anisotropy N`), same rules;
`todo/enhancements.md` carries the list.

Wired: **the crowd density** goes to `Session::setStreetActivity`, and **the
clip distance** drives a real visible-set walk in `omk-play` - the distance
half of `sub_48D3B0`, run per set mesh over runs of consecutive corners
precomputed at the load. Anekbah's five choices give 68 / 242 / 565 / 902 /
1264 mesh runs drawn of 1632, and an absurd distance culls 0. The four SIDE
planes are deliberately left out: they are the camera's business rather than
the option's, and a wrong plane sign deletes the world silently.

Two things worth knowing from doing it:

* **the density is a SPACING, not a count** (`39 * (5 - level) * h[3]`), and
  the walker pool caps at 200 - so on Anekbah levels 2, 3 and 4 all saturate
  and only level 0 (138 walkers) looks different. The port is faithful; the
  option simply has less room than its five labels suggest.
* **the clip distance without the fog looks wrong**, and that is the argument
  for step 4. At 25 m the far buildings are gone behind a hard black edge -
  which is exactly the edge `FOGEND = clipdistance` exists to hide.

`omk-play --config <ini>` and `--clip <metres>`; a `--save`'s header supplies
both otherwise, and an explicit flag beats both. `verify.py: settings
resolve`, shown to fail with the precedence inverted and with the fog end read
from the 0.95 split.

## Step 1, done

`engine/src/platform/options.*` reads an ini the way the Win32 profile API
does - case-insensitive sections and keys, `;`/`#` comments, a missing file
falling back rather than failing - and carries the 65 `[Preferences]` key
names. `Non`/`Oui` are understood beside `0`/`1`, since that is what the
French build's menu writes. A key under `[Preferences]` the engine never reads
is REPORTED, because a typo in a hand-written file is otherwise silent.

`verify.py: options file` asserts the 65 the binary reads, that the port's
table is the same SET and not merely the same size, and the reader's behaviour
on a file with a `[User]` section, mixed case, a French `Non`, a comment and a
misspelt key. Shown to fail by misspelling one key in the port's table: the
counts stay 65 and 65 while the set comparison goes False.

Nothing is wired to the engine yet - that is step 2.

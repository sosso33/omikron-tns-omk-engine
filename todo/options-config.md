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
| `displayshadows` | option row 5, Non/Oui |
| `SoftwareMode`, `GFXCard`, `screen_x`, `screen_y`, `window` | the device rows 2 and 8 |
| `music`, `DialogAttenuation`, `FxAttenuation`, `AmbientAttenuation` | the audio rows 10-12 |
| `displayframerate`, `debug_zones`, `display_path`, `viewer`, ... | the debug set |

**Two settings are menu-only and have no ini key**: the crowd density (row 6)
and the level of detail (row 7). `displaypassersclock` is a DEBUG display, not
the density - the labels mislead, and the exact strings had to come out of the
data section rather than IDA's truncated names (`aDisplaypassers` is really
`displaypassersclock`, `aAmbientattenua` is `AmbientAttenuation`). Their
apply-hooks write globals; density's is the one `Slider_Init` reads as
`39 * (5 - density) * h[3]`.

## Steps

| # | step | state |
|---|---|---|
| 1 | the reader: `[Preferences]` with the game's own key names | **DONE** 2026-09-05 |
| 2 | wire what the port already has - density, clipdistance | **open** |
| 3 | the sky: establish whether one exists in the data at all | open |
| 4 | fog | open |

Step 3 is research and may end in "narrowed": `PORTING` records that fog has
**no reachable evidence tier**, because the captures cannot validate pixel
values. The one hard datum so far points at no sky in an interior - the
dialog-402 captures are black where the render has holes, "because the game
does not clear to a sky either" - but that is an apartment and says nothing
about a street.


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

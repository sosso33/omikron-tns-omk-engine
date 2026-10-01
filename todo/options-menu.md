# The OPTIONS MENU, live - and fullscreen

Opened 2026-10-01. The reader: *"I would like a fullscreen mode. Then, the
implementation of the options menu, with graphics options actually changing
the graphics (resolution, level of details, crowd density,...). For now, only
use the real game options."*

So: screen 35 drawn and driven from the start menu, every row the game's own
(the 74-row table, `tables/ui.json`), and the Video page's rows applied to the
running viewer. Nothing from `[Enhancements]` goes in the menu.

## What was read for it (2026-10-01)

All from the raw listing, by address; none of these has a `proc` label except
the row hook.

| address | what |
|---|---|
| `0x0047BB40` | the START MENU's Options panel (`0x004CF420`) `+4` enter hook: `UI_FocusScreen(35)` and both panels slide in. Screen 29's open callback had already `UI_LoadScreen(35, -1, -1)`, so the options are RESIDENT under the menu and the entry is a focus change - the answer `todo/start-menu.md` §4 step 1 asked for |
| `0x00490D50` | screen 35's OPEN callback: param 0 when the SNEAK (screen 9) is up, 1 when the START MENU (29) is. Param 1: rows list x 70, prompt list x 120, the rows coloured (255, 100, 70) (`byte_4DE0D8..DA`), the prompt white, no cursor (`0x40000200` cleared), panel slid OUT. Param 0: x 100 / 160, both coloured, cursor on, shown and slid in. **It ends by clearing bit 1 and SETTING bit 2 of every row's `+132`** |
| `0x00492DA0` | `Opt_RowInput` (already in `readable/`). With `+132 & 2` set a change calls the row's APPLY hook at once and clears the dirty bit; without it, it only marks the row dirty. Since the open callback sets bit 2 on all 74, **every change in the running menu applies LIVE**. Either way it sets `dword_9103C8`, the dirty latch |
| `0x00492AD0` | page 1's panel hook. Param 1: BACK focuses 29 and slides the page out. Param 0: LEFT/RIGHT focus the sneak (9), BACK or CLOSE close it |
| `0x00492A70` / `0x00492AA0` / `0x00492AB0` | PAGE 0 (`0x004DD3D0`), the prompt every sub-page's Retour and BACK pass through: its builder bounces to the root unless the latch is set, and then shows list `0x004DD3B0` - **"Sauvegarder les options" / Oui / Non** (`IAM\Options` 71 / 61 / 60), Non selected. `Oui` (`0x492AB0`) is `sub_4092A0`, the SETTINGS-ONLY SAVE, then the root; `Non` descends to the root; leaving page 0 clears the latch |
| `0x00493380` | the ROW DRAW HOOK, `+20` of all sixteen row widgets. The label in `[x, x + w/2 - 20]`, the value in `[x + w/2 + 20, x + w]` left-aligned; a slider is a mode-1 quad `2 * value` wide over the middle half of the row, coloured (255, 245 - 245 t, 0); a header, back or defaults row is one block over the whole row |
| `0x004910B0` | `Opt_LayOutPage`: rows from y 80 (param 0) or 120 (param 1), spread over 280 px (140 for three rows or fewer) |
| `0x0048FA50..0x0048FDE0` | the Video page's apply / read hooks: resolution re-sets the display mode, clip `dword_90E194` (capped 200) then re-applies it to the scene, sky / shadows / street / detail one byte each, Accel 3D re-picks the driver and re-reads rows 3..7 |
| `0x0043AE70` | the resolution list: DirectDraw modes of 16 bpp, at least 640x480, labelled `"%d x %d x %d bpp"` |

## Steps

| # | step | state |
|---|---|---|
| 1 | FULLSCREEN: `--fullscreen` / `--window`, the game's own `[Preferences] window` key, F11 to toggle | **DONE** 2026-10-01 - all three backends; the desktop's mode, the frame scaled in at its aspect. Vulkan remakes its swapchain when the window's extent changes |
| 2 | the menu MODEL: `OptionsMenu` (`ui/options.*`) - pages, page 0's prompt, `Opt_RowInput`, the read and apply hooks over a `SettingsBlock` | **DONE** 2026-10-01 |
| 3 | DRAWN: the row hook and the layout in the composer (`ScreenComposer::drawOptions`) | **DONE** 2026-10-01 |
| 4 | WIRED: the start menu's Options and the SNEAK's Options tab host it; the Video rows reach the running viewer; `Oui` writes the settings header | **DONE** 2026-10-01 |
| 5 | a check, shown to fail | **DONE** - `engine: options menu`, red under both mutations with its own figures: bit 2 not set (the header keeps 50 m) and page 0 never prompting (4 pages, no file) |
| 6 | PLAY - the reader's pass | waiting; `todo/play-test.md` 13 |

## What each Video row does in the viewer

| row | applied |
|---|---|
| 2 Résolution | LIVE: the framebuffer, the interface's scale and the 3D target are remade between frames (software, GLES, and Vulkan through `vulkanResize`); a window follows the size, fullscreen keeps the desktop. The list is the host's display modes of at least 640x480, plus the running size. NOT taken back at start from the save header, because both shipped saves say 640x480 - `--res` or `screen_x/screen_y` in the ini is how a size survives a restart |
| 3 Distance de clipping | LIVE: the cull, the fog and the splits (`--clip` or the unlimited enhancement hold it) |
| 4 Ciel / 5 Ombres / 7 Niveau de détail | LIVE (`--sky`, `--shadows`, `--detail` hold them) |
| 6 Activité dans les rues | at the NEXT AREA LOAD, as `Slider_Init` reads it (`--density` holds it) |
| 8 Accélération 3D | SHOWN (the running backend, then `Rendu logiciel`) and NOT switched: the viewer picks its renderer at start |

The other pages' rows apply into the same block, which a slot save and `Oui`
write to the header; the viewer reads the fight and shoot difficulty and the
fight camera from it when they next start, and the volumes and mouse rows from
nowhere yet.

## Not done, labelled

* the keybinding rows (type 3) read no key and show no value, and `Défaut`
  (type 5) restores nothing - both need the key-name table at `0x004D10CC`;
* the panels do not SLIDE (`Ui_SlidePanelFrom`), they appear;
* the sneak's cursor highlight (`0x40000200`, set under param 0) is not drawn
  on the options rows;
* the slider is the software I2D back end's flat 50% fill; the D3D path's
  gradient to (255, 245 - 245 v/100, 0) is not drawn.

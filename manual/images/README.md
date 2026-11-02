# Illustrations

Two kinds of picture, per `manual/README.md` rule 6, and no third.

## Captures of the original engine — NOT here

They live in [`../../traces/frames/`](../../traces/frames/) and are referenced
from there. They are the original game's own framebuffer, grabbed under
CrossOver by `tools/goldentrace.py capture` and recovered by `tools/frame.py`,
which **refuses rather than degrades**: a 2x Retina grab is accepted only if
every 2x2 block is uniform. They are input. Never edit, crop or re-encode one
into this folder.

The ones the manual uses:

| file | what it is |
|---|---|
| `menu-22.png` | the start menu, 640x480, one of three grabs of the same screen |
| `dlg402-47.png` | conversation 402 in Kay'l's apartment through camera 4555, parked at the end of its 160-frame travel; 640x352 of picture inside the 1.818:1 letterbox |

## Renders by the port — here, each with its command

Every file below is reproducible. `$DATA` is your game data tree
(`python3 tools/omkpaths.py` prints what resolved where). The dumps are raw
RGB565 framebuffers; `tools/frame.py:write_png` turns one into a PNG.

### `dlg402-port-render.png`

The same set, through the same camera, as `traces/frames/dlg402-47.png` — drawn
by the port's software rasterizer. The capture also carries Telis, the props
and a subtitle; the render draws the set alone, which is exactly the asymmetry
the silhouette metric in `verify.py: engine: silhouette` is built around.
Re-run on 2026-09-18 its pixels differ from the 2026-09-07 file because the
port now dithers by default (`docs/ASSETS.md` §4); `--no-dither` reproduces the
undithered frame for laying beside the older one.
Re-run on 2026-11-02 it differs from the 2026-09-29 file in **4 pixels of
307 200**, too few to see; which of the commits since moved them was not
traced.

```sh
cd engine && ./build/omk-play "$DATA" ../tables --scene Aapkayl \
    --eye 3526,1015,-905 --at 3412,1032,-882 --fov 83 \
    --letterbox --frames 1 --dump /tmp/dlg402-port.bin
```

### `anekbah-street.png`

Adventure mode: Kay'l standing in Anekbah's main street with the procedural
crowd, 800x600, drawn after 120 ticks so the pedestrians have walked.

Re-run on 2026-09-07 it carries three things it did not when it was first
taken: the set's **ambient emitters** (the flame on the left, the neon), the
**lights** the decor's own `.3DO` table supplies to the moving population, and
the painted **sky**. Same command — the picture changed because the port did.
Re-run on 2026-09-18 it changed again, for the same reason: the characters now
cast the engine's **blob shadows** (option row 5, on in the save's settings),
and the frame carries the **dither** the original's render states turn on.
Re-run on 2026-11-02 it changed in 96 612 of its 480 000 pixels, most
visibly in how Kay'l is lit: his armour reads grey-green where it read blue.
The commit that changed how a CPU-posed player is lit is `6c12852`, whose
message records that his normals had been turned by his bones but not by his
facing since his lighting arrived, so the set's lights fell on him as if he
faced the way his model is authored. The software reference poses on the CPU,
so it took that fix. The walkers behind him moved by a few pixels too; that
was not traced to a commit. **The command now names `--res 800x600`**:
without it the run dumped a 640x480 frame on 2026-11-02, and the picture has
always been 800x600.

```sh
cd engine && ./build/omk-play "$DATA" ../tables \
    --save ../traces/save-appart.bin --area 0 --stand 1804,0,-6890,336 \
    --res 800x600 --frames 120 --dump /tmp/anekbah.bin
```

### `menu-text-port.png`

The start menu's four labels, drawn by the ported `Text_DrawRun` out of the
game's own `.FNT` fonts, at the coordinates the widget tree gives them —
6132 pixels painted. Lay it over `traces/frames/menu-22.png`.

```sh
engine/build/run_text_draw "$DATA" tables/ui.json /tmp/menutext.bin
```

## Converting a dump to a PNG

```sh
python3 - /tmp/dump.bin manual/images/out.png 640 480 <<'PY'
import sys, struct
sys.path.insert(0, "tools")
import frame
w, h = int(sys.argv[3]), int(sys.argv[4])
data = open(sys.argv[1], "rb").read()[-w * h * 2:]     # some tools prefix a header
out = bytearray()
for v in struct.unpack("<%dH" % (w * h), data):
    r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
    out += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
frame.write_png(sys.argv[2], w, h, bytes(out))
PY
```

Note what that expansion is for: it is **for looking at**. Comparisons between
a render and a capture are made **in 565, never in 888** — `docs/PORTING.md`
§A3 — because a capture's 8-bit values are the host's expansion and not the
game's data.

## When these were last refreshed

**2026-11-02**, against commit `cb957fef` (with the day's uncommitted
documentation changes on top, none of them in `engine/`), by re-running every
command above with a fresh `make play` (`SDL_VIDEODRIVER=dummy`; dumps to a
scratch folder; PNGs through `frame.write_png` as below). All three still run:
the dialogue render presents its 1 frame, the street its 120 (twice, the two
dumps byte-identical: a `--frames` run passes no seed, so the crowd is the
same every time - `todo/drift-audit.md` T5), and the text render still paints
exactly **6 132** pixels. `menu-text-port.png` came out **byte-identical** and
was not replaced; `dlg402-port-render.png` (4 pixels) and
`anekbah-street.png` (above) were replaced.

The refresh before that, **2026-09-29**, against commit `200e4b1`, by re-running every command above
with the current build (`SDL_VIDEODRIVER=dummy` so no window opens; the dumps
written to a scratch folder, not `/tmp`). All three still run: the dialogue
render presents its 1 frame, the street its 120, and the text render still
paints exactly **6 132** pixels. **All three PNGs came out byte-identical to
the previous ones**, so none was replaced - which also says the day's speed
work (`todo/optimization.md` steps 16-18) left the software street at frame 120
unchanged.

The previous refresh, 2026-09-18, was recorded against commit `4b71e07`, which
no longer resolves: the history was rewritten on ~2026-09-19 and every later
SHA changed.

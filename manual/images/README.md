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
the silhouette metric in `verify.py: engine silhouette` is built around.

```sh
cd engine && ./build/omk-play "$DATA" ../tables --scene Aapkayl \
    --eye 3526,1015,-905 --at 3412,1032,-882 --fov 83 \
    --letterbox --frames 1 --dump /tmp/dlg402-port.bin
```

### `anekbah-street.png`

Adventure mode: Kay'l standing in Anekbah's main street with the procedural
crowd, 800x600, drawn after 120 ticks so the pedestrians have walked.

```sh
cd engine && ./build/omk-play "$DATA" ../tables \
    --save ../traces/save-appart.bin --area 0 --stand 1804,0,-6890,336 \
    --frames 120 --dump /tmp/anekbah.bin
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

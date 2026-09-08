# Enhancements - what the original never had, all OFF by default

The reader's rule (2026-09-08): anything the 1999 game did not draw stays
optional, behind a launch flag on `omk-play` or a key in the config file's
`[Enhancements]` section - a category of its own, beside the engine's
`[Preferences]` and this port's `[Options]` stand-ins for real menu rows.
Only the Vulkan backend draws them; the software reference stands where D3D
stood and draws what `sub_4638C0` told D3D to (`docs/ASSETS.md` 4).
Each one gets a `verify.py` check that pins the default OFF in the source and
measures the enhancement's own property on the GPU, shown to fail.

| # | enhancement | key / flag | status |
|---|---|---|---|
| 0 | anti-aliasing (MSAA 2/4/8) | `antialiasing = N` / `--aa N` | **done 2026-09-08**, `ed349ca`; `engine: anti-aliasing` |
| 1 | bilinear texture filtering. The colour key (flag 0x800, black) travels in the texture's ALPHA so a filtered sample is premultiplied: discard below 0.5, divide by alpha above - no dark fringe, and the nearest path is bit-identical to before | `texturefiltering = bilinear` / `--filter bilinear` | **done 2026-09-08**; `engine: texture filter`; judged by eye on Aapden's floor stain |
| 2 | mipmaps (trilinear) and anisotropic filtering, generated at upload; the alpha key averages correctly into the chain | `texturefiltering = trilinear`, `anisotropy = N` / `--filter trilinear --anisotropy N` | todo |
| 3 | interface scaling: linear or integer instead of nearest for the 640x480 layer | `uiscaling = linear|integer` / `--ui-scaling` | todo |
| 4 | unlimited draw distance: options row 3 is a CAP the port already runs the visible-set walk from; 0 lifts it. Authored risk: the sets end inside the fog | `clipdistance = 0` under `[Enhancements]` / `--clip 0` | todo |

Known limit of 1 and 2, to be judged by eye: the sets sample sub-rectangles
of shared atlases (the Anekbah signs), and a filter reaches half a texel past
a rectangle's edge into its neighbour. Nothing per draw says where the
rectangle ends, so it is not clamped.

# The 1999 official engine spec, audited line by line

A reader supplied the technical bullet list from the game's official site, via
an Internet Archive copy of the 1999 original. It is **external evidence**: not
derivable from this tree, and written to sell rather than to specify — several
lines state the obvious ("Moteur 3D gérant décors intérieurs et extérieurs"),
which in 1999 was worth saying. Stating the obvious is not the same as being
wrong, and none of these lines turned out to be false. But it is the developers describing their own engine, and several
lines land on things this repo has read, has half-read, or has never looked at.

Audited against what the repo can actually show. The column that matters is the
last one.

| the 1999 claim | this repo | state |
|---|---|---|
| *Moteur 3D temps réel, SVGA, 16 bit (65000 couleurs)* | the framebuffer is RGB565, 65536 colours, and `PORTING` A2 fixes it as the boundary format | **confirmed** |
| *Compatibilité Direct 3D (3DFX et Power VR)* | Direct3D throughout; and the engine branches on a display-driver index — `sub_45EF50` gates the mirror pass to hardware mode | **confirmed** |
| *Moteur écrit en Assembler et en C* | consistent with the listing | consistent |
| *Rendu Painter Back to Front et Front to Back* | `Render_FlushBuckets` walks `0..0x4000` **ascending, once**. The key's two depth bits order near (`0x80`) before far (`0x1000`), so the shipped walk is one direction, not two | **partly refuted** — one walk ships; whatever the second order was, it is not in this build |
| *Gouraud mappé* | `D3DFVF_DIFFUSE` per vertex plus a texture; the baked dword is a colour, not a brightness (ASSETS 4c) | **confirmed** |
| *Z-mapping (mapping exact)* | `D3D_SetRenderState(dev, 4, 1)` — state 4 is `D3DRENDERSTATE_TEXTUREPERSPECTIVE`. "Mapping exact" is perspective-correct texturing | **confirmed**, and the marketing phrase decodes |
| *Personnages en faces déformables* | the `.3DM` face morph, 777/777 files | **confirmed** |
| *Système de tri de face par Arbre BSP* | the shipped face sort is the 14-bit bucket key, and byte accounting now shows there is **no room** for a tree in a `.3DO` | **one candidate refuted**, 2026-09-05 — see below |
| *Algorithme de collisions de grande précision* | the walker and the collision soups | true, not specific |
| *Animations par rotation et par Morph en Motion Blending* | quaternion tracks (`.ani`/`.CTL`) **and** the `.3DM` morph, with the two-sided fade `min(30, frames/4)` at a k/256 slerp — which is the "motion blending" | **confirmed**, and it names the blend |
| *Multilights* | the `.3DO` header carries a light table at `+40` with a count at `desc+232`. The port reads the OFFSET AND THE COUNT and **nothing reads the records** | **REAL AND UNREAD** — see below |
| *Transparence/Opacité* | two blend modes, additive (211 meshes) and multiply (6), plus the cutout path | **confirmed** |
| *Flat/Gouraud Fog* | linear fog, `FOGTABLEMODE` 3, density 1.0, range from the clip distance (ASSETS) | **confirmed**, and it is step 4 |
| *Chargement dynamique des données* | `Area_TickLoad`'s nine staged cases | **confirmed** |
| *Animations en Motion Capture* | consistent with the clip data; nothing in the files says "mocap" either way | true, not checkable here |
| *Motion Capture sur des visages temps réel, entrelacé avec les voix* | the `.3DM` frame record is face vertices, then nodes, then a `float[3]`, **then the audio** — literally interleaved with the voice, one record per frame | **confirmed, and it explains the format's shape** |

## The two leads worth chasing

### 1. Multilights — 6244 light records nobody has read

`Mesh3doHeader` has carried `lightOff` (`+40`) and `lights` (`desc+232`) since
the format was decoded, and the port parses both and then ignores them. Over
the shipped `MESHES` tree:

    635 models, 375 of them with lights, 6244 lights in total,
    max 470 in Qalisar.3DO

That is not a vestigial field. And it raises a question the repo has not asked:
the sets are shaded by a colour **baked into every vertex**, so what are 6244
runtime lights for? The obvious candidate is the thing that cannot have baked
light — the **characters**, who walk through those sets — but nothing here has
established that, and the records themselves are undecoded. `verify.py:
mesh lights` pins the counts so the claim has a number behind it.

### 2. BSP — one candidate refuted, 2026-09-05

The face sort this repo has read end to end is the 14-bit bucket key and
`Render_FlushBuckets`'s single ascending walk, which is not a BSP by any
reading. Three possibilities were open, and exactly one of them was testable
from this tree — that a tree sits in the `.3DO` where the nine known header
offsets do not reach. `tools/domap.py` is that test, built the way
`chunkmap.py` accounts for `IAM\AREA`:

    635 models, 33370836 bytes
    99.9986% claimed by documented structures
    460 bytes unexplained, in 24 files
    611 of 635 accounted for byte for byte

**There is no room for a BSP.** A tree over Anekbah's 16188 meshes would be
tens of kilobytes; what is left is 460 bytes across 33 MB, the largest single
run 80 bytes, and their contents are short ascending integers — index lists,
not nodes.

So the two surviving explanations are both outside this tree: an **earlier or
internal build** (the same list claims two painter directions where one ships,
which leans that way), or the phrase describing the bucket sort or the mesh
hierarchy loosely for a page written to sell. Recorded as the refutation of
one candidate, not as a verdict on the sentence — what this cannot see is a
tree the engine BUILDS at run time (nothing in `Read3DO_Init` does, but that is
one function), or anything in a build this repo does not have.

**The accounting paid for itself on the way.** The descriptor is **328 bytes at
+44**, so the header and descriptor together are exactly 372 — and the first
table begins at 372 in all 635 files, which is what fixes it rather than
assumes it. A first pass claiming only the 244 bytes up to the counts left 84
bytes unexplained in every single model, and that is the shape of a size that
is too small rather than of a hidden structure: **a real gap does not appear
identically everywhere.** `verify.py: .3DO bytes`.

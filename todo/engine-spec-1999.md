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
| *Système de tri de face par Arbre BSP* | **nothing in this repo evidences a BSP.** The shipped face sort is the 14-bit bucket key; the `.3DO` header's nine offsets include no tree, and the meshes' parent/child/next is a scene hierarchy, not a BSP | **not corroborated** — see below |
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

### 2. BSP — claimed, and not visible

The face sort this repo has read end to end is the 14-bit bucket key and
`Render_FlushBuckets`'s single ascending walk, which is not a BSP by any
reading. Three possibilities, in the order they should be tested:

* the copy describes an **earlier or internal** build (the same list claims two
  painter directions where one ships, which leans this way);
* "arbre BSP" describes the **mesh hierarchy** loosely, for a marketing page;
* there is a tree in the `.3DO` that the nine known offsets do not reach — the
  test for which is byte accounting over a `.3DO`, the way `chunkmap.py` does
  it for `IAM\AREA`. That does not exist yet and would settle it.

Until one of those is done, the honest state is **not corroborated**, and the
CLAUDE.md 1 rule applies in its usual form: a document is a hypothesis about
the code, and only the code decides.

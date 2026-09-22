# The `MeshNameIndex` adoption in `play.cpp` — APPLIED 2026-09-22

**Integrated 2026-09-22**: the five edits below are in `play.cpp`; `engine: character shadow`, `fitted shadows`, `mapped shadows` and `mesh name index` green and unchanged; shown to fail as prescribed (the fallback forced to the index and both builds dropped: `character shadow` and `fitted shadows` red). The frame figure is still unmeasured, as the last paragraph says.

Written 2026-09-17 by the Vita-port session (`todo/handoff-vita.md` §2 item 1).
`engine/backends/sdl/play.cpp` was owned by another session at the time, so the
index landed with **no consumer** and this file is the consumer, for whoever
holds that file next. Until it is applied, `findMeshContaining` is still doing
the full name scan every frame and nothing is faster.

**What is already in**: `omk::MeshNameIndex` in `engine/src/o3de/shadow.{h,cpp}`,
`MeshNameIndex::shadowNames()` beside the bone table, `engine/tools/
meshidx_equiv.cpp`, and `verify.py: engine: mesh name index` (green, and shown
to fail four ways).

**Why it is worth applying**: per call, measured on an M1, PSH_FN 924 ns scan ->
**31 ns** index, FSH_FN 871 -> 32, HO1_FN 106 -> 16, JEN_FNM 125 -> 16. The
handoff charges the scan 0.09 ms a frame; 10 bones x 2 calls a bone on the
fitted path, plus 2 for each crowd walker's feet, is where it goes.

---

## The four edits

**1. `CharModel` gains the index** (`play.cpp` ~3520, beside `face`):

```cpp
        omk::FaceMesh face;
+       // Built once per model name, and the model is loaded once and shared -
+       // see `o3de/shadow.h`. It stores mesh INDICES, so it must be rebuilt
+       // if `meshes` is ever replaced; here it never is.
+       omk::MeshNameIndex boneIdx;
```

**2. built in `charModelFor`** (~4455, right after `m.face` is set):

```cpp
             m.face = omk::faceMeshOf(m.meshes);
+            m.boneIdx.build(m.meshes, omk::MeshNameIndex::shadowNames());
```

**3. the player's own array** (~6620, where `playerMeshes` is filled). The
player is not a `CharModel`, so his index is a sibling local — declare
`omk::MeshNameIndex playerBoneIdx;` beside `playerMeshes` and, wherever
`playerMeshes` is assigned:

```cpp
             if (const auto mh = omk::readHeader(md)) playerMeshes = omk::readMeshes(md, *mh);
+            playerBoneIdx.build(playerMeshes, omk::MeshNameIndex::shadowNames());
```

There is more than one assignment to `playerMeshes` if the model can change
(a `player.become`); **every** one needs the rebuild beside it, and that is the
one place this patch can go wrong silently — a stale index answers about the
model he used to wear. Grep `playerMeshes =` and do them all.

**4. `castBones` takes the index** (~16636), and its two `findMeshContaining`
calls go through it:

```cpp
                 const auto castBones = [&](const std::vector<omk::Mesh>& meshes,
                                            const std::vector<float>& at, int lvl,
-                                           int root, const omk::Geometry* ref) {
+                                           int root, const omk::Geometry* ref,
+                                           const omk::MeshNameIndex* idx) {
```

then at both sites inside it (~16658 and ~16675):

```cpp
-                            const int mi = omk::findMeshContaining(
-                                meshes, omk::kShadowBones[static_cast<std::size_t>(bi)].bone, root);
+                            const char* bn = omk::kShadowBones[static_cast<std::size_t>(bi)].bone;
+                            const int mi = idx && idx->built() ? idx->find(bn, root)
+                                                               : omk::findMeshContaining(meshes, bn, root);
```

and the two call sites (~16727, ~16732):

```cpp
-                    castBones(playerMeshes, playerMeshAt, detail, -1, nullptr);
+                    castBones(playerMeshes, playerMeshAt, detail, -1, nullptr, &playerBoneIdx);
-                        castBones(up->mo->meshes, up->meshAt, detail - 1, up->shadowRoot,
+                        castBones(up->mo->meshes, up->meshAt, detail - 1, up->shadowRoot,
+                                  /* ... */ &up->mo->boneIdx);
```

**5. the crowd's feet** (~15497):

```cpp
                         const int fi[2] = {
-                            omk::findMeshContaining(p.mo->meshes, "Piedg", lodRoot),
-                            omk::findMeshContaining(p.mo->meshes, "Piedd", lodRoot)};
+                            p.mo->boneIdx.find("Piedg", lodRoot),
+                            p.mo->boneIdx.find("Piedd", lodRoot)};
```

The `idx && idx->built()` fallback is deliberate: an unbuilt index answers -1
for everything, which would silently draw NO shadows, and that is exactly the
failure `findMeshContaining`'s own docs warn about for the equality test. With
the fallback a missed `build` costs speed and not correctness.

---

## How to prove it after applying

Three checks are the real before/after, and they were green against the full
scan on 2026-09-17:

```
python3 tools/verify.py --only "engine: classic shadows" "fitted shadows" "mapped shadows"
```

They must stay green and **unchanged** — the index is not supposed to move a
pixel. Then `engine: mesh name index` still covers the index against the scan
over the adversarial corpus.

**And SHOW it fails**: drop the `build` call in `charModelFor` (edit 2). With
the fallback in place that is not a failure at all, only a slow path, so the
honest mutation is to change the fallback to `idx->find(...)` unconditionally
and then drop the build — the shadow checks must go red. If they do not, the
index is not on the path the checks exercise and the adoption has not landed.

**The one thing nobody has measured**: what this is worth IN A FRAME. The
per-call numbers are from `meshidx_equiv`; the frame figure needs a capped A/B
on the street start, old / new / old back to back with the spread quoted
(`todo/handoff-vita.md` §4). Do not quote 0.09 ms as though it had been
confirmed saved.

# Bodies posed on the GPU - P5 of the Vita port

Asked for 2026-09-24, after the reader measured the depth tie off and found it
*"a little smoother, [but it] does not make a great difference"*. The console's
city frame is ~100 ms of work without the tie, against a 33 ms budget
(`vita-port.md`, entry of 2026-09-23 morning), and the largest share is the
BODIES: every character is posed on the CPU, lit on the CPU and its whole
vertex buffer sent to the GPU again, every frame.

| where, per city frame on the console (2026-09-23) | ms |
|---|---|
| vertex uploads - the posed bodies, ~6 MB a frame | 19-25 |
| staged skin (`applyPose` over the scene actors) | 10-11 |
| the crowd's `ped apply` + `ped compose` | 11-17 |
| the crowd's light (`applyLights`, per corner) | in the above; 116x the M1 in `omk_bench` |

`omk_bench` on the console: compose 7.1, apply **102.4**, light 35.5 ms a
frame inline for 45 bodies - apply and light are the memory-bound half, and
they are exactly what a vertex shader does for free.

## What a body IS - why this is cheap to move

A character is RIGID PER MESH (`actor/pose.h`): each of its meshes follows one
node's transform, `rot[m]`, `pos[m]`, and a corner is posed by
`pos[m] + rot[m] * (rest - rest[m])`. Only the FACE mesh is morphed per vertex,
and only while a line plays (130 vertices). So the GPU needs, per body:

* its REST geometry, uploaded ONCE, with each corner's mesh index as an
  attribute (`Geometry::cornerMesh`);
* per frame, ONE 3x4 matrix per mesh - 20 for Kay'l, 76 for `PSH_FN` - as
  uniforms: ~1-4 KB against the ~130 KB a posed body sends today;
* the face's 130 posed vertices, where a line animates it.

`composePose` (the bone matrices, 7 ms for 45 bodies) stays on the CPU: it is
what the shadows, the head look and the fight read.

## What must not change

* **The picture.** A GPU multiply is not the CPU's bit for bit, so the proof
  is the same one the Vulkan backend's was: render the same frame both ways on
  the Mac and require the COVERAGE to agree (Vulkan's: 0.995), and every
  per-vertex colour law compared numerically where it can be.
* **The other backends.** The software reference and Vulkan keep posing on the
  CPU. The renderer says whether it can pose (`Renderer::posesBodies()`), and
  the frontend picks the path - so `verify.py`'s software renders are untouched
  by construction.
* **The depth tie.** A body's tie class is its MESH, and the tie already
  replays a rigid body's losers unchanged between revisions
  (`Geometry::tieRigidFrom`) - coincident faces inside one rigid mesh stay
  coincident under its transform. So the losers can be written into the REST
  buffer once. To be measured in step 1, not assumed.
* **The Vita's shaders.** Every new program must reach the shader CACHE: a
  console without `libshacccg.suprx` runs only cached `.gxp`. The reader has
  the compiler, so one console run with it makes the cache, which then travels
  in the VPK as the five existing ones do.

## Steps

| # | what | state |
|---|---|---|
| 0 | this file | **done 2026-09-24** |
| 1 | GLES: the POSING PROGRAM - a vertex shader that applies a per-mesh 3x4 from a uniform array to a rest VBO carrying the mesh index; `Draw` gains the pose (matrix pointer and count) and the rest geometry; `Renderer::posesBodies()`. Proof: a probe renders one posed crowd body through the CPU path and the GPU path on the Mac and compares coverage | **done 2026-09-24** - see below |
| 2 | the crowd's LIGHT in the shader: `vertexlight.*`'s law (`-(N.L)` over a linear falloff through the `(t*c)>>8` ramp) with the lights a body reaches as uniforms, the normal rotated by the mesh's matrix. Proof: per-vertex colours against `applyLights`, and the frame | |
| 3 | the WALKERS take the GPU path when the renderer offers it: no `applyPose`, no `applyLights`, no upload - the matrices only. Proof: the street at density 4, CPU path against GPU path, coverage and colour | |
| 4 | the STAGED bodies (scene actors, the speaker) - with the face's morph as its own small dynamic buffer | |
| 5 | the PLAYER | |
| 6 | the console: the new programs into the shader cache, and a city log | |

Each step ends in a commit and a report, and waits for the reader's go.

## Step 1, done - the posing program

* **The boundary**: `Draw::meshPose` / `meshPoses` (one 3x4 a mesh of the
  model) and `Renderer::posesBodies()`, false everywhere but GLES.
  `omk::meshAffines` (`actor/pose.h`) folds a pose AND the body's placement
  into those affines, R taken from `qrot` of the three axes so it cannot
  disagree with `applyPose` about the conjugate.
* **GLES**: the scene program compiled a second time with `OMK_POSED` -
  `attribute float aSlot`, `uniform vec4 uPose[96]` (32 slots). A body's rest
  geometry is uploaded ONCE, STATIC, each corner carrying its mesh's SLOT (the
  meshes it uses, numbered densely - 19 for a crowd skeleton of a 76-mesh
  model). A body with more than 32 is posed by the backend on the CPU into a
  geometry of its own. The pose goes in as a `vec4` array on purpose: vitaGL
  copies one straight, where a float array overran the heap (2026-09-18).
* **The tie**: resolved on a private rest copy with `tieClass` = the mesh,
  a new revision each frame naming the last as rigid, so it replays; what it
  degenerates is written into the static buffer once.
* **Proof** (`gles_probe --pose`, `verify.py: engine: gles pose`): Kay'l on
  line 125338 and a crowd skeleton, turned and moved, CPU-posed against
  GPU-posed through the same backend: coverage 1.0000, 0-6 pixels of 307200
  differing, over four consecutive frames with the tie on; the posed tie
  degenerates 142 and 334 triangles, the CPU path's own counts. Three
  mutations caught (no rotation 0.19, one slot 0.49, the winner degenerated
  0.96). Blind on the Mac to the tie's EFFECT - the M1 keeps the first-drawn
  face without it.
* **Not yet on the Vita**: the program links at start with the others, so a
  console WITH `libshacccg.suprx` compiles it into the cache on the first run;
  one without it logs `no posing program` and poses on the CPU as before.
  Nothing submits a posed draw until step 3.

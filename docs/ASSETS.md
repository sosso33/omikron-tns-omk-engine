# Meshes, textures and how a conversation reaches them

Start at [`../CLAUDE.md`](../CLAUDE.md) for the working practice these
findings depend on.

How `IAM\DIALOG` → `MORPH/*.3DM` → `MESHES/*/*.3DO` + `.3dt` fit together, plus
what is known about the mesh and texture containers.

The `.3DO` / `.3DT` field layouts here come from `Omikron.txt`, the notes
shipped with an earlier Unity importer. Those were **not** derived from the
executable, so they are treated as reported rather than proven — except where
this document says something was checked against the shipped data.

---

## 1. The chain

```
IAM\DIALOG chunk                  one conversation
   └── DialogNode.name            a 6-character asset id, e.g. "071348"
        └── MORPH/071348.3DM      the recorded line:
                                    per-frame face vertices + normals
                                    per-frame skeleton quaternions
                                    per-frame ADPCM voice, 30 fps
             ├── vertexCount  ==  the character's face-mesh vertex count
             └── nodeCount    ==  the character's mesh count - 1
                  └── MESHES/PERSOS/BOZ_FNM.3DO   topology, hierarchy, materials
                       └── MESHES/PERSOS/BOZ_FNM.3dt   the textures
```

**A `.3DM` carries no topology at all** — only per-frame vertex positions and
normals. The triangle list, the UVs, the material assignment and the mesh
hierarchy all live in the character's `.3DO`; the `.3DM` is a stream of
replacement vertex data for one mesh in it.

### Which mesh

**verified.** Every character in `MESHES/PERSOS` has a mesh named `*Visage`
("face"). Its vertex count is what a `.3DM` uses:

| face-mesh vertex counts in PERSOS | 130, 132, 134, 141, 129, 119, 148, 38, 154 |
|---|---|
| vertex counts across the 708 `.3DM` files | 130, 132, 134, 141, 129, 148, 38, 135 |

The overlap is near-total. `119` and `154` belong to background characters with
no dialogue; `135` appears in exactly one `.3DM`.

**verified.** `nodeCount == meshCount - 1`, which fits the division of labour:
the `.3DM` supplies explicit vertices for the *face* and a rotation quaternion
for each of the character's other nodes. Of the 64 PERSOS models with a face
mesh, 57 have a `(faceVertices, meshCount-1)` pair that matches a `.3DM`
combination; the 7 that do not are spectators and other non-speaking models.

### Resolving a line to a character

When the `(vertexCount, nodeCount)` pair is rare it identifies the model
outright, and the result agrees with the human-readable name in `DIALOGS.TAG`:

| dialogue | `DIALOGS.TAG` | 3DM | resolves to |
|---|---|---|---|
| 168 | `Boz/Fin Section 2` | 071348 | `BOZ_FNM.3DO` |
| 200 | `Soks/Base1/1` | 042262 | `SOK_FNM.3DO` |
| 217 | `Dakobah/Base 1` | 03B1F8 | `DKB_FNM.3DO` |
| 219 | `Dakobah/Base1/Magie` | 05229D | `DKB_FNM.3DO` |

22 of the 321 conversations pin a single model this way. 138 land on a shared
body type — `(130, 19)` alone matches 25 different characters — and 160 have
their `.3DM` on another CD.

That ambiguity is not a gap in the format: the counts only establish *which
models a stream is compatible with*. The game does not use them to pick a
character. The conversation header holds a scene-local `speakerObjectId`, which
`Dialog_Load` resolves through `Scene_FindObjectIndexById` into a live scene
object, and the actor record that object points at already carries its model.
`Morph_Play` (0x0041AFC0) then plays the stream onto that actor:

```c
Morph_Play(actorIndex, "071348.3dm");   /* resolved under MORPH\ */
```

So the counts are a consistency check between the stream and the model, and a
useful offline handle for matching them up — not the runtime lookup.

---

## 2. `.3DO` — meshes

Signature `OD3X`, then a 372-byte header of section offsets and counts.
Sections are located by their offsets, not by order.

```
+0    char[4]  "OD3X"
+4    int      major, minor
+12   int      offsets: materials, vertices, triangles, quads,
                        meshes, doors, cameras, lights
+232  int      triangleCount, quadCount, vertexCount
+252  int      materialCount
+264  int      cameraCount, meshCount, doorCount, lightCount
```

Records, per `Omikron.txt`:

| section | size | fields |
|---|---|---|
| material | 80 | name[20], bmp[20], tga[20], imageDataSize, ×2 unknown, bitsPerPixel, width, height |
| vertex | 32 | pos[3], normal[3], colour, lighting |
| triangle / quad | 24 | indices, UVs, materialId, face normal |
| mesh | 136 | flags, id, editorId, name[20], pos[3], parent/child/next, vertex/triangle/quad counts, bounding sphere, bounding box |
| camera | 52 | name[20], pos[3], target[3], unknown, fov |

**verified.** The camera record is 52 bytes. Read at any other stride the names
turn to noise after the first entry or two, which is how the size is checked -
`Aapkayl.3DO` yields a clean `CAMERA`, `CAMERA0` … `CAMERA14`, all at 80
degrees. Across `gamedata/MESHES`, 485 cameras carry fov 80 and 181 carry 0 while
still having sensible names and positions, so 0 most likely means "use the
default". Unlike a `DialogCamera`, a scene camera is a fixed **eye and target**,
not a move.

**verified** from the shipped data: the signature holds, and the 80-byte
material record and 136-byte mesh record both parse — mesh names come out clean
(`AgVisage`, `AgBuste`, `AgMaind`) and the material names and BMP filenames are
sensible (`FACE`/`BOZTET.BMP`, `CASQUE`/`CASQUE1.BMP`).

Points worth carrying over from `Omikron.txt`:

* A face index with the top bit set refers to the **parent mesh's** vertices —
  `index & 0x7FFF`. That is how vertex skinning works.
* A child mesh's vertex positions are relative to its parent's position.
* `materialId == -1` means the face is not drawn.
* UVs are in pixels and one byte each, so textures are at most 256×256.
* Quads are two triangles, `(0,1,2)` and `(0,2,3)`.
* Mesh flags encode render mode, transparency, mirrors, collision-only,
  scrolling UVs and so on; the notes list them with varying confidence.

`tools/mesh3do.py` reads the header and the mesh table.

    python3 tools/mesh3do.py gamedata/MESHES/PERSOS/BOZ_FNM.3DO

---

## 3. `.3DT` — textures

One `.3DT` sits beside each `.3DO` and holds that model's textures **in
material order**. Each texture is a palette followed by image data:

```
palette     3 bytes per colour (RGB)
            16 colours if bitsPerPixel == 4, otherwise 256
image data  imageDataSize bytes, as given in the material record
```

Uncompressed when `imageDataSize == 65536`; otherwise LZ-style, driven by a
control byte whose bits say literal-or-sequence. A sequence byte gives
`size = (b >> 2) + 3` and a 2-bit type: repeat the previous pixel, or copy from
1, 2 or 3-byte back-references. Full pseudocode is in `Omikron.txt`.

**verified.** For all **635** `.3DO`/`.3DT` pairs in `gamedata/MESHES`, the `.3DT`
file size equals

```
sum over materials of (paletteSize * 3 + imageDataSize)
```

exactly. That independently confirms both the 80-byte material record and the
"textures concatenated in material order" rule.

### The first byte is a literal

**verified.** The stream opens with **one literal pixel byte, before the first
group byte**:

```
data.Add(file.ReadByte()); // first byte is always uncompressed
```

That line is in the reference importer (`OmikronImporter.cs`) and missing from
the pseudocode in `Omikron.txt`. Without it every stream is desynchronised from
its very first byte, which produces textures that are still *recognisable* —
the LZ back-references keep re-establishing local structure — but shot through
with wrong-coloured blocks and streaks, and ending at the wrong pixel count.

With it, **all 2534 textures in the 635 models under `gamedata/MESHES` decode to
exactly `width * height`**. Nothing needs padding or trimming.

---

## 3b. The effect sprites — the game's particles

### `gamedata/SOUNDS/` — two files, one of which nothing can load

**Established 2026-09-01, from the binary's own path templates.** The directory
ships exactly two `.wav`s, and they are not a pair:

* **`SLIDERM01.wav` is used.** `Slider_Init` (0x00453450) pushes the literal
  `"SOUNDS\sliderm01.wav"` — the slider's motor loop, which is the same
  subsystem as `ACTOR_STATE` 7 and 8 (`MDSLIDIN` mounts, `sub_468FA0` puts the
  rider on `.CTL` group 61, `MDSLIDOU` refuses to dismount from anything but 8).
* **`pluie.wav` cannot be reached.** The whole executable contains **two**
  `.wav` path forms — that literal, and `i2d\sounds\%s.wav` for the 45
  interface sounds. There is no `SOUNDS\%s.wav` template, so no code path
  produces `SOUNDS\pluie.wav`, and the string `pluie` does not appear in the
  binary at all. Everything else the engine plays is `.ADP`: `%s.ADP`,
  `VOICEOFF\%s` and `TRACKS\%d.ADP`.

The name **does** occur in the data, which is what makes it worth writing down
rather than assuming a typo: `SCPTDATA/hamesta.Sfx` carries `pluie` — French
for rain — as one of **nine effect names at an 80-byte stride** (`chute`,
`plouf`, `blur`, `soulj`, `souli`, `pluie`, `bigsoul`, `mat1`, `noou1`). That is
a particle effect in the Hamesta scene, not a sound reference. The only other
occurrence anywhere is prose in `IAM\DIALOG` — *"parler de la pluie et du beau
temps"*.

So `pluie.wav` is an **orphan asset**: shipped, and unreachable by any path the
engine can build. It joins the six spell recipes whose gate is never 8, the
`X-Tech` shoot callback no character selects, and options page 12 — built and
unreachable. `verify.py: SOUNDS folder`.

### 3c. The sound path — DirectSound, and what the engine actually decides

**Read 2026-09-01, when the audio row was ported.** The whole of the engine's
audio is seventeen thin wrappers around DirectSound. **It does not mix.**
`Sound_Init` (0x0046C3A0) enumerates devices, creates the object, takes
`SetCooperativeLevel(hWnd, DSSCL_EXCLUSIVE)`, creates a **primary** buffer with
`DSBCAPS_PRIMARYBUFFER|DSBCAPS_CTRL3D`, queries `IDirectSound3DListener` off
it, and writes the mix format:

| field | value |
|---|---|
| `wFormatTag` | 1 (PCM) |
| `nChannels` | **2** |
| `nSamplesPerSec` | **22050** |
| `nAvgBytesPerSec` | 88200 |
| `nBlockAlign` | 4 |
| `wBitsPerSample` | 16 |

then `Play(0, 0, DSBPLAY_LOOPING)` once. Everything afterwards is a *secondary*
buffer that DirectSound's own mixer sums into that. **No loop in the image adds
two samples together**, which is why `docs/PORTING.md` B6's original audio
criterion — "the mix compared offline" — had to be corrected rather than met.

#### The two tables

**160 buffers** (`dword_53B7E8`) and **16 voices** (`unk_53B368`, 64 bytes
each). `Sound_CreateBuffer` (0x0046C740) takes the first free buffer slot and
asks for a caps word that depends on the 3D switch `dword_4CC85C`:

| 3D | flags | what |
|---|---|---|
| on | `0xB0` | `CTRL3D` \| `CTRLFREQUENCY` \| `CTRLVOLUME` |
| off | `0xE0` | `CTRLFREQUENCY` \| `CTRLPAN` \| `CTRLVOLUME` |

Note what swaps: **PAN is asked for only when 3D is off**, which is
DirectSound's own rule (a 3D buffer's pan belongs to the listener) and the
engine follows it rather than asking for both.

The choice is **branchless**, and `0xB0` is never an immediate — the image holds
`neg ecx ; sbb ecx, ecx ; and ecx, 0FFFFFFD0h ; add ecx, 0E0h`, i.e.
`0xE0 − 0x30` when the switch is on and `0xE0` when it is off. Worth stating
because searching the image for `176` finds nothing at all.

`Sound_Play3D` (0x0046CDC0) takes the first free **voice**, `DuplicateSoundBuffer`s
the source, and fills the 64-byte record:

| offset | field |
|---|---|
| +0 | the `IDirectSound3DBuffer`, when 3D is on |
| +4 | the duplicated buffer — **0 means the slot is free** |
| +8 … +19 | position `float[3]` |
| +20 … +31 | velocity `float[3]` |
| +32 | `maxDistance` |
| +36 | `minDistance` |
| +40 | `-1.0f`, written and never read |
| +44 | the source buffer's `GetFrequency`, as a float |
| +48 | flags: bit 0 in use, bit 1 looping, **bit 2 = not a 3D sound** |
| +52 | the source buffer id — **initialised to −1** by `Sound_Init` |
| +56 | the caller's owner tag |

The placement argument is `pos[3], vel[3], minDistance, maxDistance`, and that
layout is corroborated **three ways**: the copy into this record,
`Sound_SetVoice3D` (0x0046CFC0) copying the same block into
`SetPosition`/`SetVelocity`/`SetMinDistance`/`SetMaxDistance`, and the slider's
own call site (0x00456B40) passing the literals **39.0** and **585.0** — the
second occurring **exactly once** in that function and loaded twice, as
`maxDistance` and as the audibility test `d < 585.0`, so the slider is dropped
at exactly its own max distance. (First written here as 560.0 and a separate
585.0 cut-off, from misreading the decompiler's `1142046720`; there is one
literal, not two.) Passing no block at all means
`DS3DMODE_DISABLE`, which is how the interface plays a sound:
`Ui_PlaySound` (0x00482D90) calls `Sound_Play3D(id, 0, 0, 0)`.

`Sound_FreeBuffer` (0x0046C8B0) stops every voice whose +52 names the buffer
**before** releasing it — a voice is a duplicate of the original, so releasing
the original under a live duplicate is what that walk prevents.

#### The listener, and the world unit

`Sound_SetListener` (0x0046D080) takes a 48-byte struct in the engine's own
order — **position, top, front, velocity** — which is *not* `DS3DLISTENER`'s
order (position, velocity, front, top), so reading it as a straight copy swaps
the orientation for the velocity. It writes
`flDistanceFactor = 0x3CD013A9 = 0.0254` — **metres per inch** — and
`flRolloffFactor = 1.0`, leaving Doppler at whatever `GetAllParameters`
returned. So the engine tells DirectSound the world unit is an **inch**, which
is the same unit `docs/FILE_FORMATS.md` reads in the staging offsets.

When 3D is off it keeps a 2D fallback basis instead: the **front** vector
normalised, with the guard `if (len <= 0.0001) basis = 0` — a zero, not a
division by a clamped length.

#### The volume "percentage" is an attenuation

`Music_SetVolume` (0x0042BE60) and its sibling 0x0042BBB0 clamp the argument at
100 and compute `-10000 * pct / 100`. DirectSound's volume is hundredths of a
dB with 0 = unity, so **0 is full volume and 100 is silence**; the global each
feeds is initialised to 0. It is an attenuation, not a level.

#### Three functions that are not in the decompilation

`Sound_SetFrequency` (**0x0046CBF0**), `Sound_GetFrequency` (**0x0046CC30**)
and `Sound_LengthMs` (**0x0046CC70**) sit in the gap between
`Sound_SetVolume` and `Sound_FindVoice`, and are in neither `Runtime.exe.c` nor
`readable/`. Their addresses were recovered by disassembling the gap and
measuring each block back to a 16-byte alignment.

**The reason is not the prologue**, which is what this section first said.
`Sound_SetVolume` (0x0046CBB0) opens with the identical `A1 <ppDS>` and no
`push`, and it *is* decompiled. Measured over the whole image: `Sound_SetVolume`
has **6 direct `E8` callers** and each of the three has **0**, and **no dword
anywhere holds any of their addresses**. Nothing reaches them, so IDA never made
functions of them — and that makes them **dead code**, alongside `pluie.wav`
above, options page 12 and the X-Tech shoot callback.

`Sound_LengthMs` is worth stating because it is exact and checkable:

```
lengthMs = dwBufferBytes * 1000 / ((wBitsPerSample / 8) * frequency * nChannels)
```

integer, truncating — and it uses `(bits/8) * channels` rather than
`nBlockAlign`, which is the same number for every shipped file and would not be
if one were 8-bit.

#### `Wav_LoadToBuffer`, and the two branches no shipped file takes

`sub_49F830` reads a 20-byte header and requires `RIFF` at +0 and `WAVEfmt ` at
+8; reads **sixteen** bytes of `WAVEFORMATEX` whatever the chunk's own size
field says; requires format tag 1; then **peeks two bytes and seeks back over
them only if they are non-zero** — a trick for the 18-byte extended form, which
reads the `da` of the following chunk id as "not a `cbSize`" and rewinds. Then
it walks 8-byte chunk headers until `data`, `Lock`s the buffer and `fread`s
straight into the pointer.

Over the **61** shipped `gamedata/I2D/sounds/*.wav`: all 61 are accepted, all 61 take
the rewind, **0 take the other arm and 0 chunks are skipped**. Two of the
loader's branches are dead against the shipped corpus.

#### The interface's cache holds 32 of its 45 sounds

The name table at `0x004D0990` is **45 rows of 20 bytes**: the five functions
that walk it all stop at `offset aNoOne_0`, and the span between the two labels
is **exactly 900 bytes**, 45 x 20 with nothing left over. A one-line accessor in
the same region returns the literal `2Dh` = 45, which agrees. Every one of the
45 names resolves to a shipped `.wav`. The **cache** is
`unk_657B40`…`dword_657D40` — **32 slots of 16 bytes** (+0 sound id, +4 buffer
handle, +12 flags, bit 0 = loaded). `Ui_LoadSound` (0x00482D00) takes the first
slot whose bit 0 is clear and **returns silently** when there is none: no
eviction, no error. So **13 of the 45 can never be resident together**, and the
loader does not check whether an id is already cached before taking a slot.

Sixteen further `.wav` ship that the table never names — `FS004`–`FS006`,
`MUL001`–`MUL006`, `SNK010`, `arc004`–`arc006`, `asc004`, `asc005` and two
countdown files, one of them called **`cptrebour02..wav`**, with two dots.

`verify.py: engine audio`; the port is `engine/src/audio/mixer.*`.


Extracted 2026-08-29. Fire, smoke, explosions, impacts, muzzle flashes and
glows are not a particle system in the modern sense. They are **26 small
`.3DO` models**, stored **whole inside the `.SCX` files that use them** and
registered per scene in chunk 4 — **230 registrations across the 220 shipped
files**, of which 68 carry at least one. The game names them itself:

| sprite | frames | scenes | | sprite | frames | scenes |
|---|---|---|---|---|---|---|
| `EFFECTS2_SMOKE1` | 8 | **60** | | `EFFECTS1_SPIN` | 8 | 6 |
| `EFFECTS1_IMPACT1` | 8 | **45** | | `EFFECTS1_M16I` / `M16D` | 4 | 5 / 3 |
| `EFFECTS1_EXPLO1` | 16 | 26 | | `EFFECTS1_BUBBLE1` | 1 | 4 |
| `EFFECTS2_GLOW` | 1 | 14 | | `EFFECTS2_SMOKE2` | 12 | 4 |
| `EFFECTS1_EXPLO2` | 16 | 8 | | `IMPFUM` (*impact fumée*) | 16 | 1 |

plus `EFFECTS2/3_IMPACT*`, `EFFECTS3_GLOWB/C/D`, `EFFECTS3_STAR`,
`EFFECTS2_STAR1`, `EFFECTS3_SMOKB` and Bozo's three `FX*BOZ1`.

### What one is

**One mesh, one material, one 256×256 atlas, and a flat sheet of quads at
z = 0 — and each quad is one FRAME**, carrying its own cell of the atlas. That
is what the engine's own error string calls `NbTrames`:

```
EFFECTS2_SMOKE1   8 quads in a row,      32×32 texel cells   →  8 frames
EFFECTS1_EXPLO1  16 quads in two rows,   32×33 cells         → 16 frames
EFFECTS2_GLOW     1 quad,                64×64               →  a still billboard
```

The quads sit **side by side in space**, not stacked, so the model is a frame
*library* rather than something to draw as it stands: the engine picks one quad
and places it. Rendered, the frames are exactly what the names promise — a
fireball igniting and fading over 16 frames, a smoke puff expanding and
dissipating, a muzzle flash resolving into scattering sparks.

`python3 tools/sprite_fx.py --png out/` writes a contact sheet per sprite.

### The blend, which is a confirmation

Every sprite mesh carries flags **`0x3800`** (25 of them) or **`0x3000`**
(`IMPFUM`): that is `0x1000 | 0x2000` — **additive** — plus cutout on all but
one. The game's smoke and explosions are additive, which is what §4c's
correction says and what a 50%-alpha reading would have got visibly wrong for
exactly the assets that most need to glow. The data agreed with the code
reading without being asked.

### The runtime instance, and how a frame is picked

Read 2026-08-29. `Scene_LoadSCX`'s `0xDEAD0004` case reads the 12-byte header,
streams the embedded `.3DO` through `Scene_Load3DOStream`, and calls
**`Sprite_SpawnInstance`** — **one instance per registered sprite, made at
load**, not one per effect occurrence. So the chunk-4 record gains two runtime
fields, `+24` the loaded model and `+28` the instance, and the 230
registrations are 230 of the pool's **2048** slots (`Sprite_AllocPool(0x800)`).

An instance is **64 bytes**, and the field that matters is `+22`:

| off | field |
|---|---|
| +4 | the model's node array — also the in-use marker |
| +20 | int16, set to 0 at load |
| **+22** | **uint16 FRAME INDEX** — `Sprite_SetFrame` bounds-checks it against `meshdef+72`, the model's **quad count**, and stores `-1` when out of range |
| +24, +28 | float, both 1.0 at spawn — what the two `ScaleSprite` functions name |
| +32 | float, 0.9 at spawn |
| +48 | 0xFFFFFF, a colour |
| +52 | the 8-slot morph-palette scratch (`o3de_SetSpriteMorphPalette`) |
| +56, +60 | list links |

**That the frame index is validated against the quad count is the engine
confirming the format**: one quad *is* one frame, read out of the code rather
than inferred from the UV layout.

### What advances the frame — two systems, and neither is the script

The script layer does **not** animate sprites. `Script_Display3DSprite` (232
uses) and `ScaleSpriteOnX`/`OnY`/`SetSpriteRolling` (58 each) appear only in
`Script_StartScript`, never in `Script_PlayScript`
([FILE_FORMATS](FILE_FORMATS.md)); there is no third dispatcher, and the only
two functions that scan a program by id are the object loader and
`Scene_PreloadObjectSprites`. `Script_Reinit_Display3DSprite` validates the
sprite, checks `NbTrames` and zeroes parameter 3 — and that is all it does.
The `+24`/`+28` scale setters have **no callers at all**.

Two other systems own the instances, and they are the two families of effect.

**1. Character effects — `Cef_TickEffects`, from the `.CTL` state effect
records (§7).** `Cef_SpawnEffect` stashes the model's **quad count** as the
frame count and zeroes the frame index; the ticker then computes, every frame:

```c
if (start <= t && t <= start + duration && t <= windowEnd) {
    f = (t - start) / duration * frameCount;      /* rec +4, +0 */
    if (f > frameCount - 1) f = frameCount - 1;
    inst->frame = (int)f;                         /* instance +22 */
    if (rec.flags & 1) inst->pos = attachPoint;   /* follow the bone */
    if (rec.flags & 8) inst->angle += 2 * dt;     /* +32, a spin   */
}
if (t > start + duration || t > windowEnd)
    Sprite_ReleaseInstance(inst);
```

So an effect plays its frames **once across its authored duration** and is then
released — which is what an impact or a muzzle flash is. The same tick fires
the record's sound at `+12`, corroborating the `.CTL` decode from the other
side.

**2. Scene effects — `Sfx_TickAmbient` is a real particle emitter.** It owns a
**1000-slot** table of its own (72 bytes a slot), and spawns sprite instances
into it with a randomised direction **in a cone**: a half-angle at emitter
`+60` (itself randomised under flag `0x1000`), a uniform random azimuth over
360°, then `Matrix3x3_RotateVector` into the emitter's frame. Each particle
gets a lifetime from emitter `+32` (± 10% random under flag `0x100`), a frame
rate of `emitter+56 / lifetime` signed by flags `4` / `0x2000`, a rotation rate
at `+64`, and a random start angle under flag `0x10`.

That is the fire, smoke and steam, and it is driven by the **ambient `.SFX`
system**, not by the scripts that register the sprites.

### How one is drawn — and it joins the mesh path exactly

`Render_Frame` (0x00441030) is the per-frame entry: it clears the 0x4000
buckets, runs `sub_48D3B0` (the mesh visible set, §4b), then
**`Render_SubmitSprites`** (0x004969C0), two further passes, and finally
`Render_FlushBuckets`. The sprite pass is a **screen-space billboard**
renderer:

```c
for (inst = scene->sprites /* +36 */; inst; inst = inst->next /* +60 */) {
    if (inst->frame == 0xFFFF) continue;              /* not drawing */
    z = view * inst->pos;  if (z <= near || z >= far) continue;
    quad = inst->node->quads + 32 * inst->frame;      /* THE FRAME PICKS THE QUAD */
    halfW = (v[quad[2]].x - v[quad[0]].x) * inst->scaleX / z * xScale;
    halfH = (v[quad[2]].y - v[quad[0]].y) * inst->scaleY / z * yScale;
    /* rotate the screen quad by cos/sin(inst->angle), offset the UVs by
       inst->+40/+44, colour it inst->colour, alpha = inst->+36 * 255 */
    key = *(uint16_t *)(material + 64);               /* the TEXTURE SLOT again */
    ... near/fog bits, then the mode below ...
    g_RenderBuckets[key & 0x3FFF] <- two triangles
}
```

**`node->quads + 32 * frame` is the third independent statement that one quad
is one frame** — after `Sprite_SetFrame`'s bounds check and `Cef_SpawnEffect`'s
frame count. And a sprite lands in the **same bucket array, under the same
key**, as ordinary geometry: its texture is the material's slot at `+64` exactly
as a mesh's is.

### Instance `+20` is the blend mode

Last read left this a guess. It is not: the renderer switches on it and ORs
bucket bits, which are the ones §4b decodes.

| mode | bucket bits | blend |
|---|---|---|
| 0 | — | opaque |
| 1, 8 | `0x400` | cutout |
| 2 | `0x2000` | transparent |
| 3 | `0x2400` | transparent + cutout |
| **4** | **`0x2100`** | **transparent + ADDITIVE** |
| 5 | `0x2500` | additive + cutout |
| 6 | `0x2200` | multiply |
| 7 | `0x2600` | multiply + cutout |

**`Cef_SpawnEffect` sets mode 4**, so every character effect in the game is
additive — a third confirmation of §4c's correction, arriving from the sprite
path this time rather than from mesh flags.

### Still not established

* ~~**The scene sprites are never drawn.**~~ — **WRONG, and overturned
  2026-09-02.** It said only `Cef_TickEffects` and `Sfx_TickAmbient` ever link
  an instance into the scene's draw list, that `Script_Display3DSprite`'s 232
  calls validate and return, and that the code as read says dead content — with
  the caveat that *"nothing else does X" is only as strong as the search behind
  it*. It was the search.

  **`Sprite_LinkToScene` (0x0048ECE0) has FOUR call sites in the image and the
  decompilation reports two**, because two of them sit in functions IDA never
  labelled — CLAUDE.md §1's trap, which predicts a missing `proc` from *nothing
  calls it directly*, and a handler reached only through a dispatch table is
  exactly that. The two that were lost open with their own id test:

  | id | function | where the call went instead |
  |---|---|---|
  | `0x0400000D` | `Script_Display3DSpriteOnPath` | attributed to nothing |
  | `0x04000028` | **`Script_Display3DSprite`** | attributed to `Script_MorphPaletteSprite` |

  `sub_4A2D10` is genuinely `Script_MorphPaletteSprite` — it names itself in
  its own error string — and it **ends before** the `0x04000028` handler
  begins, which is how the call came to be read as its. The link itself is
  plain, at `loc_4A3097`:

  ```
  cmp  dword ptr [ebx], 0     ; not already linked
  jnz  short loc_4A30AA
  mov  ecx, [esp+34h]         ; the scene node
  push ebx                    ; the instance
  push ecx
  call sub_48ECE0             ; Sprite_LinkToScene
  ```

  So the chain is whole: `Scene_LoadSCX` chunk 4 spawns an instance per sprite
  and sets its frame to 0, the script function links it, and
  `Render_SubmitSprites` walks the scene node's `+36` list drawing every
  instance whose frame `+22` is not `0xFFFF`. What a reader's screenshot of the
  intro shows — a blue portal behind Kay'l that GRID's three meshes cannot
  produce — is those sprites. `verify.py: sprite linkers` asserts the four call
  sites against the banner's two, so the negative result cannot be re-derived
  from the same wrong count.

  **Ported 2026-09-05**: `Script_Display3DSprite` and the five setters
  (`SetSpriteType`/`Frame`/`Rolling`, `ScaleSpriteOnX`/`Y`) run in
  `Program::tick`, the scene's instances live in `SceneRunner::sprites()`,
  and `omk-play` draws them through the particle path with the instance's
  own frame, two scales, roll and type. Two readings worth keeping: the
  handler places a sprite at the active camera's **TARGET** - it prefers a
  256-row XYZ table (`unk_905F20`) whose two writers are unreferenced in the
  listing, so every one of the 232 shipped sites takes the fallback - and it
  **never unlinks** the instance, so a shown sprite stays in the draw list
  where it was last put. `Script_SetSpriteRolling`'s finishing tick writes
  its `end` parameter RAW into the radians field (the running value is
  `x pi/180`); every shipped site rolls 0, so it shows nowhere. `verify.py:
  engine scene sprites`.

  **And what STARTS a sprite object is still open — the search is bounded.**
  `Script_StartScript` (0x0044A7E0) has **9** call sites. Eight are explicit
  *start THIS object* paths: the `scx.play*` handlers reach it through
  `sub_41DC10` / `sub_41D9C0`, and `Script_PlayScript` re-enters it to restart
  a looping program. The ninth, at **0x44B150**, is the one that would answer
  the obvious hypothesis — that a scene's effects come up with the environment
  — because it walks the scene's **whole** instance array (`+12`, count `+8`,
  stride 100) and starts every entry. **Nothing references it**: no call, no
  data reference. It is dead code.

  **And a capture settles what the reading could not.**
  `traces/frames/intro-75.png` is the engine's own framebuffer of the intro,
  taken with `goldentrace capture --tag intro`: Kay'l mid-arrival in front of a
  large blue portal. It is not the set — rendered from either intro camera
  (2172 and 2148) **all 576 of GRID's triangles are offscreen**, because
  `circle01`/`circle2` sit at x 50..908 and the action is at x ≈ -487. So
  something draws it, and it is not the geometry.

  **The chain is the AMBIENT one, reached from the ANIMATION**, and it needs
  the `.SFX`:

  ```
  Anim_TickClipSfx  ->  sub_44F560  ->  Sfx_RegisterEmitter
                            |                  |
                    .SFX section C        Sfx_TickAmbient
                    (the effect)                |
                                        Sprite_LinkToScene
                                                |
                                        Render_SubmitSprites
  ```

  `sub_44F560(effectId, x, y, z, mode)` scans the loaded `.SFX`'s section C —
  the 80-byte effect records, at `dword_536B80[slot]` with the count at
  `dword_536B90[slot]`, which `Sfx_LoadFile` fills — and registers an emitter
  at that point. **`grid.sfx` holds 10 effects and two are named `kay arr` and
  `kaylarr`**, with `fxtun1`, `fash1`, `vd`, `vd2`, `burn` and `part01`
  alongside. Its section D has a single binding (`part` → `burn`) matching no
  mesh, which is why the ambient-mesh walk reports 0 emitters for GRID: these
  effects are not bound to geometry, they are fired by the clip.

  So the `.SFX` is not optional for the effects after all, and the scene-object
  sprites (`Script_Display3DSprite`) are a separate mechanism that GRID's
  script never starts.

  **And what FIRES an effect is starting a scene OBJECT** — the trigger the
  scene-sprite path never had, found 2026-09-02. All four handlers that start
  an object do two things, adjacent:

  ```
  call sub_44A7E0          ; Script_StartScript(instance)
  ...
  call sub_44CD40          ; the instance's own id
  and  eax, 0FFFFh
  push eax                 ; a2
  push 0                   ; a1
  call sub_451470          ; show the set pieces keyed (a1, a2)
  ```

  `sub_451470` walks **section E** — 76-byte rows at `dword_536BAC[slot]`,
  count `dword_536B54[slot]`, both filled by `Sfx_LoadFile` — and calls
  `SetPiece_Show` for every row whose `+8` is `a1` and `+12` is `a2`. So
  **section E binds an effect to an object-START event.**

  `grid.sfx` keys its eleven rows to objects **1**, **8** and **20** —
  `1KaylArrives`, `3KaylLeaves` and `Wait5sec` — and AREA 118's script starts
  all three. That is why the portal appears exactly with the arrival, and it
  means `Wait5sec` is not the do-nothing its name suggests: it waits 150 frames
  *and* carries five set pieces. `verify.py: sfx set pieces`.

  **A PARTICLE'S SIZE IS THE SPRITE QUAD'S OWN**, not the effect's `scale`
  field — settled 2026-09-02 by rendering it. `Render_SubmitSprites` builds the
  billboard from the quad's diagonal CORNERS scaled by the instance's
  `+24`/`+28`, and `Sprite_SpawnInstance` initialises those to **1.0**. The
  shipped sprites' quads are 24.61, 34.45, 196.85 or 275.59 units across —
  62.5, 87.5, 500 and 700 cm in the inch unit — so a particle is METRES wide.
  The effect's `+56` is the RAMP's magnitude (`±scale/life`), not a size:
  taking it as the half-extent makes every particle ~75× too small, and a frame
  rendered that way shows specks where the game has a glow.

  Two corollaries, both found the same way. The extent must be **that quad's
  own span**, not `max|p|` over the model — an eight-quad sprite lays them out
  across the file, so the larger reading makes every frame sprite-sized. And
  `Sprite_SpawnInstance` does initialise the instance ALPHA to `0x3F630C6A` =
  0.887 — but see below: `Sfx_TickAmbient` overwrites it, and both halves of
  this paragraph turned out to be about a value the tick sets for itself.

  **An emitter is ONE-SHOT.** `Sfx_TickAmbient` decrements its countdown, emits
  when it goes negative, and frees the slot once both countdowns have —
  `if (a < 0 && b < 0) *(v0 - 13) = 0`. So a sustained effect is
  RE-REGISTERED, which is what `sub_451600` does each frame for every shown
  piece, and what section D's period does for an ambient binding.

  **What a shown piece actually emits** — `sub_451600`, read 2026-09-02:

  ```c
  if ((row[+72] & 1) && !(row[+72] & 0x20)) {   // SHOWN
      pos = { row[+28], row[+32], row[+36] };
      v8  = row[+52];                            // the EFFECT ID
      ...scan section C for a row whose +0 == v8...
      Sfx_RegisterEmitter({ effect, pos, slot, 0, 0 });
  }
  if (!sub_450D60(row)) row[+72] &= 0xFE;        // done -> clear SHOWN
  ```

  So it is **one emitter per shown row**, at the ROW's CURRENT position, every
  frame — and section C is matched by its **`+0` ID, which is 1-BASED**
  (`id = index + 1` across all ten of GRID's), so an index lookup is off by one
  on every effect in the game. GRID's four standing rows name `burn`, `burnv`,
  `vd` and `ttt`; its five `Wait5sec` rows name `fxtun1`, `burn`, `burnv`,
  `vd2` and `fash1`. **The section F sub-records are not emitters** — treating
  them as such puts 36 emitters where the game has four — but "the tick never
  reads them" was too strong: `sub_450E50`, called first in that same loop,
  walks them as the WAYPOINTS that move the row, and the indirect form
  (`+52 <= 0`) takes its effect from them. See the block paragraph below.

  **And the effect's `+8` is a sprite ID resolved through the SCENE**, not an
  index into the global library: `sub_4A5800(scene + 8, sprite)` walks the
  36-byte registry rows at `scene[+52] + 32` matching the id, and `sub_4A5B60`
  turns the index it returns into the model. `Grid.SCX` registers ids **9..12**
  and its effects name exactly those; indexing `aventure.scx`'s twenty instead
  swapped `EFFECTS1_IMPACT1` with `IMPACT2` and turned `burn`'s
  `EFFECTS3_SMOKB` into `EFFECTS1_M16D`, a muzzle flash.

  **A PARTICLE HAS A COLOUR — and it is what makes the portal blue.** The last
  block of `Sfx_TickAmbient`'s spawn reads section C `+48` and `+52` as two
  packed `0x00RRGGBB` dwords, taking the bytes high to low, and builds a
  per-frame ramp only when they differ:

  ```c
  v32 = u32(fx, 48);  v33 = u32(fx, 52);
  if (v32 == v33) { col = (v32>>16, BYTE1(v32), (uint8)v32); rate = 0; }
  else            { col = ...;  rate = (channel(v33) - channel(v32)) / life; }
  ```

  GRID's `vd` — the effect with by far the most particles, count 5 over a
  36-frame life — draws the **orange** `EFFECTS1_IMPACT1` sprite modulated by
  `0x2125AF`, and `burn` draws `EFFECTS3_SMOKB` at `0x327592 → 0x778295`. So
  the portal's blue is authored in the `.SFX`, not in the texture, and no
  reading of the sprites alone could have reached it. Rendered, the port goes
  from a fire-orange spray to the capture's blue disc with a white core.

  The same block settles three more numbers this section had wrong:

  | field | what the tick does | what was recorded here |
  |---|---|---|
  | `+56` scale | written into the instance's `+24` **and** `+28`, so it is the STARTING size | "the RAMP's magnitude, not a size" |
  | the ramp | `+scale/life` under flag `0x4`, `-scale/life` under `0x2000`, **0 otherwise** | applied unconditionally |
  | `+36` alpha | set to **0.5** outright (`0x3F000000`), or ramped from 0 by `1/life` under flag `0x2` | 0.887, "which nothing changes" |

  Eight of GRID's ten effects would grow or shrink under the unconditional
  reading; only two actually do (`burn` 2.2 → 4.4 over nine frames, `kaylarr`
  7 → 14 over thirteen). And `+64`, the spin, is **degrees a frame** — the
  engine multiplies it by π/180 on the way into the instance.

  **WHAT THE BLENDS DO WITH THAT ALPHA — nothing, for every shipped effect**
  (2026-09-02, found by rendering the intro beside its capture). The bucket
  walk puts `+36 × 255` into the diffuse dword's HIGH byte (`u32(v6,108) <<
  24`), and `Raster_DrawTriangles` sets the additive bucket to
  `SRCBLEND=ONE / DESTBLEND=ONE` and the multiply bucket to `ZERO /
  INVSRCCOLOR` — neither reads an alpha. Only modes 2/3, `SRCALPHA /
  INVSRCALPHA`, would, and **0 of 396** shipped section C rows use them
  (362 are mode 4, 32 mode 6, 1 mode 5, 1 mode 0). So a particle draws at its
  FULL colour: `dst + tex × colour`, or `dst × (1 − tex × colour)`. Both the
  port and `/cutscene` had folded the 0.5 into the colour, halving every
  particle in the game — which is why the portal came out a dim haze where the
  capture is saturated cyan with a white core.

  **And the multiply is `dst × (1 − src)`**, not `dst × src`: D3DBLEND 4 is
  INVSRCCOLOR. It darkens where the sprite is BRIGHT and leaves the frame
  alone where it is black — the shape a starburst subtracts is a dark
  starburst, where `dst × src` would paint a black square with a bright star
  cut out of it. `/cutscene` had it right; the port's two backends did not
  until 2026-09-02.

  **The order is the bucket key's.** `Render_SubmitSprites` ORs the mode's
  bits into the same 14-bit key as meshes and `Render_FlushBuckets` walks it
  ascending, so every additive sprite (0x2100) draws before every multiply one
  (0x2200) whatever their textures. Grouped by sprite first, `ttt`'s multiply
  starburst (sprite 10) drew before `burn`'s puffs (sprite 12) and darkened
  only black — no ring at all.

  **Two off-by-ones in the tick, both one step.** Its per-particle test is
  `f32(v41,4) <= f32(v41,0)` — AGE ≤ LIFE — so a particle of life L is
  integrated and drawn L+1 times and GRID's four rows hold **210** particles,
  not 194; and the frame index `(frames−1) × age / life` is taken from the age
  BEFORE the increment, so a newborn is drawn on frame 0 and the last draw is
  frame `frames−1`. Computed after, `burnv`'s three-frame life never showed
  IMPACT2's bright frame 0. **And the drift is NEGATED** unless flag bits
  `0x600` read exactly `0x200` (`fld [ebx+1Ch]; fchs` at 0x46DD9D): Y points
  down, so a positive `+28` climbs, which is what the brazier measurement
  above already said. 104 shipped rows carry a non-zero drift with the bit
  clear; `tools/ambientfx.py` had the sign right and the port did not.
  `verify.py: engine particles` asserts all of these through
  `engine/tools/particle_probe.cpp`, and its first run failed every one.

  **A row's `+16` names a section F BLOCK, and the block is a PATH OF
  WAYPOINTS** — the third reading of it in a day, and the one the code
  supports (2026-09-02). Section F is `{16-byte header + n × 36}` per block,
  the header's `+8` the count and `+12` the block's duration; each 36-byte
  record is `+4` an effect id (used only by the INDIRECT form below), `+8` a
  position, `+20` the frames to the next record, `+24`/`+28` a link. They are
  not emitters — `sub_451600` registers ONE per shown row per frame — and the
  row is not a group: it is a set piece that MOVES. Five functions run it:

  | function | what it does |
  |---|---|
  | `SetPiece_Show` 0x00451340 | shows a row: inits it, chains into the row it links (`+40 == 1` → the row `+44` names, and every record linked the same way), zeroes the clock `+60`, takes the direction bit `0x8` from bit `0x4`, sets the iteration `+68` to 1, and starts the DELAY (`+56 != 0` → bit `0x20`) |
  | `sub_451220` | init: finds the block by `+16`, resolves the row's link (`+40`/`+44` → `+48`) and every record's (`+24`/`+28` → `+32`), points `+24` at the first record (the last-but-one when reversed) and writes its position into `+28..+36` |
  | `sub_450E50` | the ADVANCE, every frame: walks the records by their `+20` durations to the segment the clock is in, LERPs the two positions, writes `+24` and `+28..` |
  | `sub_451600` | every frame, for every shown row not waiting: advance, then ONE emitter at the row's position with `+52` — or, when `+52 <= 0`, the current record's `+4` (the record before it when reversed) — then the end test |
  | `sub_450D60` | the END TEST: `clock += dt`; while waiting, stop waiting when it passes `+56`; otherwise when it passes the duration, HIDE the row if `+68 >= +64`, else flip the direction under `0x10` (ping-pong), zero the clock and count the loop unless `+64` is 999 |

  **A position is a LINK.** `sub_450330` puts a record's `+8` into the frame
  of what it is linked to — type 1 a section E row (that row's current
  position, and a heading from its direction of travel, `sub_450940`), type 2
  an ACTOR found by the first three letters of its name (`sub_450FC0` case 2;
  the id is the tag packed big-endian, `0x484F31` = `HO1` = Kay'l), type 3 the
  PLAYER (`unk_8F5EA0`); the row's own link wins over the record's when both
  are set. The heading matrix is `sub_442400` → `sub_441FF0`, applied through
  `Matrix3x3_RotateVectorT`. Over the 382 shipped rows: 267 unlinked, 79 to an
  actor, 36 to a row; 442 of 1457 records name the player. 162 rows carry a
  delay, 111 play once, 100 for ever, 38 are the indirect form.

  **This is the capture's dark ring.** `ttt` — GRID's one multiply effect,
  scale 0.9, a 44-unit dark star — is row 3, linked to row 0 and carrying a
  27-record block whose positions lie on a tilted circle of radius **39.4**
  at **0.385 frames a segment: ten frames a turn, which is `ttt`'s lifetime**.
  So its eleven live particles are spaced round that circle, and the spiky dark
  ring inside the disc is eleven small dark starbursts orbiting the disc's
  centre. No reading of the effect table alone could reach it: `ttt`'s own
  position is 40 units left of the disc and its size is a quarter of the
  ring's. Rows 0–2 travel a 4-unit line over 22 frames and loop for ever; row
  4 (`1KaylArrives`, the arrival) is the indirect form linked to `HO1`,
  playing `kaylarr` then `kay arr` at Kay'l's position after a 6-frame delay,
  forward then back, twice; row 5 (`3KaylLeaves`) plays the same block once,
  reversed, relative to the player, after 21 frames.
  `engine/src/o3de/setpiece.{h,cpp}` runs it; `verify.py: engine particles`
  asserts the orbit's radius and period and the arrival's delay, effects and
  hide frame. **Unported and counted:** the smoothed heading under row flag
  `0x40` (8 rows), the roll under `0x80` (4) and its interpolation under
  `0x100` (4), and the effect's sound.

  **And the STANDING rows come up with the set.** `Sfx_BindAmbientEffects`
  ends by walking section E and calling `SetPiece_Show` on every row keyed
  `(1, -1)`, hard-coded:

  ```
  v30 = row + 12;
  do { if (*(v30 - 1) == 1 && *v30 == -1) SetPiece_Show(v30 - 3);
       v30 += 19; } while (--n);
  ```

  So they are the scene's static background effects, activated when the
  environment's effects are bound — which is why no object start reaches them
  and why a search through every `Script_StartScript` call site found nothing.

  So GRID's four `fx*` objects are started by nothing that can be found:
  AREA 118's script starts objects 20, 1, 6 and 8 and never 13, 15, 17 or 19;
  `Wait5sec` is literally `Script_Wait(150.0)` on a loop; and
  `Script_PlayAllScripts` walks only instances that are already running. The
  counts are asserted in `verify.py: sprite linkers` so the next search begins
  from a fact rather than a memory.
* **The emitter record is `.SFX` section C**, and it is now decoded — the
  sound id at `+4`, the lifetime, cone angle and rates, the name at `+70`, and
  the **sprite blend mode at `+78`** that this section's mode table consumes.
  See [FILE_FORMATS](FILE_FORMATS.md) 5b6. Two fields carry it: `+78` lands
  inside the 0..8 enum for **366/366** rows (**332 additive**), and `+4` uses
  the engine's own `-1`/`0xFFFF` silent sentinel in **338**.

  **Section D's tag is settled too** — the assembly compares `meshdef+16`, the
  first four characters of the mesh's name, as a dword against it, so a
  `0x40000000` mesh finds its effect by name. **321 of the 579** flagged set
  meshes bind to a section C row that way, `neon` (102) and `fume` (33)
  commonest.

  **The particle integrator is read too**, and it is where the shape comes
  from: velocity added to the position each frame, **`+28` added to the Y
  velocity** (so a flame accelerates upward — the brazier's climbs 175 units
  over 23 frames, not 46), `±scale/life` to the scale, the rotation rate to the
  angle, and the frame index as **`(frames − 1) × age / life`**.

  The emission **axis** is not the authored vector: `Sfx_RegisterEmitter`
  jitters it under flag `0x40` and builds the cone's basis from the result, so
  a flame **leans** — once per emitter, over the whole `+x..+z` quadrant, and
  **differently on every launch of the game**, because `srand` is seeded from
  `timeGetTime()`. So a viewer should use its own random source; see
  [FILE_FORMATS](FILE_FORMATS.md) 5b6.

  **`+68` and the flags word are decoded too** (FILE_FORMATS 5b6): `+68` is
  how many particles one emission makes, and the eight flag bits are all
  randomisations or scale ramps — including the `0x4`/`0x2000` grow/shrink that
  **241 of the 366** effects use and that makes a flame broaden as it rises.
  One bit, `0x2`, is used by **nothing**, so the alpha ramp never runs.

  `+36` and `+40`, **0 in all 396 rows**, are the emitter's two COUNTDOWNS —
  `Sfx_RegisterEmitter` copies `+36` into the sound timer and `+40` into the
  spawn timer, `Sfx_TickAmbient` decrements both by the frame delta and fires
  each when it goes negative — so a registered emitter spawns on the frame it
  was registered, and the "period" a set piece runs at is exactly one emission
  a frame, transcribed rather than assumed. The `+48`/`+52` pair is the colour
  ramp above.

`/cutscene` draws the ambient emitters; `engine/` draws those and the set
pieces.

## 4. Proof: the 3DM vertices *are* the face-mesh vertices

The clearest confirmation is numerical. For `BOZ_FNM` the `.3DO` face mesh and
frame 0 of `MORPH/071348.3DM` hold the **same 130 vertices in the same order**:

```
            3DO BoVisage            3DM 071348 frame 0
vertex 0    [ 1.055,  1.472, -1.187]   [ 1.055,  1.472, -1.199]
vertex 1    [ 0.781, -2.366, -1.572]   [ 0.781, -2.366, -1.585]
range x     -2.34 .. 2.34              -2.34 .. 2.34
range y     -2.43 .. 3.14              -2.43 .. 3.14
```

Mean distance over all 130 vertices: **0.036**, on a face spanning about 5.6
units. Frame 0 is the bind pose; later frames move the mouth and eyes (mean
displacement 0.327 by frame 120, peak 0.837).

So a `.3DM` is literally a replacement stream for one mesh's vertex array. The
`.3DO` supplies everything else.

`tools/facepreview.py` renders a single frame to a PNG with a software
rasteriser, using the same data path as the browser app:

    python3 tools/facepreview.py 071348 BOZ_FNM 40 out.png

### Drawing the whole character

`.3DO` meshes are laid out sequentially — the sum of the per-mesh vertex,
triangle and quad counts equals the header totals — so mesh *i* owns the
vertices from the running total of the meshes before it.

**verified.** Mesh positions are **absolute in model space**, not relative to
the parent. Accumulating them up the parent chain pulls the figure apart;
adding each mesh's own position to its local vertices gives a coherent
character. The reference importer agrees — `Omikron.cs` sets `t.parent` and
then `t.position` (Unity's *world* position, not `localPosition`), so nesting
depth never compounds the offset.

The triangle record is **28 bytes**, not the 24 the notes imply: 3 × int16
indices, 3 × 2 UV bytes, int32 material id, then a 12-byte normal. A material
id of -1 means the face is not drawn.

**Texture coordinates are the raw bytes divided by the material's `width` on
both axes** — `OmikronObjects.cs` uses `texcoordmultiplier = 1/width` for u and
v alike, and does **not** add 1, though the notes say to. The code is what
produced working output, so it wins.

**Characters are unlit.** A vertex is `pos[3], normal[3], colour RGBA8,
lighting RGBA8` (32 bytes). The importer shades with `Color.white *
lighting.g` — nothing else. Across every `PERSOS` model the `colour` field is
the bit pattern of float 1.0 and `lighting` is `ff ff ff 00` on *every* vertex,
so `lighting.g` is 1.0 and the texture is drawn at full brightness with no
shading at all. Applying a directional light instead — which is the obvious
thing to do and what these tools did at first — makes every character muddy
and dark. Scenery is where the baked lighting actually varies.

`tools/modelpreview.py` renders a whole character, optionally with a `.3DM`
frame driving the face, through the same data path the browser app uses:

    python3 tools/modelpreview.py BOZ_FNM out.png 071348 120

### Two details that matter when rendering

* **The model's Y axis points down.** Rendering with +Y up gives an upside-down
  face.
* **A negative face index is local to a mesh higher in the hierarchy, not to
  the model.** Mask with `0x7FFF`, then add that mesh's vertex base and its
  position. Getting this wrong throws the triangles across the screen — 45 of
  `BoVisage`'s 260 are skinned this way.

  **`Omikron.txt` says "the parent mesh", and that is not quite right.** Two
  things have to be skipped walking up:

  * **Attachment markers.** An arm's parent is the 3-vertex shoulder marker,
    whose coordinates sit far outside the model, so an index of 0-2 "fits" it
    and drags a stray triangle off the shoulder. The reference importer skips
    any ancestor with `vertexCount <= 3`, and its comment — *"This is not
    entirely correct as there are issues on character's shoulders!!!"* — is
    pointing at this exact artefact.
  * **Out-of-range indices.** Over 40 models, 9405 corners are skinned: 7876
    fit their immediate parent's vertex array, but **1529 index past it**, and
    every one of those fits some further ancestor. None fits no ancestor.

  So: walk up past any mesh with 3 or fewer vertices, and keep walking while
  the index is out of range. Applying only one of the two tests leaves visible
  strays — the marker test alone can read out of bounds, and the range test
  alone puts a wedge on every shoulder.

* **`materialId == -1` means the face is not drawn**, and some models lean on
  that heavily. Boz's legs look like bare diamonds because only two or three
  triangles per leg have a real material; the rest are marked undrawn. That is
  the model, not a decoding fault.

* **Three kinds of mesh must be skipped**, or they draw as stray dark
  rectangles floating beside the character:
  * `flags == 0` — 123 meshes in PERSOS, all `Vise`/`Tire` aim-and-shoot
    markers or `M*` proxy skeletons;
  * `flags & 0x800000` (CollisionOnly) and the mesh named `tir`, which the
    reference importer also skips;
  * a single triangle of 3 vertices with **flag bit 0** set — 532 of the 547
    bit-0 meshes are exactly this, all named `*Epauled` / `*Epauleg` /
    `*Ventre`, and their positions sit far outside the model. Only 4 meshes of
    that shape lack the flag, so testing both fields is precise.

  **The engine's own rule is simpler, and it settles two of the three**
  (2026-08-29). The traversal that feeds `Render_SubmitMesh` guards every call
  with one test — `test dword ptr [eax], 800043h`, i.e. skip the mesh if
  `flags & (0x1 | 0x2 | 0x40 | 0x800000)` — plus a null check on the node's
  transformed-vertex array. So **bit 0 alone** means "do not draw": no shape
  test is needed, and it is exact — all **511** PERSOS meshes the engine skips
  are skipped by bit 0, **497** of them the lone-triangle shape and 14 not.
  `0x800000` (CollisionOnly) is confirmed; `0x2` and `0x40` are in the mask and
  no shipped mesh carries either.

  What the mask does **not** contain is `flags == 0`. The engine draws those 96
  PERSOS meshes and the viewer drops them, so the rule above is a viewer
  heuristic with no counterpart in the renderer — something else must hide them
  (a per-node bit, not the mesh flags). Narrowed, not closed. Across the 220
  sets the mask costs **3** meshes the viewers currently draw.

* **Mesh flag 0x40000000 marks an ANIMATED set effect.** At load
  `sub_44F840` (0x0044F840) walks the scene, tests `mesh->flags & 0x40000000`,
  and registers each hit in a **256-entry** table of 7 dwords: the mesh, a
  pointer into the `.SFX`'s 80-byte section C, a duration, and — the giveaway —
  a starting phase seeded with **`rand()`**, so no two start together.

  **579 meshes across the 220 sets carry it**, and the names say what they are:
  `neon` (104), `halo` (80), `fume` (smoke, 35), `poubelle` (33), `fire` (15),
  `agaz` (12). Anekbah alone has 209, five of them (`neon12`, `neon65`–`neon68`)
  in a single frame of the title sequence.

  **Corrected 2026-08-29: the neon does not flicker.** That reading came from
  the `rand()`, and the `rand()` is only reached when the emitter's **period**
  — section D `+12` — is greater than `flt_4BC520`, which is `0.0`. **120 of
  the 151 shipped binding rows have period 0**, which means a particle every
  frame; with `neon`'s lifetime of **1 frame** that is a steady glow, not a
  blink, and **all 102** of Anekbah's neon emitters are period 0.

  Anekbah is not *entirely* steady, and the difference is worth stating
  precisely rather than sweepingly: **148 of its 153 emitters are period 0**,
  and the other **5** are `cacC` — lifetime 2 frames, periods 3, 16, 28, 35 and
  55 — which do blink, every 0.1 to 1.8 seconds. Those five are the only thing
  in the set that switches on and off.

  What the flag really marks is an **emitter**, and §3b now follows it all the
  way to the picture. `/cutscene`'s `fx` button draws them.

  **The cadence sets the particle count.** An emitter with period `P` and
  lifetime `L` keeps `ceil(L / P)` particles alive — one every `P` frames, each
  living `L` — and summed over a whole set the worst case in the game is
  **893**, inside `Sprite_AllocPool`'s own **1000**. So the viewer reproduces
  the count exactly instead of capping it, which it did at first: wrapping a
  particle's age at `n × step` rather than at its lifetime held Anekbah's
  brazier flame (`afum01`, effect `agaz`, 23 frames rising 46 units) to **6**
  frames — a quarter of its height, stuck on the first few of its 16 animation
  frames. Found against a screenshot of the real game, which is the only place
  it could have been found.

* **The renderer draws by RENDER FLAG, not by mesh order** — and the flag word
  carries the texture. See **§4b** below, which reads the walk end to end: the
  bucket key, what each bit means, what orders the buckets, and where the
  texture actually comes from.

* **The depth buffer is REVERSED.** `SetRenderState(23, 5)` at device init is
  ZFUNC = **D3DCMP_GREATER** (the D3DCMPFUNC values are NEVER 1, LESS 2,
  EQUAL 3, LESSEQUAL 4, **GREATER 5**), so a larger depth value is *nearer*.
  A second pass sets `23, 3` = EQUAL. The engine also never culls —
  `SetRenderState(22, 1)` is D3DCULL_NONE — so both faces of a two-sided panel
  are always submitted.

  For a viewer this changes nothing by itself: GREATER and WebGL's default
  LESS are both *strict*, so of two exactly-coincident faces both keep the
  **first** one drawn. Reading the 5 as LESSEQUAL and "fixing" the viewer to
  match put the later face in front and drew the wrong advert on Anekbah's
  billboards — see CLAUDE.md section 6.

* **Coincident faces are common, and their tie-break is now known.** ANEKBAH
  carries **189** face groups whose world positions agree to four decimals,
  **18** of them pairing different materials with completely different UVs — two adverts printed on
  one quad. They are not the `AApub*` billboards — each of those uses a single
  material, and **none of the 36 has any two of its three quads coincident**:
  their closest face centres are 13.8–15.3 units apart, because the mesh is a
  triangular **prism** (a ring of six vertices with a seventh above, quads
  indexing (0,2,3,1), (1,3,5,4), (4,5,2,0)) — a trivision hoarding whose three
  SIDES carry the same advert. That refutes the standing account of the panel
  FLICKER, which had them as coincident faces z-fighting; the flicker now has
  no candidate. `verify.py: aapub prism`. The coincident pairs are the
  **shop signs**, one mesh apiece and the names say so:
  `Abank01`, `Apolice01`–`04`, `Adrugs02`, `Ahosp04`/`05`, `Asmarket01`–`07`,
  `Abooks01`/`02`. All 18 are exact to the float, all 18 sit on meshes with
  `flags = 0x4`, and both faces of every pair are **in the same mesh** — so the
  state bits cancel and the tie is decided by the one part of the key that
  differs, the **texture slot** (§4b). Slots are handed out in material-record
  order within one load, so the pair member with the **lower material index**
  is drawn first and, under a strict depth test, wins.

  That is why "material-id order looks right" — it was never an accident of
  numbering. It is the slot order, seen through the one case where the two
  coincide.

* **Mesh flags 0x1000 and 0x2000 make the mesh TRANSPARENT — and the pair is
  ADDITIVE, which is a correction** (2026-08-29). The earlier reading here had
  the two flags doing one job for two back ends. They are two different jobs,
  and reading the bucket key (§4b) separates them: mesh `0x1000` becomes bucket
  bit `0x2000`, mesh `0x2000` becomes bucket bit `0x100`, and
  `Raster_DrawTriangles` does

  ```c
  if (key & 0x2000) {                       /* mesh flag 0x1000 */
      SetRenderState(14, 0);                /* ZWRITEENABLE  = FALSE          */
      SetRenderState(27, 1);                /* ALPHABLENDENABLE = TRUE        */
      if      (key & 0x100) { SRCBLEND = ONE;  DESTBLEND = ONE; }   /* mesh 0x2000 */
      else if (key & 0x200) { SRCBLEND = ZERO; DESTBLEND = INVSRCCOLOR; }
      /* with neither, the blend factors are left as the previous bucket set them */
  } else {
      SetRenderState(14, 1);                /* ZWRITEENABLE = TRUE            */
      SetRenderState(27, (key & 0x400) != 0);   /* mesh 0x800 = MaterialCutout */
      SRCBLEND = ONE; DESTBLEND = ZERO;
  }
  ```

  So `D3D_SetRenderState(dev, 27, 1)` is reached two ways and the one the old
  note pointed at is the **cutout** path, from mesh flag `0x800`, not from
  `0x2000`. The cross-tab over the 220 shipped sets has only two cells, and the
  empty ones are the content:

  | mesh flags | bucket | blend | meshes |
  |---|---|---|---|
  | `0x1000 \| 0x2000` | `0x2100` | ONE / ONE — **additive** | **211** |
  | `0x1000 \| 0x4000` | `0x2200` | ZERO / INVSRCCOLOR — **multiply** | **6** |
  | `0x1000` alone | `0x2000` | no sub-mode | **0** |
  | a sub-mode without `0x1000` | — | never blended | **0** |

  So every transparent mesh in the game asks for one of exactly two modes, and
  the old note's "no shipped set uses either sub-mode" was wrong twice over.
  The names agree with additive better than they ever agreed with 50% alpha —
  `halo`, `neon`, `eclair c0*`, `lum fx*` are glows — and the six multiply
  meshes are `AB_mirror` and `mirroir` (the two mirrors) plus Bozo's `flam01`,
  `flam02`, `scre01`, `scre02`. **INVSRCCOLOR is `dst × (1 − src)`**: a
  multiply pass darkens where its texture is bright and leaves the frame alone
  where it is black. The replica's two backends computed `dst × src` until
  2026-09-02 — the complementary picture — and it went unnoticed because only
  those six meshes and the mode-6 sprites (32 of 396 effects) use it; the
  intro's `ttt` starburst is what told the two apart (§3b).

  > **CORRECTED 2026-09-01, and the raised eyebrow was right.** The sentence
  > that stood here said a mirror is "a darkening overlay ... with the
  > environment-map pass dead there is nothing for it to reflect, and **it is
  > what the code does**". The code does something else: there is a **planar
  > reflection pass**. `sub_440D90`, called once a frame from the camera setup
  > (`Runtime.exe.c` 96088), reflects the eye and target through the mirror's
  > plane — `p -= 2·dist·normal` — reflects the pitch, and calls
  > **`sub_441030`, the scene draw**, for a second full pass. So the blend mode
  > is not a substitute for a reflection, it is the **compositing operator over
  > one**, which is why one mirror can be additive and another multiply without
  > either being an anomaly. The env-map pass being dead was true and was not
  > the whole story.
  >
  > **And "the two mirrors" was an artefact of the enumeration.** The rule is
  > mesh flag **`0x100000`**, carried by exactly **6 of 12203** meshes —
  > `CSmiro`, `AP_mirror`, `AB_mirror`, `mirroir`, `Coffre sol`, `plaque00` —
  > and enumerating by the multiply blend cannot reach it: `AP_mirror` is
  > **additive** (`0x00103000`) and `CSmiro` carries the bit with **no
  > transparency at all** (`0x00100000`). Nor is the name the rule: `miroir` in
  > the restaurant and `BAMiror01`/`02` in the bar carry no flags and never
  > reflect. A negative result over a corpus is only as strong as the
  > enumeration behind it — the same lesson as the cutscene beats.
  >
  > **One at a time, and hardware only.** A flagged mesh is stored in a single
  > global (`dword_534F48`), so at most one mirror is live; and the pass is
  > gated on `!sub_45EF50()`, the display-driver index, so it runs in **mode 0
  > only** and a mirror really is a flat blended pane in the software modes.
  >
  > **Found by playing**, which is why it is here: a reader flying the C++
  > viewer reported *a wall has transparency but is supposed to be a mirror —
  > same issue in both renderers*. **Ported the same day** (`drawWithMirror`,
  > on the renderer boundary, so both backends get it — 0.998 coverage
  > agreement) and **confirmed by playing again**: flown across viewpoints the
  > mirror is correct and its edges line up. That second report is the evidence
  > for the two parts that are reconstruction rather than trace — the plane's
  > normal and the confinement to the mirror's area — because a wrong plane, a
  > flipped normal and the wrong screen flip all look plausible in a still
  > frame and only come apart when the camera moves. `verify.py: mirror pass`.

  **Applied to both viewers** (2026-08-29, on request): additive and multiply
  are two blend states rather than one flat 50%, `decor_geometry` returns the
  mode as `"add"` / `"mul"` / `""`, and the batches sort additive before
  multiply, which is the engine's ascending bucket order.

  **And the order is global, not per object.** The engine bins the whole frame
  — set, actors and sprites alike — into the one 0x4000-entry bucket array, so
  transparent geometry follows *all* opaque geometry regardless of which object
  it belongs to. Drawing a whole object at a time breaks that visibly:
  `/cutscene` composited the set's glass before the actors existed, so a
  character standing behind a pane was blended out of it and then drawn over
  the top, appearing in front of glass he was behind. It cost two symptoms that
  looked unrelated: Kayl in front of the prison glass, and **Koopy the lizard**
  drawn wrong inside Kay'l's vivarium — he is the actor `KOP_FN`, not set
  geometry, so the tank's glass composited before him. Fixed 2026-08-29 by
  running two passes over everything. Note what is *not* done, because the
  engine does not do it either: transparent surfaces are **not** depth-sorted
  against each other — the key sorts by render state and texture slot, not by
  distance.

  The names corroborate it without any measurement: `Ap01vitre` and `ss1vitre*`
  (*vitre* = pane of glass), `eaubassin` (water), `eclair c0*` and `lum fx*`
  (light cards), `mhplante*`, and one called outright **`adtranspa`**.

  There is no alpha channel to read: a `.3DT` palette is 3 bytes per colour, so
  the translucency is the mesh's property, not the texture's — which is why the
  blend has to come out of the flags.

  *Found from play* — the vivarium in Kay'l's apartment rendered as an opaque
  brown box where the game shows the room through it.

* **Mesh flag 0x800 (MaterialCutout) means solid black in the texture is
  transparent.** 10 PERSOS models use it. Boz is the striking case — every one
  of his 18 meshes is cutout, so he is meant to render as a fragmented gold
  hologram with a solid face, not as the gold-and-black checkerboard you get by
  ignoring the flag.

---

## 4b. The renderer — the visible-set walk, the bucket key, the texture cache

Read 2026-08-29 (phase 4). Two things were open here: what orders the draw, and
where a face's texture comes from. Both are the same 14-bit number.

### The walk

The visible set is decided in `sub_48D3B0`, which walks a scene's **flat node
list** (`for (n = scene[1]; n; n = n->next)`) and applies three rejects in
order, cheapest first:

1. **the hidden bit** — `if (meshdef->flags & 0x40) continue;`
2. **a bounding-sphere distance cull** against the camera at `dword_90730C`:
   centre at `meshdef+36/40/44`, radius at `meshdef+88`, kept when
   `(r + far)² > |c − eye|²`;
3. **four half-space tests**, `n·c + d <= r` against `flt_660AD4 … flt_660B18`
   — the four side planes of the frustum.

There is no portal walk and no PVS: the `.3DO`'s door records are not consulted
here. Survivors are transformed and handed to `Render_SubmitMesh` (0x004951C0),
and every call site is guarded by the same second test:

```asm
test    dword ptr [eax], 800043h      ; eax = the mesh definition; +0 is flags
jnz     skip                          ; 0x1 | 0x2 | 0x40 | 0x800000  =  do not draw
mov     eax, [esi+1Ch]                ; node+28, the transformed vertex array
test    eax, eax
jz      skip
```

`Render_SubmitMesh` then walks the node's triangles (stride
**28**, count at meshdef+68, material index at +12) and quads (stride **32**,
count at meshdef+72, material index at +16), rejects anything wholly off
screen, and for each surviving face writes a **124-byte** record into a pool and
pushes it onto one of **0x4000 bucket lists** (`g_RenderBuckets`, 0x4000
pointers). It pushes at the **head**, so a bucket is consumed in reverse
submission order.

### The key

The bucket index is the low 14 bits of a flag word composed once per mesh and
then adjusted per face:

```c
g_MeshFlags = meshdef->flags;                       /* per mesh */
state = 0;
if (flags & 0x10000) state  = 0x800;                /* NOTE: assignment, not OR */
if (flags & 0x800)   state |= 0x400;
if (flags & 0x8000)  state |= 0x400;
if (flags & 0x1000)  state |= 0x2000;
if (flags & 0x2000)  state |= 0x100;
if (flags & 0x4000)  state |= 0x200;
if (hw_texturing() && (flags & 0x4000000)) state |= 0x40;   /* envmap, not "textured" */
g_MeshBucketFlags = state;

key = g_MeshBucketFlags | *(uint16_t *)(material + 64);        /* per face */
if (maxz <  g_NearSplit)                    key |= 0x80;       /* near   */
if (maxz >= g_FarSplit && !(key & 0x800))   key |= 0x1000;     /* fogged */
bucket = key & 0x3FFF;
```

`maxz` is the largest of the three (or four) transformed vertices' `+16`, so the
two depth bits are a property of the **face**, not the mesh — which is why they
cancel between two coincident faces and never break a tie.

The `0x10000 → state = 0x800` line really is a `mov esi, 800h`, not an `or`: that
one flag **wipes** every state bit set before it. Worth knowing before writing
this out as a table of independent bits.

### What draws it

`Render_FlushBuckets` (0x00460060) runs `for (i = 0; i < 0x4000; i++)`, and for
each non-empty bucket transforms that bucket's faces into a shared 32-byte
`D3DTLVERTEX` array (`sx sy sz rhw colour specular tu tv`) and calls
`Raster_DrawTriangles(count, i)`. So:

> **the draw order is the bucket key, ascending** — state bits first, then the
> texture slot, then reverse submission order inside a bucket.

Ascending key plus a strict depth compare means **the lower key wins a tie**,
and blended geometry (mesh flags `0x1000|0x2000` → key bits `0x2000|0x100`)
lands at the top of the range and is therefore drawn last, which is what
transparency needs. `sz` and `rhw` are written from the *same* float, so the
depth value is a reciprocal-w and `ZFUNC = GREATER` is the right way round.

`Raster_DrawTriangles` (0x00460B80) is where the two back ends part:
`dword_53ADF0 == 2` dispatches to the libpoly2d software rasterizers, otherwise
it drives D3D. Both take the bucket index as their state word.

There is a **second** bucket walk, `sub_42FF80`, at vtable entry 1 of
`off_4C4918`. It is **never installed**: `sub_42F9A0` wires entry [0] at
startup and `sub_42FA00` is called exactly once, with `0`. Its four vtable
siblings in the 0x431xxx range are dead with it. Recorded so nobody reads them.

### Where the texture comes from — and the 58-slot cache

The first line of the D3D path in `Raster_DrawTriangles`:

```c
SetTexture(dev, 0, g_D3DTextures[key & 0x3F].surface);   /* 104-byte records, +100 */
```

and the software path's equivalent is `dword_9070B0 + ((key & 0x3F) << 16)` —
a 64 KB page, i.e. 256x256 at 8bpp. **The texture is the low six bits of the
bucket key, and nothing else.** Those six bits come from the material, at
`+64`.

`+64` is not in the file. `SetMaterialsMemory` (0x004406B0, named from its own
error string) is called once as `SetMaterialsMemory(58, 0)` and allocates
**58 texture slots** — `g_TextureSlots`, 36 bytes each — a 768-byte palette per
slot and a `(58 + 1) << 16` page arena. `Tex3DT_BindMaterials` (0x004A6CF0)
then walks the `.3dt` beside each `.3DO` and, for every material of that scene,
either claims a free slot or **reuses one already loaded**:

```c
/* full-page (256x256) textures */
for (i = 0; i < 58; i++) {
    if (slot[i].subslots) continue;                 /* a packed page, skip */
    if (!strcmp(slot[i].name, material + 20)) break;
}
if (i != 58) {                       /* CACHE HIT                          */
    Dbg_Printf("cached texture :%s\n", material + 20);
    fseek(stream, material->imageDataSize, SEEK_CUR);   /* skip the pixels! */
    *(uint16_t *)(material + 64) = i;                   /* point at what is there */
} else {
    i = first slot with slot[i].used == 0;       /* else "no free textures" */
    strncpy(slot[i].name, material + 20, 19);
    slot[i].used = 1;
    fread(...); upload;
    *(uint16_t *)(material + 64) = i;
}
```

Smaller textures are packed several to a page and take a sub-slot index in the
**high** half of `+64`; palettes run the identical dance over `g_PaletteSlots`
(32-byte records) into `+68`. `Materials_ReleaseSlots` (0x00441840) is the
mirror: on free it walks **every other resident scene**'s materials looking for
the same slot index and only releases a slot nothing else points at, zeroing
the name's first byte so it cannot be matched again. The reference counting is
sound; there is no stale-slot bug.

The material record's runtime fields are therefore:

| off | size | field |
|---|---|---|
| +0 | 20 | short name (`BATITR15`) |
| +20 | 20 | **texture file name (`BATITR15.BMP`) — the cache key, compared and copied at 19 chars** |
| +40 | 20 | palette file name — the palette cache key |
| +60 | 4 | image data size in the `.3dt` |
| +64 | 2+2 | **texture slot, then sub-slot — `0xFFFF/0xFFFF` on disk, written by the loader** |
| +68 | 2+2 | **palette slot, then sub-slot — same** |
| +72 | 4 | bits per pixel (8, or 4 for a 16-colour palette) |
| +76 | 2+2 | width, height |

`+64` and `+68` shipping as `-1` is the proof that they are runtime fields: the
renderer reads `+64` as the bucket key's low bits every frame, and an
unassigned material would name slot 63 of 58.

### The consequence: **the texture cache is keyed on the name alone**

Two materials whose texture file names agree in their first 19 characters get
**the same pixels**, whatever their `.3dt` actually holds — the loser's data is
`fseek`'d past and never read. And the pool is **global**, not per scene: the
engine keeps **two decor sets resident at once**. `Area_LoadSet` owns a
two-entry table (`g_DecorSlots`, 132 bytes each) with a state byte at `+110` —
`2` = linked into the render list (`o3de_InsertScene`), `1` = loaded but
unlinked, `0` = empty — and a load takes a `0` slot in preference to freeing a
`1`. **Hidden is not unloaded**: a state-1 set is still in the o3de registry
and still holds its texture slots, so the incoming set's materials cache-hit
against the *outgoing* location's atlases.

The data says this is not a corner case:

* **182 texture names ship with different pixel data in different `.3DO`
  files**, across the 635 models.
* **All twenty** of ANEKBAH's do — `BATITR01`…`BATITR20` — and the files they
  collide with are Anekbah's own neighbouring locations: `AToit` (the rooftops),
  `AImpasse` and `AImpasas` (the Impasse and its airlock), `A_shootg` (the
  shooting gallery), `Qalisar`.
* The alternates are **revisions of the same atlas**, not different images:
  ANEKBAH vs `AToit` differs by 21.4% of `BATITR15`, 12.1% of `BATITR12`, 1.9%
  of `BATITR18`, and 0.0% of `BATITR09`.

So a substitution does not wreck a wall — it repaints **some** of the adverts
printed on one atlas and leaves the rest alone. Measured per sign, over the UV
rectangle each one samples, with `AToit`'s `BATITR12` standing in for
ANEKBAH's:

| sign | UV rect on `BATITR12` | of that patch, changed |
|---|---|---|
| `Adrugs02`, `Ahosp04`, `Ahosp05`, `Asmarket05` | 223–253 x 1–88 | **4 %** — looks right |
| `Apolice01`, `Apolice02`, `Abank04`, `AApub02`, `AApub13`, `Apub01` | 0–126 x 62–95 | **48 %** — looks wrong |
| `Asmarket01`, `Asmarket02`, `Asmarket06` | 1–127 x 95–127 | **40 %** — looks wrong |

**One substituted atlas, and neighbouring panels in the same shot disagree.**
That is the shape the report asked for and that no depth rule, cull mode or
draw order can produce, and it needs no coincident faces at all — a sign with a
single quad is hit exactly as hard.

What is established here is the mechanism and that ANEKBAH is fully exposed to
it. What is **not** established is that this is what produced the particular
screenshot — but a golden trace taken before any of this was known lands
squarely on it. `traces/impasse-walk.log` announces `AREAS 222` and then
`AREAS 0`: **AREA 222 is `AIMPASSE` and AREA 0 is `ANEKBAH`**, so the capture
walks the player from the Impasse into Anekbah with the Impasse's set still in
the other `Area_LoadSet` slot. Seven of ANEKBAH's atlases name-match
AIMPASSE's and are therefore substituted — `BATITR06` (38.8% of the image
different), `09` (0.0%), `10` (64.3%), **`12` (12.1%)**, `16` (8.1%), `19`
(55.6%), `20` (71.3%) — and `BATITR12` is the atlas the wrong sign panels
sample. Pinned by `verify.py: anekbah residency`.

> **RENDERED, 2026-09-01, and the mechanism is confirmed.** With a software
> rasterizer there is no need to argue from an atlas. Same set, same camera,
> the only change being which neighbour was resident: **`AImpasse` substitutes
> 7 atlases and moves 6920 frame pixels (33 visibly); `AToit` substitutes 18
> and moves 121588 (1763 visibly)** — a quarter of the frame repaints depending
> on where you walked in from.
>
> **The attribution is narrowed rather than confirmed.** Masked exactly — the
> atlas repainted a flat colour and the set re-rendered, so a pixel belongs to
> the sign precisely when that changes it — `BATITR12` shows **8023** pixels,
> of which 545 move and **0 move visibly**. From that viewpoint this is not the
> panel that changes, and the reported symptom comes from another atlas or
> another shot.
>
> And a fact that explains the symmetry: **`AImpasse` and `AToit` ship
> byte-identical copies** of these atlases (same md5) while Anekbah's differ —
> Anekbah is the odd one out, and the two neighbours differ only in how many
> names they share, 7 against 18. `verify.py: anekbah rendered`.

Note which side that puts in the wrong: under this reading the **viewers are
right and the game is the odd one out** — a viewer resolves each material to
its own pixels, and the engine does not. "Fixing" a viewer here would mean
*reproducing the cache*, not correcting a decode, which is not obviously worth
doing.

To close it outright you want the substitution observed rather than inferred.
**The obvious route is shut**: `Tex3DT_BindMaterials` calls `Dbg_Printf`
("cached texture :%s") on every hit, but `Dbg_Printf` is `nullsub_1` — a
one-byte `retn`. The debug printfs were compiled out of the shipped build and
only their string literals survive, so there is nothing on that channel to
capture. What would settle it is the load ORDER, and that the simulator can
already produce: `tools/sim` models the area load (phase 6 stage 6), so
teaching it the two-slot `g_DecorSlots` table and the name cache would let it
replay the capture's own area sequence and name the substituted materials
without running anything.

A falsifiable prediction to test by playing, meanwhile: **the same Anekbah
panel should look different depending on which location you walked in from** —
wrong when you arrive from the rooftops, the Impasse or the shooting gallery,
right when you arrive from anywhere with no `BATITR*` of its own.

---

## 4c. The lighting model

Read 2026-08-29, finishing phase 4. Short version: **the engine does not light
sets at all.** Every set surface is shaded by a colour baked into its vertices
at authoring time; the only live lighting in the game is a per-object dynamic
pass that sets never enter.

### The two vertex records

The transform pass `sub_4947F0` runs once per node, called from `sub_440CA0`
just before `Render_SubmitMesh`, and turns each **32-byte source vertex** into
a **48-byte transformed vertex**:

| source +off | field |
|---|---|
| +0, +4, +8 | position, float[3] |
| +12, +16, +20 | **normal**, float[3] — used only for the environment-map UV below |
| +28..+31 | **B, G, R, A** — the baked light, as a D3D `0xAARRGGBB` dword |

| transformed +off | field |
|---|---|
| +0, +4, +8 | camera-space x, y, z |
| +12 | 1/z |
| +16 | `(int)(z * 16384.0)` — the depth the near/far bucket bits are cut on |
| +20, +24 | screen x, y |
| +36, +40 | second UV set (environment map) |
| +44 | the colour dword, copied whole and clamped |

`Render_SubmitMesh` then copies +20/+24 as the screen position, +8 and +12 as
`sz` and `rhw`, +36/+40 as `tu2`/`tv2`, and **+44 straight through as the
D3DTLVERTEX colour**. So a face's shade is entirely its vertices' authored
colour, modulated by nothing.

### The channel order is proved, not guessed

`vertex + 28` is copied as one dword, so nothing in that path says which byte
is which. **The vertex format does.** `Raster_DrawTriangles` calls
`DrawPrimitive(D3DPT_TRIANGLELIST, fvf, verts, n, 24)` with two FVFs, and the
colour is `D3DFVF_DIFFUSE` in both:

| FVF | = | meaning | bytes/vertex |
|---|---|---|---|
| **452** | `0x1C4` | `XYZRHW \| DIFFUSE \| SPECULAR \| TEX1` | **32** |
| **708** | `0x2C4` | `XYZRHW \| DIFFUSE \| SPECULAR \| TEX2` | **40** |

and both sizes are exactly the strides the two emit loops step by — 32 for the
plain path, 40 for the environment-map one that carries a second UV set. A
`D3DFVF_DIFFUSE` field is a **`D3DCOLOR`**, which is `0xAARRGGBB`: little-endian
byte 0 **B**, byte 1 **G**, byte 2 **R**, byte 3 **A**. That is the D3D ABI, not
an inference, and it settles the order without any judgement about which render
looks nicer.

It is corroborated from a second direction, and the corroboration is worth
naming precisely because of where it lives. The **second render back end**
(`sub_42FF80`, §4b) converts the same bytes to grey with

```c
v = (587 * tri[69] + 299 * tri[70] + 114 * tri[68]) / 1000;
```

— the standard luma coefficients, **0.587 G, 0.299 R, 0.114 B** — against a
triangle-record colour that starts at +68, which names the three bytes B, G, R
in that order. Same answer.

> **Correction, 2026-09-01: that code is NOT unreachable.** This paragraph, and
> the box below, said it "never runs in the shipped build". `sub_42FF80` is
> `0x004C4918[1]`, one of **six two-entry arrays** at `0x004C4910` that
> `sub_42FA00(bank)` swaps as a set — five function pointers the renderer calls
> indirectly plus an activate hook. `Game_Init` installs bank 0; **VM opcode
> 150 is `sub_42FA00(1)`**, and the shipped world scripts carry **14** sites of
> it against **15** of opcode 151, the restore, across eight chunks that are
> not obscure: Mahaleel, Anekbah Grotte Gandhar Light, Jaunpur Zone 24,
> Lahoreh Konshu, 1-13 Morgue, 1-12 Anissa Aka's Bar, 1-02 Appart Kayl
> Rencontre and 1-20 Concert Bowie Bar 02. The near-pairing is what says these
> are live switches rather than debris.
>
> The `dword_6A05E0` gate at the top of those handlers is not an off switch
> either: **22 of the 153 VM handlers** open with the identical five
> instructions, `fade.to_black`, `fade.from_black` and `game.restart` among
> them.
>
> **What this does not overturn**: the sets are still drawn in COLOUR, because
> that rests on `Raster_DrawTriangles` declaring `D3DFVF_DIFFUSE`, which is
> independent of this bank, it is what bank 0 does, and bank 0 is what every
> frame outside the fourteen brackets uses. What falls is only the supporting
> sentence.
>
> **And bank 1 is now identified.** `dword_90E09C` is the SCENE renderer —
> `Render_Frame` calls it once a frame after the buckets are filled — and its
> two implementations are the same 0x4000-bucket walk of near-identical
> length, `Render_FlushBuckets` **659** lines against `sub_42FF80`'s **660**,
> differing in that bank 1 converts every vertex colour to luma grey: **17**
> occurrences against **0**. All 14 sites of opcode 150 are closed by a later
> 151, and **74 of the 82 instructions between them are camera and fade
> opcodes**, so the bank brackets a **cutscene**. The game has black-and-white
> cutscenes, in the eight chunks above. `SCRIPT_VM.md`'s earlier guess that
> the pointers "draw the 2D sprites" is superseded; the other four pointers
> remain unread.
>
> **And the player confirms it** (2026-09-01): asked whether the game has
> black-and-white cutscenes, the answer was yes. The prediction ran from the
> bytes outward — it was not fitted to a remembered effect — which is the
> strongest standing this claim can get without a capture rig that records a
> cutscene. It does **not** confirm the eight-chunk list. `verify.py: render
> back ends`, [`SCRIPT_VM.md`](SCRIPT_VM.md) §150/151.

### Why it matters: the sets are in colour and the viewers were not

The reference importer renders `Color.white * lighting.g` and this repo
followed it, taking byte +29 as a brightness. **157562 of the 405537 vertices
in the shipped sets — 38.9% — do not have r == g == b.** Anekbah is the obvious
case: its street runs from `(0.99, 0.95, 0.80)` warm white under the lamps to
`(0.20, 0.25, 0.33)` cold blue in the shadow, and read through the green byte
the whole city came out olive-grey.

Both viewers now carry the colour: `decor_geometry` returns `(r, g, b)` per
vertex, `/api/decorgeo` ships 8 floats a vertex instead of 6 (the stride is
measured from the payload, never assumed), and `aLit` is a `vec3` in both
clients — characters, whose light is a flat white in every shipped model, send
it up all three channels.

> **Which mode is the game?** **Colour.** The `lights` button's *grey* setting
> and `camshot.py --grey` are **this repo's own bug**, kept only so the two can
> be compared on one frame — they are not a mode the original has. The engine
> does own a luma-to-grey conversion, but it is in the second back end that
> `sub_42FF80` heads and **nothing shipped installs it** (§4b), so no player
> ever saw it. **(Both halves of that last clause are wrong as of
> 2026-09-01** — see the correction above: opcode 150 installs it at 14 sites
> in eight chunks. The **conclusion** stands on the vertex format instead.)**
> If a screenshot of the real game is the reference, compare it against
> **colour**.

### The clamp, and the two animated flags

The copy is clamped between the scene's `+416` and `+420` — a whole-dword
compare, not per channel, which is crude but is what the code does.

Two mesh flags animate the colour after it is read, and they are **not** the
`0x40000000` `.SFX` effect of §4:

* **`0x8000000` — the vertex colour SHIMMERS**, and the table settles what
  kind of animation it is. `byte_4DDBB0` is **32 entries running 0 → +64 → 0 →
  −64 → 0**, and the assembly reads them with **`movsx`** — signed. So it is a
  full oscillation about zero, not a pulse. The set path (`sub_4947F0`, taken
  because a set's node carries no dynamic light) adds it to all three channels
  through the identity clamp table:

      colour = clamp(src + wave[(clock >> 2 + (vertexAddress >> 4)) & 31])

  The clock (`dword_907310`) advances **2 a frame and wraps at 256**, so `>> 2`
  gives a **32-step cycle over 64 frames — about 2.1 s**. The address term
  steps by 2 per vertex, since the vertices are a contiguous 32-byte stride, so
  neighbours are 1/16 of a cycle apart and the shimmer travels across a
  surface. That is reproducible as `2 × vertexIndex` even though the address is
  not, which is what `/cutscene` does (the phase rides as a 9th float per
  vertex, −1 meaning none).

  The **lit** path's variant, `sub_494180`, offsets through
  `byte_6A2C60`/`byte_6A2C80` instead and so darkens as well as pulses — but
  that path is for objects registered with the dynamic light manager, which no
  set mesh is (§4c), so it is the actor variant and is not implemented.

  **233 set meshes carry it, 0 characters**, and the names put it on distant
  scenery rather than on lights: `berg*` (91), `fond*` (background, 52),
  `paro*`, `mont*`, `mafo*`. **Lahoreh** has 132 of them — 15% of its
  vertices — where Anekbah has one.
* **`0x4000000` — a second UV set, generated.** `tu2/tv2` come out as
  `asin(normal · cameraAxis) * 2/π + scroll * 2.5 + 0.5` — a spherical
  environment map, and the reason the source vertex carries a normal at all.
  This is the flag behind bucket bit `0x40`, which turns on **texture stage 1**
  in `Raster_DrawTriangles`.

  **The pass is dead in the shipped build.** The only texture stage 1 is ever
  given is `g_D3DTextures[dword_4C9520]`, and `dword_4C9520` is initialised to
  `-1` and **never assigned anywhere in the binary** — the three writes are all
  `-1`. The data agrees from the other side: **2** meshes in all of `DECORS`
  carry `0x4000000` and **none** in `PERSOS`.

### The dynamic lights — real, and not for sets

`sub_440CA0` takes a second path when `node+180 != -1`, collecting lights that
reach the node's bounding sphere (`sub_48E590` / `sub_48E980` / `sub_48E9C0`)
and accumulating them per vertex in `sub_493E40`, whose light record carries a
position at `+60/+64/+68`, an inner and outer radius at `+28` / `+24`
(and the outer squared at `+240`), and an intensity at `+32`, with a **linear**
falloff `I * (1 - (d - inner) / (outer - inner))`.

`node+180` is set only by the function whose own error string is
**`"LightObject, internal error, object %s has a parent"`**, and its two callers
are **`Actor_LoadModel`** and **`Object_Load`**. Sets never pass through
either, and the unlight path is gated on **mesh flag `0x8`, which no shipped
mesh carries** — 0 of 12203 in `DECORS`, 0 of 3517 in `PERSOS`. So the dynamic
lighting is for characters and props, and a set is baked, full stop.

Not implemented in the viewers: characters ship a flat white baked light, so
they are drawn unlit, and the dynamic pass would need the 304-byte light
records at `scene[8]` (169 of them in ANEKBAH) decoded first. That decode is
**not** done — the four fields above are what `sub_493E40` reads, nothing more.

---

## 5. Not established

* The remaining `.3DO` sections — doors, cameras and lights are parsed by the
  reference importer but not here.
* Quads are not handled — no `PERSOS` model uses them, but scenery does.
* The 44-byte header at the start of each `.3DM` frame record — a smoothly
  varying `float[3]` of length ≈ 0.985 and two non-unit 4-float groups.
* The mesh-flag meanings are `Omikron.txt`'s, several marked uncertain there.
* Nothing maps a `.3DM` to its `.3DO` at runtime in a way that has been traced
  through the executable — the scene-object route above is inferred from
  `Dialog_Load` and `Morph_Play`, not followed end to end.

---

## 6. Skeleton animation

**verified against the engine.** The demuxer `sub_42D960` (0x0042D960) reads
each frame as:

```c
for (i = 0; i < trackCount; ++i) {
    if (i == rootTrack)  read 12 bytes -> root translation
    read 16 bytes        -> quaternion[i]
}
read 24 * vertexCount    -> vertices
```

So a frame carries **`nodeCount` quaternions, one per track** — and the
12-byte block is *not* a header at the start of the record. It belongs to the
root track and sits wherever that track falls, which for a typical character is
after two other tracks. That is the whole reason a naive scan for one aligned
quaternion array lands at offset 44 and finds `nodeCount - 2` of them: the two
tracks before the root are simply skipped.

With the root track located, **every quaternion is unit** (380/380 sampled on
`000000.3DM`).

* The **preamble** is the track table: `nodeCount` uint32 ids, which are mesh
  indices in `.3DO` file order. Every shipped file has a plain `0, 1, 2, …`,
  so track *i* drives mesh *i*. The last mesh — the face — has no track; its
  vertices are animated instead.
* The **root track** is the one whose mesh has no parent. The player finds it
  by matching the root node's id against the track ids
  (`if (rootId == *v22) rootTrackIndex = i`).
* The root's 12 bytes are a **per-frame delta**, accumulated by the player
  (`f32(v4, 212) += ...`), not an absolute position.
* Quaternions are **w-first**, **parent-relative**, and stored as the
  **conjugate** of the rotation to apply — negate the vector part before using
  them. This one is easy to miss: without it the torso and head still look
  correct when the viewer cancels the root's own rotation for display, but
  every limb bends the wrong way. Arms swing backwards and knees invert, so a
  character meant to be sitting on a chair reads as diving forwards. With the
  conjugate, characters sit, stand and gesture naturally.
* Rest positions are absolute, so only the position accumulates:

```
rot[m] = rot[parent] * q[m]
pos[m] = pos[parent] + rot[parent] * (rest[m] - rest[parent])
```

and a vertex is `rot[m] * local + pos[m]`.

The root's rotation is the character's orientation in the world, so played
against a fixed camera it lays the figure on its side. `omkdata.pose()` cancels
it by default (`upright=True`) — **and that is a viewer convenience, not the
engine** (settled 2026-09-02, [FILE_FORMATS](FILE_FORMATS.md) 5, "How a line
BLENDS"). The engine applies every recorded rotation, the root's included,
RELATIVE to the actor's own frame: `sub_42D120` means to replace the root
track's key with identity, but `g_MorphRootTrack` is only ever −2 in the
shipped image, so the write misses. It is what bows Kay'l's whole body toward
the camera on `125339` — pelvis→head 47° at frame 420 with the root kept, 3° cancelled —
and `engine/` keeps a line's root whenever a scene object stages the speaker
(the cancellation stays as its fallback for a speaker no object drives). The
web viewer still cancels it.

    python3 tools/modelpreview.py TEL_FNM out.png 000000 60          # posed
    python3 tools/modelpreview.py TEL_FNM out.png 000000 60 --rest   # T-pose

### Dead ends, recorded so they are not retried

* **The engine's named bone table.** `Morph_Play`'s setup at 0x0041A9xx
  resolves meshes by name into actor-record slots 3..19 — `Visage, Tete,
  Buste, Epauleg, Brasd, Brasg, Avantd, Avantg, Maing, Maind, Cuisseg,
  Cuissed, Jambeg, Jambed, Piedg, Piedd, Ventre`. Exactly 17, which is how
  many quaternions a scan-from-offset-44 finds, so it looks like the answer.
  It is not the stream order; it is the engine's own lookup for attachments.
* **Hierarchy (depth-first) order**, with or without an index shift. It gets
  the arms, torso and head right and the legs wrong, which is a convincing
  near-miss — but it was only ever needed because two tracks were being
  skipped.
* **Taking the quaternions at face value.** They are conjugated; see above.
* **Scoring a mapping by how little it deviates from the rest pose.** Any such
  metric is degenerate: applying no rotation at all scores perfectly. A greedy
  per-bone search against one duly converged on "animate nothing".

## 7. ANIMS — body animation

`gamedata/ANIMS` holds the body-animation system, entirely separate from the
dialogue morphs. Two container types.

### `.ani` — animation libraries (solved)

Magic `"3.0V"`, loaded by `sub_434010` (0x00434010), which reads the file whole
and relocates its offsets in place.

```
+0   char[4]  "3.0V"
+4   int32    groupCount
+8   group[groupCount], 24 bytes: { int32 index; int32 firstClipOffset; ... }

clip node, 36 bytes, singly linked:
     +0  int32
     +4  int32   slot
     +8  int32   offset of the animation descriptor
     +20 int32   -1
     +24 int32   offset of the next node, 0 at the end
     +28 char[8] clip name

descriptor:
     +0  int32   frames
     +4  int32   boneCount
     +8  int32
     +12 bone[boneCount], 40 bytes:
             +0  char[20] bone name
             +20 int32 posKeys;  +24 int32 posOffset   12 bytes per key
             +28 int32 rotKeys;  +32 int32 rotOffset   16 bytes per key
             +36 int32 flags
```

**Both track offsets are relative to the descriptor, not the file.** Read as
file offsets they mostly still land on quaternions — the file is full of them —
but the root bone's tracks fall inside the bone table and decode to garbage.
That is the giveaway, and it is worth checking the *root* specifically rather
than an average.

**verified.** With the right base, **all 243362 rotation keys across all 265
clips in the 11 `.ani` files are unit quaternions** — every 16-byte group, not
a sample. Only the root carries translation. Re-measured by
`tools/verify.py --slow`; an earlier pass quoted 30842, which was a narrower
count over part of the tracks.

### Key 0 is a rest sentinel, not frame 0

**verified.** A track holds one *more* key than the clip has frames, and that
first key is the identity quaternion. So **frame `f` reads key `f + 1`**:

| | `.ani` | `.CTL` |
|---|---|---|
| tracks with `rotKeys == frames + 1` | 5193 / 5193 (100%) | 7795 / 8433 |
| tracks whose key 0 is identity | 5124 / 5193 | 8416 / 8433 |

The remaining `.CTL` tracks hold *fewer* keys than frames — they are sparse, and
clamp on the last key as before.

**Code-confirmed:** `Anim_ApplyNodeFrame` (0x00471690), the engine's per-node
animation visitor, clamps every frame to `>= 1` before indexing the key array
— the engine itself never reads key 0 during playback.

Reading key 0 as frame 0 puts a bind pose at the start of every clip, which
shows as a one-frame T-pose flash each time a clip loops. Two independent
checks confirm the shift:

* no clip contains an identity frame any more (they all did at frame 0 before);
* the loop seam closes. Comparing the first and last frame of all 398 `.CTL`
  clips, **124 are now exactly seamless (<0.01) against 0 before** — the walk
  and idle cycles are authored as true loops, and only under the `f + 1`
  reading do they actually join up. The other 274 are one-shot transitions and
  are not meant to.

### The root key 0 of a scene clip is the authored placement

**verified, both ways.** In a scene `.3DA` the root (bassin) track's key 0 is
not a rest pose: its *position* is the character's staged location in
absolute scene coordinates, and the engine consumes exactly that key —
`Anim_SnapRootToStart` (0x00471160) copies the root track's first position
key onto the node. Keys 1+ are the animation, back in local deltas.

740 of the 874 scene clips with a root position track carry a world-scale
key 0 (`verify.py: scene clip roots`).

**It is very probably the PELVIS's position, not the character's ground
point** — the root track is the bassin, and `Anim_SnapRootToStart` copies
that key onto the node. Measured on dialog 387: anchoring the posed pelvis
on the key lands Telis's feet at y **15** and Kay'l's at **16** against a
floor of **15.9**, the two agreeing with each other and with the room to one
unit, from authored data alone — where the floor probe they would replace
puts them **16 units apart** (under the player's authored x/z it returns the
**table top**, because he stands right beside it).

**Two of three faults solved 2026-08-30 and confirmed in play; the third is
OPEN** — it was mis-closed the same day and re-opened when dialog 401 came
back wrong. Which root position stages a speaker is a fit, not a reading; see
CLAUDE.md §6. The three:

1. the anchor was chosen from the current **pose meta** rather than the
   placement, so the speaker floated through each spoken line (reverted
   2026-08-28);
2. the player's matrix was built from the **npc's** `onAxis`, sinking him
   18–24 units and moving him whenever her pose changed;
3. **which root position to stage from — still open.** 387 needs the summed
   root and 401 needs key 0, both confirmed in play, and the two clips are
   structurally identical. The viewer picks by ground fit and says so.

Key 0 places the character and keys 1.. are the animation, as per-frame
deltas `Anim_RootDelta` sums onto the node — the rule this document already
states. For a clip that does not move, key 0 is the whole answer: 402's two
clips shift **1.0** and **2.0** units, which is why that conversation looked
right under either reading and could never have caught this. 387's are
sit-downs — `HO14_01R` drops its root **17.3** and `TELRES05` **19.1** — so
key 0 is where each character stands *before* sitting. The tell is exact:

| | key 0, above the floor | the model's standing pelvis→feet |
|---|---|---|
| Kay'l, `HO14_01R` | **41.7** | **41.8** |
| Telis, `TELRES05` | **45.4** | **44.9** |

Staged from key 0 the pair float 16–18 units up, seated poses hanging in the
air with the tabletop at their thighs. Staged from the **settled** root they
sit on the stools with their feet on the floor — 0.9 and 1.5 units off the
ground — which is what the game draws. `speaker_positions` now returns the
settled root and keeps key 0 beside it as `<who>RootKey0`.

**What caught it was a screenshot, and that is the lesson.** Every number
this repo can compute agreed with itself through both earlier fixes, and
`verify.py` passed each time. It took one frame of the running game — Kay'l
and Telis seated on stools with the table at chest height, against a render
that had the table at their thighs — to show the whole class of error. See
CLAUDE.md §1, "some errors are invisible at rest": this one is invisible at
rest *and* to every self-consistent check.

**One thing got worse, on purpose.** Applying the root's position lowers
bodies whose clips drop a long way while nothing lays them DOWN, so the
corpus count of bodies sinking more than 3 units goes **2 → 6**. The five
large ones are exactly the clips with a big drop and no matching root
rotation — `M_DEAD` 34.6 at a 0° swing, `KUS_DIAR` 19.3 at 3°, `TECINE12`
21.2 at 1°. What lays those down is the root **orientation**,
`Anim_RootDelta`'s optional 3×3, still untraced. Key 0 hid the question by
never moving them at all; this surfaces four concrete test cases for it.

In Re14: `TELRES05`
places Telis at (2496, −13.7, −6976.9) — two units from the camera-ray solve,
which had been close by luck — and `HO14_01R` places Kay'l at
(2535.7, −10, −6913.8), **41 units from the solve**, which is why the viewer
had him seated in the wrong spot. Both speakers are now staged from the clip
root where one exists; the camera solve remains the fallback.

**The anchor is carried on the PLACEMENT, not on the pose**, and that is the
whole of the fix. `speaker_positions` marks each speaker `pelvis` or `floor`
and the viewer's lift follows the mark: the pelvis's model-space **rest**
height for the first, the pose's or the model's feet for the second. The rest
height works in every pose for a reason the models supply — the pelvis is the
**hierarchy root** in all 181 character models, so `_compose` hands it
`tuple(m["pos"])` unchanged and no rotation can move it.

The reverted attempt (2026-08-28) instead chose the anchor from whichever pose
meta was current, and a line's own `.3DM` pose stream declares no `feet`, so
mid-line it silently fell back to the standing estimate: the speaker *floated
through each spoken line and snapped back afterwards*.

Two anchors really are needed, and one conversation shows both: 402's npc
comes from a clip root, while an `actor.goto_address` teleport — its fallback
for the player — names a spot on the ground (ADDRESSES 678 sits **2 units**
above Aapkayl's floor, not a pelvis height above it).

**The second half of the bug, undiagnosed until now.** The player's vertex
buffers are baked in raw model coordinates (`bakeStatic` subtracts no centre)
and he is a different model with a different pelvis, but his matrix was built
from the **npc's** `onAxis`. So he was displaced by *her* bounding-box centre:
measured through 387, sunk **18.1 to 24.0** units into the floor and moved by
up to **5.9** of them every time Telis changed pose, since `recentre()`
re-fits that centre on each one. He now gets his own axis matrix.

Measured, with the viewer's own matrices (`verify.py: dialog staging`):

| | 387 in AResto14 | 402 in Aapkayl |
|---|---|---|
| the two speakers' crowns, pelvis anchor | **0.1 apart** | 2.4 apart |
| the same, under the floor rule it replaces | **15.3 apart** | 2.5 apart |
| feet vs the room's own ground | 17.6 above it (they are on a bench) | within **0.5** |

402 is the confirmation and 387 is the discriminator: in the apartment both
speakers stand on one flat floor, so the floor rule looks right there too. In
the restaurant the surfaces under their two x/z **differ** — 15.9 (the bench)
under Telis and 31.7 (the room floor) under Kay'l, 74 units away — so
standing each on its own probe seats one of them most of a head below the
other. That is exactly the "the player sits at the wrong spot, and the NPC is
slightly off too" that play-testing kept reporting.

Across the corpus (`verify.py: dialog staging sweep`, `--slow`): **50** bodies
are staged from a clip root with ground under them, their feet land a median
**0.5 units** above it, **27** within three, and just **2** sink more than
three — both of those clips whose root *orientation* matters, which is the
part of the scene-clip root still only read and not confirmed. Bodies well
above the ground are authored that way: Astaroth hangs 333 units up in
PAstar, 387's pair sit 17 up on their bench.

**How it was tested — and note that these passed while the staging was
still wrong, which is why the screenshot was needed: they pin the
placement→matrix path and nothing past it.**
`tools/stagecheck.js` runs `tools/omkweb.html`'s own `stageMatrices` under
node with a DOM stub and walks a whole conversation through it — the scene
idle, every node's `.3DM` line, then the idle again — and asserts over the
*transitions*, not over any one frame: the staged pelvis must land on the
placement exactly (0.01) in all 15 poses of 387 and all 21 of 402, the player
must not move at all while the npc's pose changes, and the idle must stage
identically at the start and the end. `--floor` reproduces the old rule
through the same matrices for the contrast; `tools/stagerender.py` draws the
result. The fault both previous times was in the client, one level past where
every check stopped, and every server-side number was already right.

Bones are named `<prefix>Bassin`, `<prefix>Cuissed`, … so a clip maps onto any
character by name suffix: `ChBassin` in `policier.ani` drives `TeBassin` in
`TEL_FNM.3DO`. That resolves 19 of a character's 20 meshes — the face is the
one left out, and body animations do not drive it.

The bone list also **independently confirms the hierarchy order** that the
`.3DM` tracks use:

```
.ani:  Bassin Cuissed Jambed Piedd Cuisseg Jambeg Piedg Ventre Buste Cou Tete        Epauled Brasd Avantd Maind Epauleg Brasg Avantg Maing
3DO:   Bassin Cuissed Jambed Piedd Cuisseg Jambeg Piedg Ventre Buste Cou Tete Visage Epauled Brasd Avantd Maind Epauleg Brasg Avantg Maing
```

Clip names are descriptive where the authors bothered — `Assis` (sitting),
`Debout` (standing), `Poteau` (leaning on a pole), `rechg`/`rechd` (reloading)
— and `NULL` where they did not.

    python3 tools/anim_ani.py                    # every clip in every .ani

### Clip types — how the engine asks for an animation

**verified.** The engine never asks for an animation by name. Each clip node in
a `.ani` carries a **type at +0**, and `List_PickRandomByType` (0x004345E0)
walks the list — following the next pointer at +24 — collecting every clip of
the requested type and returning a **random** one. 91 call sites pass a literal
type; type 11 is the most requested, at 23 of them.

```
clip node +0   int32  type      the behaviour slot this clip can fill
          +24  int32  next
```

**Type 11 is ATTENTE — the idle.** Two independent lines of evidence: the
engine's fallback chain ends `"anim ATTENTE non existante dans le .ANI"` after
asking for type 11, and two of the shipped clips of that type are named
`attend`.

```c
case 10:
    v14 = List_PickRandomByType(list, 23);
    if (v14 || (v14 = List_PickRandomByType(list, 11)) != 0)
        ...
    Dbg_Trace("anim ATTENTE non existante dans le .ANI");
```

**The other types are deliberately left unnamed.** 30 of the 34 have no clip
names at all in the shipped files — every one is `"NULL"` — and the error
strings sit at the end of *fallback chains* rather than against a single type,
so attaching a name to them would be guesswork. What the data does support:
type 13 is the partner in `"anim ATTENTE et RECULE"` (backing away); types 14
and 16 hold the same contextual set (`Assis`, `Poteau`, `Scato`, `Appel`,
`Attend`); type 15 holds named set pieces (`Debout`, `path1`, `rechd`).

> A caution from getting this wrong once: the `case N:` labels in the behaviour
> switch are **not** the type constants. `case 9` reaches its "ATTAQUE PROCHE"
> error through a by-id lookup, not through type 9. And a naive regex over the
> call sites mis-reads `List_PickRandomByType(u32(a2, 20), 11)` as type 20 —
> the argument split has to respect nesting, or type 20 appears to be the most
> popular in the game.

### What a character does after a dialogue line

**verified — the answer is nothing in the dialogue data.** `IAM\DIALOG` carries
no indication of the character's state once a line's audio finishes. Four places
it could have been, and it is in none:

* the `DialogNode` is fully accounted for — 9 pointers, 4 branch targets, the
  id, a 10-byte asset name, 4 camera ids, 64 bytes exactly;
* the driver's dialogue commands 51-63 are pure data accessors; the only
  animation any of them names is the line's own `.3DM`, through command 58
  `Dialog_GetAssetName`;
* the script VM cannot address animation — its operand domains are the `.TAG`
  tables, and there is no animation TAG;
* `op_92`, the one media opcode the action scripts use, builds `%s.ADP` and
  `IMAGES\%s` — audio and pictures.

So the dialogue decides what is said and how it is shot, and the animation
system decides what happens next: the character falls back to a **random clip of
type 11 (ATTENTE)** from its own `.ani` library. Inside a `.CTL` machine the
equivalent is a state's `goto` — what it runs into when its clip finishes.

### `.CTL` — animation state tables — **solved**

Magic `"CE70"`. A `.CTL` is a **saved memory image**: every pointer in it holds
whatever address the authoring tool happened to have when it wrote the file.
That is why the trailing fields looked like stale heap addresses — they are.

The loader, `sub_45D270` (0x0045D270), never follows them. It walks the file in
a fixed order and *overwrites* each one, allocating the variable-length blocks
back to back as it goes. Mirroring that walk is the only way in, and
`tools/anim_ctl.py` does exactly that.

```
+0    char[4]  "CE70"
+12   int32    groupCount
+16   ptr      groups -> +88            (fixed up)
+76   int32    tableCount
+80   ptr      table                    (fixed up)
+88   Group[groupCount]                 32 bytes each

Group (32 bytes)
  +0  int32 id          how the engine requests a move set - the walker asks
                        for group 300 on a ladder mesh, Actor_TickNpc resets
                        states 16/17 through group 400; the Avnt/Meca/
                        Sham files carry clean ids (100 = the default group),
                        the combat files keep stale authoring pointers and
                        are reached through the graph only (Cef_FindGroupById)
  +4  int32 entryCount  +8 int32 flags (bit 0 = the file's default group -
                        exactly one per file, Cef_DefaultGroup)
  +12 ptr entries
Entry (88 bytes)
  +8  uint32 flags        gates the optional blocks
  +28 ptr -> 8 + 32*count  if byte +76 bit 3     +32 ptr -> 4 * byte@+86
  +36 ptr -> 4 * byte@+87                        +44 ptr -> 24  if flags & 0x140
  +48 ptr -> 20  if flags & 0x280                +52 ptr -> 40  if flags & 0x2000000
  +64 ptr -> 12  if flags & 0x10
  +68 ptr -> the 12-byte clip name, unless flags & 0x8002
  +72 ptr    the clip, filled in at run time
```

**What the flag-gated blocks hold.** Established from the shipped values and
the states' own names; the `0x2000000` combat block, `0x8002`, bit 2 and
bit `0x20` are now confirmed from their runtime consumers (below); the turn
and root-shift consumers remain untraced.

| flag | block | contents |
|---|---|---|
| `0x10` | +64, 12 B | ~~"appears nowhere else"~~ **the state's engine callback**: `Cef_QueueSpecialMove` (0x0045D0E0) matches it — player only — against the binary's own **`tab_special_move[]`** (its debug string's name; 66 rows of `char[8]` + function pointer at 0x004CB168, invisible to a string search because the names pack as dword pairs) and queues the handler. All **54 distinct shipped names, 209 sites, resolve**. The rows are gameplay events: locomotion (`MDSTAND/WALK/RUN/STOPR/STOPW/ROT000/ADJSTP`), actions (`MDGETOBJ/LETOBJ/NOTAKE`, `MDJUMP0A..03`, `MDDIVBEG/END`, `MDSLIDIN/OU`, `MDSNEAK0`, `MDSHOOT0`), camera modes (`MDCAMADV/SHT/FGT`), input tokens and the 2D-menu keys (`I2DM0..9`) |
| `+76` bit 3 | +28, 8 + 32·n B | the state's **effect records** — frame-triggered sprites and sounds, fully decoded from `Cef_UpdateStateEffects`/`Cef_SpawnEffect`/`Cef_TickEffects` (0x0045B260/0x0045B3B0/0x0045ADF0):
```
+0  f32 sprite duration     +4/+8  f32 active window (+8 = 0 -> open)
+12 f32 sound trigger time  +16    u32 sprite parameter
+20 u16 sprite id    the scene's chunk-4 sprite registry; an instance is
                     spawned at the attach point for the duration
+22 u16 sound id     played once when the effect clock passes +12 -
                     H_WALK carries the footstep pair (199/203)
+24 u8  attach point a small code mapped to cached actor nodes (hands,
                     head, feet - Actor_AttachPoint, 0x0045B120)
+25 u8  flags        1 = follow the bone every frame, 4 = random roll,
                     8 = double frame advance, 0x10 = kill on state change
+28 f32 scale
```
All **590** shipped records carry a sound (525) or a sprite (220) or both — none carry neither — with 0 malformed windows and every attach code in range (`verify.py: ctl effects`).<br><br>**The sound id is an ID, not an index** (read 2026-09-03), and that is the opposite of the scene programs': `Cef_TickEffects` resolves it with `Scene_FindSoundIndex` (0x0048CC80), which SEARCHES the resident scene's 26-byte chunk-3 records for a matching `+24` and returns that record's `+22` handle, where `Script_PlaySound`'s param 0 is a bounds-checked INDEX (`sub_48CB30`). So the same id names different sounds in different scenes — 34 is `AASC.WAV` almost everywhere and `STPR.WAV` in the Impasse — and **a state's footstep is silent in a scene that does not carry its id**, which is the engine's own behaviour. All **62** distinct ids the states name exist in some scene; none is orphaned. `H_WALK` is the case that shows what these records are for: sound **203 at frame 3** and **199 at frame 15**, one per footfall. And note it **loops its clip without being re-entered**, so the latch has to re-arm or a walk gives two footsteps and then silence — the tail of `Cef_TickEffects` zeroes the instance's latch words when its clock leaves the record's window, and for these open-window records what that reduces to is **not traced**; `engine/src/actor/channel.cpp` re-arms on the clip wrap and labels that a reconstruction (`verify.py: engine: actor sounds`) |
| `0x140` | +44, 24 B | a **turn** `(startFrame, endFrame, dX, dY, dZ, ?)` in degrees, added to the actor's Euler angles and wrapped 0..360 (`Cef_ApplyTurn`, 0x0045C1B0). The two bits are the two application modes: **`0x40`** = spread over the frame window during playback (`Cef_TickChannel` applies rate × frame-delta each tick), **`0x100`** = applied whole when the state is *left* (`GoToMove`) |
| `0x280` | +48, 20 B | a **root shift** `(startFrame, endFrame, dx, dy, dz)`, rotated by the actor's facing and added to his position (`Cef_ApplyRootShift`, 0x0045C2F0 — this is how `HLStep`/`HRStep` sidestep and the locomotion states advance). Same two modes: **`0x80`** = over the window per tick, **`0x200`** = whole on transition |
| `0x2000000` | +52, 40 B | the **combat action block**, now fully decoded from its consumer `Fight_ResolveHit` (0x0049A960) — see below. Present **only** in the three combat files: 46 entries in each `H1`/`f1cmbt`, 24 in `D1Cmbt`, zero in `Avnt`/`Meca`/`Sham` |
| `0x8002` | — | the entry has **no clip name** of its own; confirmed in code — the unloader (0x0045DBB0) frees every entry's clip *except* these. Bit 2 also redirects: `SetPersoBank` (0x0045A510, its own error string) follows the entry's `+40` GoTo chain to the state that owns the real clip |
| `0x20` | — | **the group's default entry** — `Cef_DefaultEntry`/`Cef_DefaultClip` scan for it (`H_STAND`, `HGUARD`, `MC_STAND`, `SH_STAND`…). The data is unanimous: **exactly one per group in all 202 groups** of the 7 files |

### The combat action block (flag `0x2000000`), decoded

**verified.** `Fight_ResolveHit` (0x0049A960) consumes every field:

```
+0   u32 flags     bits 1/2 = attack line - a defender in guard state 4/3
                   ignores it (high/low guard); 0x10000000 = knockdown
+4   i32 damage    1..25 in the shipped blocks; doubled when the victim is
                   the player, scaled by the attacker's [132] and reduced by
                   the defender's [136] stat multipliers; subtracted from the
                   defender's hit points (combat context +124, u16)
+12  f32 window start } the clip frames during which the attack can land -
+16  f32 window end   } the "hit frames" the data pass had already found
+20  i32 reaction     the entry the victim is thrown into (via
                      Cef_FindEntryByCode, matching the **low 16 bits** of
                      the entry id; -1 = none)
+24  i32 reaction 2   a second reaction id, -1 = none
+28  f32[3] knockback the victim's displacement, authored in the *reaction*
                      entry's own block, rotated by the victim's facing
```

**The fight setup** (`Fight_Begin`, 0x004455B0) builds both combat contexts
from character properties (event 44 → `Actor_GetProperty`), which the stat
block names: property **1 = Vie** (the bars' maximum), **16 = Carac Attack**
(×0.005 into the context's +132 multiplier), **18 = Carac Dodge** (×0.00125
into +136 — the "defense"; easy difficulty adds a flat +0.5 to the
player's), **19 = Carac Fight Experience** (a channel-rate scale — an
experienced fighter literally animates faster). The six standard reaction entries are cached per fighter by a **role
code in the low 16 bits of entry `+12`** (`Cef_FindEntryByCodeGlobal`) —
**3, 4, 5, 9, 18, 20**, with 9 the named `I_GROUND` knocked-down state —
into the context slots `Fight_ResolveHit` reads (+36..+48). The data is
exact: each combat file carries each role exactly once, the adventure
files none (`verify.py: ctl combat block`).

**The opponent's AI** (`Fight_TickAI`, 0x00464830) is data-driven and plays
by the same rules as the player: it **injects synthetic inputs into the
channel** (`Perso_InjectInput` — virtual key presses matched by
`Cef_InputMatches` like any real ones). Each fighter carries an AI profile
record: a random wait window (u16 min/max ms) for the idle/taunt states, a
table of taunt moves picked uniformly, and **percentage weights** for the
attack choices; past 2 m (78.74 in — the same metric authoring) it closes
distance, inside it rolls the weighted attack or, 25% of the time, one of
two specials.

All **232** reaction references across the three combat files resolve against
their file's entry ids by low-16 match, with **0** low-16 collisions —
`verify.py: ctl combat block`. On a kill (hit points reach 0) the engine
freezes both fighters' channels and records winner and loser in a global
pair; `Fight_FaceOpponent` (0x0049A550) turns the attacker toward the victim
by writing the facing Euler directly.

### The walker — how an actor actually moves

**read from the per-frame chain** `Actor_TickPlayerAndOpponent` /
`Actor_TickNpc` / `Actor_TickDialogue` → `Cef_TickChannel` →
`Actor_ApplyMotion` (0x004672D0) → `Actor_Move` (0x00469580):

* **gravity and velocity live on the actor record** — `+216/+224` velocity
  x/z, `+220` vertical speed integrated with the acceleration at `+228` and
  clamped at **787.4 in/s = 20 m/s** terminal; `+232..240` is the last safe
  position, `+244..252` the current one.
* each frame the walker *tries* the move, undoes it, and submits the delta to
  `Actor_Move` — an iterative **collide-and-slide** (up to 3 passes) sweeping
  the model's collision spheres against the world, with a **30° slope limit**
  and a step height of **11.87 in = 30 cm** (the third cm→inch constant in
  the engine), sliding along walls via axis-clamped normals
  (`Walk_ClampNormal`).
* if the probe under the actor (`Walk_ProbeGround`) finds a floor,
  `Walk_GroundResponse` snaps and handles the slope; **if it finds none the
  position reverts** — nobody walks into the void. A ground mesh flagged as a
  ladder switches the actor to `.CTL` group 300, scripted-state 11, and
  requests camera mode 21.
* `Walk_GroundResponse` (0x00465460) carries the rest of the walking rules:
  a rise is clamped so the head keeps **50 cm** (19.685 in) of ceiling
  clearance (an upward ray against the world); a step down larger than the
  30 cm limit is refused unless `walk.ledges.ignore` is in force
  (`g_IgnoreLedges`); a walkable slope adds its normal to the velocity —
  the slide — while one past 30° clears it; ground-mesh flags
  `0x10/0x20/0x40/0x80` are **conveyor surfaces**, pushing ±2 units/frame in
  x/z; and the accumulated fall distance (actor `+280`) grades the landing
  at **3 m** (118.1 in) and **5 m** (196.9 in) — the injury and death tiers —
  with `.CTL` **group 2** as the falling state and camera mode 18 its shot.
* **where the forward motion comes from - the CLIP.** `sub_45C680`
  (0x0045C680), the tail of `Cef_TickChannel`, case 1 (state 1 and 11..16):
  `sub_45CE90` -> `Anim_SetFrame(node, clip, prevFrame, frame, &d)` ->
  `Anim_RootDelta(clip, node+156, prev, cur)` sums the root's 12-byte
  position keys over `(prev, cur]` (fractional ends scaled: the head
  `(ceil(prev) - prev) * key[ceil(prev)]`, the whole keys, the tail
  `(cur - floor(cur)) * key[floor(cur)+1]`; both ends inside one key,
  `(cur - prev) * key[ceil(prev)]`) and rotates the sum by the 3x3 at
  node+156 - which is **actor+288, the facing matrix**
  `Matrix3x3_FromEulerAngles(+416,+420,+424)` rebuilt every frame in
  `Actors_TickAll` and installed on the node by `sub_437140(node,
  actor+288)` in `Actor_LoadModel`. The delta moves the node and is added to
  +244/+252 (and +248 only in states 11..14). `H1AVNT`'s `H_WALK` root
  carries 28 keys of 2.5-3.3 in/frame (2.1 m/s), `H_RUN` 20 of 7.4 (5.6
  m/s), `H_STAND` 1 (a sway); the root SHIFT blocks (0x80) are the
  phase-matched hand-overs between gaits (junction 11: run -> walk at
  -3.33/frame over frames 1..4) and the sidesteps. `Actor_ApplyMotion` then
  undoes the position delta since the last safe stance and hands it to
  `Actor_Move`, so the clip proposes and the collision disposes.
* **turning is the `.CTL`.** Left/right (word bits 0/1) open `H1AVNT`'s
  group-global entries 9/10 - a TURN `(0..1, 0, +-5, 0)` with flag 0x100,
  applied WHOLE by the walker-edge loop every tick the bit is held (5
  degrees a frame in place) - and, from `H_STAND`, the exact-match aliases
  19/20 into `H_SDLROT`/`H_SDRROT`, a turning clip whose window turn (flag
  0x40, 29_win32.c 330-410: the frames advanced inside `[start, end)` times
  the per-frame rate, through `Cef_ApplyTurn`) turns 4.75 degrees a frame.
  `Actor_ApplyMotion`'s 1/8-per-frame steer toward the motion direction is
  INERT for a root-driven walk: the delta was rotated by the facing it would
  steer toward.
* **nothing held is the idle word.** `sub_4A7A20` (0x004A7A20): `*word = 0;
  if (!held) *word = 0x40000000;` - and the stop edges are entries whose +4
  only FORBIDS bits (`0x20000` = `0x8000 << 2`, forward released), which
  `Cef_InputMatches` opens on any word without bit 2, the idle word
  included. The same function swaps bits 0 and 1 under channel flag 8 - the
  reverse gait reverses left and right.
**How a press gets INTO the queue — the commit** (`Cef_TickChannel`
0x004A853A..0x004A85B3, 29_win32.c 300-320). The word the tick polled is not
pushed unconditionally. It is committed only when the state's flag `0x1000` is
clear or an edge is still open for what remains of it after the latch and the
walker-edge loop have masked their bits, and only when it **differs from the
last word committed** (`+20`); a word wholly consumed becomes the idle word
`0x40000000`, and a queue already 16 deep drops it. Then two rules, both of
which decide *when* a press takes effect rather than whether:

* **a lone idle word is DROPPED.** `if (n == 1 && (queue[0] & 0x40000000)) {
  queue[0] = 0; n = 0; }` — the test is on the bit, not on equality.
  `SetPersoBankGroup`'s memset, and entry flags `0x1000000`/`0x4000000`, all
  leave the queue holding exactly that one word, and the queue pass reads only
  the FRONT — so without the drop the first press of a bank sits behind a word
  that opens nothing and waits for something to pop it. With it, the press is
  at the front and acts on the tick it arrived.
* **entry flag `0x20000000` CUTS the queue to one** when more than one word is
  queued, and does not push at all: the state refuses to accumulate a buffer of
  presses behind it. 21 of the 1286 shipped entries carry the flag — `HKWALK`,
  `HFWALK`, `H_SNKON`, `H_IN_WO`, `L_HGUARD`, `MC_STAND`, `MC_WALK`,
  `SH_KSTEP` among them, all of them gaits or guards.

* **and the push itself dedupes against the BACK**, not the front:
  `if (!n || queue[n-1] != word) queue[n++] = word;`.

**Nothing held is the idle word, and it is never 0.** `sub_4A7A20`
(0x004A7A20) opens `*a3 = 0; if (!a2) *a3 = 0x40000000;` and then ORs one bit
per bound key, so the word `Cef_TickChannel` commits is either a bitfield or
`0x40000000`. The same function swaps bits 0 and 1 under channel flag 8 — the
reverse gait reverses left and right. (`engine/src/actor/channel.h` names 0
`kQueueDrives` for the engine's *other* input path, flag `0x80`, where the poll
is skipped entirely and the queue is the only source.)

**And an entry's GoTo can be REWRITTEN at runtime.** `GoToMove` ends its entry
pass with `if (to->flags & 0x800) to->gotoState = from;` — a **dynamic return
edge**, straight into the engine's own loaded copy of the file — so every later
read of that entry's GoTo (the alias chase, the clip-end fall-through, the
`0x8000` skip, the `0x10000` phase-match target) sees where the state was
entered from and not what the file says. Two entries of `Sham.CTL`
(flags `0xC0084813`, reached from `SH_STAND`) ship with **no** authored GoTo at
all and are only reachable *because* of it. `verify.py: engine actor states`.

### `ACTOR_STATE` 0..17 — the machine the channel hangs off

**read from `Actors_TickAll` (0x004681C0) and every writer of the slot, and
RUN in `engine/src/actor/state.cpp` (`verify.py: engine actor states`).**
Int slot 101 of the 1312-byte actor record — byte `+404` — is what the whole
character system dispatches on; slot 102 (`+408`) parks the previous state
while a conversation or an interface screen holds the body.

| state | tick | what it is |
|---|---|---|
| 0 | `nullsub_6` | inert. `Actor_LoadModel`'s initial value; `Shoot_Leave` and an empty `Actor_StartPendingScx` land here |
| 1 | `Actor_TickNpc` | the ordinary actor: channel, separation push, walk, zone scan |
| 2 | `Actor_TickPlayerAndOpponent` | melee. **`Fight_Engage` is the only writer**, and it writes both fighters |
| 3 / 15 | `Actor_TickShoot` | shoot mode; the player also installs `.CTL` group 200 and camera mode 4 |
| 4 | `Actor_TickScxDriven` | an SCX scene object owns the body (slot `[43]`) |
| 5 | `Actor_StartPendingScx` | the resume half of `Morph_Play`'s suspend — its only writer |
| 6 | `Actor_TickChannelOnly` | the channel and nothing else; `MDACTION` writes it |
| 7 | **none** | the slider MOUNT. There is no `case 7`: it falls through `default:` and `Sliders_Tick` drives the actor instead, camera mode 8, delta halved |
| 8 | `Actor_TickChannelOnly` | the slider RIDE — `.CTL` **group 61** |
| 9 | `Actor_TickUiHeld` | an interface screen holds the body |
| 10 | `sub_466E70` | a full-screen bitmap holds it; leaves to `[102]` |
| 11 | `Actor_TickNpc` | the ladder (`Actor_ApplyMotion`, group 300, camera 21) |
| 12 / 13 | `Actor_TickNpc` | scripted variants; free-look is allowed in 1 and 13 only |
| 14 | `Actor_TickNpc` | **the water state** |
| 16 | `Actor_TickDialogue` | dialogue: group 400, input cut, `Dialog_TickUI` from the phase global |
| 17 | `Actor_TickUiHeld`, falling through to 16 | dialogue entered from state 9 — and it leaves to **1**, not to `[102]` |

Three corrections to what this file used to carry, all from pinning the
writers rather than searching the byte offset:

* **14 is the water state.** Its writers are `RSTNAGE` (0x0046C150 — *nage*)
  and `MDDIVEND` (0x0046BEF0), with `RSTAVNT` (0x0046C120) and `MDSW2SD`
  (0x0046BF20) writing 1 back. All four are rows of **`tab_special_move[]`**,
  so the `.CTL` machine changes `ACTOR_STATE` *through its own callbacks*.
  None has a `push` prologue, so none has a `proc` label and none is in the
  decompilation — the addresses were fixed by measuring each block and
  checking it aligns exactly onto the next row of the table.
* **7 and 8 are the two halves of one slider ride**, not "riding" and
  "channel-only". `MDSLIDIN` (0x0046B7F0) writes 7 and opens screen 7;
  `sub_468FA0` installs `.CTL` **group 61** and writes 8 to `[101]` *and*
  `[102]`; `MDSLIDOU` (0x0046B890) **refuses to dismount from anything but
  8** — "bad mode getting out of the slider !" is its own error string. Group
  61 is not in the group table above.
* **`Fight_Engage` (0x0041A3B0) is the only writer of state 2**, and it is
  invisible to a search for stores to `[reg+194h]`: it writes through the
  `dword_910834[328*index]` alias, as do `Actor_SetState` (0x00419C30 — which
  also drops the driving scene object), `Actor_LoadBankList` and
  `Player_SetActor`. A state the dispatch names with no writer is the tell
  that the *enumeration* is short, not the data.

**What the replica can and cannot claim about this.** The machine is ported
and run — 202 groups of the seven banks driven with the fourteen key bits and
with the files' own AI moves, 34056 states landed in, 12063 edges every one
re-derived from the file. But **it has no behavioural oracle and cannot be
given one**: the golden-trace logger sees only what a VM handler narrates, and
combat is two opcodes — `fight.begin` announces nothing and `player.become`
announces to CHARACTERS, which the logger filters out. `traces/fight.log` was
captured to test that and settled it the other way round. So the standard for
this row is **data-constrained, not engine-verified**, and `engine/README.md`
lists exactly which constraints, including the one the corpus cannot decide
(the priority rule, indistinguishable from a first-match rule on all 120
contested decisions).


### The camera modes

**read from `Camera_Request` (CLEAN) and its preset table** at `0x4C20C8`
(48 bytes per mode: eye offset xyz, target offset xyz — inches, authored in
metric like everything else — fov, flags). The modes met across the engine:

| mode | use | preset |
|---|---|---|
| 0 | the default third-person follow | **3.00 m** behind (−118.11 in), fov 75 |
| 3 | free look (`Actors_TickAll`, mouse/keys) | close over-shoulder |
| 4 | shoot mode (`Actor_TickShoot`) | long axis, 20 m clip |
| 8 | establishing shot | 7 m back, 2 m up |
| 12 | **framed** — dialogue line/reply cameras, zone cameras, `camera.set` | parameters from the request, not the table |
| 13 | travel (`player.move`) | request-driven |
| 14 | script hold | |
| 16 | head-look release (`Actors_TickAll`) | request-driven |
| 18/19 | falling (`Walk_GroundResponse`) | 3 m up, tight |
| 21 | ladder climb (`Actor_ApplyMotion`) | 2 m up, 0.5 m back |

**How mode 0 follows, read from the tick** (`sub_417CF0`, 04_sys.c 3672,
and its two resolvers). `Camera_LoadParams` puts the preset's eye offset at
+176 and target offset at +124 when their subjects are not -1, the fov at
+48, and the three trailing shorts at +136 (`f42`), +188 (`f44`), +192
(`f46`). Every frame, for any mode but 13: the absolute slots are copied to
the live eye (+52) and target (+64); `sub_415D10` sets the target to
`subjectPos - R(subjectEuler) * targetOffset`, chasing it by `dt / f42` (1
-> 2; a snap while flags & 0x81); `sub_415E60` chases a private Euler triple
(+76) toward the subject's by `dt / f46`, snapping any component within 0.1
degrees, and sets the eye to `subjectPos - R(+76) * eyeOffset`, chasing by
`dt / f44`. `sub_414F30` supplies the subject: `+244..+252` and
`+416/+420/+424` of the actor at +144 (subject type 0). So mode 0's row -
`(0,0,-118.11) / (0,0,0) / 75 / 3, 8, 8` - is a target a third of the way
per frame, a yaw an eighth, and an eye an eighth of the way to 3.00 m behind
the lagged yaw. **Not read to the end**: `sub_414520` case 0 (`sub_413C00`)
gives the mode flags 4|8|0x10 and tunables (+312/+316 = -0.7 x the actor's
height at +276, +300 = 1.2, +320 = 8, +324 = 4); flag 8 runs `sub_417070`,
271 lines that ray-test from the target toward the eye (`sub_444810`) and
pull the camera in and up to 0.7 x height behind a wall; flag 0x10 runs
`sub_416450`, a floor clamp re-probed when the subject drops more than two
inches. A world camera whose eye subject is 0 takes the same path (case 12
-> LABEL_19), so SCENE 55's camera 0 - eye (-1, 26, -119), target just
ahead - IS a mode-0 camera with its own offsets. `engine/src/actor/player.h`
ports the resolve and the lag and labels the two passes unported.

---

### The `.CTL` group ids the engine asks for

| group | meaning | requested by |
|---|---|---|
| 2 | falling | `Walk_GroundResponse` |
| 45 | get up / recover | `Actor_TickUiHeld` |
| 100 | locomotion — the file's default group | `Actor_LeaveDialogueMode`, `Walk_GroundResponse` |
| 200 | shoot stance | `Actor_TickShoot`, `Shoot_TickPlayer` |
| 300 | ladder climb | `Actor_ApplyMotion` |
| 400 | dialogue stance | `Actor_EnterDialogueMode`, `Actor_TickNpc` |
* steering: the facing at `+420` turns **1/8 of the remaining angle per
  frame** toward the motion direction — the smoothing every 1999 player felt.
* `+1308` on the actor is a one-shot "face along the spawn node" flag —
  resolved with the same −Z-heading recipe as everything else, then the
  channel resets to the bank's default group.

**What this settles about staging**: an actor's resting place is
**simulated**, not authored — the address teleport puts him near the spot,
then gravity, the ground snap and the collision spheres decide where he
actually stands. That is why no authored "final position" for the player
exists anywhere in the data (the parked viewer issue): reproducing the
game's placement needs a ground-snap against the set mesh, not more data
archaeology.

### The transition model — how a state picks its successor

**read from `Cef_FindTransition` (0x004A8BD0) and `Cef_TickChannel`,
corpus-validated.** The plain entry fields the format table left blank are the
*transition* data — an entry is not only a state, it is also the edge into
that state:

```
+4   u32 input code    a bitfield: low 15 bits = inputs that must be held
                       (subset match), bit N+15 = input bit N must NOT be
                       held (Cef_InputMatches); 0x80000000 = "no input" -
                       the idle/return edges. 640 of 1286 entries carry one
+16  f32 window start  } the frames of the *current* clip during which this
+20  f32 window end    } edge may be taken - a cancel window. 168 entries,
                       all sane (0 <= start <= end <= 70), verify-checked
+56  ptr               the owning group (load-time)
+84  u16 priority      among simultaneous matches the highest wins;
                       shipped values are only 0/1/2
```

Per tick the channel matches the queued input (the channel's small stack at
+24/+28 is the **input queue** — entry flags pop it or reset it to the
neutral sentinel on transition) against the current state's **children**;
a child with flag `0x80000` requires an exact code match instead of the
bitfield test. Two more entry flags widen the search: `0x4000` marks an
entry as a **group-global** transition and `0x2000` on the current state
lets it inherit those, so guard breaks and hit reactions need not be wired
into every state. Byte `+76` bit `0x20` scans the children in reverse.

The input bits are the engine's 14 key-binding slots (`Input_Poll` maps
binding k to bit 1<<k), and what each bit MEANS is per context:

| bit | | bit | |
|---|---|---|---|
| 0x0001 | ← | 0x0100 | |
| 0x0002 | → | 0x0200 | |
| 0x0004 | ↑ (walk — `H_WALK` requires it) | 0x0400 | sidestep (`HLSTEP`) |
| 0x0008 | ↓ | 0x0800 | run (`H_RUN`) |
| 0x0010 | combat row `A*` | 0x1000 | |
| 0x0020 | row `B*` | 0x2000 | holster (`S_ARMOFF`) |
| 0x0040 | row `C*`, `S_CROUCH` | | |
| 0x0080 | row `D*` | | |

> **Correction, 2026-08-30: the key names this table used to carry were the
> wrong table's.** They were the `.data` initialiser of `0x004C65B8` — E, R, D,
> F, LCTRL, SPACE, G, H, LSHIFT — and **`Game_Init` overwrites it before any
> play happens**. `Input_InstallScheme(group)` copies fourteen bindings out of
> the live tables, and the game installs a group as the player changes
> context: 0 at `Game_Init` and at the end of a fight or a shoot,
> **3 at `Fight_Begin`**, 2 at `Shoot_Enter`, 1 on the swim transitions. So
> the initialiser matches no scheme and is never what the player presses. The
> bit *semantics* above are unaffected — the schemes' own row labels
> corroborate them — and the real defaults are §"the four control schemes"
> below.

One matcher, per-bank meanings; `0x80000000` stays the no-input sentinel
for the idle/return edges. `verify.py: ctl transitions`.

### The four control schemes — **solved 2026-08-30**

A keybind row's apply hook (0x004902C0) writes its three codes to
`table[group*14 + action]`, the group at the option record's `+92` and the
action index at `+112`. That fixes the shape of the three compiled tables at
`0x004C8F90` / `0x004C9070` / `0x004C9150`: **4 groups × 14 actions**, one
table per device, restored into the live tables by
"Revenir à la configuration par défaut" and installed fourteen at a time by
`Input_InstallScheme`.

The groups are **contexts**, and the engine switches them where their names
say — `Game_Init` 0, `Fight_Begin` **3**, `Shoot_Enter` **2**, the swim
transitions **1**, and 0 again on the way out of each:

| bit | 0 Aventure | 1 Nager | 2 Tirer | 3 Combat |
|---|---|---|---|---|
| 0x0001 | Tourner à gauche ←| Tourner à gauche ← | Tourner à gauche NUM4 | Reculer ← |
| 0x0002 | Tourner à droite → | Tourner à droite → | Tourner à droite NUM6 | Avancer → |
| 0x0004 | Avancer ↑ | Avancer ↑ | Avancer ↑ | Sauter ↑ |
| 0x0008 | Reculer ↓ | Reculer ↓ | Reculer ↓ | S'accroupir ↓ |
| 0x0010 | Action / Utiliser RET | Action / Utiliser RET | Tir RSHIFT / **LMB** | **Coup de poing 1 Q** |
| 0x0020 | Annuler / Sauter SPACE | Plonger RCTRL | Sauter SPACE / RMB | **Coup de poing 2 W** |
| 0x0040 | — | — | S'accroupir RCTRL | **Coup de pied 1 A** |
| 0x0080 | Vue première personne L | — | — | **Coup de pied 2 S** |
| 0x0100 | — | — | Action / Utiliser RET | — |
| 0x0200 | — | — | Regarder En-Haut NUM8 | — |
| 0x0400 | Pas de côté / Demi-tour RCTRL | — | Glisser à gauche ← | Glisser à gauche DEL |
| 0x0800 | Courir RSHIFT | Crawl RSHIFT | Glisser à droite → | Glisser à droite END |
| 0x1000 | — | — | Regarder En-Bas NUM2 | — |
| 0x2000 | Ouvrir sneak TAB | — | Arme LALT | — |

Every joystick cell is button 0..9 in bit order plus the two axes; the mouse
table binds only four cells, all in `Tirer` and `Vue première personne`.

**The check the data could fail** is that three independent tables all land
inside the code space of the one function that produces those codes,
`Input_ReadOneControl` — a keyboard scan 1..255, a joystick button its index
plus 48, a mouse button 12..14. They do, with 41, 48 and 4 non-zero cells and
nothing outside its own space. `verify.py: key bindings`.

**Rebinding is group-local.** `Opt_RebindKey` scans the whole 74-row option
table but clears only a row in the **same** `+92` group, which is exactly why
`Avancer` is ↑ in Aventure while `Sauter` is ↑ in Combat and neither disturbs
the other. Codes 0, 1 and 4 are refused before the scan — 0 and 4 are the
joystick axes and 1 is ESC. Modelled in `tools/sim/ui.py: rebind`.

**And there is a second, confusable value.** The *queue* holds `0x40000000`
when nothing is pressed — `Perso_SetInputEnabled` resets it to exactly that,
length 1. It is not the same as the `0x80000000` edge code, and deliberately
so: `Cef_InputMatches` ends `if (a1 == 0x80000000) return a2 <= 0x2000`, so
the idle queue word matches **no** idle edge. That is what lets the AI put a
gap between two presses of a combo.

### The `.CTL` fight AI — the `+76` / `+80` table

**solved 2026-08-30.** The header map above listed `+76 tableCount` and
`+80 table` with no contents, and `tools/anim_ctl.py` walked past them because
the walk has to land exactly. They are the **fight AI**: 156-byte profiles,
which `Fight_FindAiProfile` (0x0045DCB0) selects by matching `+0` against the
**difficulty level + 1**.

```
+0        int32  the id: level + 1
+4  / +6  u16    a delay range in ms, used on entering the fight
+8  / +10 u16    a delay range in ms, used between moves
+16+12k   {int32 count, ptr moves, int32}   TWELVE situation slots
  move (16 bytes)   +4 how many input words, +8 the words
```

Slot **7** is the one `Fight_TickAI`'s state-6/21 branch reads, at `+100` and
`+104` — which is what fixes the slot stride and the field order.

**The AI does not drive the actor; it presses buttons.** `Fight_TickAI` picks
a move and hands the whole sequence to `Perso_InjectInput`, which loads it
into the actor's input queue — the same queue the matcher above consumes. So
a move is a combo like `E, idle, R, idle, F`, and the AI and the player go
through one state machine.

**Three of the seven `.CTL` files carry profiles, and they are exactly the
three combat files** — `H1Cmbt.CTL`, `f1cmbt.ctl` and `D1Cmbt.ctl`. (Two of
the seven are lowercase; a case-sensitive glob finds five and misses two of
the three profile sets. The same trap as the `.3DM` sweep in CLAUDE.md §4.)

| id | enter delay | move delay | H1Cmbt | f1cmbt | D1Cmbt | |
|---|---|---|---|---|---|---|
| 1 | 750–1250 ms | 500–1000 ms | 32 / 110 | 26 / 86 | 28 / 96 | the plain four attacks and short pairs |
| 2 | 100–400 ms | 250–750 ms | 47 / 173 | 45 / 165 | 35 / 136 | longer chains, sidesteps |
| 3 | 300–500 ms | **100–200 ms** | 47 / 212 | 46 / 196 | 31 / 140 | seven-input combos, and every `attack+G` |
| 4 | 500–1000 ms | **3000–5000 ms** | 5 / 12 | 5 / 12 | — | **one** attack and four walk directions |

*(moves / input words; `D1Cmbt` has no profile 4.)* A harder profile is mostly
a **shorter wait**, not a better move — the delay ladder is identical across
the files while the move counts differ. Id 4 is the outlier by an order of
magnitude and has no combos at all: a sparring partner, and only the two
Kay'l/female files have one.

The 30 distinct input words are all real bindings (`w & ~0x40000000` inside
the 14 bits), and their **union is `0xCFF`**: the AI presses ten of the
fourteen and never `CTRL`, `SPACE`, `SHIFT` or `TAB` — the four non-combat
ones. `verify.py: fight ai`.

> **Which profile the shipped scripts select is NOT established.**
> `Fight_SelectAiProfile` takes the level from `Fight_Engage`'s second
> argument; two of its three call sites pass a literal 1 (→ profile 2) and the
> third is inside opcode **62 `fight.begin`**, whose operand length the VM
> table gives as 4 and `tools/vm_oplen.py` reads from the handler as **6**.
> Decoding its fields under the disputed length would decide the answer and
> could as easily be wrong, so it is left open rather than counted.
> (The `Difficulté des combats` option is a separate thing: it sets
> `word_90E1A6`, which `Fight_Begin` turns into a flat **+0.5 / +0.25 / +0**
> bonus on one player stat — it does not choose the AI profile.)

### The shoot AI — four callbacks, and the data that was said not to exist

**Corrected 2026-09-01.** This section used to be headed "four functions, no
data at all" and ended "There is no table to read — which is what the plan
meant by calling this thin, data-poor territory." That is right about the
**dispatch** and wrong about the **AI**, and the difference matters.

The dispatch really is a `switch`. `Shoot_ActorEnter` (0x00422C10) raises event
44 for property **7** — the character type, record `+176` — and switches on it
to install one behaviour callback in the 192-byte shoot record at `+0`, which
`Shoot_TickNpc` (0x004279C0) calls each frame:

| type | | behaviour |
|---|---|---|
| 7 | `X-Tech` | `nullsub_9` — **nothing**; an inert target |
| 10 | `Gandhar` | `sub_47F6F0` |
| 13 | `Astaroth` | `sub_4800C0` |
| any other | | `sub_424DE0`, the generic shooter |

**But Gandhar is data-driven**, and so are the type names themselves. Five
tables sit together at 0x004CFA30 and **chain end to end** — each one's last
byte is the next one's first, and the last lands on the string the first
points at, which is the check a wrong base address could not survive:

```
0x004CFA30  the 14 character types    pointers to their own names
0x004CFA70  behaviour: healthy        14 {action, repeats}   -> 0x004CFAE0
0x004CFAE0  behaviour: wounded        10 entries             -> 0x004CFB30
0x004CFB30  behaviour: critical       13 entries             -> 0x004CFB98
0x004CFB98  action ENTER handlers     12 pointers            -> 0x004CFBC8
0x004CFBC8  action TICK  handlers     12 pointers            -> 0x004CFBF8
0x004CFBF8  'No one' — the first type name
```

The type table names them: **0 No one, 1 Man passer, 2 Woman passer, 3 Man
enemy, 4 Woman enemy, 5 Mecagarde, 6 Mecadog, 7 X-Tech, 8 Z-Tech, 9
Incarnable, 10 Gandhar, 11 Zombie, 12 Spectre, 13 Astaroth** — so the three
named cases above come from the binary rather than from inference, and the
model names corroborate them (`ZOH_FN` is a Zombie, `SPV_FNM` a Spectre,
`AST_FNM` an Astaroth, `MCG_FN` a Mecagarde).

**The behaviour script.** `sub_47FB40` (0x0047FB40) walks it: play the current
entry `repeats` times (counter at `+100`, index at `+144`), then step on; a
`{0, n}` entry rewinds. Which script plays is re-read **every frame** from the
health at `+92` — **> 100 healthy, ≤ 100 wounded, ≤ 50 critical** — and
changing band **resets** the index and the counter, so a wounded Gandhar
restarts his routine rather than resuming it. Expanded:

| band | the routine |
|---|---|
| healthy | 23, 23, 24×2, 20×2, 24×2, 23, 20, 23, 22, 18, 16×2, 17, 19 |
| wounded | 24×5, 23×2, 20×5, 23×2, 22, 18, 16×2, 17, 19 |
| critical | 24×5, 23×3, 24×5, 23×3, 20×5, 24×5, 23×3, 22, 18, 16×2, 17, 19 |

All three share the same five-action tail and differ only in the head: healthy
varies, critical repeats 24 and 23 five and three times over. It is one routine
getting more single-minded as he is hurt.

**The actions.** A code 16..27 picks a row through a `switch` **permutation**,
not by subtraction — 16→11, 17→0, 18→1, 19→2, 20→5, 21→6, 22→9, 23→3, 24→4,
25→7, 26→8, 27→10 — and each row is an ENTER handler (0x0047E230..0x0047E4F0)
that writes `+156`, sets or clears channel flag `0x800`, and picks an
animation by TYPE from the character's own list, plus a TICK handler run while
the action lasts. Action **16 is the only one with no animation**: it waits
`(rand() & 0x1F) + 30` frames, one to two seconds at 30 Hz.

**Astaroth** has no table — he is a hand-written machine on `+156` with states
16..21, 27 and 29, closing to **195** units to grapple, throwing inside
**273** with an impulse of 3700 / 2300 / 1200 by the **78** and **156** unit
bands, and timers of **150** and **60** frames. His speed and turn rate step
with health too, but on `<` where Gandhar's bands use `≤`: **1.0 / 1.5 / 2.0**
and **60° / 40° / 30°**.

**What the shipped data says about all four**, which is what decides how much
each is worth reading:

* **1032 character records**, of which **330 carry type `0xFFFFFFFF`** and
  reach the generic shooter through `default:` — that arm is load-bearing,
  not defensive;
* **317 `shoot.actor.enter` sites, 306 resolving in their own chunk**,
  splitting **302 generic / 3 Astaroth / 1 Gandhar / 0 X-Tech**;
* **nothing in the game is type 7.** Zero records. `nullsub_9` is unreachable
  content, like the six spell recipes whose gate is never 8.

**One thing that looked like AI and is not.** `Shoot_ActorEnter` also installs
a table at record `+84` — 0x004C3798 for everyone, 0x004C37E8 for Astaroth.
`sub_434C30` hands it straight to the poser `sub_471950`, so it is an
animation node mask, not behaviour. Located, not lifted.

`Shoot_Think` (0x00420AB0) is the navigation half: `sub_435020` picks a node
into record `+188` — except for Gandhar, who is hard-coded to node 1 — and
`sub_435770` packs a destination into `+136`/`+140`.

The tables are in `tables/shoot_ai.json`; the port and its limits are
`engine/src/actor/shoot.h` and `verify.py: engine shoot AI`.

Then the bulk of the file — 95% of it — is the animations: one `int32 length`
followed by a clip, per named entry, in entry order. Entries repeating an
earlier name share that clip and consume nothing, so the chain only lines up if
the same de-duplication is applied.

**A clip is an `.ani` descriptor with no `"3.0V"` wrapper.** The wrapper belongs
to the library file, not to a clip: `sub_45D1F0` hands each block straight to
`Anim_RegisterClip`, the same function `.ani` clips go through. That is why
searching a `.CTL` for `"3.0V"` finds nothing — and why the earlier note here
concluded the clip data "has not been located". It was in plain sight, behind
the pointer fix-up.

Verified: the walk lands **exactly on the file size for all 7 files**, all
**398/398** clips produce a valid descriptor, and all **296177** rotation keys
across their 8433 tracks are unit quaternions.

| file | bytes | groups | entries | clips |
|---|---|---|---|---|
| `D1Cmbt.ctl` | 690288 | 22 | 171 | 57 |
| `F1Avnt.CTL` | 1345872 | 57 | 274 | 81 |
| `H1Avnt.CTL` | 1334228 | 57 | 274 | 81 |
| `H1Cmbt.CTL` | 811376 | 25 | 233 | 77 |
| `Meca.CTL` | 146332 | 10 | 45 | 14 |
| `Sham.CTL` | 312492 | 6 | 56 | 11 |
| `f1cmbt.ctl` | 811032 | 25 | 233 | 77 |

The names are the state machine — `H_STAND`, `H_WALK`, `H_RUN`, `HGUARD`,
`HFWALK`, `MC_STAND`, `SH_RUN` — so this is where the standing and locomotion
animation lives, alongside the combat sets. All 398 are in the viewer's
animation picker (663 clips total, up from 265).

    python3 tools/anim_ctl.py                    # every .CTL, with its clips

### `.3DA` — scene animations, standalone and embedded in the SCX — **solved**

The third member of the clip family, read by `sub_46E880` (0x0046E880) both
for the standalone files in `gamedata/ANIMS` and for the clips streamed inside every
`SCPTDATA/*.SCX`:

```
+0   int32  frames
+4   int32  trackCount
+8   track[trackCount], 40 bytes:
       +0   int32    node          (0 = the root)
       +4   char[20] bone name     TeBassin (pelvis), TeCuissed (cuisse
                                   droite), TeJambed… — per-character prefixes
       +24  int32    posKeys       +28  int32  posOffset   12-byte keys
       +32  int32    rotKeys       +36  int32  rotOffset   16-byte keys, w first
then the key data, tracks in order.
```

The same descriptor family as `.ani`/`.CTL` — 40-byte bone tracks, keys =
frames + 1 with key 0 the rest sentinel — with one extra leading int32 per
track and offsets relative to the payload (fixed up to pointers at load).

**The SCX streamed section.** After the structural block, every record the
block declared streams its payload in block order. Each record opens with
`[u32 fileOffset, u32 size]` — the first word is **the record's own offset**,
which makes the walk self-checking — followed by `size` bytes.

**Chunk 4 is the exception, and reading it properly removed the last resync**
(2026-08-29). Its header is **three** words, `[own offset, model size,
texture size]`, and the payload is a whole `.3DO` immediately followed by its
`.3dt`. A walk that read only the second word ran short and had to scan
forward to the next self-locating header; read as `12 + model + texture` it is
exact — **230 sprite records, 0 resyncs**, every payload starting with `OD3X`.
`tools/anim_3da.py` implements it, `tools/sprite_fx.py` reads the sprites.

| check | result |
|---|---|
| SCX streamed walks landing exactly on the file size | **220 / 220** |
| embedded `.3DA` clips that parse | **1490 / 1490** |
| sampled rotation keys that are unit quaternions | **5710 / 5710** |
| standalone `gamedata/ANIMS/*.3DA` | 2 / 2 exact |

**These are the contextual idles.** `Script_SelectBodyAnimation`'s first
parameter names the target **bone** in the scene object's table (`TeBassin` —
the pelvis; the names are anatomy, not poses), and its second selects the clip
**by position** in the scene's animation array — by *id* dialog 402's selector
would land on `VIRTUAL.3DA`, whose `Vi*` rig is the wrong skeleton, which is
what settles position over id. So 402's `A_3_TelisStand` plays **`TE_STD.3DA`**
(Telis's flat pose) and 387's `Telis_eat` plays **`TELRES05.3DA`** — sitting at
the restaurant table. `omkdata.scene_idle()` resolves the chain (including the
scene→area hop through the `scene.load` corpus and a bone-prefix fallback for
launches that never touch the actor), and the viewer plays the result as the
speaker's idle.

**Scene clips are self-orienting.** Unlike the `.ani`/`.CTL` body clips, which
never animate the pelvis (H_STAND's first track is a forearm), a scene clip's
root track carries the character's **authored facing** — `TELRES05` holds a
constant 118.2° yaw, `TE_STD` 72.9° — so a renderer must not add its own
facing on top while one is playing. The yaw is a game-world angle: pushed
through the model-space mirror it comes out on the wrong side (first left,
then right, one sign apart — both observed in play); applied through the
game's own facing convention, forward = `(sin θ, −cos θ)` — the same rule the
`ADDRESSES` records follow — it sits correctly. The viewer strips the root
rotation from the served pose and applies the yaw that way. And the facing is
**for the whole conversation**: compared against a longplay of dialog 387, the
game keeps the seated orientation while the character speaks her `.3DM` lines
too — the line animations are authored for the scene's facing, not for "face
the player".

**An object's clips alternate.** `Telis_eat` carries two
`Script_SelectBodyAnimation` entries — `TELRES05` (sitting) and `TELRES02`
(eating) — and the game switches between them (observed in play). The viewer
cycles an object's clip list, one full loop each; the object's real program
presumably times the alternation, which is unread.

### `.3DP` — the paths: authored placement and motion — **solved**

Chunk 0 was noted as "meshes (.3dp)"; its streamed payloads are the engine's
**paths** — the loader names itself in its errors (`Read3DP`, `PtrPtrPath`,
`"not enough memory for paths keys"`). `Path_Read3DP` (0x0049FCA0):

```
u32 pathCount, then per path:
  char[20] name        Telisplace, Pos1Telis, Ported14, pass00…
  u32      duration    == the last key's frame, in all 6756 shipped paths
  u32      keyCount
  key[keyCount], 32 bytes:
      u32   frame
      float pos[3]
      float quat[4]    (w, x, y, z)
```

`Path_Sample` (0x004B0C70) evaluates one at a time *t*: keys bracketed by
frame, position interpolated linearly (mode 1) or cubically (mode 2), and the
quaternion turned into a 3×3 by `Matrix3x3_FromQuaternion` — the canonical
R(q). `Script_MoveObjectOnPath` drives scene objects and characters along
them.

**The facing convention, traced end to end and closed.** The engine's own
recipe (documented in `Morph_Play`) is *rotate −Z by the node transform; atan2
of the result is the heading* — and `Matrix3x3_RotateVector` applies matrices
in the **row-vector convention** (v′ = v·M, i.e. Mᵀ for column thinking). Run
`Telisplace`'s quat (pure yaw ≈121°) through exactly that chain and the
authored facing is **(+0.855, 0, +0.519)** — within 3.6° of what the viewer
renders via `(sin θ, −cos θ)`. Position agrees too: `Telisplace` puts dialog
387's seat 1.9 units from the viewer's independent line-camera solve. Both
halves of the staging are therefore confirmed three ways (camera rays,
authored path, engine math).

> The last staging feature, the **head look-at** (387's launch runs
> `character.look_at_player`; the renderer turns the head toward the other
> speaker via the actor's `+400` field, ops 138/139), is now implemented in
> the viewer: the head family yaws about the neck toward the player, with the
> rotation sign verified numerically against the engine's matrix conventions.

### Who chooses a `.CTL`

**from code.** An actor record names its own state machine: `+72` holds the
`.CTL` stem, and the engine appends the extension from a constant
(`dword_4C0D14` = `".CTL"`) before handing it to `Actor_LoadBankList`
(0x00419CB0) → `LoadBankList` (0x0045D970) → **`InitCEFFile`**. A `.CTL` is a
"bank list" in the engine's own vocabulary — that is what its error strings call
it.

The same record names the character's model at `+144` with `".3DO"` appended,
so a character's mesh and its animation graph are chosen together, from one
place. The four values that appear are `MECA`, `H1AVNT`, `F1AVNT` and `SHAM` —
matching four of the seven shipped files. See
[FILE_FORMATS.md](FILE_FORMATS.md) section 5e for the full chain from a
conversation to its speaker.

### The state machine

Entries are not just clip slots — they are the states of a machine, wired up by
`InitCEFFile`'s own link pass (its remaining error strings are "Parent not
found", "Child not found", "GoTo not found"):

```
Entry +0   int32  id          what the edges below refer to
      +32  ptr    parents     parentCount ids, resolved within the group
      +36  ptr    children    childCount ids, resolved within the group
      +40  ptr    goto        one id, resolved across the whole file
      +56  ptr    group       back-pointer, set by the link pass
```

**verified.** Every one of the 931 parent/child and 1113 GoTo edges across the
seven files resolves — `InitCEFFile` refuses to load the file otherwise.
Children and parents are the *same* edges stored both ways: all 931 child edges
are listed back as a parent by their target (100%).

Of the 1286 states, 581 carry an animation; the rest have `flags & 0x8002` and
are junctions. They cannot be skipped — the machine routes through them:

```
MC_STAND ──> MC_SD-WK ──goto──> MC_WALK ──> (junction) ──goto──> MC_WK-SD ──goto──> MC_STAND
```

That is the locomotion loop: stand, start walking, walk, stop walking, stand.
`children` is what the state *may* go to; `goto` is what it runs into when its
clip finishes.

The viewer's **states** button walks this: pick a file, click through children,
parents and GoTo, and each state's clip plays on the current model as you land
on it, with a breadcrumb of the path taken.

## 8. Dialogue cameras

`IAM\DIALOG` carries 1923 `DialogCamera` records. Each one is a fixed **eye and
look-at target**, the same shape as a `.3DO` scene camera. The movement during a
line comes from somewhere else — see below.

```
+0   int32  pos[6]      pos[0..2] the eye, pos[3..5] the look-at target
+24  int16  id
+26  int16
+28  int16  angle[0]    a small yaw; 0 on 1688 of the 1923
+30  int16  angle[1]    field of view in degrees; 74 on 1547 of them
+32  uint16 subject[2]  which speaker each endpoint is relative to, -1 for none
```

Values are stored in authoring units and converted by `Dialog_Load`, and the
offsets are relative to the speaker's **origin** - which sits at head height. A
model's Y runs from about -6.6 at the crown to +64.3 at the feet, so y = 0 is
the head and the common `y = -2` puts the camera just above it. Aiming at the
middle of the bounding box instead points the camera at the waist and throws
the whole move off.

Axes: a model faces **-Z** (the face mesh's centroid sits 3.7 units toward -Z
of the head's) and **+Y is down**. The camera offsets are given the other way
on both, so both are negated when placing one; taken at face value the camera
ends up behind the character looking at the back of its head.

It is the **horizontal** field of view, and the 3D view is letterboxed.

Measured off a capture of the running game (dialog 402 node 0, camera 4554): an
800x600 frame carries an **800x440** strip with exactly 80px of black above and
below — **1.818:1**. At 83 degrees *horizontal* on that aspect an offline
render of the set lands on the real frame feature for feature — the bamboo
columns, the plant, the sofa, the rug, the ceiling line; read as vertical it
does not, and the speaker ends up at roughly twice the right distance. That is
exactly what the viewer did until the capture exposed it.

    python3 tools/camshot.py 402 0 --over frame.png

| angle | vertical fov at 1.818:1 |
|---|---|
| 74 | 45.0 |
| 79 | 48.8 |
| 84 | 52.7 |
| 90 | 57.6 |

**verified.** The second angle is a field of view, not a rotation: 74 degrees on
1547 cameras, then 79, 84, 90 — the spread of a lens, not of an orientation.
`+26` is a camera *mode*, always 12 here, and `sub_4147F0` treats 12 and 20
specially. `+36/+38/+40` are non-zero on exactly the 33 cameras whose `subject`
is 6 (both speakers), so they are two-shot framing parameters, not timing.

### World cameras are set coordinates

**verified.** When `subject` is -1 the camera carries absolute world
coordinates, in the same space as the scene cameras stored in the set's own
`MESHES/DECORS/*.3DO`. Matching a conversation's world cameras to the nearest
set therefore names where it is staged:

```
Aapkayl  CAMERA12   eye 3507.2 1019.1 -906.8  ->  at 3437.2 1024.8 -861.9
dlg 402  cam 4554   eye 3535   1015   -907    ->  at 3421   1033   -884
```

164 of the 321 conversations have world cameras; 57 land within 250 units of a
set, and the names corroborate the match independently:

| dialog | | set | dist | runner-up |
|---|---|---|---|---|
| 387 | Telis/Déjeûner | `AResto14` (a restaurant) | 45 | 1687 |
| 402 | Telis/Appart | `Aapkayl` (Kayl's flat) | 71 | 1176 |
| 407 | Jenna/Appart/Qazef | `SAppt` | 84 | 1566 |
| 261 | Vendeur Fuan/hello | `Qfuan` | 113 | 1322 |
| 200 | Soks/Base1/1 | `Sawaken4` | 106 | 290 |

The remaining ~65% sit further out — those sets are either absent from
`MESHES/DECORS` or the conversation is staged somewhere the camera list does not
cover. The full table is `tools/dialog_decor.json`.

Note the set's *mesh vertices* are small (Aapkayl spans ±376 × ±78 × ±238)
because each of its 120 mesh records is placed separately; only the camera
block is in world space.

### Where the speakers stand

**verified against the game.** No file records where the two speakers stand,
and the obvious-looking answer is wrong: `pos[3..5]` is not the subject, it is
an **aim**, fixed at 768 raw units from the eye on 1615 of the 1670 absolute
cameras. Taking the median of those points — which this section used to
recommend — puts every speaker 118 units from the lens whatever the shot.

What locates a speaker is where the aim rays **meet**. Least-squares
intersection of a conversation's line-camera bundle:

| check | result |
|---|---|
| conversations with ≥3 usable line cameras | 89 |
| median residual of the bundle | **1.9 units** |
| same for the reply cameras | 9.1 units |
| dialog 402, from the rays | `3502.8, 1020.0, -901.2` |
| dialog 402, solved from a screenshot | `3502.3, 1018.6, -899.3` |
| **disagreement** | **2.4 units** |

The screenshot solution shares nothing with the ray fit: it takes a real frame
of the game, measures how many pixels tall `TEL_FNM`'s head is, and solves for
the depth at which a 9.2-unit head subtends that angle. It is self-checking —
fitted on head size alone, the feet then land at y=1081.5 against a floor at
1078.

    python3 tools/camshot.py 402 0 --speakers --over frame.png

The **player** is authored where the launching script teleports him
(`actor.goto_address`, 95 conversations, see FILE_FORMATS 5e); the reply-camera
bundle is the fallback and a loose one.

A `lineCamera`'s **eye** sits on the player's head — it is the player's own
viewpoint, not an over-the-shoulder framing. Across the 46 conversations where
the player's position is authored, the median line-camera eye is **61.2 units**
above that floor spot (quartiles 55.0 and 62.5) against models about 71 units
tall: eye height, to the unit. That is an independent corroboration of the
address being the player's, and it is why the "solo" button exists — from those
angles the player model fills the lens.

Two details matter for placing a model:

* **Anchor on the pelvis, not the model origin.** A character's origin is not
  inside the character: AMH_FN's body sits 570 units away from it, and even
  AKG_FN's is 16 units off. Turning a model about its origin therefore swings
  it sideways rather than spinning it on the spot, and since the player is the
  same model turned 180 degrees, the two ended up as much as 32 units further
  apart than they should be. The `*Bassin*` (pelvis) mesh is the body's
  vertical axis - it matches the bounding-box centre to within a unit, and 180
  of the 181 character models have one.

* **Take Y from the set's floor, not from the fit.** The convergence point is a
  head; standing the model on the floor beneath it is more accurate and
  self-correcting on sets with steps.

### Rendering: the world is right-handed with Y down

**verified against the game.** The engine's Y axis points down. A viewer that
maps a world point with `[x, -y, z]` and then looks at it with up = `+Y` has
applied a **reflection**, not a rotation, and its right vector comes out
negated: every frame is mirrored left to right. Nothing inside such a viewer
can show it — set, models and cameras are all reflected together and stay
consistent with each other.

Laying an offline wireframe over a real frame of dialog 402 settles it: with
up = `(0,-1,0)` and a right-handed basis, the bamboo columns, the plant, the
sofa, the rug and the ceiling line all land on their counterparts; mirrored,
the room comes out reversed. `tools/omkweb.html` corrects it by negating the X
scale of the projection once, which undoes the single-axis reflection for every
path at once — see `persp()`.

### The movement: two cameras per line, 160 frames

**verified.** `0x004013B0` issues camera command 12 **twice**:

| id from | duration (`dword_930818`) |
|---|---|
| `*a1` — the node's first camera field | `-1.0f` (immediate) |
| `a1[1]` — the node's second camera field | `160.0f` |

The driver fills those two from the node: command 53 passes
`lineCamera` and `lineCamera2`, command 57 passes `replyCamera` and
`replyCamera2`. So the view snaps to the first framing and then travels to the
second over **160 frames — 5.3 seconds at 30 fps**, which is why the movement
finishes well before a long line does.

**1237 of the 2348 camera slots are genuine pairs** (601 spoken lines, 636
replies), and the two ids are almost always consecutive — 39→40, 344→345,
393→394 — so they were authored together as a "from" and a "to". The rest name
a single camera (658) or none (453).

`subject` splits the set cleanly:

| | count | median \|A\| | meaning |
|---|---|---|---|
| `subject != -1` | 253 | 40 | offsets from that speaker |
| `subject == -1` | 1670 | 12305 | absolute scene coordinates |

So most cameras are placed in the world and cannot be reproduced without the
scene; the speaker-relative ones can be placed against a character directly.

**`subject` says *which* speaker, and it matters.** `0x004013B0` maps 0/1 to the
first speaker, 2/3 to the second and 6 to both. Of the 253 speaker-anchored
cameras, 112 hang off the first, 108 off the second and 33 off both - so
roughly half of them end up on the wrong character if the distinction is
ignored. The offsets are in that speaker's own frame, so a camera anchored to
someone facing the other way has its X and Z negated relative to the first.

**The player's position is not in the dialogue data.** A conversation header
names one speaker (`speakerObjectId`); the other is wherever the player happens
to be standing. The viewer therefore stands a second figure at a plain
conversational distance - directly in front of the NPC at 0.95 of a body
height, turned to face it - so that second-speaker cameras have something to
hang off.

### Which camera a line uses

**Named in the node itself.** `DialogNode`'s four trailing int16 are all
DialogCamera ids — every one of the 3132 non-(-1) values in the shipped file
names a camera in its own chunk.

The conversation driver is `sub_4067D0`, a command dispatcher reached through
`sub_4083F0(command, state)`; the loop that runs a line lives in `21_d3d.c`
around 0x0046xxxx. Two of its commands load a camera:

```c
case 53:                                  /* the character's spoken line */
    state->cameraId = Dialog_GetLineCamera(node);
    state->second   = Dialog_GetLineCamera2(node);

case 57:                                  /* the player's reply */
    state->cameraId = Dialog_GetReplyCamera(node);
    state->second   = Dialog_GetReplyCamera2(node);
```

and command 62 applies whichever was loaded, via `0x004013B0`. So:

| field | offset | used for |
|---|---|---|
| `lineCamera` | +60 | the line the character speaks |
| `lineCamera2` | +62 | loaded alongside it |
| `replyCamera` | +56 | the player's reply |
| `replyCamera2` | +58 | loaded alongside it |

**This was rejected twice before being found**, both times by the same bug: the
corpus test read the camera id from the record's offset 0 instead of +24, so it
scored 1% and looked like chance. Reading the right field gives **100%**. A
single chunk inspected by hand had matched all along; the aggregate test was
what was wrong, not the hypothesis.

## 9. The viewer

`tools/omkweb.py` serves a browser front end that walks a conversation with the
character rendered and animated, straight from `gamedata/` — nothing is pre-exported.

    python3 tools/omkweb.py          # then open http://localhost:8752

Textures and morph streams are expensive to decode, so they are served with a
long cache lifetime — which once meant a browser kept serving the output of a
decoder bug after it was fixed. The page is `no-store` and every cacheable URL
now carries a build stamp derived from the tools' modification times, so
changing a decoder invalidates the cache by itself.

* left: all 321 conversations, filterable; a small ○ marks the ones whose
  voice/animation is on another CD
* centre: the whole character in WebGL — every mesh, every material — with the
  face mesh driven by the `.3DM` when the model fits it. Pick a model whose
  face vertex count differs and the character still draws correctly, using its
  own face in bind pose; the HUD and the model list both say so rather than
  the head silently collapsing. Drag to rotate, wheel to zoom, and the
  **body / head** button switches between the full figure and a close-up of
  the animated face, and **skeleton / rest** toggles the body animation. The model picker lists models that fit the stream's
  `(vertexCount, nodeCount)` first, then every other character — so a line can
  be played on any body
* bottom left: the current line and the available replies, with hidden ones
  struck through
* the **idle picker** plays any of the 265 `ANIMS/*.ani` clips on the current
  model instead of the line's own animation, looping on the clip's own length
* **`cam`** follows the camera each line names — `lineCamera` for a spoken
  line, `replyCamera` for the player's — flying it along its move as the line
  plays, at that camera's field of view. Speaker-relative cameras are placed
  against the model; for the world ones only the movement is reproduced,
  applied from the current framing
* bottom right: a live trace — condition results, zone toggles, and every
  variable write with its `VARIABLES.TAG` name

The bytecode still runs server-side on the same VM as `tools/omkdialog.py`, so
the page never interprets anything itself; `/api/script/<dialog>/<offset>`
takes the current variables and returns the trace and the new ones.

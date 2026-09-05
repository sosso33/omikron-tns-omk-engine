# Omikron: The Nomad Soul — game file formats

Start at [`../CLAUDE.md`](../CLAUDE.md) for the working practice these
findings depend on.

What has been established by reading `Runtime.exe` and checking it against the
shipped data in `gamedata/`. Every claim is marked with how it was arrived at:

* **verified** — the code says it *and* the shipped files agree.
* **from code** — read directly out of the disassembly, not yet checked against data.
* **inferred** — a hypothesis the data supports but does not prove.
* **rejected** — checked and found false. Recorded so it is not re-tried.

Reproduce the data-side checks with `python3 tools/dialog_dump.py`.

---

## 1. IAM archives — the container

**verified.** Files directly under `gamedata/IAM/` with no extension (`DIALOG`, `AREA`,
`GLOBAL`, …) are flat archives. Read by `Archive_ReadChunk` (0x0040FF90).

```
offset 0   directory: an array of 8-byte entries
             uint32 offset      absolute, from the start of the file
             uint32 size        in bytes
             (0,0) means "no chunk with this index")
offset N   payload
```

The directory ends where the first payload byte begins, so its length is
implied rather than stored. `gamedata/IAM/DIALOG` has a 4096-byte directory = 512
entries, 420 of which have data.

The loader computes the entry address as `(index >> 8) * 2048 + 8 * (index & 255)`,
which is just `8 * index` written the long way — it reads a whole 2048-byte
sector at a time and then indexes inside it.

There is a second access path: when `Archive_ReadChunk` is called with a
positive fourth argument the directory is skipped entirely and the caller
supplies the offset and stride itself.

---

## 2. IAM\DIALOG — conversations

**verified.** One chunk is one complete conversation. Loaded by `Dialog_Load`
(0x00401800). 321 of the 420 chunks in the shipped file parse as conversations,
giving 1174 nodes and 1923 cameras.

```
+0   int16  speakerObjectId    scene-local; resolved to an object index on load
+2   int16  nodeCount
+4   int16  cameraCount
+6   int16  unread by the loader
+8   DialogNode   nodes[nodeCount]       64 bytes each
     DialogCamera cameras[cameraCount]   44 bytes each
     string pool                         packed, NUL-terminated
```

`8 + 64*nodeCount + 44*cameraCount` lands exactly on the first string in every
chunk, which is what confirms both strides.

### DialogNode (64 bytes)

```
+0   uint32 ptr[9]      file offsets, relocated to pointers on load; 0 = none
+36  int16  param[4]    branch targets: index into nodes[]. -1 = unused
+44  int16  id          the key the accessors match on
+46  char   name[10]    a 6-character asset id, e.g. "0C64BF"
+56  int16  field56     unknown
+58  int16  field58     unknown
+60  int16  field60     unknown
+62  int16  field62     unknown
```

**A node is one spoken line plus up to four replies.** The nine pointers group
onto the four branches:

| slot | role | evidence |
|---|---|---|
| `ptr[k]`, k=0..3 | condition script for branch k | 0x004012F0 runs it on the VM and returns the top of stack |
| `ptr[4+k]`, k=0..3 | action script for branch k | 0x004012B0 hands it out without evaluating |
| `ptr[8]` | string pool for this node | 0x004011D0 / 0x00401220 walk it as packed strings |
| `param[k]` | node to go to if branch k is taken | all 1452 used values are valid node indices |

**The reply menu's condition evaluation is observable in a golden trace**
(2026-08-29). `Dialog_TickUI` evaluates **every** branch's condition to build
the menu, and a condition that reads a variable does so with `push.var`, which
logs through the tag API like any other operand — so a capture records what the
menu *offered*, not only what the player took.

Node 12 of conversation 402 proves it rather than suggesting it: its three
conditions read, in branch order, `{669, 668, 667}`, `{671}` and `{670}`, and
`traces/telis-dialog.log` holds exactly `669 668 667 671 670` as one batch at
t=167.5 s. The consequence is a test with a control — scoring only the chosen
branch's **action**, *no* walk of the graph reproduces that capture; with the
menu build modelled, 36 do, and all 36 agree on the two choices the trace
actually determines. `verify.py: telis dialogue`.

**verified.** `ptr[k]` is essentially never present without `param[k]` also being
used — across 1174 nodes the exceptions number 3, 5, 1 and 1 for k=0..3, and 50,
3, 0, 0 for `ptr[4+k]`. A branch may exist without a script, but a script
without a branch is a rounding error.

**verified.** The string pool holds six strings per node, in a fixed order:

| index | role | fetched by |
|---|---|---|
| 0 | the line the NPC speaks | `0x004011D0` with index -1 |
| 1..4 | the text of reply *k* | `0x004011D0` with index *k* |
| 5 | the line the *player* speaks to open the exchange | `0x00401220`, which skips exactly 5 |

Across the shipped file 1072 nodes use only [0], 9 use only [5], and 19 use
both — so [5] is a real second slot, not padding. A node with only [5] is one
the player initiates, e.g. *"Ça y est, j'ai trouvé l'indice d'Aegmaar…"*
followed by four numeric answers as replies.

**verified.** `name` is the stem of a file in `MORPH/` — the voice recording and
lip-sync for that line. 750 of the 779 `.3DM` files present are referenced by a
dialogue node.

**rejected.** `field56`/`field60` looked like camera ids in the first chunk
examined; across all 1174 nodes they match a camera in their own chunk only
246 and 234 times, which is chance. Still unknown.

### DialogCamera (44 bytes)

```
+0   int32  pos[6]        eye = pos[0..2], look-at target = pos[3..5]
+24  int16  id            matched against a node id by Dialog_ApplyLineCameras
+26  int16  mode          camera mode; 12 on all 1923 shipped cameras
+28  int16  angle[2]      roll, then the **horizontal** field of view in degrees
+32  uint16 subject[2]    whose frame each of the two points is given in
+36  uint16 twoShot[3]    0 except on the 33 cameras with subject == 6
+42  2 bytes no code reads
```

A record is a **fixed viewpoint**, not a move - the same eye-and-target shape
as a scene camera in a `.3DO`. The movement during a line comes from a node
naming *two* cameras; see below.

`subject` selects whose position the camera uses: 0/1 → first speaker, 2/3 →
second, 6 → both, anything else → none (**from code**, `Dialog_ApplyLineCameras`).
**0xFFFF (-1) is not a speaker**: those carry absolute set coordinates, in the
same space as the scene cameras in `MESHES/DECORS/*.3DO`. 1923 cameras in the
shipped file; see [ASSETS.md](ASSETS.md) for how they locate a conversation in
a set and where the speakers stand.

It applies **per point, not per camera**: `Camera_LoadParams` (0x004146C0)
routes the eye into the camera's absolute slot (+20) when `subject[0]` is -1
and into its relative slot (+176) otherwise, and does the same for the target
with `subject[1]`. So one point can be anchored to an actor while the other is
absolute — which is what an over-the-shoulder shot needs.

### The look-at point is an aim, not the subject's position

**verified, and it matters.** `pos[3..5]` sits at a **fixed 768 raw units**
from `pos[0..2]`: 1615 of the 1670 absolute cameras are within 1 unit of
exactly that (768 raw = 118.1 after the unit conversion below).

| \|eye → at\|, raw | cameras |
|---|---|
| 768 | 1212 |
| 767 or 769 | 442 |
| anything else | 16 |

So the second triple carries **direction only**. It says nothing about how far
away the person being framed is, and treating it as the speaker's position —
which the viewer did — puts every speaker 118 units from the lens whatever the
shot. In a real frame of dialog 402 the speaker is 24 units away.

What does locate a speaker is where the rays **meet**. A conversation's line
cameras all aim at whoever is speaking, from slightly different places, so the
least-squares intersection of the bundle is that character's head. Across the
89 conversations with three or more usable line cameras the median residual is
**1.9 units** — the bundles really do converge on a point.

Checked against the game. For dialog 402 the bundle meets at
`(3502.8, 1020.0, -901.2)`; solving a screenshot of that line for the depth at
which `TEL_FNM`'s head is the size it appears on screen gives
`(3502.3, 1018.6, -899.3)`, **2.4 units** away, from two methods sharing no
assumptions. The screenshot solution is self-checking: it was fitted on head
size alone, and the feet then land at y=1081.5 against a floor at 1078.
`tools/camshot.py` draws the comparison.

The reply cameras converge much more loosely — median residual 9.1 — because a
reply shot is framed far more freely, so the player's authored `ADDRESSES`
entry is preferred where it exists (see 5e).

`mode` (+26) is read by `Camera_RequestChanged`, which substitutes it for the
requested camera mode unless it is 12 or 20. Since it is 12 everywhere in the
shipped file the substitution never fires with the game's own data.

`twoShot` (+36/+38/+40) is 0 on 1890 cameras and `(5, 8, 16)` on exactly the 33
whose `subject` is 6 - the shots framed on both speakers at once.

### One line, two cameras

**verified**, `Dialog_ApplyLineCameras` (0x004013B0). The node's four camera
fields are two pairs, and the function issues each pair as two camera commands
with different durations:

| id from | duration |
|---|---|
| `lineCamera` / `replyCamera` | `-1.0` — cut, immediately |
| `lineCamera2` / `replyCamera2` | `160.0` — travel there over 160 frames |

So a line cuts to the first framing and then moves to the second over **160
frames, 5.3 seconds at 30 fps** — which is why the camera settles well before a
long line has finished. 1237 of the 2348 camera slots name a real pair, and the
two ids are almost always consecutive (39→40, 344→345): authored together as a
"from" and a "to". The rest name a single camera (658) or none (453).

### On-disk units

**verified.** `Dialog_Load` converts every camera record once, at load. Constants
read from the binary (`dbl_4BC008` = 0.00390625, `dbl_4BC010` = 0.3937007874015748,
`dbl_4BC018` = 1.0, `dbl_4BC020` = 0.087890625); the FPU code uses `fild` and
`movsx`, so both inputs are signed.

| field | conversion | reading |
|---|---|---|
| `pos[]` | `trunc(v * 100/256/2.54) - 1` | 1/256 undoes 8.8 fixed point, 1/2.54 is inches-per-cm: centimetres in the file, hundredths of an inch in the engine. Real record: 252 → 37. |
| `angle[]` | `trunc(v * 360/4096)` | 0.087890625 is exactly 360/4096: a 4096-step turn in the file, degrees in the engine. Real records: 853 → 74°, 910 → 79°. |

The `* 100` happens in 32-bit integer arithmetic and can overflow. That is what
the original does.

---

## 3. The dialogue script VM

**verified.** This is how a conversation remembers what you did. Summarised
here; the full instruction set, the opcode table and a working disassembler are
in [SCRIPT_VM.md](SCRIPT_VM.md).

All 612 scripts in the shipped `IAM\DIALOG` disassemble cleanly, which
validates the model end to end.

Each node's `ptr[]` scripts are bytecode for a small stack machine. A context is
built by `sub_406290`, run by `sub_406460`, and its result read with a stack pop
(0x00401A80).

Context layout, as used by the handlers:

```
+12  uint8 *pc          program counter, into the bytecode
+16  int32 *stack
+20  uint16 sp          stack pointer, pre-decremented on pop
+36  int16 *fixups      operand indirection table
```

### Operand encoding

A 16-bit operand is stored little-endian-ish as two separate byte loads
(`cl = [esi]`, `dh = [esi+1]`). `0xFFFF` means "none". If bit `0x4000` is set,
the bit is cleared and the remainder indexes `fixups` — an extra indirection
layer, presumably so the authoring tool could patch operands after the fact.

### Opcode table

At `0x004C0140`, dispatched with `call off_4C0140[eax*8]`. Entries are 8 bytes:

```
uint32 handler
uint32 operandCount
```

Handlers identified so far:

| address | opcode |
|---|---|
| 0x00401C50 | unconditional relative jump |
| 0x00401C90 | pop, jump if non-zero |
| 0x00401CE0 | pop, jump if zero |
| 0x00401D30 | push 8-bit immediate |
| 0x00401D70 | push 16-bit immediate |
| 0x00401DD0 | push 32-bit immediate |
| 0x00401E30 | push the value of game variable *n* |
| 0x00401EA0 | drop |
| 0x00401B40 | debug: hex-dump 64 bytes of bytecode |

**Hex-Rays decompiled only 4 of the 153 handlers.** The rest sit after `align`
with no function label, so they are absent from `Runtime.exe.c` and from
`clean/` and `readable/`. They are in `Runtime.exe.asm` and have now been
extracted and decoded — see [SCRIPT_VM.md](SCRIPT_VM.md) for all 153.

### Game variables

**verified against the shipped tables.**

```c
Var_Get(i)      /* 0x0040E530 */  return gameDB->vars[i];
Var_Set(i, v)   /* 0x0040E510 */  gameDB->vars[i] = v;
```

where `gameDB` is `dword_4E6D94`:

```
+8   int32 *vars        the variable array
+12  int16 *sceneParent indexed by scene id
+24  uint8 *bits        a bitset, indexed by a 16-bit id (0x0040E540)
```

Roughly twenty VM opcodes call `Var_Set`, most of them paired with a `Var_Get` —
the shape of `+=`, `-=` and friends alongside plain assignment.

So: **conversation branches are guarded by bytecode that reads and writes a
global variable array, and taking a branch runs more bytecode that writes it.**
That is the mechanism behind the game remembering what you have already read or
chosen. The separate bitset at `+24` is a second, one-bit-per-id store.

---

## 4. IAM\*.TAG — name tables

**verified.** Plain Windows INI text, CRLF, cp1252:

```
[VARIABLES]
13=ObjetUtilisé
79=1-A Sorcellerie Ouverte
51=1 Section 1 Finie
```

`DIALOGS.TAG`, `VARIABLES.TAG`, `OBJECTS.TAG`, `SCENES.TAG`, `ZONES.TAG`,
`AREAS.TAG`, `CAMERAS.TAG`, `ADDRESSES.TAG`.

**The DIALOG files do not reference the .TAG files.** The link is positional: a
`.TAG` key is the numeric id used everywhere else — dialog chunk index for
`DIALOGS.TAG`, variable index for `VARIABLES.TAG`.

They are read at runtime by exactly one function, `sub_40EC70`, which is a
**debug logger**: given an id and a section name it does
`GetPrivateProfileStringA(section, "<id>", …, "IAM\<SECTION>.TAG")` and appends
the human-readable name to a debug window. Two sections are special-cased and
never hit the disk — `"VALUES"` is formatted as a bare number and
`"CHARACTERS"` is resolved from memory.

That is why the VM's push-immediate opcodes log under `"VALUES"` and the
variable-read opcode logs under `"VARIABLES"` first and `"VALUES"` after: it is
tracing "variable *n* (named X) has value *v*".

`VARIABLES.TAG` is therefore a readable index of the game's entire persistent
state — "1 Section 1 Finie", "1-A Dial Telis", "Inventaire", "Vie".

---

## 5. MORPH files — `.3DM` (PC) and `.DDM` (Dreamcast)

`.3DM` and `.DDM` hold the same thing — one recorded line of dialogue, with the
lip-sync/morph animation and the voice audio interleaved frame by frame — but
they are the **PC** and **Dreamcast** builds of it respectively. `gamedata/MORPH`
ships the PC `.3DM` files; the `.DDM` files under `MORPH_ANALYSIS/` come from
the Dreamcast version. Where the two diverge, the PC form is what matters.

`DialogNode::name` is the stem, so `0C64BF` in a conversation means
`MORPH/0C64BF.3DM`.

**verified.** Both start with the same 16-byte header:

```
+0   uint32  audio bytes per frame, always 368
+4   uint32  a geometry count; the record size grows 24 bytes per unit
+8   uint32  nominal frame count
+12  uint32  layout version
```

### PC `.3DM` — solved

The layout is not guesswork: the loader `sub_42C300` (0x0042C300) computes the
frame count with

```c
frames = (fileSize - 4*hdr[3] - 16)
       / ((hdr[0] & 0xFFFFFF) + 24*hdr[1] + 16*hdr[3] + 12);
```

which gives the header size and the record size directly. `hdr[3]` is a **node
count**, not a version — the earlier reading of it as a version was wrong, and
is what made five of the six "layouts" look unsolvable.

```
header, 16 bytes
    +0   uint24 audioBytes     per frame: 368 mono, 736 stereo
    +3   uint8  channels - 1   0 = mono, 1 = stereo, same as the .ADP header
    +4   uint32 vertexCount
    +8   uint32 frameCount     nominal; the real count comes from the size
    +12  uint32 nodeCount

preamble, 4 * nodeCount bytes
    uint32 index[nodeCount]    always 0,1,2,... in all 777 shipped files

then frameCount records of
    audioBytes + 24*vertexCount + 16*nodeCount + 12 bytes:

    +0                      44 bytes of header (see below)
    +44                     float  node[nodeCount - 2][4]  rotation quaternions,
                                                           w first
    +12 + 16*nodeCount      struct { float pos[3]; float normal[3]; }
                                   vertex[vertexCount]
    record end - audioBytes ADPCM voice for this frame
```

**verified.** The rotations start at **offset 44**, not 12. Trying every
4-byte alignment, 44 is the first at which *every* 16-byte group is unit
length — 1700 of 1700 sampled. At offset 12 it is 89%, because the two groups
before 44 belong to the header and are never unit. That leaves
`nodeCount - 2` quaternions, which is exactly `meshCount - 3`: one for each
mesh of the character that is not one of the three attachment markers, the
face included.

They are stored **w first**. Posing a model with w last gives a mean vertex
error of 9.1 against the rest pose; w first gives 1.6, the residual being the
genuine difference between the rest T-pose and the animation's standing pose.

The 44-byte header is 11 floats — a `float[3]` of length ≈ 0.985 that varies
smoothly, then two 4-float groups that are never unit length. Its role is not
established.

**verified.** Frame counts derived this way account for all **777** PC files
exactly — 582 end on a record boundary, and in the other 195 the last frame is
short by exactly its audio block, which is the only remainder that occurs.

> 777, not 708. `gamedata/MORPH` holds 708 `*.3DM` and 69 `*.3dm`, and an earlier
> case-sensitive sweep counted only the first. The invariant holds on all of
> them, so the correction widens the evidence rather than weakening it — which
> is the sort of thing `tools/verify.py --slow` exists to catch. Cross-checked against every file that also ships as a
Dreamcast `.DDM`: **707 of 707 frame counts agree**, across all six
(vertexCount, nodeCount) families.

**verified.** For the record body, sampled across the shipped set: every one of
1368 sampled vertex normals is a unit vector, and 1263 of 1368 node entries
have sum-of-squares 1, i.e. are unit quaternions. The exceptions are only ever
nodes 0 and 1 (in a wider seeded sample of 80 frames, node 0 is unit in 15 and
node 1 in 2, with components reaching 35) — those two slots are not rotations.

**The leading `float[3]`: mechanism read, meaning NOT established.** What the
code demonstrably does: the streaming parser (0x0042CC40, the `timeSetEvent`
callback) reads it at the track position whose preamble id equals the mesh's
own root node id — position 0 in every shipped file — and **integrates it**
(an accumulator at `+212`, confirmed in the raw assembly: the sums are stored
back both to the accumulator and to the served frame array). The apply pass
rotates the stored value by the actor's facing, subtracts the first frame's,
and moves the morph's scene node.

**But that cannot be the playable semantics.** 57 of 60 sampled files carry a
**near-constant, near-unit vector** whose total deviation across the whole
file is under one unit — integrated, that walks *every* character ≈ 30 units a
second, which is not what the game does, and applying exactly that recipe in
the viewer made every speaker drift (it was reverted). Something in the engine
must neutralise the integral — an accumulator reset, a compensation in the
node update, or the branch not running in the common path — and it has not
been traced. Until it is, the honest description is: a slowly-varying
near-unit `float[3]` per frame, fed through an integrator whose output is
demonstrably *not* applied as raw displacement. A per-frame **root direction**
(facing) is a candidate reading; nothing yet confirms it.

> This section previously declared the field "solved — per-frame root-motion
> deltas". The corpus refuted that within hours, through the viewer: the
> deltas reading predicts universal drift, and universal drift is what
> applying it produced. A mechanism read out of the code is still only a
> hypothesis about the data until something the data could fail has passed —
> the ground rule cuts both ways.

Two loose ends, recorded rather than resolved:

* `g_MorphRootTrack`, the machinery that would instead bind the translation to
  a bone-track entry, is initialised to **-2 and never written again** — the
  apply loop's `track == g_MorphRootTrack` test cannot fire, and the two
  accesses that index by it land 32 and 80 bytes *before* their arrays, on
  neighbouring globals. Vestigial, and mildly buggy, in the shipped build.
* Node slots 0 and 1 are uploaded like every other track, carrying preamble
  ids 0 and 1; the skeleton binds tracks by node id and the drawn meshes
  account for tracks 2 and up (`meshCount − 3 = nodeCount − 2`), so the two
  appear unbound at run time. What their values are — node 1's four floats
  range past 35 — is **not established**.

The models are small — 38 to 148 vertices and 6 to 26 nodes — which is the
right size for a 1999 talking head:

| vertices | nodes | files |
|---|---|---|
| 38 | 15 | 44 |
| 129 | 26 | 11 |
| 130 | 17 | 24 |
| 130 | 19 | 237 |
| 132 | 15 | 32 |
| 132 | 16 | 69 |
| 132 | 19 | 191 |
| 134 | 19 | 75 |
| 135 | 19 | 1 |
| 141 | 19 | 15 |
| 148 | 6 | 9 |

368 bytes of audio per frame is 736 samples at 22050 Hz = 1/30 s, matching the
30 fps the loader sets up (`sub_42BD90(0x5640, 1, 30)` in `sub_41AFC0`).

    python3 tools/morph3dm.py gamedata/MORPH/000000.3DM out.wav
    python3 tools/morph3dm.py --info gamedata/MORPH/071348.3DM
    python3 tools/morph3dm.py --scan

`morph3dm.frame(path, i)` returns one frame's quaternions and vertices.

### Dreamcast `.DDM` — pi-tagged sections

A `.DDM` is a run of frame records, each holding four sections introduced by
the digits of pi as 32-bit tags:

```
0x31415926   0x31415927   0x31415928   0x31415929
```

In `000000.DDM` each appears 174 times — the header's frame count — at a
constant 1872-byte stride. Section `…29` is the ADPCM voice.

**correction to `MORPH_ANALYSIS/separate.cpp`:** that tool treats the four tags
as a one-time, four-way split of the file. They actually repeat once per frame.
For the audio this turns out not to matter — concatenating every frame's `…29`
section is exactly right, and those extracted `.ADP` files decode cleanly.

**correction to `MORPH_ANALYSIS/separate_3dm.cpp`:** it looks for `0xBFE91026`,
`0x3EFD8327`, `0x3F1B7C28`, `0xB9CEF729` in `.3DM` files. Those are float bit
patterns (about -1.82, 0.496, 0.607, -0.00039) that matched once by chance. No
`.3DM` contains the pi tags — the PC container is not tagged at all, it is the
flat record array described above.

### ADP audio

**verified.** Header, per `adp/pc_adp_otns.c` and confirmed on all 162 shipped
`.ADP` files in `gamedata/VOICEOFF` and `gamedata/TRACKS`:

```
+0   uint24 dataSize        payload length; dataSize + 0x10 == file size
+3   uint8  stereoFlag      0 = mono, 1 = stereo
+4   12 bytes of zero
+16  ADPCM payload, 22050 Hz
```

**The codec is recovered.** `tools/adp.py` is a transcription of the game's own
decoder — `sub_483200` (mono) and `sub_483340` (stereo) at 0x00483200 /
0x00483340, with the step table at `dword_4BCC50` and the standard IMA index
table at `dword_4BCC10`. It is IMA-ADPCM with two departures from the textbook
version, both of which matter:

* **the high nibble of each byte is decoded first**, not the low one;
* **the delta is `(4*b2 + 2*b1 + b0) * step >> 2`** — IMA's unconditional
  `step >> 3` bias term is simply absent.

Leaving the bias in is what wrecks the output: the predictor drifts to roughly
-9000 DC on a full line and the speech is buried under it. With the game's
formula the decode is unambiguously correct:

| | textbook IMA | game's formula |
|---|---|---|
| DC offset | -4488 | 2 |
| clipping | 0.3% | 0.0% |
| loud/quiet frame ratio | 4.2x | 205x |

A 205x dynamic range is the silence-between-phrases signature of real speech;
4x is noise. Measured on `gamedata/VOICEOFF/ZVOCUBE.ADP`, and it holds across the set.

The game writes each decoded sample twice, upsampling to its mixer rate; that
is dropped in `tools/adp.py`, which emits 22050 Hz to match vgmstream.

    python3 tools/adp.py gamedata/VOICEOFF/ZVOCUBE.ADP out.wav

---

## 5b. `.3DO` meshes — characters and sets

Magic `OD3X`. `MESHES/PERSOS/*.3DO` are the 181 characters, `MESHES/DECORS/*.3DO`
the 220 sets. Same format for both; the differences are in how they are used.

The header carries eight section offsets and the counts that go with them
(`tools/mesh3do.py`). Record sizes, all confirmed by dividing the gap between
consecutive section offsets by the matching count across **all 401 files**:

| section | stride | contents |
|---|---|---|
| vertices | 32 | 3 floats position (+0), 3 floats **normal** (+12), and a baked light at +28 that is a **BGRA colour dword**, not a brightness — see [`ASSETS.md`](ASSETS.md) 4c |
| triangles | 28 | 3 int16 indices, 6 UV bytes, int32 material |
| quads | 32 | 4 int16 indices, 8 UV bytes at +8, int32 material at +16 |
| materials | 80 | see below — and two of its fields are **runtime state** |

The 80-byte material record is used straight out of the mapped file (the
loader relocates a pointer to it, `scene[5]`, count at `desc+208`; it never
copies), so the loader writes into it:

| off | size | field |
|---|---|---|
| +0 | 20 | short name, `BATITR15` |
| +20 | 20 | **texture file name, `BATITR15.BMP` — the texture cache's only key**, compared and copied at 19 chars |
| +40 | 20 | palette file name — the palette cache's key |
| +60 | 4 | bytes of image data in the `.3dt` |
| +64 | 2+2 | **texture slot 0..57, then sub-slot — `-1` on disk, written at load** |
| +68 | 2+2 | **palette slot, then sub-slot — `-1` on disk** |
| +72 | 4 | bits per pixel (8, or 4 for a 16-colour palette) |
| +76 | 2+2 | width, height |

`+64` is the low six bits of every render bucket key and therefore *is* the
texture the face draws with. All **2534** materials in `gamedata/MESHES` ship `+64`
and `+68` as `-1`, which is what establishes them as runtime rather than
authored. See [`ASSETS.md`](ASSETS.md) §4b for the pool, the cache and what
goes wrong when two files share a name.
| meshes | **140** | flags, id, name[20] at +16, position at +36, hierarchy at +48, counts at +64 |
| cameras | 52 | name[20], eye float[3], target float[3], unused, fov |

**The mesh record is 140 bytes, not the 136 the earlier notes gave.** Only 140
divides evenly on every file, and only 140 keeps the mesh names readable past
the first one — at 136 the second name reads as `\x01\x00\x00\x00Ag`.

Mesh positions are **absolute** in model space. Accumulating them up the parent
chain pulls the model apart; rendering them as given produces a coherent figure.

### Which meshes are geometry

This differs between characters and sets, and getting it wrong is silent:

* **Characters**: skip `flags == 0` (aim/shoot markers and `M*` proxy
  skeletons — 123 of 3730 meshes), `CollisionOnly` (`flags & 0x800000`), the
  bullet mesh, and single-triangle attachment markers (`flags & 1` with
  `vertexCount <= 3`, 532 of the 547 such meshes, all `*Epauled`/`*Epauleg`/
  `*Ventre`).
* **Sets**: skip **only** `CollisionOnly`. The flagless rule does not carry
  over — in Aapkayl the 21 flagless meshes are the bed, a wall, the doors, the
  chests and the books, 830 of the set's 3495 faces. Its `CollisionOnly` meshes
  really are volumes (`introgrid`, the `APface*` series, 76 faces). 3495 − 76 =
  3419 drawn, exactly.

`flags & 0x800` is `MaterialCutout`: solid black in the texture is the
transparent colour.

### Skinning

A negative triangle index means the corner is skinned to a mesh **higher in the
hierarchy**; mask with `0x7FFF` and walk up the parents. Two things must be
skipped on the way: attachment markers (`vertexCount <= 3`, whose coordinates
sit far outside the model, so an index of 0–2 "fits" one and drags a stray
triangle off the shoulder), and any ancestor whose vertex array is too short —
1529 of the 9405 skinned corners in the shipped models index past their
immediate parent, and every one fits some further ancestor.

### The body axis

A character's **origin is not inside the character**: `AMH_FN`'s body sits 570
units from it, and even `AKG_FN`'s is 16 units off. The `*Bassin*` (pelvis)
mesh is the body's vertical axis — its position matches the bounding-box centre
to within a unit, and 180 of the 181 characters have one. Rotate and place a
model on that, not on its origin.

---

## 5b2. How the engine stages an actor during a dialog

**read from the code, corpus-verified.** `Dialog_Begin` (0x0041B280) puts the
**player** — and only the player — into dialogue mode
(`Actor_EnterDialogueMode`, 0x00468DE0): his input is cut, his channel
switches to `.CTL` **group 400, the dialogue-stance group**, and his
`ACTOR_STATE` becomes 16 (or 17 when a UI screen already held him — state 9,
which all three writers in the binary set for interface screens). Leaving
restores group **100** and the saved state. The NPC is *never touched*: her
scene-object program simply keeps running, which is why Telis stays seated
and eating through dialog 387 with no dialogue-specific machinery at all.

**The player's pose is authored per launch.** The launch script decides
whether the dialogue stance applies:

```
1090  player.anim.hold                 <- freeze the channel: the group-400
1091  scx.play.player.wait 22            stance will NOT drive the body
1096  scx.play.player  32              <- the scene clip that owns the body
1109  dialog.start     387                (Uzal_Stand - Kay'l's seated loop)
```

The state machine reinforces the same rule from a second side: an actor whose
body an SCX object is driving is in **`ACTOR_STATE` 4** (slot +172 holds the
object — `Actor_TickScxDriven`), and `Actor_EnterDialogueMode` sets the
channel's **no-playback flag** for exactly that prior state, so the group-400
stance is tracked but never applied and the scene pose survives the dialog.
(The old reading of state 4 as "a morph is loaded" conflated +168/+172 and is
corrected in `types.h`.)

So the precise rule for the player's dialog pose:

* `player.anim.hold` (op 104) before `dialog.start` **and** a
  `scx.play.player` loop (ops 46/90) → the scene clip poses him — seated in
  387 (`HO14_01R`), standing in 402 (`1-02KAY`), whatever the clip authors;
* a hold with no scene clip → he freezes as he stands;
* no hold (85 of the 205 decoded launches) → the group-400 dialogue stance.

The corpus is unanimous: **all 16** launches that scene-drive the player also
hold him first, and **none** does so without the hold —
`verify.py: player anim hold`.

## 5b1b. Kay'l's two models — `HO1_FN` and `HO1_FNM`

**verified, and reported from play.** Six of the 193 `PERSOS` models have an
`M`-suffixed twin, and the pair is not interchangeable. Two of the six bases
carry **no face mesh at all** — `FUA_FN` and **`HO1_FN`, the player** — and
their twins exist to supply one: `HO1_FNM` adds `UVisage`, 130 vertices,
which is the vertex count **267 of the shipped `.3DM` morphs** use. A morph
is a talking head, so a character can only be morphed while wearing the M
model.

The game uses **`HO1_FNM` only in the opening cutscene** (where Kay'l's
morph plays) and **`HO1_FN` everywhere in-world, dialogs included** — so the
player never lip-syncs during a conversation, and cannot: his in-world model
has no face to drive. The viewer therefore defaults the player to `HO1_FN`.
The other four twins already have a face on the base and differ elsewhere —
`SPV_FNM` adds the `Vise`/`Tire` aim-and-shoot markers, not a face.
`verify.py: morph face models`.

## 5b2a. The dialogue interface machine

**read from `Dialog_TickUI` (0x0046A200) and `Game_HandleEvent`'s dialog
events.** The phase global `dword_9103DC` drives it (Actors_TickAll calls the
tick while the player is in dialogue state 16/17):

* **1** — stage the current node: line text present → draw the subtitle
  strip, load `<asset>.3dm`, phase 2 (or 7 for the skip-on-any-key sentinel
  line); no line → build the menu from the four replies, conditions
  evaluated (event 55), already-read replies greyed `0x808080`;
* **2 / 7 / 8** — the line plays. Which of the three it is was decided in
  case 4, from the **asset name alone**: `strcmp` against `125338` gives 7,
  a seven-byte `memcmp` against `02E19A` — the terminator included, so an
  exact match and not a prefix — gives 8, and anything else gives 2. The one
  condition that ends all three is
  `(a2 & 0x10) || state == 7 && (a2 & 0xFFFFFFF3) || state == 8 && Morph_IsDone()`:
  the action button (0x10) always, **any** button for 7 except 4 and 8, and
  for 8 no button at all — the morph ending is enough. It then calls
  `Morph_Stop` and moves to 3. Buttons 4/8 scroll a long subtitle, which is
  why 7 excludes exactly those two.

  **Both sentinel assets ship, once each** (`verify.py: engine dialogue line
  states`, a scan of all 420 chunks): of 321 conversations and 1174 nodes,
  **1172 are ordinary**, `125338` is **conversation 272 node 0** — `Kay'l /
  Intro`, the line a new game opens with — and `02E19A` is **conversation 186
  node 0**, `Telis Demon/Toit  (Scene)`. That second one is a **single node
  whose four `param` are all −1**: no branch, no menu, nothing to press. It
  opens, speaks for 7.51 s and closes by itself, which is what a line inside a
  cutscene has to do. `02E19A` is the same line `Morph_Play` cuts without a
  blend-out (`sub_42BE10(1, strstr(path, "02E19A") == 0)`), so the engine
  singles it out twice for one reason. Ported 2026-09-02 (`DialogPlayer::
  lineState`); the port used to wait for a press on both.
* **3** — the player's own line (event 56), drawn blue-grey `0x8080C0`,
  phase 5; none → the menu;
* **4** — the menu: 4/8 move the white highlight, 0x10 selects — event 59
  **executes the reply's action script**, event 61 fetches the branch
  target, a negative target raises event 63 (dialog end) and returns to
  phase 0.

The dialog events, all backed by the CLEAN `Dialog_Get*` accessors:
52 line strings, 53 line cameras, 54 reply text, 55 evaluate a reply's
condition, 56 the player's own line, 57 reply cameras, 58 the line's
`.3dm`/voice asset name, 59 run a reply's action, 60/61 the branch target,
62 start the voice, 63 end the dialog. This is the machine the web viewer
mirrors (line → next → menu, the cut on next, actions on selection).

## 5b2b. The trigger zones — what the "script records" actually are

**solved, both ways.** The 68-byte records the whole trigger scan indexes
(SCENE +16 count +44, AREA +48 count +76) are the game's **trigger zones**:

```
+0/+4/+8  the three script slots (relocated to pointers at load)
+12       4 corners x {int32 x, y, z} - the zone's quad footprint
+60       u16 facing-arc center  } 4096-step angles; an actor "activates"
+62       u16 facing-arc width   } the zone only while facing into the arc
                                   (0 = any facing)
                                   ON DISK. Area_Load / Scene_Load convert both in
                                   place by 360/4096 as they relocate the record, so
                                   every runtime consumer (Actor_ScanZones) reads
                                   whole degrees (2026-09-02)
+64       int16 ZONES.TAG id - all 4558 shipped ids are distinct, one
          save-game bit each (the bitmap at game-DB +28, Zone_StateBit;
          bit 15 of the id is a flag the mask strips). zone.enable/disable
          flips it, and Zones_RegisterAll only registers zones whose bit
          is set
+66       int16 world-camera id, -1 = none - walking into one of the 54
          zones that carry one forces that camera (event 8 -> camera
          mode 12): the game's walk-into-a-room auto-cut
```

`Zones_RegisterAll` registers `record + 12` (past the script slots) into a
sweep-and-prune spatial index (`Zone_Add` - three sorted axis lists), which
is why the runtime consumers read the arc at +48/+50, the id at +52 and the
camera at +54. `Actor_ScanZones` tests containment and the arc each frame,
raising event 8 on touch and event 7 - the 16-slot "interactables in range"
prompt table - when the facing matches too. Dialog 387's own launch zone is
record 0 of SCENE 53: script slot +4 holds the launch script, id **3732** -
the exact operand of the `zone.disable 3732` that retires it.

**verified.** 4558 zones: 0 invalid script offsets, 0 arcs past 4096, 0
duplicate ids, 54 cameras (`verify.py: zone records`).

**`+64`'s bit 15 is a flag, and what it does is LATCH the zone — it does not
free it** (read from the assembly 2026-09-02). `Zone_StateBit` (`0x0040D500`)
indexes the save bitmap with `(id & 0x7FFF) / 8`, which is why the mask strips
it; `Script_Pump` then tests the same bit as `byte ptr [slot+0Bh] & 0x80`
immediately after queueing an activate and puts the prompt slot in state 5.
That reads like "free the zone next pump", and it was ported that way and is
**wrong**: `Game_HandleEvent` case 7 maps state 5 back to **4** on every frame
`Actor_ScanZones` still finds the player armed — case 7 is raised per frame,
not on an edge — pump case 4 maps 4 back to **5**, and `Script_Pump(1)` runs
before `Actors_TickAll` in `Game_Tick`, so the pair ping-pong until the player
leaves and the ordinary leave-then-free path takes over. What the bit buys is
the gap that `Script_Execute`'s `ctx+32` clear opens: an ordinary zone whose
activate script has ended can be activated again by the next press, and a
one-shot one cannot. **37 of the 4558 shipped zones carry it.** The full
state machine is in [SCRIPT_VM.md](SCRIPT_VM.md), under the context status
word; `verify.py: engine zone pump`.

**`+66`'s camera is asked for on TOUCH, before the facing test.**
`Actor_ScanZones` (`0x00467770`) tests containment first and raises event 8 for
every zone the player stands in, whatever way he faces; `Game_HandleEvent` case
8 hands a non-`-1` `+66` to `Camera_FindWorld`. The arc gates the *prompt*
(event 7), not the camera. **54 zones carry one, over 40 distinct camera ids.**

## 5b2c. The zone-script lifecycle

**read from `Script_Pump` (0x00407DC0) and `Script_ProcessActions`
(0x00408220), both CLEAN.** The three script slots of a zone are its
**enter**, **activate** and **leave** scripts, run through one shared
context by a 4-deep action FIFO (action n = run slot n-1; action 4 frees
the context):

* a zone becoming activatable (event 7) arms a prompt slot; the pump gives
  the zone's three scripts to a fresh context and queues the *enter* script;
* the player pressing action (event 6) runs the context inline —
  `Script_Run`, or `Script_RunToOpcode75` while the player **holds an
  object**, so a use-object script stops at `var.set.used_object` when the
  object is not consumed — then queues the *activate* script (dialog
  launches live in this slot, +4 on disk);
* leaving the zone queues the *leave* script and the free;
* an action press nothing handles posts **message 26** — the game's
  "nothing here" default;
* a context stuck on a pending area transition for 60 seconds is retried
  and unwedged by the watchdog in `Script_ProcessActions`.

## 5b3. The message system — how events reach the world scripts

**read from `Game_HandleEvent` (0x004067D0, 899 lines — the engine's central
event switch, 162 raise sites) and `Message_RunHandlers` (0x00409420).**

The engine runs on numbered events (`Game_RaiseEvent`). The ones met so far:
0 = load a dialog (**dead — nothing in the binary raises it**, one more
mechanism ruled out for the 106 launchless conversations), 3/4/5 = script
lifecycle, 7/8 = zone activatable / zone touched (`Actor_ScanZones` — 7 needs
the facing arc), **43 = post a message** (27 sites — SCX `Script_SendMessage`,
`.CTL` states, the UI), 44/45 = get/set a character property
(`Actor_GetProperty`, 54 sites).

> Event 43's **27 is a floor and disagrees with
> [SCRIPT_VM.md](SCRIPT_VM.md)'s 28**, which is recorded there rather than
> reconciled: 27 is a count over `readable/src/*.c`, and the decompilation is
> missing every function nothing calls.

A posted message runs the scripts subscribed to it. The subscription tables
are the "second script table" the trigger scan already indexes — now with
their `+4` field decoded:

```
SCENE +36 (count int16 +54), AREA +68 (+86), GLOBAL +8 (+24)
    8 bytes per record: +0 script offset (relocated), +4 int16 message id
```

`Message_RunHandlers` searches scene → parent area → GLOBAL, first match
wins, and runs the script in a fresh context with `{message id, sender}` as
its parameters (message type 25 executes inline, the rest queue) — **of which
a script can reach only the sender**, because the VM's shared operand fetch
indexes the parameter block from its *second* word (`movsx eax, word ptr
[block + eax*2 + 2]`), so the indirect operand `0x4000` is `args[1]`. That is
what the corpus wants: all **59** indirect operands in the world scripts are
`push <actor or object id>; push.i16 0x4000; cmp.eq`, and a script subscribed
to message 3 has nothing to learn from the number 3. See
[SCRIPT_VM.md](SCRIPT_VM.md), "Operand encoding". The
shipped data carries **154 subscriptions, 138 with a script** (16 registered
ids with a null script), message ids 0..32 — `verify.py: message tables`.

## 5b4. The interface text files — `IAM\<Screen>` and `IAM\FRENCH\`

**surveyed.** The lowercase IAM entries are per-screen NUL-separated string
files for the 37 interface screens, with `IAM\FRENCH\` holding the same set
localized (the shipped build is French, so most root copies match). Strings
carry inline markup, **decoded 2026-08-30** — see [`UI.md`](UI.md) §5, which
supersedes the guess this line used to record. `{f<letter>}` picks one of the
13 fonts by its id letter (so `{fC}` is the COMPUTER face, **not** centring);
`{C}`, `{D}`, `{F}`, `{G}` are the four alignments; and `{I...}` is **nine
decimal digits**, `RRRGGGBBB` — `{I255120045}` — not a hex triple.
`verify.py: ui fonts`.

| file | strings | screen |
|---|---|---|
| `Menu` | 14 | the main menu ("Nouvelle partie" …) |
| `Options` | 84 | options ("Vidéo" …) |
| `Save` / `Pause` / `HScore` | 17 / 5 / 7 | save slots, pause, highscores |
| `Buy` | 25 | the shops ("Acheter", "Vente", "Examiner") |
| `Arch` / `Term` / `Morg` / `Surv` | 10 / 13 / 7 / 4 | the in-world computers: police archive, terminals ("Dossier N° 94727"), the morgue, surveillance |
| `Lift` | 7 | elevator floor labels ("Bureau du commandant Gandhar") |
| `Fsim` / `Multip` / `Meca` | 7 / 11 / 5 | the shooting-gallery difficulty prompt, the supermarket, Meca status |
| `Sneak` | 44 | the crouch/examine interface (5b4a below) |
| `Den` / `Gand` / `Shoot` | 0 | shipped empty |

`IAM\GAMES` (created at run time, absent from the shipped tree) is the
**save-slot directory**: 256 slots of 72 bytes — a 32-byte name plus
metadata — managed by the `SaveDir_*` family and listed by the save/load
screens; `OMK_SAVE` is the default profile name.

### `IAM\SNEAK` — the interface string table

**solved.** Despite the name, no stealth: 535 bytes of NUL-separated French
interface labels ("Identité", "Inventaire", "Utiliser sur", "Seteks en votre
possession :" …), split into 6 sections by a count table compiled into the
binary, loaded by `Hud_LoadResources` (0x004490D0) together with the gauge
bitmaps (`jauge1/jauge2/jaugeg.bmp`). The "sneak" special move
(`tab_special_move` row 0, `MDSNEAK0` → `Sneak_Start`) just opens UI screen
9 over these strings — the crouch/examine interface, not a minigame.

## 5b6. `SCPTDATA/*.SFX` — the scene sound files

**walk solved, 59/59 exact** (`verify.py: sfx files`). Magic `5.0V`, then six
counted sections:

```
u32 A + A x 40   (14 shipped)
u32 B + B x 44   the cin-sfx definitions (64) - Sfx_LoadFile keeps the ones
                 whose +8 bit 0x80 is set and binds them to animation clips
                 by the id at +4; Anim_LoadClipSfx copies a row into the
                 clip's sfx state and Anim_TickClipSfx plays it inside its
                 frame window ("sfx for cin")
u32 C + C x 80   (366)     the AMBIENT EFFECT table - the fire, smoke and
                           sparks. Decoded 2026-08-29, below
u32 D + D x 16   (151)     the effect BINDING: [i32 id -> a C row]
                           [char[4] tag] [f32 9999999.0 in every row]
                           [f32 0 / 1 / 2]
u32 E + E x 76   (310)     the DECOR PIECES - see below
u32 F + F x (16 + 36n)  (219) - each carries its own sub-count at +8
```

The engine keeps at most three .SFX files resident (one per area slot plus
`default_sfxscx`), and `Sfx_LoadFile`'s own walk names every section's base and
count global: C is `dword_536B80`/`dword_536B90`, D `dword_536BC4`/`dword_536B6C`,
E `dword_536BAC`/`dword_536B54`, F `dword_536B4C`/`dword_536BB4`, all indexed by
the resident slot.

### Section C — the ambient effects

`Sfx_BindAmbientEffects` (0x0044F840) matches set meshes flagged `0x40000000`
against these and registers each hit with a duration and a `rand()` starting
phase; `Sfx_RegisterEmitter` (0x0046E3A0) then puts it in a **100-slot,
64-byte** runtime table, and `Sfx_TickAmbient` runs it — firing a sound and
emitting sprite particles ([`ASSETS.md`](ASSETS.md) §3b).

| off | field | evidence |
|---|---|---|
| +4 | **the sound id**, `-1`/`0xFFFF` when silent | `Sfx_TickAmbient` guards on exactly those two values, and **338 of 366** rows carry one; the other 28 use **24 distinct** real ids |
| +32 | particle lifetime — 0..100, median 12 | read from the tick; range is consistent |
| +12 | **the flags** — see the table below | |
| +16, +20, +24 | the emission **velocity**; its magnitude is the speed in units per frame | `v60 = sqrt(x²+y²+z²)` |
| +28 | an **acceleration on world Y**, signed by flag `0x200` — the integrator does `vel.y += it` once a frame, so Y is quadratic in the particle's age while X and Z stay linear. **159 of 366** rows use it, and it is what makes a flame a tall column rather than a short puff | |
| +36, +40 | read at registration; **0 in all 366 rows**, so their meaning is not established |
| +48, +52 | a colour pair (equal in most rows) | unpacked byte-wise into three floats |
| +56 | the sprite **scale** | copied into the instance's `+24`/`+28` |
| +60 | emission cone half-angle in **degrees** — 0..185, median 12, 71 zeros | the tick multiplies it by π/180 |
| +64 | rotation rate, 0 in 311 of 366 | as above |
| +68 | **particles per emission**, 1..15 (264 rows are 1) | the emit loop runs exactly this many times |
| +70 | **char[8] the effect's name** — `fume`, `fire`, `explo`, `feu`, `fumee`, `plouf`, `SMOKE`, `boom`; all **366** rows carry a printable one | |
| +78 | **the sprite BLEND MODE** | the byte lands in the renderer's 0..8 enum for **366/366** rows: 332 mode 4 (**additive**), 32 mode 6 (multiply), 1 mode 5, 1 mode 0 |

The flags at `+12`, every one of them a randomisation or a ramp — the engine
has no other per-particle behaviour:

| bit | effect | rows |
|---|---|---|
| `0x0002` | alpha ramps `1/life` per frame | **0** — no shipped effect uses it, so every particle keeps the default 0.5 |
| `0x0004` | **grow**: scale `+= scale/life` per frame, so it doubles over its life | 120 |
| `0x0010` | random start angle, 0..360° | 301 |
| `0x0040` | **jitter the emission AXIS**, once per emitter at registration: `+[0, 2)` on each component of the velocity, then the cone's basis is rebuilt from the result while the *speed* stays the authored magnitude. The jitter is always **positive**, so the lean is consistent in world space — which is why the game's braziers all tilt the same way | 73 |
| `0x0080` | jitter the particle **count** by 10% | 26 |
| `0x0100` | jitter the **lifetime** by 10% | 47 |
| `0x0200` | the sign of the `+28` drift | 79 |
| `0x1000` | widen the **cone** by up to its own value | 294 |
| `0x2000` | **shrink**: the same ramp, negative | 121 |

241 of the 366 effects ramp their scale one way or the other, which is what
makes a flame broaden as it rises.

**On `0x40`, because it looks like a bug in a viewer and is not.** The jitter
is added to all three components and is always **positive**, so the axis lands
somewhere in the `+x .. +z` quadrant — a **90° spread of azimuth**, plus a
vertical component anywhere in `[-2, 0)` when the authored vector is
`[0, -2, 0]` as Anekbah's braziers' is. On a given camera that means some
flames lean screen-left and others screen-right: for the title sequence's
cam 125, `+x` projects up-left and `+z` right.

The chain was traced to the assembly rather than inferred, because the
decompiler's argument order for the FPU calls is not to be trusted:
`sub_442690` builds `Matrix3x3_FromEulerAngles(pitch, yaw, 0)` with **both
angles measured from −Z**, and `Sfx_TickAmbient` builds its local cone vector
as `(cosφ·sinθ·s, sinφ·sinθ·s, −cosθ·s)` — whose axis is also **−Z**. They
agree, so the emission axis really is the jittered vector.

**A viewer cannot match the game flame for flame — and neither can the game
match itself.** `sub_40E170` calls **`srand(timeGetTime() - base)`** once at
start-up, so the seed is the millisecond system clock and the leans are
different on every launch. Only the distribution is reproducible, which makes
`Math.random()` the *faithful* choice in a viewer rather than a lazy one: a
fixed seed would be less like the original, not more. `/cutscene` uses it.

The blend mode is the one that ties the file to the picture: `Sfx_TickAmbient`
copies it straight into the sprite instance's `+20`, which
`Render_SubmitSprites` switches on to choose the render bucket
([`ASSETS.md`](ASSETS.md) §3b). A byte that lands inside an 8-value enum for
every one of 366 rows is not a coincidence — and it makes **332 of the game's
366 ambient effects additive**, the fourth independent confirmation of that
correction.

### Section D — how a mesh finds its effect

Read from the assembly, because the Hex-Rays output made it look as if the
match were on the id:

```asm
mov     eax, [ebp+0]                 ; the mesh DEFINITION
test    dword ptr [eax], 40000000h   ; the animated-effect flag
jz      skip
mov     ecx, [eax+10h]               ; meshdef+16 = the NAME, first 4 bytes
mov     [esp+var_8], ecx
...
mov     eax, [esi+4]                 ; a section D row's +4 tag, esi += 10h
cmp     edx, eax                     ; a plain DWORD compare - case sensitive
jnz     next_D_row
mov     edx, [esi]                   ; that row's +0 = the effect id
...
cmp     edx, [ecx]                   ; against a section C row's +0, ecx += 50h
```

So the chain is **mesh name (4 chars) → D tag → D's id → C row**, and it
resolves in the shipped data:

| | |
|---|---|
| set meshes flagged `0x40000000` | **579** |
| of those, in a set with no `.SFX` at all | 185 |
| **bound to a section C effect** | **321** |
| distinct flagged-mesh prefixes that find a D row | **91 of 97** |
| section D tags that name a flagged mesh in their own set | 91 of 107 |

and the effects they resolve to are what the names promised: **`neon` (102
meshes)**, `fume` (33), `jon0` (17), `agaz` (12), `bulles`, `encen`, `cac*`.
The dword compare is case-sensitive, which is why `SPiT` in `A_shootg` finds
nothing while `SPOT` does — the 16 unmatched tags and 6 unmatched prefixes are
authored rows with no counterpart, not a gap in the reading.

**This is the last link in the effects chain**, which now runs end to end:
mesh flag → D tag → C effect → `Sfx_RegisterEmitter`'s 100-slot table →
`Sfx_TickAmbient` → sprite particles → `Render_SubmitSprites` → the render
buckets.

**Section E is the decor-piece table**, and it is not sound at all. The loader
(0x0044FA30) walks the six sections to reach it and keeps its base and count in
`dword_536BAC`/`dword_536B54`; `SetPiece_Find` (0x00450070) then scans it with
a stride of **19 dwords = the section's own 76 bytes**, matching on the id at
`+0`. At the end of the load it shows every piece whose `+8` is 1 and whose
`+12` is -1 — **55 of the 310** shipped pieces — and VM opcode 123
`set.hide_piece` clears the visible bit, which `sub_4501D0` does by masking
`+72`. The show path is never scripted, only this load-time pass.

So a set can ship geometry that is hidden until something reveals it, and the
32 `set.hide_piece` sites are the reverse. `xendar` carries the most pieces
(22, none shown at load), `jangir` the most visible (9 of 16); Anekbah has
exactly one, hidden. *Established while hunting the Anekbah billboards, which
it turned out not to explain.*

**The 76-byte row, and the section F block it walks** (2026-09-02, from
`SetPiece_Show`, `sub_451220`, `sub_451600`, `sub_450E50`, `sub_450D60` — the
state machine is in [ASSETS](ASSETS.md) §3b):

```
section E row (76)                    section F block: 16-byte header + n x 36
+0   i32  id (a type-1 link names it)   hdr +0  block index (row +16 names it)
+8   i32  key a1  \ matched by            +8  n records
+12  i32  key a2  / sub_451470(a1,a2)      +12 duration = sum of the first n-1 +20s
+16  i32  block index                   record +4   effect id (the INDIRECT form)
+20  ptr  block (runtime)                      +8   f32[3] position, in the link's frame
+24  ptr  current record (runtime)             +20  f32 frames to the next record
+28  f32[3] position (runtime: the walk)       +24  link type   0 none, 1 a row by id,
+40  i32  the row's link type                        2 an ACTOR by 3-letter tag,
+44  u32  the row's link id ('HO1'=0x484F31)         3 the PLAYER
+48  ptr  link, resolved (runtime)             +28  link id
+52  i32  effect id, 1-based; <= 0 = indirect  +32  ptr link, resolved (runtime)
+56  f32  delay, frames (-> flag 0x20)
+60  f32  clock (runtime)
+64  i32  loops; 999 = for ever
+68  i32  iteration (runtime, starts 1)
+72  u32  flags: 1 shown, 4 start reversed, 8 reversed (runtime), 0x10 ping-pong,
          0x20 waiting (runtime), 0x40 smoothed heading, 0x80 roll, 0x100 roll lerp
```

Over the 382 shipped rows: 267 unlinked, 79 linked to an actor, 36 to a row;
162 delayed; 111 play once, 100 for ever; 38 indirect. 1457 records, 442 of
them in the player's frame. `engine/tools/dump_setpieces` prints every field.

## 5b5. `MAP2D/` — the in-game map screens — and `HASHCODE.TAB`

* **`MAP2D/*.mpt`** — **solved** (the runtime file; `Map2D_Load`,
  0x00434E30, loads `map2d\<AREA +106>.MPT` when the area does —
  **AREA +106 is the area's map name**, 16 areas carry one, all shipped
  except the cut `ARCHIV04`):

  ```
  u32 scale
  u32 floorCount                       (79 floors across the 16 maps)
  per floor: u32 nSegs + nSegs x {u32 kind, f32[6] wall segment}
             f32[6] bound + u32 W + u32 H + W*H byte cells
             (74987 cells - the map picture and its reveal state)
  per floor: u32 k + k x [u32 len][(len+2) dwords]
  per floor: one 192-byte record, 0xFF-filled on disk - runtime scratch
             the loader points dword_907EB4 at (the reveal overlay)
  ```

  The walk lands exactly with one trailing record per floor in all 16 files
  (`verify.py: map2d`). **`*.map`** is the authoring twin — same content
  with `name[20]` room labels and an index list per room, 0xFF-padded; the
  engine never opens it (the extension constant is `.MPT`).
* **`RADAR/*.WRE`** — **solved**: the radar/ambience wireframes the
  `ambience.on/off` opcodes toggle. `{u32 nVerts, u32 nEdges,
  f32[3] × nVerts world positions, u16[2] × nEdges edge indices}` — 15
  files, 11 exact and 4 shipped one edge short, **all 35788 edges
  reference valid vertices** (`verify.py: wre wireframes`).
* **`HASHCODE.TAB`** (24 KB) — the multi-CD index: `name\0` + the disc
  number (1/2/3) per streamed asset (`.ADP`, `.3DM`, `.SCX`, `.3DO`,
  `.3DT`, `.SFX`), the lookup behind `cdchange.txt` prompts.

## 5c. `SCPTDATA/*.SCX` — scene scripts

220 files, one per playable location — the same count as `MESHES/DECORS`, though
only 134 share a name (`banque` stages `Abank`, `bar` stages `Abar06`).

Loaded by `sub_449750` (0x00449750). Like a `.CTL` it is a saved memory image
with dead pointers the loader overwrites; unlike a `.CTL` it is also a stream —
most of the file sits after the structural block and is pulled in by `fread` as
each resource record is reached. `Aapkayl.SCX` is 7 MB with a 39 KB block.

```
+0   int32  0x00DEAD00        magic
+4   int32  5                 version; the loader rejects anything else
+8   int32
+12  int32  blockSize
+16  the block: chunks tagged 0xDEAD00NN, ending at 0xDEADFFFF
then the streamed resources
```

A dword that is not a known tag is **skipped** and the walk continues — that is
the loader's `default:` case — so the block carries padding between chunks, and
does.

| chunk | in-block record | holds | records |
|---|---|---|---|
| 0 | 32 | `.3dp` meshes | 185 |
| 1 | 36 | `.3DA` animations | 508 |
| 2 | variable | **the script objects** | 4511 |
| 3 | 26 | `.WAV` sounds | 1667 |
| 4 | 36 | **`.3DO` effect sprites** — the game's particles, [`ASSETS.md`](ASSETS.md) §3b | 230 |
| 5 | 28 | — empty in all 220 files | 0 |
| 6 | 792 | — empty in all 220 files | 0 |
| 7 | 32 | a fixed global array | 13952 |
| 10 | no count | **the camera editings** (streamed; 29 scenes) | 125 |

### Chunk 2 — the script objects

Every file has it. Objects come in **state pairs** linked by name at load time
(`CoffreOpen` / `CoffreClosed`, `PorteCuiOpen` / `PorteCuiClosed`), and the
sounds match them one for one: `CoffreOpen` → `COFFREOPEN.WAV`.

```
object record, 100 bytes
  +0   ptr      the scene (set at load)
  +4   char[20] name
  +24  int32    handle
  +28  int16    running            (runtime)
  +30  int16    status: low nibble = busy bits, bits 8-11 = editing slot,
                bit 12 = "was busy this run"        (runtime)
  +32  int32    functionCount      +40 ptr functions      (fixed up)
  +36  int32    program counter    (runtime)
  +44  int32    syncFunctionCount  +48 ptr sync functions (fixed up)
  +52  int32    loop count: 1 = run the list once, -1 = loop forever
                (the only two shipped values - 3551 / 960 of 4511)
  +56  int32    loops done         (runtime)
  +76  int32    instance count     +84 ptr instance pool  (runtime)
  +84  ptr      the linked partner object              (set at load)
  +88  float    the program clock, += frame dt per tick   (runtime)
  +93  u8       hand-over flag - see "linked pairs" below (runtime)
  +94..97  u8   up to four linked camera *editings* - see below. 0 on disk
                in all 4511 objects; Script_LinkCamEditing writes them

then per object, in order:
  [u8 hasLink][21-byte partner name if hasLink]
  function[functionCount] and function[syncFunctionCount], 24 bytes each
  two tables, each [u32 n][u32 x n][u32 x n][21 bytes x n]
                   - the object's name pools: [0] object/bone names,
                     [1] cached handles, resolved lazily at run time

function record, 24 bytes
  +0 int32 id   +4 int32 paramCount
  +8 int32 index into the scene's parameter pool -> pointer at load
  +12 int32 index of a sync function, -1 for none -> pointer at load
            **into the SYNC array, not into the two laid end to end**:
            `scene_read_objects` resolves it `obj->syncFunctions + sync`
            and refuses the file past `syncFunctions + syncCount`
            ("Address of SyncFunction isn't valid."), and
            `Script_FunctionsIndexesToAdresses` does the same for the sync
            records' own links
  +16 int32 repeat count: times to run, -1 = forever
            (1 in 13247 of 13887 shipped records, else 2..27 or -1)
  +20 int32 run counter - 0 on disk in all 13887
```

**verified.** The chunk-2 walk lands exactly on the following chunk tag in all
220 files, and the chunk stream reaches `0xDEADFFFF` in all 220. The strongest
check is the parameter pool: it is one shared `int32` array per scene and the
functions consume it **strictly in order** — each index is the previous index
plus its `paramCount`, with no gaps, in all 220 files.

### How a script runs — the interpreter

**read from the code** (`Script_PlayScript`, 0x0044C860 — the engine's own
name, from its log strings; `Script_PlayAllScripts` ticks every object of both
resident scenes each frame, plus a pool of dynamically spawned *instances*,
removing an instance when its program ends).

An object *is* a program. `Script_StartScript` (0x0044A7E0) arms it: running=1,
program counter, loop counter and clock cleared, every function's `Reinit_*`
run. It is called from the `scx.play*` VM opcode handlers — that is what
opcodes 57-60/46/90 do — and again by the interpreter itself when the list
completes with loops remaining. Then, per frame:

* the function at the **program counter** runs, together with the whole chain
  reached through its `+12` sync link (`Script_SyncChainTail` follows it) —
  chained functions execute the same tick, which is how an animation carries
  its sounds (`Uzal---->assis`: `SelectBodyAnimation` → three chained
  `PlaySyncSound`). **The link counts in the sync array**, per the record
  layout above, and reading it flat is a trap worth naming because it fails
  quietly: the two arrays sit end to end in memory, all 6308 shipped links
  stay in range either way, and 4511 objects out of 4511 still run. It cost
  a shot. Flat, a leading `sync = 0` becomes a self-loop and drops whatever
  hung off it — a link points at `Script_PlaySound` 37% of the time,
  `MoveObjectOnPath` 36% and `PlaySyncSound` 21%, and the first is never busy
  at all — but at **20 sites** it lands on a different *main* step and
  merges two program steps into one, so the object ends at the longer of the
  two instead of their sum. `Impasse.SCX`'s `A_2_DemonLook` is one: clip 15
  (91 frames) then clip 17 (41) is **132**, which is exactly the duration of
  `sautdemon`, the camera editing linked to it — the demon's jump off the
  wall. Flat it runs 92, so the shot was cut 40 frames short and the demon's
  "Te voilà, enfin ! Je t'attendais..." came in over the end of his own jump.
  The **editings adjudicate it**, though only weakly, and the margin is worth
  quoting honestly: over the 95 that name an object, agreement with their own
  authored `+24` duration goes 65 → 66, and on the 11 rows where the two
  readings differ at all the sync-array one is exact 4 against 3. (This read
  66 → 69 when first measured, on the 8 rows that then differed — but that was
  taken while the interpreter still spent a frame per program step, and the
  margin does not survive correcting it. A corpus verdict recorded before a
  timing fix is worth re-running rather than re-reading.) What settles the
  question is the loader, which is not ambiguous; what the corpus still shows
  decisively is the single row above, where flat runs 92 against an authored
  132 (`verify.py: scx sync chain`, `engine: scene steps`);
* each handler returns a busy bit. While any function in the chain is busy the
  PC holds; when all are done the PC advances. A function that has run `+16`
  times reports done immediately (`+20` is its counter; -1 = forever).
  **So an object's clip is not one number** — a program is a sequence and each
  step may name a different animation, which is easy to lose sight of because
  most objects have only one. `A_2_DemonLook` is clip 15 (`1-02DEM`, the demon
  perched on the wall, 91 frames) and then clip 17 (`1-03DEM`, his jump down,
  41), and since `Script_SelectBodyAnimation` **snaps the body to the clip's
  own root key 0** on the tick where its param 2 is still 0, the step decides
  *where the character is* as much as what he does. A reader that takes the
  first step's clip for the whole program leaves the demon at clip 15's last
  frame — 267 units up the wall — for the whole shot, and the descent never
  happens. The beat's three clips are authored to **chain**, each starting
  exactly where the last ends (15 → 17 → 25, both gaps **0 units**), which is
  the invariant that catches it (`verify.py: engine: scene steps`);
* past the last function, `+56` is compared with the loop count `+52`:
  1 = the program ends (and the pair hands over, below), -1 = rewind and go
  again via `Script_StartScript`;
* several parameters are **mutable state**: `SelectBodyAnimation` keeps its
  current frame in params 2/3 (written back into the shared pool each tick),
  `Wait` accumulates elapsed time in param 1.

**Linked pairs alternate.** `Scene_LinkObjectPair` resolves the 21-byte
partner name to the pointer at `+84`. A paired object refuses to run while its
`+93` byte is clear; when a program completes it clears its own `+93` and sets
its partner's — the two take turns, one full program per turn
(`Porte14open` / `Porte14closed`; `Uzal---->assis` / `Uzal----->debout`,
Kay'l sitting down and standing up in the restaurant).

The contextual idles are exactly this machinery: `Telis_eat` is loop -1 with
two main functions — `SelectBodyAnimation` of scene anims 11 then 8, each
once — so Telis sits, eats, sits, eats, forever. The viewer's alternation is
the authored program, not a heuristic.

**The camera editings.** The four bytes at `+94..97` link up to four *camera
editings* to the object — the loader wires them (`Script_LinkCamEditing`,
which errors "You cannot link more editings to this script"), the editing
records living in the container at scene `+92`. They are **0 on disk in all
4511 shipped objects**, and only **95** ever take a value — which is also what
settles that they are the editing slots and not, as SCRIPT_VM.md first had it,
a "still running" flag: see [CUTSCENES.md](CUTSCENES.md). While the object runs,
`Script_PlayScript` samples the current editing at the program clock
(`Cam_PlayEditing`, its own name — keyframed position/target/roll/fov) into a
scratch camera and makes it the scene's active camera (scene `+376`); the two
camera *functions* (`Script_SelectCamera` 0x01000001, with its "Can't find
camera" log, and `Script_InterpolateCameras` 0x01000002) are skipped while an
editing is driving. Editing slots advance in order as each one's duration
expires.

**`Script_SelectBodyAnimation`, fully decoded** (0x004A35D0, read with its
whole callee cluster — 28 functions named):

```
param 0   object/bone name - an index into the object's first string table,
          resolved by o3de_FindNodeByName and cached in the table's ptr slot
param 1   the clip - an index into the scene's chunk-1 registry
param 2   start frame     } mutable runtime state, written back into the
param 3   current frame   } shared parameter pool every tick
param 4-6 the actor's Euler angles, degrees -> actor record [104..106]
          (an authored facing - 20 of the 545 shipped sites set one,
          e.g. AToit's JennaStand: yaw -40)
param 7-9 a position offset, authored in centimetres and scaled by
          1/2.54 into engine inches (75 sites set one)
```

Per tick it advances the clip on the node hierarchy (`Anim_SetFrame`; the
per-node visitor `Anim_ApplyNodeFrame` **clamps the frame to >= 1** — key 0
is the rest pose, the engine's own code confirming what the corpus showed),
moves the actor by the clip's sampled **root motion** (`Anim_RootDelta` →
`Actor_MoveBy`, which skips the spatial-index update for the player record),
and plays the clip's synced sfx. When one `SelectBodyAnimation` follows
another in the program it starts blended from the previous chain's clip and
frame. The scene's chunk-1 registry record (36 bytes, in memory at scene
`+44`, count `+20`) carries per clip: `+0` name, `+24` the loaded clip,
`+28` an sfx block — up to two frame-windowed sounds `Anim_TickClipSfx`
positions at the actor — and `+32` id.

**verified.** The field readings above are runtime claims about shipped bytes,
and the corpus agrees: across all 220 files the function run counter `+20` is
0 in all 13887 records, the repeat count `+16` is 1 in 13247 and otherwise
2..27 or -1, and the object loop count `+52` takes exactly two values — 1
(3551 objects) and -1 (960, the ambient loops). `verify.py: script programs`.

### Chunk 10 — the camera editings

**solved.** The streamed chunk-10 payload is what the engine calls a *camera
file* — its loader (0x0049EEF0, into scene `+92`) rejects any version but 3
with "Invalid camera file version". Four id-crossreferenced arrays; the
engine resolves every id to a pointer at load and refuses the file otherwise,
so "every reference resolves" is the shipped invariant:

```
+0  u32 version = 3
+4  u32 counts: cameras, keys, tracks, editings   +20 16 runtime bytes
+36 camera[52]: +0 id  +4 char[12] name  +16 pos[3]  +28 target[3]
                +40 roll  +44 fov
then key[28]:   +0 id  +4 camera id  +8 f32 frame  +20 mode
then track[24]: +0 id  +4 char[10] name  +14 u16 keyCount, then its key ids
then editing[32]: +0 u8 id  +1 char[11] name  +12 u16 trackCount
                +24 u32 duration (frames)  +28 u16 target script object id
                (the u16 at object +26; 0 = shipped unlinked), then track ids
```

An *editing* ("1DenRet", "2CaisAll"…) is a keyframed camera cut: tracks in
order, each a run of (frame, camera) keys interpolated by `Cam_PlayEditing`
(0x0049ECE0, its own name). `Scene_LoadSCX` links each editing to the script
object its target id names (`Script_LinkCamEditing` — at most four per
object, the `+94..97` bytes); while that object's program runs, the sampled
editing **is** the scene camera.

**verified.** 29 of 220 scenes carry the chunk; all 29 payloads walk exactly
to their size, all 125 editings' id references resolve (0 dangling), and
every nonzero target id names a chunk-2 object (30 of 125 ship with target 0
— authored but unwired). `python3 tools/cam_editing.py --selftest`;
`verify.py: camera editings`.

### The script functions

There are **two dispatchers**, and which one a function appears in is a fact
about it (read 2026-08-29): **`Script_StartScript`** (0x0044A7E0) runs a
function once when its program starts, **`Script_PlayScript`** (0x0044C860)
runs it every frame. `Script_GetNumParam` (0x0044C090) is the third piece — a
`(function id, parameter TYPE) → slot index` map, which is how a caller finds,
say, the sprite operand without knowing the layout by heart.

Only **17 distinct ids** occur in the 13887 shipped calls:

| function | uses | id | params | start | play |
|---|---|---|---|---|---|
| `Script_MoveObjectOnPath` | 4841 | `0x03000008` | 15 | ✓ | ✓ |
| `Script_PlaySound` | 3797 | `0x05000014` | 4 | | ✓ |
| `Script_SelectRelativeBodyAnimation` | 2398 | `0x0200002A` | 12 | ✓ | ✓ |
| `Script_PlaySyncSound` | 1628 | `0x05000015` | 5 | | ✓ |
| `Script_SelectBodyAnimation` | 545 | `0x02000004` | 10 | ✓ | ✓ |
| **`Script_Display3DSprite`** | 232 | `0x04000028` | 4 | ✓ | **—** |
| `Script_StopSound` | 86 | `0x05000016` | 2 | | ✓ |
| `Script_SetSpriteFrame` | 59 | `0x04000029` | 2 | ✓ | — |
| `Script_SetSpriteType` | 58 | `0x0400000C` | 2 | ✓ | — |
| **`Script_ScaleSpriteOnX`** | 58 | `0x0400001B` | 6 | ✓ | **—** |
| **`Script_ScaleSpriteOnY`** | 58 | `0x0400001C` | 6 | ✓ | **—** |
| **`Script_SetSpriteRolling`** | 58 | `0x0400001D` | 6 | ✓ | **—** |
| `Script_Wait` | 31 | `0x06000017` | 2 | ✓ | ✓ |
| `Script_ScaleObjectX` / `Y` / `Z` | 15 / 6 / 14 | `0x03000023-25` | 7 | | ✓ |
| `Script_SetSpriteDefaultPalette` | 3 | `0x0400001F` | 1 | ✓ | — |

**The two sound functions, and how a scene names a sound** (read from the
handlers 2026-09-03). They are what makes an animation carry its own effects —
they hang off the body animation through the `+12` sync link and
`Script_PlayScript` runs them in the same chain walk, so `Impasse.SCX`'s
arrival clip fires `STPR`/`STPL`/`STPL`/`STPR` at frames **170, 200, 210, 280**
— Kay'l's footsteps — plus a cloth movement and an ambient.

```
Script_PlaySyncSound  0x004A14D0   0 sound  1 the FRAME on the OBJECT's clock
                                   2 &1 loop  3 the latch  4 the node
Script_PlaySound      0x004A12D0   0 sound  1 &1 loop
                                   2 the latch  3 the node
```

**The layouts differ, and reading one as the other invents a cue time for
every call of the second.** `PlaySyncSound` holds the chain while its cue is
pending (`if (GetParamFloat(a2,1) > obj+88) return busy`) and both fire
**once**: the handler tests its latch on entry and writes it on the way out
(`sub_44C690(fn, 2, 1)`), and `Script_StartScript` clears it with the rest.
Cue times are on the object's clock, not the clip's, so a `Wait` step ahead of
the animation shifts none of them.

**Param 0 is a plain INDEX into chunk 3** — worth stating because the sprites
in this same format are NOT (an effect's sprite field is an *id* resolved
through the scene, and a global-index lookup lands on the wrong sprite).
`sub_48CB30` is the whole lookup:

```c
if (a2 < scene[+24])  return u16(scene[+48] + 26 * a2, 22);   /* the handle */
else                  return -1;                              /* refused */
```

a bounds-checked index into the 26-byte records, returning the record's `+22`
sound handle. So **186 of the 5425** references a program makes point past
their own scene's array, and the engine plays nothing for them — the caller
tests `!= 0xFFFF` — which makes them a property of the data, like the 551
voice-overs that do not ship. The other **5239 all begin `RIFF` and are all
accepted by `Wav_LoadToBuffer`**; the corpus streams **1667** chunk-3 records
across the 220 scenes. `verify.py: engine: scene sounds`.


#### How the two body-animation functions PLACE the character — **read from the loaders, 2026-08-30**

The two are not variants of one thing. They select the same kind of clip and
they place the character from **different sources**, and a reader that treats
them alike stages 2398 of the 2943 calls from the wrong data.

**`Script_SelectBodyAnimation` (`0x02000004`, 545 uses) — from the CLIP ROOT.**
Both are gated on **param 2 == 0.0**; when it is non-zero neither places
anything and the node keeps the position it already has.

```c
Anim_BindToHierarchy(node, clip);
Anim_SnapRootToStart(node);                 /* node <- root track key 0 */
x = node.x - GetParamFloatB(fn, 7) * -0.39370078;   /* params 7/8/9,   */
y = node.y - GetParamFloatB(fn, 8) * -0.39370078;   /* an offset in    */
z = node.z - GetParamFloatB(fn, 9) * -0.39370078;   /* INCHES          */
o3de_SetNodePos(node, x, y, z);  Actor_SetPosition(node, x, y, z);
```

**`Script_SelectRelativeBodyAnimation` (`0x0200002A`, 2398 uses) — from a
`.3DP` PATH.** It never reads the clip's root at all:

```c
paths = sub_4A6500(scene, GetParamInt(fn, 7));      /* param 7: chunk-0 record */
Path_Sample(paths[GetParamInt(fn, 8)], 1.0,         /* param 8: path within it */
            &x, &y, &z, ..., 1);
x -= GetParamFloatB(fn,  9) * -0.39370078;          /* params 9/10/11, inches  */
y -= GetParamFloatB(fn, 10) * -0.39370078;
z -= GetParamFloatB(fn, 11) * -0.39370078;
o3de_SetNodePos(node, x, y, z);  Actor_SetPosition(node, x, y, z);
```

That is what "relative" names: relative to an authored path, not to the clip.
The 12-vs-10 parameter count in the table above is exactly this — two extra
slots, because the path takes a file **and** an index where the root takes
neither.

**And params 4/5/6 are an EULER that turns the ROOT MOTION as well as the
body** — read from the same function, 2026-09-05, which closes what
`CLAUDE.md` §6 listed as open about `Anim_RootDelta`'s optional 3×3. The tail
of `Script_SelectRelativeBodyAnimation` is

```c
Anim_SetFrame(node, clip, prev, cur, delta);      /* delta out */
if (Anim_TickClipSfx(rec, node, delta)) {
    Actor_SetEuler(node, p4, p5, p6);             /* the facing, EVERY tick */
    Actor_MoveBy (node, delta[0], delta[1], delta[2]);
```

and the order is load-bearing, because the Euler is what the delta is turned
by:

| step | what |
|---|---|
| `Actors_TickAll` | `Matrix3x3_FromEulerAngles(a[104], a[105], a[106], a + 288)` — every actor, every frame, from the Euler `Actor_SetEuler` wrote |
| `Actor_LoadModel` | `sub_437140(node, actor + 288)` — binds that matrix to **`node+156`** |
| `Anim_SetFrame` | `Anim_RootDelta(clip, node+156, prev, cur, out)` |
| `Anim_RootDelta` tail | `out.x = m0·dx + m3·dy + m6·dz`, `out.y = m1·dx + m4·dy + m7·dz`, `out.z = m2·dx + m5·dy + m8·dz` — the row-vector multiply |

So a scene clip's root motion travels in the direction the call's Euler
points, not the clip's authored one. **The Euler is STICKY**:
`Script_SelectBodyAnimation` writes none, so a program that alternates the two
carries the last one written — the restaurant's `Serveur00` is Euler y 145 on
its `V5H_ATT` waits and its `SERV00`/`01`/`02` walk clips travel under that
145. Turning the body and not the motion is a walk cycle played forward while
the character slides backward, which is how this was found.
`verify.py: engine: scene facing`.

**Worked example, and it is the one that found this.** Dialog 401
(`Telis/Amoureuse`) is launched from AREA 237 with
`scx.play.actor 53, 184`; object 184 in `Aapkayl.SCX` is `TelisAuRevoir`,
whose single function is `0x0200002A` with params
`[0, 15, 0, 1.0, 0, …, 0, 6, 0, 0, 0, 0]` — clip 15, **path file 6, path 0,
no offset**. Chunk-0 record 6 is `Tecin11p.3dp`; the path inside it is named
**`Tecin11r1`** and the clip is **`TCIN11R1.3DA`**, which is the corroboration
that the two params point where this says. Its three keys are all
`[3650.9, 1039.6, -597.5]` with an identity quaternion — Telis's authored
placement, **365 units** from the clip-root position a root-only reader
produces.

**What follows for the root-key reading.** For `0x02000004` the node is
snapped to key 0 **once** and `Anim_RootDelta(prev, cur)` then adds the
movement between the previous and current frame every tick (`sub_434C30`
keeps them at `+192`/`+188`). There is **no reset**: on a loop wrap
`ceil(prev) <= cur` fails and that tick applies nothing, so the accumulated
offset stands. A clip whose whole root motion sits in key 1 — every one
examined does — therefore shows key 0 for the first frame and
`key 0 + delta` from then on.

**And the relative variant accumulates the SAME delta from its own
placement** — settled 2026-09-02, after a reader watching the intro reported
that a character rising from the floor went *higher* instead, "as if the center
of the character has to remain at the same place". The placement above happens
**once**, on the tick where the clip frame is still 0; every tick after it the
function does

```c
Anim_SetFrame(node, clip, prevFrame, frame, delta);   /* delta[3] out */
...
Actor_SetEuler(node, GetParamFloatC(fn, 4), ...(fn, 5), ...(fn, 6));
Actor_MoveBy(node, delta[0], delta[1], delta[2]);
```

and `Anim_SetFrame` fills `delta` from `Anim_RootDelta`. So **both** functions
place once and then move by the summed root deltas; they differ only in where
the placement comes from. Two things follow that a reading of the data alone
gets wrong:

* the **facing** is `Actor_SetEuler(param 4/5/6)`, authored in the call — *not*
  the path key's quaternion, which is the obvious guess and is wrong;
* `Path_Sample(path, 1.0, …, 1)` is a **linear sample at t = 1**, not
  `keys[0]`. The two coincide only when the path's first key is at frame 1.

**Where the deltas live.** A `.3DA` track's position keys are at `+24`/`+28`,
and exactly **one track per clip has any** — track 2, `UBassin`, the pelvis,
which is the hierarchy root in all 181 character models — holding `frames + 1`
of them, key 0 a sentinel exactly as for the rotations. GRID's three:
`INTRO1` walks Kay'l **106.7 units in −Z**, `INTRO2` stands still, and
`INTRO3` jumps him **158.9 in +Z and 17.4 up** (Y points down).
`verify.py: engine clip root`.

**Corrected:** the three sprite ids were previously listed as
"`SetSpriteRolling` / `ScaleSpriteOnX` / `ScaleSpriteOnY`, `0x0400001B-D`",
which is the wrong order. `Script_StartScript`'s own switch settles it —
`0x1B` is ScaleSpriteOnX, `0x1C` ScaleSpriteOnY, `0x1D` SetSpriteRolling.

~~**Three ids have no handler in the binary at all**~~ — **wrong, corrected
2026-09-02.** It said `0x0400000C`, `0x04000029` and `0x0400001F` (120 calls)
had none, and that two of them "do not occur anywhere in the executable". All
three have a handler, and each names ITSELF in the error string it prints on a
bad function type: `Script_SetSpriteType`, `Script_SetSpriteFrame` and
`Script_SetSpriteDefaultPalette`. What made them invisible is that their
handlers carry no `proc` label, so they are absent from the decompilation and a
search of it finds nothing — CLAUDE.md §1's trap, and the same one that hid two
of `Sprite_LinkToScene`'s four callers. **The lesson is the shape of the
search**: "does not occur anywhere in the executable" was a search of
`Runtime.exe.c`, not of the image. `verify.py: sprite linkers`.

**And nine implemented functions are never used**: `Script_InterpolateCameras`
(`0x01000002`), `AnimationFromExternalScene`, `MorphObject`, `SwapObject`,
**`Display3DSpriteOnPath`** (`0x0400000D`), **`ChainObjects`** (`0x04000011`),
**`MorphPaletteSprite`** (`0x04000020`), `SendMessage` (`0x06000027`) and
`0x01000001`.

**Three of these were listed as "(no handler anywhere)" and all three have
one** — corrected 2026-09-02. `0x0400000C`, `0x04000029` and `0x0400001F` are
`Script_SetSpriteType`, `Script_SetSpriteFrame` and
`Script_SetSpriteDefaultPalette`, each naming ITSELF in the error string its
handler prints on a bad function type. Their handlers carry no `proc` label —
CLAUDE.md §1's trap, which is also what hid two of `Sprite_LinkToScene`'s four
callers ([ASSETS](ASSETS.md) §3b). `Script_SetSpriteFrame` is what writes the
sprite instance's `+22`, a question §3b had left open. So "all the sprite
machinery beyond plain display is dead content" was **wrong**: what is true is
narrower — nine functions are never CALLED by the shipped scripts, which is a
fact about the content and not about the engine.

**No dialogue is launched from here.** There is no `Script_*` function that
touches conversations — the full recovered set covers objects, paths, sprites,
body animation, sound, waiting and messaging, and nothing else. Conversations
are started by the *other* script system: VM opcode 61 `dialog.start`, from the
bytecode in `IAM\GLOBAL`, `IAM\SCENE` and `IAM\AREA`. See
[SCRIPT_VM.md](SCRIPT_VM.md).

    python3 tools/scene_scx.py                   # every scene, with its chunks

---

### Two scene slots

**verified.** The engine keeps **two** areas resident. `Area_Transition`
(0x00408530) handles a change by calling `Area_LoadIntoSlot(1 - currentSlot,
areaId)` — loading the destination into the other slot while the current one is
still live — then setting the requesting script's status to 10 so it waits.
`Area_LoadIntoSlot` unloads whatever the slot held and calls `Area_Load`.

This is what the paired globals throughout the engine are:
`dword_69BC48[4 * slot]` is the scene id in each slot, `dword_69BC40[4 * slot]`
the loaded block. `Scene_FindObjectIndexById` searches the named scene first and
its parent second for the same reason.

### Every byte of an AREA / SCENE chunk, accounted for

**verified** — `tools/chunkmap.py`, `verify.py: chunk accounting`.

After twice mistaking an incomplete *enumeration* for a fact about the game
(see [CUTSCENES §5](CUTSCENES.md)), the question "do we have all the scripts?"
was settled the other way round: claim every byte a documented structure
explains, and look at what is left.

A chunk is its fixed header, eight tables, the character strings, and the
scripts. The tables' `(pointer, count, stride)` come from the loaders
themselves — `Area_Load` (0x0040CC90) and `Scene_Load` (0x0040C120) relocate
the same nine pointer fields and then loop over each with its count, and
**AREA's offsets are SCENE's plus 32 throughout, except the shared `+4`**:

| table | AREA ptr / count | SCENE ptr / count | stride |
|---|---|---|---|
| startup script | +4 | +4 | — |
| object | +40 / +72 | +8 / +40 | 20 |
| prop | +44 / +74 | +12 / +42 | 24 |
| zone | +48 / +76 | +16 / +44 | 68 |
| prop-asset | +52 / +78 | +20 / +46 | 24 |
| character | +56 / +80 | +24 / +48 | 276 |
| address | +60 / +82 | +28 / +50 | 16 |
| world camera | +64 / +84 | +32 / +52 | 44 |
| subscriptions | +68 / +86 | +36 / +54 | 8 |

Two things the corpus decided rather than the reader:

* **the tables tile exactly.** `Area_Load` walks the object and prop tables as
  `u32(v2, 40) + 8` stepping 20 / 24, and reading that `+ 8` as a *table
  header* makes each overlap its neighbour by exactly 8 bytes in **310 of 330**
  chunks. It is a field offset inside each record. Dropped, the eight tables
  abut with **0 overlaps in 330 chunks** — the test the layout could fail.
* **character record `+0` and `+4` are its two strings.** Both are relocated
  by the loader, and each points at a NUL-terminated line in the chunk: the
  biography (*"Spécialiste des armes, entraîné au combat rapproché…"*) and a
  short line, `Néant.` where there is none. **990 of the 1032** shipped
  records leave both at 0.

Scripts are marked by **reachability**, not by a linear decode — a script can
jump forward over its own `end`, and stopping at the first `op3` leaves the
tail looking unclaimed (AREA 59 did exactly that on the first run).

**The result.** 330 chunks leave **three** unclaimed runs, **97 bytes**, and
all three decode clean and land exactly on their own boundary on a terminator,
which random bytes do not do three times:

```
AREA 0   [21859, 21913)  54 B   a variable countdown, actor.stat.set (named `hud.show_var` until 2026-09-02), player.move.wait
AREA 45  [836, 847)      11 B   player.move.wait; ui.open 2; end
AREA 59  [13413, 13445)  32 B   a highscore computation, ending in a jmp
```

None is reached by any jump from any script, and only AREA 45's is named by a
pointer at all — by `+68`, whose subscription count is 0, the same
empty-table-based-at-the-code coincidence as AREA 118 (`verify.py: intro
script`). That chunk's `+4` is also 0, so it reads as a startup script that was
**unhooked**, not one that is hidden.

So the script inventory is complete to within 97 bytes of unreferenced
bytecode. It also settled the then-open `OBJECTS/314` residue in `verify.py:
trace agreement` — **not** a missing script, since none of the three announces
it. That was right, and the conclusion drawn alongside it (that the emitter
must therefore lie outside the corpus) was wrong: it turned out to be
`inventory.add`'s announced field being mis-mapped, in a script the corpus had
held all along. Both captures now replay with zero disagreements.

### `AREA +4` / `SCENE +4` — the chunk's startup script

**verified.** Every AREA and SCENE chunk can carry a **startup script** at
`+4`, run automatically when the chunk is loaded. It is a pointer field like
any other: `Scene_Load` (0x0040C120) relocates it in the same run that fixes
up `+8`, `+12`, `+16`, `+20`, `+24`, `+28`, `+32` and `+36`
(`if (v11) u32(v2,4) = v2 + v11`).

`Area_TickLoad` (0x0040C7E0) is what runs it — once for the AREA block and
once for the SCENE block, in the raw assembly:

```asm
mov  esi, dword_69BC40      ; the AREA block (then 69BC44, the SCENE)
mov  ecx, [esi+4]           ; <-- the startup script
push 0 / push 0 / push ecx / push ebp
call sub_406290             ; Script_NewContext(slot, script, 0, 0)
mov  [esi], eax             ; the context is stored at block +0
push 1 / push eax
call sub_4063D0             ; Script_QueueAction(ctx, 1)
```

That also settles `+0`, which is **0 on disk in every chunk**: it is the slot
the running context is written back into.

**173 of 330 chunks carry one** — 117 of 259 AREA, 56 of 71 SCENE; the other
157 hold 0. The field is self-checking in the [CLAUDE.md §1](../CLAUDE.md)
sense, because a pointer that was really something else would land
mid-instruction: **all 173 disassemble clean, 0 failures**, 1968 instructions
in total. `verify.py: startup scripts`.

**This is the table the 5785-slot inventory does not contain.** That inventory
walks the zone records (three script fields each) and the message
subscriptions; neither reaches `+4`. Two long-standing "nothing in the shipped
scripts does X" results were artifacts of the gap:

* **what starts a cutscene's beats** — SCENE 55's startup script fires all
  sixteen of Impasse's in authored order ([CUTSCENES §5](CUTSCENES.md));
* **AREA 118's intro script**, found earlier at `+68` because that chunk's
  subscription table is empty and based at the same address. `+4` is the field
  that names it; for chunk 118 both are 1040.

It does **not** close the conversations with no launch path: across all 173
startup scripts the only `dialog.start` not already reachable from a slot is
**272** ("Kay'l / Intro"), which the golden trace had already caught. One, not
a hundred.

### `AREA +40` / `SCENE +8` — the object table

**verified.** The 20-byte records a `CHARACTERS` or `OBJECTS` script operand
resolves to. `Scene_FindObjectIndexById` returns the index,
`Scene_FindObjectRecord` the record.

```
+0   int16  index      -1 on disk, filled in at run time
+2   int16  id         what the script operand matches
+4   int32  x
+8   int32  y
+12  int32  z
+16  int16  angle      a 4096-step turn
+18  int16  stateBit   index into the save-game bit array
```

The counts are int16 at **`AREA +72`** and **`SCENE +40`** — 830 and 202
records — both read straight out of `Scene_FindObjectIndexById`, which searches
the area's table first and then the table of the scene loaded over it. Every
operand of VM opcodes 69, 82 and 84 names a record here: **1135 / 1135**.

Three independent things line up on this reading: the 830 records in `AREA`
carry **830 distinct** `stateBit` values (0..1031) — one bit each, which is what
a persistent "has this character been removed" flag needs; `angle` spans
0..4084, the 4096-step encoding used everywhere else; and `id` spans 0..676,
exactly the operand range of opcodes 78/79.

`Actors_SpawnFromTables` (0x0040BB90) confirms every field by consuming them:
for each record it takes the id at `+2` to `Actor_FindById`, appends `".3DO"`
to that actor record's `+144` and loads the model, then hands
`{ +4, +8, +12, +16 }` to `Actor_SetPlacement` as x, y, z, facing, and calls
`Actor_Attach` only if the save bit at `+18` is set. It walks the area's table
and then the scene's, so both are the same record.

> This is the character's **world** position, not a per-conversation one.
> Telis is placed here at `(3635, 1278, -638)`, on a different floor of the
> flat, while dialog 402 is staged around `(3503, 1081, -901)` — 355 units
> away. Nothing in `AREA` or `SCENE` puts any character within 190 units of
> where that conversation actually happens, which is why the speakers have to
> come from the cameras (see 2, "The look-at point is an aim").

`State_SetBit` is the only writer of that bitmap, at game-state `+20`.
`character.show` sets an object's bit, `character.hide` clears it — so object
visibility is part of the save.

### `AREA +44` / `SCENE +12` — the prop table

**verified.** The sibling of the object table above, and laid out the same way:
24-byte records, count as an int16 at `AREA +74` / `SCENE +42`. Where the
20-byte table holds the world's **characters**, this one holds its **props** —
the medikits, ammunition, rings and quest items lying around.

```
+0   int16  slot       -1 on disk, filled in at run time
+2   int16  id         OBJECTS
+4   int32  x
+8   int32  y
+12  int32  z
+16  int16  angle      4096-step
+18  int16  angle      zero in every shipped record
+20  int16  angle      4096-step
+22  int16  stateIndex index into a **two-bit** array in the game DB
```

`Scene_LoadProps` (0x00409FC0) consumes every field: for each record whose
state has bit 0 set it takes a runtime slot from `word_4E6CA0` and writes it
back to `+0`, resolves `+2` through `IAM\OBJECT` to a model name, calls
`Object_Load` — whose own debug line is `"chargement de l'objet %s"` and whose
path is `MESHES\OBJETS\%s` — hands `{+4, +8, +12, +16, +18, +20}` to
`Object_SetPlacement` as position and angles, and finally, **if bit 1 is set**,
calls `Object_ShowInScene`. It walks the area's table and then the scene's,
which is the same two-table search the script opcodes do.

| check | result |
|---|---|
| records (546 in `AREA`, 124 in `SCENE`) | 670 |
| `slot` is -1 on disk | **670 / 670** |
| distinct ids | 300 |
| ids present in `OBJECTS.TAG` | **300 / 300** |
| `stateIndex` values | **exactly 0..669** — dense, one slot per record |
| operands of opcodes 66/76/77 that name a record here | **443 / 443** |

The dense allocation is the check that a misread field could not survive: 670
records carrying 670 distinct indices covering the range with no gaps is a
save-game slot handed out one per prop, in file order across both archives.

**Three state arrays, not one.** The game DB keeps three bitmaps side by side,
and each table uses its own:

| game DB | width | accessors | used by |
|---|---|---|---|
| `+16` | **2 bits** | `ObjectState_Get` / `ObjectState_Set` | the prop table — bit 0 "exists", bit 1 "in the scene" |
| `+20` | 1 bit | `State_SetBit` and the getter at 0x0040AF60 | the 20-byte object table — `character.show` / `character.hide` |
| `+24` | 1 bit | 0x0040B090 / 0x0040B0C0 | not yet traced |

`object.show` and `object.hide` set and clear bit 1, and `Scene_LoadProps`
replays `Object_ShowInScene` for every record that has it — so where a prop is
at the moment is part of the save, exactly as a character's visibility is.

### `AREA +52` / `SCENE +20` — the prop-asset catalog

**verified.** The third sibling (counts int16 at `AREA +78` / `SCENE +46`):
24-byte records mapping an `OBJECTS` id to the model the prop uses in the
world. `Scene_LoadProps` looks the id up (`PropAsset_Find`, searching the
loaded scene first and its area second) and loads `MESHES\OBJETS\<stem>.3DO`
(`Object_ModelPath` appends the extension).

```
+0   int16    id          OBJECTS
+2..+13       display parameters (not established)
+14  char[10] model stem  "PASSLAHO", "WIKI", ...
```

All **670** records / **124** distinct stems resolve to a shipped `.3DO` in
`MESHES/OBJETS` — none missing (`verify.py: prop assets`).

### `IAM\OBJECT` — the object records

**verified.** One 1304-byte record per object id, and every field is consumed
by a reader in the binary:

```
+0    int16      id — equals the slot number, 1002/1002
+2    int16      kind          -1 voice-only pseudo-object (561 "ZVO" records)
                               1..6   gun: Waver, Double-Waver, Octogun,
                                      Decagun, Megazooka, Hypra
                               7..11  ammunition for gun kind-5
                               12 seteks (money) · 13 anneaux (rings)
                               15/16  readable documents
+4    int16      flags         bit 0 usable · bit 1 lost on reincarnation
                               (245/1002) · bit 3 spell · bit 4 document ·
                               bit 5 counted valuable
+6    int16      effect        which stat a consumable restores (see below)
+8    int16      amount        added to that stat on use
+10   int16      price         in seteks; exposed to scripts (service 46.4)
+12   int16      quantity      ammo per pack, seteks value, ring count (46.5)
+14   char[10]   asset stem    MESHES\OBJETS\<stem>.3DO model; <stem>.ADP voice
+24   char[..]   display name  copies take 32 bytes; +56..+280 is zero in all
                               1002 records
+280  char[1024] description   the item toast: service 47 prints it with a
                               {I255000000} colour tag and plays JINGOFF2.ADP
```

The reader that settles the stat fields is `Object_ApplyEffect` (0x00409780),
the use-item path. For a plain consumable it maps **`+6` to an actor property
and adds `+8`**:

| `+6` | property | which is | carried by |
|---|---|---|---|
| 1 | 2 | Mana | mana potions |
| 2 | 3 | Carac Speed | |
| 3 | 16 | Carac Attack | `Potion Attack lev 1..3` |
| 4 | 17 | Carac Body Shield | |
| 5 | 18 | Carac Dodge | `Potion Dodge lev 1..3` |
| 6 | 1 | **Vie** | the 27 medikits, food, beer |
| 7 | 19 | Carac Fight Experience | `Potion Fight Exp +1` |

— the property numbers being exactly `Actor_GetProperty`'s, written back
through `Actor_SetProperty` (which clamps Vie and Mana at 200). For a
**counted valuable** (`+4` bit 5) it switches on `+2` instead: seteks add
`+12` to property 4 `Argent`, rings to property 5 `Anneaux`, guns and
ammunition go to the weapon store with their kind index.

The checks the data could have failed:

| check | result |
|---|---|
| `+0` equals the slot | **1002 / 1002** |
| prop-table ids whose `+14` stem names a shipped `MESHES\OBJETS` model | **300 / 300** |
| `Seteks N` / `Anneaux N` objects whose `+12` equals the N in their own name | **11 / 11** |
| gun/ammo kinds vs the `GLOBAL +32` weapon table | ammo kind = gun kind + 5 on **all five** pairs |
| descriptions saying `vie +N` where `+8` = N | 6 / 8 |

The two misses are the same item twice: `Medikit Petit`'s description says
`vie +15` while the field says 20 — the text and the tuning drifted apart, in
the data itself. `+2` = 14 (one object, `Beshem Magique`) and the difference
between document kinds 15 and 16 are **not established**.

### `AREA +64` / `SCENE +32` / `GLOBAL +20` — the world camera table

**verified.** `Camera_FindWorld` (0x0040B220) resolves a world camera id by
scanning **44-byte records with the id at +24** in the two resident areas'
tables (`AREA +64`, count int16 `+84`; `SCENE +32`, count `+52`) and falling
back to `GLOBAL +20` (count `+30`) — which is what `GLOBAL`'s "record array,
44 bytes each" is. 5381 records in all.

```
+0   int32[3]  eye x, y, z
+12  int32[3]  aim x, y, z
+24  int16     id           what Camera_FindWorld matches
+26  int16     mode         12 in 5342 of 5381; 20 in 38; 4 in one
+28  int16     roll         4096ths of a turn
+30  int16     fov          4096ths of a turn — the FULL HORIZONTAL angle
+32  int16     target subject   -1 = an absolute point
+34  int16     eye subject      -1 = an absolute point
+36  int16[3]  three further fields mode 12 carries, unread
```

The consumer that fixes the layout is VM opcode 126 `camera.set.at_address`
([SCRIPT_VM](SCRIPT_VM.md)): it converts the six int32s to floats for the
camera request block and re-aims the camera at an `ADDRESSES` entry. All 84 of
its camera operands resolve in this table.

**A quarter of the table is not two absolute points**, and this was found by
rendering it. `Camera_LoadParams` (0x004146C0) unpacks the request block and
decides **per point**, not per camera:

| subject | where the point goes |
|---|---|
| `-1` | the camera's absolute slot — eye `+20/+24/+28`, target `+32/+36/+40` |
| otherwise | the OFFSET slot — eye `+176/+180/+184`, target `+124/+128/+132`, for `sub_414520` to resolve against that actor |

The two guards are record **`+34`** for the eye and **`+32`** for the target,
and note that they reach `Camera_LoadParams` in the opposite order to their
file order, through request-block `+38`/`+40`. **1440 of 5381 records set at
least one**: 3941 are wholly absolute, 959 track an actor with a fixed eye, 406
are wholly relative. SCENE 55's camera 0 — the one the Impasse's own startup
script cuts to with `camera.set 0` — has both fields **0**, so it is an eye 119
units behind actor 0 and 27 above, looking just in front of him. That is the
third-person follow, and the camera-mode preset table states the same thing in
metric: mode 0 sits **3.00 m** behind, which is 118.11 inches.

Read as absolute it puts the camera at the world origin while `AImpasse.3DO`
sits 7000 units away, and the frame is empty with nothing to say why. Nothing
in `verify.py` could see that; a picture showed it in one look.

**They are the majority of the game's camera work, not a corner of the table.**
Counted over every `camera.set` / `camera.set.wait` site whose id resolves, the
camera is actor-relative in **1707 of 2852 cuts** and **715 of 1674 travels**.
So a replica cannot point most scripted cameras until it knows where its actors
are — and on a new game it does not: `IAM\START` leaves the player at the
`(-1, -1, -1)` sentinel, and he is placed by the actor runtime.

**The fov is the full horizontal angle in degrees, and there is no clamp.**
`Camera_LoadParams` copies request `+32` (record `+30`) straight to camera
`+48`, and the projection setup takes `tan(fov * 0.5 * π/180)` off it:

```
projX = (w/2)            / tan(fov/2)
projY = (h/2) * 1.3333334 / tan(fov/2)
```

So **247 of the 5381 records really do ask for more than 105°**, across 68
chunks, and six of AREA 118's intro cameras are in that group at 171–175°.
Rendered, they give the radial smear of a wormhole — which is what that
sequence is. The other 5092 are tight around the preset: median exactly
**75.0°**, which is 853, the value 4543 of them store.

Note the `1.3333334` on the vertical: at a 4:3 viewport `projY == projX`, and
at any other one the vertical fov stays pinned as though it were 4:3.

**The search order has no reachable tier.** `Camera_FindWorld` takes the
resident chunk's tables before `GLOBAL`, but `GLOBAL` holds only **4** cameras;
**280** chunk records carry an id it also has and **0** of them differ from
`GLOBAL`'s. `tools/cutscene.py` searches the opposite way and no shipped file
can tell the two apart. The order here follows the code and is recorded as
tier 6, read and explained (`verify.py: engine world camera`, whose GLOBAL-first
mutation passes — deliberately).

### The AREA header's asset manifest

**read from `Area_TickLoad`** (0x0040C7E0), the staged loader `Script_Pump`
polls during a transition. The header carries a run of 9-byte asset names,
each loaded in its own phase with a compiled-in extension:

| offset | extension | loads |
|---|---|---|
| +88 | `.3DO` | the set model (`Area_LoadSet`) |
| +97 | `.SCX` | the scene scripts (`Area_LoadScx`), plus the `.sfx` sibling — see [CUTSCENES](CUTSCENES.md) |
| +106 | `.MPT` | the 2D map (`Map2D_Load`); named by 16 areas, `ARCHIV04` cut |
| +115 | `.OPT` | the area's **slider circuit**, `TRAJECTOIRES\<stem>.OPT` (`Area_LoadSliderTrack` → `Slider_Init`). Only 5 areas name one and all five ship — Anekbah, Jaunpur (`SOUK`), Lahoreh, Qalisar (`QCHAUD`), Anekbah CS Puits; `BIBLIO.OPT` ships unnamed |
| +124 | — | an `.ani` animation library (`Anim_Load`) |
| +133 | `.3DO` | a `MESHES\MISC` backdrop model (`Area_LoadMiscModel`) |
| +142 | — | the music track id (int16 — the `music.play` domain) |

Later phases load the props (`Scene_LoadProps`) and hand the int parameters
at +144/+160/+176 to the fog/clip setters.

### `AREA +97` — the area's SCX name, and the contextual idle

**verified (mechanism); the embedded clips are an open format.** `AREA +97`
holds the base name of the area's scene script — `AAPKAYL` →
`SCPTDATA\Aapkayl.SCX` — read by the loader case that `sprintf`s
`SCPTDATA\%s` and appends the extension. 252 of 259 areas carry one.

It also carries the first half of the contextual-idle chain (*how does the
engine pose Telis sitting during dialog 402?*): the launch script's last
`scx.play.actor` before `dialog.start` is a loop object — 402's object 306,
`A_3_TelisStand` — whose whole program is one `Script_SelectBodyAnimation`.
Its first parameter indexes the object's name table, and those names are
**skeleton nodes, not animations**: `TeBassin` is Telis's *pelvis* (bassin,
the body part — alongside `TeCuissed`, *cuisse droite*), i.e. the table names
the **target** the function animates, `Ap01Porte` a door for
`Script_MoveObjectOnPath`, and so on. The animation itself is the numeric
second parameter, resolved in the scene's clip registry — the SCX streamed
block, whose format phase 3 opens with.

> The first version of this note read `TeBassin` as "the bathtub pose" — a
> false friend caught by a reader who plays the game: bassin here is anatomy,
> and the sibling name `TeCuissed` settles it.

The clip data itself lives in the SCX **streamed block**, now solved — see
[ASSETS](ASSETS.md), "`.3DA` — scene animations": 220/220 streamed walks land
on the file size and all 1490 embedded clips parse. 402 resolves to
`TE_STD.3DA` and 387 to `TELRES05.3DA`, the restaurant sitting pose.

### `AREA +142` — the area's music track

**verified.** `Area_Load`'s case 9 — the branch that finishes an area change —
reads an **int16 at +142** of the loaded area block and, when it is non-zero and
different from `g_MusicTrack`, hands it to `Music_PlayTrack`, which streams
`TRACKS\%d.ADP`. `Game_Close` calls `Music_PlayTrack(0, 1)` and resets
`g_MusicTrack` to `-1`.

`gamedata/TRACKS` holds **145** files scattered over 0..250, so the field has to name
one:

| check | result |
|---|---|
| AREA chunks | 259 |
| `+142` is zero — the area is silent | 70 |
| non-zero values that name a file in `gamedata/TRACKS` | **189 / 189** |

The same track ids are what VM opcode 103 `music.play` takes, under the
identical "already playing" guard — the field tested once from the loader and
once from the scripts. See [SCRIPT_VM.md](SCRIPT_VM.md).

`SCENE` chunks have a different header and nothing meaningful at +142; the
music belongs to the area, not to the scene loaded over it.

### `AREA +60` — the ADDRESSES table

**verified.** `Address_Find` (0x0040E5E0) resolves an `ADDRESSES` index by
scanning the `AREA` array at `+60` (count int16 at `+82`, stride 16) and
matching an id at **+14**.

| check | result |
|---|---|
| records across all AREA chunks | 791 |
| entries in `ADDRESSES.TAG` | **791** |
| ids at +14 present in the tag file | **791 / 791 (100%)**, all distinct |

So `ADDRESSES` are **named world positions** — `"Anekbah - Appartement de Kay'l"`,
`"Anekbah - Restaurant Tahira St."` — and the whole 16-byte record is

```
+0   int32  x
+4   int32  y
+8   int32  z
+12  int16  facing     a 4096-step turn
+14  int16  id         what Address_Find matches on
```

`Area_Load` converts the three coordinates with the usual `* 100/256/2.54 - 1`
and `+12` with `* 360/4096`, and `Actor_MoveToAddress` (0x0041BF50) reads
exactly those four fields — the position into the actor record's `[58..63]`,
the facing into `[105]` — which is what fixes the layout.

**The facing is authored too, and it points at the other speaker.** Address
678, `'Dialogue Telis Cuisine'`, is at `(3541, 1079, -914)` facing 250°. The
speaker of dialog 402 is at `(3503, 1081, -901)`, and the bearing from the
address to her is **251.5°** under `(sin θ, −cos θ)` — 1.5° out. So the
convention is `x = sin θ`, `z = −cos θ`, and the authored facing has the player
already looking at whoever he is about to talk to.

VM opcode 73 (`actor.goto_address`) is the consumer: it resolves its operand
here and hands the result to `Actor_MoveToAddress` along with `Actor_Player`,
so it always moves the player.

> The id is at `+14`, not `+0`. Field 0 matched the tag file on 1% of records —
> which is what prompted reading the resolver instead of guessing again.

#### How a line BLENDS in and out of the idle — **read from the morph player, 2026-09-02**

A reader: at the end of a line *the character animation is supposed to
"fade" to an idle animation*, and the port held the line's last frame. The
fade is the morph player's own, and it runs at BOTH ends of the line.

`Morph_Play` (0x0041AFC0) hands the player the actor's bound clip and its
current frame — `sub_42BDD0(rec[42], rec[47], facing)` — and arms the two
blends with `sub_42BE10(1, strstr(path, "02E19A") == 0)`: in always, out for
every line but the one whose name contains `02E19A`. With no clip bound it
arms neither (`sub_42BE10(0, 0)`). `Morph_Start` (0x0042C870) then sets both
lengths to **`min(30, frames × 0.25)`** — a quarter of the line, at most a
second. And `sub_42D120`, the per-frame applier, does the blend itself:

```
t <  in             from the CLIP at rec[47]   to the MORPH        f = t / in
t >  frames - out   from the MORPH             to the CLIP's KEY 1  f = (t - (frames - out)) / out
otherwise           the morph alone
```

through the same `sub_471710` → `sub_471820` → `sub_4721F0` path the `.CTL`
clip transitions use: per node, a quaternion slerp of the two tracks' keys
with the weight quantised to **k/256** (`f32(blend, 8) * 255.0`, then
`k * 0.00390625`), the root position lerped the same way. When the morph
stops (`Dialog_TickUI` case 8, `Morph_IsDone` → `Morph_Stop`) the actor's own
tick carries the clip on from whatever frame the scene program has reached —
so the engine can step at that hand-over, and the port reproduces rather than
smooths it. The general clip-to-clip transition is the same machinery with a
**5-frame** duration (`sub_434A90` writes `5.0` into actor `+480`), or the
`.CTL` entry's own through `Actor_BlendToClip`.

**And the line's ROOT ROTATION IS APPLIED, not cancelled** — the same day,
from a screenshot pair. `sub_42D120` computes the root's quaternion times a
heading yaw and means to replace the root track's key with identity, but the
index it uses, `g_MorphRootTrack` (`dword_4EB12C`), is written **once in the
whole image** — to −2, in `Morph_ResetTracks` — and the product is never read
(`var_58`, dead before `retn`). So in the shipped build no track is replaced:
all 19 recorded rotations reach the skeleton, the pelvis's included, relative
to the actor's own frame (node `+92`, the authored facing). That is why the
game bows Kay'l's whole body toward the camera on `125339` — pelvis→head
pitch **47°** at frame 420 with the root kept, 3° with it cancelled (47° against 14° over the whole line)
(`engine/tools/dump_lineblend`) — and, because the pelvis is the anchor, why
cancelling it on every line swung his feet at each fade. The `upright`
cancellation `tools/omkdata.pose` stages conversations with is a viewer
convenience the engine does not have; `engine/` keeps it only for a speaker
no scene object drives.

Ported in `engine/src/actor/pose.h` (`qslerp`, `blendTracks`,
`morphBlendFrames`) and `backends/sdl/play.cpp`; `verify.py: engine pose
blend` asserts the arithmetic and the lengths through
`tools/blend_probe.cpp`. The `/dialog` web viewer still cuts to the idle
without a blend, and still cancels the root.

## 5d. `IAM\GLOBAL` and `IAM\START` — plain files, not archives

Both are opened directly with `fopen` (through `sub_411940`), **not** through
the IAM directory. Running the archive reader over them finds a plausible-looking
"chunk" and everything after that is guesswork — which is exactly how an earlier
pass lost two of GLOBAL's ten scripts.

### `IAM\GLOBAL` — the ambient conversations

`Global_Load` (0x0040DE60):

```
+8   int32  script table   (file-relative, relocated in place)
+20  int32  record array    44 bytes each
+24  int16  script count
+30  int16  record count
```

The table is 8 bytes per entry, script offset at `+0`. Self-checking: `+20`
plus `44 * count` lands exactly on the file size (6584 + 44*4 = 6760). Ten
scripts, 161 `dialog.start` sites, 30 conversations — all the `ZP *` ambient
street people (`ZP méfiant`, `ZP Anissa 1`, `ZP banque lourd`).

**`+32` is the weapon table.** `Weapon_ObjectForSlot` indexes it as
`+32 + 2*slot` and `Weapon_SlotForObject` scans it from `+42` returning 5
upward, so slots 0..4 sit below the scan and 5..14 inside it. What the split
is, the shipped table says outright:

| slot | id | name | | slot | id | name |
|---|---|---|---|---|---|---|
| 0 | 193 | `Munitions Double-Waver` | | 5 | 189 | `Gun Double-Waver` |
| 1 | 194 | `Munitions Octogun` | | 6 | 190 | `Gun Octogun` |
| 2 | 39 | `Munitions Decagun` | | 7 | 192 | `Gun Decagun` |
| 3 | 195 | `Munitions Megazooka` | | 8 | 17 | `Gun Megazooka` |
| 4 | 311 | `Munitions Hypra` | | 9 | 191 | `Gun Hypra` |
| | | | | 10 | 40 | `Bâton de pouvoir` |
| | | | | 11 | 42 | `Gun Waver` |
| | | | | 12–14 | -1 | empty |

Five ammunition types, then the guns that fire them **index-aligned**, `slot n`
paired with `slot n + 5` on all five — a check the data could fail flatly. The
two extras have no ammunition: a melee staff and the starting pistol. VM opcode
80 `shoot.begin` takes one of these object ids as its operand, and
`g_WeaponSlot` holds the resulting slot for as long as shoot mode lasts.

### `IAM\START` — the initial game state

**Not scripts at all.** `Game_NewGame` (0x0040E060) reads the whole file and
hands it to `State_Apply` (0x0040DB00), which is the same function that applies
a *saved* game read from `IAM\GAMES` — so START is simply the new-game save.

```
+8..+28   six int32 offsets, relocated  (1420, 4196, 4716, 4884, 5016, 5116)
+44       player start: x, y, z, angle - the same *100/256/2.54 - 1 and
          *360/4096 conversions used on DialogCamera
+60       the player's own 276-byte character record
+1414     int16  starting area id
```

**The whole 8192-byte block is decoded in [GAME_STATE.md](GAME_STATE.md)** -
every field, the six arrays and their counts, the three object lists, the save
file and the clock, with `tools/gamestate.py` as its reader. What follows here
is only the part that identifies the *file*.

> An earlier version of this section called `+60` "the game-variable block".
> It is not: the variables are the first relocated array, at `+8`, and `+60` is
> the player's character record - the one `Actor_FindById` returns before it
> scans anything. Both facts come from the same function, three lines apart.

**verified.** The starting area is **118**, a real `AREA` chunk, and
`State_Apply` calls `Area_Load` on it directly. A nice cross-check: area 118 is
one of the 18 with *zero* trigger records — an intro area with nothing to walk
into.

The shipped file's player position is `-1, -1, -1` and its starting actor is
`0xFFFF`, and the loader guards on exactly that (`if (actor != 0xFFFF)`), so
that whole placement branch is skipped on a new game; the area places the
player instead.

---

## 5e. From a conversation to a character model

**verified.** Nothing stores "this conversation uses that model" directly, but
the chain is short and every step is in a loader:

```
IAM\DIALOG chunk, header word 0        the speaker's actor id
  -> the 276-byte actor record with that id at +272, in the AREA or SCENE
     chunk whose script launches the conversation   (sub_40B190)
       +144  the character model in MESHES/PERSOS
       +72   its .CTL animation state machine
```

`Actor_FindById` (0x0040B190) is what establishes the table: to resolve an actor
id it scans exactly two arrays — `AREA +56` (count int16 at `+80`) and
`SCENE +24` (count int16 at `+48`), 276 bytes each — matching the id at `+272`.
Those are the same arrays a script-hunting pass dismissed as "not bytecode";
they are the character table.

**Which field is which is not inferred from the strings** — the extensions are
constants in the binary, appended to the field before it is used:

| field | extension | goes to | which does |
|---|---|---|---|
| `+144` | `dword_4C0D1C` = `.3DO` | `Actor_LoadModel` | `sprintf("MESHES\\Persos\\%s")`, then looks up `Visage`, `Tete`, `Buste`, `Brasd`… |
| `+0`, `+4` | — | (relocated, never decoded) | two French description strings — `"Spécialiste…"`, `"Aime la musique"`, `"Tatouage sur…"`: the character's bio, not bytecode |
| `+72` | `dword_4C0D14` = `.CTL` | `Actor_LoadBankList` → `LoadBankList` | calls **`InitCEFFile`** — the state machine loader |

So the model path resolves to the exact directory these tools read characters
from, and its mesh lookups are the same bone names used for skinning; and the
`.CTL` path lands in the loader decoded in ASSETS.md. Both ends check out
without appealing to what the strings look like.

| check | result |
|---|---|
| actor records naming a real `MESHES/PERSOS` model | **1032 / 1032 (100%)** |
| distinct models referenced | 161 of 193 |
| `+72` values | `MECA`, `H1AVNT`, `F1AVNT`, `SHAM` — the four `.CTL` files |
| conversations whose speaker id resolves to an actor | 198 / 235 (84%) |
| ...in the chunk that launches it | 156 / 235 (66%) |

**Cross-checked against an independent chain.** A line's `.3DM` supplies face
vertices for one specific model, so its vertex count must match the model's face
mesh. For the 153 conversations where both are known, **150 agree (98%)** — two
routes with no data in common landing on the same model. The three that differ
are near misses (134 vs 130, 132 vs 135), most likely a model variant.

The names corroborate it without any measurement at all:

| dialog | | model |
|---|---|---|
| 402 | Telis/Appart | `TEL_FNM` |
| 167 | Matanboukous/Wikis | `MBK_FNM` |
| 180 | Infirmière/Morgue | `NUR_FNM` |
| 101 | Vendeur Armurerie | `V5H_FNM` |

`omkdata.dialog_actor(id)` returns it, and the viewer now selects the speaker's
model by itself instead of asking.

### How a node is presented — the menu phase

**Observed in the running game** (dialog 402, node 17), not derivable from the
data alone; recorded because it fixes what the fields *mean*:

* The NPC line and the selection menu are **never on screen together**. The
  line plays; the player presses **next**; and on that click the game shows
  the menu — **even when it has a single entry** — and cuts to the menu's own
  camera, the node's `replyCamera` pair. That is what the reply pair on a
  plain NPC node (402's node 0 carries `4575/4576`) is for; it is not only
  for player-line nodes. Pressing next on a node with nothing but an unnamed
  continue branch goes straight to the next line instead.
* A node carrying **both** `line` and `selfLine` is one whole staged beat, in
  this order: the NPC `line` plays (line cameras) → a **single-choice menu**
  offering the node's own `selfLine` (reply cameras) → the player speaks it →
  the node's replies menu. Node 17: `"Kay'l ? Tu es sûr que tu te sens
  bien ?"` → menu `["Non, je vais bien…"]` → the three real choices.
* A single-choice menu can equally be followed by another NPC line — that is
  just the target node opening with a `line` of its own.

The viewer stages this the same way — line, a **next** button, then the menu
with the camera cut on the click — and used to render the both-fields node
backwards (`YOU:` above the NPC line) and the line and menu simultaneously,
until the in-game behaviour was checked.

### The character record's stat block

**verified.** `Actor_GetProperty` (0x0040B360) exposes the rest of the 276-byte
record to the scripts one case at a time, each case reading a different offset,
and VM opcode 86 `var.set.actor_stat` writes the result into a variable the
script names. So `IAM\VARIABLES.TAG` supplies the field names:

```
+108  byte    Sexe — ASCII 'M' (77) or 'F' (70); what property 0 points at
              (657 M / 74 F / 289 unset across the 1032 records)
+154  int16   (property 8; no script uses it)
+156  int16   Mana
+158  int16   Carac Speed
+160  int16   Carac Attack
+162  int16   Carac Body Shield
+164  int16   Carac Dodge
+166  int16   Carac Fight Experience
+168  int16   (property 20; zero in all 1032 records)
+170  int16   Vie
+172  uint16  Argent
+174  int16   Anneaux
+176  uint32  Type Spectre
+270  int16   the OBJECTS id this character is holding, -1 for none
```

**The three 9-byte `.CTL` slots** at `+72`/`+81`/`+90` name themselves in the
shipped data: `H1AVNT` (*aventure* — walking), `H1SHOT` (*shoot*), `H1CMBT`
(*combat*), with `F1`/`D1` variants for women and demons and `MECA`/`SHAM`
using one machine for all three. `player.become` copies this whole record into
the game DB at `+68` — the player's persistent character sheet — and
`fight.begin` switches both combatants to slot 2. See
[SCRIPT_VM](SCRIPT_VM.md).

**And a character can carry no slot at all, which is not a gap in the data.**
Of the 1032 records, 136 name an adventure machine, 92 a shoot one and 141 a
combat one; the rest name none, and the twelve `ZOH_FN` zombies and `GND_FN`
of AREA 2 (Gandhar's cave, a **shoot-mode** area) are empty in all three.
Those characters are posed from a different place entirely — an **`.ani`
library named by the AREA chunk's `+124`**:

```
0x0040CA9C   sprintf(buf, "%s%s", chunk+124, ".ani")  ->  sub_434010(buf)
sub_434010   sprintf("ANIMS\%s"), checks the file's own "3.0V" magic, and
             fills dword_52B95C with 24-byte records — ONE library resident
             at a time, holding every character type in the area
sub_434530   scans it for a record whose +0 is the CHARACTER TYPE, else
             Dbg_Trace("Perso %d non existant dans le .ani")
Shoot_ActorEnter (0x00422C10)   stores that list in the shoot record's +20,
             sets ACTOR_STATE 3, and installs one of the four AI callbacks
Shoot_ActorAction (0x00423170)  List_PickRandomByType(list, 9 / 10 / 11) —
             a clip drawn at RANDOM by type, else "anim non existante dans
             le .ANI"
```

**20 of the 259 AREA chunks name one, 9 distinct names, and every one of the
9 is a shipped file.** They corroborate themselves against the models the same
areas stage: AREA 2 names `GANDHAR` and stages `GND_FN`/`ZOH_FN`, AREA 144
names `ZTECH` and stages `ZTK_FN`, AREA 59 and 230 name `BRAQUEUR` and stage
`BRA_FN`, the three cities name `PASSANTH` and wear the street crowd. Two of
the eleven shipped libraries — `zombie.ani` and `biblio.ani` — are named by no
area. `SCENE` has no such field: the same offset there is inside its pointer
block. `verify.py: area .ani library`.

Contiguous, and in property order. Across opcode 86's 459 sites **18 distinct
variables are written and every one by exactly one property**, which is what
attaches each name to its offset; see [SCRIPT_VM](SCRIPT_VM.md). The values
read as a stat block too: `Vie` is 100 in 386 records and 0 in 242, `Carac
Attack`/`Body Shield`/`Speed` move together at 10/15/20, `Argent` runs 0..1250
and `Anneaux` is non-zero in only 4 records of 1032.

### The player's position is authored, not inferred

**verified.** The world scripts stage a conversation before starting it:

```
actor.goto_address N   ->   fade.to_black   ->   dialog.start   ->   fade.from_black
```

So when a `dialog.start` is preceded by `actor.goto_address`, that `ADDRESSES`
entry **is** the player's position — authored, not derived from where the reply
cameras happen to point. **95 conversations** have one, and the names say what
they are: `'Teleport Dialogue'`, `'Dialogue Vendeur'`, `'Dialogue Telis Cuisine'`.

The two methods corroborate each other. On the 65 conversations where both
exist they agree within 150 units in **60** cases, median gap **43** — and the
handful that disagree are ones the camera inference gets wrong (dialogs 171,
174 and 175 land 10234 units away while their siblings 172/173 are correct).

`omkdata.dialog_player_address()` returns it and `speaker_positions` prefers it,
reporting which source it used.

> The NPC's position is **not** taken the same way. Each character has a
> position in the 20-byte object record, but it agrees with the conversation's
> own cameras only 38% of the time (median gap 205) — it is where the character
> stands in the world, not where this conversation is staged, and a reused actor
> id resolves to the wrong area. The camera median stays.

### The 22 that do not resolve

They fall into two groups, and neither is a gap in the chain.

**19 have speaker id `-1`**, and they are exactly the `ZP *` ambient street
conversations launched from `IAM\GLOBAL` (`ZP méfiant`, `ZP Anissa 1`,
`ZP banque lourd`, `ZPF drague`…). `-1` is not "unknown" — `Actor_FindById`
takes it as its first branch:

```c
result = dword_69BC6C;                       /* the player's own record */
if (i16(dword_69BC6C, 272) == a1 || a1 == -1)
    return result;
```

So the speaker is **whoever the player currently is**. In a game built on body-
hopping that has to be resolved at run time, which is why nothing static can
name a model for it — correctly, not for want of looking.

**The other three fail every test there is.** Dialogs 19 (`Kamiji 5/Ignore
Moy'eb`), 51 and 63 (`Discours Reshev 1` and `2`) name speaker ids 16 and 33,
and those ids appear in **no** actor table and **no** object table — 0 of the
601 object ids and 0 of the 1032 actor records. None of the three is launched
from anywhere either: opcode 61 is the **only** opcode whose operand indexes
`DIALOGS`, and the one other route into `Dialog_Load` — the dialogue driver's
command 0 — is never issued (all 162 call sites of the ScenarEngine wrapper pass
a literal command, and none is 0).

Being unlaunched is **not** on its own unusual, which is worth stating plainly:

| of the 321 real conversations | |
|---|---|
| launched by a script | 215 |
| never launched | **106** |
| ...of those, speaker resolves to a real actor | **103 (97%)** |
| ...speaker does not resolve | **3** |

So 103 perfectly ordinary conversations are also never launched by any script.
**Nothing in the shipped data reaches them**, and that is now a searched result
rather than an assumption:

* `dialog.start` (opcode 61) is the **only** way into `Dialog_Load`. The other
  route, the dialogue driver's command 0, is never issued — all 162 call sites
  of the ScenarEngine wrapper pass a literal command and none is 0 — and no
  other VM handler calls `Dialog_Load`.
* All **1246** `dialog.start` operands are **direct literals**. The handler
  supports an indirect mode (bit 14 fetches the index from the script's own
  operand table at run time, which could have chosen a conversation
  dynamically), and it is used **zero** times.
* The conversation scripts cannot start a conversation: opcode 61 appears in
  none of the 612, re-checked after the operand-table corrections.
* Every relocated pointer array in `AREA` and `SCENE` is accounted for — object
  table (20), the prop table (24), the world camera table (44), coordinates
  (16), trigger volumes (68), actor table (276), second script table (8) —
  plus the single pointers at `AREA +52` and `SCENE +20`/`+28`, none of which
  are bytecode.
* `IAM\OBJECT` is not a directory archive but **1002 fixed slots of 2048 bytes,
  1304 used** (`Archive_ReadChunk(path, index, 1304, 2048)` takes
  `offset = index * 2048`, `size = 1304`). Scanned in full: **one**
  `dialog.start` site in the lot. The record itself is now solved — see
  "`IAM\OBJECT` — the object records" above.

So 106 of the 321 conversations — a third — have no launch path in the data at
all. 103 of them name a real speaker, which is why they read as finished content
rather than debris. Omikron shipped heavily cut, and a systematic set like
`Jenna/Refus d'objet 1`, `Dakobah/Accepte Objet 1`, `Vieux Kamiji/Guéri` looks
like interactions that were written and then dropped. That is a reading, not a
proof — what is proven is only that nothing here reaches them.

> Searching the VM handlers for a call to `Dialog_Load` returns opcodes 61
> **and 152**; the 152 hit is spurious, because its handler block is not
> bounded. See [SCRIPT_VM.md](SCRIPT_VM.md).



---

## 6. Playing a conversation

`tools/omkdialog.py` walks a conversation from the shipped data: it resolves
the node graph, runs the branch scripts on the VM, and prints every variable
write and engine call.

    python3 tools/omkdialog.py                  # list all 321 conversations
    python3 tools/omkdialog.py 268              # play one
    python3 tools/omkdialog.py 268 --set 89=1,90=1
    python3 tools/omkdialog.py --selftest
    python3 tools/adp.py <file.adp> out.wav      # decode a voice line

**verified.** `--selftest` walks all 321 conversations taking the first
available reply: 811 nodes visited, 737 lines shown, 248 scripts run, no
failures. Nothing about the node layout, the pool walk, the branch targets or
the VM breaks anywhere in the corpus.

### Audio

Voice playback is on by default. Lines are taken from the PC `gamedata/MORPH/*.3DM`
via `tools/morph3dm.py`, keyed by `DialogNode::name`, and decoded with
`tools/adp.py`. If a `.3DM` is missing the player falls back to the Dreamcast
extraction and says so. `--no-audio` turns it off.

## 7. Open questions

* ~~`DialogNode` fields at +56, +58, +60, +62.~~ **Solved**: they are
  DialogCamera ids — `replyCamera`, `replyCamera2`, `lineCamera`,
  `lineCamera2`. Every one of the 3132 non-(-1) values names a camera in its
  own chunk (100%).
* ~~Which of `ptr[k]` / `ptr[4+k]` is condition and which is action~~
  **Proven by tracing** the shipping event dispatcher: event 55 (fired while
  `Dialog_TickUI` builds the reply menu) *evaluates* `ptr[0..3]` for a value
  through `Dialog_EvalBranchCondition`; event 59 (fired when a reply is
  chosen) *executes* `ptr[4..7]` in a throwaway context through
  `Dialog_GetBranchAction`. Conditions gate, actions run.
* How the engine reaches the 106 conversations that no script launches. Every
  route in the shipped data has been checked and none of them reaches these -
  see section 5e. Either the mechanism is outside the data files entirely, or
  the content is cut.
* Most VM opcodes are identified only by their `.TAG` domain, not decoded
  individually. The conversation scripts use 25 of them; the **world** scripts
  in `IAM\AREA`, `IAM\SCENE` and `IAM\GLOBAL` use **124**, so the great
  majority are exercised but unnamed. Nine operand counts in the table at
  0x004C0140 were wrong and are corrected in `tools/dialog_disasm.py`; the
  method for recovering them from the handler assembly is in
  [SCRIPT_VM.md](SCRIPT_VM.md) and `tools/vm_oplen.py`.
* ~~`DialogCamera` fields at +26, +36, +38, +40.~~ **Solved**: +26 is a camera
  mode read by `Camera_RequestChanged`; +36/+38/+40 are two-shot framing
  parameters, non-zero on exactly the 33 cameras with `subject == 6`.
* The `float[3]` at the start of each `.3DM` frame record, and what node
  slots 0 and 1 hold instead of quaternions.
* `.CTL` entry flags: the bits that gate each optional block are known by
  position (0x10, 0x140, 0x280, 0x2000000, 0x8002) but not by meaning, and the
  32-byte group records and 156-byte second table are walked for their sizes
  without their fields being understood. The clips they carry are fully
  readable — see [ASSETS.md](ASSETS.md).
* The `.3DM` topology question is answered in [ASSETS.md](ASSETS.md): the
  triangle list lives in the speaking character's `MESHES/PERSOS/*.3DO`, and a
  `.3DM`'s vertex count matches that model's face mesh.

// SPDX-License-Identifier: GPL-3.0-or-later
// POSING A CHARACTER - the `.3DM`'s bone tracks composed down the hierarchy.
//
// A `.3DM` is a conversation line: `docs/FILE_FORMATS.md` has the container,
// and `src/formats/morph.h` walks it. Per frame it carries
//
//     nodeCount quaternions   the skeleton
//     one 12-byte translation the ROOT's, and a per-frame DELTA
//     vertexCount * 24 bytes  the FACE, morphed rather than boned
//     the ADPCM block         the voice
//
// read straight off the demuxer `sub_42D960`:
//
//     for i in 0 .. trackCount-1:
//         if i == rootTrack:  read 12 bytes -> the root translation
//         read 16 bytes -> quaternion[i]
//     read 24 * vertexCount bytes -> the vertices
//
// so there are `nodeCount` quaternions and the 12-byte block is **not** a
// header at the start of the record: it belongs to the root track and sits
// wherever that track falls. Scanning for one aligned quaternion array misses
// the tracks before it.
//
// The file's preamble is the track table - `nodeCount` uint32s which are mesh
// indices in `.3DO` file order, and a plain 0,1,2,... in all 777 shipped
// files.
//
// **The stored quaternion is the CONJUGATE of the rotation to apply.** Without
// negating the vector part the torso and head still look right - when the
// root's rotation is cancelled - and every limb bends the wrong way: arms
// swing backwards and knees invert. That is the shape of error CLAUDE.md 1
// calls invisible at rest, since a single still frame of a bent arm looks
// plausible either way.
//
// **Rest positions are ABSOLUTE**, not relative to a parent, so only the
// position accumulates down the tree:
//
//     rot[m] = rot[parent] * q[m]
//     pos[m] = pos[parent] + rot[parent] * (rest[m] - rest[parent])
//
// The last mesh - the face - has no track; its vertices are animated instead,
// which is the morph half and is NOT done here.
//
// TIER: this is a transcription of `tools/omkdata.pose` / `_compose`, which
// the `/dialog` viewer has posed characters with since 2026-08-27 and which a
// reader has watched. `verify.py: engine pose` differences the two.
#pragma once

#include "formats/mesh3do.h"
#include "o3de/geom3do.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include <vector>

namespace omk {

struct Quatf { float w = 1, x = 0, y = 0, z = 0; };

Quatf qmul(const Quatf& a, const Quatf& b);
void  qrot(const Quatf& q, const float v[3], float out[3]);

// One frame of every track, plus the root's accumulated translation.
struct NodeTracks {
    int  count = 0;                        // nodeCount
    int  frames = 0;
    int  rootTrack = 0;
    std::vector<std::int32_t> ids;         // mesh index per track
    std::vector<std::vector<Quatf>> quats; // [frame][track]
    std::vector<std::array<float, 3>> trans;   // [frame], accumulated
    bool valid() const { return count > 0 && frames > 0; }
};

// ------------------------------------------------------- A .3DA CLIP
//
// The other source of a pose. A `.3DM` is a conversation line; a `.3DA` is a
// scene clip out of the `.SCX` stream, and it is what a scene object plays
// through `Script_SelectBodyAnimation` (and its relative variant). `GRID`'s
// three - `INTRO1/2/3.3DA` - are Kay'l arriving, standing and leaving, which
// is the whole of the intro's action around the conversation.
//
// The payload:
//
//     +0   int32  frames
//     +4   int32  trackCount
//     +8   track[n], 40 bytes:
//          +0   int32     the MESH INDEX this track drives
//          +4   char[20]  the bone's name
//          +24  int32 posKeys   +28 int32 posOffset   (12 bytes a key)
//          +32  int32 rotKeys   +36 int32 rotOffset   (16 bytes a key, wxyz)
//
// **The position keys are the ROOT MOTION** and dropping them was a visible
// bug for a while: exactly one track per clip has any - track 2, `UBassin`,
// the pelvis - and it holds `frames + 1` of them, key 0 a sentinel like the
// rotations'. `Anim_RootDelta` SUMS keys 1..frame and `Actor_MoveBy` applies
// the result, so `trans[f]` here is that running sum. With it dropped the
// character is pinned at his placement while every limb rotates about a fixed
// pelvis - he rises off the floor instead of standing up, and GRID's INTRO3
// never performs its 158.9-unit jump.
//
// **Key 0 is a REST SENTINEL, not frame 0** - a track holds `frames + 1` keys
// (CLAUDE.md 5), so frame `f` reads key `f + 1`. Reading key 0 as frame 0 puts
// a one-frame T-pose at the start of every loop.
//
// The result is the same `NodeTracks` a `.3DM` produces, so `composePose`
// takes either without knowing which it has.
NodeTracks clipTracks(std::span<const std::byte> clip);

// A clip's ROOT MOTION on its own - the accumulated position keys of the one
// track that has any, per frame. `clipTracks` fills its `trans` from this;
// callers that only need to know where a character IS (the camera that follows
// him, say) use it directly rather than decoding every rotation in the clip.
std::vector<std::array<float, 3>> clipRootMotion(std::span<const std::byte> clip);

// ...and the clip's root key 0, which is the authored PLACEMENT that
// `Script_SelectBodyAnimation` snaps the node to before the deltas accumulate
// (docs/FILE_FORMATS.md). -> false when the clip has no position track.
bool clipRootStart(std::span<const std::byte> clip, float out[3]);

// Which track carries the root - the one whose mesh has no parent.
int rootTrackOf(const std::vector<Mesh>& meshes);

// Read a `.3DM`'s tracks. `rootTrack` comes from the MODEL, not the file.
NodeTracks nodeTracks(std::span<const std::byte> morph, int rootTrack);

// One mesh's resolved transform.
struct MeshPose {
    Quatf q;
    float pos[3] = {0, 0, 0};
};

// Compose one frame down the hierarchy. `upright` cancels the ROOT's own
// rotation about its own origin, which is what `tools/omkdata.pose` does by
// default and what keeps a character standing when the clip's root spins.
//
// **It is a STAGING convenience, not the engine, and it is wrong for an action
// clip.** `Anim_ApplyNodeFrame` traverses and applies every node's quaternion,
// the root's included; the only thing layered on top is
// `Actor_SetEuler(param 4/5/6)`, the authored facing. So a `.3DM` dialogue
// line - a character standing and talking, whose root spin is camera-relative
// noise - keeps the cancellation, and a `.3DA` SCENE CLIP must not: its root
// rotation is the character's real orientation. Cancelled, GRID's INTRO1
// forces a man crawling on the floor upright and he appears to float through
// the scene, which is what a reader saw. The tell is only in the TRANSITION:
// kept, his posture goes from 19.5 high by 37.4 wide at frame 30 to 57.9 by
// 26.4 at 184; cancelled, he measures "standing" at every frame.
// `verify.py: engine clip root`.
std::vector<MeshPose> composePose(const std::vector<Mesh>& meshes,
                                  const NodeTracks& t, int frame,
                                  bool upright = true);

// ------------------------------------------------- BLENDING TWO POSES
//
// THE FADE AT A LINE'S TWO ENDS, read out of the morph player 2026-09-02
// after a reader said the character "should fade to an idle animation" where
// the port froze on the line's last frame.
//
// `sub_42D120`, the per-frame morph applier, blends the skeleton in and out
// of the line itself, node by node, through the same `sub_471710` ->
// `sub_471820` -> `sub_4721F0` path the `.CTL` clip transitions use:
//
//     Morph_Play:   sub_42BDD0(rec[42] = the actor's bound CLIP, rec[47] =
//                   its current FRAME, facing); sub_42BE10(1, blendOut) with
//                   blendOut = 0 only for the one line whose name contains
//                   "02E19A"; no clip bound -> sub_42BE10(0, 0), no blend
//     Morph_Start:  in  = min(30, frames * 0.25)     dword_4EA8CC
//                   out = min(30, frames * 0.25)     dword_4EA8D0
//     sub_42D120:   t < in            -> from the CLIP at rec[47], to the
//                                        morph, fraction t / in
//                   t > frames - out  -> from the morph, to the CLIP at
//                                        KEY 1 (its first frame), fraction
//                                        (t - (frames - out)) / out
//                   between           -> the morph alone
//
// and `sub_471820` slerps each node's two quaternions with the fraction
// quantised to k/256 (`sub_4721F0`, `v38 = k * 0.00390625`), then builds the
// node matrix; the root position lerps the same way. So a line does not stop
// on its last frame: over its last quarter (at most a second) the body eases
// into the idle's first frame, and when the morph stops the actor's own tick
// carries the idle on from there.
//
// `qslerp` is that interpolation; `blendTracks` is one frame of `a` eased
// toward one frame of `b` per track, matched by mesh id, as a one-frame
// NodeTracks `composePose` takes unchanged. `cancelRootA`/`B` drop that
// side's ROOT quaternion to identity before blending - which is exactly what
// `composePose(upright = true)` does after composing, since the root has no
// parent - so a line (staged upright) blends against a scene clip (not) with
// one composition and no flip mid-fade. A track present on one side only
// blends against identity; the engine's `sub_471820` falls back to its pose
// snapshot there, which this does not carry.
//
// **AND THE LINE'S ROOT IS KEPT, not cancelled** (2026-09-02, from a
// screenshot pair). `sub_42D120` means to replace the root track's key with
// identity, but `g_MorphRootTrack` (dword_4EB12C) is written exactly once in
// the whole image - to -2, in `Morph_ResetTracks` - so in the shipped build
// no track is replaced and every recorded rotation reaches the skeleton, the
// pelvis's included. The `.3DM` records the root RELATIVE to the actor's own
// frame (node +92, the authored facing), which is why the game keeps a seated
// speaker's orientation while she talks AND bows Kay'l's whole body toward
// the camera on 125339 (pelvis->head 47 degrees at frame 420, 3 with the
// root cancelled; 47 against 14 over the whole line). `composePose(upright = true)` - the cancellation the web
// viewer stages conversations with - drops both; the viewer keeps it only as
// the fallback for a speaker no scene object drives. `tools/dump_lineblend`
// prints the two readings side by side over a line.
//
// NOT ported: the engine's blend-out targets the idle's key 1 and then hands
// the node to the actor tick at whatever frame the scene program has reached,
// so the engine itself can step at the hand-over; this reproduces that rather
// than smoothing it. `tools/verify.py: engine pose blend` asserts the
// arithmetic and the two lengths through `tools/blend_probe.cpp`.
Quatf qslerp(const Quatf& a, const Quatf& b, float t);
NodeTracks blendTracks(const NodeTracks& a, int frameA, bool cancelRootA,
                       const NodeTracks& b, int frameB, bool cancelRootB,
                       float t);
// `Morph_Start`'s lengths: a quarter of the line's frames, at most 30.
inline float morphBlendFrames(int frames) {
    const float q = static_cast<float>(frames) * 0.25f;
    return q < 30.0f ? q : 30.0f;
}

// ---------------------------------------------------------- THE FACE
//
// The `.3DM` is a MORPH file, and the bone tracks are only half of it. After
// the quaternions each record carries `vertexCount * 24` bytes - six floats a
// vertex, of which the first three are a POSITION in the face mesh's own local
// space and the rest a normal this renderer does not use.
//
// Those vertices are the FACE, and they are why the format is called a morph:
// the face mesh has no bone track (`nodeCount` tracks cover every other mesh),
// so it is animated by replacing its vertices outright rather than by
// rotating it. That is what makes a character's lips move.
//
// **Which mesh is the face is a NAME**: the model's mesh called `*visage*` -
// French for face, and the model's own word. `tools/omkdata.model_geometry`
// picks it the same way, and the corroboration is a count: for 150 of the 153
// conversations where both ends are known, the model's face-vertex count is
// exactly what the line's `.3DM` supplies. Two independent chains agreeing.
// When they do NOT agree the stream does not fit the model and the face must
// be drawn from its own bind vertices instead of animated - the viewer says so
// on screen rather than drawing a scrambled head.
struct FaceMesh {
    int         mesh  = -1;      // index into the model's meshes
    std::size_t base  = 0;       // its first GLOBAL vertex index
    int         count = 0;       // how many vertices it has
    bool valid() const { return mesh >= 0 && count > 0; }
};

// The mesh named `*visage*`, with its global vertex range.
FaceMesh faceMeshOf(const std::vector<Mesh>& meshes);

// One frame's face vertices, `count * 3` floats in the mesh's local space.
// Empty when the file has none or the frame is out of range.
std::vector<float> faceFrame(std::span<const std::byte> morph, int frame);

// Apply a pose to a character's geometry, in place: every corner is moved by
// its own mesh's transform, out of the rest position the mesh's absolute
// offset already put it at.
//
// `rest` must be the geometry `buildGeometry` produced for the same model -
// the corners carry `cornerMesh`, which is what selects the transform.
// `face` and `faceVerts` animate the face mesh: where a corner belongs to it,
// its local position comes from the morph frame instead of from the rest
// geometry. Pass a null `face` or an empty frame and the face is posed like
// any other mesh, which is the bind pose - what the viewer falls back to when
// the stream does not fit the model.
void applyPose(Geometry& g, const Geometry& rest,
               const std::vector<Mesh>& meshes,
               const std::vector<MeshPose>& pose,
               const FaceMesh* face = nullptr,
               const std::vector<float>* faceVerts = nullptr);

}  // namespace omk

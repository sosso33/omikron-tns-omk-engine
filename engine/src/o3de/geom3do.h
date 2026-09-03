// SPDX-License-Identifier: GPL-3.0-or-later
// Assembling a .3DO's faces into drawable batches.
//
// This is the step after mesh3do.h's record readers: resolving each face's
// vertex indices - including the NEGATIVE ones, which name a vertex of an
// ancestor mesh - adding the owning mesh's offset, and grouping the corners
// by material into the order the engine draws them in.
#pragma once

#include "formats/mesh3do.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

// Which meshes to draw.
enum class DrawFilter {
    // The ENGINE's rule, and the one a replica wants. The traversal feeding
    // Render_SubmitMesh guards every call with one test - `test [eax], 800043h`
    // - so a mesh is skipped when flags & (0x1 | 0x2 | 0x40 | 0x800000).
    // Note what is NOT in it: flags == 0 is drawn (docs/ASSETS.md 4).
    Engine,
    // What tools/omkdata.py's decor_geometry does: drop CollisionOnly only.
    // Here so the arithmetic can be proved against that reader, since the two
    // filters disagree in both directions and would otherwise mask a real
    // difference in the resolution below.
    DecorViewer,
};

inline constexpr std::uint32_t kDrawableMask = 0x800043u;  // the engine's test
inline constexpr std::uint32_t kCollisionOnly = 0x800000u;
inline constexpr std::uint32_t kCutout       = 0x800u;
inline constexpr std::uint32_t kTransparent  = 0x1000u;
inline constexpr std::uint32_t kAdditive     = 0x2000u;
inline constexpr std::uint32_t kMultiply     = 0x4000u;
// The per-vertex colour SHIMMERS: sub_4947F0 adds a signed value from a
// 32-entry table to all three channels, phased by (clock>>2)+(addr>>4). The
// vertices are a 32-byte stride so addr>>4 steps by 2 a vertex.
inline constexpr std::uint32_t kFlicker      = 0x8000000u;
// The MIRROR bit. 6 meshes of 12203 carry it (ASSETS 4c); the engine stores
// the one it finds in a single global and reflects the camera through it.
inline constexpr std::uint32_t kMirror       = 0x100000u;

enum class Blend { Opaque, Add, Mul };

// One corner, in the layout the viewers' wire already uses.
struct Corner {
    float x, y, z;      // world position: the vertex plus its mesh's offset
    float u, v;         // texel, still in the material's own pixel units
    float r, g, b;      // the baked light - a COLOUR, not a brightness
    float phase;        // shimmer phase, or -1 when this mesh does not
};

struct Batch {
    std::int32_t material = 0;
    bool         cutout   = false;
    Blend        blend    = Blend::Opaque;
    std::size_t  start    = 0;   // index into Geometry::corners
    std::size_t  count    = 0;
};

struct Geometry {
    std::vector<Corner> corners;   // three a triangle, batches contiguous
    std::vector<Batch>  batches;
    // 1 where the corner came from a mesh carrying the MIRROR flag 0x100000,
    // parallel to `corners`. Kept beside them rather than inside `Corner`
    // because `Corner` is the GPU vertex and this is not a vertex attribute -
    // it selects which corners the mirror pass must draw separately.
    std::vector<std::uint8_t> cornerMirror;
    // Which MESH's transform applies to each corner, in `.3DO` file order,
    // parallel to `corners`. Not always the mesh that declared the face: a
    // negative vertex index is SKINNED to an ancestor, and such a corner is
    // built from that ancestor's vertex and offset, so it is the ANCESTOR that
    // must pose it. 9405 corners across the shipped character models are
    // skinned this way, and posing them by their declaring mesh tears the
    // model open at the shoulders. A decor set has no use for it; a CHARACTER does, because
    // posing one means transforming each mesh by its own bone and the corners
    // are flattened into batches by material, not by mesh. Kept beside the
    // corners for the same reason `cornerMirror` is: it is not a vertex
    // attribute, it selects which corners a transform applies to.
    std::vector<std::int32_t> cornerMesh;
    // The GLOBAL vertex index each corner resolved to - the running sum of
    // every earlier mesh's vertex count plus the face's own index. Parallel to
    // `corners`, and there for the FACE MORPH: a `.3DM` frame supplies the
    // face mesh's vertices by their index in its own block, so a corner has to
    // be able to say which vertex it is.
    std::vector<std::int32_t> cornerVertex;
    // The mesh that DECLARED the face this corner belongs to, which differs
    // from `cornerMesh` exactly where the corner is skinned to an ancestor.
    // Kept so the wrong reading can be rendered beside the right one every
    // run - the intrinsic control this repo uses for the near-clip rule too.
    // Posing by this instead of by `cornerMesh` is what tore the shoulders.
    std::vector<std::int32_t> cornerDeclared;

    // WHICH VERSION of these corners this is.
    //
    // A backend may cache a `Geometry` - the Vulkan one keys a vertex buffer
    // on the POINTER - and that is right for a decor set, which is built once
    // and never touched again. It is wrong for a POSED CHARACTER: `applyPose`
    // rewrites the same object every frame, so the pointer is unchanged while
    // the vertices are not, and a pointer-keyed cache then draws the pose it
    // first saw for ever. That is exactly what happened: the software frame
    // showed the character at frame 105 of his line and the Vulkan frame
    // showed him at the frame the conversation opened, with the same camera,
    // the same submissions and the same 2409 corners.
    //
    // So the boundary says it out loud. Anything that mutates a `Geometry`
    // after a backend may have seen it must bump this, and a backend must
    // re-upload when it changes. `applyPose` does.
    std::uint64_t revision = 0;
};

// THE MIRROR PLANE - mesh flag 0x100000, and the engine reflects through it.
//
// `docs/ASSETS.md` 4c: exactly 6 of 12203 meshes carry the bit, at most one is
// live at a time (the engine keeps a single global, `dword_534F48`), and
// `sub_440D90` reflects the camera through this plane and calls the scene draw
// again. The point is the mesh's own position - `sub_440D90` reads mesh +36
// and adds the instance's, and +36 is `Mesh::pos`, the same field the
// visible-set walk takes its bounding sphere's centre from.
//
// **The NORMAL is a reconstruction and says so.** The engine reads a
// precomputed one out of a RUNTIME face structure (node +20 for a triangle,
// +24 for a quad, at float 4..6 and 5..7), and those offsets coincide with the
// unread tails of the on-disk 28- and 32-byte face records - but the disk
// floats there are **not unit** (94264 of 106768 sampled faces are off by more
// than 1e-3, with magnitudes ranging to 1e38), so they are not the normals and
// whatever fills the runtime field was not traced. What IS determined is the
// PLANE, which is a property of the face's geometry, so the normal here is the
// face's own cross product. That fixes it up to SIGN, and the sign matters -
// `sub_440D90` gives up when the camera is behind (`if (v11 > 0.0)`), so a
// flipped normal is a mirror that only works from inside the wall.
struct MirrorPlane {
    bool  found     = false;
    float point[3]  = {0, 0, 0};
    float normal[3] = {0, 0, 0};
    int   mesh      = -1;
};

MirrorPlane mirrorPlane(std::span<const std::byte> d);

// Build the drawable geometry of one .3DO.
//
// Batches come out in the engine's draw order: opaque first, then additive,
// then multiply, and by material id within each. That order is the engine's
// own - Render_FlushBuckets walks 0x4000 buckets ASCENDING, the transparent
// keys are 0x2100 and 0x2200, and the key's low six bits are the material's
// runtime texture slot, handed out in material-record order (ASSETS 4b).
Geometry buildGeometry(std::span<const std::byte> d, DrawFilter filter);

}  // namespace omk

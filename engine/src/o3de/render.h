// SPDX-License-Identifier: GPL-3.0-or-later
// The render DECISIONS - what is drawn, in what order, with what texture.
//
// Not a rasterizer. Everything upstream of turning a triangle into pixels is a
// decision the shipped data can be checked against, and all of it is a single
// 14-bit number: `Render_SubmitMesh` (0x004951C0) composes a flag word per mesh
// and adjusts it per face, `Render_FlushBuckets` (0x00460060) walks the 0x4000
// buckets in ASCENDING key order, and `Raster_DrawTriangles` (0x00460B80)
// binds `g_D3DTextures[key & 0x3F]`. So the key IS the draw order and the key
// IS the texture, and neither is stored anywhere.
//
// The rasterizer is deliberately not here: `RECONSTRUCTION` 3 drops the
// standard to "read and explained" at the pixel, and there is nothing to diff
// pixels against. Everything in this file has a corpus that can fail it.
#pragma once

#include "formats/mesh3do.h"

#include <cstdint>

namespace omk {

// ------------------------------------------------------------ what is drawn
//
// One test, and it replaces the three separate heuristics the viewers grew.
// Every call to `Render_SubmitMesh` is guarded by it:
//
//     test dword ptr [eax], 800043h   ; +0 is the mesh flags
//     jnz  skip
//
// 0x1 | 0x2 | 0x40 | 0x800000. It disagrees with the viewers in BOTH
// directions, which is why it is worth stating as one thing: bit 0 alone
// accounts for every skipped PERSOS mesh, and 3 decor meshes the viewers draw
// the engine does not.
inline constexpr std::uint32_t kNotDrawableMask = 0x800043u;

inline bool meshDrawable(std::uint32_t flags) {
    return (flags & kNotDrawableMask) == 0;
}

// ------------------------------------------------------------ the state bits
//
// Composed ONCE per mesh, in this order. Note the first line: it is a
// `mov esi, 800h`, an assignment, not an `or` - flag 0x10000 WIPES every state
// bit set before it. Written out as a table of independent bits it would be
// wrong, and it looks like a table.
//
// `envmap` is `hw_texturing()`: the 0x40 bit is an environment map on hardware,
// not "this mesh is textured".
inline std::uint16_t meshStateBits(std::uint32_t flags, bool envmap = true) {
    std::uint16_t state = 0;
    if (flags & 0x10000u) state  = 0x800;              // assignment, not |=
    if (flags & 0x800u)   state |= 0x400;
    if (flags & 0x8000u)  state |= 0x400;
    if (flags & 0x1000u)  state |= 0x2000;
    if (flags & 0x2000u)  state |= 0x100;
    if (flags & 0x4000u)  state |= 0x200;
    if (envmap && (flags & 0x4000000u)) state |= 0x40;
    return state;
}

// Every state bit `Render_SubmitMesh` can set. Its intersection with the six
// bits the texture slot occupies must be EMPTY, or a mesh's state would
// silently rename its texture - which is the invariant that says the two
// halves of the key really are independent.
inline constexpr std::uint16_t kStateBits =
    0x800 | 0x400 | 0x2000 | 0x100 | 0x200 | 0x40 | 0x80 | 0x1000;
inline constexpr std::uint16_t kTextureSlotMask = 0x3F;

// ---------------------------------------------------------------- the key
//
// `maxz` is the largest of the face's transformed vertices' +16, so the two
// depth bits belong to the FACE, not the mesh - which is why they cancel
// between two coincident faces and can never break a tie between them.
inline std::uint16_t bucketKey(std::uint16_t state, std::uint16_t textureSlot,
                               float maxz, float nearSplit, float farSplit) {
    std::uint16_t key = static_cast<std::uint16_t>(state | (textureSlot & kTextureSlotMask));
    if (maxz < nearSplit) key |= 0x80;
    if (maxz >= farSplit && !(key & 0x800)) key |= 0x1000;
    return static_cast<std::uint16_t>(key & 0x3FFF);
}

inline constexpr int kBuckets = 0x4000;

// ------------------------------------------------------------- transparency
//
// Two blend modes, not one flat 50%. No mesh asks for either without 0x1000,
// and the `SetRenderState(27, 1)` an earlier reading pointed at is the CUTOUT
// path from flag 0x800 - a different thing again.
enum class BlendMode { Opaque, Cutout, Additive, Multiply };

inline BlendMode meshBlend(std::uint32_t flags) {
    if (flags & 0x1000u) {
        if (flags & 0x2000u) return BlendMode::Additive;
        if (flags & 0x4000u) return BlendMode::Multiply;
    }
    if (flags & 0x800u) return BlendMode::Cutout;
    return BlendMode::Opaque;
}

// -------------------------------------------------------- the visible set
//
// `sub_48D3B0` walks the scene's FLAT node list - `for (n = scene[1]; n; n =
// n->next)`, no portal walk and no PVS; the `.3DO`'s door records are not
// consulted - and applies three rejects in order, cheapest first.
//
// The order is the engine's and is kept: a flag test, then one distance
// compare, then four dot products. Reordering them changes nothing about what
// survives, but it is the shape of the original and the cheap tests really do
// remove most of the work.
struct Frustum {
    float eye[3] = {0, 0, 0};
    float farDist = 0.0f;
    // The four SIDE planes, as (normal, d) with `n.c + d <= r` meaning "inside
    // or straddling". Only four: there is no near or far plane here - distance
    // is the sphere test above, and nothing clips the near side.
    struct Plane { float n[3]; float d; };
    Plane side[4];
};

// The ENGINE's construction, read out of `sub_48D0D0` (the function directly
// before the visible-set walk).
//
// It does not build planes from angles at all. It takes the four corners of
// the view rectangle **at the far plane**, transforms them to world space, and
// hands the camera position plus two adjacent corners to `sub_442FB0` four
// times - a plane through three points, written into `flt_660AD4 …
// flt_660B18` struct-of-arrays, which is why the walk reads that range as one
// block.
//
// In camera space the corners are
//
//     (+-far * width  / (2 * projX),  +-far * height / (2 * projY),  far)
//
// where `projX`/`projY` are the camera's `+0xE4`/`+0xE8` and the viewport size
// is the uint16 pair at `+0x19C`/`+0x19E`. **The two axes are scaled
// independently** - there is no aspect-ratio term anywhere, which is what an
// earlier fixture here got wrong by deriving the vertical half-angle from the
// horizontal one.
//
// `far` is `dword_6A2B9C`, copied from the camera's `+0x154`; the near plane
// is `flt_6A2BBC` from `+0x144`. Both are confirmed by the range test that
// rejects a vertex with `z <= near` or `z >= far`.
Frustum frustumFromCamera(const float eye[3], const float at[3],
                          float projX, float projY,
                          int width, int height, float farDist);

// A convenience for callers that have an angle rather than a projection
// scale: `tan(halfH) = width / (2 * projX)` is the relation the above
// implies, so this inverts it. Same construction, different way in.
Frustum frustumFromFov(const float eye[3], const float at[3],
                       float fovDegrees, int width, int height, float farDist);

// A plane through three points, as `sub_442FB0` builds one. Kept separate
// from the fixture above because THIS is the engine's, and the four calls that
// use it are what a full port needs next.
struct Plane4 { float n[3]; float d; };
Plane4 planeThroughPoints(const float p1[3], const float p2[3],
                          const float p3[3]);

enum class CullResult { Hidden, TooFar, OffSide, Visible };

// The three rejects, in the engine's order.
//
//   1. `if (meshdef->flags & 0x40) continue;`     the hidden bit
//   2. `(r + far)^2 > |c - eye|^2`                keep when true
//   3. `n.c + d <= r` on all four side planes     keep when true
//
// Note the second is a KEEP test written with the radius added to the far
// distance, not a reject with it subtracted - so a mesh straddling the far
// distance survives, which is what stops a large set popping.
CullResult cullMesh(const Mesh& m, const Frustum& f);

inline bool meshVisible(const Mesh& m, const Frustum& f) {
    return cullMesh(m, f) == CullResult::Visible;
}

}  // namespace omk

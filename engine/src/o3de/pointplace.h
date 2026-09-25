// SPDX-License-Identifier: GPL-3.0-or-later
// THE POINT PLACEMENT - a moving set mesh's corners and collision triangles,
// put where its script moves them, every frame (`play.cpp`, "scripted motion:
// meshes placed"). 2730 triangles and the render corners with them on
// Anekbah's street; 9.6 ms of a console's city frame (2026-09-23).
//
// Each point goes through the same three steps the mesh patch always did:
//
//     local = (in - origin) * scale
//     r     = rotated ? qrot(q, local) : local
//     out   = r + at
//
// in ONE place, with TWO implementations (the reader's rule, 2026-09-25: an
// ARM-specific path is allowed beside a generic one):
//
//   * the GENERIC loop, plain C++, everywhere;
//   * a NEON loop wherever the compiler offers `__ARM_NEON` - the Vita's
//     Cortex-A9 and Apple silicon alike - four points at a time.
//
// **They must agree bit for bit.** The NEON loop does the generic loop's
// operations in its order - separate multiplies and adds, never a fused
// multiply-add - and the generic loop is compiled with contraction OFF so the
// compiler cannot fuse it behind our back (the Vita build is already
// `-ffp-contract=off`; Apple clang fuses by default on arm64). The A9 build
// does emit VFP `vmla.f32` for the generic loop - a CHAINED multiply-add, the
// product rounded before the add, so the same bits as the two operations; only
// `vfma` fuses, and none is emitted (checked with objdump). One difference
// is the hardware's and is MEASURED rather than assumed: the A9's NEON unit
// flushes denormals to zero where its VFP does not. `vita_bench`'s `place`
// stage hashes both paths' output on the device and says EXACT or DIFFERENT.
#pragma once

#include "actor/pose.h"

#include <cstddef>
#include <cstdint>

namespace omk {

struct PointPlace {
    float origin[3] = {0, 0, 0};   // the mesh's authored position
    float scale[3]  = {1, 1, 1};
    Quatf q;                        // used only when `rotated`
    bool  rotated   = false;
    float at[3]     = {0, 0, 0};    // where the origin goes
};

// Place `n` points: point `idx[i]` is read at `in + idx[i] * inStride` and
// written, x y z, at `out + idx[i] * outStride` (strides in floats; `in` and
// `out` may be different arrays of the same layout, and must not overlap
// unless they are the same array with the same stride).
void placePoints(const PointPlace& p, const float* in, std::size_t inStride,
                 float* out, std::size_t outStride,
                 const std::uint32_t* idx, std::size_t n);

// The same with the implementation chosen by the caller - for the bench and
// the check that compares the two. `neon` is ignored where there is no NEON.
void placePointsGeneric(const PointPlace& p, const float* in, std::size_t inStride,
                        float* out, std::size_t outStride,
                        const std::uint32_t* idx, std::size_t n);
void placePointsNeon(const PointPlace& p, const float* in, std::size_t inStride,
                     float* out, std::size_t outStride,
                     const std::uint32_t* idx, std::size_t n);
bool pointPlaceHasNeon();

}  // namespace omk

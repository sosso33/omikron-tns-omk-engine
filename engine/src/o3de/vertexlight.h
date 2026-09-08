// SPDX-License-Identifier: GPL-3.0-or-later
// THE DYNAMIC LIGHTING - `sub_493E40`, transcribed.
//
// A decor set's `.3DO` carries a table of lights (`formats/light3do.h`) and
// the street's moving population receives them: `sub_4380B0` ("LightInstance")
// registers a walker, a vehicle or the player's ride in the same spatial
// structure the set's lights went into, and `sub_48D7F0` queries it per mesh
// per frame, calling `sub_493E40` for each overlapping light just before
// `Render_SubmitMesh`. The static set needs none of it - it is shaded by a
// colour baked into every vertex. `todo/mesh-lights.md`.
//
// The engine's arithmetic, which this file follows exactly:
//
//     d = mesh.pos - light.pos
//     if |d|^2 > light.radiusA^2:  the light does not reach; done
//     k  = light.intensity * 256
//     k *= 1 - (|d| - radiusB) / (radiusA - radiusB)     LINEAR falloff
//     L  = light.direction * k                (the direction is
//                                              normalize(centre - pos), which
//                                              the loader writes at +112)
//     per vertex:  t = -(N . L)
//     if t > 0:  channel += ramp[colourByte][t]          saturating
//
// **The two tables are built in code and are transcribed rather than guessed**
// (`sub_493F70`): `byte_6A2CE0[j] = j` sits between 256 zeros and 256 0xFFs,
// so indexing it with a sum is a SATURATING CLAMP to 0..255; and the ramp at
// `unk_660BA8` is filled by accumulating the row index, which makes
// `ramp[c][t] == (t * c) >> 8`. So the colour modulation is a shift, not a
// multiply - what a 1999 engine does instead of a per-channel divide.
//
// ## What this port does differently, and it is one thing
//
// The engine tests the reach and computes `k` **per MESH**. A character is
// many meshes (one a bone) within a metre of each other, and this port's posed
// geometry is flattened into batches by material, so it tests **per BODY** -
// one distance for the whole figure. The per-vertex half, which is what you
// see, is unchanged. On a 20 m median radius the difference is below the
// quantisation of a colour byte for anything the size of a person; it would
// matter for a long object, and the vehicles are the case to watch.
#pragma once

#include "formats/light3do.h"
#include "o3de/geom3do.h"

#include <cstdint>
#include <span>

namespace omk {

// `ramp[c][t] = (t * c) >> 8`, the engine's own table, as a function.
inline std::uint8_t lightRamp(std::uint8_t colour, int t) {
    if (t < 0) t = 0;
    if (t > 255) t = 255;
    return static_cast<std::uint8_t>((t * colour) >> 8);
}

// Add every light that reaches `bodyPos` into `g`'s corner colours, from
// `first` for `count` corners. The corners must already be POSED into world
// space and their normals rotated with them.
//
// -> how many lights actually reached.
int applyLights(Geometry& g, std::size_t first, std::size_t count,
                const float bodyPos[3], std::span<const Light3do> lights);

// The light that reaches `p` most strongly, on exactly `applyLights`'s own
// rules - the squared-radius reach test, the degenerate-pair refusal, and the
// linear falloff from the inner radius to the outer, clamped. -> nullptr when
// none reaches.
//
// This is what the MAPPED shadow (`todo/enhancements.md` 6) takes its
// direction from, so that a real shadow is cast by a light the set's own
// author placed rather than by a sun this port invented.
// `k` receives the falloff-weighted strength the pick was made on, so a
// caller comparing across two resident sets compares the same number. Doing
// that on the raw intensity instead is what made a distant fill light beat the
// street lamp the character was standing under.
const Light3do* strongestLightAt(const float p[3], std::span<const Light3do> lights,
                                 float* k = nullptr);

}  // namespace omk

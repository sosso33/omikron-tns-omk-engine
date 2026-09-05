// SPDX-License-Identifier: GPL-3.0-or-later
// The `.3DO` LIGHT record - 304 bytes, and the record names itself.
//
// `todo/mesh-lights.md`. The table is the last thing in a `.3DO`, at the
// header's `+40`, with the count at `desc+240` (NOT `desc+232`, which
// `Read3DO_Init` overwrites from +240 before using - see `mesh3do.h`).
//
// Every one of the 4179 shipped records opens with the tag `LIGH` and a name
// beginning `LIGHT`, which is what settles that these are lights at all rather
// than a plausible reading of some other table.
//
// ## The layout
//
//     +0    u32       flags. Low byte 2 or 0x12, bit 0x40000000 set or not -
//                     exactly four combinations across the corpus. At runtime
//                     `sub_493CE0` ORs in 8 to mark the record transformed.
//     +4    char[12]  the NAME: `LIGHT`, `LIGHT0`, `LIGHT1`, ... 573 distinct
//     +16   u32 x2    zero in all 4179
//     +24   float     radius A - 1.0 m minimum, 20.0 m median
//     +28   float     radius B - 10.0 m median, always <= A in the corpus
//     +32   float     median 1.5, max 25 - an intensity or falloff. OPEN
//     +36   float     median 164     OPEN
//     +40   float     median 169     OPEN
//     +44   u32       the COLOUR, 0x00RRGGBB. 420 distinct: white, warm
//                     orange, cyan, red - a neon city's palette
//     +48   float[3]  the light's POSITION
//     +60   float[3]  RUNTIME: the position through the node's matrix
//                     (`sub_493CE0` writes a2[15..17]). Zero on disk, 4179/4179
//     +72   float[3]  zero on disk
//     +80   float[3]  the CENTRE of the footprint below
//     +92   float[5]  zero on disk
//     +112  float[3]  footprint corner 1 on disk - and the slot the runtime
//                     REUSES for the light's DIRECTION: `sub_493C30` writes
//                     `normalize(centre - position)` here at load
//     +124  float[3]  RUNTIME: that direction through the node's 3x3
//                     (`sub_493CE0` writes a2[31..33]). Zero on disk
//     +144  float[3]  footprint corner 2
//     +176  float[3]  footprint corner 3
//     +208  float[3]  footprint corner 4
//     +240  float     radius A squared - `sub_493C30` computes it at load
//     +244  float     radius B squared - likewise. Zero on disk
//     +248  ...       zero on disk
//
// The four corners are a QUAD - the lit footprint as authored - and the centre
// at +80 lies inside their bounding box in **4178 of 4179** records, which is
// the geometric invariant the parse can fail. It is only approximately their
// centroid (within 5% of the span in 4012), so the authoring rounded it.
//
// ## What is NOT established
//
// Three floats (+32, +36, +40) have no traced consumer. And the light is only
// ABOVE its footprint in 2871 of 4179, so "a spot shining down" is the common
// case and not the rule - wall and up lights are in here too.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

struct Mesh3doHeader;

inline constexpr std::size_t kLightRecord = 304;

struct Light3do {
    std::uint32_t flags = 0;
    std::string   name;
    float         radiusA = 0.0f, radiusB = 0.0f;
    float         f32 = 0.0f, f36 = 0.0f, f40 = 0.0f;   // no traced consumer
    std::uint32_t colour = 0;                            // 0x00RRGGBB
    float         pos[3] = {0, 0, 0};
    float         centre[3] = {0, 0, 0};
    float         corner[4][3] = {};
};

// Every light of a model, in file order. Empty when the header has none.
std::vector<Light3do> readLights(std::span<const std::byte> d, const Mesh3doHeader& h);

}  // namespace omk

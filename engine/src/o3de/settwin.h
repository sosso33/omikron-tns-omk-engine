// SPDX-License-Identifier: GPL-3.0-or-later
// A DOOR'S TWIN: the same faces in both resident sets.
//
// A city's set and the interior a door leads to each carry their own copy of
// the door's leaves - `Ported26` is in `Anekbah.3DO` and in `Aresto.3DO`, at
// the same place, moved by the same path while both sets are shown - and the
// copies are lit differently, because each was baked with its own set's
// lights. Their corners differ only by the exporter's rounding, about 0.001
// (`tools/door_twins.cpp`, docs/ASSETS.md 4b).
//
// The engine's strict test on its quantised z-buffer keeps the FIRST drawn, so
// the later copy shows nothing. A GPU's depth is not coarse enough to call them
// equal, and the rounding decides pixel by pixel: the dashes on the door that
// come and go as the camera creeps. The depth tie in `depthtie.h` cannot see
// this, since it works inside one geometry.
//
// So the decision is taken once, where it is decidable for every backend: of a
// pair of faces lying on the same corners, the one drawn later can never win a
// pixel, and is not submitted.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace omk {

// How far apart two corners may lie and still be one corner: 1 mm in the game's
// units. Over Anekbah's interiors (AToit, the rooftops, apart) 0.005 already ties
// 926 faces and this 931, the last few `Porteg40`'s at 0.05; at 0.5 it is 940,
// and those are faces the two sets really do differ on (`Porteg30` has a corner
// 0.8 off), which are no tie: both are simply drawn.
inline constexpr float kTwinCornerApart = 0.1f;

struct TwinFace {
    std::array<float, 9> xyz{};   // the three corners at rest
    std::uint32_t order = 0;      // the draw's sort key: the lower is drawn first
    bool opaque = false;          // only a face that writes depth can claim a pixel
};

// Which faces of each mesh are never seen, one byte a face. `a` belongs to the
// slot drawn first, so it keeps a pixel when the two orders are equal (the draw
// sort is stable). A face matches from any of its three corners, because two
// sets number a door's faces differently, but never reversed: the other winding
// is the face's other side.
struct TwinVerdict {
    std::vector<std::uint8_t> loseA, loseB;
    int tied = 0;
};
TwinVerdict settleTwinFaces(std::span<const TwinFace> a, std::span<const TwinFace> b,
                            float apart = kTwinCornerApart);

}  // namespace omk

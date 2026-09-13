// SPDX-License-Identifier: GPL-3.0-or-later
// THE DEPTH TIE, settled at SUBMIT - the decision, out of the Vulkan backend.
//
// Two faces on the same positions - the two SIDES of a shop sign, 18 pairs in
// Anekbah - are both submitted (CULLMODE = NONE), and the engine's strict test
// on a quantised z-buffer keeps the FIRST drawn on every pixel. A GPU compare
// cannot read the buffer, so the Vulkan backend settles the tie where it is
// decidable exactly: a face whose position set an earlier DEPTH-WRITING face of
// the same geometry already claimed, in draw order, can never win a pixel from
// it, and is degenerated in the vertex buffer (`vkrender.cpp` has that half and
// the full reading; `raster.cpp`'s `kDepthTie` is the software reference).
//
// This class is only the decision, moved here on 2026-09-13 so it can be tested
// without a GPU and made cheaper (todo/optimization.md step 3). It is held to
// the answer the in-backend version gave, draw for draw:
//
//   * the state resets when the geometry's REVISION or triangle count changes -
//     every pose, every moved door - exactly as before;
//   * faces are walked in the order the draws arrive, and a consecutive pair
//     `buildGeometry` emits as (0,1,2)(0,2,3) is keyed as one quad, the same;
//   * a key already claimed makes a LOSER, otherwise a depth-writing draw
//     claims it - the same two decisions in the same order.
//
// What changed is only how it is stored and one shortcut that cannot change a
// decision: hashed sets that keep their buckets across revisions instead of
// ordered sets rebuilt from nothing, and a draw that writes no depth while
// nothing has been claimed yet just marks its range handled - no face in it can
// lose, and none can be claimed. `engine/tools/tie_equiv.cpp` keeps the old code
// verbatim and compares the two (`verify.py: engine: tie equivalence`).
#pragma once

#include "o3de/geom3do.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace omk {

class DepthTie {
public:
    // Append to `losers` the triangles of the draw [start, start + count) that an
    // earlier depth-writing face of this geometry already claimed.
    void resolve(const Geometry& g, std::size_t start, std::size_t count, bool writes,
                 std::vector<std::size_t>& losers);

    // The backend's bookkeeping, reset with the state as it always was, except
    // `logged`, which survives a revision so the report prints once.
    long dropped = 0;
    bool touched = false;
    bool logged  = false;

private:
    template <std::size_t N>
    struct KeyHash {
        std::size_t operator()(const std::array<std::uint32_t, N>& k) const noexcept {
            std::uint64_t h = 0x9E3779B97F4A7C15ull;
            for (std::uint32_t v : k) {
                h ^= v;
                h *= 0xFF51AFD7ED558CCDull;
                h ^= h >> 32;
            }
            return static_cast<std::size_t>(h);
        }
    };
    std::uint64_t revision_ = 0;
    std::vector<std::uint8_t> done_;   // per triangle, this revision
    std::unordered_set<std::array<std::uint32_t, 12>, KeyHash<12>> quads_;   // 4 sorted positions
    std::unordered_set<std::array<std::uint32_t, 9>, KeyHash<9>>   tris_;    // 3 sorted positions
};

}  // namespace omk

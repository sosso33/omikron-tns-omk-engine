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
// without a GPU and made cheaper (todo/optimization.md steps 3 and 7b). It is
// held to the answer the in-backend version gave, draw for draw:
//
//   * the state resets when the geometry's REVISION or triangle count changes -
//     every pose, every moved door - exactly as before;
//   * faces are walked in the order the draws arrive, and a consecutive pair
//     `buildGeometry` emits as (0,1,2)(0,2,3) is keyed as one quad, the same;
//   * a key already claimed makes a LOSER, otherwise a depth-writing draw
//     claims it - the same two decisions in the same order;
//   * a KEY is the multiset of the face's corner positions, bit for bit - what
//     the old sorted-array key meant.
//
// What changed is only how the claimed keys are stored, and one shortcut that
// cannot change a decision. Step 3 moved them from ordered sets to hashed ones;
// step 7b (a set's revision changes EVERY frame while its cargo moves, so the
// whole city is re-keyed every frame) stores them flat: each claimed face's
// positions copied into a reused array, found through an open-addressed table
// keyed by an ORDER-FREE hash of the positions, reset by bumping a generation
// rather than freeing anything - no node allocated or freed per face, no sort
// per face (the positions are sorted and compared only on a hash match). And a
// draw that writes no depth while nothing has been claimed yet just marks its
// range handled - no face in it can lose, and none can be claimed.
// `engine/tools/tie_equiv.cpp` keeps the original pass verbatim and compares
// (`verify.py: engine: tie equivalence`).
#pragma once

#include "o3de/geom3do.h"

#include <array>
#include <cstddef>
#include <cstdint>
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
    // One kind of key (a triangle's 3 positions or a quad's 4): the claimed keys'
    // positions, flat, and the table that finds them.
    struct Claimed {
        int corners = 3;                          // positions per key
        std::vector<std::uint32_t> keys;          // corners*3 position words a key, append-only per revision
        std::vector<std::uint64_t> cell;          // generation 16 | hash fingerprint 16 | key number + 1 (32)
        std::uint64_t mask = 0;
        std::uint32_t gen = 0;
        std::size_t count = 0;                    // keys claimed this revision
        void reset(std::size_t capacityFor);
        bool empty() const { return count == 0; }
    };
    std::uint64_t revision_ = 0;
    std::vector<std::uint8_t> done_;   // per triangle, this revision
    Claimed quads_{4, {}, {}, 0, 0, 0};
    Claimed tris_{3, {}, {}, 0, 0, 0};
};

}  // namespace omk

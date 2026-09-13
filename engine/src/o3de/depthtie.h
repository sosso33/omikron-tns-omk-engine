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
// without a GPU and made cheaper (todo/optimization.md steps 3, 7b and 8). It is
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
// Step 3 moved the claimed keys from ordered sets to hashed ones; step 7b
// stores them flat (an open-addressed table over an ORDER-FREE hash of the
// positions, reset by a generation, the exact multiset compare only on a
// fingerprint match). And a draw that writes no depth while nothing has been
// claimed yet just marks its range handled - no face in it can lose.
//
// STEP 8: A REVISION THAT SAYS WHICH CORNERS MOVED. A set whose cargo moves gets
// a new revision every frame, and the walk above re-keys all 46415 of
// Anekbah's faces for a few hundred that moved. When the geometry says which
// corners changed (`Geometry::dirtyCorners`), the walk is kept instead - every
// face it visited, in order, with its draw, and the faces sharing each key
// chained in walk order - and the next revision REPLAYS it: the moved faces
// leave their keys' chains and join their new ones, only the chains they
// touched are re-decided, and each draw's losers are read back. That is the
// same answer by construction, because a face's decision depends only on the
// earlier faces sharing its key, and it is only taken while it is provably so:
//
//   * the draws must arrive exactly as logged (start, count, writes); the
//     first that does not throws the replay away, the log's prefix is walked
//     afresh, and the walk continues as before - a mesh crossing the clip
//     distance does this;
//   * a moved face must still pair (or not) into a quad exactly as it did,
//     since that is a positional test; if any pairing would flip, the revision
//     is walked afresh;
//   * the previous revision's log must be complete, and `dirtyFrom` must be
//     the revision it was taken for.
//
// And because a replay no longer re-uploads the whole vertex buffer, the class
// also keeps which triangles it has told the backend to degenerate: `restore`
// names those that are no longer losers, and `vboReplaced()` is how the backend
// says a full upload has put every corner back.
//
// `engine/tools/tie_equiv.cpp` keeps the original pass verbatim and compares,
// losers and a simulated vertex buffer both (`verify.py: engine: tie equivalence`).
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
    // earlier depth-writing face of this geometry already claimed - to be
    // degenerated - and to `restore` triangles degenerated earlier that must be
    // written back from `g.corners` first.
    void resolve(const Geometry& g, std::size_t start, std::size_t count, bool writes,
                 std::vector<std::size_t>& losers, std::vector<std::size_t>& restore);

    // The backend uploaded every corner again: nothing is degenerated any more.
    void vboReplaced();

    // The backend's bookkeeping, reset with the state as it always was, except
    // `logged`, which survives a revision so the report prints once.
    long dropped = 0;
    bool touched = false;
    bool logged  = false;
    // Revisions answered by a replay, by a full walk of a tracked geometry, and
    // replays abandoned part way through a revision.
    long replays = 0, walks = 0, fallbacks = 0;

private:
    using P = std::array<std::uint32_t, 3>;

    // One kind of key (a triangle's 3 positions or a quad's 4): the claimed keys'
    // positions, flat, and the table that finds them. The untracked walk's.
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

    // What the backend has degenerated, per triangle, and the list of them.
    std::vector<std::uint8_t> applied_;
    std::vector<std::uint32_t> appliedList_;
    std::size_t appliedCount_ = 0;

    // THE LOG of a tracked geometry's walk. A unit is one face visited - a
    // triangle or a quad - and its number is its place in the walk.
    struct Unit {
        std::uint32_t tri = 0, call = 0;
        std::uint32_t next = 0;        // the next unit sharing this key, in walk order
        std::uint32_t tail = 0;        // for a chain's head: its last unit
        std::uint8_t quad = 0;
        std::uint8_t cand = 0;         // a single triangle whose successor was open to pair with it
        std::uint8_t writes = 0, loser = 0;
        std::uint8_t head = 0, hasWriter = 0;
    };
    struct Call {
        std::size_t start = 0, count = 0;
        bool writes = false;
        std::vector<std::uint32_t> losers;   // unit numbers, ascending
    };
    bool tracked_ = false, replaying_ = false, anyWriter_ = false;
    std::size_t cursor_ = 0, ncalls_ = 0;
    std::vector<Unit> units_;
    std::vector<std::uint64_t> unitHash_;
    std::vector<std::uint32_t> unitOfTri_;
    std::vector<Call> calls_;
    // key -> the head unit of its chain; generation 16 | fingerprint 16 | head + 1
    // (0 = a deleted cell)
    std::vector<std::uint64_t> table_;
    std::uint64_t tmask_ = 0;
    std::uint32_t tgen_ = 0;
    std::size_t tombs_ = 0;
    // replay scratch
    std::uint32_t stamp_ = 0;
    std::vector<std::uint32_t> stampTri_, stampUnit_, stampChg_;
    std::vector<std::uint8_t> orig_;
    std::vector<std::uint32_t> affected_, changed_;
    std::vector<std::uint64_t> marked_;
    std::vector<std::array<std::size_t, 3>> prefix_;
    std::vector<std::size_t> scratch_;

    void resetFor(const Geometry& g, std::size_t ntri, std::vector<std::size_t>& restore);
    void resetTracked(std::size_t ntri);
    void walkFlat(const Geometry& g, std::size_t start, std::size_t count, bool writes,
                  std::vector<std::size_t>& losers);
    void walkTracked(const Geometry& g, std::size_t start, std::size_t count, bool writes,
                     std::vector<std::size_t>& losers);
    bool prepareReplay(const Geometry& g, std::size_t ntri, std::vector<std::size_t>& restore);
    void fallback(const Geometry& g, std::vector<std::size_t>& restore);
    void markApplied(const std::vector<std::size_t>& losers, std::size_t from);

    void tableReset(std::size_t units);
    void tableRebuild();
    void setCell(std::size_t slot, std::uint64_t h, std::uint32_t head);
    std::uint32_t findHead(const Geometry& g, std::uint64_t h, const P* ps, int n, std::size_t& slot) const;
    void unlinkUnit(std::uint32_t u);
    void linkUnit(const Geometry& g, std::uint32_t u);
    void recomputeMarked();
    void recomputeChain(std::uint32_t head);
    void noteChange(std::uint32_t u);
};

}  // namespace omk

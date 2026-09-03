// SPDX-License-Identifier: GPL-3.0-or-later
//
// The crowd index must REJECT a non-finite entry.
//
// omk-play 61: walking out of the Impasse into the city gave a black screen
// with the walker-bump message. The chain was one comparison: a pedestrian's
// body went non-finite, and the index's reach test was written
// `if (e.reach + myReach < m) continue;` - which is FALSE when `m` is NaN, so
// the entry was not skipped but ACCEPTED. Its push came back NaN, `nudge`
// wrote it into the player's position, and the follow camera then had no eye
// to look from: the frame goes black while the bump message plays.
//
// Written as `!(m <= e.reach + myReach)` the NaN falls out instead. Every
// shipped value is finite, so nothing the engine keeps is rejected - this
// only decides what happens to a value the engine could never produce.
#include "actor/spatial.h"

#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    using namespace omk;
    std::vector<CollisionSphere> mine(1);
    mine[0].pos[0] = mine[0].pos[1] = mine[0].pos[2] = 0.0f;
    mine[0].radius = 20.0f;
    std::vector<CollisionSphere> theirs = mine;

    SpatialIndex ix;
    const int slot = ix.add(0, 0, 20.0f, &theirs);

    const float me[3] = {0.0f, 0.0f, 0.0f};
    float out[3] = {0, 0, 0};

    // 1. a FINITE neighbour right on top of me is still found and still pushes
    const float near[3] = {5.0f, 0.0f, 0.0f};
    ix.update(slot, near, 0.0f);
    const bool hit = ix.query(mine, 20.0f, me, 0.0f, out, -1);
    const bool pushFinite = std::isfinite(out[0]) && std::isfinite(out[2]);
    const bool pushed = hit && pushFinite && (out[0] != 0.0f || out[2] != 0.0f);

    // 2. the same neighbour with a non-finite Y - the shape the play report
    //    produced (x and z finite, y NaN) - must be skipped entirely
    const float bad[3] = {5.0f, std::nanf(""), 0.0f};
    ix.update(slot, bad, 0.0f);
    float out2[3] = {0, 0, 0};
    const bool hit2 = ix.query(mine, 20.0f, me, 0.0f, out2, -1);
    const bool clean = std::isfinite(out2[0]) && std::isfinite(out2[1]) && std::isfinite(out2[2]);

    std::printf("finite neighbour: hit %d, push %.3f %.3f (finite %d, moved %d)\n",
                hit ? 1 : 0, out[0], out[2], pushFinite ? 1 : 0, pushed ? 1 : 0);
    std::printf("non-finite neighbour: hit %d (want 0), push %f %f %f (finite %d, want 1)\n",
                hit2 ? 1 : 0, out2[0], out2[1], out2[2], clean ? 1 : 0);

    const bool ok = pushed && !hit2 && clean;
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

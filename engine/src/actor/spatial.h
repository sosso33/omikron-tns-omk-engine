// SPDX-License-Identifier: GPL-3.0-or-later
// THE SPATIAL INDEX - what shoves the player out of the crowd
// (docs/STREET_LIFE.md 3). Read from `sub_45DFF0` / `sub_45E040` (register an
// actor node / an instance), `SpatialIndex_Update` (0x0045E110, the position),
// `SpatialIndex_Query` (0x0045E190, the walk over the entries within reach)
// and its two per-entry tests, `sub_45E390` (an actor entry: sphere against
// sphere) and `sub_45E690` (an instance entry: the player's spheres against
// an ELLIPSE two radii long along the instance's heading and one across).
//
// An entry is 20 bytes: the owner, a flag word (1 = an instance, 2 = touched
// this query), x y z. The query is made for ONE slot - `Player_SetActor`
// hands the player's node to `sub_45E140` - and `Actor_TickNpc` adds what it
// returns to the player's position before `Actor_ApplyMotion`, except in
// dialogue (state 16). The result is horizontal: both tests write y = 0.
//
// The spheres. Both tests read a list at the model's `+244` (count) and
// `+248` (16-byte records: x y z radius), turned by the node's matrix and
// carried to its position; this replica takes them as the meshes' bounding
// spheres (`Mesh::pos` + `Mesh::radius`, the volume `sub_48D3B0` culls
// against) of the model's FIRST skeleton - the loaded model struct is not the
// file and its list was not traced back to a writer, so this is labelled a
// reading of the shape rather than of the bytes. The reach test before either
// is `max(|dx|,|dy|,|dz|) <= entryRadius + myRadius`, both the model's `+88`.
//
// Two places the decompilation does not decide, both marked in the .cpp: in
// the sphere test, which of the two radii the penetration is measured
// against (`v16` is undefined in the listing - the horizontal overlap is
// taken); and `sub_45E690`'s vertical term, written `0.0 * 0.0`, which it
// takes at its word.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace omk {

struct CollisionSphere {
    float pos[3] = {0, 0, 0};    // in the model's frame
    float radius = 0.0f;
};

struct SpatialEntry {
    bool  live = false;
    int   owner = -1;            // whatever the caller keys it by
    int   kind = 0;              // 0 an actor node, 1 an instance (`sub_45E040`)
    float pos[3] = {0, 0, 0};    // +8/+12/+16
    float facing = 0.0f;         // degrees, the node's yaw
    float reach = 0.0f;          // the model's `+88`
    const std::vector<CollisionSphere>* spheres = nullptr;
    bool  touched = false;       // flag 2: the last query hit it
};

class SpatialIndex {
public:
    // `sub_45DFF0` / `sub_45E040`: the first free slot; -1 when full (the
    // engine's table is sized at boot; 512 here).
    int  add(int owner, int kind, float reach, const std::vector<CollisionSphere>* spheres);
    void remove(int slot);
    void clear();
    // `SpatialIndex_Update`
    void update(int slot, const float pos[3], float facing);

    // `SpatialIndex_Query`: the push for a body of `mine` spheres standing at
    // `pos` facing `facing` (its model radius `myReach`), summed over every
    // live entry within reach - `self` excluded - and each hit entry marked
    // touched. -> whether anything was hit. `out` is (x, 0, z).
    bool query(const std::vector<CollisionSphere>& mine, float myReach,
               const float pos[3], float facing, float out[3], int self = -1);
    // `sub_45DF30(slot)`: flag 2 of that entry after the last query.
    bool touched(int slot) const;
    // the entries the last query touched, in slot order
    const std::vector<int>& lastTouched() const { return touched_; }

    const std::vector<SpatialEntry>& entries() const { return entries_; }
    int liveCount() const;

    // The two per-entry tests, exposed for the probe: `mine` at `pos`/`facing`
    // against ONE entry; the push accumulates into `out`.
    static bool pushSphere(const std::vector<CollisionSphere>& mine, const float pos[3], float facing,
                           const SpatialEntry& e, float out[3]);      // sub_45E390
    static bool pushEllipse(const std::vector<CollisionSphere>& mine, const float pos[3], float facing,
                            const SpatialEntry& e, float out[3]);     // sub_45E690

private:
    std::vector<SpatialEntry> entries_;
    std::vector<int> touched_;
};

// The spheres a model contributes: every mesh under its first root (the first
// skeleton of a crowd model, the whole of a one-skeleton one).
struct Mesh;
std::vector<CollisionSphere> collisionSpheresOf(const std::vector<Mesh>& meshes);

// THE MODEL'S OWN SWEEP SPHERES - the list `Actor_Move` (0x00469580) reads
// for its swept capsule: `*(node+40)` is the model, `*model` its descriptor
// (file + 44), the count at descriptor +244 and 16-byte `(x, y, z, r)`
// records from +248 (the y is read at +252+16k, the radius at +260+16k).
// HO1_FN carries FOUR of radius 10.9 stacked up the body, so the capsule is
// 28 cm wide - not the metre-wide body sphere the per-mesh list above gives,
// which is the crowd push's and would jam him in every doorway. Empty when
// the count is not sane.
std::vector<CollisionSphere> modelSweepSpheres(std::span<const std::byte> model);

}  // namespace omk

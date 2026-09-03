// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/spatial.h"

#include "actor/player.h"        // rotateYaw
#include "formats/mesh3do.h"

#include <cmath>

namespace omk {
namespace {

constexpr std::size_t kSlots = 512;

void toWorld(const float local[3], float facing, const float at[3], float out[3]) {
    float r[3];
    rotateYaw(facing, local, r);
    for (int k = 0; k < 3; ++k) out[k] = r[k] + at[k];
}

}  // namespace

int SpatialIndex::add(int owner, int kind, float reach, const std::vector<CollisionSphere>* spheres) {
    if (entries_.size() < kSlots) entries_.resize(kSlots);
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].live) continue;
        SpatialEntry& e = entries_[i];
        e = SpatialEntry{};
        e.live = true; e.owner = owner; e.kind = kind; e.reach = reach; e.spheres = spheres;
        return static_cast<int>(i);
    }
    return -1;
}

void SpatialIndex::remove(int slot) {
    if (slot < 0 || static_cast<std::size_t>(slot) >= entries_.size()) return;
    entries_[static_cast<std::size_t>(slot)] = SpatialEntry{};
}

void SpatialIndex::clear() { entries_.clear(); touched_.clear(); }

void SpatialIndex::update(int slot, const float pos[3], float facing) {
    if (slot < 0 || static_cast<std::size_t>(slot) >= entries_.size()) return;
    SpatialEntry& e = entries_[static_cast<std::size_t>(slot)];
    for (int k = 0; k < 3; ++k) e.pos[k] = pos[k];
    e.facing = facing;
}

bool SpatialIndex::touched(int slot) const {
    if (slot < 0 || static_cast<std::size_t>(slot) >= entries_.size()) return false;
    return entries_[static_cast<std::size_t>(slot)].touched;
}

int SpatialIndex::liveCount() const {
    int n = 0;
    for (const auto& e : entries_) if (e.live) ++n;
    return n;
}

bool SpatialIndex::pushSphere(const std::vector<CollisionSphere>& mine, const float pos[3], float facing,
                              const SpatialEntry& e, float out[3]) {
    // `sub_45E390`: each of my spheres against each of the entry's, both
    // turned by their node's yaw and carried to its position; my centre
    // moves by every push found so far (`v36`/`v35`), so the records after it
    // test the already-pushed body
    bool hit = false;
    float accX = 0.0f, accZ = 0.0f;
    if (!e.spheres) return false;
    for (const auto& s : mine) {
        float c[3];
        toWorld(s.pos, facing, pos, c);
        c[0] += accX; c[2] += accZ;
        for (const auto& t : *e.spheres) {
            float w[3];
            toWorld(t.pos, e.facing, e.pos, w);
            const float dx = w[0] - c[0], dy = w[1] - c[1], dz = w[2] - c[2];
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (t.radius + s.radius < d) continue;
            hit = true;
            float nx = 0.0f, ny = 0.0f, nz = 0.0f;
            if (d > 0.0001f) { nx = dx / d; ny = dy / d; nz = dz / d; }
            // the overlap, projected horizontally - the listing's `v16`
            // (which radius the projection is measured against) is
            // undefined, so the overlap itself is taken
            const float pen = t.radius + s.radius - d;
            const float penH = std::sqrt(std::fabs(pen * pen - (pen * ny) * (pen * ny)));
            accX -= nx * penH; accZ -= nz * penH;
            c[0] -= nx * penH; c[2] -= nz * penH;
        }
    }
    out[0] += accX; out[1] = 0.0f; out[2] += accZ;
    return hit;
}

bool SpatialIndex::pushEllipse(const std::vector<CollisionSphere>& mine, const float pos[3], float facing,
                               const SpatialEntry& e, float out[3]) {
    // `sub_45E690`: an instance is an ellipse in the ground plane, centred on
    // its position, two radii long along its heading (`2r`, `v13`) and one
    // across, for every radius in its sphere list; a hit pushes my centre by
    // the whole penetration and the RESULT by a quarter of it (`* -0.25`)
    bool hit = false;
    float accX = 0.0f, accZ = 0.0f;
    if (!e.spheres) return false;
    const float local[3] = {0.0f, 0.0f, -1.0f};
    float h[3];
    rotateYaw(e.facing, local, h);              // `Matrix3x3_RotateVector(0, 0, -1, matrix)`
    for (const auto& s : mine) {
        float c[3];
        toWorld(s.pos, facing, pos, c);
        c[0] += accX; c[2] += accZ;
        const float rp2 = s.radius * s.radius;
        for (const auto& t : *e.spheres) {
            const float two = t.radius + t.radius;
            const float r2 = (two * 0.5f) * (two * 0.5f);
            const float dx = e.pos[0] - c[0], dz = e.pos[2] - c[2];
            const float d = std::sqrt(dx * dx + dz * dz);
            if (d <= 0.0f) continue;
            const float cosA = (-dx * h[0] + -dz * h[2]) / d;
            const float ell2 = (two * two - r2) * (cosA * cosA) + r2;
            if (d * d > rp2 + ell2) continue;
            hit = true;
            float nx = 0.0f, nz = 0.0f;
            if (d > 0.0001f) { nx = dx / d; nz = dz / d; }
            float px, pz;
            if (rp2 < ell2) {
                const float a = std::sqrt(std::fabs((d - s.radius) * (d - s.radius)));   // `- 0.0 * 0.0`
                const float pen = std::sqrt(ell2) - a;
                px = -(nx * pen); pz = -(nz * pen);
            } else {
                const float a = d - std::sqrt(ell2);
                const float pen = s.radius - std::sqrt(std::fabs(a * a));
                px = -(nx * pen); pz = -(nz * pen);
            }
            accX += px * 0.25f; accZ += pz * 0.25f;
            c[0] += px; c[2] += pz;
        }
    }
    out[0] += accX; out[1] = 0.0f; out[2] += accZ;
    return hit;
}

bool SpatialIndex::query(const std::vector<CollisionSphere>& mine, float myReach,
                         const float pos[3], float facing, float out[3], int self) {
    out[0] = out[1] = out[2] = 0.0f;
    touched_.clear();
    bool any = false;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        SpatialEntry& e = entries_[i];
        e.touched = false;
        if (!e.live || static_cast<int>(i) == self) continue;
        // A non-finite entry is skipped HERE, before anything reads it.
        // It cannot be left to the reach test below: that test maxes the
        // per-axis distances with `std::fmax`, which by definition RETURNS
        // THE OTHER OPERAND for a NaN, so a walker with a NaN y measures a
        // perfectly finite distance and is accepted. Its push then comes back
        // NaN, `PlayerController::nudge` writes it into the player, and the
        // follow camera has no eye to look from - a black frame, with the
        // bump message still playing (omk-play 61). Every shipped value is
        // finite, so this rejects nothing the engine keeps.
        if (!std::isfinite(e.pos[0]) || !std::isfinite(e.pos[1]) ||
            !std::isfinite(e.pos[2]) || !std::isfinite(e.facing)) continue;
        // `max(|dx|,|dy|,|dz|) <= reach + mine`
        float m = std::fabs(e.pos[0] - pos[0]);
        m = std::fmax(m, std::fabs(e.pos[1] - pos[1]));
        m = std::fmax(m, std::fabs(e.pos[2] - pos[2]));
        if (e.reach + myReach < m) continue;
        const bool hit = e.kind == 1 ? pushEllipse(mine, pos, facing, e, out)
                                     : pushSphere(mine, pos, facing, e, out);
        if (hit) { e.touched = true; touched_.push_back(static_cast<int>(i)); any = true; }
    }
    return any;
}

std::vector<CollisionSphere> collisionSpheresOf(const std::vector<Mesh>& meshes) {
    std::vector<CollisionSphere> out;
    // the first root, and everything under it
    int root = -1;
    for (std::size_t i = 0; i < meshes.size() && root < 0; ++i) {
        bool hasParent = false;
        for (const auto& p : meshes) if (p.id == meshes[i].parent) { hasParent = true; break; }
        if (!hasParent) root = static_cast<int>(i);
    }
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        int m = static_cast<int>(i);
        bool under = false;
        for (int guard = 0; guard < 64 && m >= 0; ++guard) {
            if (m == root) { under = true; break; }
            const std::int32_t pid = meshes[static_cast<std::size_t>(m)].parent;
            int next = -1;
            for (std::size_t j = 0; j < meshes.size(); ++j) if (meshes[j].id == pid) { next = static_cast<int>(j); break; }
            m = next;
        }
        if (!under) continue;
        CollisionSphere s;
        for (int k = 0; k < 3; ++k) s.pos[k] = meshes[i].pos[k];
        s.radius = meshes[i].radius;
        out.push_back(s);
    }
    return out;
}

}  // namespace omk

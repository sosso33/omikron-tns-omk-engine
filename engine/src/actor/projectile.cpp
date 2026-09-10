// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/projectile.h"

#include "actor/shootfire.h"

namespace omk {

int ProjectilePool::freeSlot() const {
    for (int i = 0; i < kProjectileSlots; ++i)
        if (pool_[static_cast<std::size_t>(i)].node == 0) return i;
    return -1;
}

int ProjectilePool::live() const {
    int n = 0;
    for (const auto& p : pool_) if (p.node) ++n;
    return n;
}

int ProjectilePool::tick(int actor, std::array<WeaponSlot, kWeaponSlots>& slots,
                         const FireIn& in, float dt) {
    int fired = 0;
    for (int s = 0; s < kWeaponSlots; ++s) {
        auto& w = slots[static_cast<std::size_t>(s)];
        // `v34 = u32(v1 + 4*v33, 84); if (!v34) return 1;` - the walk STOPS at
        // the first empty slot rather than skipping it, so a gap hides every
        // weapon behind it. Transcribed rather than tidied.
        if (!w.present) break;
        w.timer -= dt;
        if (w.timer > 0.0f) continue;
        // property 35 for this slot: no round, no shot, and the timer is left
        // expired so it tries again next frame
        if (w.ammo <= 0) continue;

        const int slot = freeSlot();
        if (slot < 0) return fired;      // POOL FULL: the shot is not taken,
                                         // and the engine returns outright
        --w.ammo;                        // event 45 writes it back
        w.timer = in.reload;

        Projectile& p = pool_[static_cast<std::size_t>(slot)];
        p.node  = slot + 1;              // the clone; non-zero IS occupied
        p.speed = static_cast<float>(in.speedHi) * 3.9000001f;
        p.owner = actor;
        p.kind  = in.kindLo;
        p.pos[0] = w.muzzle[0]; p.pos[1] = w.muzzle[1]; p.pos[2] = w.muzzle[2];

        // `sub_44D7F0` sets the direction and MAY REFUSE - and when it does
        // the engine `return 0`s with the entry already allocated and the
        // node already cloned. Reproduced: the round and the timer are spent
        // either way, and the walk stops.
        // `sub_44D7F0`: zero two words, rotate the slot's direction into
        // world and scale it by the speed, set the three scales to 1. The
        // rotation itself needs the node's matrix, which this port does not
        // hold, so the caller supplies a direction already in world.
        if (!in.aimAccepts) return fired;
        p.vel[0] = w.dir[0] * p.speed;
        p.vel[1] = w.dir[1] * p.speed;
        p.vel[2] = w.dir[2] * p.speed;
        p.scale[0] = p.scale[1] = p.scale[2] = 1.0f;
        ++fired;
    }
    return fired;
}

RecordShotOut ProjectilePool::fireFromRecord(int actor, const ShootWeaponRow& row,
                                             const RecordShot& in) {
    RecordShotOut out;
    // the free scan comes FIRST on this path, before the magazine is read -
    // so a full pool spends nothing
    const int slot = freeSlot();
    if (slot < 0) return out;
    if (row.ammoIndex) {
        const int count = in.ammo ? *in.ammo : 0;
        // count <= 0 for the player raises event 48, the weapon change - not
        // modelled - and the round below is taken regardless
        out.hudAmmo = count - 1;
        if (in.ammo) *in.ammo = count - 1;
        out.ammoSpent = true;
    }
    Projectile& p = pool_[static_cast<std::size_t>(slot)];
    p.node  = slot + 1;
    p.speed = row.speed;
    p.kind  = row.damage;
    p.owner = actor;
    p.travelled = 0.0f;                  // `a1[3] = 0` in `sub_44D7F0`
    for (int k = 0; k < 3; ++k) p.pos[k] = in.muzzle[k];
    // the node's matrix, and `(-1, 0, 0)` through it times the speed
    shootShotMatrix(in.yawDeg, in.pitchDeg, p.rot);
    const float mx[3] = {-1.0f, 0.0f, 0.0f};
    float dir[3];
    shootRotateRow(mx, p.rot, dir);
    for (int k = 0; k < 3; ++k) p.vel[k] = dir[k] * p.speed;
    p.scale[0] = p.scale[1] = p.scale[2] = 1.0f;
    p.sprite = in.sprite ? 1 : 0;
    p.grow   = in.sprite ? in.grow : 0.0f;
    p.windUp = in.sprite ? in.windUp : 0.0f;
    for (int k = 0; k < 3; ++k) p.growStep[k] = in.sprite ? in.growStep[k] : 0.0f;
    out.entry = slot;
    return out;
}

int ProjectilePool::fly(float dt, const WorldRay& world, std::vector<FlightEvent>* out,
                        const ActorSweep& actors) {
    int retired = 0;
    for (std::size_t i = 0; i < pool_.size(); ++i) {
        Projectile& e = pool_[i];
        if (!e.node) continue;
        // `v5 = *v1; if (v5 <= 0.0) { ...fly... } else *v1 -= flt_4C30D8;`
        if (e.windUp > 0.0f) {
            e.windUp = static_cast<float>(double(e.windUp) - dt);
            continue;
        }
        // `Matrix3x3_RotateVector(-1.0, 0.0, 0.0, node + 56, ...)`
        const float mx[3] = {-1.0f, 0.0f, 0.0f};
        float h[3];
        shootRotateRow(mx, e.rot, h);
        float a[3], b[3];
        for (int k = 0; k < 3; ++k) {
            a[k] = static_cast<float>(double(e.pos[k]) - h[k] * 0.5);
            e.pos[k] = static_cast<float>(double(e.vel[k]) * dt + e.pos[k]);
        }
        // `if (sprite) { if (+40 > 0) { +40 -= dt; scale += step * dt; } }`
        if (e.sprite && e.grow > 0.0f) {
            e.grow = static_cast<float>(double(e.grow) - dt);
            for (int k = 0; k < 3; ++k)
                e.scale[k] = static_cast<float>(double(e.growStep[k]) * dt + e.scale[k]);
        }
        for (int k = 0; k < 3; ++k)
            b[k] = static_cast<float>(double(e.pos[k]) - h[k] * -0.5);
        e.travelled = static_cast<float>(double(e.speed) * dt + e.travelled);
        float at[3] = {e.pos[0], e.pos[1], e.pos[2]};
        // the BODIES first (`sub_45E9C0`); the world only if none was met
        int victim = -1;
        const bool body = actors && actors(a, b, e.owner, at, victim);
        const bool hit = body || (world && world(a, b, at));
        if (e.travelled > kProjectileRange || hit) {
            if (out) {
                FlightEvent ev;
                ev.entry = static_cast<int>(i);
                ev.why = body ? FlightEvent::Why::Actor
                       : hit  ? FlightEvent::Why::World : FlightEvent::Why::Range;
                for (int k = 0; k < 3; ++k) ev.at[k] = at[k];
                ev.travelled = e.travelled;
                ev.victim = body ? victim : -1;
                ev.owner = e.owner;
                ev.damage = e.kind;
                for (int k = 0; k < 3; ++k) ev.vel[k] = e.vel[k];
                out->push_back(ev);
            }
            // `o3de_UnlinkObject; ...; sub_437890(node); *v3 = 0`
            e.node = 0;
            ++retired;
        }
    }
    return retired;
}

}  // namespace omk

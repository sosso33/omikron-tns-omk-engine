// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/projectile.h"

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

}  // namespace omk

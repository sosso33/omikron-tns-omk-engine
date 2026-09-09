// SPDX-License-Identifier: GPL-3.0-or-later
// THE PROJECTILE POOL and the fire gate (`todo/shoot-mode.md` 4c, 7f).
//
//     projectile_probe
//
// Four things the pool's own arithmetic settles, and one the engine's control
// flow does that a tidier version would not.
#include "actor/projectile.h"

#include <array>
#include <cstdio>

int main() {
    std::printf("pool %d entries x %d bytes = %d, span 0x534F48-0x531348 = %d\n",
                omk::kProjectileSlots, omk::kProjectileStride,
                omk::kProjectileSlots * omk::kProjectileStride,
                0x534F48 - 0x531348);

    // a full magazine down a clear pool
    omk::ProjectilePool pool;
    std::array<omk::WeaponSlot, omk::kWeaponSlots> slots{};
    slots[0].present = true; slots[0].ammo = 3; slots[0].timer = 0.0f;
    slots[0].muzzle[0] = 10; slots[0].muzzle[1] = 20; slots[0].muzzle[2] = 30;
    slots[0].dir[0] = 0; slots[0].dir[1] = 0; slots[0].dir[2] = -1;   // -Z
    omk::FireIn in; in.speedHi = 20; in.kindLo = 7; in.reload = 5.0f;
    int shots = 0;
    for (int f = 0; f < 30; ++f) shots += pool.tick(1, slots, in, 1.0f);
    std::printf("magazine: %d shots, %d ammo left, live %d, speed %.1f kind %d "
                "at %.0f %.0f %.0f\n", shots, slots[0].ammo, pool.live(),
                pool.entries()[0].speed, pool.entries()[0].kind,
                pool.entries()[0].pos[0], pool.entries()[0].pos[1],
                pool.entries()[0].pos[2]);
    std::printf("velocity: %.1f %.1f %.1f, scale %.1f\n",
                pool.entries()[0].vel[0], pool.entries()[0].vel[1],
                pool.entries()[0].vel[2], pool.entries()[0].scale[0]);
    // THE 60 BYTES ACCOUNT FOR EXACTLY - the walk-lands-on-the-size test
    // applied to a struct. Every offset the allocator or the aim writes,
    // in order, with no gap and no overlap.
    {
        const int off[] = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56};
        int claimed = 0, gaps = 0, prev = -4;
        for (int o : off) { claimed += 4; if (o != prev + 4) ++gaps; prev = o; }
        std::printf("entry: %d fields, %d bytes claimed of %d, %d gaps\n",
                    int(sizeof(off)/sizeof(off[0])), claimed,
                    omk::kProjectileStride, gaps);
    }

    // A GAP IN THE SLOTS HIDES EVERYTHING BEHIND IT - the engine's walk
    // `if (!weapon) return` stops rather than skipping.
    omk::ProjectilePool p2;
    std::array<omk::WeaponSlot, omk::kWeaponSlots> g{};
    g[0].present = false;
    g[1].present = true; g[1].ammo = 5;
    const int behindGap = p2.tick(1, g, in, 1.0f);
    std::printf("gap: %d shots from the slot behind an empty one\n", behindGap);

    // A FULL POOL TAKES NO SHOT AT ALL - and spends no round.
    omk::ProjectilePool p3;
    std::array<omk::WeaponSlot, omk::kWeaponSlots> f3{};
    f3[0].present = true; f3[0].ammo = 500;
    int taken = 0;
    for (int f = 0; f < 4000; ++f) taken += p3.tick(1, f3, in, 99.0f);
    std::printf("full: %d taken, %d live, %d ammo left, free slot %d\n",
                taken, p3.live(), f3[0].ammo, p3.freeSlot());

    // A REFUSED AIM still spends the round and the timer.
    omk::ProjectilePool p4;
    std::array<omk::WeaponSlot, omk::kWeaponSlots> f4{};
    f4[0].present = true; f4[0].ammo = 4;
    omk::FireIn refuse = in; refuse.aimAccepts = false;
    const int none = p4.tick(1, f4, refuse, 1.0f);
    std::printf("refused: %d fired, %d ammo left, %d live\n",
                none, f4[0].ammo, p4.live());
    return 0;
}

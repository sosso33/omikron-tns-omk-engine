// SPDX-License-Identifier: GPL-3.0-or-later
// THE SHOT, FROM THE TRIGGER TO THE POOL (`todo/shoot-mode.md` 7h,
// `actor/shootfire.h`).
//
//     shoot_fire <tables-dir> [<data-root>]
//
// Steps the chain the way the engine does - `MDSHOOT0`'s latch, the channel
// tick's shoot branch, `sub_47C2A0`'s gate, the request, and
// `Actor_TickProjectiles`' record path - and prints what each scenario did.
// With a data root it also resolves the ten weapon objects at `IAM\GLOBAL
// +42` through `Shoot_InitWeapon`'s table walk.
#include "actor/projectile.h"
#include "actor/shoot.h"
#include "actor/shootfire.h"
#include "actor/state.h"
#include "platform/datafs.h"
#include "script/objects.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// One frame of the player's shoot branch: the tick consumes the latch the
// PREVIOUS frame's queue drain set.
int step(omk::ShootRecord& r, omk::ShotLatch& l, bool press) {
    const auto g = omk::shootChannelTick(r, l, true, true, 1.0f);
    if (press) omk::mdShoot0(l, static_cast<int>(omk::ActorState::Shoot));
    const bool fired = l.request;
    l.request = false;
    return fired ? 2 : static_cast<int>(g);
}

std::string frames(const std::vector<int>& v) {
    std::string s;
    for (int f : v) s += (s.empty() ? "" : " ") + std::to_string(f);
    return s.empty() ? "-" : s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: shoot_fire <tables-dir> [<data-root>]\n");
        return 2;
    }
    const omk::ShootWeaponTable t =
        omk::ShootWeaponTable::loadJson(std::string(argv[1]) + "/shoot_weapons.json");
    if (!t.loaded()) { std::printf("rows: not read\n"); return 1; }
    std::printf("rows: player %zu other %zu\n", t.rows(true).size(), t.rows(false).size());
    const omk::ShootWeaponRow* gun = t.find(1, true);
    if (!gun) { std::printf("row: key 1 missing\n"); return 1; }
    std::printf("row: key 1 rate %.1f speed %.1f damage %d magazine %d\n",
                double(gun->rate), double(gun->speed), gun->damage, gun->ammoIndex);

    // THE LATCH is state-gated, and only the shoot branch has a rate: the
    // other branch turns a latch straight into a request.
    {
        omk::ShotLatch l;
        const bool a1 = omk::mdShoot0(l, static_cast<int>(omk::ActorState::Normal));
        const bool a3 = omk::mdShoot0(l, static_cast<int>(omk::ActorState::Shoot));
        omk::ShootRecord r; r.weapon = gun;
        omk::ShotLatch l2; l2.latch = true;
        omk::shootChannelTick(r, l2, false, true, 1.0f);
        std::printf("latch: state 1 arms %d, state 3 arms %d; no shoot pose: request %d latch %d\n",
                    int(a1), int(a3), int(l2.request), int(l2.latch));
    }

    // A: the very first tick of the mode. The record is zeroed, so the weapon
    // is UP and the caller's reload makes the countdown equal the rate.
    {
        omk::ShootRecord r; r.weapon = gun;
        omk::ShotLatch l; l.latch = true;
        const int g = step(r, l, false);
        std::printf("first tap: outcome %d\n", g);
    }
    // B: at rest, then one tap. C: held from rest.
    {
        omk::ShootRecord r; r.weapon = gun;
        omk::ShotLatch l;
        for (int f = 0; f < 12; ++f) step(r, l, false);
        std::printf("rested: lowered %.6f timer %.6f\n", double(r.weaponLowered),
                    double(r.weaponTimer));
        // the press is queued at frame -1's drain, so the tick of frame 0 sees it
        omk::mdShoot0(l, static_cast<int>(omk::ActorState::Shoot));
        std::vector<int> fired;
        for (int f = 0; f < 20; ++f) if (step(r, l, false) == 2) fired.push_back(f);
        std::printf("tap from rest fires at %s\n", frames(fired).c_str());
    }
    {
        omk::ShootRecord r; r.weapon = gun;
        omk::ShotLatch l;
        for (int f = 0; f < 12; ++f) step(r, l, false);
        omk::mdShoot0(l, static_cast<int>(omk::ActorState::Shoot));
        std::vector<int> fired;
        for (int f = 0; f < 60; ++f) if (step(r, l, true) == 2) fired.push_back(f);
        std::printf("held from rest fires at %s\n", frames(fired).c_str());
    }
    // D: released after a shot - how long the weapon takes to come down, and
    // the floor the countdown rests on.
    {
        omk::ShootRecord r; r.weapon = gun;
        omk::ShotLatch l; l.latch = true;
        step(r, l, false);
        int n = 0;
        while (r.weaponLowered != 1.0f && n < 100) { step(r, l, false); ++n; }
        std::printf("lowered after %d frames, timer %.1f\n", n, double(r.weaponTimer));
    }
    // E: the rate is the ROW's - key 3, the player's f0 = 2.
    {
        const omk::ShootWeaponRow* fast = t.find(3, true);
        omk::ShootRecord r; r.weapon = fast;
        omk::ShotLatch l; l.latch = true;
        std::vector<int> fired;
        for (int f = 0; f < 20; ++f) if (step(r, l, true) == 2) fired.push_back(f);
        std::printf("rate %.0f held fires at %s\n", fast ? double(fast->rate) : -1.0,
                    frames(fired).c_str());
    }
    // F: no weapon row, no shot - the gate's first line.
    {
        omk::ShootRecord r;
        omk::ShotLatch l; l.latch = true;
        int any = 0;
        for (int f = 0; f < 20; ++f) if (step(r, l, true) == 2) ++any;
        std::printf("no row: %d shots\n", any);
    }

    // THE AIM: `sub_442160(0, yaw + 90, -pitch)` through (-1, 0, 0), held
    // against the (0, 0, -1) forward through `sub_442160(0, yaw, 0)`.
    {
        double worst = 0.0;
        for (int yaw = -180; yaw <= 180; yaw += 15) {
            float d[3], m[9], fwd[3];
            omk::shootShotDirection(float(yaw), 0.0f, d);
            omk::shootEulerMatrix(0.0f, float(yaw) * 0.0174532925f, 0.0f, m);
            const float mz[3] = {0.0f, 0.0f, -1.0f};
            omk::shootRotateRow(mz, m, fwd);
            for (int k = 0; k < 3; ++k) {
                const double e = std::abs(double(d[k]) - fwd[k]);
                if (e > worst) worst = e;
            }
        }
        float up[3], down[3], side[3];
        omk::shootShotDirection(0.0f, 30.0f, up);
        omk::shootShotDirection(0.0f, -30.0f, down);
        omk::shootShotDirection(135.0f, 0.0f, side);
        std::printf("aim: worst %.6f off the forward over 25 yaws; pitch +30 %.4f %.4f %.4f; "
                    "pitch -30 y %.4f; yaw 135 %.4f %.4f %.4f\n", worst,
                    double(up[0]), double(up[1]), double(up[2]), double(down[1]),
                    double(side[0]), double(side[1]), double(side[2]));
    }

    // THE RECORD PATH: the pool entry, the magazine, and a full pool.
    {
        omk::ProjectilePool pool;
        omk::RecordShot rs;
        rs.muzzle[0] = 10; rs.muzzle[1] = 20; rs.muzzle[2] = 30;
        const auto o = pool.fireFromRecord(7, *gun, rs);
        const auto& e = pool.entries()[static_cast<std::size_t>(o.entry)];
        std::printf("record shot: entry %d speed %.1f damage %d owner %d at %.0f %.0f %.0f "
                    "vel %.1f %.1f %.1f spent %d\n", o.entry, double(e.speed), e.kind,
                    e.owner, double(e.pos[0]), double(e.pos[1]), double(e.pos[2]),
                    double(e.vel[0]), double(e.vel[1]), double(e.vel[2]), int(o.ammoSpent));
        const omk::ShootWeaponRow* mag = t.find(2, true);
        int count = 5;
        rs.ammo = &count;
        const auto o2 = pool.fireFromRecord(7, *mag, rs);
        const int after5 = count;
        count = 0;
        const auto o3 = pool.fireFromRecord(7, *mag, rs);
        std::printf("magazine: slot %d, 5 -> %d (hud %d); 0 -> %d and the shot %s\n",
                    mag->ammoIndex - 1, after5, o2.hudAmmo, count,
                    o3.entry >= 0 ? "IS taken" : "is not taken");
        omk::ProjectilePool full;
        omk::RecordShot plain;
        while (full.freeSlot() >= 0) full.fireFromRecord(7, *gun, plain);
        count = 9;
        const auto o4 = full.fireFromRecord(7, *mag, rs);
        std::printf("full: entry %d, magazine %d, live %d\n", o4.entry, count, full.live());
    }

    // THE TYPE: the object's kind, and the one hand-written exception.
    // The name is the node's +48, `Scene_Load3DO`'s copy of the path
    // `Object_Load` built: "MESHES\OBJETS\" + stem + ".3DO".
    std::printf("type: kind 1 BATPOUV -> %d, kind 1 WAVER -> %d, kind 3 BATPOUV -> %d\n",
                omk::shootWeaponType(1, "MESHES\\OBJETS\\BATPOUV.3DO"),
                omk::shootWeaponType(1, "MESHES\\OBJETS\\WAVER.3DO"),
                omk::shootWeaponType(3, "MESHES\\OBJETS\\BATPOUV.3DO"));

    // THE SHIPPED WEAPONS, when there is a data root: every object the ten
    // slots at `IAM\GLOBAL +42` name, through the table walk.
    if (argc >= 3) {
        const omk::DataFs fs(argv[2]);
        const auto g = fs.read("IAM/GLOBAL");
        const auto objs = omk::loadObjects(fs);
        if (g.size() < 62 || objs.empty()) { std::printf("weapons: not read\n"); return 0; }
        int named = 0, resolved = 0;
        for (int s = 0; s < 10; ++s) {
            const auto lo = std::to_integer<int>(g[static_cast<std::size_t>(42 + 2 * s)]);
            const auto hi = std::to_integer<int>(g[static_cast<std::size_t>(43 + 2 * s)]);
            const int id = static_cast<std::int16_t>(lo | (hi << 8));
            if (id < 0 || static_cast<std::size_t>(id) >= objs.size()) {
                std::printf("weapon slot %d: object %d\n", 5 + s, id);
                continue;
            }
            ++named;
            const auto& o = objs[static_cast<std::size_t>(id)];
            const int type = omk::shootWeaponType(o.kind, "MESHES\\OBJETS\\" + o.stem + ".3DO");
            const omk::ShootWeaponRow* w = t.find(type, true);
            if (w) ++resolved;
            std::printf("weapon slot %d: object %d '%s' stem '%s' kind %d -> type %d -> %s\n",
                        5 + s, id, o.name.c_str(), o.stem.c_str(), o.kind, type,
                        w ? ("row key " + std::to_string(w->key)).c_str() : "NO ROW");
        }
        std::printf("weapons: %d of 10 slots name an object, %d resolve to a row\n",
                    named, resolved);
    }
    return 0;
}

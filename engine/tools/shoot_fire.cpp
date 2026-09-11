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
#include "actor/shoothit.h"
#include "actor/shootaim.h"
#include "actor/shootmove.h"
#include "script/program.h"
#include "actor/state.h"
#include "formats/map2d.h"
#include "formats/sfx.h"
#include "o3de/collision.h"
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

    // THE FLIGHT - `Projectiles_Tick`. A bolt from the origin at yaw 0 heads
    // down -Z; the wall is a triangle across z = -499.5, and the world ray is
    // the soup sweep at radius 0, which is a segment against each face. The
    // -499.5 is chosen to SEPARATE the segment's half-unit front edge from
    // none: 4 x 124.8 = 499.2 falls short of it and 499.2 + 0.5 does not, so
    // the edge is what makes it frame 4 rather than 5.
    {
        const omk::TriangleSoup wall = {-1000.0f, -1000.0f, -499.5f,
                                        1000.0f, -1000.0f, -499.5f,
                                        0.0f, 1000.0f, -499.5f};
        const auto rayOn = [](const omk::TriangleSoup& soup) {
            return [&soup](const float a[3], const float b[3], float hit[3]) {
                const double p0[3] = {a[0], a[1], a[2]};
                const double d[3] = {double(b[0]) - a[0], double(b[1]) - a[1],
                                     double(b[2]) - a[2]};
                const auto h = omk::sweepSphere(soup, p0, d, 0.0);
                if (!h) return false;
                for (int k = 0; k < 3; ++k) hit[k] = static_cast<float>(p0[k] + h->t * d[k]);
                return true;
            };
        };
        const auto flyOut = [&](omk::ProjectilePool& pool, const omk::WorldRay& ray,
                                std::vector<omk::FlightEvent>& ev) {
            int f = 0;
            while (pool.live() && f < 200) { ++f; pool.fly(1.0f, ray, &ev); }
            return f;
        };
        omk::RecordShot rs;
        {
            omk::ProjectilePool pool;
            pool.fireFromRecord(1, *gun, rs);
            std::vector<omk::FlightEvent> ev;
            const int f = flyOut(pool, rayOn(wall), ev);
            std::printf("wall: retired on frame %d by the %s at z %.1f after %.1f\n", f,
                        !ev.empty() && ev[0].why == omk::FlightEvent::Why::World ? "world" : "range",
                        ev.empty() ? 0.0 : double(ev[0].at[2]),
                        ev.empty() ? 0.0 : double(ev[0].travelled));
        }
        {
            omk::ProjectilePool pool;
            pool.fireFromRecord(1, *gun, rs);
            std::vector<omk::FlightEvent> ev;
            const omk::TriangleSoup none;
            const int f = flyOut(pool, rayOn(none), ev);
            std::printf("range: retired on frame %d by the %s after %.1f\n", f,
                        !ev.empty() && ev[0].why == omk::FlightEvent::Why::World ? "world" : "range",
                        ev.empty() ? 0.0 : double(ev[0].travelled));
        }
        {
            omk::ProjectilePool pool;
            omk::RecordShot w = rs; w.sprite = true; w.windUp = 12.0f;
            pool.fireFromRecord(1, *gun, w);
            int moved = 0;
            for (int f = 1; f <= 40 && !moved; ++f) {
                pool.fly(1.0f, nullptr);
                if (pool.entries()[0].pos[2] != 0.0f) moved = f;
            }
            std::printf("wind-up 12: first move on frame %d\n", moved);
        }
        {
            omk::ProjectilePool pool;
            omk::RecordShot g = rs; g.sprite = true; g.grow = 8.0f;
            g.growStep[0] = 5.9f; g.growStep[1] = 0.2f; g.growStep[2] = 0.2f;
            pool.fireFromRecord(1, *gun, g);
            for (int f = 0; f < 12; ++f) pool.fly(1.0f, nullptr);
            const auto& e = pool.entries()[0];
            std::printf("grow: scale after 12 frames %.2f %.2f %.2f\n", double(e.scale[0]),
                        double(e.scale[1]), double(e.scale[2]));
        }
    }

    // THE HIT (`actor/shoothit.h`). One body, actor 9: a pelvis at z -300
    // (radius 44, the whole body's) with a small box, and a head 25 above it.
    {
        omk::HitBody body;
        body.actor = 9;
        body.root = 0;
        omk::HitMesh pelvis;
        pelvis.pos[2] = -300.0f;
        pelvis.radius = 44.0f;
        pelvis.boxMin[0] = -7; pelvis.boxMin[1] = -5; pelvis.boxMin[2] = -8;
        pelvis.boxMax[0] =  7; pelvis.boxMax[1] =  6; pelvis.boxMax[2] =  3;
        omk::HitMesh head;
        head.pos[1] = -25.0f; head.pos[2] = -300.0f;
        head.centre[1] = -3.0f;
        head.radius = 6.0f;
        head.boxMin[0] = -4; head.boxMin[1] = -8; head.boxMin[2] = -6;
        head.boxMax[0] =  4; head.boxMax[1] =  1; head.boxMax[2] =  3;
        body.meshes = {pelvis, head};
        const std::vector<omk::HitBody> bodies = {body};
        const auto shoot = [&](float x, float y, int exclude) {
            const float a[3] = {x, y, 0.0f}, b[3] = {x, y, -400.0f};
            omk::BodyHit h;
            const bool hit = omk::shootSweepBodies(a, b, bodies, exclude, h);
            char buf[96];
            if (hit)
                std::snprintf(buf, sizeof buf, "actor %d mesh %d at %.1f %.1f %.1f dist %.1f",
                              h.actor, h.mesh, double(h.at[0]), double(h.at[1]),
                              double(h.at[2]), double(h.dist));
            else
                std::snprintf(buf, sizeof buf, "miss");
            return std::string(buf);
        };
        // "over the head" is the engine's box test ON ITS OWN TERMS: 40 above
        // the pelvis the line misses the head's sphere, passes inside the
        // body's 44, and the pelvis box - whose start lies BELOW its minimum
        // on y with no motion along y - has only its MAXIMUM checked, so it
        // counts. Transcribed from 0x004987F0..0x00498852, not corrected.
        std::printf("hit: straight %s; over the head %s; 60 aside %s; own body %s\n",
                    shoot(0, 0, -1).c_str(), shoot(0, -40, -1).c_str(),
                    shoot(60, 0, -1).c_str(), shoot(0, 0, 9).c_str());
        const float vA[3] = {0, 0, -1}, vB[3] = {0, 0, 1}, vC[3] = {1, 0, 0},
                    vD[3] = {0.9f, 0, -0.44f};
        const int bands[4] = {omk::shootHitBand(0, vA), omk::shootHitBand(0, vB),
                              omk::shootHitBand(0, vC), omk::shootHitBand(0, vD)};
        std::printf("bands: along %d back %d side %d slant %d; death types %d %d %d %d\n",
                    bands[0], bands[1], bands[2], bands[3],
                    omk::shootDeathClipType(bands[0]), omk::shootDeathClipType(bands[1]),
                    omk::shootDeathClipType(bands[2]), omk::shootDeathClipType(bands[3]));
        // THE DAMAGE's gates and arithmetic
        const auto hitWith = [](omk::ShootRecord r, omk::HitIn in) {
            const auto o = omk::shootApplyHit(r, in);
            return o.refused ? -1 : o.health;
        };
        omk::ShootRecord g; g.health = 15;
        omk::HitIn fromPlayer; fromPlayer.damage = 5; fromPlayer.shooterIsPlayer = true;
        fromPlayer.boltVel[2] = -1.0f;
        omk::HitIn gunOnGun = fromPlayer; gunOnGun.shooterIsPlayer = false;
        omk::HitIn notInShoot = fromPlayer; notInShoot.victimInShoot = false;
        omk::ShootRecord inv = g; inv.flags = 0x800;
        omk::HitIn baton = fromPlayer; baton.damage = 6;
        omk::ShootRecord t11 = g; t11.type = 11;
        omk::ShootRecord spectre = g; spectre.flags = 0x4000;
        std::printf("gates: player hit %d, gunman on gunman %d, not in shoot %d, 0x800 %d, "
                    "baton on type 0 %d, baton on type 11 %d, Waver on 0x4000 %d, "
                    "baton on 0x4000 %d\n",
                    hitWith(g, fromPlayer), hitWith(g, gunOnGun), hitWith(g, notInShoot),
                    hitWith(inv, fromPlayer), hitWith(g, baton), hitWith(t11, baton),
                    hitWith(spectre, fromPlayer), hitWith(spectre, baton));
        omk::ShootRecord me; me.health = 100;
        omk::HitIn atMe; atMe.damage = 5; atMe.victimIsPlayer = true; atMe.bodyShield = 30;
        omk::ShootRecord me2 = me;
        omk::HitIn atMe2 = atMe; atMe2.bodyShield = 100;
        std::printf("shield: 5 through 30 -> %d, through 100 -> %d\n",
                    100 - hitWith(me, atMe), 100 - hitWith(me2, atMe2));
        // three Waver hits on a 15-health gunman, the third from behind him
        omk::ShootRecord v; v.health = 15;
        omk::HitIn react = fromPlayer; react.reactAt = 12;
        const auto h1 = omk::shootApplyHit(v, react);
        const auto h2 = omk::shootApplyHit(v, react);
        const auto h3 = omk::shootApplyHit(v, react);
        std::printf("kill: %d %d %d, actions %d %d, killed %d, death type %d, enemy drop %d, "
                    "flags 0x%x\n", h1.health, h2.health, h3.health, h1.action, h2.action,
                    int(h3.killed), h3.deathType, int(h3.enemyCountDrop), v.flags);
    }

    // THE RAISE (`actor/shootaim.h`): `sub_471950`'s key choice and slerps on
    // a synthetic track whose key k is a turn of 10k degrees about X, so every
    // blend reads back as one angle.
    {
        std::vector<omk::Quatf> keys;
        for (int k = 1; k <= 15; ++k) {
            const double h = k * 10.0 * 3.14159265358979 / 360.0;
            keys.push_back({static_cast<float>(std::cos(h)), static_cast<float>(std::sin(h)), 0, 0});
        }
        const auto deg = [](const omk::Quatf& q) {
            return 2.0 * std::acos(std::fabs(double(q.w)) > 1.0 ? 1.0 : std::fabs(double(q.w)))
                   * 180.0 / 3.14159265358979;
        };
        const float r15 = 15.0f * 0.0174532925f, r45 = 45.0f * 0.0174532925f,
                    r30 = 30.0f * 0.0174532925f;
        const omk::Quatf up15 = omk::shootAimBone(keys, 0.0f, r15);
        const omk::Quatf dn15 = omk::shootAimBone(keys, 0.0f, -r15);
        const omk::Quatf up45 = omk::shootAimBone(keys, 0.0f, r45);
        const omk::Quatf side = omk::shootAimBone(keys, r30, r15);
        const omk::Quatf stance = keys[8];                    // key 9, 90 degrees
        std::printf("raise: pitch +15 %.2f, -15 %.2f, +45 %.2f; yaw 30 pitch 15 %.2f; "
                    "lowered 1 %.2f, 0.5 %.2f, 0 %.2f\n", deg(up15), deg(dn15), deg(up45),
                    deg(side), deg(omk::shootAimLower(up15, stance, 1.0f)),
                    deg(omk::shootAimLower(up15, stance, 0.5f)),
                    deg(omk::shootAimLower(up15, stance, 0.0f)));
        omk::ShootAim a;
        std::string steps;
        for (int f = 0; f < 4; ++f) {
            omk::shootAimSlew(a, 0.0f, 1.2f, 1.0f);
            char b[16];
            std::snprintf(b, sizeof b, "%s%.4f", f ? " " : "", double(a.pitch));
            steps += b;
        }
        std::printf("slew: toward 1.2 rad %s\n", steps.c_str());
    }

    // THE MOVER (`actor/shootmove.h`): `sub_47CC70`'s row and speeds, then
    // `sub_47D4D0` stepped by hand, dt 1 - forward held to the top speed,
    // released to a stop, a strafe, a crouch at speed, a reversal, the step
    // turned by LAST frame's facing, and the refusals.
    {
        std::string rows;
        for (int sp : {0, 50, 51, 100, 101, 110, 200}) {
            char b[24];
            std::snprintf(b, sizeof b, "%s%d->%d", rows.empty() ? "" : " ", sp,
                          omk::shootSpeedRow(sp));
            rows += b;
        }
        omk::ShootMover m;
        omk::shootMoveInit(m, 100, 40.0f);
        std::printf("mover rows: %s; Speed 100: top %.4f accel %.6f brake %.5f\n",
                    rows.c_str(), double(m.top), double(m.accel), double(m.brake));
        float pitch = 0.0f;
        int toTop = 0;
        for (int f = 1; f <= 40; ++f) {
            omk::shootMoveForward(m, true, false, 1.0f, pitch);
            omk::shootMoveTick(m, 0.0f, 1.0f);
            if (!toTop && m.fwd <= -m.top) toTop = f;
        }
        const float held = m.fwd;
        const unsigned flagsAfter = m.flags;
        int stop = 0;
        for (int f = 1; f <= 20 && !stop; ++f) {
            omk::shootMoveTick(m, 0.0f, 1.0f);
            if (m.fwd == 0.0f) stop = f;
        }
        int sideTop = 0;
        for (int f = 1; f <= 40; ++f) {
            omk::shootMoveStrafe(m, true, false);
            omk::shootMoveTick(m, 0.0f, 1.0f);
            if (!sideTop && m.side <= -m.top) sideTop = f;
        }
        std::printf("mover held: forward %.4f after %d frames, intents cleared 0x%x; "
                    "released: 0 after %d; strafe right %.4f after %d\n",
                    double(held), toTop, flagsAfter, stop, double(m.side), sideTop);
        // a crouch at full speed: over the halved top it slows by twice the
        // acceleration, then the ordinary clamp takes it
        omk::shootMoveInit(m, 100, 40.0f);
        for (int f = 0; f < 30; ++f) {
            omk::shootMoveForward(m, true, false, 1.0f, pitch);
            omk::shootMoveTick(m, 0.0f, 1.0f);
        }
        omk::shootMoveCrouch(m, true);
        std::string crouch;
        for (int f = 0; f < 8; ++f) {
            omk::shootMoveForward(m, true, false, 1.0f, pitch);
            omk::shootMoveTick(m, 0.0f, 1.0f);
            char b[16];
            std::snprintf(b, sizeof b, "%s%.2f", f ? " " : "", double(m.fwd));
            crouch += b;
        }
        // a reversal zeroes the velocity before the new intent
        omk::shootMoveCrouch(m, false);
        omk::shootMoveForward(m, false, false, 1.0f, pitch);
        omk::shootMoveTick(m, 0.0f, 1.0f);
        const float reversed = m.fwd;
        // turned: at full speed forward, facing 90 - the first step still
        // turns by the facing of the frame before
        omk::shootMoveInit(m, 100, 40.0f);
        for (int f = 0; f < 30; ++f) {
            omk::shootMoveForward(m, true, false, 1.0f, pitch);
            omk::shootMoveTick(m, 0.0f, 1.0f);
        }
        omk::shootMoveForward(m, true, false, 1.0f, pitch);
        const omk::ShootMoveStep s1 = omk::shootMoveTick(m, 90.0f, 1.0f);
        omk::shootMoveForward(m, true, false, 1.0f, pitch);
        const omk::ShootMoveStep s2 = omk::shootMoveTick(m, 90.0f, 1.0f);
        m.flags |= omk::kShootMoveBlock;
        const bool blocked = !omk::shootMoveForward(m, true, false, 1.0f, pitch);
        m.flags = 0;
        const bool falling = !omk::shootMoveStrafe(m, true, true);
        std::printf("mover crouch: %s; reversed %.4f; facing 90: step %.2f %.2f then "
                    "%.2f %.2f; refused 0x800 %d, falling %d; turn MDRG %.1f MDRD %.1f\n",
                    crouch.c_str(), double(reversed), double(s1.dx), double(s1.dz),
                    double(s2.dx), double(s2.dz) + 0.0, int(blocked), int(falling),
                    double(omk::shootTurnDegrees(-50, 20)),
                    double(omk::shootTurnDegrees(50, 20)));
        // THE LOOK (`sub_47D370`'s pitch half) at the default row 24, 15
        std::printf("look: dy 10 -> %.2f, inverted %.2f, 60 fps %.2f; clamped %.2f %.2f; "
                    "MDLUP %.2f MDLDO %.2f\n",
                    double(omk::shootPitchStep(0.0f, 10, 15, false, 1.0f)),
                    double(omk::shootPitchStep(0.0f, 10, 15, true, 1.0f)),
                    double(omk::shootPitchStep(0.0f, 10, 15, false, 0.5f)),
                    double(omk::shootPitchStep(0.0f, 400, 15, false, 1.0f)),
                    double(omk::shootPitchStep(0.0f, -400, 15, false, 1.0f)),
                    double(omk::shootPitchStep(0.0f, -25, 15, false, 1.0f)),
                    double(omk::shootPitchStep(0.0f, 25, 15, false, 1.0f)));
        // THE NOISE (`sub_4246E0`'s test of one record): hearing 5 cells of
        // 39, the point at the origin, him 100 away (in) or 1000 (out)
        const float at[3] = {0.0f, 0.0f, 0.0f};
        const float nearBy[3] = {100.0f, 0.0f, 0.0f}, farOff[3] = {1000.0f, 0.0f, 0.0f};
        const auto rec = [](std::uint32_t flags, int state, int step, int action) {
            omk::ShootRecord r;
            r.flags = flags; r.state = state; r.scriptStep = step; r.hitAction = action;
            return r;
        };
        omk::ShootRecord a = rec(0x40, 0, 0, -1), b = rec(0x40, 0, 0, -1), c = rec(0x40, 0, 0, -1),
                         d = rec(0x40, 0, 8, -1), e = rec(0, 0, 0, -1), f = rec(0x40, 14, 0, -1),
                         g = rec(0x40, 0, 0, 7);
        const omk::NoiseHearing h1 = omk::shootHearNoise(a, nearBy, at, 5, 39.0f, 0, 0);
        const bool again = omk::shootHearNoise(a, nearBy, at, 5, 39.0f, 0, 0).heard;
        const omk::NoiseHearing h2 = omk::shootHearNoise(b, nearBy, at, 5, 39.0f, 0, 1);
        const bool far1 = omk::shootHearNoise(c, farOff, at, 5, 39.0f, 0, 0).heard;
        const omk::NoiseHearing h3 = omk::shootHearNoise(d, nearBy, at, 5, 39.0f, 0, 0);
        const bool deaf = omk::shootHearNoise(e, nearBy, at, 5, 39.0f, 0, 0).heard;
        const bool st14 = omk::shootHearNoise(f, nearBy, at, 5, 39.0f, 0, 0).heard;
        const omk::NoiseHearing h4 = omk::shootHearNoise(g, nearBy, at, 5, 39.0f, 0, 0);
        std::printf("noise: same floor alerted %d flags 0x%x action %d; again %d; other floor "
                    "alerted %d act %d action %d; out of range %d; step 8 alerted %d act %d; "
                    "not entered %d; state 14 %d; +148 7 -> action %d\n",
                    int(h1.alerted), unsigned(a.flags), h1.action, int(again), int(h2.alerted),
                    int(h2.act), h2.action, int(far1), int(h3.alerted), int(h3.act), int(deaf),
                    int(st14), h4.action);
    }

    // A GUNMAN'S AIM (`Actor_TickProjectiles`' other arm): the bolt must fly
    // along the jittered offset exactly, whatever the offset's direction. A:
    // straight along +x with no jitter. B: up and away with r 30 and k
    // (0, 50, 99), which move x by +10, y by 0 and z by -9.8.
    {
        const float mz[3] = {0.0f, 0.0f, 0.0f};
        const float ta[3] = {100.0f, 0.0f, 0.0f}, tb[3] = {0.0f, -50.0f, -100.0f};
        const int k0[3] = {0, 0, 0}, kb[3] = {0, 50, 99};
        const omk::GunmanAim a = omk::shootGunmanAim(ta, mz, 0.0f, k0);
        const omk::GunmanAim b = omk::shootGunmanAim(tb, mz, 30.0f, kb);
        float da[3], db[3];
        omk::shootShotDirection(a.yawDeg, a.pitchDeg, da);
        omk::shootShotDirection(b.yawDeg, b.pitchDeg, db);
        std::printf("gunman aim: A yaw %.2f pitch %.2f dir %.3f %.3f %.3f; B d %.1f %.1f %.1f "
                    "dist %.2f yaw %.2f pitch %.2f dir %.3f %.3f %.3f vs d/|d| %.3f %.3f %.3f\n",
                    double(a.yawDeg), double(a.pitchDeg), double(da[0]), double(da[1]),
                    double(da[2]), double(b.d[0]), double(b.d[1]), double(b.d[2]), b.dist,
                    double(b.yawDeg), double(b.pitchDeg), double(db[0]), double(db[1]),
                    double(db[2]), b.d[0] / b.dist, b.d[1] / b.dist, b.d[2] / b.dist);
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
        // THE WALL TEST (`sub_421140`) on the supermarket's own floor map: the
        // first free cell whose +x neighbour the AI refuses, a one-cell step
        // into it from the cell's centre; the first free cell with a free +x
        // neighbour, the same step; and a step off the floor. The cells are
        // the map's, found here, so the probe checks itself.
        {
            omk::Map2d mp;
            const auto raw = fs.read("MAP2D/SMARKET1.MPT");
            if (!raw.empty() && mp.load(raw) && !mp.floors().empty()) {
                const auto& f = mp.floors()[0];
                const float sc = static_cast<float>(mp.scale());
                int bx = -1, bz = -1, fx = -1, fz = -1;
                for (int z = 1; z + 1 < static_cast<int>(f.h) && (bx < 0 || fx < 0); ++z)
                    for (int x = 1; x + 2 < static_cast<int>(f.w); ++x) {
                        const bool here = !omk::Map2d::blockedValue(f.cell(x, z));
                        const bool next = !omk::Map2d::blockedValue(f.cell(x + 1, z));
                        if (here && !next && bx < 0) { bx = x; bz = z; }
                        if (here && next && fx < 0) { fx = x; fz = z; }
                    }
                const auto centre = [&](int cx, int cz, float o[2]) {
                    o[0] = cx * sc + f.bound[0] + sc * 0.5f;
                    o[1] = cz * sc + f.bound[4] + sc * 0.5f;
                };
                omk::ShootRecord w;
                w.state = 6; w.node = 0;
                float c[2], snap[2];
                w.destX = bx; w.destZ = bz; centre(bx, bz, c);
                const int blockedR = omk::shootWallTest(w, mp, c[0], c[1], sc, 0.0f, 1, snap);
                const bool snapHere = std::fabs(snap[0] - c[0]) < 0.01f && std::fabs(snap[1] - c[1]) < 0.01f;
                omk::ShootRecord w2;
                w2.state = 6; w2.node = 0; w2.destX = fx; w2.destZ = fz; centre(fx, fz, c);
                const int freeR = omk::shootWallTest(w2, mp, c[0], c[1], sc, 0.0f, 1, snap);
                omk::ShootRecord w3;
                w3.state = 6; w3.node = 0;
                const int offR = omk::shootWallTest(w3, mp, f.bound[0] - 100.0f, f.bound[4], sc, 0.0f, 1, snap);
                omk::ShootRecord w4 = w;
                w4.state = 2;
                const int edgeR = omk::shootWallTest(w4, mp, c[0], c[1], sc, 0.0f, 1, snap);
                std::printf("wall test: into byte %u from (%d,%d) -> %d, snapped to his cell %d; free step "
                            "(%d,%d) -> %d, cell now (%d,%d) flag 0x20000 %d; off the floor %d; "
                            "state 2 %d\n", unsigned(f.cell(bx + 1, bz)), bx, bz, blockedR,
                            int(snapHere), fx, fz, freeR, w2.destX, w2.destZ,
                            int((w2.flags & 0x20000u) != 0), offR, edgeR);
            } else {
                std::printf("wall test: SMARKET1.MPT not read\n");
            }
        }
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
        // `shoot2.sfx`, which `Shoot_Enter` loads: section A, the shot sprites
        const auto sb = fs.read("SCPTDATA/shoot2.sfx");
        const omk::SfxFile sf = omk::readSfx(sb);
        const omk::FxShotSprite* wv = sf.shotSprite("Waver");
        const omk::FxShotSprite* mz = sf.shotSprite("Megazok");
        const omk::FxShotSprite* hy = sf.shotSprite("Gigazok");
        std::printf("shot sprites: %zu, walk exact %d; Waver grow %.0f wind-up %.0f step "
                    "%.1f %.1f %.1f; Megazok wind-up %.0f; Gigazok wind-up %.0f\n",
                    sf.shotSprites.size(), int(sf.exact), wv ? double(wv->grow) : -1.0,
                    wv ? double(wv->windUp) : -1.0, wv ? double(wv->growStep[0]) : 0.0,
                    wv ? double(wv->growStep[1]) : 0.0, wv ? double(wv->growStep[2]) : 0.0,
                    mz ? double(mz->windUp) : -1.0, hy ? double(hy->windUp) : -1.0);
        // THE SHOT'S SOUNDS (`todo/shoot-mode.md` 8.2): the row's three
        // effect ids, the section C sound each names, and whether the mode's
        // library - `shoot2.scx`, what `Game_Start` puts in `stru_930780` -
        // carries it (`Scene_FindSoundIndex`). 65535 is "no sound".
        const omk::ScxRuntime lib(fs.read("SCPTDATA/shoot2.scx"));
        std::string sounds;
        for (const omk::FxShotSprite* s : {wv, mz}) {
            if (!s) continue;
            const int ids[3] = {s->muzzleEffect, s->flightEffect, s->impactEffect};
            sounds += (sounds.empty() ? "" : "; ") + s->name;
            for (int k = 0; k < 3; ++k) {
                const omk::FxEffect* e = sf.byId(ids[k]);
                const int snd = e ? e->sound : -1;
                const int w = (snd > 0 && snd != 65535 && lib.valid()) ? lib.wavBydId(snd) : -1;
                char b[96];
                std::snprintf(b, sizeof b, " %d:%d%s%s", ids[k], snd,
                              w >= 0 ? "=" : "", w >= 0 ? lib.wavName(w).c_str() : "");
                sounds += b;
            }
        }
        std::printf("shot sounds: library %d wavs;%s\n", lib.valid() ? lib.wavCount() : -1,
                    (" " + sounds).c_str());
    }
    return 0;
}

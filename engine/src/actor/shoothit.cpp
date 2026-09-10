// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/shoothit.h"

#include "actor/shoot.h"
#include "actor/shootfire.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace omk {

namespace {

// world = pos + M * v, M column-major
void toWorld(const HitMesh& h, const float v[3], float out[3]) {
    for (int k = 0; k < 3; ++k)
        out[k] = h.pos[k] + h.m[k] * v[0] + h.m[3 + k] * v[1] + h.m[6 + k] * v[2];
}
// M^T * v: a world DIRECTION into the mesh's frame (`Matrix3x3_RotateVectorT`)
void toLocalDir(const HitMesh& h, const float v[3], float out[3]) {
    for (int c = 0; c < 3; ++c)
        out[c] = h.m[3 * c] * v[0] + h.m[3 * c + 1] * v[1] + h.m[3 * c + 2] * v[2];
}

}  // namespace

bool shootRaySphere(const float o[3], const float dir[3], const float c[3], float r,
                    float& tIn, float& tOut) {
    // v13/v14/v7 = o - c;  v9 = dir . (o - c);
    // v10 = v9*v9 - (|o - c|^2 - r*r);  hit when v10 >= 0
    const double ox = double(o[0]) - c[0], oy = double(o[1]) - c[1], oz = double(o[2]) - c[2];
    const double b = dir[0] * ox + dir[1] * oy + dir[2] * oz;
    const double disc = b * b - (ox * ox + oy * oy + oz * oz - double(r) * r);
    if (disc < 0.0) return false;
    const double s = std::sqrt(disc);
    tOut = static_cast<float>(s - b);
    tIn  = static_cast<float>(-b - s);
    return true;
}

bool shootRayBox(const float lo[3], const float hi[3], const float o[3],
                 const float dir[3], float hit[3]) {
    // quadrant: 0 = the start is ABOVE the box on this axis (the plane met is
    // its maximum), 1 = BELOW it (the minimum), 2 = between its two planes
    int quad[3];
    float cand[3] = {0, 0, 0};
    bool inside = true;
    for (int i = 0; i < 3; ++i) {
        if (o[i] >= lo[i]) {
            if (o[i] <= hi[i]) quad[i] = 2;
            else { quad[i] = 0; cand[i] = hi[i]; inside = false; }
        } else {
            quad[i] = 1; cand[i] = lo[i]; inside = false;
        }
    }
    if (inside) {
        for (int i = 0; i < 3; ++i) hit[i] = o[i];
        return true;
    }
    float maxT[3];
    for (int i = 0; i < 3; ++i)
        maxT[i] = (quad[i] == 2 || dir[i] == 0.0f) ? -1.0f : (cand[i] - o[i]) / dir[i];
    int which = 0;
    for (int i = 1; i < 3; ++i)
        if (maxT[which] < maxT[i]) which = i;
    if (maxT[which] < 0.0f) return false;
    for (int i = 0; i < 3; ++i) {
        if (i == which) { hit[i] = cand[i]; continue; }
        hit[i] = dir[i] * maxT[which] + o[i];
        if (quad[i] == 0 && hit[i] < lo[i]) return false;   // only the lower bound
        if (quad[i] == 1 && hit[i] > hi[i]) return false;   // only the upper bound
        // quad 2: not checked - the engine's own omission
    }
    return true;
}

bool shootSweepBodies(const float a[3], const float b[3],
                      const std::vector<HitBody>& bodies, int exclude, BodyHit& out) {
    float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float len = static_cast<float>(std::sqrt(double(d[0]) * d[0] + double(d[1]) * d[1] +
                                                   double(d[2]) * d[2]));
    if (len <= 0.000099999997f) return false;
    for (float& v : d) v /= len;
    float best = FLT_MAX;                 // `dword_53AA40 = 0x7F7FFFFF`
    for (std::size_t bi = 0; bi < bodies.size(); ++bi) {
        const HitBody& body = bodies[bi];
        if (body.actor == exclude) continue;
        if (body.root < 0 || static_cast<std::size_t>(body.root) >= body.meshes.size()) continue;
        // the NODE's sphere: `*v13 + 36/40/44` and radius `+88`, no centre offset
        const HitMesh& root = body.meshes[static_cast<std::size_t>(body.root)];
        float tIn = 0, tOut = 0;
        if (!shootRaySphere(a, d, root.pos, root.radius, tIn, tOut)) continue;
        if (!(tIn <= len && tOut >= 0.0f)) continue;
        for (std::size_t mi = 0; mi < body.meshes.size(); ++mi) {
            const HitMesh& h = body.meshes[mi];
            float wc[3];
            toWorld(h, h.centre, wc);
            if (!shootRaySphere(a, d, wc, h.radius, tIn, tOut)) continue;
            if (!(tIn <= len && tOut >= 0.0f)) continue;
            const float rel[3] = {a[0] - h.pos[0], a[1] - h.pos[1], a[2] - h.pos[2]};
            float s[3], ld[3], hit[3];
            toLocalDir(h, rel, s);
            toLocalDir(h, d, ld);
            if (!shootRayBox(h.boxMin, h.boxMax, s, ld, hit)) continue;
            const double dx = double(hit[0]) - s[0], dy = double(hit[1]) - s[1],
                         dz = double(hit[2]) - s[2];
            const float dist = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
            if (dist <= len && dist < best) {
                best = dist;
                out.body = static_cast<int>(bi);
                out.actor = body.actor;
                out.mesh = static_cast<int>(mi);
                out.dist = dist;
                // `dword_53AA60..68` is the hit in the MESH's frame offset by
                // its origin, which the engine then treats as a world point
                // for the impact; this reports the world point instead
                toWorld(h, hit, out.at);
            }
        }
    }
    return best != FLT_MAX;
}

int shootHitBand(float victimYawDeg, const float boltVel[3]) {
    float m[9];
    shootEulerMatrix(0.0f, static_cast<float>(double(victimYawDeg) * 0.0174532925199433),
                     0.0f, m);
    const float mz[3] = {0.0f, 0.0f, -1.0f};
    float fwd[3];
    shootRotateRow(mz, m, fwd);
    const float len = static_cast<float>(std::sqrt(double(boltVel[2]) * boltVel[2] +
                                                   double(boltVel[0]) * boltVel[0]));
    if (len == 0.0f) return 0;        // the engine's NaN compares unordered: band 0
    const float vx = boltVel[0] / len, vz = boltVel[2] / len;
    const double dot = double(vx) * fwd[0] + double(vz) * fwd[2];
    if (std::fabs(dot) <= 0.5) return dot > 0.0 ? 1 : 0;
    return dot > 0.0 ? 3 : 2;
}

int shootDeathClipType(int band) {
    switch (band) {
        case 0: return 6;
        case 1: return 7;
        case 2: return 5;
        case 3: return 8;
        default: return 5;
    }
}

HitOut shootApplyHit(ShootRecord& r, const HitIn& in) {
    HitOut o;
    // `if (v9 != player && entry+44 != player) return -1;`
    if (!in.victimIsPlayer && !in.shooterIsPlayer) return o;
    // `if (u32i(v9, 101) != 3) return -1;`
    if (!in.victimInShoot) return o;
    // `if ((v10 & 0x800) != 0) return -1;`
    if (r.flags & 0x800u) return o;
    int dmg = in.damage;
    if (!(r.flags & 0x4000u)) {
        // damage 6 from the player is refused unless the victim is type 11
        if (dmg == 6 && in.shooterIsPlayer && r.type != 11u) return o;
    } else {
        if (dmg != 6) return o;
        // types 13 and 10 go through `sub_47FD90` / `sub_47DF60`, unread
    }
    o.refused = false;
    o.healthWas = o.health = r.health;
    if (r.health <= 0) return o;                  // `return v30`, and nothing else
    if (dmg > 0 && in.victimIsPlayer) {
        const int p = std::min(in.bodyShield, 100);
        dmg += dmg * p / -100;                    // C's truncation, as the engine's
        if (!dmg) dmg = 1;
    }
    r.flags |= 0x1020u;
    o.band = shootHitBand(in.victimYaw, in.boltVel);
    r.health -= dmg;
    o.damage = dmg;
    o.health = r.health;
    if (in.victimIsPlayer) {                      // `sub_423FC0` / `sub_47D1F0`
        o.killed = r.health <= 0;
        return o;
    }
    if (r.health > 0) {
        // `sub_423EF0`
        if (r.scriptStep != 8 && !(r.flags & 2u) && !(r.flags & 0x4000u) && r.type != 12u) {
            if (r.health > in.reactAt) {
                if (r.hitAction != -1) { r.scriptStep = r.hitAction; o.action = r.hitAction; }
            } else {
                r.scriptStep = 4;
                o.action = 4;
            }
        }
        return o;
    }
    o.killed = true;
    if (!(r.flags & 2u)) o.enemyCountDrop = true;
    o.deathType = shootDeathClipType(o.band);
    r.flags |= 8u;
    return o;
}

}  // namespace omk

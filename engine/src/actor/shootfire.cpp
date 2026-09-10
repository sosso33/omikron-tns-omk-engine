// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/shootfire.h"

#include "actor/shoot.h"
#include "actor/state.h"
#include "platform/json.h"

#include <cctype>
#include <cmath>

namespace omk {

ShootWeaponTable ShootWeaponTable::loadJson(const std::string& path) {
    ShootWeaponTable t;
    if (path.empty()) return t;
    const Json j = Json::parseFile(path);
    const Json& rows = j["rows"];
    auto read = [](const Json& side, std::vector<ShootWeaponRow>& out) {
        const Json& rs = side["rows"];
        for (std::size_t i = 0; i < rs.size(); ++i) {
            const Json& r = rs[i];
            ShootWeaponRow w;
            w.rate      = static_cast<float>(r["f0"].num());
            w.speed     = static_cast<float>(r["f1"].num());
            w.damage    = static_cast<int>(r["i2"].i64());
            w.key       = static_cast<int>(r["key"].i64(-1));
            w.ammoIndex = static_cast<int>(r["index"].i64());
            out.push_back(w);
        }
    };
    read(rows["player"], t.player_);
    read(rows["other"], t.other_);
    return t;
}

const ShootWeaponRow* ShootWeaponTable::find(int type, bool player) const {
    // `v8 = v7 + 3; if (v7[3] != -1) { while (*v9 != v4) { ...; if (*v8 == -1)
    // goto none; } }` - the key column, walked until the -1 row.
    for (const auto& w : rows(player)) {
        if (w.key == -1) return nullptr;
        if (w.key == type) return &w;
    }
    return nullptr;
}

int shootWeaponType(int objectKind, const std::string& modelName) {
    // `if (v16 == 1) { v5 = name; strupr(v5); if (v5[strlen(v5) - 11] == 'B')
    // v4 = -2; }` - the engine indexes without a length test; a name shorter
    // than 11 reads before the string, which this does not reproduce.
    if (objectKind != 1) return objectKind;
    if (modelName.size() < 11) return objectKind;
    const char c = static_cast<char>(
        std::toupper(static_cast<unsigned char>(modelName[modelName.size() - 11])));
    return c == 'B' ? -2 : objectKind;
}

bool mdShoot0(ShotLatch& l, int actorState) {
    if (actorState != static_cast<int>(ActorState::Shoot)) return false;
    l.latch = true;
    return true;
}

FireGate shootFireGate(ShootRecord& r, bool pulled, float dt) {
    // `v65 = i32(a4, 180); if (as_f32(v65) != 0.0) { ... }` - no weapon row,
    // no gate: the function returns the null pointer it read.
    if (!r.weapon) return FireGate::Released;
    r.flags |= 0x80000000u;
    if (pulled || (r.flags & kShootPullPending)) {
        FireGate out = FireGate::Pulled;
        if (!(r.flags & kShootPullCounted)) r.flags |= kShootPullPending | kShootPullCounted;
        const double step = double(dt) * 0.2;
        if (r.flags & kShootFired) {
            const double v = step + r.weaponLowered;
            r.weaponLowered = static_cast<float>(v);
            if (v > 0.0) { r.weaponLowered = 0.0f; r.flags &= ~kShootFired; }
        } else {
            const double v = double(r.weaponLowered) - step;
            r.weaponLowered = static_cast<float>(v);
            if (v < 0.0) r.weaponLowered = 0.0f;
        }
        if (r.weaponLowered == 0.0f) {
            // `if (f32(v65, 0) == f32(v4, 172))` - an exact float compare
            if (r.weapon->rate == r.weaponTimer) {
                out = FireGate::Fired;
                r.flags |= kShootFired;
            }
            const double t = double(r.weaponTimer) - dt;
            r.weaponTimer = static_cast<float>(t);
            if (t < 0.0) r.weaponTimer = 0.0f;
        }
        if (out == FireGate::Fired) r.flags &= ~kShootPullPending;
        return out;
    }
    // released: the countdown runs down to a FLOOR of 1.0 - so a released
    // gun is never reloaded by the caller's `<= 0` test - and the weapon
    // lowers at half the speed it rises
    const double t = double(r.weaponTimer) - dt;
    r.weaponTimer = static_cast<float>(t);
    if (t < 1.0) r.weaponTimer = 1.0f;
    const double v = double(dt) * 0.1 + r.weaponLowered;
    r.weaponLowered = static_cast<float>(v);
    if (v > 1.0) { r.weaponLowered = 1.0f; r.flags &= ~kShootFired; }
    r.flags &= ~(kShootPullPending | kShootPullCounted);
    return FireGate::Released;
}

FireGate shootChannelTick(ShootRecord& r, ShotLatch& l, bool shootPose,
                          bool isPlayer, float dt) {
    if (!shootPose) {
        // the branch every other state takes: the latch IS the request, with
        // no rate and no raise
        if (l.latch) { l.request = true; l.latch = false; }
        return FireGate::Released;
    }
    // `v21 = rec+180; if (v21 && f32(rec,172) <= 0) rec+172 = *v21;`
    if (r.weapon && r.weaponTimer <= 0.0f) r.weaponTimer = r.weapon->rate;
    // `if (latch && v21) gate(.., 1, ..) else gate(.., 0, ..); latch = 0`
    const FireGate g = shootFireGate(r, l.latch && r.weapon, dt);
    l.latch = false;
    if (g == FireGate::Fired && isPlayer) l.request = true;
    return g;
}

void shootEulerMatrix(float a1, float a2, float a3, float m[9]) {
    // 0x00442160, written out as the decompilation has it: m is row-major,
    // m[0..2] the dwords at +0/+4/+8.
    const float v4 = std::sin(a3), v5 = std::cos(a1), v6 = std::cos(a2);
    const float v8 = v4, v10 = std::sin(a2), v14 = std::cos(a3);
    const float v12 = std::sin(a1), v13 = v5, v11 = v4 * v10, v9 = v6;
    m[0] = v14 * v9;
    m[3] = v8 * v9;
    m[6] = -v10;
    m[1] = v10 * v12 * v14 - v8 * v13;
    m[4] = v12 * v11 + v14 * v13;
    m[7] = v12 * v9;
    m[2] = v10 * v14 * v13 + v8 * v12;
    m[5] = v13 * v11 - v12 * v14;
    m[8] = v9 * v13;
}

void shootRotateRow(const float v[3], const float m[9], float out[3]) {
    // 0x00442D70: `a5 = m0*a1 + m6*a3 + m3*a2`, and so on - v times M
    out[0] = m[0] * v[0] + m[6] * v[2] + m[3] * v[1];
    out[1] = m[7] * v[2] + m[4] * v[1] + m[1] * v[0];
    out[2] = m[8] * v[2] + m[5] * v[1] + m[2] * v[0];
}

void shootShotDirection(float yawDeg, float pitchDeg, float dir[3]) {
    // `v54 = -sub_47D4C0() * 0.0174532925199433; v29 = f32(v1, 420);
    //  v52 = (v29 - -90.0) * 0.0174532925199433;
    //  sub_442160(0.0, v52, v54, v57); v63 = {-1, 0, 0}`
    const float pitch = static_cast<float>(-double(pitchDeg) * 0.0174532925199433);
    const float yaw = static_cast<float>((double(yawDeg) - -90.0) * 0.0174532925199433);
    float m[9];
    shootEulerMatrix(0.0f, yaw, pitch, m);
    const float v[3] = {-1.0f, 0.0f, 0.0f};
    shootRotateRow(v, m, dir);
}

}  // namespace omk

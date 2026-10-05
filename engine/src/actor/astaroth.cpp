// SPDX-License-Identifier: GPL-3.0-or-later
// Astaroth's fight (`actor/astaroth.h`, `todo/astaroth.md`).
#include "actor/astaroth.h"

#include <cmath>

namespace omk {

void astarothSetup(AstarothFight& fight, ShootRecord& rec,
                   const std::function<int(const char*)>& findSoul, int backMesh,
                   int difficulty) {
    fight.destroyed = 0;                 // dword_657AFC = 0
    rec.state = 29;                      // +156
    rec.actionCounter = 10;              // +88
    rec.flags |= 0x4020u;                // +160
    fight.callbackArmed = true;          // sub_44CD90(sub_47FCF0)
    fight.backMesh = backMesh;           // dword_657AF4
    for (int i = 0; i < kAstarothSouls; ++i) {
        fight.soulMesh[i] = findSoul ? findSoul(kAstarothSoulNames[i]) : -1;
        fight.hits[i] = kAstarothSoulHits;
    }
    fight.rate = 1.0f;                   // flt_6A062C
    // `word_90E1A8` 0 / 1 / 2; any other value leaves the factor as it was
    if (difficulty == 0)      fight.difficulty = 1.0f;
    else if (difficulty == 1) fight.difficulty = 1.2f;
    else if (difficulty == 2) fight.difficulty = 1.4f;
}

SoulHit astarothSoulHit(AstarothFight& fight, int mesh) {
    SoulHit out;
    int i = kAstarothSouls - 1;
    for (; i >= 0; --i)
        if (fight.soulMesh[i] >= 0 && fight.soulMesh[i] == mesh) break;
    if (i >= 0) {
        out.soul = i;
        out.hitsLeft = --fight.hits[i];
        if (fight.hits[i] == 0) {
            ++fight.destroyed;
            fight.soulMesh[i] = -1;
            out.destroyed = true;
            out.message = kAstarothSoulMessages[i];
        }
    }
    if (fight.destroyed == kAstarothSouls && fight.callbackArmed) {
        fight.callbackArmed = false;     // sub_44CD90(0)
        out.disarmed = true;
    }
    return out;
}

AstarothBand astarothBand(AstarothFight& fight, int health, float dt) {
    AstarothBand b;
    // `cmp eax, 0x64; jge` then `cmp eax, 0x32; jge` - each band on `<`
    if (health < 100) { b.rate = 1.5f; b.wait = 40.0f; }
    if (health < 50)  { b.rate = 2.0f; b.wait = 30.0f; }
    // `flds flt_657AF8; fmul st(1); fmuls flt_4C30D8; fstps flt_6A062C` -
    // x87, so the product is kept wide and rounded once at the store
    fight.rate = static_cast<float>(double(fight.difficulty) * double(b.rate) * double(dt));
    b.rate = fight.rate;
    b.wait = static_cast<float>(double(b.wait) / double(fight.difficulty));
    return b;
}

AstarothGate astarothGate(const AstarothFight& fight, ShootRecord& rec, int damage,
                          float yawDeg, const float seg[6],
                          const std::function<bool()>& backSweep) {
    AstarothGate g;
    const int s = rec.state;
    if (!((s < 17 || s > 19) && fight.destroyed >= kAstarothSouls)) return g;
    const double y = double(yawDeg) * 0.0174532925199433;
    const double dx = double(seg[3]) - seg[0], dz = double(seg[5]) - seg[2];
    g.fromBehind = std::sin(y) * dx + -std::cos(y) * dz > 0.0;
    if (g.fromBehind && backSweep && backSweep()) {
        rec.flags |= 8u;
        rec.flags |= 0x800u;
        rec.state = 16;
        rec.repeats = 0;
        rec.actionCounter = 10;
        g.reaction = AstarothGate::Reaction::Back;
        g.damage = damage;
        return g;
    }
    if (!(rec.flags & 8u)) --rec.actionCounter;
    if (rec.state != 21 && rec.actionCounter <= 0) {
        rec.flags |= 8u;
        rec.state = 16;
        rec.repeats = 10;
        rec.actionCounter = 10;
        g.reaction = AstarothGate::Reaction::Flinch;
    }
    return g;
}

AstarothClipOver astarothClipOver(ShootRecord& rec) {
    if (--rec.repeats > 0) {
        rec.flags |= 8u;
        return AstarothClipOver::Replay;
    }
    if (rec.health <= 0) return AstarothClipOver::Dead;
    return AstarothClipOver::Resume;
}

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
// Astaroth's fight (`actor/astaroth.h`, `todo/astaroth.md`).
#include "actor/astaroth.h"

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

}  // namespace omk

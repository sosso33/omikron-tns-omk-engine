// SPDX-License-Identifier: GPL-3.0-or-later
// Astaroth's fight (`actor/astaroth.h`, `todo/astaroth.md`).
#include "actor/astaroth.h"

#include "actor/player.h"      // rotateYaw
#include "actor/shootfire.h"   // shootEulerMatrix, shootRotateRow

#include <cmath>
#include <cstdint>
#include <cstring>

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

namespace {

// `Anim_RootDelta` of `id` between two frames, turned by his facing (the
// node's matrix, `node+156`)
void turnedDelta(const AstarothWorld& w, int id, float t0, float t1, float facing, float out[3]) {
    float d[3] = {0.0f, 0.0f, 0.0f};
    if (w.rootDelta) w.rootDelta(id, t0, t1, d);
    rotateYaw(facing, d, out);
}

// `sub_472820` (0x00472820), the grid's root delta: four `Anim_RootDelta`s at
// the four key offsets, mixed with the same two weights as the pose -
//   A = (d10 * wp + d8 * (256 - wp)) / 256,  B = (d12 * wp + d14 * (256 - wp)) / 256
//   out = (A * (256 - wy) + B * wy) / 256
void gridDelta(const AstarothActor& a, float t0, float t1, float facing,
               const AstarothWorld& w, float out[3]) {
    const AstarothGrid& g = a.blend;
    float d8[3], d10[3], d12[3], d14[3];
    turnedDelta(w, a.clip, t0 + float(g.off8),  t1 + float(g.off8),  facing, d8);
    turnedDelta(w, a.clip, t0 + float(g.off10), t1 + float(g.off10), facing, d10);
    turnedDelta(w, a.clip, t0 + float(g.off12), t1 + float(g.off12), facing, d12);
    turnedDelta(w, a.clip, t0 + float(g.off14), t1 + float(g.off14), facing, d14);
    const double wp = std::abs(g.wPitch), wq = 256 - std::abs(g.wPitch);
    const double wy = std::abs(g.wYaw),   wz = 256 - std::abs(g.wYaw);
    for (int k = 0; k < 3; ++k) {
        const float A = static_cast<float>((double(d10[k]) * wp + double(d8[k]) * wq) * 0.00390625);
        const float B = static_cast<float>((double(d12[k]) * wp + double(d14[k]) * wq) * 0.00390625);
        out[k] = static_cast<float>((double(A) * wz + double(B) * wy) * 0.00390625);
    }
}

// `o3de_MoveNodeBy` with the record kept beside it, as every helper does
void moveBoth(AstarothActor& a, const float d[3]) {
    for (int k = 0; k < 3; ++k) { a.pos[k] += d[k]; a.node[k] += d[k]; }
}

// `sub_434C30` + `sub_434D30` for a PLAIN clip: the frame advanced by the
// animation rate, and the root moved by its delta prev -> frame
void plainStep(AstarothActor& a, const AstarothWorld& w, float facing) {
    float d[3];
    turnedDelta(w, a.clip, a.prev, a.frame, facing, d);
    moveBoth(a, d);
}

}  // namespace

void astarothAim(AstarothActor& a, float facing, const AstarothWorld& w) {
    float sh[3] = {0, 0, 0}, pl[3] = {0, 0, 0};
    if (w.shoulder) w.shoulder(sh);
    if (w.player) w.player(pl);
    float m[9];
    shootEulerMatrix(0.0f, static_cast<float>(double(facing) * 0.0174532925199433), 0.0f, m);
    const float mz[3] = {0.0f, 0.0f, -1.0f};
    float f[3];
    shootRotateRow(mz, m, f);
    const float dx = pl[0] - sh[0], dy = pl[1] - sh[1], dz = pl[2] - sh[2];
    const float dz2 = dz * dz, dx2 = dx * dx;
    const float d3 = static_cast<float>(std::sqrt(double(dy) * dy + dz2 + dx2));
    const float dh = static_cast<float>(std::sqrt(double(dz2) + dx2));
    if (d3 == 0.0f || dh == 0.0f) return;              // `return 0`: the blend stands
    const float c = static_cast<float>((double(f[2]) * dz + double(f[0]) * dx) / dh);
    float yaw = static_cast<float>(std::acos(double(c)) * 180.0 * 0.3183098861837907);
    if (double(f[0]) * dz - double(f[2]) * dx < 0.0) yaw = -yaw;
    if (std::fabs(yaw) > 45.0f) yaw = yaw < 0.0f ? -45.0f : 45.0f;
    a.aimYaw = yaw;
    a.aimPitch = static_cast<float>(std::asin(double(-dy) / d3) * 180.0 * 0.3183098861837907);
    // `sub_4B3260(him, +1272)` with +1268 = 9
    float p = a.aimPitch, y = a.aimYaw;
    int row, col;
    if (p < 0.0f) { if (p < -33.0f) p = -33.0f; row = 6; }
    else          { if (!(p <= 33.0f)) p = 33.0f; row = 0; }
    if (y < 0.0f) { if (y < -45.0f) y = -45.0f; col = 0; }
    else          { if (!(y <= 45.0f)) y = 45.0f; col = 2; }
    const unsigned L = (static_cast<unsigned>(a.clipFrames) + 1u) / 9u;
    AstarothGrid& g = a.blend;
    g.off8  = static_cast<int>(4u * L);
    g.off14 = static_cast<int>(L * static_cast<unsigned>(col + 3));
    g.off10 = static_cast<int>(L * static_cast<unsigned>(row + 1));
    g.off12 = static_cast<int>(L * static_cast<unsigned>(row + col));
    const double pk = double(p) * 256.0;
    g.wPitch = static_cast<int>(static_cast<std::int64_t>(
        pk * (p <= 0.0f ? double(-0.030303031f) : double(0.030303031f))));
    g.wYaw = static_cast<int>(static_cast<std::int64_t>(
        std::fabs(double(y) * 256.0 * double(0.022222223f))));
    if (g.wYaw > 256) g.wYaw = 256;
    if (g.wPitch > 256) g.wPitch = 256;
    if (g.wYaw < 0) g.wYaw = 0;
    if (g.wPitch < 0) g.wPitch = 0;
}

void astarothStartGrid(AstarothActor& a, ShootRecord& rec, int id, float facing,
                       const AstarothWorld& w) {
    a.prevClip = a.clip;                         // rec+12 = rec+8
    a.clip = id;
    a.clipFrames = w.clipFrames ? w.clipFrames(id) : 0;
    a.grid = true;
    a.prev = 0.0f;                               // actor+192
    a.frame = 1.0f;                              // actor+188
    a.blend = AstarothGrid{};                    // the shorts and +1272/+1276 zeroed
    // `sub_4725B0(node, desc, 0.0, 1.0, d, blend)` - every offset 0, so
    // cell 0's frame 0 -> 1
    float d[3];
    gridDelta(a, 0.0f, 1.0f, facing, w, d);
    a.pos[0] += d[0];
    a.pos[2] += d[2];
    a.node[0] = a.pos[0];
    a.node[1] = rec.groundY + d[1];              // `rec+60 + dy`
    a.node[2] = a.pos[2];
    a.pos[1] = a.node[1];
    astarothAim(a, facing, w);
}

void astarothStartClip(AstarothActor& a, ShootRecord& rec, int id, float facing,
                       const AstarothWorld& w) {
    a.clip = id;
    a.clipFrames = w.clipFrames ? w.clipFrames(id) : 0;
    a.grid = false;
    a.prev = 0.0f;
    a.frame = 1.0f;
    float d[3];
    turnedDelta(w, id, 0.0f, 1.0f, facing, d);
    a.node[0] = a.pos[0];                        // `o3de_SetNodePos(node, +244,
    a.node[1] = rec.groundY + d[1];              //   rec+60 + dy, +252)`
    a.node[2] = a.pos[2];
}

void astarothResume(AstarothActor& a, ShootRecord& rec, float facing, const AstarothWorld& w) {
    switch (rec.state) {
        case 16: astarothStartGrid(a, rec, 1, facing, w); break;
        case 17: astarothStartClip(a, rec, 4, facing, w); break;
        case 21: astarothStartClip(a, rec, 11, facing, w); break;
        case 29: astarothStartGrid(a, rec, 13, facing, w); break;
        default:
            // `+160 &= ~8; sub_421A20(+12, 0)` - his previous clip back
            if (a.prevClip >= 0) astarothStartClip(a, rec, a.prevClip, facing, w);
            break;
    }
}

namespace {

// `sub_4B33C0` (0x004B33C0): one tick of a grid clip, states 16 and 29.
// -> 1 when a cell has played out (the clip wrapped).
bool gridStep(AstarothActor& a, ShootRecord& rec, float rate, float& facing,
              const AstarothWorld& w) {
    a.prev = a.frame;
    a.frame = a.frame + rate;
    float target[3] = {0, 0, 0};
    if (w.player) w.player(target);
    const unsigned L = (static_cast<unsigned>(a.clipFrames) + 1u) / 9u;
    const float last = static_cast<float>(static_cast<int>(L) - 1);
    bool wrapped = false;
    if (a.frame >= last) {
        // `o3de_SetNodePos(node, +244, rec+60, +252)`
        a.node[0] = a.pos[0]; a.node[1] = rec.groundY; a.node[2] = a.pos[2];
        a.frame = static_cast<float>(double(a.frame) - -1.0) - last;
        a.prev = 0.0f;
        wrapped = true;
    }
    // FIRE SLOT 0 as the cell's middle frame is crossed
    const std::int64_t half = static_cast<std::int64_t>(L / 2u);
    if (half > static_cast<std::int64_t>(a.prev) && half <= static_cast<std::int64_t>(a.frame) &&
        w.fire)
        w.fire(0, target);
    astarothAim(a, facing, w);
    if (rec.state != 29 && w.fieldTurn) {
        int type = -1;
        float per = 0.0f;
        if (w.fieldTurn(facing, type, per) && type >= 0) {
            a.queuedTurnType = type;
            a.queuedTurn = per;
        }
    }
    float d[3];
    gridDelta(a, a.prev, a.frame, facing, w, d);
    moveBoth(a, d);                              // no wall test: `o3de_MoveNodeBy`
    if (w.cell) w.cell(a.node);
    return wrapped;
}

// `sub_4B39F0` (state 17): the wind-up, id 4. -> 1 at its end, with id 12
// started and `+168 = 30`.
bool windUpStep(AstarothActor& a, ShootRecord& rec, float rate, float facing,
                const AstarothWorld& w) {
    a.prev = a.frame;
    a.frame = a.frame + rate;
    if (a.frame < static_cast<float>(a.clipFrames)) {
        plainStep(a, w, facing);
        return false;
    }
    astarothStartClip(a, rec, 12, facing, w);
    rec.timer = 30.0f;
    return true;
}

// `sub_4B3B40` (state 18): crouched, id 12 looping, until the player's cell
// is in sight and not refused (or refused with the timer run out) - then the
// LEAP, id 5, and its step toward where the player stands now.
bool crouchStep(AstarothActor& a, ShootRecord& rec, float rate, float facing,
                const AstarothWorld& w) {
    bool sight = false, refused = false;
    if (w.leapCheck) w.leapCheck(sight, refused);
    if (!sight || (refused && rec.timer >= 0.0f)) {
        a.prev = a.frame;
        a.frame = a.frame + rate;
        if (static_cast<float>(a.clipFrames) <= a.frame) {
            a.frame = static_cast<float>(double(rate) - -1.0);
            a.prev = 0.0f;
            // `actor+248 += rec+60 - rootMesh+40; o3de_SetNodePos(node,
            // pos.x, rec+60, pos.z)` - pos is the node's, read at the head
            a.pos[1] += rec.groundY - a.node[1];
            a.node[1] = rec.groundY;
        }
        plainStep(a, w, facing);
        return false;
    }
    astarothStartClip(a, rec, 5, facing, w);
    const float n = static_cast<float>(static_cast<unsigned>(a.clipFrames));
    if (refused) {
        a.leap[0] = 0.0f;
        a.leap[1] = 0.0f;
    } else {
        float p[3] = {0, 0, 0};
        if (w.player) w.player(p);
        a.leap[0] = (p[0] - a.pos[0]) / n;       // the PLAYER's +244 less his
        a.leap[1] = (p[2] - a.pos[2]) / n;
    }
    return true;
}

// `sub_4B3E10` (state 19): the leap. The record moves by the step times the
// ANIMATION RATE, the node by the step times `flt_4C30D8` - as shipped.
// -> 1 at its end, the landing (id 6) queued.
bool leapStep(AstarothActor& a, ShootRecord& rec, float rate, float dt, float facing,
              const AstarothWorld& w) {
    a.prev = a.frame;
    a.frame = a.frame + rate;
    if (a.frame < static_cast<float>(a.clipFrames)) {
        float d[3];
        turnedDelta(w, a.clip, a.prev, a.frame, facing, d);
        a.pos[0] = a.leap[0] * rate + a.pos[0];
        a.pos[1] = d[1] + a.pos[1];
        a.pos[2] = a.leap[1] * rate + a.pos[2];
        a.node[0] += a.leap[0] * dt;
        a.node[1] += d[1];
        a.node[2] += a.leap[1] * dt;
        if (w.cell) w.cell(a.node);
        return false;
    }
    a.queued = 6;
    a.queuedTurnType = -1;
    a.queuedTurn = 0.0f;                         // `+184 = 0`
    (void)rec;
    return true;
}

// `sub_4B3800` (state 21): the big shot, id 11 - slot 1 fired as frame 2 is
// crossed, the turn toward the player while he is not aimed. -> 1 at its end.
bool shotStep(AstarothActor& a, ShootRecord& rec, float rate, float& facing,
              const AstarothWorld& w) {
    a.prev = a.frame;
    a.frame = a.frame + rate;
    float target[3] = {0, 0, 0};
    if (w.player) w.player(target);
    if (static_cast<float>(a.clipFrames) <= a.frame) return true;
    if (static_cast<int>(static_cast<std::int64_t>(a.prev)) < 2 &&
        static_cast<int>(static_cast<std::int64_t>(a.frame)) >= 2 && w.fire)
        w.fire(1, target);
    if (w.aimed && !w.aimed(a.node, target) && w.turnToTarget) w.turnToTarget(facing);
    plainStep(a, w, facing);
    (void)rec;
    return false;
}

}  // namespace

void astarothTireSlots(const std::vector<Mesh>& meshes, int out[4]) {
    for (int k = 0; k < 4; ++k) out[k] = -1;
    const auto at = [&](std::int32_t id) -> int {
        for (std::size_t i = 0; i < meshes.size(); ++i)
            if (meshes[i].id == id) return static_cast<int>(i);
        return -1;
    };
    int found = 0;
    // the walk is iterative so a malformed link cannot recurse for ever
    std::vector<int> stack;
    for (std::size_t i = 0; i < meshes.size(); ++i)
        if (meshes[i].parent < 0) { stack.push_back(static_cast<int>(i)); break; }
    std::vector<std::uint8_t> seen(meshes.size(), 0);
    while (!stack.empty() && found < 4) {
        const int m = stack.back();
        stack.pop_back();
        if (m < 0 || seen[static_cast<std::size_t>(m)]) continue;
        seen[static_cast<std::size_t>(m)] = 1;
        const Mesh& me = meshes[static_cast<std::size_t>(m)];
        if (std::strncmp(me.name, "Tire", 4) == 0) out[found++] = m;
        // pre-order: the children, first child first - pushed in reverse
        std::vector<int> kids;
        for (int c = at(me.child); c >= 0 && kids.size() < meshes.size();
             c = at(meshes[static_cast<std::size_t>(c)].next))
            kids.push_back(c);
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) stack.push_back(*it);
    }
}

void astarothTick(AstarothFight& fight, AstarothActor& a, ShootRecord& rec,
                  const AstarothBand& band, float dt, float& facing,
                  const AstarothWorld& w) {
    const float rate = band.rate;
    // 0x4802ED: `if (dword_657AF0) sub_44F020(dword_657AF0, wait)`
    if (w.slot1Wait) w.slot1Wait(band.wait);
    rec.repeats = 0;                             // +100
    a.queued = -1;                               // +16
    a.queuedTurnType = -1;
    rec.flags &= ~0x800u;
    float target[3] = {0, 0, 0};
    switch (rec.state) {
    case 16:                                     // WALK at the player
        if (gridStep(a, rec, rate, facing, w)) {
            astarothStartGrid(a, rec, 1, facing, w);
            // `Actor_GetPosAndFacing(target)` (unused), then the footstep
            if (w.shake) w.shake(30.0f, 10);
        }
        if (w.player) w.player(target);
        {
            const double dx = double(a.node[0]) - target[0], dz = double(a.node[2]) - target[2];
            if (std::sqrt(dx * dx + dz * dz) < 195.0) {
                astarothStartClip(a, rec, 4, facing, w);   // `sub_4B39A0`
                rec.state = 17;
            }
        }
        break;
    case 17:                                     // the WIND-UP
        if (windUpStep(a, rec, rate, facing, w)) rec.state = 18;
        if (w.stamp) w.stamp();
        break;
    case 18:                                     // CROUCHED
        if (crouchStep(a, rec, rate, facing, w)) rec.state = 19;
        if (w.stamp) w.stamp();
        break;
    case 19:                                     // the LEAP
        if (leapStep(a, rec, rate, dt, facing, w)) {
            if (w.player) w.player(target);
            const double dx = double(a.node[0]) - target[0], dz = double(a.node[2]) - target[2];
            const double d = std::sqrt(dx * dx + dz * dz);
            if (d < 273.0 && w.strike) {
                // the SLAM: `sub_423B10(target, dmg, {t - pos, 0})` by band
                const float dir[3] = {target[0] - a.node[0], 0.0f, target[2] - a.node[2]};
                w.strike(d < 78.0 ? 3700 : d < 156.0 ? 2300 : 1200, dir);
            }
            rec.timer = 150.0f;
            rec.state = 21;
            astarothStartClip(a, rec, 11, facing, w);      // `sub_4B37C0`
        }
        if (w.stamp) w.stamp();
        break;
    case 20:
        // FACE THE PLAYER - nothing of his writes state 20 (`todo/astaroth.md`);
        // not ported, LABELLED
        break;
    case 21:                                     // the BIG SHOT
        if (shotStep(a, rec, rate, facing, w)) {
            rec.timer = 60.0f;
            rec.state = 16;
            astarothStartGrid(a, rec, 1, facing, w);
        }
        break;
    case 27:
        break;                                   // inert
    case 29:                                     // STAND while souls remain
        if (gridStep(a, rec, rate, facing, w)) {
            if (fight.destroyed >= kAstarothSouls) {
                astarothStartGrid(a, rec, 1, facing, w);
                rec.state = 16;
            } else {
                astarothStartGrid(a, rec, 13, facing, w);
            }
        }
        break;
    default:
        // 22..26, 28: `sub_47FF70` again and `sub_4B2DF0` - only an outside
        // write reaches these; the re-setup is the caller's (LABELLED: the
        // `sub_4B2DF0` turn-and-walk step is not ported)
        break;
    }
}

}  // namespace omk

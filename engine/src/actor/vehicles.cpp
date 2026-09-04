// SPDX-License-Identifier: GPL-3.0-or-later
// THE ROAD TRAFFIC - the vehicle half of the `.OPT` traffic circuit
// (docs/STREET_LIFE.md 2b, todo/road-traffic.md). The hover-taxis (`sli_fn`)
// and the motos share a city's lane network with the procedural pedestrians,
// so this file is the other half of `Sliders`, not a second pool: the
// movers, the occupancy lists and - the part that matters - the reservation
// groups are the same, which is what stops a slider driving through a
// crossing walker.
//
// Transcribed from, in the order the engine runs them:
//
//   `Slider_Init`   (0x00453450) its vehicle half - the two model tables, the
//                   ride pool, the spawner call
//   `sub_4539B0`    the 12-byte model-table walk, shared with the crowd
//   `sub_4543F0`    the spawner over lanes header[2]..header[5], spacing
//                   header[4] - NO density factor
//   `sub_4544B0`    the spawn callback: a mover slot, a coin between the two
//                   kinds, the model quota, the ride record
//   `Sliders_Tick`  (0x00454BB0) its vehicle loop, over the 40 ride records
//   `sub_456530`    the ride state machine; state 0 is ambient traffic and is
//                   what this file drives. States 1..7 are the player
//                   mounting and riding, and are NOT ported (out of scope)
//   `sub_456C70`    the drive: the shared mover step, the gait with the
//                   vehicle thresholds, the acceleration, the body
//   `sub_456B40`    SOUNDS\sliderm01.wav, 3D, inside 585 units
//
// UNITS are the engine's throughout: a speed is units*256 a frame, so a body
// advances `speed * dt / 256`.
#include "actor/sliders.h"

#include <algorithm>
#include <cmath>

namespace omk {

// ------------------------------------------------------------ the tables

const std::vector<std::string>& vehModelTable(int kind) {
    // `aSliFn` and `aMoto` in Runtime 2.exe, the same 12-byte rows as the
    // crowd's tables (an 8-char name, a weight of 1) read out of the image at
    // file offsets 0xC1638 and 0xC14B8. The slider table really does hold the
    // same name twice: row 0 is the slot `sub_4544B0` reserves for the
    // player's own vehicle and row 1 up is the ambient traffic.
    static const std::vector<std::string> sliders = {"sli_fn", "sli_fn"};
    static const std::vector<std::string> motos = {"moto"};
    return kind == 1 ? sliders : motos;
}

// ------------------------------------------------------------ the load

void Sliders::loadVehicles(std::uint32_t sliMask, std::uint32_t motoMask) {
    // `Slider_Init`: `if (v12[2] < v12[5])` - the circuit has vehicle lanes.
    // Lahoreh and the Puits have none, and their masks are 0 as well, so
    // either gate alone would empty their roads.
    if (track_.pedEnd >= track_.laneCount) return;
    // `sub_4539B0(aSliFn, pool, sub_40EA10(area) | 1)` - bit 0 is forced, so
    // the reserved row always loads - and `sub_4539B0(aMoto, pool,
    // sub_40E9D0(area))`, which is not forced.
    const auto& sliTable = vehModelTable(1);
    const auto& motoTable = vehModelTable(0);
    const std::uint32_t sli = sliMask | 1u;
    for (std::size_t i = 0; i < sliTable.size(); ++i)
        if (sli & (1u << i)) ++nSliderModels_;
    for (std::size_t i = 0; i < motoTable.size(); ++i)
        if (motoMask & (1u << i)) ++nMotoModels_;
    if (!nSliderModels_ && !nMotoModels_) return;
    // The 40 ride records. `Slider_Init` allocates them only when a slider
    // model loaded (`if (dword_539934)`) but writes the -1 sound handle into
    // all 40 regardless, and `sub_4543F0` runs when EITHER pool is non-empty -
    // so with sliders absent and motos present the engine walks a null pool.
    // Nothing in the shipped data reaches that: bit 0 of the slider mask is
    // forced, so `nSliderModels_` is never 0 here.
    vehicles_.assign(static_cast<std::size_t>(kMaxVehicles), Vehicle{});
    for (auto& v : vehicles_) v.sound = -1;
    // `sub_4543F0`: the walkers' own spawner over the vehicle lanes, with
    // `u32i(dword_8F5E48, 4)` - the vehicle spacing, undivided. The street
    // activity thins the crowd and never the traffic.
    spawnAlongLanes(track_.pedEnd, track_.laneCount,
                    static_cast<float>(track_.vehSpacing), true);
}

int Sliders::vehicleSpawnCount(const OptTrack& t, bool cap) {
    if (!t.valid || t.pedEnd >= t.laneCount) return 0;
    const float threshold = kSpawnSpacingFactor * static_cast<float>(t.vehSpacing);
    float acc = 0.0f;
    int n = 0;
    for (std::uint32_t li = t.pedEnd; li < t.laneCount; ++li) {
        const auto& L = t.lanes[li];
        for (int k = 0; k < L.keyCount; ++k) {
            const auto& K = t.keys[static_cast<std::size_t>(L.firstKey + k)];
            const float len = len3(K.delta);
            if (acc > threshold) {
                if (cap && ++n >= kMaxVehicles) return kMaxVehicles;
                if (!cap) ++n;
                acc = 0.0f;
            }
            acc += len;
        }
    }
    return n;
}

// `sub_4544B0`'s and `sub_453ED0`'s common opening: the first mover slot with
// flag 2 clear, marked in use. The engine's pool is 240 fixed slots; here it
// grows, and the two callbacks' own caps (200 and 40) are what bound it.
int Sliders::newMover() {
    movers_.push_back(Pedestrian{});
    return static_cast<int>(movers_.size()) - 1;
}

bool Sliders::spawnVehicle(int lane, const float at[3], const float dir[3],
                               float segLen, int keyIndex) {
    // `sub_4544B0(a1)`: `if (a1 >= 40) return 0` - and a 0 return ends the
    // spawner's whole walk.
    int slot = -1;
    for (int i = 0; i < static_cast<int>(vehicles_.size()); ++i)
        if (!vehicles_[static_cast<std::size_t>(i)].live) { slot = i; break; }
    if (slot < 0) return false;
    Vehicle v;
    // The coin: `rand() & 1`, 1 a slider and 0 a moto, forced to the other
    // when a pool is empty. Then `v20 = dword_539934 - 1`: the ambient
    // sliders are rows 1.., so a single slider model (Qalisar's mask of 1)
    // leaves none and every vehicle there is a moto - and with no motos
    // either the callback returns the record's null mover, ending the walk.
    int kind = static_cast<int>(rnd() & 1u);
    if (!nMotoModels_) kind = 1;
    if (nSliderModels_ - 1 <= 0) {
        kind = 0;
        if (!nMotoModels_) return false;
    }
    v.kind = kind;
    const auto& table = vehModelTable(kind);
    // `sub_453E80` never runs for the vehicle pools, so every entry keeps the
    // table's own weight of 1 as its quota: the first spawn of a kind spends
    // it and every later one falls through to the first entry (LABEL_29 /
    // LABEL_35). With one ambient row of each kind that is simply that row.
    const std::size_t first = kind == 1 ? 1u : 0u;
    v.model = table[std::min(first, table.size() - 1)];
    v.live = true;
    v.speedCap = kVehSpeedCap;                       // `u32i(v3, 3) = 5000.0`
    v.sound = -1;                                    // `u16i(v3, 10) = -1`
    v.reserved = false;
    v.lodBase = 0;                                   // ambient traffic takes `v16[1]`
    const int mi = newMover();
    v.mover = mi;
    vehicles_[static_cast<std::size_t>(slot)] = v;

    Pedestrian& m = movers_[static_cast<std::size_t>(mi)];
    m.live = true;
    m.vehicle = slot;
    m.model = v.model;
    // `u32i(v7, 13) = u32i(v7, 14) = 256.0` - the mover's +52 and +56. For a
    // vehicle +52 is the BODY's live speed, which `sub_456C70` accelerates,
    // and +56 is the carrot's, which the gait sets from it each frame. (For a
    // walker +52 is the clip's stride and never moves.)
    m.baseSpeed = m.speed = kVehSpawnSpeed;
    // `sub_438040`: half the model's bounding radius. The pool holds no
    // models; the Session hands the real one in with `setVehicleModelRadius`
    // and until then a vehicle is the crowd's 20 - a LABELLED stand-in, and
    // the value only decides who blocks whom.
    m.bodyRadius = 20.0f;
    m.radius = m.bodyRadius * 0.5f;                  // `f32i(v7, 15) = r + r` over 2
    // ...then `sub_453B40` fills the mover, exactly as it does for a walker
    for (int i = 0; i < 3; ++i) { m.pos[i] = at[i]; m.prev[i] = at[i]; m.dir[i] = dir[i]; }
    // `sub_4543F0`'s trailing loop orients every mover it placed with
    // `sub_4427D0(dir.x, dir.y, dir.z)` - the crowd's passes 0 for y, so a
    // vehicle's body may pitch with its lane and a walker's may not.
    setHeading(m, dir[0], dir[1], dir[2]);
    // `sub_453B40`: `[ecx+24h] = x - dir.x * 117` and `[ecx+2Ch] = z - dir.z
    // * 117`, but `[ecx+28h] = y` with NO 117 term - the body starts 117
    // units back along the lane in the ground plane only.
    m.body[0] = at[0] - dir[0] * kCarrotBehind;
    m.body[1] = at[1];
    m.body[2] = at[2] - dir[2] * kCarrotBehind;
    m.remaining = segLen * 256.0f;
    m.seg = keyIndex + 1;
    m.lane = lane;
    const auto& L = track_.lanes[static_cast<std::size_t>(lane)];
    const int nr = L.routeCount > 0 ? L.routeCount : 1;
    counter_ = (counter_ + 1) & 0x7FFFFFFFu;
    m.route = L.firstRoute + static_cast<int>(counter_ % static_cast<std::uint32_t>(nr));
    m.flags = 0x8;                       // `sub_453B40`: every mover it places
    auto& list = listFor(lane, keyIndex + 1);
    list.insert(list.begin(), mi);
    return true;
}

void Sliders::setVehicleModelRadius(const std::string& model, float radius) {
    for (auto& v : vehicles_) {
        if (!v.live || v.model != model || v.mover < 0) continue;
        Pedestrian& m = movers_[static_cast<std::size_t>(v.mover)];
        m.bodyRadius = radius;
        m.radius = radius * 0.5f;
    }
}

void Sliders::setPlayer(const float pos[3], bool onRoad) {
    playerPos_[0] = pos[0]; playerPos_[1] = pos[1]; playerPos_[2] = pos[2];
    playerKnown_ = true;
    playerOnRoad_ = onRoad;                 // `dword_8F5E38`
}

// ------------------------------------------------------------ the tick

void Sliders::tickVehicles(float dt) {
    bumped_.clear();
    if (vehicles_.empty()) return;
    // `Sliders_Tick`: the 90-frame latch that lets one bump be reported at a
    // time (`flt_536C28` counts down, and at 0 `dword_538E20` is released).
    if (bumpHold_ > 0.0f) {
        bumpHold_ -= dt;
        if (bumpHold_ < 0.0f) { bumpHold_ = 0.0f; bumpLatch_ = -1; }
    }
    for (int vi = 0; vi < static_cast<int>(vehicles_.size()); ++vi) {
        Vehicle& v = vehicles_[static_cast<std::size_t>(vi)];
        if (!v.live || v.mover < 0) continue;
        // `if ((u8(v17, 180) & 9) != 0) sub_456530(...)` - flag 8 is the
        // spawner's mark and flag 1 is "blocked", so every placed mover
        // passes and a hand-placed one would not.
        if (!(movers_[static_cast<std::size_t>(v.mover)].flags & 9u)) continue;
        // `sub_456530`'s switch on the record's +8. State 0 - ambient
        // traffic - falls to the default, which is the drive. States 1..7
        // are the player's mount and ride and are not ported; a record in
        // one of them is left alone rather than driven wrongly.
        if (v.state != 0) continue;
        vehicleDrive(vi, dt);
        vehicleSound(vi);
    }
}

void Sliders::vehicleDrive(int vi, float dt) {
    // `sub_456C70`
    Vehicle& v = vehicles_[static_cast<std::size_t>(vi)];
    const int mi = v.mover;
    Pedestrian& m = movers_[static_cast<std::size_t>(mi)];

    moverStep(mi, dt);                                  // `sub_454F40`

    // the body chases the mover
    float to[3] = {m.pos[0] - m.body[0], m.pos[1] - m.body[1], m.pos[2] - m.body[2]};
    const float dist = len3(to);
    const int g = gait(m, to, dist, kVehGaitNear, kVehGaitFar);   // `unk_4C8888`
    if (!g) {                                           // stopped
        m.flags |= 0x100u;
        ++v.stops;
        return;
    }
    if (m.flags & 0x100u) {                             // ...and moving again
        m.flags &= ~0x100u;
        m.baseSpeed = kVehSpawnSpeed;
    }

    // the run-over: above 1706.6666 a vehicle whose spatial entry touches the
    // player raises event 43 with game message 17. `sub_45DF30` is the
    // index's own touch flag; the port has no index here, so the test is the
    // reach box the index would have applied - `max(|d|) <= r + r` over the
    // two radii, which is `SpatialIndex_Query`'s own gate (actor/spatial.*).
    if (playerKnown_ && bumpLatch_ < 0 && m.baseSpeed > kVehRunOver) {
        const float dx = std::fabs(m.body[0] - playerPos_[0]);
        const float dy = std::fabs(m.body[1] - playerPos_[1]);
        const float dz = std::fabs(m.body[2] - playerPos_[2]);
        const float reach = m.bodyRadius + 20.0f;
        if (dx <= reach && dy <= reach && dz <= reach) {
            bumpLatch_ = vi;
            bumpHold_ = 90.0f;
            bumped_.push_back(vi);
            ++v.bumps;
        }
    }

    // the speed: `+256 * dt` to the cap, or `-768 * dt` to 0 while blocked
    float speed = m.baseSpeed;
    if (g == 2) {
        speed -= dt * kVehBrake;
        if (speed < 0.0f) speed = 0.0f;
    } else {
        speed += dt * kVehAccel;
        if (speed > v.speedCap) speed = v.speedCap;
    }
    m.baseSpeed = speed;

    // ...and the body advances along the unit vector to the mover
    const float step = speed * dt * (1.0f / 256.0f) / (dist > 0.0f ? dist : 1.0f);
    float d[3] = {to[0] * step, to[1] * step, to[2] * step};
    float body[3] = {m.body[0] + d[0], m.body[1] + d[1], m.body[2] + d[2]};

    // it brakes for a player standing in the road: while `dword_8F5E38`, if he
    // is within 195 units, the vehicle is doing more than 1 unit a frame and
    // the step would CLOSE the distance, take 768*dt off, floored at 256.
    if (playerOnRoad_ && playerKnown_) {
        const float before = std::sqrt(
            (m.body[0] - playerPos_[0]) * (m.body[0] - playerPos_[0]) +
            (m.body[1] - playerPos_[1]) * (m.body[1] - playerPos_[1]) +
            (m.body[2] - playerPos_[2]) * (m.body[2] - playerPos_[2]));
        if (before < kVehBrakeRange && m.baseSpeed > kVehSpawnSpeed) {
            const float after = std::sqrt(
                (body[0] - playerPos_[0]) * (body[0] - playerPos_[0]) +
                (body[1] - playerPos_[1]) * (body[1] - playerPos_[1]) +
                (body[2] - playerPos_[2]) * (body[2] - playerPos_[2]));
            if (before > after) {
                float s = m.baseSpeed - dt * kVehBrake;
                if (s < kVehSpawnSpeed) s = kVehSpawnSpeed;
                m.baseSpeed = s;
                ++v.brakes;
            }
        }
    }

    m.body[0] = body[0]; m.body[1] = body[1]; m.body[2] = body[2];
    // `sub_437F80(mover + 80, x, y - 30.75, z)`: the 3D node sits 30.75 units
    // above the body point (y is down, docs/ASSETS handedness). The node is
    // the frontend's business; the lift is recorded so it draws the same.
}

void Sliders::vehicleSound(int vi) {
    // `sub_456B40`: SOUNDS\sliderm01.wav, 3D at the body with the mover's
    // velocity, started inside 585 units of the listener and stopped outside.
    // The mixer is the Session's; this keeps the record's `+20` so a frontend
    // can start and stop the one voice the engine keeps per vehicle.
    Vehicle& v = vehicles_[static_cast<std::size_t>(vi)];
    if (!playerKnown_) return;
    const Pedestrian& m = movers_[static_cast<std::size_t>(v.mover)];
    const float dx = m.body[0] - playerPos_[0];
    const float dy = m.body[1] - playerPos_[1];
    const float dz = m.body[2] - playerPos_[2];
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (v.sound == -1) {
        if (d < kVehSoundRange) { v.sound = vi; ++v.soundOn; }
    } else if (d > kVehSoundRange) {
        v.sound = -1;
    }
}

}  // namespace omk

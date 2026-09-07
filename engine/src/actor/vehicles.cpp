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
#include "actor/slider.h"

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
        // traffic - falls to the default, which is the drive; 2 and 6 are the
        // player's slider COMING, which is also driven, because the arrival
        // test in those arms only decides when to stop. 3 (open), 4/5
        // (aboard) and 7 (leaving) hold still: `Slider_TickRide` owns the
        // body in 4/5, and an open slider is waiting for `MDSLIDIN`.
        if (vi == called_) {
            float d = 1e9f;
            if (callRide_.state == 2 || callRide_.state == 6) {
                const Pedestrian& m = movers_[static_cast<std::size_t>(v.mover)];
                const float dx = m.pos[0] - callTarget_[0];
                const float dy = m.pos[1] - callTarget_[1];
                const float dz = m.pos[2] - callTarget_[2];
                d = std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            const int was = callRide_.state;
            callRide_.tick(dt, d, 0.0f, false);
            if (was != callRide_.state && callRide_.state != 2 &&
                callRide_.state != 6) {
                // It stopped where it arrived. `sub_456530` leaves state 1
                // (the 600-frame idle) on the transport arm; a slider CALLED
                // to be boarded goes OPEN instead, which is mode 3 - what
                // `MDSLIDIN` demands.
                callRide_.state = 3;
                v.state = 3;
            }
            if (callRide_.state == 2 || callRide_.state == 6) {
                vehicleDrive(vi, dt);
                vehicleSound(vi);
            }
            continue;
        }
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

// ------------------------------------------- `sub_452570`'s own lane search
//
// `sub_452A80(state, point, key, best)`: the distance from the TARGET (which
// the caller left at `state+32/36/40`) to the segment that starts at `point`
// and runs along the key's delta, with the closest point itself written back
// to `state+20/24/28`. -1 when the target is outside the 3900-unit box on any
// axis, or when the result is no better than `best`.
//
// Transcribed rather than replaced by a library routine, because two details
// are the engine's and not a textbook's: the box reject comes FIRST, so a
// far-off lane is never measured at all; and outside the segment it compares
// the two ENDPOINTS and keeps the nearer, rather than clamping the parameter.
static float segmentDistance(const float target[3], const float point[3],
                             const float delta[3], float best, float out[3]) {
    const float d[3] = {point[0] - target[0], point[1] - target[1],
                        point[2] - target[2]};
    if (std::fabs(d[0]) > 3900.0f || std::fabs(d[1]) > 3900.0f ||
        std::fabs(d[2]) > 3900.0f)
        return -1.0f;
    const float len2 = delta[0] * delta[0] + delta[1] * delta[1] +
                       delta[2] * delta[2];
    float r[3] = {d[0], d[1], d[2]};
    float dist;
    if (len2 <= 0.0f) {
        dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    } else {
        const float u = -(d[2] * delta[2] + d[1] * delta[1] + d[0] * delta[0]) / len2;
        if (u < 0.0f || u >= 1.0f) {
            dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            const float e[3] = {d[0] + delta[0], d[1] + delta[1], d[2] + delta[2]};
            const float de = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
            if (de < dist) { dist = de; r[0] = e[0]; r[1] = e[1]; r[2] = e[2]; }
        } else {
            r[0] = delta[0] * u + d[0];
            r[1] = delta[1] * u + d[1];
            r[2] = delta[2] * u + d[2];
            dist = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        }
    }
    if (best >= 0.0f && dist >= best) return -1.0f;
    for (int k = 0; k < 3; ++k) out[k] = target[k] + r[k];
    return dist;
}

LanePoint nearestVehicleLane(const OptTrack& t, const float target[3]) {
    LanePoint best;
    if (!t.valid) return best;
    // `for (lane = header[2]; lane < header[5])` - the lanes AFTER the
    // pedestrian range, which is what makes this a VEHICLE search.
    for (std::size_t lane = t.pedEnd; lane < t.laneCount && lane < t.lanes.size();
         ++lane) {
        const OptLane& L = t.lanes[lane];
        float p[3] = {L.origin[0], L.origin[1], L.origin[2]};
        const int n = L.keyCount;
        for (int k = 0; k < n; ++k) {
            const std::size_t ki = static_cast<std::size_t>(L.firstKey) +
                                   static_cast<std::size_t>(k);
            if (ki >= t.keys.size()) break;
            const OptKey& K = t.keys[ki];
            float at[3];
            const float d = segmentDistance(target, p, K.delta, best.dist, at);
            if (d >= 0.0f) {
                best.dist = d;
                best.lane = static_cast<int>(lane);
                best.key = k;
                for (int j = 0; j < 3; ++j) best.at[j] = at[j];
            }
            for (int j = 0; j < 3; ++j) p[j] += K.delta[j];
        }
    }
    return best;
}

int laneRoute(const OptTrack& t, int lane, unsigned counter) {
    if (lane < 0 || static_cast<std::size_t>(lane) >= t.lanes.size()) return -1;
    const OptLane& L = t.lanes[static_cast<std::size_t>(lane)];
    // `if (!u8(v24, 20)) u8(v24, 20) = 1;` - a lane with no routes is read as
    // having one, and the engine WRITES the 1 back rather than only reading
    // it. Nothing downstream here can see the write, so this only reads.
    const int count = L.routeCount ? L.routeCount : 1;
    return L.firstRoute + static_cast<int>(counter % static_cast<unsigned>(count));
}

// `sub_452CC0`'s DECISIONS - see the header for what is deliberately not
// transcribed. The engine reaches this only after walking all 40 slots
// without finding a blocker; with one it swaps the two vehicles instead, and
// either way the last thing it does is put the node at `y - 30.75`.
SliderCall planSliderCall(const OptTrack& t, const float target[3],
                          unsigned counter) {
    SliderCall c;
    c.at = nearestVehicleLane(t, target);
    if (!c.at.found()) return c;
    c.route = laneRoute(t, c.at.lane, counter);
    const OptLane& L = t.lanes[static_cast<std::size_t>(c.at.lane)];
    const std::size_t ki = static_cast<std::size_t>(L.firstKey);
    if (ki >= t.keys.size()) { c.at.lane = -1; return c; }
    const OptKey& K = t.keys[ki];
    const float len = std::sqrt(K.delta[0] * K.delta[0] + K.delta[1] * K.delta[1] +
                                K.delta[2] * K.delta[2]);
    if (len > 0.0f)
        for (int k = 0; k < 3; ++k) c.dir[k] = K.delta[k] / len;
    // The set-back is in x and z only: `u32(v22, 40) = v61[6]` leaves the
    // origin's y exactly as it is.
    c.place[0] = L.origin[0] - c.dir[0] * SliderCall::kSetBack;
    c.place[1] = L.origin[1];
    c.place[2] = L.origin[2] - c.dir[2] * SliderCall::kSetBack;
    c.nodeY = c.place[1] - SliderRide::kHover;
    return c;
}

// `sub_456530`'s switch, and only that - see the header. Every arm sets the
// ride's 90-degree fov, which is why it is not repeated per case here.
void RideMachine::tick(float dt, float toTarget, float toPlayer, bool ahead) {
    camera = -1;
    fadeIn = false;
    released = false;
    switch (state) {
    case 1:
        // `flt_8F5E90 -= dt`, and on expiry the assignment is dropped.
        idleClock -= dt;
        if (idleClock <= 0.0f) { state = 0; released = true; }
        return;
    case 2:
        // Camera 8 on the SLIDER while it comes...
        camera = 8;
        if (toTarget < kArrive) {
            // ...and on arrival the camera hands back to the PLAYER at mode
            // 0, the fade comes in and the hold is released.
            state = 1;
            idleClock = kIdle;
            camera = 0;
            fadeIn = true;
            released = true;
        }
        return;
    case 3:
        return;                       // OPEN, and waiting for `MDSLIDIN`
    case 4:
    case 5:
        return;                       // aboard: `Slider_TickRide` owns it
    case 6:
        if (toTarget < kArrive) {
            // The departure shot: mode 10, framed between the destination's
            // own address record and the vehicle.
            state = 4;
            camera = 10;
        } else {
            camera = 8;
        }
        return;
    case 7:
        // It drives off only once he is clear of it AND in front of it.
        if (toPlayer > kLeave && ahead) { state = 0; released = true; }
        return;
    default:
        return;                       // 0: ambient traffic, the ordinary drive
    }
}

// ------------------------------------------------- THE PLAYER'S OWN SLIDER
//
// `sub_452570`'s ARM arm in this pool's terms. The engine takes a FREE SLOT
// out of the 40 - `slot[+22] == 1` and the mover's `+180 & 8` - which this
// port's own read of `Slider_Init` already identified as slot 0, the player's
// reserved slider; here the spawner picks the first dead slot, which is the
// same thing for a pool that has one call out at a time.
bool Sliders::callSlider(const float target[3]) {
    if (!loaded_ || !track_.valid) return false;
    if (called_ >= 0) return true;              // one call at a time
    const SliderCall c = planSliderCall(track_, target, counter_ + 1);
    // No vehicle lanes at all is LAHOREY, and the engine fails there too.
    if (!c.ok()) return false;
    const OptLane& L = track_.lanes[static_cast<std::size_t>(c.at.lane)];
    const OptKey& K = track_.keys[static_cast<std::size_t>(L.firstKey)];
    const float segLen = std::sqrt(K.delta[0] * K.delta[0] +
                                   K.delta[1] * K.delta[1] +
                                   K.delta[2] * K.delta[2]);
    // A FREE SLOT FIRST, and an AMBIENT ONE when the pool is full - which it
    // is in a city, because the spawner fills all 40. The engine has the same
    // problem and answers it twice over: it reserves slot 0 for the player
    // (`slot[+22] == 1`, which this port's read of `Slider_Init` already
    // named), and where the target lane is occupied `sub_452CC0` SWAPS the
    // two vehicles outright so the one in the way becomes the player's. Both
    // come to the same thing - a call always finds a vehicle where there are
    // roads - and taking an ambient one is the second of them.
    const int before = liveVehicles();
    if (spawnVehicle(c.at.lane, c.place, c.dir, segLen, 0) &&
        liveVehicles() != before) {
        for (int i = 0; i < static_cast<int>(vehicles_.size()); ++i)
            if (vehicles_[static_cast<std::size_t>(i)].live &&
                vehicles_[static_cast<std::size_t>(i)].state == 0 &&
                !vehicles_[static_cast<std::size_t>(i)].reserved)
                called_ = i;                    // the newest live slot
    } else {
        for (int i = 0; i < static_cast<int>(vehicles_.size()); ++i) {
            Vehicle& av = vehicles_[static_cast<std::size_t>(i)];
            if (!av.live || av.state != 0 || av.reserved || av.mover < 0) continue;
            // `sub_452CC0`'s relink, in this pool's terms: off whatever lane
            // it was on, onto the one the call chose, at the same place the
            // spawner would have put it.
            removeFromLists(av.mover);
            Pedestrian& m = movers_[static_cast<std::size_t>(av.mover)];
            for (int k = 0; k < 3; ++k) {
                m.pos[k] = c.place[k];
                m.prev[k] = c.place[k];
                m.dir[k] = c.dir[k];
            }
            setHeading(m, c.dir[0], c.dir[1], c.dir[2]);
            m.body[0] = c.place[0] - c.dir[0] * kCarrotBehind;
            m.body[1] = c.place[1];
            m.body[2] = c.place[2] - c.dir[2] * kCarrotBehind;
            m.remaining = segLen * 256.0f;
            m.seg = 1;
            m.lane = c.at.lane;
            m.route = c.route;
            m.flags = 0x8;
            listFor(c.at.lane, 1).insert(listFor(c.at.lane, 1).begin(), av.mover);
            called_ = i;
            break;
        }
    }
    if (called_ < 0) return false;
    Vehicle& v = vehicles_[static_cast<std::size_t>(called_)];
    v.reserved = true;                          // `slot[+22] = 1`
    v.state = 2;                                // `u32(slot, 8) = 2` - COMING
    callRide_ = RideMachine{};
    callRide_.state = 2;
    // THE PICKUP POINT IS THE LANE POINT, not the player. `sub_452A80`
    // writes the closest point on the lane into the request block's `+20`,
    // and `sub_456530`'s arrival test reads `flt_8F5E74` - which is that same
    // `+20`, twenty bytes into `dword_8F5E60`. Measuring against the PLAYER
    // instead is a test a slider on a road can never pass: the nearest lane
    // point to him here is 518 units away and the radius is 117, so it drove
    // all the way in and then sat there. Caught by running it, not by
    // re-reading.
    for (int k = 0; k < 3; ++k) callTarget_[k] = c.at.at[k];
    return true;
}

bool Sliders::calledAt(float out[3]) const {
    if (called_ < 0) return false;
    const Vehicle& v = vehicles_[static_cast<std::size_t>(called_)];
    if (!v.live || v.mover < 0) return false;
    const Pedestrian& m = movers_[static_cast<std::size_t>(v.mover)];
    for (int k = 0; k < 3; ++k) out[k] = m.pos[k];
    return true;
}

float Sliders::calledYaw() const {
    if (called_ < 0) return 0.0f;
    const Vehicle& v = vehicles_[static_cast<std::size_t>(called_)];
    if (!v.live || v.mover < 0) return 0.0f;
    const Pedestrian& m = movers_[static_cast<std::size_t>(v.mover)];
    // the inverse of `setHeading(m, sin t, 0, cos t)`
    return static_cast<float>(std::atan2(m.dir[0], m.dir[2]) * 57.29577951308232);
}

// `MDSLIDIN`'s two data conditions - "no active slider !" and "slider is not
// in open mode !". The third, `player[+404] == 6`, belongs to the caller.
bool Sliders::canMount(const float playerPos[3], float reach) const {
    if (!calledIsOpen()) return false;
    float at[3];
    if (!calledAt(at)) return false;
    const float dx = at[0] - playerPos[0], dy = at[1] - playerPos[1],
                dz = at[2] - playerPos[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz) <= reach;
}

void Sliders::mountCalled() {
    if (called_ < 0) return;
    callRide_.state = 4;                        // aboard
    vehicles_[static_cast<std::size_t>(called_)].state = 4;
}

void Sliders::dismountCalled() {
    if (called_ < 0) return;
    callRide_.state = 7;                        // LEAVING
    vehicles_[static_cast<std::size_t>(called_)].state = 7;
}

// While he is aboard the vehicle IS the ride: `sub_457F50` writes the
// slider's node from the ride's own position every frame, so the model the
// pool draws has to follow it or he flies on nothing.
void Sliders::placeCalled(const float pos[3], float yawDeg) {
    if (called_ < 0) return;
    const Vehicle& v = vehicles_[static_cast<std::size_t>(called_)];
    if (!v.live || v.mover < 0) return;
    Pedestrian& m = movers_[static_cast<std::size_t>(v.mover)];
    for (int k = 0; k < 3; ++k) { m.prev[k] = m.pos[k]; m.pos[k] = pos[k]; }
    for (int k = 0; k < 3; ++k) m.body[k] = pos[k];
    const float t = yawDeg * 0.0174532925199433f;
    setHeading(m, std::sin(t), 0.0f, std::cos(t));
}

}  // namespace omk

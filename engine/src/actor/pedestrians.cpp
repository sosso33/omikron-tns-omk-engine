// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/sliders.h"

#include "formats/anim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace omk {
namespace {

constexpr float kPi = 3.14159265358979f;

float len2(float x, float z) { return std::sqrt(x * x + z * z); }

}  // namespace

float len3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// `sub_453330`: the orientation from a direction. Stored as the unit forward.
// Not file-local: the vehicles' drive (`sub_456C70`, actor/vehicles.cpp) aims
// its body the same way, through the same gait function.
void setHeading(Pedestrian& m, float x, float y, float z) {
    const float n = std::sqrt(x * x + y * y + z * z);
    if (n <= 0.0f) return;
    m.heading[0] = x / n; m.heading[1] = y / n; m.heading[2] = z / n;
    m.facing = pedFacingOf(m.heading);
}

// ------------------------------------------------------------ the clips

const PedClip* PedClips::randomOfType(int sex, int type, std::uint32_t& rng) const {
    // `List_PickRandomByType`: count the matches, `rand() % n`, return that one
    const auto& g = group(sex);
    int n = 0;
    for (const auto& c : g) if (c.type == type) ++n;
    if (!n) return nullptr;
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    int pick = static_cast<int>(rng % static_cast<std::uint32_t>(n));
    for (const auto& c : g) if (c.type == type && pick-- == 0) return &c;
    return nullptr;
}

const PedClip* PedClips::bySlot(int sex, int slot) const {
    // `sub_434630`: the list walked matching +4; 0 names nothing
    if (!slot) return nullptr;
    for (const auto& c : group(sex)) if (c.slot == slot) return &c;
    return nullptr;
}

const PedClip* PedClips::byNameType(int sex, const std::string& name, int type) const {
    // `sub_4346C0` (type 14) / `sub_434730` (type 16): the same name, that type
    for (const auto& c : group(sex)) if (c.type == type && c.name == name) return &c;
    return nullptr;
}

PedClips pedClipsFrom(std::span<const std::byte> ani) {
    PedClips out;
    for (const auto& clip : animClips(ani)) {
        if (clip.group != 1 && clip.group != 2) continue;
        const auto desc = animDescriptor(ani, clip.descriptor);
        if (!desc) continue;
        PedClip c;
        c.slot = clip.slot; c.type = clip.type; c.name = clip.name; c.frames = desc->frames;
        c.descriptor = clip.descriptor;
        // `sub_437FE0`: the root is the first track with rotation keys
        for (const auto& t : desc->tracks) {
            if (!t.rotKeys) continue;
            c.root.resize(static_cast<std::size_t>(std::max(t.posKeys, 0)) * 3);
            for (std::int32_t k = 0; k < t.posKeys; ++k) {
                const std::size_t o = t.posOffset + 12 * static_cast<std::size_t>(k);
                if (o + 12 > ani.size()) { c.root.clear(); break; }
                std::memcpy(&c.root[static_cast<std::size_t>(3 * k)], ani.data() + o, 12);
            }
            break;
        }
        // `sub_453D80`: root travel over [1, frames-1] in xz, ×256, per frame
        if (c.frames > 2 && !c.root.empty()) {
            float d[3];
            pedRootDelta(c, 1.0f, static_cast<float>(c.frames - 1), nullptr, d);
            c.strideX256 = len2(d[0], d[2]) * 256.0f / static_cast<float>(c.frames);
        }
        (clip.group == 2 ? out.women : out.men).push_back(std::move(c));
    }
    return out;
}

void pedRootDelta(const PedClip& c, float t0, float t1, const float* heading, float out[3]) {
    float d[3] = {0, 0, 0};
    const int keys = static_cast<int>(c.root.size() / 3);
    auto key = [&](int k, int i) -> float {
        if (k < 0 || k >= keys) return 0.0f;
        return c.root[static_cast<std::size_t>(3 * k + i)];
    };
    const float cf = std::ceil(t0);
    if (cf <= t1) {
        const int cIdx = static_cast<int>(cf);
        if (cf > t0) for (int i = 0; i < 3; ++i) d[i] += (cf - t0) * key(cIdx, i);
        const float fl = std::floor(t1);
        int k = cIdx;
        while (static_cast<float>(k) < fl) { for (int i = 0; i < 3; ++i) d[i] += key(k + 1, i); ++k; }
        if (t1 > static_cast<float>(k)) for (int i = 0; i < 3; ++i) d[i] += (t1 - static_cast<float>(k)) * key(k + 1, i);
    } else {
        const int cIdx = static_cast<int>(cf);
        for (int i = 0; i < 3; ++i) d[i] += (t1 - t0) * key(cIdx, i);
    }
    if (!heading) { out[0] = d[0]; out[1] = d[1]; out[2] = d[2]; return; }
    // the frame: row2 = -forward, row0 = horizontal right, row1 = row2 x row0
    const float fx = heading[0], fy = heading[1], fz = heading[2];
    const float h = std::sqrt(fx * fx + fz * fz);
    float r0[3], r1[3], r2[3] = {-fx, -fy, -fz};
    if (h > 0.0f) { r0[0] = -fz / h; r0[1] = 0.0f; r0[2] = fx / h; }
    else          { r0[0] = 1.0f;    r0[1] = 0.0f; r0[2] = 0.0f; }
    r1[0] = r2[1] * r0[2] - r2[2] * r0[1];
    r1[1] = r2[2] * r0[0] - r2[0] * r0[2];
    r1[2] = r2[0] * r0[1] - r2[1] * r0[0];
    for (int i = 0; i < 3; ++i) out[i] = d[0] * r0[i] + d[1] * r1[i] + d[2] * r2[i];
}

float pedFacingOf(const float heading[3]) {
    return std::atan2(heading[2], heading[0]) * 180.0f / kPi + 90.0f;
}

void pedHeadingOf(float facingDeg, float heading[3]) {
    const float a = facingDeg * kPi / 180.0f;
    heading[0] = std::sin(a); heading[1] = 0.0f; heading[2] = -std::cos(a);
}

// ------------------------------------------------------------ the tables

const std::vector<std::string>& pedModelTable(int sex) {
    // `aPshFn` / `aFshFn` in Runtime 2.exe: 12-byte rows, name + weight 1
    static const std::vector<std::string> men = {
        "PSH_FN", "CMH_FN", "KSH_FN", "PVH_FN", "AMH_FN", "CEM_FN", "KHO_FN", "TEH_FN",
        "YES_FN", "PSH1_FN", "KSH1_FN", "PVH1_FN"};
    static const std::vector<std::string> women = {
        "FSH_FN", "CWH_FN", "KWH_FN", "VFH_FN", "AN1_FN", "KIL2_FN", "FSH1_FN", "KWH1_FN",
        "VFH1_FN"};
    return sex == 2 ? women : men;
}

const std::vector<std::string>& pedNameTable(int sex) {
    // `off_4C88A0` / `off_4C8920`: null-terminated pointer arrays
    static const std::vector<std::string> men = {
        "Nicolas", "Gwenael", "Fabien", "Stephane", "Xavier", "Christophe", "Johny", "Cliff",
        "Jordan", "Lionel", "Henri", "David", "Francois", "Michel", "Olivier", "Didier", "Eric",
        "Pierre", "Julien", "Gille", "Jean Charles", "KMel", "Franck", "Edouard", "Phillipe",
        "Dominique", "Jerome", "Yan", "Regis", "Thierry", "Amar"};
    static const std::vector<std::string> women = {
        "Caroline", "Gaelle", "Salma", "Florence", "Aicha", "Christelle", "Leila", "Sophie",
        "Natalie", "Audrey", "Fanta", "Silvie", "Stephanie", "Helene", "Sandrine", "Claire",
        "Anne", "Sidonie", "Noemie", "Josephine", "Chloe", "Marie", "Sandra", "Charlie",
        "Charlotte", "Christine", "Julie", "Lucille", "Lucie", "Rose"};
    return sex == 2 ? women : men;
}

// ------------------------------------------------------------ the pool

std::uint32_t Sliders::rnd() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return rng_;
}

void Sliders::clear() {
    track_ = OptTrack{}; clips_ = PedClips{};
    models_.clear(); walkers_.clear(); states_.clear(); actionClip_.clear();
    laneHead_.clear(); keyList_.clear(); routeHead_.clear(); groupBusy_.clear();
    groupBusyVeh_.clear(); crossWaitVeh_ = crossWaitPed_ = 0;
    vehicles_.clear(); bumped_.clear();
    playerKnown_ = playerOnRoad_ = false; bumpHold_ = 0.0f; bumpLatch_ = -1;
    nSliderModels_ = nMotoModels_ = 0;
    talkTarget_ = -1; counter_ = 0; nameNext_[1] = nameNext_[2] = 0;
    loaded_ = false;
}

void Sliders::setModelRadius(const std::string& model, float radius) {
    for (auto& w : walkers_) {
        if (w.vehicle >= 0 || w.model != model) continue;
        w.bodyRadius = radius;
        w.radius = radius * 0.5f;
    }
}

int Sliders::liveCount() const {
    int n = 0;
    for (const auto& w : walkers_) if (w.live && w.vehicle < 0) ++n;
    return n;
}

int Sliders::liveVehicles() const {
    int n = 0;
    for (const auto& v : vehicles_) if (v.live) ++n;
    return n;
}

void Sliders::load(const OptTrack& track, const PedClips& clips, std::uint32_t menMask,
                       std::uint32_t womenMask, int streetActivity, std::uint32_t seed,
                       std::uint32_t sliMask, std::uint32_t motoMask) {
    clear();
    if (!track.valid) return;
    track_ = track; clips_ = clips;
    level_ = std::clamp(streetActivity, 0, 4);
    rng_ = seed ? seed : 1u;
    // `sub_4539B0` over each table with its mask, then `sub_453E80(pool, n,
    // 100)`: every model's weight (1) scaled so the quotas sum to 100 -
    // integer division, so 12 models get 8 each and the rest goes unspent
    for (int sex = 1; sex <= 2; ++sex) {
        const auto& table = pedModelTable(sex);
        const std::uint32_t mask = sex == 1 ? menMask : womenMask;
        std::vector<ModelQuota> picked;
        for (std::size_t i = 0; i < table.size() && i < 32; ++i)
            if (mask & (1u << i)) picked.push_back({table[i], sex, 1});
        const int total = static_cast<int>(picked.size());
        for (auto& m : picked) m.quota = total ? (100 / total) * 1 : 0;
        models_.insert(models_.end(), picked.begin(), picked.end());
    }
    // `Slider_Init`: the pedestrian half exists only with a lane range and a
    // walk clip for the men (`sub_434530(1)` + type 9) - the women's pool is
    // gated on its own walk clip
    const bool menOk = std::any_of(models_.begin(), models_.end(), [](const ModelQuota& m) { return m.sex == 1; }) &&
                       std::any_of(clips_.men.begin(), clips_.men.end(), [](const PedClip& c) { return c.type == 9; });
    const bool womenOk = std::any_of(models_.begin(), models_.end(), [](const ModelQuota& m) { return m.sex == 2; }) &&
                         std::any_of(clips_.women.begin(), clips_.women.end(), [](const PedClip& c) { return c.type == 9; });
    if (!menOk) models_.erase(std::remove_if(models_.begin(), models_.end(), [](const ModelQuota& m) { return m.sex == 1; }), models_.end());
    if (!womenOk) models_.erase(std::remove_if(models_.begin(), models_.end(), [](const ModelQuota& m) { return m.sex == 2; }), models_.end());
    loaded_ = true;
    states_.assign(track_.actions.size(), ActionState{});
    actionClip_.resize(track_.actions.size());
    for (std::size_t i = 0; i < track_.actions.size(); ++i) actionClip_[i] = track_.actions[i].clip;
    laneHead_.assign(track_.lanes.size(), {});
    keyList_.assign(track_.keys.size(), {});
    routeHead_.assign(track_.routes.size(), {});
    groupBusy_.assign(track_.groups.size(), 0);
    groupBusyVeh_.assign(track_.groups.size(), 0);
    walkers_.reserve(static_cast<std::size_t>(kMaxWalkers + kMaxVehicles));
    // `Slider_Init` runs its two halves in order and each has its own gate:
    // the crowd needs a pedestrian lane range and a model, the traffic needs
    // vehicle lanes. Neither is a precondition of the other, and Lahoreh
    // (0 vehicle lanes) and a maskless area each exercise one of them.
    if (track_.pedFirst < track_.pedEnd && !models_.empty()) {
        // THE DENSITY: `(5 - SBYTE2(dword_90E724)) * u32i(dword_8F5E48, 3)`
        spawnAlongLanes(track_.pedFirst, track_.pedEnd,
                        static_cast<float>((5 - level_) * static_cast<int>(track_.pedSpacing)), false);
    }
    // `Slider_Init`'s last loop: every pedestrian's speed factor and base speed
    for (auto& w : walkers_) {
        if (!w.live || w.vehicle >= 0) continue;
        const float f = static_cast<float>(5 - static_cast<int>(rnd() % 10)) * 0.05f + 1.0f;
        w.speedFactor = f;
        const float stride = w.clip ? w.clip->strideX256 : 0.0f;
        w.baseSpeed = w.speed = f * stride;
    }
    loadVehicles(sliMask, motoMask);
}

int Sliders::spawnCount(const OptTrack& t, int level) {
    // `sub_453B40`'s rule alone: the accumulated key lengths across ALL the
    // pedestrian lanes (the accumulator is not reset per lane), a spawn
    // whenever it exceeds 39 * spacing, the pool's 200 cap ending the walk
    const float spacing = kSpawnSpacingFactor * static_cast<float>((5 - std::clamp(level, 0, 4)) * static_cast<int>(t.pedSpacing));
    float acc = 0.0f;
    int n = 0;
    for (std::uint32_t li = t.pedFirst; li < t.pedEnd; ++li) {
        const auto& L = t.lanes[li];
        for (int k = 0; k < L.keyCount; ++k) {
            const auto& K = t.keys[static_cast<std::size_t>(L.firstKey + k)];
            if (acc > spacing) {
                if (n >= kMaxWalkers) return n;
                ++n; acc = 0.0f;
            }
            acc += len3(K.delta);
        }
    }
    return n;
}

void Sliders::spawnAlongLanes(std::uint32_t firstLane, std::uint32_t endLane,
                                  float spacing, bool vehicles) {
    const float threshold = kSpawnSpacingFactor * spacing;
    float acc = 0.0f;
    for (std::uint32_t li = firstLane; li < endLane; ++li) {
        const auto& L = track_.lanes[li];
        float p[3] = {L.origin[0], L.origin[1], L.origin[2]};
        for (int k = 0; k < L.keyCount; ++k) {
            const auto& K = track_.keys[static_cast<std::size_t>(L.firstKey + k)];
            const float len = len3(K.delta);
            if (acc > threshold) {
                float dir[3] = {K.delta[0] / len, K.delta[1] / len, K.delta[2] / len};
                // the callback returning 0 - the pool full - ends the walk
                const bool ok = vehicles ? spawnVehicle(static_cast<int>(li), p, dir, len, k)
                                         : spawnOne(static_cast<int>(li), p, dir, len, k);
                if (!ok) return;
                acc = 0.0f;
            }
            acc += len;
            p[0] += K.delta[0]; p[1] += K.delta[1]; p[2] += K.delta[2];
        }
    }
}

bool Sliders::spawnOne(int lane, const float at[3], const float dir[3], float segLen, int keyIndex) {
    // `sub_453ED0`: the callback - a record and a mover, a sex, a model from
    // its quota, a name, the walk clip; then `sub_453B40` fills the mover
    if (static_cast<int>(walkers_.size()) >= kMaxWalkers) return false;
    const bool menLeft = std::any_of(models_.begin(), models_.end(), [](const ModelQuota& m) { return m.sex == 1; });
    const bool womenLeft = std::any_of(models_.begin(), models_.end(), [](const ModelQuota& m) { return m.sex == 2; });
    int sex = static_cast<int>(rnd() & 1u) ? 2 : 1;
    if (!menLeft) sex = 2;
    if (!womenLeft) { if (!menLeft) return true; sex = 1; }
    // the first model of that sex with quota left, else the first of that sex
    ModelQuota* pick = nullptr;
    for (auto& m : models_) if (m.sex == sex && m.quota > 0) { pick = &m; break; }
    if (!pick) for (auto& m : models_) if (m.sex == sex) { pick = &m; break; }
    if (!pick) return true;
    if (pick->quota > 0) --pick->quota;
    Pedestrian w;
    w.live = true; w.sex = sex; w.model = pick->name;
    const auto& names = pedNameTable(sex);
    int& next = nameNext_[sex];
    if (next >= static_cast<int>(names.size())) next = 0;
    w.name = names[static_cast<std::size_t>(next++)];
    w.clip = clips_.randomOfType(sex, 9, rng_);
    w.clock = 1.0f;
    w.frames = w.clip ? w.clip->frames : 0;
    w.footY = 0.0f;
    // `sub_438040`: the model's bounding radius - not loaded here, the value
    // every PERSOS model the crowd wears carries is taken as 20 until the
    // frontend hands the real one in (a labelled stand-in, step 4)
    w.bodyRadius = 20.0f;
    w.radius = w.bodyRadius * 0.5f;
    // the mover, from `sub_453B40`
    for (int i = 0; i < 3; ++i) { w.pos[i] = at[i]; w.prev[i] = at[i]; w.dir[i] = dir[i]; }
    w.body[0] = at[0] - dir[0] * kCarrotBehind;
    w.body[1] = at[1];
    w.body[2] = at[2] - dir[2] * kCarrotBehind;
    setHeading(w, dir[0], 0.0f, dir[2]);       // `sub_4427D0(dir.x, 0, dir.z)` -> `sub_4423C0`
    w.remaining = segLen * 256.0f;
    w.seg = keyIndex + 1;
    w.lane = lane;
    const auto& L = track_.lanes[static_cast<std::size_t>(lane)];
    const int nr = L.routeCount > 0 ? L.routeCount : 1;
    counter_ = (counter_ + 1) & 0x7FFFFFFFu;
    w.route = L.firstRoute + static_cast<int>(counter_ % static_cast<std::uint32_t>(nr));
    w.flags = 0x8;
    const int idx = static_cast<int>(walkers_.size());
    walkers_.push_back(std::move(w));
    auto& list = listFor(lane, keyIndex + 1);
    list.insert(list.begin(), idx);
    return true;
}

// ------------------------------------------------------------ the lists

std::vector<int>& Sliders::listFor(int lane, int seg) {
    // a segment index k on a lane lives in the lane's own head list for k == 1
    // (`a3 + 12`) and in key (k - 2)'s list after that - the spawner inserts
    // key k's mover into the list of the key BEFORE it
    if (seg <= 1) return laneHead_[static_cast<std::size_t>(lane)];
    const auto& L = track_.lanes[static_cast<std::size_t>(lane)];
    return keyList_[static_cast<std::size_t>(L.firstKey + seg - 2)];
}

void Sliders::removeFromLists(int w) {
    auto strip = [&](std::vector<int>& l) { l.erase(std::remove(l.begin(), l.end(), w), l.end()); };
    for (auto& l : laneHead_) strip(l);
    for (auto& l : keyList_) strip(l);
    for (auto& l : routeHead_) strip(l);
}

// ------------------------------------------------------------ the mover

bool Sliders::stepLaneKey(const Pedestrian& m, float p[3]) const {
    // `sub_455570`: add the key being walked; false at the lane's last key
    const auto& L = track_.lanes[static_cast<std::size_t>(m.lane)];
    const auto& K = track_.keys[static_cast<std::size_t>(L.firstKey + m.seg - 1)];
    p[0] += K.delta[0]; p[1] += K.delta[1]; p[2] += K.delta[2];
    return m.seg != L.keyCount;
}

void Sliders::aimAtRouteStep(const Pedestrian& m, const float p[3], int step, float dir[3], float& rem) const {
    // `sub_4554B0`: the next step's delta, or after the last step the
    // straight line to the destination lane's origin
    const auto& R = track_.routes[static_cast<std::size_t>(m.route)];
    float d[3];
    if (step == R.stepCount) {
        const auto& D = track_.lanes[static_cast<std::size_t>(R.dest)];
        for (int i = 0; i < 3; ++i) d[i] = D.origin[i] - p[i];
    } else {
        const auto& S = track_.steps[static_cast<std::size_t>(R.firstStep + step)];
        for (int i = 0; i < 3; ++i) d[i] = S.delta[i];
    }
    const float n = len3(d);
    if (n > 0.0f) for (int i = 0; i < 3; ++i) dir[i] = d[i] / n;
    rem = n * 256.0f;
}

int Sliders::aimAtLaneKey(int lane, int key, float dir[3], float& rem) {
    // `sub_4555C0`: aim along key `key` (1-based) of `lane`; every second
    // mover reaching a key with a live action point is sent to it
    const auto& L = track_.lanes[static_cast<std::size_t>(lane)];
    const auto& K = track_.keys[static_cast<std::size_t>(L.firstKey + key - 1)];
    const float n = len3(K.delta);
    if (n > 0.0f) for (int i = 0; i < 3; ++i) dir[i] = K.delta[i] / n;
    rem = n * 256.0f;
    if (K.action < 0) return -1;
    if (actionClip_[static_cast<std::size_t>(K.action)] == 0) return -1;
    counter_ = (counter_ + 1) & 0x7FFFFFFFu;
    if ((counter_ & 1u) == 0) return -1;
    return K.action;
}

bool Sliders::checkAhead(Pedestrian& m, const float p[3], int candidate) {
    // `sub_455230` / `sub_4552B0` / `sub_455340`'s test on ONE candidate: not
    // in an action, it is the one ahead; within the two radii the follower is
    // blocked. True when the candidate counted.
    const auto& o = walkers_[static_cast<std::size_t>(candidate)];
    if (o.flags & 0x280u) return false;
    m.ahead = candidate;
    const float dx = p[0] - o.pos[0], dy = p[1] - o.pos[1], dz = p[2] - o.pos[2];
    const float r = o.radius + m.radius;
    if (r * r > dx * dx + dy * dy + dz * dz) m.flags |= 1u;
    return true;
}

bool Sliders::checkAheadOnLane(Pedestrian& m, const float p[3], int lane, int fromKey) {
    // `sub_455340`: `fromKey` 1 is the lane's own head list; otherwise the
    // key lists from segment `fromKey` onward, the first with a head that is
    // not in an action
    if (fromKey == 1) {
        const auto& l = laneHead_[static_cast<std::size_t>(lane)];
        return !l.empty() && checkAhead(m, p, l[0]);
    }
    const auto& L = track_.lanes[static_cast<std::size_t>(lane)];
    for (int k = fromKey - 2; k < L.keyCount; ++k) {
        const auto& l = keyList_[static_cast<std::size_t>(L.firstKey + k)];
        if (l.empty() || (walkers_[static_cast<std::size_t>(l[0])].flags & 0x280u)) continue;
        return checkAhead(m, p, l[0]);
    }
    return false;
}

bool Sliders::reserve(const OptRoute& r, int leaving, int entering, bool vehicle) {
    // `sub_453230`: `entering` 0 is the route's own group, k > 0 its step k;
    // busy means wait; else every group in its list is marked. `leaving` the
    // same way, unmarking. -1 for neither.
    auto groupOf = [&](int seg) -> int {
        if (seg == 0) return r.group;
        return track_.steps[static_cast<std::size_t>(r.firstStep + seg - 1)].group;
    };
    if (entering > -1) {
        const int g = groupOf(entering);
        if (g != -1) {
            if (groupBusy_[static_cast<std::size_t>(g)] > 0) {
                // whose marks are they? the OTHER class's is the interesting
                // case, and the one a per-class counter could never produce
                const int veh = groupBusyVeh_[static_cast<std::size_t>(g)];
                const int ped = groupBusy_[static_cast<std::size_t>(g)] - veh;
                if (vehicle && ped > 0) ++crossWaitVeh_;
                if (!vehicle && veh > 0) ++crossWaitPed_;
                return true;
            }
            const auto& G = track_.groups[static_cast<std::size_t>(g)];
            for (int i = 0; i < G.count; ++i) {
                const std::size_t e = static_cast<std::size_t>(track_.lists[static_cast<std::size_t>(G.first + i)]);
                ++groupBusy_[e];
                if (vehicle) ++groupBusyVeh_[e];
            }
        }
    }
    if (leaving != -1) {
        const int g = groupOf(leaving);
        if (g != -1) {
            const auto& G = track_.groups[static_cast<std::size_t>(g)];
            for (int i = 0; i < G.count; ++i) {
                const std::size_t e = static_cast<std::size_t>(track_.lists[static_cast<std::size_t>(G.first + i)]);
                --groupBusy_[e];
                if (vehicle) --groupBusyVeh_[e];
            }
        }
    }
    return false;
}

int Sliders::changeSegment(int wi, int oldLane, std::uint32_t newFlags, int oldSeg) {
    // `sub_455680`: the segment index after this one, the list the mover moves
    // to, the reservation groups crossed - and -1 with the mover BLOCKED when
    // a group it needs is busy
    Pedestrian& m = walkers_[static_cast<std::size_t>(wi)];
    const auto& R = track_.routes[static_cast<std::size_t>(m.route)];
    std::vector<int>* from = nullptr;
    std::vector<int>* to = nullptr;
    int result;
    if ((m.flags ^ newFlags) & 0x10u) {
        if (!(newFlags & 0x10u)) {
            // leaving the route onto its destination lane
            reserve(R, oldSeg, -1, m.vehicle >= 0);
            m.lane = R.dest;
            ++m.laneChanges;
            const auto& L = track_.lanes[static_cast<std::size_t>(m.lane)];
            const int nr = L.routeCount > 0 ? L.routeCount : 1;
            counter_ = (counter_ + 1) & 0x7FFFFFFFu;
            from = &routeHead_[static_cast<std::size_t>(m.route)];
            m.route = L.firstRoute + static_cast<int>(counter_ % static_cast<std::uint32_t>(nr));
            to = &laneHead_[static_cast<std::size_t>(m.lane)];
            result = 1;
        } else {
            // entering the route from the lane's last key
            if (reserve(R, -1, 0, m.vehicle >= 0)) { m.flags |= 1u; return -1; }
            const auto& L = track_.lanes[static_cast<std::size_t>(oldLane)];
            from = L.keyCount - 2 >= 0 ? &keyList_[static_cast<std::size_t>(L.firstKey + L.keyCount - 2)]
                                       : &laneHead_[static_cast<std::size_t>(oldLane)];
            to = &routeHead_[static_cast<std::size_t>(m.route)];
            result = 0;
        }
    } else if (newFlags & 0x10u) {
        if (reserve(R, oldSeg, oldSeg + 1, m.vehicle >= 0)) { m.flags |= 1u; return -1; }
        return oldSeg + 1;
    } else {
        const auto& L = track_.lanes[static_cast<std::size_t>(oldLane)];
        from = oldSeg == 1 ? &laneHead_[static_cast<std::size_t>(oldLane)]
                           : &keyList_[static_cast<std::size_t>(L.firstKey + oldSeg - 2)];
        to = &keyList_[static_cast<std::size_t>(L.firstKey + oldSeg - 1)];
        result = oldSeg + 1;
    }
    from->erase(std::remove(from->begin(), from->end(), wi), from->end());
    to->insert(to->begin(), wi);
    return result;
}

void Sliders::moverStep(int wi, float dt) {
    // `sub_454F40`. Everything is computed into locals and stored only if the
    // mover is not blocked - a blocked mover stands, and recomputes the same
    // segment end from its segment start next frame.
    Pedestrian& m = walkers_[static_cast<std::size_t>(wi)];
    m.flags &= ~1u;
    std::uint32_t flags = m.flags;
    float advance = m.speed * dt;
    float rem = m.remaining - advance;
    float p[3] = {m.pos[0], m.pos[1], m.pos[2]};
    float start[3] = {m.prev[0], m.prev[1], m.prev[2]};
    float dir[3] = {m.dir[0], m.dir[1], m.dir[2]};
    int seg = m.seg;
    const int oldLane = m.lane;
    bool changed = false;
    int action = -1;
    if (rem < 0.0f) {
        if (flags & 0x80u) return;              // in an action: the carrot waits
        p[0] = m.prev[0]; p[1] = m.prev[1]; p[2] = m.prev[2];
        advance = -rem;                         // the overshoot carries over
        int next;
        int aimLane = oldLane;
        if (flags & 0x10u) {
            const auto& R = track_.routes[static_cast<std::size_t>(m.route)];
            if (seg == R.stepCount) {
                const auto& D = track_.lanes[static_cast<std::size_t>(R.dest)];
                p[0] = D.origin[0]; p[1] = D.origin[1]; p[2] = D.origin[2];
                flags &= ~0x10u;
                next = 0;
                aimLane = R.dest;                // `v51`, the destination's table row
            } else {
                const auto& S = track_.steps[static_cast<std::size_t>(R.firstStep + seg)];
                p[0] += S.delta[0]; p[1] += S.delta[1]; p[2] += S.delta[2];
                next = seg;
            }
        } else {
            next = seg;
            if (!stepLaneKey(m, p)) { flags |= 0x10u; next = -1; }
        }
        if (flags & 0x10u) {
            aimAtRouteStep(m, p, next + 1, dir, rem);
        } else {
            action = aimAtLaneKey(aimLane, next + 1, dir, rem);
            if (action >= 0) flags |= 0x80u;
        }
        start[0] = p[0]; start[1] = p[1]; start[2] = p[2];
        changed = true;
        seg = next;
    }
    p[0] += advance * dir[0] / 256.0f;
    p[1] += advance * dir[1] / 256.0f;
    p[2] += advance * dir[2] / 256.0f;

    // who is ahead: the node after mine in my own list; else the head of the
    // next segment's list (the destination lane's for a route), else the
    // route's own head
    m.ahead = -1;
    {
        std::vector<int>* mine = (m.flags & 0x10u) ? &routeHead_[static_cast<std::size_t>(m.route)]
                                                   : &listFor(oldLane, m.seg);
        const auto it = std::find(mine->begin(), mine->end(), wi);
        const std::size_t at = it == mine->end() ? mine->size() : static_cast<std::size_t>(it - mine->begin()) + 1;
        const bool nextOk = at < mine->size() && !(walkers_[static_cast<std::size_t>((*mine)[at])].flags & 0x280u);
        if (nextOk) {
            checkAhead(m, p, (*mine)[at]);
        } else if (m.flags & 0x10u) {
            checkAheadOnLane(m, p, track_.routes[static_cast<std::size_t>(m.route)].dest, 1);
        } else {
            const auto& L = track_.lanes[static_cast<std::size_t>(oldLane)];
            if (m.seg > L.keyCount || !checkAheadOnLane(m, p, oldLane, m.seg + 1)) {
                const auto& rh = routeHead_[static_cast<std::size_t>(m.route)];
                if (!rh.empty()) checkAhead(m, p, rh[0]);
            }
        }
    }
    if (m.flags & 1u) { ++m.blockedFrames; return; }   // blocked: nothing is stored, the mover stands

    int newSeg = seg;
    if (changed) {
        newSeg = changeSegment(wi, oldLane, flags, m.seg);
        if (newSeg == -1) { ++m.blockedFrames; return; }
    }
    for (int i = 0; i < 3; ++i) { m.pos[i] = p[i]; m.prev[i] = start[i]; m.dir[i] = dir[i]; }
    m.remaining = rem;
    m.flags = flags;
    m.seg = newSeg;
    if (action >= 0) m.action = action;
}

// ------------------------------------------------------------ the body

void Sliders::setClip(Pedestrian& m, const PedClip* c) {
    // `sub_455E20`
    m.clip = c;
    m.clock = 1.0f;
    m.frames = c ? c->frames : 0;
    m.footY = 0.0f;
}

int Sliders::gait(Pedestrian& m, const float toMover[3], float dist, float near, float far) {
    // `sub_455D10`: how the mover paces the body
    const float d256 = dist * 256.0f;
    if (d256 < 512.0f) {
        if (m.speed == 0.0f) m.speed = 256.0f;
        return 0;                                            // the idle
    }
    setHeading(m, toMover[0], toMover[1], toMover[2]);       // `sub_453330`
    const int walking = (m.flags & 1u) ? 2 : 1;
    if (d256 >= near * 256.0f) {
        m.speed = (far * 256.0f >= d256) ? m.baseSpeed : 0.0f;
        return walking;
    }
    if (m.flags & 1u) return 0;
    m.speed = m.baseSpeed + m.baseSpeed;
    return 1;
}

bool Sliders::beginAction(Pedestrian& m, int actionIndex) {
    // `sub_455830`'s 0x80 branch: a state for the point, its clips by the
    // point's id and by name, the point placed relative to the segment start
    ActionState* s = nullptr;
    for (auto& st : states_) if (!st.used) { s = &st; break; }
    if (!s) return false;
    const auto& A = track_.actions[static_cast<std::size_t>(actionIndex)];
    const int id = actionClip_[static_cast<std::size_t>(actionIndex)];
    const PedClip* main = clips_.bySlot(m.sex, id);
    if (!main) return false;
    s->used = true;
    s->main = main;
    s->enter = clips_.byNameType(m.sex, main->name, 14);
    s->exit = clips_.byNameType(m.sex, main->name, 16);
    for (int i = 0; i < 3; ++i) s->point[i] = m.prev[i] + A.point[i];
    s->actionIndex = actionIndex;
    s->savedClip = id;
    s->facing = A.facing;
    s->phase = 0;
    s->count = A.count;
    actionClip_[static_cast<std::size_t>(actionIndex)] = 0;
    m.action = actionIndex;
    ++m.actionsVisited;
    return true;
}

void Sliders::bodyStep(int wi, float dt) {
    // `sub_455830`
    Pedestrian& m = walkers_[static_cast<std::size_t>(wi)];
    const std::uint32_t oldFlags = m.flags;
    m.ahead = -1;
    m.action = -1;
    moverStep(wi, dt);
    std::uint32_t flags = m.flags;
    // overtaking: a slower walker ahead on my segment, within 39 units
    if (m.ahead >= 0) {
        const Pedestrian& a = walkers_[static_cast<std::size_t>(m.ahead)];
        if (a.baseSpeed < m.baseSpeed && !(a.flags & 1u) && !((flags ^ a.flags) & 0x10u) &&
            ((flags & 0x10u) || m.seg == a.seg)) {
            const float dx = a.body[0] - m.body[0], dz = a.body[2] - m.body[2];
            const float sumR = a.radius + m.radius;
            const float dist = len2(dx, dz);
            flags &= ~1u;
            if (dist <= kOvertakeReach && dist > 0.0f) {
                const float nx = dx / dist, nz = dz / dist;
                // the two swap places in their list
                std::vector<int>* list = (flags & 0x10u) ? &routeHead_[static_cast<std::size_t>(m.route)] : &listFor(m.lane, m.seg);
                auto i1 = std::find(list->begin(), list->end(), wi);
                auto i2 = std::find(list->begin(), list->end(), m.ahead);
                if (i1 != list->end() && i2 != list->end()) std::iter_swap(i1, i2);
                m.side[0] = (nx - nz) * sumR + dx;
                m.side[1] = (nz + nx) * sumR + dz;
                m.sideLen = len2(m.side[0], m.side[1]);
                flags |= 0x20u;
                ++m.overtakes;
            }
        }
    }
    m.flags = flags;
    if ((flags & 0x80u) && m.action >= 0) {
        if (!beginAction(m, m.action)) { m.flags &= ~0x80u; m.action = -1; }
    } else if (flags & 0x80u) {
        m.flags &= ~0x80u;
    }
    if (!m.clip) return;

    // the body: the walk clip's root motion, aimed at the mover
    const float offBefore[2] = {m.pos[0] - m.body[0], m.pos[2] - m.body[2]};
    float t0 = m.clock;
    float clock = m.speedFactor * dt + m.clock;
    if (clock >= static_cast<float>(m.frames)) {
        clock = clock + 1.0f - static_cast<float>(m.frames);
        t0 = 0.0f;
        m.footY = 0.0f;
    }
    float d[3];
    pedRootDelta(*m.clip, t0, clock, m.heading, d);
    m.clock = clock;
    m.body[0] += d[0]; m.footY += d[1]; m.body[2] += d[2];
    const float to[3] = {m.pos[0] - m.body[0], m.pos[1] - m.body[1], m.pos[2] - m.body[2]};
    float dist = len3(to);
    // offBefore is the (x, z) pair: its z is [1]. Reading [2] walked one
    // float off the end of the array, so the side-step countdown below was
    // fed a distance with a garbage z. `-Warray-bounds` had been flagging it.
    const float moved = len2(to[0] - offBefore[0], to[2] - offBefore[1]);
    std::uint32_t wanted;
    if (m.flags & 0x20u) {
        m.sideLen -= moved;
        if (m.sideLen <= 0.0f) m.flags &= ~0x20u;
        setHeading(m, m.side[0], 0.0f, m.side[1]);
        wanted = oldFlags & ~0x100u;
    } else {
        wanted = gait(m, to, dist, kGaitNear, kGaitFar) == 0 ? (oldFlags | 0x100u) : (oldFlags & ~0x100u);
    }
    if ((m.flags ^ wanted) & 0x100u) {
        if (wanted & 0x100u) { setClip(m, clips_.randomOfType(m.sex, 11, rng_)); m.flags |= 0x100u; }
        else                 { setClip(m, clips_.randomOfType(m.sex, 9, rng_));  m.flags &= ~0x100u; }
    }
    if (dist == 0.0f) dist = 1.0f;
    m.body[1] += moved * to[1] / dist;
    // NaN TRAP (temporary, omk-play merge regression): a walker whose body
    // goes non-finite is never rejected by the crowd index's reach test (a
    // comparison against NaN is false), so it reaches the player's push and
    // blacks his frame. Name the step that did it, with its inputs.
    {
        static bool told = false;
        if (!told && (!std::isfinite(m.body[0]) || !std::isfinite(m.body[1]) ||
                      !std::isfinite(m.body[2]))) {
            told = true;
            std::printf("NaN: bodyStep %d '%s' -> body %f %f %f | target(pos) %f %f %f | "
                        "prev %f %f %f | dir %f %f %f | to %f %f %f | dist %f moved %f | "
                        "rootDelta %f %f %f | speed %f remaining %f seg %d lane %d "
                        "flags %08x\n",
                        wi, m.model.c_str(), m.body[0], m.body[1], m.body[2],
                        m.pos[0], m.pos[1], m.pos[2], m.prev[0], m.prev[1], m.prev[2],
                        m.dir[0], m.dir[1], m.dir[2], to[0], to[1], to[2], dist, moved,
                        d[0], d[1], d[2], m.speed, m.remaining, m.seg, m.lane, m.flags);
        }
    }
}

// ------------------------------------------------------------ the action

bool Sliders::advanceActionClip(Pedestrian& m, float dt, float delta[3]) {
    // `sub_4561B0`: the clip clock, false on the wrap
    bool playing = true;
    float t0 = m.clock;
    float clock = m.speedFactor * dt + m.clock;
    if (clock >= static_cast<float>(m.frames)) {
        t0 = 0.0f;
        clock = clock + 1.0f - static_cast<float>(m.frames);
        m.footY = 0.0f;
        playing = false;
    }
    if (m.clip) pedRootDelta(*m.clip, t0, clock, m.heading, delta);
    else delta[0] = delta[1] = delta[2] = 0.0f;
    m.clock = clock;
    return playing;
}

void Sliders::finishAction(Pedestrian& m, ActionState& s) {
    setClip(m, clips_.randomOfType(m.sex, 9, rng_));
    m.flags &= ~0x80u;
    actionClip_[static_cast<std::size_t>(s.actionIndex)] = static_cast<std::int16_t>(s.savedClip);
    s.used = false; s.main = nullptr;
    m.action = -1;
}

void Sliders::actionTransition(Pedestrian& m, ActionState& s) {
    // `sub_456250`: enter -> main, main looped `count` times unless the
    // player is talking to this walker, exit if the library has one, then the
    // walk again and the point's id restored
    if (s.phase == 1) { s.phase = 2; setClip(m, s.main); return; }
    if (s.phase == 2) {
        const int idx = static_cast<int>(&m - walkers_.data());
        if (idx != talkTarget_ && --s.count < 0) {
            if (s.exit) { s.phase = 3; setClip(m, s.exit); }
            else finishAction(m, s);
        }
        return;
    }
    finishAction(m, s);
}

void Sliders::actionStep(int wi, float dt) {
    // `sub_455E90`
    Pedestrian& m = walkers_[static_cast<std::size_t>(wi)];
    if (m.action < 0) { m.flags &= ~0x80u; return; }
    ActionState* s = nullptr;
    for (auto& st : states_) if (st.used && st.actionIndex == m.action) { s = &st; break; }
    if (!s) { m.flags &= ~0x80u; m.action = -1; return; }
    float d[3];
    if (s->phase) {
        if (s->phase >= 1 && s->phase <= 3) {
            if (!advanceActionClip(m, dt, d)) {
                actionTransition(m, *s);
                if (!(m.flags & 0x80u)) return;
                advanceActionClip(m, dt, d);
            }
            m.body[0] += d[0]; m.body[1] += d[1]; m.body[2] += d[2];
        }
        return;
    }
    // phase 0: the walk clip carries the body to the point
    float t0 = m.clock;
    float clock = m.speedFactor * dt + m.clock;
    if (clock >= static_cast<float>(m.frames)) {
        clock = clock + 1.0f - static_cast<float>(m.frames);
        t0 = 0.0f;
        m.footY = 0.0f;
    }
    if (m.clip) pedRootDelta(*m.clip, t0, clock, m.heading, d); else d[0] = d[1] = d[2] = 0.0f;
    m.clock = clock;
    const float rootXZ = len2(d[0], d[2]);
    m.body[0] += d[0]; m.body[2] += d[2]; m.footY += d[1];
    const float tx = s->point[0] - m.body[0], tz = s->point[2] - m.body[2];
    const float dist = len2(tx, tz);
    if (dist > 0.0f) setHeading(m, tx, 0.0f, tz);
    if (dist >= m.radius) {
        m.body[1] += (s->point[1] - m.body[1]) * rootXZ / (dist > 0.0f ? dist : 1.0f);
        {
            static bool told = false;
            if (!told && !std::isfinite(m.body[1])) {
                told = true;
                std::printf("NaN: actionStep approach '%s' -> body %f %f %f | point %f %f %f "
                            "| rootXZ %f dist %f\n", m.model.c_str(), m.body[0], m.body[1],
                            m.body[2], s->point[0], s->point[1], s->point[2], rootXZ, dist);
            }
        }
        return;
    }
    for (int i = 0; i < 3; ++i) m.body[i] = s->point[i];
    const PedClip* first = s->enter;
    s->phase = 1;
    if (!first) { first = s->main; s->phase = 2; }
    pedHeadingOf(s->facing, m.heading);       // `sub_442120(angle, matrix)`
    m.facing = s->facing;
    setClip(m, first);
}

// ------------------------------------------------------------ the frame

void Sliders::tick(float dt) {
    // `Sliders_Tick`'s pedestrian loop
    if (!loaded_) return;
    for (int wi = 0; wi < static_cast<int>(walkers_.size()); ++wi) {
        Pedestrian& m = walkers_[static_cast<std::size_t>(wi)];
        if (!m.live || m.vehicle >= 0) continue;   // a vehicle's mover: the road loop below
        if (!(m.flags & 0x80u)) {
            bodyStep(wi, dt);
        } else {
            moverStep(wi, dt);
            actionStep(wi, dt);
        }
    }
    tickVehicles(dt);
}

int Sliders::actionPhase(int w) const {
    if (w < 0 || static_cast<std::size_t>(w) >= walkers_.size()) return -1;
    const auto& m = walkers_[static_cast<std::size_t>(w)];
    if (!(m.flags & 0x80u) || m.action < 0) return -1;
    for (const auto& st : states_) if (st.used && st.actionIndex == m.action) return st.phase;
    return -1;
}

int Sliders::nearestInFront(const float pos[3], float facingDeg) const {
    // `sub_452280`: the nearest walker within 117 units that is STANDING AT AN
    // ACTION POINT (`flags & 0x80`, state phase 2), in front of the player
    // (dot > 0.2 with his facing) and turned toward him (< -0.2)
    float best = kCarrotBehind;
    int found = -1;
    const float a = facingDeg * kPi / 180.0f;
    const float fs = -std::sin(a), fc = std::cos(a);
    for (int i = 0; i < static_cast<int>(walkers_.size()); ++i) {
        const auto& w = walkers_[static_cast<std::size_t>(i)];
        if (!w.live || !(w.flags & 0x80u) || w.action < 0) continue;
        const ActionState* s = nullptr;
        for (const auto& st : states_) if (st.used && st.actionIndex == w.action) { s = &st; break; }
        if (!s || s->phase != 2) continue;
        const float dx = w.body[0] - pos[0], dy = w.body[1] - pos[1], dz = w.body[2] - pos[2];
        if (std::fabs(dx) > best || std::fabs(dz) > best || std::fabs(dy) > best) continue;
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > best) continue;
        if (d == 0.0f) d = 1.0f;
        const float nx = dx / d, nz = dz / d;
        const float wa = w.facing * kPi / 180.0f;
        const float ws = -std::sin(wa), wc = std::cos(wa);
        if (wc * nz + ws * nx > 0.2f && fs * nx + fc * nz < -0.2f) { found = i; best = d; }
    }
    return found;
}

}  // namespace omk

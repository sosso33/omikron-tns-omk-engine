// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/gamestate.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace omk {
namespace {
constexpr std::size_t kPtrBase = 8;    // +8  + 4*k
constexpr std::size_t kCntBase = 32;   // +32 + 2*k
}

GameState GameState::fromBytes(std::span<const std::byte> d) {
    GameState s;
    s.raw_.assign(d.begin(), d.end());
    s.imageSize_ = s.raw_.size();       // before the pad: what the file held
    if (s.raw_.size() < kGameDbSize) s.raw_.resize(kGameDbSize, std::byte{0});
    return s;
}

GameState GameState::fromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return fromBytes({});
    const auto n = static_cast<std::size_t>(f.tellg());
    std::vector<std::byte> d(n);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(d.data()), static_cast<std::streamsize>(n));
    return fromBytes(d);
}

std::uint32_t GameState::u32(std::size_t o) const {
    if (o + 4 > raw_.size()) return 0;
    return static_cast<std::uint32_t>(raw_[o    ])       |
           static_cast<std::uint32_t>(raw_[o + 1]) <<  8 |
           static_cast<std::uint32_t>(raw_[o + 2]) << 16 |
           static_cast<std::uint32_t>(raw_[o + 3]) << 24;
}

std::uint16_t GameState::u16(std::size_t o) const {
    if (o + 2 > raw_.size()) return 0;
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(raw_[o]) |
        static_cast<std::uint16_t>(raw_[o + 1]) << 8);
}

std::uint32_t GameState::offset(StateArray a) const {
    return u32(kPtrBase + 4 * static_cast<std::size_t>(a));
}

std::uint16_t GameState::count(StateArray a) const {
    return u16(kCntBase + 2 * static_cast<std::size_t>(a));
}

std::int32_t GameState::var(int i) const {
    if (i < 0) return 0;
    const auto o = offset(StateArray::Variables) + 4u * static_cast<std::uint32_t>(i);
    if (o + 4 > raw_.size()) return 0;
    return static_cast<std::int32_t>(u32(o));
}

void GameState::setVar(int i, std::int32_t v) {
    if (i < 0) return;
    const auto o = offset(StateArray::Variables) + 4u * static_cast<std::uint32_t>(i);
    if (o + 4 > raw_.size()) return;             // out of range is a no-op,
    const auto u = static_cast<std::uint32_t>(v); // as it is in the reference
    for (int k = 0; k < 4; ++k)
        raw_[o + static_cast<std::size_t>(k)] = static_cast<std::byte>(u >> (8 * k));
}

int GameState::bit(StateArray a, int i) const {
    if (i < 0) return 0;
    const auto o = offset(a) + static_cast<std::uint32_t>(i) / 8u;
    if (o >= raw_.size()) return 0;
    return (static_cast<std::uint8_t>(raw_[o]) >> (i % 8)) & 1;
}

void GameState::setBit(StateArray a, int i, int value) {
    if (i < 0) return;
    const auto o = offset(a) + static_cast<std::uint32_t>(i) / 8u;
    if (o >= raw_.size()) return;
    const auto mask = static_cast<std::uint8_t>(1u << (i % 8));
    auto b = static_cast<std::uint8_t>(raw_[o]);
    b = static_cast<std::uint8_t>((b & ~mask) | (value ? mask : 0));
    raw_[o] = static_cast<std::byte>(b);
}

std::int16_t GameState::sceneOfArea(int area) const {
    if (area < 0 || area >= 259) return -1;
    const auto o = offset(StateArray::SceneOfArea) + 2u * static_cast<std::uint32_t>(area);
    return static_cast<std::int16_t>(u16(o));
}

std::int16_t GameState::currentArea() const  { return static_cast<std::int16_t>(u16(1414)); }
std::int16_t GameState::currentScene() const { return static_cast<std::int16_t>(u16(1416)); }

void GameState::setSceneOfArea(int area, std::int16_t scene) {
    if (area < 0 || area >= 259) return;
    const auto o = offset(StateArray::SceneOfArea) + 2u * static_cast<std::uint32_t>(area);
    if (o + 2 > raw_.size()) return;
    const auto u = static_cast<std::uint16_t>(scene);
    raw_[o]     = static_cast<std::byte>(u & 0xFF);
    raw_[o + 1] = static_cast<std::byte>(u >> 8);
}

std::size_t GameState::arrayBytes(StateArray a) const {
    // 32 bits for the int32 variables, 16 for the int16 scene-per-area table,
    // 2 for the prop states (ObjectState_Get shifts by 2 * (i % 4)) and 1 for
    // the three one-bit maps.
    static constexpr int kBits[6] = {32, 16, 2, 1, 1, 1};
    const auto k = static_cast<std::size_t>(a);
    return (static_cast<std::size_t>(count(a)) * static_cast<std::size_t>(kBits[k]) + 7) / 8;
}

std::vector<GameState::Segment> GameState::walk() const {
    std::vector<Segment> seg = {
        {"header", 0, 8}, {"array offsets", 8, 32}, {"array counts", 32, 44},
        {"player placement", 44, 60},
        {"player record", kPlayerRecord,
         static_cast<std::size_t>(kPlayerRecord + kPlayerRecordSize)},
        {"bio string 0", static_cast<std::size_t>(kBio[0]),
         static_cast<std::size_t>(kBio[0] + kBioSize)},
        {"bio string 1", static_cast<std::size_t>(kBio[1]),
         static_cast<std::size_t>(kBio[1] + kBioSize)},
        {"object list 0", static_cast<std::size_t>(kListOffset[0]),
         static_cast<std::size_t>(kListOffset[0] + 2 * kListCapacity[0])},
        {"object list 1", static_cast<std::size_t>(kListOffset[1]),
         static_cast<std::size_t>(kListOffset[1] + 2 * kListCapacity[1])},
        {"object list 2", static_cast<std::size_t>(kListOffset[2]),
         static_cast<std::size_t>(kListOffset[2] + 2 * kListCapacity[2])},
        {"current area", 1414, 1416}, {"current scene", 1416, 1418},
    };
    static const char* kNames[6] = {"variables", "scene_of_area", "prop_state",
                                    "object_shown", "address_enabled",
                                    "zone_state"};
    for (int k = 0; k < 6; ++k) {
        const auto a = static_cast<StateArray>(k);
        const std::size_t o = offset(a);
        seg.push_back({kNames[k], o, o + arrayBytes(a)});
    }
    std::sort(seg.begin(), seg.end(),
              [](const Segment& x, const Segment& y) { return x.begin < y.begin; });
    return seg;
}

// ----------------------------------------------------------------------------
// The object lists.  `ObjectList_SetCapacity` 0x00409B00, and the four
// inventory opcodes' handlers - see gamestate.h for what each transcribes.

std::size_t GameState::listSlot(int list, int i) const {
    if (list < 0 || list >= kLists) return std::size_t(-1);
    if (i < 0 || i >= kListCapacity[list]) return std::size_t(-1);
    const auto o = static_cast<std::size_t>(kListOffset[list] + 2 * i);
    return o + 2 <= raw_.size() ? o : std::size_t(-1);
}

std::int16_t GameState::listRead(std::size_t o) const {
    return static_cast<std::int16_t>(u16(o));
}

int GameState::listCount(int list) const {
    if (list < 0 || list >= kLists) return 0;
    int n = 0;
    for (; n < kListCapacity[list]; ++n) {
        const auto o = listSlot(list, n);
        if (o == std::size_t(-1) || listRead(o) == -1) break;   // 0xFFFF
    }
    return n;
}

std::int16_t GameState::listAt(int list, int i) const {
    const auto o = listSlot(list, i);
    return o == std::size_t(-1) ? std::int16_t(-1) : listRead(o);
}

bool GameState::listHas(int list, int id) const {
    const int n = listCount(list);
    for (int i = 0; i < n; ++i)
        if (listAt(list, i) == static_cast<std::int16_t>(id)) return true;
    return false;
}

bool GameState::listAdd(int list, int id, bool noDuplicate) {
    if (list < 0 || list >= kLists) return false;
    if (noDuplicate && listHas(list, id)) return false;
    const int cap = kListCapacity[list];
    if (listCount(list) >= cap) return false;      // ObjectList_InsertFront's
                                                   // `capacity == count` bail
    // memmove(base + 2, base, 2 * capacity - 2), then base[0] = id
    for (int i = cap - 1; i > 0; --i) {
        const auto dst = listSlot(list, i), src = listSlot(list, i - 1);
        if (dst == std::size_t(-1) || src == std::size_t(-1)) continue;
        raw_[dst]     = raw_[src];
        raw_[dst + 1] = raw_[src + 1];
    }
    const auto o = listSlot(list, 0);
    if (o == std::size_t(-1)) return false;
    put16(o, static_cast<std::int16_t>(id));
    return true;
}

bool GameState::listRemove(int list, int id) {
    if (list < 0 || list >= kLists) return false;
    const int cap = kListCapacity[list], n = listCount(list);
    int k = -1;
    for (int i = 0; i < n; ++i)
        if (listAt(list, i) == static_cast<std::int16_t>(id)) { k = i; break; }
    if (k < 0) return false;
    for (int i = k; i + 1 < cap; ++i) {
        const auto dst = listSlot(list, i), src = listSlot(list, i + 1);
        if (dst == std::size_t(-1) || src == std::size_t(-1)) continue;
        raw_[dst]     = raw_[src];
        raw_[dst + 1] = raw_[src + 1];
    }
    const auto last = listSlot(list, cap - 1);
    if (last != std::size_t(-1)) put16(last, -1);   // the vacated slot
    return true;
}

int GameState::listRemoveAll(int list, int id) {
    int gone = 0;
    while (listRemove(list, id)) ++gone;
    return gone;
}

void GameState::listClear(int list) {
    if (list < 0 || list >= kLists) return;
    for (int i = 0; i < kListCapacity[list]; ++i) {
        const auto o = listSlot(list, i);
        if (o != std::size_t(-1)) put16(o, -1);
    }
}

// ----------------------------------------------------------------------------
// The prop state.  ObjectState_Get 0x0040B010 / ObjectState_Set 0x0040AFC0.

int GameState::propState(int index) const {
    if (index < 0) return 0;
    const auto o = offset(StateArray::PropState) +
                   static_cast<std::uint32_t>(index) / 4u;
    if (o >= raw_.size()) return 0;
    const int sh = 2 * (index % 4);
    // Exactly the shipped arithmetic: a SIGNED byte, a mask built in a byte
    // register and sign-extended (0xC0 -> -64 at sh == 6), and an arithmetic
    // shift - so states 2 and 3 read back as -2 and -1 for index % 4 == 3.
    const int byte = static_cast<std::int8_t>(raw_[o]);
    const int mask = static_cast<std::int8_t>(static_cast<std::uint8_t>(3u << sh));
    return (byte & mask) >> sh;
}

int GameState::propStateBits(int index) const {
    if (index < 0) return 0;
    const auto o = offset(StateArray::PropState) +
                   static_cast<std::uint32_t>(index) / 4u;
    if (o >= raw_.size()) return 0;
    return (static_cast<std::uint8_t>(raw_[o]) >> (2 * (index % 4))) & 3;
}

void GameState::setPropState(int index, int value) {
    if (index < 0) return;
    const auto o = offset(StateArray::PropState) +
                   static_cast<std::uint32_t>(index) / 4u;
    if (o >= raw_.size()) return;
    const int sh = 2 * (index % 4);
    const auto mask = static_cast<std::uint8_t>(3u << sh);
    auto b = static_cast<std::uint8_t>(raw_[o]);
    b = static_cast<std::uint8_t>((b & ~mask) |
                                 ((static_cast<unsigned>(value) & 3u) << sh));
    raw_[o] = static_cast<std::byte>(b);
}

// ----------------------------------------------------------------------------
// The clock and the script timer.  Clock_Tick 0x0041E600, and the five entry
// points at 0x0041E260 / 0x0041E2B0 / 0x0041E2D0 / 0x0041E270 / 0x0041E290,
// read with Timer_Elapsed 0x0041E430 and expired by sub_41E480's tail.

void GameState::clockTick(float frames) {
    clockAccum_ += frames;
    if (clockAccum_ > 5.0f) {                 // strictly greater, as shipped
        const std::int32_t before = clockTime_ / kClockUnitsPerDay;
        clockAccum_ -= 5.0f;
        clockTime_ += 166;
        clockDay_ += clockTime_ / kClockUnitsPerDay - before;
    }
}

void GameState::timerReset() { timerFlags_ = kTimerStopped; }

bool GameState::timerStop() {                 // op 111 -> loc_41E2B0
    if (timerFlags_ & kTimerStopped) return false;
    timerFlags_ |= kTimerStopped;
    return true;
}

bool GameState::timerStart() {                // op 112 -> loc_41E2D0
    if (!(timerFlags_ & kTimerStopped)) return false;
    timerStart_ = clockTime_;
    timerFlags_ &= ~(kTimerStopped | kTimerExpired);   // and 0FFFFFFEEh
    return true;
}

bool GameState::timerSet(std::int32_t ms) {   // op 113 -> Timer_SetValue
    if (!(timerFlags_ & kTimerStopped)) return false;
    timerValue_ = ms;
    return true;
}

bool GameState::timerMode(int mode) {         // op 114 -> Timer_SetMode
    if (!(timerFlags_ & kTimerStopped)) return false;
    timerFlags_ = mode | kTimerStopped;
    return true;
}

bool GameState::timerElapsed(std::int32_t& out) const {
    if (timerFlags_ & kTimerExpired) { out = timerValue_; return false; }
    if (timerFlags_ == kTimerStopped) { out = 0; return false; }
    out = clockTime_ - timerStart_;
    return true;
}

bool GameState::timerCheckExpiry() {
    if (timerFlags_ & kTimerStopped) return false;        // the early bail
    if (timerFlags_ & kTimerExpired) return false;
    if (clockTime_ <= timerValue_ + timerStart_) return false;
    timerStart_ = clockTime_;
    timerFlags_ |= kTimerExpired;
    return true;
}

void GameState::put32(std::size_t o, std::int32_t v) {
    if (o + 4 > raw_.size()) return;
    const auto u = static_cast<std::uint32_t>(v);
    for (int k = 0; k < 4; ++k) raw_[o + static_cast<std::size_t>(k)] =
        static_cast<std::byte>((u >> (8 * k)) & 0xFF);
}

void GameState::put16(std::size_t o, std::int16_t v) {
    if (o + 2 > raw_.size()) return;
    const auto u = static_cast<std::uint16_t>(v);
    raw_[o]     = static_cast<std::byte>(u & 0xFF);
    raw_[o + 1] = static_cast<std::byte>(u >> 8);
}

void GameState::relocate() {
    put32(kPlayerRecord + 0, kBio[0]);
    put32(kPlayerRecord + 4, kBio[1]);
    setSceneOfArea(currentArea(), currentScene());
}

void GameState::unrelocate() {
    put16(1414, currentArea());
    put16(1416, currentScene());
}

}  // namespace omk

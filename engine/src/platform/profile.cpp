// SPDX-License-Identifier: GPL-3.0-or-later
// The profiler's probes - see profile.h.
#include "platform/profile.h"

#if OMK_PROFILE

#include "platform/datafs.h"
#include "platform/threads.h"

#include <cstdio>
#include <unordered_map>
#include <vector>
#if OMK_THREADS
#  include <thread>
#endif

namespace omk::prof {
namespace {

constexpr std::size_t kZones = 16384;     // a frame's zones; past it, counted as dropped
constexpr int kDepth = 64;

struct Rec { const char* name; std::uint64_t t0, t1; std::uint16_t depth, kind; };

struct State {
    std::FILE* file = nullptr;
    Clock clock = nullptr;
    std::uint64_t hz = 1;
    std::uint64_t openedAt = 0;
    long frame = 0;
    std::vector<Rec> zones;             // the frame's, in opening order
    int stack[kDepth] = {};             // indices into `zones`; -1 a dropped zone
    int sp = 0;
    std::uint32_t dropped = 0;
    std::unordered_map<const char*, std::uint32_t> names;
    std::vector<unsigned char> out;
#if OMK_THREADS
    std::thread::id owner;
#endif
};

State& st() {                            // made on first use, never destroyed
    static State* s = new State;
    return *s;
}

bool g_on = false;

bool mine() {
#if OMK_THREADS
    return std::this_thread::get_id() == st().owner;
#else
    return true;
#endif
}

void put8(unsigned v) { st().out.push_back(static_cast<unsigned char>(v)); }
void put16(unsigned v) { put8(v & 0xFF); put8((v >> 8) & 0xFF); }
void put32(std::uint32_t v) { for (int k = 0; k < 4; ++k) put8((v >> (8 * k)) & 0xFF); }
void put64(std::uint64_t v) { for (int k = 0; k < 8; ++k) put8(static_cast<unsigned>((v >> (8 * k)) & 0xFF)); }

std::uint32_t us(std::uint64_t ticks) {
    return static_cast<std::uint32_t>(static_cast<double>(ticks) * 1e6 / static_cast<double>(st().hz));
}

// A chunk's length is patched in once its payload is written.
std::size_t beginChunk(unsigned type) {
    put8(type);
    const std::size_t at = st().out.size();
    put32(0);
    return at;
}
void endChunk(std::size_t at) {
    auto& o = st().out;
    const auto n = static_cast<std::uint32_t>(o.size() - at - 4);
    for (int k = 0; k < 4; ++k) o[at + static_cast<std::size_t>(k)] = static_cast<unsigned char>((n >> (8 * k)) & 0xFF);
}

std::uint32_t nameId(const char* name) {
    auto& s = st();
    const auto it = s.names.find(name);
    if (it != s.names.end()) return it->second;
    const auto id = static_cast<std::uint32_t>(s.names.size());
    s.names.emplace(name, id);
    const std::size_t at = beginChunk(1);
    put32(id);
    for (const char* p = name; *p; ++p) put8(static_cast<unsigned char>(*p));
    endChunk(at);
    return id;
}

}  // namespace

void setClock(Clock c, std::uint64_t hz) {
    st().clock = c;
    st().hz = hz ? hz : 1;
}

std::uint64_t now() { return st().clock ? st().clock() : 0; }

bool open(const std::string& path) {
    close();
    if (!safeOutputPath(path)) return false;
    State& s = st();
    s.file = std::fopen(hostPath(path).c_str(), "wb");
    if (!s.file) return false;
#if OMK_THREADS
    s.owner = std::this_thread::get_id();
#endif
    s.zones.reserve(kZones);
    s.names.clear();
    s.out.clear();
    for (const char* m = "OMKPROF1"; *m; ++m) put8(static_cast<unsigned char>(*m));
    put32(1);
    std::fwrite(s.out.data(), 1, s.out.size(), s.file);
    s.out.clear();
    s.openedAt = now();
    s.sp = 0;
    s.zones.clear();
    g_on = true;
    return true;
}

void close() {
    State& s = st();
    g_on = false;
    if (s.file) { std::fclose(s.file); s.file = nullptr; }
}

bool on() { return g_on; }

void enter(const char* name) {
    if (!g_on || !mine()) return;
    State& s = st();
    if (s.sp >= kDepth) { ++s.dropped; return; }
    if (s.zones.size() >= kZones) { s.stack[s.sp++] = -1; ++s.dropped; return; }
    s.stack[s.sp++] = static_cast<int>(s.zones.size());
    s.zones.push_back({name, now(), 0, static_cast<std::uint16_t>(s.sp - 1), 0});
}

void leave() {
    if (!g_on || !mine()) return;
    State& s = st();
    if (s.sp <= 0) return;
    const int i = s.stack[--s.sp];
    if (i >= 0) s.zones[static_cast<std::size_t>(i)].t1 = now();
}

void section(const char* name, std::uint64_t t0, std::uint64_t t1) {
    if (!g_on || !mine()) return;
    State& s = st();
    if (s.zones.size() >= kZones) { ++s.dropped; return; }
    s.zones.push_back({name, t0, t1, 0, 1});
}

void beginFrame(long frame) {
    if (!g_on) return;
    State& s = st();
    s.frame = frame;
    s.zones.clear();
    s.sp = 0;
    s.dropped = 0;
    enter("frame");
}

void endFrame() {
    if (!g_on || !mine()) return;
    State& s = st();
    while (s.sp > 0) leave();               // anything left open closes with the frame
    if (s.zones.empty()) return;
    const std::uint64_t f0 = s.zones.front().t0, f1 = s.zones.front().t1;
    // the names first, so the reader meets each before a frame uses it
    std::vector<std::uint32_t> ids;
    ids.reserve(s.zones.size());
    for (const Rec& z : s.zones) ids.push_back(nameId(z.name));
    const std::size_t at = beginChunk(2);
    put32(static_cast<std::uint32_t>(s.frame));
    put64(us(f0 - s.openedAt));
    put32(us(f1 - f0));
    put32(static_cast<std::uint32_t>(s.zones.size()));
    put32(s.dropped);
    for (std::size_t k = 0; k < s.zones.size(); ++k) {
        const Rec& z = s.zones[k];
        put32(ids[k]);
        put16(z.depth);
        put16(z.kind);
        put32(us(z.t0 - f0));
        put32(us((z.t1 >= z.t0 ? z.t1 : z.t0) - z.t0));
    }
    endChunk(at);
    std::fwrite(s.out.data(), 1, s.out.size(), s.file);
    std::fflush(s.file);                    // a live reader sees each frame as it ends
    s.out.clear();
    s.zones.clear();
}

}  // namespace omk::prof

#endif  // OMK_PROFILE

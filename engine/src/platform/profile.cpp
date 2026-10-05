// SPDX-License-Identifier: GPL-3.0-or-later
// The profiler's probes - see profile.h.
#include "platform/profile.h"

#if OMK_PROFILE

#include "platform/datafs.h"
#include "platform/threads.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <unordered_map>
#include <vector>
#if OMK_THREADS
#  include <atomic>
#  include <mutex>
#  include <thread>
#endif

// ---- THE MEMORY COUNTERS (step 4) -------------------------------------------
//
// Outside `omk::prof` so `operator new` below can reach them with nothing
// constructed: every one is constant-initialised (it runs before `main`).
namespace omk::prof::mem {

constexpr int kTags = 128;
#if OMK_THREADS
using Count = std::atomic<long long>;
#  if defined(__vita__)
// On the Vita a `thread_local` is SHARED by the kernel threads `omk::Threads`
// makes (`actor/pose.cpp`), so the category is the main thread's alone there:
// a worker's blocks are counted as "worker threads" and its scopes swap
// nothing (`memTagSwap`).
int t_tag = 0;
const std::thread::id g_main = std::this_thread::get_id();   // static init runs on main
#  else
thread_local int t_tag = 0;
#  endif
std::mutex g_tagLock;
#else
using Count = long long;
int t_tag = 0;
#endif
Count g_live[kTags] = {};
Count g_blocks[kTags] = {};
Count g_total{0}, g_peak{0}, g_framePeak{0}, g_totalBlocks{0};
const char* g_names[kTags] = {"untagged"};
int g_count = 1;

inline void raise(Count& peak, long long v) {
#if OMK_THREADS
    long long cur = peak.load(std::memory_order_relaxed);
    while (v > cur && !peak.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
#else
    if (v > peak) peak = v;
#endif
}

inline void account(int tag, long long bytes, long blocks) {
    g_live[tag] += bytes;
    g_blocks[tag] += blocks;
    const long long now = (g_total += bytes);
    g_totalBlocks += blocks;
    if (bytes > 0) { raise(g_peak, now); raise(g_framePeak, now); }
}

// The header before every block: its size, its category and the code that
// allocated it (the SITE - `todo/ram-vs-original.md` 1), 32 bytes so the block
// keeps `malloc`'s alignment.
struct Header { std::size_t size; std::uint32_t site; std::uint32_t tag; std::uint32_t magic; };
constexpr std::size_t kHeader = 32;
static_assert(sizeof(Header) <= kHeader, "the header fits its 32 bytes");

// THE SITES: live bytes and blocks per allocating return address, in a FIXED
// open-addressed table - nothing here may allocate, since this runs inside
// `operator new`. A full table files the rest under one overflow entry.
#if defined(macintosh)
// on classic Mac OS the table is in the image's data, inside the very
// partition being measured: 4096 sites, 96 KB
constexpr std::size_t kSites = 1 << 12;
#else
constexpr std::size_t kSites = 1 << 15;
#endif
// A site is a short CALL CHAIN, not one address: the code that calls `new` is
// most often a container's own (`vector::__append`), one level below the code
// that owns the data, and the linker folds identical template code into one
// symbol. Four return addresses, by the frame-pointer chain where the
// platform keeps one (macOS on arm64 and x86-64 does); one elsewhere.
constexpr int kDepth = 4;
struct Site { const void* at[kDepth]; long long bytes; long blocks; };
Site g_sites[kSites] = {};
std::size_t g_sitesUsed = 0;
#if OMK_THREADS
std::atomic_flag g_siteLock = ATOMIC_FLAG_INIT;
struct SiteLock {
    SiteLock() { while (g_siteLock.test_and_set(std::memory_order_acquire)) {} }
    ~SiteLock() { g_siteLock.clear(std::memory_order_release); }
};
#else
struct SiteLock {};
#endif

struct Chain { const void* at[kDepth]; };

// -> the site's index in `g_sites` (the header keeps it, so a free finds its
// row without the chain); the last row is the overflow
std::uint32_t siteOf(const Chain& c) {
    std::uintptr_t h = 0;
    for (const void* a : c.at) h = (h ^ reinterpret_cast<std::uintptr_t>(a)) * 0x9E3779B97F4A7C15ull;
    for (std::size_t i = 0; i < kSites - 1; ++i) {
        const std::size_t k = (h + i) & (kSites - 2);       // the last row stays the overflow
        Site& s = g_sites[k];
        bool same = s.at[0] != nullptr;
        for (int d = 0; same && d < kDepth; ++d) same = s.at[d] == c.at[d];
        if (same) return static_cast<std::uint32_t>(k);
        if (!s.at[0]) {
            if (g_sitesUsed + 1 >= kSites / 2) break;      // keep probes short
            for (int d = 0; d < kDepth; ++d) s.at[d] = c.at[d];
            ++g_sitesUsed;
            return static_cast<std::uint32_t>(k);
        }
    }
    return static_cast<std::uint32_t>(kSites - 1);
}

std::uint32_t siteAccount(const Chain& c, long long bytes, long blocks) {
    SiteLock lk;
    const std::uint32_t k = siteOf(c);
    g_sites[k].bytes += bytes;
    g_sites[k].blocks += blocks;
    return k;
}

void siteRelease(std::uint32_t k, long long bytes) {
    if (k >= kSites) return;
    SiteLock lk;
    g_sites[k].bytes -= bytes;
    g_sites[k].blocks -= 1;
}

// The chain: `first` is `operator new`'s own return address; the frames above
// it by the frame pointers, checked to climb the stack, where the platform
// keeps them.
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__x86_64__))
__attribute__((always_inline)) inline Chain chainFrom(const void* first, void* const* fp) {
    Chain c{};
    c.at[0] = first;
    // Up from `counted`'s own frame: each record is {the caller's frame, the
    // return address into it}. Find the record whose return address is
    // `first` - wherever it is, so a tail call from `operator new` into
    // `counted` (no frame of its own) changes nothing - and take the ones
    // above it.
    void* const* f = fp;
    bool found = false;
    int d = 1;
    for (int guard = 0; f && guard < 8 + kDepth && d < kDepth; ++guard) {
        const void* ra = f[1];
        if (found) c.at[d++] = ra;
        else if (ra == first) found = true;
        void* const* up = static_cast<void* const*>(f[0]);
        if (up <= f || reinterpret_cast<std::uintptr_t>(up) - reinterpret_cast<std::uintptr_t>(f) > (1u << 24)) break;
        f = up;
    }
    return c;
}
#else
inline Chain chainFrom(const void* first, void* const*) { Chain c{}; c.at[0] = first; return c; }
#endif
constexpr std::uint32_t kMagic = 0x4F4D4B6Du;   // "OMKm"

int workersTag();

__attribute__((noinline)) void* counted(std::size_t n, const void* site) {
    auto* p = static_cast<unsigned char*>(std::malloc(n + kHeader));
    if (!p) {
        // A REFUSED ALLOCATION SAYS ITS SIZE - the Vita's own `operator new`
        // did (vita_main.cpp, a city load dead at 86 MB of 192); this one
        // takes its place in a profiling build, so it says it too
        std::printf("new: %lu bytes REFUSED - %ld KB live in %ld counted blocks\n",
                    static_cast<unsigned long>(n), static_cast<long>(static_cast<long long>(g_total) / 1024),
                    static_cast<long>(static_cast<long long>(g_totalBlocks)));
        throw std::bad_alloc();
    }
#if OMK_THREADS && defined(__vita__)
    const int tag = std::this_thread::get_id() == g_main ? t_tag : workersTag();
#else
    const int tag = t_tag;
#endif
    auto* h = reinterpret_cast<Header*>(p);
    h->size = n;
    h->tag = static_cast<std::uint32_t>(tag);
    h->magic = kMagic;
    account(tag, static_cast<long long>(n), 1);
    h->site = siteAccount(chainFrom(site, static_cast<void* const*>(__builtin_frame_address(0))),
                          static_cast<long long>(n), 1);
    return p + kHeader;
}

void uncounted(void* q) {
    if (!q) return;
    auto* p = static_cast<unsigned char*>(q) - kHeader;
    auto* h = reinterpret_cast<Header*>(p);
    if (h->magic == kMagic && h->tag < static_cast<std::uint32_t>(kTags)) {
        account(static_cast<int>(h->tag), -static_cast<long long>(h->size), -1);
        siteRelease(h->site, static_cast<long long>(h->size));
    }
    h->magic = 0;
    std::free(p);
}

}  // namespace omk::prof::mem

// The standard's replaceable allocation functions: defined here, they take
// the place of the library's in every program linking the engine - a
// PROFILING build only (`OMK_PROFILE=0` has none of this file).
void* operator new(std::size_t n) { return omk::prof::mem::counted(n, __builtin_return_address(0)); }
void* operator new[](std::size_t n) { return omk::prof::mem::counted(n, __builtin_return_address(0)); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return omk::prof::mem::counted(n, __builtin_return_address(0)); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try { return omk::prof::mem::counted(n, __builtin_return_address(0)); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { omk::prof::mem::uncounted(p); }
void operator delete[](void* p) noexcept { omk::prof::mem::uncounted(p); }
void operator delete(void* p, std::size_t) noexcept { omk::prof::mem::uncounted(p); }
void operator delete[](void* p, std::size_t) noexcept { omk::prof::mem::uncounted(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { omk::prof::mem::uncounted(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { omk::prof::mem::uncounted(p); }

namespace omk::prof {

int memTag(const char* name) {
    using namespace mem;
#if OMK_THREADS
    std::lock_guard<std::mutex> lk(g_tagLock);
#endif
    for (int i = 0; i < g_count; ++i)
        if (g_names[i] == name || std::strcmp(g_names[i], name) == 0) return i;
    if (g_count >= kTags) return 0;              // full: counted as untagged
    g_names[g_count] = name;
    return g_count++;
}

int memTagSwap(int id) {
#if OMK_THREADS && defined(__vita__)
    if (std::this_thread::get_id() != mem::g_main) return id;   // see mem::t_tag
#endif
    const int prev = mem::t_tag;
    mem::t_tag = id;
    return prev;
}

namespace mem {
int workersTag() {
    static const int id = memTag("worker threads");
    return id;
}
}  // namespace mem

// ---- THE GPU MEMORY (step 5) ------------------------------------------------
namespace {
struct GpuRes { int tag; std::uint64_t bytes; };
struct Gpu {
#if OMK_THREADS
    std::mutex lock;
#endif
    std::unordered_map<std::uint64_t, GpuRes> res;
    long long live[mem::kTags] = {};
    long count[mem::kTags] = {};
    long long total = 0;
};
Gpu& gpu() {                              // made on first use, never destroyed
    static Gpu* g = new Gpu;
    return *g;
}
}  // namespace

void gpuAlloc(const char* tag, std::uint64_t key, std::uint64_t bytes) {
    const int id = memTag(tag);
    Gpu& g = gpu();
#if OMK_THREADS
    std::lock_guard<std::mutex> lk(g.lock);
#endif
    auto it = g.res.find(key);
    if (it != g.res.end()) {              // re-specified: the old storage goes
        g.live[it->second.tag] -= static_cast<long long>(it->second.bytes);
        --g.count[it->second.tag];
        g.total -= static_cast<long long>(it->second.bytes);
        it->second = {id, bytes};
    } else {
        g.res.emplace(key, GpuRes{id, bytes});
    }
    g.live[id] += static_cast<long long>(bytes);
    ++g.count[id];
    g.total += static_cast<long long>(bytes);
}

void gpuFree(std::uint64_t key) {
    Gpu& g = gpu();
#if OMK_THREADS
    std::lock_guard<std::mutex> lk(g.lock);
#endif
    auto it = g.res.find(key);
    if (it == g.res.end()) return;
    g.live[it->second.tag] -= static_cast<long long>(it->second.bytes);
    --g.count[it->second.tag];
    g.total -= static_cast<long long>(it->second.bytes);
    g.res.erase(it);
}

MemTotals memTotals() {
    MemTotals t;
    t.live = mem::g_total;
    t.peak = mem::g_peak;
    t.blocks = static_cast<long>(mem::g_totalBlocks);
    return t;
}

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
    // the control (step 3)
    std::string base;                   // the capture's path, the three files beside it
    long seenSeq = -1;
    bool paused = false;
    long steps = 0;                     // frames owed while paused
    bool snapOwed = false;
    long snaps = 0;
    std::string told;                   // the last state written
    bool toldPaused = false;            // ...what it said, apart from the frame
    long toldSnaps = -1;
    long toldFrame = -1000000;
    int tagsWritten = 0;                // TAG chunks already in the capture
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
    s.base = path;
    s.seenSeq = -1;
    s.paused = false;
    s.steps = 0;
    s.snapOwed = false;
    s.snaps = 0;
    s.told.clear();
    s.tagsWritten = 0;
    // a control file from an earlier run is not a command to this one
    if (std::FILE* c = std::fopen(hostPath(path + ".ctl").c_str(), "r")) {
        long q = -1;
        if (std::fscanf(c, "%ld", &q) == 1) s.seenSeq = q;
        std::fclose(c);
    }
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

namespace {
// THE SITE TABLE (chunk 6) - live bytes per allocating return address, with
// the runtime address of one known function (`omk::prof::open`) so the reader
// can find the image's slide and name every site (`omkprof.py --sites`).
void writeSites() {
    State& s = st();
    // reserved BEFORE the lock: growing it inside would call `operator new`,
    // which takes the same lock
    std::vector<mem::Site> live;
    live.reserve(mem::kSites);
    {
        mem::SiteLock lk;
        for (const mem::Site& x : mem::g_sites)
            if (x.at[0] && x.bytes > 0) live.push_back(x);
    }
    const std::size_t at = beginChunk(6);
    put64(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
        reinterpret_cast<const void*>(static_cast<bool (*)(const std::string&)>(&open)))));
    put32(static_cast<std::uint32_t>(live.size()));
    put32(static_cast<std::uint32_t>(mem::kDepth));
    for (const mem::Site& x : live) {
        for (int d = 0; d < mem::kDepth; ++d)
            put64(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(x.at[d])));
        put64(static_cast<std::uint64_t>(x.bytes));
        put32(static_cast<std::uint32_t>(x.blocks));
    }
    endChunk(at);
    std::fwrite(s.out.data(), 1, s.out.size(), s.file);
    s.out.clear();
}
}  // namespace

void close() {
    State& s = st();
    if (s.file && g_on) writeSites();
    g_on = false;
    if (s.file) { std::fclose(s.file); s.file = nullptr; }
}

bool on() { return g_on; }

bool recording() { return g_on && mine(); }

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
    mem::g_framePeak = static_cast<long long>(mem::g_total);   // the frame's own peak from here
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
    // THE MEMORY at the frame's end, every category that ever held a block
    {
        const int n = mem::g_count;
        for (; s.tagsWritten < n; ++s.tagsWritten) {
            const std::size_t t = beginChunk(4);
            put32(static_cast<std::uint32_t>(s.tagsWritten));
            for (const char* p = mem::g_names[s.tagsWritten]; *p; ++p) put8(static_cast<unsigned char>(*p));
            endChunk(t);
        }
        const std::size_t m = beginChunk(3);
        put32(static_cast<std::uint32_t>(s.frame));
        put64(static_cast<std::uint64_t>(static_cast<long long>(mem::g_total)));
        put64(static_cast<std::uint64_t>(static_cast<long long>(mem::g_framePeak)));
        put32(static_cast<std::uint32_t>(static_cast<long long>(mem::g_totalBlocks)));
        int used = 0;
        for (int i = 0; i < n; ++i)
            if (static_cast<long long>(mem::g_blocks[i]) != 0) ++used;
        put32(static_cast<std::uint32_t>(used));
        for (int i = 0; i < n; ++i) {
            const long long b = mem::g_blocks[i];
            if (b == 0) continue;
            put32(static_cast<std::uint32_t>(i));
            put64(static_cast<std::uint64_t>(static_cast<long long>(mem::g_live[i])));
            put32(static_cast<std::uint32_t>(b));
        }
        endChunk(m);
    }
    // THE GPU MEMORY at the frame's end, as the renderers reported it
    {
        Gpu& g = gpu();
#if OMK_THREADS
        std::lock_guard<std::mutex> lk(g.lock);
#endif
        const int n = mem::g_count;
        const std::size_t m = beginChunk(5);
        put32(static_cast<std::uint32_t>(s.frame));
        put64(static_cast<std::uint64_t>(g.total));
        int used = 0;
        for (int i = 0; i < n; ++i) if (g.count[i]) ++used;
        put32(static_cast<std::uint32_t>(used));
        for (int i = 0; i < n; ++i) {
            if (!g.count[i]) continue;
            put32(static_cast<std::uint32_t>(i));
            put64(static_cast<std::uint64_t>(g.live[i]));
            put32(static_cast<std::uint32_t>(g.count[i]));
        }
        endChunk(m);
    }
    std::fwrite(s.out.data(), 1, s.out.size(), s.file);
    std::fflush(s.file);                    // a live reader sees each frame as it ends
    s.out.clear();
    s.zones.clear();
}

namespace {

// The game's state for the page. It carries the frame number, so it changed
// EVERY frame and was opened, written and closed every frame - 0.37 ms of each
// captured frame (todo/cpu-vs-original.md tier A). Now at once when anything
// but the frame changes (a pause, a resume, a step, a snapshot) and while
// paused, and while running only once a second for the page's frame counter.
void tellState(long frame) {
    State& s = st();
    char line[96];
    std::snprintf(line, sizeof line, "%s %ld %ld\n", s.paused ? "paused" : "running", frame, s.snaps);
    if (s.told == line) return;
    const bool sameState = s.toldPaused == s.paused && s.toldSnaps == s.snaps;
    if (sameState && !s.paused && frame - s.toldFrame < 30) return;
    s.toldPaused = s.paused; s.toldSnaps = s.snaps; s.toldFrame = frame;
    s.told = line;
    if (std::FILE* f = std::fopen(hostPath(s.base + ".state").c_str(), "w")) {
        std::fputs(line, f);
        std::fclose(f);
    }
}

}  // namespace

Run control(long frame) {
    if (!g_on || !mine()) return Run::Go;
    State& s = st();
    if (std::FILE* c = std::fopen(hostPath(s.base + ".ctl").c_str(), "r")) {
        long q = -1, n = 1;
        char cmd[32] = {};
        const int got = std::fscanf(c, "%ld %31s %ld", &q, cmd, &n);
        std::fclose(c);
        if (got >= 2 && q != s.seenSeq) {
            s.seenSeq = q;
            const std::string k = cmd;
            if (k == "pause") { s.paused = true; s.steps = 0; s.snapOwed = true; }
            else if (k == "resume") { s.paused = false; s.steps = 0; }
            else if (k == "step") { s.paused = true; s.steps += (got >= 3 && n > 0) ? n : 1; }
            else if (k == "snapshot") s.snapOwed = true;
            std::printf("profile: control %ld - %s\n", q, cmd);
        }
    }
    if (s.paused && s.steps > 0) {
        if (--s.steps == 0) s.snapOwed = true;   // the frame the step comes to rest on
        tellState(frame);
        return Run::Go;
    }
    tellState(frame);
    return s.paused ? Run::Hold : Run::Go;
}

bool snapshotOwed() { return g_on && st().snapOwed; }

void snapshot(long frame, int w, int h, const std::uint16_t* px) {
    State& s = st();
    s.snapOwed = false;
    if (!g_on || w <= 0 || h <= 0 || !px) return;
    ++s.snaps;
    std::vector<unsigned char> out;
    out.reserve(24 + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 2);
    auto p32 = [&](std::uint32_t v) { for (int k = 0; k < 4; ++k) out.push_back(static_cast<unsigned char>((v >> (8 * k)) & 0xFF)); };
    for (const char* m = "OMKSNAP1"; *m; ++m) out.push_back(static_cast<unsigned char>(*m));
    p32(static_cast<std::uint32_t>(w));
    p32(static_cast<std::uint32_t>(h));
    p32(static_cast<std::uint32_t>(frame));
    p32(static_cast<std::uint32_t>(s.snaps));
    for (std::size_t i = 0, n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h); i < n; ++i) {
        out.push_back(static_cast<unsigned char>(px[i] & 0xFF));
        out.push_back(static_cast<unsigned char>(px[i] >> 8));
    }
    // written beside, then renamed over: a reader never sees half a picture
    const std::string tmp = s.base + ".snap.tmp";
    if (std::FILE* f = std::fopen(hostPath(tmp).c_str(), "wb")) {
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
        std::remove(hostPath(s.base + ".snap").c_str());
        std::rename(hostPath(tmp).c_str(), hostPath(s.base + ".snap").c_str());
    }
    s.told.clear();                      // the state names the new snapshot
    tellState(frame);
}

}  // namespace omk::prof

#endif  // OMK_PROFILE

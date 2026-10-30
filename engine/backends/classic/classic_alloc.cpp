// SPDX-License-Identifier: GPL-3.0-or-later
// THE CLASSIC BUILD'S ALLOCATOR (`todo/handoff-classic-mac.md` 3i, step 3).
//
// Retro68's `malloc` (`libretro/malloc.c`) is the Memory Manager one for one:
// every `malloc` a `NewPtr`, every `free` a `DisposePtr`. Every `new`, every
// container and every string goes through it - libstdc++'s copy-on-write
// `std::string` has no small-string buffer, so even a short key is a block -
// and the Memory Manager searches the heap for each nonrelocatable block.
// Measured on the emulated Mac OS 9 (2026-10-30): a 32-byte `new` + `delete`
// 53 us, 69 us with 2000 blocks held; on the reader's iMac G3 that search
// over a 100 MB partition on a 100 MHz bus is what took the street to ~3 fps
// (the staged bodies 124-210 ms a frame, the scripted motion ~75 ms).
//
// A 1999 port would not have called `NewPtr` per block: Metrowerks' MSL
// `malloc` sub-allocated from pools. So does this - a SLAB allocator:
//   * a request of up to 4080 bytes takes a block of one of 28 SIZE CLASSES
//     (32 .. 4096 bytes, its 16-byte header included), from a SLAB of that
//     class - one `NewPtr` of 32 KB (64 KB for the largest classes) cut into
//     equal blocks, with its own free list. Allocating and freeing are a few
//     pointer moves;
//   * a slab with room is on its class's list; a slab whose last block is
//     freed is GIVEN BACK (`DisposePtr`) - except one empty slab kept per
//     class, so a block made and freed every frame does not make and dispose
//     a slab every frame. A load's transient blocks therefore do not keep
//     the partition from the next area's large buffers (the 64 MB budget);
//   * anything larger is its own `NewPtr`, as before.
// The 16-byte header (the slab, the class with a magic, the requested size)
// keeps every block 16-byte aligned, as `NewPtr` aligns on PowerPC. A block
// whose header does not carry the magic was not made here and is handed to
// `DisposePtr`, which is what the old `free` did with everything.
//
// Single-threaded by construction: OS 9 is cooperative, the classic build
// poses its bodies on one runner, and the Sound Manager callback - the one
// thing at interrupt time - only sets a flag (`carbonfront.cpp`).
#include <MacMemory.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr std::size_t kHead = 16;
// the block sizes, header included: 16-byte steps to 160, then about a
// quarter apart, to 4096
constexpr std::uint32_t kClass[] = {32,   48,   64,   80,   96,   112,  128,  144,  160,  192,
                                    224,  256,  320,  384,  448,  512,  640,  768,  896,  1024,
                                    1280, 1536, 1792, 2048, 2560, 3072, 3584, 4096};
constexpr int kClasses = static_cast<int>(sizeof(kClass) / sizeof(kClass[0]));
constexpr std::size_t kSmallMax = 4096 - kHead;   // the largest request a class takes
constexpr std::uint32_t kMagic = 0x0A110000u;     // in the header's class word
constexpr std::uint32_t kLarge = 0xFFFFu;          // its own NewPtr
constexpr std::uint32_t kAligned = 0xFFFEu;        // its own NewPtr, over-aligned

struct Slab {
    Slab* prev;
    Slab* next;            // on the class's list while it has room
    void* free;            // its freed blocks, linked through their first word
    char* bump;            // the next never-used block
    char* end;
    std::uint32_t live;    // blocks out
    std::uint32_t cls;
    bool listed;
};
constexpr std::size_t kSlabHead = (sizeof(Slab) + 15) & ~std::size_t{15};

struct Head {
    void* slab;            // its slab; for kAligned the NewPtr'd base
    std::uint32_t cls;     // kMagic | class index, kLarge or kAligned
    std::uint32_t size;    // the requested size
};
static_assert(sizeof(Head) <= kHead, "the header fits its 16 bytes");

// the request (header included, rounded to 16) -> its class, by 16-byte step
constexpr int kSteps = 4096 / 16 + 1;
struct ClassTable {
    unsigned char of[kSteps];
    constexpr ClassTable() : of() {
        int c = 0;
        for (int s = 0; s < kSteps; ++s) {
            while (c < kClasses - 1 && kClass[c] < static_cast<std::uint32_t>(s * 16)) ++c;
            of[s] = static_cast<unsigned char>(c);
        }
    }
};
constexpr ClassTable kTable;

Slab* g_list[kClasses];     // slabs with room, per class
Slab* g_spare[kClasses];    // the one empty slab kept per class
struct Stats { long slabs, slabsMax, large, largeBytes; } g_stats;

std::size_t slabBytes(int c) { return kClass[c] > 2048 ? 65536 : 32768; }

void unlink(Slab* s) {
    if (s->prev) s->prev->next = s->next; else g_list[s->cls] = s->next;
    if (s->next) s->next->prev = s->prev;
    s->prev = s->next = nullptr;
    s->listed = false;
}
void link(Slab* s) {
    s->prev = nullptr;
    s->next = g_list[s->cls];
    if (s->next) s->next->prev = s;
    g_list[s->cls] = s;
    s->listed = true;
}
bool full(const Slab* s) { return !s->free && s->bump + kClass[s->cls] > s->end; }

void* bigBlock(std::size_t n) {
    char* p = NewPtr(static_cast<Size>(n + kHead));
    if (!p) { errno = ENOMEM; return nullptr; }
    Head* h = reinterpret_cast<Head*>(p);
    h->slab = nullptr;
    h->cls = kLarge;
    h->size = static_cast<std::uint32_t>(n);
    ++g_stats.large;
    g_stats.largeBytes += static_cast<long>(n);
    return p + kHead;
}

void* alloc(std::size_t n) {
    if (n > kSmallMax) return bigBlock(n);
    const int c = kTable.of[(n + kHead + 15) / 16];
    Slab* s = g_list[c];
    if (!s) {
        const std::size_t bytes = slabBytes(c);
        char* raw = NewPtr(static_cast<Size>(bytes));
        if (!raw) return bigBlock(n);     // no slab to be had: the block on its own
        s = reinterpret_cast<Slab*>(raw);
        s->free = nullptr;
        s->bump = raw + kSlabHead;
        s->end = raw + bytes;
        s->live = 0;
        s->cls = static_cast<std::uint32_t>(c);
        link(s);
        if (++g_stats.slabs > g_stats.slabsMax) g_stats.slabsMax = g_stats.slabs;
    }
    char* b;
    if (s->free) {
        b = static_cast<char*>(s->free);
        s->free = *reinterpret_cast<void**>(b);
    } else {
        b = s->bump;
        s->bump += kClass[c];
    }
    ++s->live;
    if (g_spare[c] == s) g_spare[c] = nullptr;
    if (full(s)) unlink(s);
    Head* h = reinterpret_cast<Head*>(b);
    h->slab = s;
    h->cls = kMagic | static_cast<std::uint32_t>(c);
    h->size = static_cast<std::uint32_t>(n);
    return b + kHead;
}

void release(void* p) {
    if (!p) return;
    char* b = static_cast<char*>(p) - kHead;
    Head* h = reinterpret_cast<Head*>(b);
    if (h->cls == kLarge) {
        --g_stats.large;
        g_stats.largeBytes -= static_cast<long>(h->size);
        DisposePtr(b);
        return;
    }
    if (h->cls == kAligned) { DisposePtr(static_cast<char*>(h->slab)); return; }
    if (h->cls == 0) return;                       // freed already (a double free): left alone
    if ((h->cls & 0xFFFF0000u) != kMagic) { DisposePtr(static_cast<char*>(p)); return; }   // not ours
    Slab* s = static_cast<Slab*>(h->slab);
    const int c = static_cast<int>(s->cls);
    h->cls = 0;                                    // a second free of it is caught above
    *reinterpret_cast<void**>(b) = s->free;
    s->free = b;
    if (!s->listed) link(s);
    if (--s->live == 0) {
        if (!g_spare[c]) {
            g_spare[c] = s;                        // kept: the next block of its size is free
        } else if (g_spare[c] != s) {
            unlink(s);
            DisposePtr(reinterpret_cast<char*>(s));
            --g_stats.slabs;
        }
    }
}

std::size_t sizeOf(void* p) { return reinterpret_cast<Head*>(static_cast<char*>(p) - kHead)->size; }
bool ours(void* p) {
    const std::uint32_t c = reinterpret_cast<Head*>(static_cast<char*>(p) - kHead)->cls;
    return c == kLarge || c == kAligned || (c & 0xFFFF0000u) == kMagic;
}

void* resize(void* p, std::size_t n) {
    if (!p) return alloc(n);
    if (n == 0) { release(p); return nullptr; }
    Head* h = reinterpret_cast<Head*>(static_cast<char*>(p) - kHead);
    if ((h->cls & 0xFFFF0000u) == kMagic && n + kHead <= kClass[h->cls & 0xFFFFu]) {
        h->size = static_cast<std::uint32_t>(n);   // still fits its block
        return p;
    }
    if (h->cls == kLarge && n > kSmallMax) {
        MemError();
        SetPtrSize(reinterpret_cast<char*>(h), static_cast<Size>(n + kHead));
        if (MemError() == noErr) {
            g_stats.largeBytes += static_cast<long>(n) - static_cast<long>(h->size);
            h->size = static_cast<std::uint32_t>(n);
            return p;
        }
    }
    void* q = alloc(n);
    if (!q) return nullptr;
    const std::size_t old = ours(p) ? sizeOf(p) : static_cast<std::size_t>(GetPtrSize(static_cast<char*>(p)));
    std::memcpy(q, p, old < n ? old : n);
    release(p);
    return q;
}

void* aligned(std::size_t al, std::size_t n) {
    if (al <= kHead) return alloc(n);
    char* base = NewPtr(static_cast<Size>(n + al + kHead));
    if (!base) { errno = ENOMEM; return nullptr; }
    const std::uintptr_t u = (reinterpret_cast<std::uintptr_t>(base) + kHead + al - 1) & ~(std::uintptr_t{al} - 1);
    Head* h = reinterpret_cast<Head*>(u - kHead);
    h->slab = base;
    h->cls = kAligned;
    h->size = static_cast<std::uint32_t>(n);
    return reinterpret_cast<void*>(u);
}

}  // namespace

// What the allocator holds, for the exit report (`classic_main.cpp`).
void classic_alloc_report() {
    std::printf("alloc: the classic slab allocator - %ld slabs now (at most %ld), %ld large blocks "
                "(%ld KB) its own NewPtr\n",
                g_stats.slabs, g_stats.slabsMax, g_stats.large, g_stats.largeBytes / 1024);
}

// THE C LIBRARY'S ENTRY POINTS, in place of `libretro/malloc.c`'s: defined
// here, that archive member is never pulled in. newlib's own code calls the
// reentrant `_r` forms.
extern "C" {
struct _reent;
void referenceMyMalloc(void) {}
void* malloc(std::size_t n) { return alloc(n); }
void free(void* p) { release(p); }
void* realloc(void* p, std::size_t n) { return resize(p, n); }
void* calloc(std::size_t a, std::size_t b) {
    if (b && a > static_cast<std::size_t>(-1) / b) { errno = ENOMEM; return nullptr; }
    void* p = alloc(a * b);
    if (p) std::memset(p, 0, a * b);
    return p;
}
void* memalign(std::size_t al, std::size_t n) { return aligned(al, n); }
void* aligned_alloc(std::size_t al, std::size_t n) { return aligned(al, n); }
int posix_memalign(void** out, std::size_t al, std::size_t n) {
    void* p = aligned(al, n);
    if (!p) return ENOMEM;
    *out = p;
    return 0;
}
void* _malloc_r(struct _reent*, std::size_t n) { return alloc(n); }
void _free_r(struct _reent*, void* p) { release(p); }
void* _realloc_r(struct _reent*, void* p, std::size_t n) { return resize(p, n); }
void* _calloc_r(struct _reent*, std::size_t a, std::size_t b) { return calloc(a, b); }
}

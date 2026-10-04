// SPDX-License-Identifier: GPL-3.0-or-later
// The memory budget's counters - see heapcount.h.
//
// The replacement `operator new` / `delete` below are the standard's own
// replaceable functions: defined here, they take the place of libstdc++'s for
// the whole program (this object is linked before the library, and it is
// pulled in by `classic_main.cpp`'s call to `classic_heap_report`). They do
// what libstdc++'s do on Retro68 - `malloc`, which is `NewPtr` - and count.
// No threads on classic Mac OS (`platform/threads.h`), and the Sound Manager's
// interrupt-time callback allocates nothing, so plain counters are enough.
#include "heapcount.h"

#include <Files.h>
#include <MacMemory.h>
#include <Processes.h>

#include <cstdio>
#include <cstdlib>
#include <new>

namespace {

long g_live = 0;          // bytes in live C++ blocks, by GetPtrSize
long g_peak = 0;
long g_allocs = 0;        // operator new calls so far
long g_peakAt = 0;        // ...when the peak was reached
long g_launchFree = 0;
long g_lowFree = -1;      // lowest FreeMem sampled, and MaxBlock with it
long g_lowMax = 0;
long g_lowAtFrame = 0;
long g_frames = 0;

void* counted(std::size_t n) {
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    g_live += GetPtrSize(static_cast<Ptr>(p));
    ++g_allocs;
    if (g_live > g_peak) { g_peak = g_live; g_peakAt = g_allocs; }
    return p;
}

void uncounted(void* p) {
    if (!p) return;
    g_live -= GetPtrSize(static_cast<Ptr>(p));
    std::free(p);
}

}  // namespace

void* operator new(std::size_t n) { return counted(n); }
void* operator new[](std::size_t n) { return counted(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return counted(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try { return counted(n); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { uncounted(p); }
void operator delete[](void* p) noexcept { uncounted(p); }
void operator delete(void* p, std::size_t) noexcept { uncounted(p); }
void operator delete[](void* p, std::size_t) noexcept { uncounted(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { uncounted(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { uncounted(p); }

void classic_heap_launch() {
    ProcessSerialNumber psn = {0, kCurrentProcess};
    ProcessInfoRec info = {};
    info.processInfoLength = sizeof info;
    const long part = GetProcessInformation(&psn, &info) == noErr
                    ? static_cast<long>(info.processSize) : -1;
    g_launchFree = FreeMem();
    std::printf("memory: partition %ld KB, %ld KB free at launch (MaxBlock %ld KB)\n",
                part / 1024, g_launchFree / 1024, MaxBlock() / 1024);
}

void classic_heap_sample() {
    ++g_frames;
    const long f = FreeMem();
    if (g_lowFree < 0 || f < g_lowFree) {
        g_lowFree = f;
        g_lowMax = MaxBlock();
        g_lowAtFrame = g_frames;
    }
    // A running total every 30 samples, FLUSHED: Mac OS 9 caches writes, and
    // a run whose emulator is stopped before the program's own exit would
    // otherwise leave nothing of the measurement on the disk.
    if (g_frames % 30 == 0) {
        std::printf("memory: sample %ld - %ld KB in live C++ blocks (peak %ld KB), "
                    "FreeMem %ld KB (lowest %ld KB), MaxBlock %ld KB\n",
                    g_frames, g_live / 1024, g_peak / 1024, f / 1024, g_lowFree / 1024,
                    MaxBlock() / 1024);
        std::fflush(stdout);
        FlushVol(nullptr, 0);
    }
}

void classic_heap_report() {
    classic_heap_sample();
    std::printf("memory: peak %ld KB in live C++ blocks (at allocation %ld of %ld; %ld KB "
                "live now); lowest FreeMem %ld KB at sample %ld of %ld, MaxBlock then %ld KB "
                "- so the run used at most %ld KB of the %ld KB free at launch\n",
                g_peak / 1024, g_peakAt, g_allocs, g_live / 1024, g_lowFree / 1024,
                g_lowAtFrame, g_frames, g_lowMax / 1024,
                (g_launchFree - g_lowFree) / 1024, g_launchFree / 1024);
}

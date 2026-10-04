// SPDX-License-Identifier: GPL-3.0-or-later
// The memory budget's counters - see heapcount.h.
//
// The C++ blocks are counted by the PROFILER's own `operator new` / `delete`
// (`platform/profile.cpp`, todo/debug-tools.md step 4), which this file had
// first (2026-10-04) and gave up when the profiler took them for every build:
// one counter, with categories, instead of two. This keeps what is Mac OS 9's
// own - the partition, FreeMem and MaxBlock. A release build
// (`OMK_PROFILE=0`) counts nothing, and says so.
#include "heapcount.h"

#include "platform/profile.h"

#include <Files.h>
#include <MacMemory.h>
#include <Processes.h>

#include <cstdio>

namespace {

long g_launchFree = 0;
long g_lowFree = -1;      // lowest FreeMem sampled, and MaxBlock with it
long g_lowMax = 0;
long g_lowAtFrame = 0;
long g_frames = 0;

}  // namespace

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
        const omk::prof::MemTotals t = omk::prof::memTotals();
        // KB in a `long`: Retro68's printf may not take `%lld`
        std::printf("memory: sample %ld - %ld KB in live C++ blocks (peak %ld KB), "
                    "FreeMem %ld KB (lowest %ld KB), MaxBlock %ld KB\n",
                    g_frames, static_cast<long>(t.live / 1024), static_cast<long>(t.peak / 1024),
                    f / 1024, g_lowFree / 1024,
                    MaxBlock() / 1024);
        std::fflush(stdout);
        FlushVol(nullptr, 0);
    }
}

void classic_heap_report() {
    classic_heap_sample();
    const omk::prof::MemTotals t = omk::prof::memTotals();
    std::printf("memory: peak %ld KB in live C++ blocks (%ld KB, %ld blocks live now%s); "
                "lowest FreeMem %ld KB at sample %ld of %ld, MaxBlock then %ld KB "
                "- so the run used at most %ld KB of the %ld KB free at launch\n",
                static_cast<long>(t.peak / 1024), static_cast<long>(t.live / 1024), t.blocks,
                OMK_PROFILE ? "" : " - a RELEASE build counts no blocks",
                g_lowFree / 1024, g_lowAtFrame, g_frames, g_lowMax / 1024,
                (g_launchFree - g_lowFree) / 1024, g_launchFree / 1024);
}

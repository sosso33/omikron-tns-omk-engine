// SPDX-License-Identifier: GPL-3.0-or-later
// THE MEMORY BUDGET on classic Mac OS (`todo/classic-mac-port-1999.md` 3b):
// what the partition in `omk-classic.r` has to hold. Mac OS 9 gives an
// application a FIXED heap - the SIZE resource's partition - and Retro68's
// `malloc` is `NewPtr` in it, so everything the engine allocates comes out of
// that one block, and an allocation it cannot satisfy fails rather than
// paging. Mac OS X ignores the partition.
//
// Two measurements, because they answer different questions:
//
//   * the PEAK of live C++ allocations, counted EXACTLY by the profiler's
//     `operator new` / `delete` (`platform/profile.cpp` - this file's own
//     until the profiler took them for every build, with categories), each
//     block's requested size - a load's transient buffers included, which a
//     per-frame sample would miss. The profiler's 16-byte header per block
//     is in FreeMem's figure, not in this one; a release build has neither;
//   * the lowest `FreeMem` and `MaxBlock` seen, sampled once a frame
//     (`classic_heap_sample`, called by the frontend's present) - the heap's
//     own overhead, the C allocations (`fopen` buffers, pl_mpeg) and the
//     fragmentation, which decides whether a large block can still be had.
//
// `classic_heap_launch` prints the partition at start; `classic_heap_report`
// the two at exit.
#pragma once

void classic_heap_launch();
void classic_heap_sample();
void classic_heap_report();

// SPDX-License-Identifier: GPL-3.0-or-later
// THE WORKER THREADS - one place, three implementations, and a build without any.
//
// Why a class rather than `std::thread` at the call sites: the platforms this
// port is aimed at do not agree on how a thread is made.
//
//   * desktop (macOS, Linux, Windows): `std::thread` and a condition variable;
//   * PS VITA: the standard library's thread is not the way in - a thread is
//     `sceKernelCreateThread` / `sceKernelStartThread` / `sceKernelWaitThreadEnd`
//     from the Vita SDK, with an explicit stack size, priority and cpu affinity
//     mask (the system reserves a core, so a game gets three);
//   * a single-threaded build (WebGL/Emscripten without pthreads, or any
//     platform where threads are not wanted): everything runs inline.
//
// So every call site asks this class for work to be split, and which of the
// three it gets is decided HERE, by `OMK_THREADS`:
//
//   OMK_THREADS 1  (default)      the platform's threads
//   OMK_THREADS 0                 no threads at all: `parallelFor` calls the
//                                 body itself, `workers()` is 0, and nothing
//                                 in `threads.cpp` includes a thread header.
//
// It defaults to 0 under Emscripten without pthreads and to 1 elsewhere; a
// build can force either (`-DOMK_THREADS=0`).
//
// ---------------------------------------------------------------- THE RULE
//
// **A parallel run must produce exactly what the inline run produces.** That
// is why the only thing offered is a `parallelFor` over a HALF-OPEN RANGE with
// disjoint chunks: each chunk writes its own slice of its own output and reads
// nothing another chunk writes, so there is no order to get wrong and no
// reduction to round differently. A caller that cannot be written that way -
// anything accumulating into one value, or appending to one list - does not
// belong here; split it into a per-chunk result the caller merges afterwards,
// in index order, on the calling thread.
//
// `engine/tools/thread_probe.cpp` is the check: the same work run inline and
// through the pool, compared bit for bit.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>

// The default: threads, unless this is an Emscripten build without pthreads.
#if !defined(OMK_THREADS)
#  if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#    define OMK_THREADS 0
#  else
#    define OMK_THREADS 1
#  endif
#endif

namespace omk {

// One pool of workers. Make one and keep it: on every platform here, making a
// thread costs far more than the work a frame hands it.
class Threads {
public:
    // `workers` is how many threads to START, so the work is spread over
    // `workers + 1` runners (the caller's thread takes a chunk too). 0 asks
    // for the platform's own answer - `hardwareDefault()`.
    explicit Threads(int workers = 0);
    ~Threads();
    Threads(const Threads&) = delete;
    Threads& operator=(const Threads&) = delete;

    // How many threads were actually started: 0 in a single-threaded build, or
    // when the platform refused them - in which case everything still runs,
    // inline, and the results do not change.
    int workers() const;
    // `workers() + 1`, the number of chunks `parallelFor` will make at most.
    int runners() const { return workers() + 1; }

    // The platform's idea of how many workers to start: on the Vita the three
    // cores a game may use minus the one this thread is on, elsewhere
    // `std::thread::hardware_concurrency() - 1`, and never more than 7.
    static int hardwareDefault();

    // Call `body(from, to)` over `[begin, end)` split into at most
    // `runners()` chunks, none smaller than `grain` items, and return when
    // every chunk is done. The caller's thread runs one of the chunks.
    //
    // The chunks are contiguous and in ascending order, but they run in no
    // defined order, so `body` must not depend on another chunk's work.
    // With `end - begin <= grain`, or in a single-threaded build, `body` is
    // simply called once with the whole range - which is what makes the
    // single-threaded build a build rather than a fallback.
    void parallelFor(std::size_t begin, std::size_t end, std::size_t grain,
                     const std::function<void(std::size_t from, std::size_t to)>& body);

    // The process-wide pool, made on first use. A frame loop should use this
    // rather than making its own: two pools mean twice the threads for the
    // same cores.
    static Threads& shared();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace omk

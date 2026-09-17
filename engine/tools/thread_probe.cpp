// SPDX-License-Identifier: GPL-3.0-or-later
// THE WORKER POOL GIVES THE INLINE RUN'S ANSWER - and says what it costs.
//
//     thread_probe [items] [repeats]
//
// `omk::Threads` (platform/threads.h) exists so that the per-body work of a
// frame - posing, lighting, the face-overlap walk - can be split over the
// cores a platform has, with one place deciding how a thread is made
// (std::thread, the Vita SDK, or none at all). Its whole safety rule is that a
// `parallelFor` writes disjoint slices, so this holds it to that:
//
//   * a deterministic workload over an array, computed inline and through the
//     pool, compared BYTE FOR BYTE - so a chunk boundary that dropped or
//     doubled an item shows;
//   * every chunk size from 1 item to the whole range, so the boundary
//     arithmetic is exercised, including ranges smaller than the grain (which
//     must run inline) and empty ranges;
//   * the chunks' own boundaries collected and checked to be contiguous,
//     ascending and to cover the range exactly once;
//   * a nested `parallelFor` inside a chunk, which must still be correct (it
//     runs inline on that worker, since the pool is busy);
//   * and the timing of both, which is the reason the class exists.
//
// In a build with `OMK_THREADS=0` it still runs: `workers()` is 0, everything
// is inline, and the comparison is then a check that the class's arithmetic
// does not depend on there being threads.
//
// Prints one line a size, then `mismatches <total>`. Writes nothing.
#include "platform/threads.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

// Work that depends only on the index, so the answer cannot depend on the
// order the chunks run in - and heavy enough that threads can show.
float itemWork(std::size_t i) {
    float acc = static_cast<float>(i & 1023u);
    for (int k = 0; k < 64; ++k) acc = acc * 1.0000113f + std::sqrt(acc + static_cast<float>(k));
    return acc;
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t items = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 200000;
    const int repeats = argc > 2 ? std::atoi(argv[2]) : 5;
    using clk = std::chrono::steady_clock;

    omk::Threads& pool = omk::Threads::shared();
    std::printf("workers %d (runners %d), OMK_THREADS %d\n", pool.workers(), pool.runners(), OMK_THREADS);

    long mismatches = 0;
    std::vector<float> inlineOut(items), poolOut(items);
    for (std::size_t i = 0; i < items; ++i) inlineOut[i] = itemWork(i);

    // every grain from "one chunk an item" to "one chunk for everything"
    const std::size_t grains[] = {1, 7, 64, 1000, items / 3 + 1, items, items * 2};
    double inlineMs = 0, poolMs = 0;
    for (std::size_t g : grains) {
        std::vector<std::pair<std::size_t, std::size_t>> ranges;
        std::mutex rm;
        std::fill(poolOut.begin(), poolOut.end(), 0.0f);
        const auto t0 = clk::now();
        for (int r = 0; r < repeats; ++r)
            pool.parallelFor(0, items, g, [&](std::size_t from, std::size_t to) {
                if (r == 0) { std::lock_guard<std::mutex> lk(rm); ranges.emplace_back(from, to); }
                for (std::size_t i = from; i < to; ++i) poolOut[i] = itemWork(i);
            });
        const auto t1 = clk::now();
        poolMs += std::chrono::duration<double, std::milli>(t1 - t0).count();

        long bad = std::memcmp(poolOut.data(), inlineOut.data(), items * sizeof(float)) == 0 ? 0 : 1;
        // the chunks must tile [0, items) exactly once
        std::sort(ranges.begin(), ranges.end());
        std::size_t at = 0;
        bool tiled = true;
        for (const auto& [from, to] : ranges) {
            if (from != at || to < from) { tiled = false; break; }
            at = to;
        }
        if (at != items) tiled = false;
        if (!tiled) ++bad;
        mismatches += bad;
        std::printf("grain %8zu: chunks %2zu | bytes differ %s | tiling %s\n",
                    g, ranges.size(), bad ? "YES" : "no", tiled ? "exact" : "BROKEN");
    }

    // the inline cost, for the comparison the class exists for
    {
        const auto t0 = clk::now();
        for (int r = 0; r < repeats; ++r)
            for (std::size_t i = 0; i < items; ++i) poolOut[i] = itemWork(i);
        inlineMs = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    }

    // an empty range and a range of one, which must not hang or write
    pool.parallelFor(0, 0, 1, [&](std::size_t, std::size_t) { ++mismatches; });
    std::size_t once = 0;
    pool.parallelFor(5, 6, 1, [&](std::size_t from, std::size_t to) { once += to - from; });
    if (once != 1) ++mismatches;

    // a nested parallelFor: the inner one runs inline on its worker
    std::vector<float> nested(items, 0.0f);
    pool.parallelFor(0, items, 4096, [&](std::size_t from, std::size_t to) {
        pool.parallelFor(from, to, 4096, [&](std::size_t a, std::size_t b) {
            for (std::size_t i = a; i < b; ++i) nested[i] = itemWork(i);
        });
    });
    if (std::memcmp(nested.data(), inlineOut.data(), items * sizeof(float)) != 0) ++mismatches;

    std::printf("%zu items x %d: inline %.1f ms, pool %.1f ms over %zu grains (%.2fx on the best grain)\n",
                items, repeats, inlineMs, poolMs / static_cast<double>(sizeof grains / sizeof grains[0]),
                sizeof grains / sizeof grains[0],
                inlineMs / std::max(1e-9, poolMs / static_cast<double>(sizeof grains / sizeof grains[0])));
    std::printf("mismatches %ld\n", mismatches);
    return mismatches == 0 ? 0 : 3;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// `omk::Threads` ON THE PS VITA: `sceKernelCreateThread` workers with an
// explicit stack and priority, woken through a semaphore each call and joined
// by a second (`threads.h`).
#include "platform/threads.h"   // OMK_THREADS: it must be defined before the test
#if OMK_THREADS && defined(__vita__)

#include "platform/hostthread.h"

#include <algorithm>
#include <atomic>
#include <psp2/kernel/threadmgr.h>

namespace omk {

namespace {
// HOW DEEP THIS THREAD ALREADY IS in a `parallelFor` of any pool. A pool holds
// ONE call's state at a time, so a second call entered from inside the first -
// a chunk that itself splits work, or a worker calling back into the pool -
// would overwrite the outer call's chunk count and hang it. It runs inline
// instead, which is what `threads.h` promises and what `thread_probe` checks.
// ON THE VITA `thread_local` IS NOT PER THREAD for the kernel threads made
// below (the crowd's `composePose` scratch, shared by main and a worker,
// dies on a garbage index), so a shared counter here would be raced
// by every worker and could be left non-zero - silently running every later
// call inline. Instead: a WORKER is recognised by its thread id (`isWorker`)
// and runs a nested call inline; the CALLER that holds the pool is recorded
// by its thread id (`Impl::owner`), so its own chunk splitting again runs
// inline too. A plain count of the caller's depth served while one thread
// called the pool; with two (a set loading on a BackgroundJob beside the
// frame, 2026-10-21) its unsynchronised ++/-- could be lost - left at -1, a
// nested call would wait on the semaphore its own thread holds.
}  // namespace


namespace {
// The SDK's own units. 0x10000100 is the ordinary "user main thread" priority
// a game's helpers run at, and 64 KiB is a stack for work that recurses
// nowhere - both are the values to re-check on the first device build.
constexpr int kVitaPriority  = 0x10000100;
constexpr int kVitaStack     = 64 * 1024;
// The system keeps one of the four cores, so a game has three: this mask lets
// the scheduler use them and no other.
constexpr int kVitaAffinity  = 0;   // 0 = the default set, which is those three
}  // namespace

struct Threads::Impl {
    struct Worker {
        SceUID thread = -1;
        Threads::Impl* pool = nullptr;
        int index = 0;
    };
    std::size_t from = 0, to = 0, grain = 0;
    const std::function<void(std::size_t, std::size_t)>* body = nullptr;
    int chunks = 0;
    // THE NEXT CHUNK TO TAKE, claimed by each woken worker. Taking
    // `slice(w->index)` instead - the chunk of the worker's own SLOT - is
    // wrong whenever fewer chunks than workers are handed out: the semaphore
    // wakes ANY waiting worker, so with two workers and two chunks the one
    // woken can be worker 1, which then runs chunk 1 - the CALLER's own -
    // while chunk 0 runs nowhere. Two threads posing the same walkers,
    // reallocating the same vectors: a heap corruption on the console (seen
    // in the Bowie sequence, with only one or two walkers left to pose). The
    // `std::thread` pool takes `taken++` too.
    std::atomic<int> taken{0};
    SceUID work = -1;      // signalled once per worker that has a chunk
    SceUID done = -1;      // signalled by each worker when its chunk is done
    // one CALL at a time from different threads - see `threads_std.cpp`'s
    // `calls`: a binary semaphore held across a whole call
    SceUID calls = -1;
    std::atomic<SceUID> owner{-1};   // the thread holding `calls`, -1 none
    bool stop = false;
    Worker* slots = nullptr;
    int count = 0;
    bool isWorker(SceUID self) const {
        for (int i = 0; i < count; ++i) if (slots[i].thread == self) return true;
        return false;
    }

    // Which slice of the range chunk `k` of `chunks` covers - the same
    // arithmetic in all three implementations, so the chunk boundaries do not
    // depend on the platform.
    void slice(int k, std::size_t& lo, std::size_t& hi) const {
        const std::size_t n = to - from;
        const std::size_t each = n / static_cast<std::size_t>(chunks);
        const std::size_t extra = n % static_cast<std::size_t>(chunks);
        const std::size_t kk = static_cast<std::size_t>(k);
        lo = from + kk * each + std::min(kk, extra);
        hi = lo + each + (kk < extra ? 1u : 0u);
    }

    // The worker's entry point. A MEMBER, because `Impl` is private to
    // `Threads`: a free function in an anonymous namespace cannot name it.
    // Only a VitaSDK build compiles this half, so nothing else would say so.
    static int workerMain(SceSize, void* argp);
};

int Threads::Impl::workerMain(SceSize, void* argp) {
    auto* w = *static_cast<Threads::Impl::Worker**>(argp);
    for (;;) {
        sceKernelWaitSema(w->pool->work, 1, nullptr);
        if (w->pool->stop) break;
        std::size_t lo = 0, hi = 0;
        const int mine = w->pool->taken.fetch_add(1);
        if (mine < w->pool->chunks - 1) w->pool->slice(mine, lo, hi);
        if (lo < hi) {
            // a chunk that splits work again runs it inline: `parallelFor`
            // recognises this thread as a worker (`isWorker`)
            (*w->pool->body)(lo, hi);
        }
        sceKernelSignalSema(w->pool->done, 1);
    }
    return sceKernelExitDeleteThread(0);
}

Threads::Threads(int workers) : impl_(new Impl) {
    const int want = workers > 0 ? workers : hardwareDefault();
    impl_->work = sceKernelCreateSema("omk_work", 0, 0, 64, nullptr);
    impl_->done = sceKernelCreateSema("omk_done", 0, 0, 64, nullptr);
    impl_->calls = sceKernelCreateSema("omk_calls", 0, 1, 1, nullptr);
    if (impl_->work < 0 || impl_->done < 0 || impl_->calls < 0) return;   // no threads: inline
    impl_->slots = new Impl::Worker[static_cast<std::size_t>(want)];
    for (int i = 0; i < want; ++i) {
        impl_->slots[i].pool = impl_.get();
        impl_->slots[i].index = i;
        char name[32];
        std::snprintf(name, sizeof name, "omk_worker%d", i);
        const SceUID t = sceKernelCreateThread(name, Impl::workerMain, kVitaPriority,
                                               kVitaStack, 0, kVitaAffinity, nullptr);
        if (t < 0) break;
        Impl::Worker* arg = &impl_->slots[i];
        if (sceKernelStartThread(t, sizeof arg, &arg) < 0) { sceKernelDeleteThread(t); break; }
        impl_->slots[i].thread = t;
        ++impl_->count;
    }
}

Threads::~Threads() {
    if (!impl_ || impl_->count == 0) return;
    impl_->stop = true;
    for (int i = 0; i < impl_->count; ++i) sceKernelSignalSema(impl_->work, 1);
    for (int i = 0; i < impl_->count; ++i)
        if (impl_->slots[i].thread >= 0) sceKernelWaitThreadEnd(impl_->slots[i].thread, nullptr, nullptr);
    delete[] impl_->slots;
    sceKernelDeleteSema(impl_->work);
    sceKernelDeleteSema(impl_->done);
    sceKernelDeleteSema(impl_->calls);
}

int Threads::workers() const { return impl_ ? impl_->count : 0; }
int Threads::hardwareDefault() { return 2; }   // three cores for a game, minus this one

void Threads::parallelFor(std::size_t begin, std::size_t end, std::size_t grain,
                          const std::function<void(std::size_t, std::size_t)>& body) {
    if (begin >= end) return;
    // nested - a worker's chunk splitting again, or the caller's own chunk -
    // runs inline (see `Impl::owner`)
    const SceUID self = sceKernelGetThreadId();
    if (impl_ && (impl_->isWorker(self) || impl_->owner.load() == self)) { body(begin, end); return; }
    const std::size_t n = end - begin;
    const int cap = workers() + 1;
    int chunks = grain ? static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(cap), (n + grain - 1) / grain)) : cap;
    if (chunks < 1) chunks = 1;
    if (chunks == 1) { body(begin, end); return; }
    // another thread's call holds the pool: this one runs inline rather than
    // wait for it (the same answer - `threads.h`'s rule)
    if (sceKernelPollSema(impl_->calls, 1) < 0) { body(begin, end); return; }
    impl_->owner.store(self);
    impl_->from = begin; impl_->to = end; impl_->grain = grain;
    impl_->body = &body; impl_->chunks = chunks;
    impl_->taken.store(0);                        // before any worker is woken
    for (int i = 0; i < chunks - 1; ++i) sceKernelSignalSema(impl_->work, 1);
    std::size_t lo = 0, hi = 0;
    impl_->slice(chunks - 1, lo, hi);             // the caller takes the last chunk
    if (lo < hi) body(lo, hi);
    for (int i = 0; i < chunks - 1; ++i) sceKernelWaitSema(impl_->done, 1, nullptr);
    impl_->owner.store(-1);
    sceKernelSignalSema(impl_->calls, 1);
}

namespace hostthread {
namespace {
// a detached thread's own entry: it owns the function until it has run, and
// the kernel deletes the thread as it exits - nothing waits for it
int detachedMain(SceSize, void* argp) {
    auto* fn = *static_cast<std::function<void()>**>(argp);
    (*fn)();
    delete fn;
    return sceKernelExitDeleteThread(0);
}
}  // namespace

// one priority step below the game (0x10000110), like the log's writer
bool spawnDetached(const char* name, std::size_t stackBytes, std::function<void()> fn) {
    const SceUID t = sceKernelCreateThread(name, detachedMain, 0x10000110,
                                           static_cast<SceSize>(stackBytes), 0, kVitaAffinity, nullptr);
    auto* held = new std::function<void()>(std::move(fn));
    if (t < 0 || sceKernelStartThread(t, sizeof held, &held) < 0) {
        if (t >= 0) sceKernelDeleteThread(t);
        delete held;
        return false;
    }
    return true;
}

void sleepMs(int ms) { sceKernelDelayThread(static_cast<SceUInt>(ms) * 1000); }
int drainDetached() { return 0; }   // the Vita's process ends with the app
}  // namespace hostthread

}  // namespace omk

#endif

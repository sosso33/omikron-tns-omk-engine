// SPDX-License-Identifier: GPL-3.0-or-later
// The three implementations of `omk::Threads`, in one file - see `threads.h`
// for why they are here and not at the call sites.
//
// The shape is the same in all three: `parallelFor` cuts `[begin, end)` into at
// most `runners()` contiguous chunks of at least `grain` items, hands all but
// the last to the workers, runs the last itself, and waits. What differs is how
// a worker is made and how it is woken:
//
//   * `std::thread`: workers park on a condition variable and are woken per
//     call; the pool is made once;
//   * VITA: `sceKernelCreateThread` with an explicit stack and priority, woken
//     through a semaphore each call, joined by a second semaphore. **The Vita
//     half is written from the SDK's documented shape and has NOT been compiled
//     or run on a device** - it is marked so here rather than presented as
//     tested, and the first Vita build must check the names, the priority and
//     the affinity mask against the SDK it links;
//   * no threads: the body is called once, on the caller's thread.
#include "platform/threads.h"

#include <algorithm>

#if OMK_THREADS
#  if defined(__vita__)
#    include <psp2/kernel/threadmgr.h>
#  else
#    include <condition_variable>
#    include <mutex>
#    include <thread>
#    include <vector>
#  endif
#endif

namespace omk {

#if OMK_THREADS
namespace {
// HOW DEEP THIS THREAD ALREADY IS in a `parallelFor` of any pool. A pool holds
// ONE call's state at a time, so a second call entered from inside the first -
// a chunk that itself splits work, or a worker calling back into the pool -
// would overwrite the outer call's chunk count and hang it. It runs inline
// instead, which is what `threads.h` promises and what `thread_probe` checks.
thread_local int gDepth = 0;
struct DepthGuard {
    DepthGuard() { ++gDepth; }
    ~DepthGuard() { --gDepth; }
};
}  // namespace
#endif

// ---------------------------------------------------------------- no threads
#if !OMK_THREADS

struct Threads::Impl {};

Threads::Threads(int) : impl_(nullptr) {}
Threads::~Threads() = default;
int Threads::workers() const { return 0; }
int Threads::hardwareDefault() { return 0; }

void Threads::parallelFor(std::size_t begin, std::size_t end, std::size_t,
                          const std::function<void(std::size_t, std::size_t)>& body) {
    if (begin < end) body(begin, end);
}

// ---------------------------------------------------------------- PS VITA
#elif defined(__vita__)

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
    SceUID work = -1;      // signalled once per worker that has a chunk
    SceUID done = -1;      // signalled by each worker when its chunk is done
    bool stop = false;
    Worker* slots = nullptr;
    int count = 0;

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
    // `Threads`: the first version was a free function in an anonymous
    // namespace, which cannot name it - and nobody knew, because this half had
    // never been compiled until the first VitaSDK build (2026-09-18).
    static int workerMain(SceSize, void* argp);
};

int Threads::Impl::workerMain(SceSize, void* argp) {
    auto* w = *static_cast<Threads::Impl::Worker**>(argp);
    for (;;) {
        sceKernelWaitSema(w->pool->work, 1, nullptr);
        if (w->pool->stop) break;
        std::size_t lo = 0, hi = 0;
        w->pool->slice(w->index, lo, hi);
        if (lo < hi) {
            // the guard belongs around the BODY, wherever it runs: a chunk that
            // splits work again must run that inline rather than re-enter the
            // pool it is already inside
            DepthGuard depth;
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
    if (impl_->work < 0 || impl_->done < 0) return;          // no threads: inline
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
}

int Threads::workers() const { return impl_ ? impl_->count : 0; }
int Threads::hardwareDefault() { return 2; }   // three cores for a game, minus this one

void Threads::parallelFor(std::size_t begin, std::size_t end, std::size_t grain,
                          const std::function<void(std::size_t, std::size_t)>& body) {
    if (begin >= end) return;
    if (gDepth > 0) { body(begin, end); return; }   // nested: inline, see DepthGuard
    DepthGuard depth;
    const std::size_t n = end - begin;
    const int cap = workers() + 1;
    int chunks = grain ? static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(cap), (n + grain - 1) / grain)) : cap;
    if (chunks < 1) chunks = 1;
    if (chunks == 1) { body(begin, end); return; }
    impl_->from = begin; impl_->to = end; impl_->grain = grain;
    impl_->body = &body; impl_->chunks = chunks;
    for (int i = 0; i < chunks - 1; ++i) sceKernelSignalSema(impl_->work, 1);
    std::size_t lo = 0, hi = 0;
    impl_->slice(chunks - 1, lo, hi);             // the caller takes the last chunk
    if (lo < hi) body(lo, hi);
    for (int i = 0; i < chunks - 1; ++i) sceKernelWaitSema(impl_->done, 1, nullptr);
}

// ---------------------------------------------------------------- std::thread
#else

struct Threads::Impl {
    std::vector<std::thread> pool;
    std::mutex m;
    std::condition_variable wake, finished;
    std::size_t from = 0, to = 0;
    const std::function<void(std::size_t, std::size_t)>* body = nullptr;
    int chunks = 0;          // how many chunks this call has
    int taken = 0;           // chunks handed out
    int left = 0;            // chunks not yet finished
    unsigned long long generation = 0;
    bool stop = false;

    void slice(int k, std::size_t& lo, std::size_t& hi) const {
        const std::size_t n = to - from;
        const std::size_t each = n / static_cast<std::size_t>(chunks);
        const std::size_t extra = n % static_cast<std::size_t>(chunks);
        const std::size_t kk = static_cast<std::size_t>(k);
        lo = from + kk * each + std::min(kk, extra);
        hi = lo + each + (kk < extra ? 1u : 0u);
    }
};

Threads::Threads(int workers) : impl_(new Impl) {
    const int want = workers > 0 ? workers : hardwareDefault();
    for (int i = 0; i < want; ++i) {
        impl_->pool.emplace_back([this] {
            Impl& s = *impl_;
            unsigned long long seen = 0;
            for (;;) {
                int mine = -1;
                {
                    std::unique_lock<std::mutex> lk(s.m);
                    s.wake.wait(lk, [&] { return s.stop || (s.generation != seen && s.taken < s.chunks - 1); });
                    if (s.stop) return;
                    seen = s.generation;
                    mine = s.taken++;
                }
                std::size_t lo = 0, hi = 0;
                s.slice(mine, lo, hi);
                if (lo < hi) {
                    // the guard belongs around the BODY, wherever it runs - see
                    // DepthGuard: a chunk that splits work again runs it inline
                    DepthGuard depth;
                    (*s.body)(lo, hi);
                }
                {
                    std::lock_guard<std::mutex> lk(s.m);
                    if (--s.left == 0) s.finished.notify_all();
                }
            }
        });
    }
}

Threads::~Threads() {
    if (!impl_) return;
    {
        std::lock_guard<std::mutex> lk(impl_->m);
        impl_->stop = true;
    }
    impl_->wake.notify_all();
    for (auto& t : impl_->pool) if (t.joinable()) t.join();
}

int Threads::workers() const { return impl_ ? static_cast<int>(impl_->pool.size()) : 0; }

int Threads::hardwareDefault() {
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw <= 1) return 0;
    return static_cast<int>(std::min(hw - 1u, 7u));
}

void Threads::parallelFor(std::size_t begin, std::size_t end, std::size_t grain,
                          const std::function<void(std::size_t, std::size_t)>& body) {
    if (begin >= end) return;
    if (gDepth > 0) { body(begin, end); return; }   // nested: inline, see DepthGuard
    DepthGuard depth;
    const std::size_t n = end - begin;
    const int cap = workers() + 1;
    int chunks = grain ? static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(cap), (n + grain - 1) / grain)) : cap;
    if (chunks < 1) chunks = 1;
    if (chunks == 1) { body(begin, end); return; }
    {
        std::lock_guard<std::mutex> lk(impl_->m);
        impl_->from = begin; impl_->to = end;
        impl_->body = &body;
        impl_->chunks = chunks;
        impl_->taken = 0;
        impl_->left = chunks;
        ++impl_->generation;
    }
    impl_->wake.notify_all();
    std::size_t lo = 0, hi = 0;
    impl_->slice(chunks - 1, lo, hi);             // the caller takes the last chunk
    if (lo < hi) body(lo, hi);
    {
        std::lock_guard<std::mutex> lk(impl_->m);
        if (--impl_->left == 0) impl_->finished.notify_all();
    }
    {
        std::unique_lock<std::mutex> lk(impl_->m);
        impl_->finished.wait(lk, [&] { return impl_->left == 0; });
    }
}

#endif

Threads& Threads::shared() {
    static Threads one;
    return one;
}

}  // namespace omk

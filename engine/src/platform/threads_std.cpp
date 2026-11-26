// SPDX-License-Identifier: GPL-3.0-or-later
// `omk::Threads` WITH `std::thread` (the desktops, the Quest): workers park on a
// condition variable and are woken per call; the pool is made once
// (`threads.h`).
#include "platform/threads.h"   // OMK_THREADS: it must be defined before the test
#if OMK_THREADS && !defined(__vita__)

#include "platform/hostthread.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#if defined(__SWITCH__)
// THE SWITCH'S CORES (`todo/switch-port.md` S1). libnx makes every pthread -
// so every `std::thread` - on the process's DEFAULT core (`threadCreate`'s
// cpuid -2), which for an application is core 0, the main thread's: a pool
// left there is a pool on one core. An application owns cores 0-2 (core 3 is
// the system's), so worker i is pinned to core 1 + i % 2 and the main thread
// keeps core 0. `hardware_concurrency()` is not trusted either (newlib's may
// answer 0, which would read as "no workers").
#include <switch.h>
#include <pthread.h>
#include <cstdio>
namespace {
constexpr int kSwitchAppCores = 3;   // cores 0..2; core 3 belongs to the system
void pinSwitchWorker(int i) {
    const int core = 1 + i % (kSwitchAppCores - 1);
    svcSetThreadCoreMask(threadGetCurHandle(), core, 1u << core);
    // read back, not assumed: the first console log says whether it held
    std::printf("threads: worker %d on core %u (asked %d)\n", i,
                static_cast<unsigned>(svcGetCurrentProcessorNumber()), core);
}
}  // namespace
#endif

namespace omk {

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


struct Threads::Impl {
    std::vector<std::thread> pool;
    std::mutex m;
    // ONE CALL AT A TIME from different threads, as one call at a time nested
    // (`DepthGuard`): a second thread's call would overwrite the first's chunk
    // count and one of the two waits for ever. Held across a whole call. The
    // bar in Qalisar (2026-10-21): its set loading on a BackgroundJob, the
    // texture pack decoding on the pool, the main thread's frame on it too.
    // A call that finds it held runs INLINE rather than wait - the frame
    // would otherwise stall for the whole of a set's decode.
    std::mutex calls;
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
        impl_->pool.emplace_back([this, i] {
#if defined(__SWITCH__)
            pinSwitchWorker(i);
#else
            (void)i;
#endif
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
#if defined(__SWITCH__)
    return kSwitchAppCores - 1;      // the main thread is the third
#endif
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
    std::unique_lock<std::mutex> call(impl_->calls, std::try_to_lock);
    if (!call.owns_lock()) { body(begin, end); return; }
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

namespace hostthread {
#if defined(__SWITCH__)
// THE SWITCH HAS NO DETACHED THREAD. libnx defines no
// `__syscall_thread_detach`, so newlib's `pthread_detach` answers ENOSYS (88)
// and `std::thread::detach()` THROWS - the first background job (the
// remaster's preload, at the first door) ended the game in `std::terminate`
// (the reader's crash of 2026-11-25, `todo/switch-port.md`). A thread's
// kernel object and stack are released only by a join, so the threads made
// here are kept and JOINED once they say they are done, at the next spawn.
// Made with pthreads, which take the stack size asked (std::thread's is
// libnx's 128 KB default, and a job is a set loader), and moved to core 2 -
// libnx makes every thread on core 0, the game thread's.
namespace {
struct Spawned {
    pthread_t t{};
    std::function<void()> fn;
    std::atomic<bool> done{false};
};
// never destroyed: a thread still running at exit is the kernel's to end
std::mutex& spawnedLock() { static auto* m = new std::mutex; return *m; }
std::vector<Spawned*>& spawnedList() { static auto* v = new std::vector<Spawned*>; return *v; }
void* spawnedRun(void* p) {
    auto* s = static_cast<Spawned*>(p);
    svcSetThreadCoreMask(threadGetCurHandle(), kSwitchAppCores - 1, 1u << (kSwitchAppCores - 1));
    s->fn();
    s->fn = nullptr;
    s->done.store(true, std::memory_order_release);
    return nullptr;
}
}  // namespace
bool spawnDetached(const char*, std::size_t stackBytes, std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(spawnedLock());
    auto& list = spawnedList();
    for (auto it = list.begin(); it != list.end();) {
        if ((*it)->done.load(std::memory_order_acquire)) {
            pthread_join((*it)->t, nullptr);
            delete *it;
            it = list.erase(it);
        } else {
            ++it;
        }
    }
    auto* s = new Spawned;
    s->fn = std::move(fn);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stackBytes) pthread_attr_setstacksize(&attr, stackBytes);
    const int rc = pthread_create(&s->t, &attr, spawnedRun, s);
    pthread_attr_destroy(&attr);
    if (rc != 0) { delete s; return false; }   // the caller runs it
    list.push_back(s);
    return true;
}
#else
// -> false when no thread could be made, as `hostthread.h` promises: a
// `std::system_error` out of here would reach `std::terminate`. And a
// detach that fails must not leave the thread joinable in a destructor
// (that is `std::terminate` too): it is joined - the work then done.
bool spawnDetached(const char*, std::size_t, std::function<void()> fn) {
    try {
        std::thread t(std::move(fn));
        try {
            t.detach();
        } catch (const std::system_error&) {
            t.join();
        }
        return true;
    } catch (const std::system_error&) {
        return false;
    }
}
#endif
void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}  // namespace hostthread

}  // namespace omk

#endif

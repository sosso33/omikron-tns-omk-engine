// SPDX-License-Identifier: GPL-3.0-or-later
// `omk::Threads` WITHOUT THREADS (`OMK_THREADS=0`: classic Mac OS, the 3DS):
// the body is called once, on the caller's thread (`threads.h`).
#include "platform/threads.h"   // OMK_THREADS: it must be defined before the test
#if !OMK_THREADS

#include "platform/hostthread.h"

namespace omk {

struct Threads::Impl {};

Threads::Threads(int) : impl_(nullptr) {}
Threads::~Threads() = default;
int Threads::workers() const { return 0; }
int Threads::hardwareDefault() { return 0; }

void Threads::parallelFor(std::size_t begin, std::size_t end, std::size_t,
                          const std::function<void(std::size_t, std::size_t)>& body) {
    if (begin < end) body(begin, end);
}

namespace hostthread {
bool spawnDetached(const char*, std::size_t, std::function<void()>) { return false; }
void sleepMs(int) {}
int drainDetached() { return 0; }
}  // namespace hostthread

}  // namespace omk

#endif

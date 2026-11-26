// SPDX-License-Identifier: GPL-3.0-or-later
// THE HOST'S THREADS for `FileFetch` and `BackgroundJob` (`threads.cpp`): a
// single thread nobody joins, and a sleep. One implementation per platform,
// beside that platform's `Threads` pool (`threads_std.cpp`, `threads_vita.cpp`,
// `threads_none.cpp`). Internal: not part of `threads.h`.
#pragma once

#include <cstddef>
#include <functional>

namespace omk::hostthread {

// `fn` run on a new thread of its own, `stackBytes` deep where the host takes
// a size -> false when no thread could be made (the caller then runs it).
bool spawnDetached(const char* name, std::size_t stackBytes, std::function<void()> fn);
void sleepMs(int ms);
// EVERY THREAD `spawnDetached` made, waited for and released - before the
// program hands its process on. The Switch's hbloader loads the NEXT program
// (a chained launch, `envSetNextLoad`; hbmenu again) into the SAME process: a
// job still running then (a preload's set) ran on into the new program's
// heap - the chained Jaunpur loads crashed, every one, the ones launched by
// hand never (`todo/switch-port.md`, 2026-11-26). -> how many it waited for.
// Elsewhere a process's end ends its threads: nothing to do, 0.
int drainDetached();

}  // namespace omk::hostthread

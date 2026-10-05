// SPDX-License-Identifier: GPL-3.0-or-later
// See `n3dshost.h`.
#include "n3dshost.h"

#include <3ds.h>

#include <deque>

extern "C" {
extern u32 __ctru_heap_size;     // libctru's start-up
}

namespace omk::n3ds {

namespace {
// The last complete lines and the one being written. Bounded: the panel
// shows at most a screenful, and a long run must not grow this.
constexpr std::size_t kKeep = 64;
std::deque<std::string> g_lines;
std::string g_partial;
std::string g_capture;
}  // namespace

void logAppend(const char* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        const char c = p[i];
        if (c == '\n') {
            g_lines.push_back(std::move(g_partial));
            g_partial.clear();
            if (g_lines.size() > kKeep) g_lines.pop_front();
        } else if (c != '\r' && g_partial.size() < 200) {
            g_partial += c;
        }
    }
}

std::vector<std::string> logTail(std::size_t lines) {
    const std::size_t from = g_lines.size() > lines ? g_lines.size() - lines : 0;
    return {g_lines.begin() + static_cast<std::ptrdiff_t>(from), g_lines.end()};
}

void setCapturePath(const std::string& path) { g_capture = path; }
const std::string& capturePath() { return g_capture; }

unsigned heapBytes() { return __ctru_heap_size; }

}  // namespace omk::n3ds

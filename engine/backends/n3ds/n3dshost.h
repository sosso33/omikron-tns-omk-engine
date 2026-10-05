// SPDX-License-Identifier: GPL-3.0-or-later
// WHAT THE 3DS ENTRY AND THE 3DS FRONTEND SHARE (`todo/3ds-port.md` step 2b):
// where things are on the card, the LOG's last lines (every line the tee in
// `n3ds_main.cpp` passes on is kept here, so the instrument panel can show
// them), and the profiler capture the run was started with, if any. 3DS-side
// only - nothing under `src/` or `backends/sdl/` sees it.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace omk::n3ds {

// The card's layout (`n3ds_main.cpp` has the whole of it).
inline constexpr const char* kHome     = "sdmc:/omk";
inline constexpr const char* kRoot     = "sdmc:/omk/gamedata";
// The tables in the ROMFS - WITHOUT a trailing slash: the engine joins
// "<tables>/x.json", and libctru's romfs refuses "romfs://x.json".
inline constexpr const char* kTables   = "romfs:";
inline constexpr const char* kCaptures = "sdmc:/omk/captures";

// The log: the tee hands every byte written to stdout / stderr here, and the
// panel reads back the last complete lines.
void logAppend(const char* p, std::size_t n);
std::vector<std::string> logTail(std::size_t lines);

// The profiler capture `--profile <path>` opened, or "" without one: the
// panel's PAUSE / STEP / RESUME / snapshot are written to `<path>.ctl`, the
// control the game reads once a frame (`platform/profile.h`).
void setCapturePath(const std::string& path);
const std::string& capturePath();

// What libctru's start-up gave the newlib heap, in bytes.
unsigned heapBytes();

}  // namespace omk::n3ds

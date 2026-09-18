// SPDX-License-Identifier: GPL-3.0-or-later
// `%zu` ON THE VITA - the format rewrite `printf_c99.cpp` applies.
//
// **Found by running the bench in Vita3K (2026-09-18)**: the VitaSDK's newlib
// is built without C99 printf formats, so `%zu` prints a literal "zu" and
// CONSUMES NO ARGUMENT - every argument after it shifts by one. The bench's
// `lights %zu from %s` handed the light COUNT (155) to `%s` as a string
// pointer; the emulator logged an invalid read at 0x98 and carried on, and a
// real Vita would have faulted there. The engine logs with `%zu` in 125
// places (`play.cpp` 66 of them), and `%llx` works - the library has `ll`
// but not `z`.
//
// The fix is at the platform boundary rather than in 125 call sites: on the
// Vita `size_t` and `ptrdiff_t` are 32-bit, the same width as `unsigned int`
// and `int`, so dropping the `z` / `t` length modifier gives EXACTLY the
// conversion the source asked for. `j` (intmax_t, 64-bit) is NOT rewritten -
// it would need `ll`, and nothing here uses it.
//
// Header-only and platform-free, so the host can test it
// (`c99format_test.cpp`, `verify.py: engine: vita printf`).
#pragma once

#include <cstddef>
#include <cstring>

namespace omk::vita {

// Copy `fmt` into `out` (capacity `cap`, always terminated) with every `z`
// and `t` length modifier removed. -> true if anything was removed, false if
// the format needed no change or did not fit (the caller then uses `fmt`
// itself, which is no worse than before).
inline bool stripC99Lengths(const char* fmt, char* out, std::size_t cap) {
    if (!fmt || !cap) return false;
    bool changed = false;
    std::size_t o = 0;
    const auto put = [&](char c) { if (o + 1 < cap) out[o++] = c; else o = cap; };
    for (const char* p = fmt; *p; ) {
        if (*p != '%') { put(*p++); continue; }
        put(*p++);                                   // the '%'
        if (*p == '%') { put(*p++); continue; }      // "%%"
        // flags, width, precision: "-+ #0", digits, '.', '*'
        while (*p && std::strchr("-+ #0123456789.*", *p)) put(*p++);
        if (*p == 'z' || *p == 't') { ++p; changed = true; }
    }
    if (o >= cap) return false;                     // did not fit
    out[o] = '\0';
    return changed;
}

}  // namespace omk::vita

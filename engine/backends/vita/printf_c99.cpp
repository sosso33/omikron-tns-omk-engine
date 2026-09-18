// SPDX-License-Identifier: GPL-3.0-or-later
// `%zu` for the VitaSDK's newlib - see `c99format.h` for why.
//
// Linked into every Vita target with `-Wl,--wrap=<fn>` for each function
// below (`CMakeLists.txt`), so every call in the engine reaches the wrapper,
// which strips the `z` / `t` length modifiers from the format and hands the
// rewritten format to the real function. Vita-only by construction: nothing
// on the host links it. A format that needs no change goes through untouched.
#include "c99format.h"

#include <cstdarg>
#include <cstdio>

extern "C" {
int __real_vfprintf(FILE*, const char*, va_list);
int __real_vsnprintf(char*, size_t, const char*, va_list);
int __real_vsprintf(char*, const char*, va_list);

namespace {
// Formats are short; one that does not fit is passed through as it was.
constexpr std::size_t kCap = 1024;
}  // namespace

int __wrap_vfprintf(FILE* f, const char* fmt, va_list ap) {
    char buf[kCap];
    return __real_vfprintf(f, omk::vita::stripC99Lengths(fmt, buf, kCap) ? buf : fmt, ap);
}
int __wrap_vsnprintf(char* s, size_t n, const char* fmt, va_list ap) {
    char buf[kCap];
    return __real_vsnprintf(s, n, omk::vita::stripC99Lengths(fmt, buf, kCap) ? buf : fmt, ap);
}
int __wrap_vsprintf(char* s, const char* fmt, va_list ap) {
    char buf[kCap];
    return __real_vsprintf(s, omk::vita::stripC99Lengths(fmt, buf, kCap) ? buf : fmt, ap);
}
int __wrap_vprintf(const char* fmt, va_list ap) {
    return __wrap_vfprintf(stdout, fmt, ap);
}
int __wrap_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __wrap_vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}
int __wrap_fprintf(FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __wrap_vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}
int __wrap_snprintf(char* s, size_t n, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __wrap_vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}
int __wrap_sprintf(char* s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    const int r = __wrap_vsprintf(s, fmt, ap);
    va_end(ap);
    return r;
}
}  // extern "C"

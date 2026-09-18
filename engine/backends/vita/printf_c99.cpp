// SPDX-License-Identifier: GPL-3.0-or-later
// `%zu` for the VitaSDK's newlib - see `c99format.h` for why - and WHERE
// stdout goes.
//
// THE LOG: `freopen` does not redirect stdout on the Vita - newlib keeps
// writing the standard streams to the TTY whatever file is opened over them
// (seen in Vita3K, 2026-09-18: the log file opened, every line went to
// `tty0:`), so a console run would leave `omk-play.log` empty. Every printf
// already comes through here, so `omk_vita_redirect` points stdout and stderr
// at real files instead; `puts` and `putchar` are wrapped too, because GCC
// turns `printf("text\n")` into them.
//
// Linked into every Vita target with `-Wl,--wrap=<fn>` for each function
// below (`CMakeLists.txt`), so every call in the engine reaches the wrapper,
// which strips the `z` / `t` length modifiers from the format and hands the
// rewritten format to the real function. Vita-only by construction: nothing
// on the host links it. A format that needs no change goes through untouched.
#include "c99format.h"

#include <cstdarg>
#include <cstdio>
#include <malloc.h>

extern "C" {
int __real_vfprintf(FILE*, const char*, va_list);
int __real_puts(const char*);
int __real_putchar(int);

namespace {
FILE* g_out = nullptr;   // where stdout goes, when redirected
FILE* g_err = nullptr;
FILE* route(FILE* f) {
    if (f == stdout && g_out) return g_out;
    if (f == stderr && g_err) return g_err;
    return f;
}
}  // namespace

// Point stdout / stderr at these files (unbuffered - a crash must not take
// the last lines with it). Null leaves that stream on the TTY.
void omk_vita_redirect(FILE* out, FILE* err) {
    g_out = out;
    g_err = err;
    if (g_out) setvbuf(g_out, nullptr, _IONBF, 0);
    if (g_err) setvbuf(g_err, nullptr, _IONBF, 0);
}
// A heap checkpoint: `mallinfo` walks newlib's free lists, so it faults on a
// corrupted one - the checkpoint's label, logged first, says where.
void omk_vita_heap_check(const char* where) {
    if (g_out) { std::fputs("heapcheck: ", g_out); std::fputs(where, g_out); std::fputc('\n', g_out); }
    const struct mallinfo mi = mallinfo();
    if (g_out) std::fprintf(g_out, "heapcheck: ok, %d bytes in use\n", mi.uordblks);
}
int __real_vsnprintf(char*, size_t, const char*, va_list);
int __real_vsprintf(char*, const char*, va_list);

namespace {
// Formats are short; one that does not fit is passed through as it was.
constexpr std::size_t kCap = 1024;
}  // namespace

int __wrap_vfprintf(FILE* f, const char* fmt, va_list ap) {
    char buf[kCap];
    return __real_vfprintf(route(f), omk::vita::stripC99Lengths(fmt, buf, kCap) ? buf : fmt, ap);
}
int __wrap_vsnprintf(char* s, size_t n, const char* fmt, va_list ap) {
    char buf[kCap];
    return __real_vsnprintf(s, n, omk::vita::stripC99Lengths(fmt, buf, kCap) ? buf : fmt, ap);
}
int __wrap_vsprintf(char* s, const char* fmt, va_list ap) {
    char buf[kCap];
    return __real_vsprintf(s, omk::vita::stripC99Lengths(fmt, buf, kCap) ? buf : fmt, ap);
}
int __wrap_puts(const char* s) {
    if (!g_out) return __real_puts(s);
    return (std::fputs(s, g_out) < 0 || std::fputc('\n', g_out) < 0) ? EOF : 1;
}
int __wrap_putchar(int c) {
    return g_out ? std::fputc(c, g_out) : __real_putchar(c);
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

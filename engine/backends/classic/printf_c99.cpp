// SPDX-License-Identifier: GPL-3.0-or-later
// `%zu` FOR RETRO68 - the functions `classic_printf.h` redirects the printf
// family to (it says why, and why not `--wrap`). Each strips the `z` / `t`
// length modifiers (`backends/vita/c99format.h`; `size_t` is 32 bits here, as
// on the Vita, so that is exactly the conversion asked for) and calls the
// library's own function. Compiled with OMK_PRINTF_IMPL, so the redirect does
// not apply to this file. A format with nothing to strip goes through as is.
#include "../vita/c99format.h"

#include <cstdarg>
#include <cstdio>

extern "C" {
namespace {
// the rewritten format, or the original when nothing changed (or it did not fit)
struct Fmt {
    char buf[1024];
    const char* use;
    explicit Fmt(const char* f) { use = omk::vita::stripC99Lengths(f, buf, sizeof buf) ? buf : f; }
};
}  // namespace

int omk_c99_vfprintf(FILE* f, const char* fmt, va_list ap) { const Fmt w(fmt); return vfprintf(f, w.use, ap); }
int omk_c99_vsnprintf(char* s, size_t n, const char* fmt, va_list ap) { const Fmt w(fmt); return vsnprintf(s, n, w.use, ap); }
int omk_c99_vsprintf(char* s, const char* fmt, va_list ap) { const Fmt w(fmt); return vsprintf(s, w.use, ap); }
int omk_c99_vprintf(const char* fmt, va_list ap) { return omk_c99_vfprintf(stdout, fmt, ap); }
int omk_c99_printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    const int r = omk_c99_vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}
int omk_c99_fprintf(FILE* f, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    const int r = omk_c99_vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}
int omk_c99_snprintf(char* s, size_t n, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    const int r = omk_c99_vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}
int omk_c99_sprintf(char* s, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    const int r = omk_c99_vsprintf(s, fmt, ap);
    va_end(ap);
    return r;
}
}  // extern "C"

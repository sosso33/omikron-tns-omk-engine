// SPDX-License-Identifier: GPL-3.0-or-later
// `%zu` FOR RETRO68, at COMPILE time - force-included into every classic
// translation unit (`-include`, CMakeLists.txt). Retro68's newlib prints
// `%zu` as "zu" and consumes no argument, so every later argument shifts (a
// `%s` after it reads a size as a pointer: `omk-play`'s setup crashed in
// `printf` on Tiger, 2026-10-03). `printf_c99.cpp` strips the length
// modifier (`backends/vita/c99format.h`; exact where `size_t` is 32 bits).
//
// Why a macro and not the Vita's `-Wl,--wrap`: on XCOFF a call names the
// function's ENTRY symbol (`.vfprintf`), which `--wrap=vfprintf` does not
// rename, so `__real_` ends up undefined. `<cstdio>` is included FIRST
// because it `#undef`s the printf family; its include guard then keeps the
// macros below alive for the rest of the unit, `std::printf` included.
#pragma once
#if !defined(OMK_PRINTF_IMPL)
#include <cstdarg>
#include <cstddef>
#include <cstdio>

extern "C" {
int omk_c99_printf(const char*, ...);
int omk_c99_fprintf(FILE*, const char*, ...);
int omk_c99_sprintf(char*, const char*, ...);
int omk_c99_snprintf(char*, std::size_t, const char*, ...);
int omk_c99_vprintf(const char*, va_list);
int omk_c99_vfprintf(FILE*, const char*, va_list);
int omk_c99_vsprintf(char*, const char*, va_list);
int omk_c99_vsnprintf(char*, std::size_t, const char*, va_list);
}
namespace std {
using ::omk_c99_printf; using ::omk_c99_fprintf; using ::omk_c99_sprintf;
using ::omk_c99_snprintf; using ::omk_c99_vprintf; using ::omk_c99_vfprintf;
using ::omk_c99_vsprintf; using ::omk_c99_vsnprintf;
}
#define printf    omk_c99_printf
#define fprintf   omk_c99_fprintf
#define sprintf   omk_c99_sprintf
#define snprintf  omk_c99_snprintf
#define vprintf   omk_c99_vprintf
#define vfprintf  omk_c99_vfprintf
#define vsprintf  omk_c99_vsprintf
#define vsnprintf omk_c99_vsnprintf
#endif

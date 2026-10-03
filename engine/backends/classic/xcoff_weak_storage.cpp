// SPDX-License-Identifier: GPL-3.0-or-later
// STORAGE FOR THE LIBRARY'S WEAK .bss OBJECTS (`todo/classic-mac-port-1999.md`
// 3d-iv). Retro68's XCOFF linker gives a WEAK object in .bss a csect of
// length ZERO - no bytes of its own - so it sits on whatever is laid out
// next, and the two corrupt each other. The engine has no such object any
// more (an inline variable or a static local in an inline function is one;
// `formats/scx.h` says how it bit). What is left is libstdc++'s, and a
// STRONG definition of the same symbol beats the weak one at link, with real
// storage: so each is defined here, by its mangled name, with the size and
// value the library gives it.
//
// `verify.py: engine: classic build` lists every weak object without storage
// that another object starts on, and requires none.
#include <cstddef>

extern "C" {
// std::string::_Rep::_S_empty_rep_storage - the copy-on-write string's
// shared empty representation (exception messages are such strings):
// (sizeof(_Rep_base) + sizeof(char) + sizeof(size_t) - 1) / sizeof(size_t)
// words, all zero. It sat on `initialized.0` / `__new_handler`.
std::size_t omk_cow_empty_rep[4] __asm__("_ZNSs4_Rep20_S_empty_rep_storageE") = {0, 0, 0, 0};
}

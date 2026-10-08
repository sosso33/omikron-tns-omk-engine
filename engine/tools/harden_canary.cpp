// SPDX-License-Identifier: GPL-3.0-or-later
// The HARDENED build's canary: one out-of-range `subspan`, on purpose.
//
//     harden_canary
//
// Built by `make` it prints the size of a span that runs past its vector and
// exits 0 - which is exactly the silent read the code audit of 2026-10-07
// found in five format readers. Built by `make hardened` the standard library
// must TRAP on it. `verify.py: engine: hardened` asserts both, so a hardened
// build that quietly lost its flag fails as a check rather than passing every
// corpus walk for the wrong reason.
#include <cstdio>
#include <span>
#include <vector>

int main(int argc, char**) {
    std::vector<int> v(4);
    const std::span<const int> s(v);
    // the length depends on argc so the compiler cannot fold the call away
    const auto past = s.subspan(2, static_cast<std::size_t>(8 + argc));
    std::printf("canary span %zu over %zu\n", past.size(), v.size());
    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// `c99format.h` on the host - the rewrite the Vita's printf wrapper applies.
//
//     c99format_test
//
// One line a case, `case <n> <OK|BAD> <in> -> <out>`, then `failures N`.
// `verify.py: engine: vita printf`. Prints only.
#include "c99format.h"

#include <cstdio>
#include <cstring>

int main() {
    struct Case { const char* in; const char* want; bool changed; };
    const Case cases[] = {
        {"lights %zu from %s\n", "lights %u from %s\n", true},
        {"%zu", "%u", true},
        {"%5zu|%-8zu|%08zx", "%5u|%-8u|%08x", true},
        {"%.*zu", "%.*u", true},
        {"%td", "%d", true},
        {"100%% done, %zu left", "100%% done, %u left", true},
        {"%%zu is literal", "%%zu is literal", false},
        {"%lu %llx %s", "%lu %llx %s", false},
        {"no conversions", "no conversions", false},
        {"%jd stays", "%jd stays", false},
        {"", "", false},
    };
    int failures = 0, n = 0;
    for (const auto& c : cases) {
        char out[256];
        const bool changed = omk::vita::stripC99Lengths(c.in, out, sizeof out);
        const char* got = changed ? out : c.in;
        const bool ok = changed == c.changed && std::strcmp(got, c.want) == 0;
        failures += !ok;
        std::printf("case %d %s \"%s\" -> \"%s\"\n", n++, ok ? "OK" : "BAD", c.in, got);
    }
    // a format that does not fit is passed through, not truncated
    char tiny[4];
    const bool fit = omk::vita::stripC99Lengths("%zu%zu%zu", tiny, sizeof tiny);
    std::printf("case %d %s overflow -> passthrough\n", n++, fit ? "BAD" : "OK");
    failures += fit;
    std::printf("failures %d\n", failures);
    return failures ? 1 : 0;
}

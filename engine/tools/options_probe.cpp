// SPDX-License-Identifier: GPL-3.0-or-later
// Read a config file and say what it holds - the probe behind
// `verify.py: options file`.
#include "platform/options.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: options_probe <file.ini>\n");
        return 2;
    }
    const omk::OptionsFile o = omk::loadOptionsFile(argv[1]);
    std::size_t keys = 0;
    for (const auto& s : o.sections) keys += s.second.size();
    std::printf("loaded %d  sections %zu  keys %zu  unknown %zu  known-keys %zu\n",
                o.loaded ? 1 : 0, o.sections.size(), keys, o.unknown.size(),
                omk::preferenceKeys().size());
    for (const auto& s : o.sections)
        for (const auto& kv : s.second)
            std::printf("  %s.%s = %s\n", s.first.c_str(), kv.first.c_str(), kv.second.c_str());
    for (const auto& u : o.unknown) std::printf("  UNKNOWN preferences.%s\n", u.c_str());
    // the port's own table, so a check can compare the SET and not only the
    // count - two tables of 65 different names would otherwise agree
    for (const auto& k : omk::preferenceKeys()) std::printf("KEY %s\n", k.c_str());
    std::printf("clipdistance %d  displaysky %d  displayshadows %d  music %d\n",
                o.integer("Preferences", "clipdistance", -1),
                o.boolean("Preferences", "displaysky", false) ? 1 : 0,
                o.boolean("Preferences", "displayshadows", true) ? 1 : 0,
                o.integer("Preferences", "music", -1));
    return 0;
}

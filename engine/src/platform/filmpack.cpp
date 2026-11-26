// SPDX-License-Identifier: GPL-3.0-or-later
// The film pack - see filmpack.h. Not a port of anything in the original.
// (the remaster's: compiled empty without OMK_REMASTER - `engine/Makefile`)
#if OMK_REMASTER

#include "platform/filmpack.h"

#include "platform/datafs.h"
#include "platform/hostfs.h"
#include "platform/packkey.h"

#include <cstdio>
#include <cstdlib>

namespace omk {

std::uint64_t filmKey(const std::string& path) {
    std::FILE* f = std::fopen(hostPath(path).c_str(), "rb");
    if (!f) return 0;
    Fnv1a h;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) { std::fclose(f); return 0; }
    h.mixLe(static_cast<std::uint64_t>(size), 8);
    unsigned char buf[65536];
    std::size_t left = 1u << 20;
    while (left > 0) {
        const std::size_t n = std::fread(buf, 1, left < sizeof buf ? left : sizeof buf, f);
        if (n == 0) break;
        for (std::size_t i = 0; i < n; ++i) h.mix(buf[i]);
        left -= n;
    }
    std::fclose(f);
    return h.h;
}

bool FilmPack::open(const std::string& dir) {
    dir_ = dir;
    keys_.clear();
    if (!hostfs::isDirectory(dir)) return false;
    for (const auto& name : hostfs::listNames(dir)) {
        if (name.size() != 20 || name.substr(16) != ".mpg") continue;
        char* end = nullptr;
        // the hex KEPT while `end` points into it: a temporary's buffer is
        // gone at the semicolon (AddressSanitizer, 2026-11-26)
        const std::string hex = name.substr(0, 16);
        const unsigned long long k = std::strtoull(hex.c_str(), &end, 16);
        if (end && *end == '\0') keys_.insert(k);
    }
    return active();
}

std::string FilmPack::find(const std::string& path) const {
    if (keys_.empty()) return "";
    const std::uint64_t k = filmKey(path);
    if (!k || !keys_.count(k)) return "";
    return dir_ + "/" + keyHex(k) + ".mpg";
}

FilmPack& filmPack() {
    static FilmPack p;
    return p;
}

}  // namespace omk

#endif  // OMK_REMASTER

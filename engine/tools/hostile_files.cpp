// SPDX-License-Identifier: GPL-3.0-or-later
// DAMAGED GAME FILES against the format readers (the code audit of 2026-10-07).
//
//     hostile_files [case]     (morph fnt ani map2d map2d.waypoints opt scx; default all)
//
// Each case is a buffer built here whose counts or sizes were chosen to WRAP
// the reader's old 32-bit arithmetic, or to be a negative int16 dimension,
// so that a guard written as `offset + n * stride > size` passed it. None of
// these shapes ships: the original's loaders trust every count and a file
// like these would crash it too, so what is asserted is only that the port
// refuses them - small allocations, no read outside the buffer - while the
// shipped files decode as they did (the format checks, not this one).
//
// Prints one `name value` line per case. Reads nothing from disk.
#include "formats/anim.h"
#include "formats/fnt.h"
#include "formats/map2d.h"
#include "formats/morph.h"
#include "formats/opt.h"
#include "formats/scx.h"

#include <sys/resource.h>

#include <climits>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

void put32(std::vector<std::byte>& d, std::size_t o, std::uint32_t v) {
    for (int k = 0; k < 4; ++k) d[o + static_cast<std::size_t>(k)] = static_cast<std::byte>(v >> (8 * k));
}
void put16(std::vector<std::byte>& d, std::size_t o, std::uint16_t v) {
    d[o] = static_cast<std::byte>(v & 0xFF);
    d[o + 1] = static_cast<std::byte>(v >> 8);
}

}  // namespace

int main(int argc, char** argv) {
    const auto want = [&](const char* c) { return argc < 2 || std::strcmp(argv[1], c) == 0; };
    const auto say = [](const char* what, unsigned long long v) { std::printf("%s %llu\n", what, v); std::fflush(stdout); };

    // .3DM: an audio block of 0xFFFFFF and a vertex count whose 24x makes the
    // 32-bit record 35 - smaller than its own audio, so the old audio walk
    // started 16 MB before the buffer.
    if (want("morph")) {
        std::vector<std::byte> d(64);
        put32(d, 0, 0x00FFFFFFu);        // audio, one channel
        put32(d, 4, 0x0AA00001u);        // vertices: 24 * this = 0xFF000018 (mod 2^32)
        put32(d, 12, 0);                 // nodes
        const auto L = omk::morphLayout(d);
        say("morph.valid", L.valid);
        say("morph.audio.bytes", omk::morphAudio(d, L).size());
    }

    // .FNT: glyph 'A' with width -1, so width * height wraps to 2^64 - 6 and
    // the old `offset + pixels() > size` passed with a span of that size.
    if (want("fnt")) {
        std::vector<std::byte> d(omk::kFntHeader + 64);
        const std::size_t g = 8 * static_cast<std::size_t>('A');
        put16(d, g, static_cast<std::uint16_t>(omk::kFntHeader / 8));   // offset, in 8-byte units
        put16(d, g + 4, static_cast<std::uint16_t>(-1));                 // width
        put16(d, g + 6, 6);                                              // height
        const auto f = omk::readFnt(d);
        say("fnt.coverage.size", f.coverage('A').size());
    }

    // .ani: a track claiming INT_MAX rotation keys in a 64-byte file. The
    // old reader reserved all of them (32 GB) before reading the first.
    if (want("ani")) {
        std::vector<std::byte> d(64);
        omk::AnimTrack t;
        t.rotOffset = 16;
        t.rotKeys = INT_MAX;
        const auto q = omk::animRotations(d, t);
        say("ani.keys", q.size());
        say("ani.capacity.small", q.capacity() <= 3);
    }

    // .mpt (the 2D map): a floor declaring 153391690 links, whose 28x wraps
    // to 24 - the old guard passed and resized 4 GB of links.
    if (want("map2d")) {
        std::vector<std::byte> d(128);
        put32(d, 0, 1);                  // scale
        put32(d, 4, 1);                  // floors
        put32(d, 8, 153391690u);         // links on floor 0
        omk::Map2d m;
        say("map2d.loaded", m.load(d));
        // refused either way - the old reader only after the resize - so
        // what tells them apart is this process's own peak memory
        rusage ru{};
        getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
        const unsigned long long peak = static_cast<unsigned long long>(ru.ru_maxrss);          // bytes
#else
        const unsigned long long peak = static_cast<unsigned long long>(ru.ru_maxrss) * 1024;   // KB
#endif
        say("map2d.peak.under.256MB", peak < (256ull << 20));
    }

    // .mpt again: one floor with no links and no cells, then a WAYPOINT count
    // of 0xFFFFFFFF - resized before anything was checked (found by the scan
    // that followed the audit, not by the audit).
    if (want("map2d.waypoints")) {
        std::vector<std::byte> d(128);
        put32(d, 0, 1);                  // scale
        put32(d, 4, 1);                  // floors
        put32(d, 8, 0);                  // links on floor 0
        // +12: six bound floats, then w = h = 0 at +36 / +40 - all zero
        put32(d, 44, 0xFFFFFFFFu);       // waypoints on floor 0
        omk::Map2d m;
        say("map2d.waypoints.loaded", m.load(d));
    }

    // .OPT: block 0 counted so that 24 * count wraps to 8, landing the chain
    // on a header where the next block "starts". The old walk accepted it
    // and resized 178956971 lanes.
    if (want("opt")) {
        std::vector<std::byte> d(84);
        std::memcpy(d.data(), "V1.0", 4);
        const std::uint32_t n0 = 178956971u;     // 24 * n0 = 2^32 + 8
        // the header's words count from the magic, which is h[0]
        put32(d, 4 * 5, n0);                     // h[5]  lane count
        put32(d, 4 * 6, 76);                     // h[6]  block 0 offset
        for (int b = 1; b < 7; ++b) put32(d, 4 * static_cast<std::size_t>(6 + 2 * b), 84);
        const auto t = omk::loadOpt(d);
        say("opt.valid", t.valid);
        say("opt.lanes", t.lanes.size());
    }

    // .SCX: a block size of 0xFFFFFFF8, so 16 + it wraps to 8 - in the
    // stream reader's u32 everywhere, in `readScx`'s size_t only on a 32-bit
    // build. On a 64-bit host both were refused anyway, so these two lines
    // guard the 32-bit builds' arithmetic and tell nothing apart here.
    if (want("scx")) {
        std::vector<std::byte> d(64);
        put32(d, 0, 0x00DEAD00u);
        put32(d, 4, 5);
        put32(d, 12, 0xFFFFFFF8u);
        say("scx.valid", omk::readScx(d).valid);
        say("scx.stream.valid", omk::readScxStream(d).valid);
    }
    return 0;
}

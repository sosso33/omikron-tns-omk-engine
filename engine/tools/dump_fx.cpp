// SPDX-License-Identifier: GPL-3.0-or-later
// The ambient effects chain, as far as it can be walked without the .SCX
// stream: every .SFX's six-section walk, and every decor mesh flagged
// 0x40000000 bound through section D to its section C effect.
//
//     dump_fx <gamedata/SCPTDATA> <gamedata/MESHES/DECORS> <out.bin>
//
// The last hop - section C's sprite id to the sprite itself, whose quads are
// its animation frames - lives in the .SCX STREAM and is not ported yet. It is
// worth 4 of the 325 bindings: those are emitters whose effect resolves and
// whose sprite does not.
#include "formats/mesh3do.h"
#include "formats/scx.h"
#include "formats/sfx.h"

#include <algorithm>
#include <cctype>
#include "platform/datafs.h"

#include <span>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string extOf(const fs::path& p) { return upper(p.extension().string()); }

// chunk 4's in-block registry row is 36 bytes; +32 is the sprite id a section
// C effect names. Read from the block rather than kept by readScx, which only
// counts the rows.
std::uint16_t spriteIdAt(std::span<const std::byte> d, std::uint32_t i) {
    const auto u32at = [&](std::size_t o) {
        return o + 4 > d.size() ? 0u :
            static_cast<std::uint32_t>(d[o]) |
            static_cast<std::uint32_t>(d[o+1]) << 8 |
            static_cast<std::uint32_t>(d[o+2]) << 16 |
            static_cast<std::uint32_t>(d[o+3]) << 24;
    };
    if (d.size() < 16) return 0;
    const auto blockSize = u32at(12);
    const auto b = d.subspan(16, std::min<std::size_t>(blockSize, d.size() - 16));
    // find chunk 4 by walking the tags, as the loader does
    const auto n = u32at(16 + 4);
    std::size_t o = 8u + 100u * n;
    const auto pc = (16 + o + 4 <= d.size()) ? u32at(16 + o) : 0u;
    o += 4u + 4u * pc;
    for (std::uint32_t k = 0; k < n; ++k) {
        const auto r = 8u + 100u * static_cast<std::size_t>(k);
        o += (o < b.size() && static_cast<std::uint8_t>(b[o])) ? 22u : 1u;
        const std::size_t tot = u32at(16 + r + 32) + u32at(16 + r + 44);
        o += 24u * tot;
        for (int t = 0; t < 2; ++t) { const auto c = u32at(16 + o); o += 4u + 29u * c; }
    }
    while (o + 4 <= b.size()) {
        const auto t = u32at(16 + o);
        if (t == 0xDEADFFFFu) break;
        const int ty = static_cast<int>(t & 0xFFFFu);
        if ((t >> 16) != 0xDEADu) { o += 4; continue; }
        o += 4;
        const auto it = omk::kScxStride.find(ty);
        if (it == omk::kScxStride.end()) continue;
        const auto c = u32at(16 + o);
        if (ty == 4) {
            if (i >= c) return 0;
            const auto rec = 16 + o + 4u + 36u * static_cast<std::size_t>(i);
            return rec + 34 <= d.size()
                 ? static_cast<std::uint16_t>(
                       static_cast<std::uint16_t>(d[rec + 32]) |
                       static_cast<std::uint16_t>(d[rec + 33]) << 8)
                 : 0;
        }
        o += 4u + it->second * c;
    }
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: dump_fx <SCPTDATA> <DECORS> <out.bin>\n");
        return 2;
    }
    // ---- every .SFX: the six-section walk
    std::map<std::string, fs::path> sfxOf;
    std::vector<fs::path> sfxPaths;
    for (const auto& e : fs::directory_iterator(argv[1]))
        if (e.is_regular_file() && extOf(e.path()) == ".SFX") {
            sfxPaths.push_back(e.path());
            sfxOf[upper(e.path().stem().string())] = e.path();
        }
    std::sort(sfxPaths.begin(), sfxPaths.end());

    int files = 0, exact = 0;
    long counts[6] = {};
    for (const auto& p : sfxPaths) {
        const auto s = omk::readSfx(omk::DataFs::readPath(p.string()));
        if (!s.valid) continue;
        ++files;
        if (!s.exact) continue;
        ++exact;
        for (int k = 0; k < 6; ++k) counts[k] += s.counts[k];
    }

    // ---- the chain: a flagged mesh's name, as a dword, against section D
    int sets = 0, withFx = 0, flagged = 0, bound = 0, emitters = 0;
    std::vector<fs::path> models;
    for (const auto& e : fs::directory_iterator(argv[2]))
        if (e.is_regular_file() && extOf(e.path()) == ".3DO") models.push_back(e.path());
    std::sort(models.begin(), models.end());

    for (const auto& p : models) {
        ++sets;
        const auto stem = upper(p.stem().string());
        std::map<std::string, const omk::FxBinding*> tags;
        std::map<std::uint16_t, int> sprites;      // sprite id -> stream index
        omk::SfxFile sfx;
        if (const auto it = sfxOf.find(stem); it != sfxOf.end()) {
            sfx = omk::readSfx(omk::DataFs::readPath(it->second));
            for (const auto& b : sfx.bindings)
                tags.emplace(std::string(b.tag, 4), &b);
        }
        if (!tags.empty()) ++withFx;

        // chunk 4's registry gives the sprite ids; the stream gives the
        // payloads, and an id with no payload is what strands an emitter
        if (!tags.empty()) {
            omk::DataFs scpt(argv[1]);
            if (const auto sp = scpt.resolveSibling(stem + ".SCX", "scx")) {
                const auto sd = omk::DataFs::readPath(*sp);
                const auto scene = omk::readScx(sd);
                const auto strm  = omk::readScxStream(sd);
                // the registry row's +32 is the sprite ID the effect names
                const std::uint32_t declared = scene.chunkCounts.count(4)
                    ? static_cast<std::uint32_t>(scene.chunkCounts.at(4)) : 0u;
                const auto n = std::min<std::uint32_t>(
                    declared, static_cast<std::uint32_t>(strm.sprites.size()));
                for (std::uint32_t k = 0; k < n; ++k)
                    sprites.emplace(spriteIdAt(sd, k), static_cast<int>(k));
            }
        }

        const auto d = omk::DataFs::readPath(p.string());
        const auto h = omk::readHeader(d);
        if (!h) continue;
        const auto ms = omk::readMeshes(d, *h);
        for (const auto& m : ms) {
            if (!(static_cast<std::uint32_t>(m.flags) & 0x40000000u)) continue;
            ++flagged;
            // the first FOUR BYTES of the mesh's name, compared as a dword -
            // read from the file rather than from the parsed name, because a
            // name shorter than four bytes still carries its padding
            const auto o = static_cast<std::size_t>(h->meshOff)
                         + omk::kMeshRecord * static_cast<std::size_t>(m.index) + 16u;
            if (o + 4 > d.size()) continue;
            const std::string key(reinterpret_cast<const char*>(d.data()) + o, 4);
            const auto it = tags.find(key);
            if (it == tags.end()) continue;
            // and the tag must name a real section-C effect
            const omk::FxEffect* eff = nullptr;
            for (const auto& e : sfx.effects)
                if (e.id == it->second->effect) { eff = &e; break; }
            if (!eff) continue;
            ++bound;
            // the last hop: the effect's sprite id, resolved through the .SCX
            // STREAM. An emitter whose sprite is missing is bound but cannot
            // be drawn, which is the difference between `bound` and `emitters`.
            if (sprites.count(eff->sprite)) ++emitters;
        }
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(files); put32(exact);
    for (long c : counts) put32(static_cast<std::int32_t>(c));
    put32(sets); put32(withFx); put32(flagged); put32(bound); put32(emitters);
    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf(".SFX: %d files, %d walk exactly; sections A-F %ld %ld %ld %ld %ld %ld\n"
                "chain: %d decor sets, %d with bindings; %d meshes flagged "
                "0x40000000, %d bind an effect, %d also resolve a sprite\n",
                files, exact, counts[0], counts[1], counts[2], counts[3],
                counts[4], counts[5], sets, withFx, flagged, bound, emitters);
    return 0;
}

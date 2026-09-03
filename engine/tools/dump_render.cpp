// SPDX-License-Identifier: GPL-3.0-or-later
// The render DECISIONS, over the whole corpus - the differential against
// verify.py's `drawable mask`, `render bucket key`, `texture name cache` and
// `anekbah residency`.
//
//     dump_render <gamedata/MESHES> <out.bin>
//
// The last section is the one that does something the reference does not: it
// RUNS the 58-slot cache. The Python asserts that Anekbah and AImpasse share
// texture names whose pixels differ; this loads one set and then the other,
// the way `Area_LoadSet` does with two sets resident, and reports which of the
// incoming set's materials got a cache hit and therefore point at the outgoing
// location's pixels.
//
// out.bin: int32 persoMeshes, persoSkipped, byBit0, loneTriangle, flagless,
//          int32 setMeshes, setSkipped, viewerDraws,
//          int32 stateBitsOverlappingTheSlot, slotsFitIn6Bits, keyBits,
//          int32 materials, shippingMinusOne,
//          int32 collidingNames, anekbahColliding,
//          int32 setAdditive, setMultiply, additive, multiply, blendWithout0x1000,
//          int32 impasseLoads, anekbahHits, anekbahLoads, batitr12Hit
#include "formats/mesh3do.h"
#include "o3de/render.h"
#include "o3de/texcache.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

// every .3DO under a directory, recursively, case-insensitively
void collect(const std::string& dir, std::vector<std::string>& out) {
    const omk::DataFs fs(dir);
    for (const auto& p : fs.list(".", ".3DO")) out.push_back(p);
    for (const auto& sub : fs.subdirs()) collect(sub, out);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_render <gamedata/MESHES> <out.bin>\n");
        return 2;
    }
    const std::string meshes = argv[1];
    std::vector<std::string> all;
    collect(meshes, all);
    std::sort(all.begin(), all.end());

    long persoTot = 0, persoSkip = 0, byBit0 = 0, lone = 0, flagless = 0;
    long setTot = 0, setSkip = 0, viewerDraws = 0;
    long mats = 0, minusOne = 0;
    // Counted over the DECORS alone AND over everything. ASSETS 4's table of
    // 211/6 is "the 220 shipped sets", and reading it as a whole-corpus number
    // would look like a disagreement when it is a different question.
    long additive = 0, multiply = 0, blendNo1000 = 0;
    long setAdditive = 0, setMultiply = 0;
    std::map<std::string, std::set<std::string>> byName;  // key -> distinct pixels

    for (const auto& path : all) {
        const auto d = omk::DataFs::readPath(path);
        const auto h = omk::readHeader(d);
        if (!h) continue;
        const bool isPerso = path.find("PERSOS") != std::string::npos ||
                             path.find("persos") != std::string::npos;
        const bool isDecor = path.find("DECORS") != std::string::npos ||
                             path.find("decors") != std::string::npos;
        for (const auto& m : omk::readMeshes(d, *h)) {
            const auto f = static_cast<std::uint32_t>(m.flags);
            const bool skip = !omk::meshDrawable(f);
            if (isPerso) {
                ++persoTot;
                if (skip) {
                    ++persoSkip;
                    if (f & 1u) ++byBit0;
                    if (m.triangles == 1 && m.vertices == 3 && m.quads == 0) ++lone;
                }
                if (f == 0) ++flagless;
            }
            if (isDecor) {
                ++setTot;
                if (skip) {
                    ++setSkip;
                    // the three the viewers' own heuristic draws and the
                    // engine does not: flagged 0x43 but not 0x800000
                    if ((f & 0x43u) && !(f & 0x800000u)) ++viewerDraws;
                }
            }
            switch (omk::meshBlend(f)) {
                case omk::BlendMode::Additive:
                    ++additive; if (isDecor) ++setAdditive; break;
                case omk::BlendMode::Multiply:
                    ++multiply; if (isDecor) ++setMultiply; break;
                default: break;
            }
            if (!(f & 0x1000u) && (f & (0x2000u | 0x4000u))) ++blendNo1000;
        }
        // the materials, and the .3dt walk that pairs each with its pixels
        const auto tex = omk::DataFs::readPath(
            path.substr(0, path.size() - 4) + ".3dt");
        std::size_t off = 0;
        for (int i = 0; i < h->materials; ++i) {
            const auto m = omk::readMaterial(d, *h, i);
            if (!m) break;
            ++mats;
            if (m->slotOnDisk == -1 && m->paletteOnDisk == -1) ++minusOne;
            const auto n = m->bytesIn3dt();
            if (!tex.empty() && off + n <= tex.size()) {
                // a cheap content signature: the pixels this name would load
                std::uint64_t sig = 1469598103934665603ull;
                for (std::size_t k = 0; k < n; ++k) {
                    sig ^= static_cast<std::uint64_t>(tex[off + k]);
                    sig *= 1099511628211ull;
                }
                char buf[32];
                std::snprintf(buf, sizeof buf, "%llx",
                              static_cast<unsigned long long>(sig));
                std::string key(m->texture,
                                std::min(std::strlen(m->texture), omk::kCacheKeyChars));
                byName[key].insert(buf);
            }
            off += n;
        }
    }
    long colliding = 0;
    for (const auto& [k, v] : byName) if (v.size() > 1) ++colliding;

    // ---- the residency simulation ----------------------------------------
    //
    // `traces/impasse-walk.log` announces AREAS 222 then AREAS 0 - AIMPASSE
    // then ANEKBAH - so the capture walks the player out of the Impasse into
    // Anekbah with the Impasse's set still in the other slot. This is that.
    const omk::DataFs decors(meshes + "/DECORS");
    long impLoads = 0, anekHits = 0, anekLoads = 0;
    int batitr12 = 0;
    std::vector<std::string> substituted;
    {
        omk::TextureCache cache;
        const auto ip = decors.resolve("AImpasse.3DO");
        const auto ap = decors.resolve("Anekbah.3DO");
        if (ip && ap) {
            const auto id = omk::DataFs::readPath(*ip);
            const auto ad = omk::DataFs::readPath(*ap);
            const auto ih = omk::readHeader(id);
            const auto ah = omk::readHeader(ad);
            if (ih && ah) {
                impLoads = omk::TextureCache().bind("AImpasse", *ih, id).loads;
                cache.bind("AImpasse", *ih, id);
                const auto r = cache.bind("Anekbah", *ah, ad);
                anekHits = r.hits;
                anekLoads = r.loads;
                for (const auto& e : r.materials)
                    if (e.hit) {
                        substituted.push_back(e.texture);
                        if (e.texture.rfind("BATITR12", 0) == 0) batitr12 = 1;
                    }
            }
        }
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    for (long v : {persoTot, persoSkip, byBit0, lone, flagless,
                   setTot, setSkip, viewerDraws})
        put32(static_cast<std::int32_t>(v));
    put32(omk::kStateBits & omk::kTextureSlotMask);
    put32(omk::kTextureSlots < 64 ? 1 : 0);
    put32(omk::kStateBits | omk::kTextureSlotMask);
    put32(static_cast<std::int32_t>(mats));
    put32(static_cast<std::int32_t>(minusOne));
    put32(static_cast<std::int32_t>(colliding));
    // Anekbah's own, from the simulation's own model
    long anekColliding = 0;
    {
        const auto ap = decors.resolve("Anekbah.3DO");
        if (ap) {
            const auto ad = omk::DataFs::readPath(*ap);
            if (const auto ah = omk::readHeader(ad))
                for (int i = 0; i < ah->materials; ++i)
                    if (const auto m = omk::readMaterial(ad, *ah, i)) {
                        std::string key(m->texture,
                            std::min(std::strlen(m->texture), omk::kCacheKeyChars));
                        auto it = byName.find(key);
                        if (it != byName.end() && it->second.size() > 1) ++anekColliding;
                    }
        }
    }
    put32(static_cast<std::int32_t>(anekColliding));
    for (long v : {setAdditive, setMultiply, additive, multiply, blendNo1000})
        put32(static_cast<std::int32_t>(v));
    for (long v : {impLoads, anekHits, anekLoads})
        put32(static_cast<std::int32_t>(v));
    put32(batitr12);
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));

    std::printf("%zu models. PERSOS: %ld meshes, %ld skipped by 0x800043 "
                "(%ld by bit 0, %ld lone triangles), %ld flagless\n",
                all.size(), persoTot, persoSkip, byBit0, lone, flagless);
    std::printf("DECORS: %ld meshes, %ld skipped, %ld of those the viewers draw\n",
                setTot, setSkip, viewerDraws);
    std::printf("key: state bits over the texture slot = 0x%X, %d slots fit in "
                "6 bits, the key carries 0x%X\n",
                omk::kStateBits & omk::kTextureSlotMask, omk::kTextureSlots,
                omk::kStateBits | omk::kTextureSlotMask);
    std::printf("materials: %ld, %ld shipping +64/+68 as -1; %ld texture names "
                "with DIFFERENT pixels in different models (%ld of Anekbah's)\n",
                mats, minusOne, colliding, anekColliding);
    std::printf("blend: DECORS %ld additive / %ld multiply (ASSETS 4's table); "
                "whole corpus %ld / %ld; %ld asking a sub-mode without 0x1000\n",
                setAdditive, setMultiply, additive, multiply, blendNo1000);
    std::printf("residency - AImpasse resident, then Anekbah loads:\n"
                "    AImpasse alone takes %ld slots; Anekbah then gets %ld "
                "cache HITS and %ld loads\n", impLoads, anekHits, anekLoads);
    std::printf("    substituted:");
    for (const auto& s : substituted) std::printf(" %s", s.c_str());
    std::printf("\n");
    return 0;
}

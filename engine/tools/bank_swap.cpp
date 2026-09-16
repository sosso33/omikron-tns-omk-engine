// SPDX-License-Identifier: GPL-3.0-or-later
// A BANK SWAP MUST INVALIDATE THE CLIP-KEYED CACHES.
//
//     bank_swap <gamedata>
//
// `PlayerController` memoises decoded animation tracks, root tracks and the
// variant grid on the clip INDEX (`clipTracks`, `rootOf`, `gridTracks`), and
// an index means something different in every `.CTL` bank: clip 0 of
// `H1AVNT` is Kay'l's idle, clip 0 of `H1CMBT` is his guard. `setBank` swaps
// `ctl_` and `data_`, so without clearing those caches the channel moves to
// the new bank while the POSE keeps coming out of the old one - which is what
// a reader saw after winning the supermarket fight: Kay'l walking around
// still holding his fists up (`todo/fight-mode.md` 15.6).
//
// Prints the default clip index and a checksum of the tracks actually handed
// to the poser, for AVNT -> CMBT -> AVNT. The middle one must DIFFER (the
// swap reached the poser) and the last must EQUAL the first (the swap back
// restored it, rather than leaving a third stale thing).
#include "actor/player.h"
#include "actor/pose.h"
#include "formats/ctl.h"
#include "formats/mesh3do.h"
#include "o3de/geom3do.h"
#include "platform/datafs.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
// A cheap order-sensitive checksum of what the poser will read.
std::uint64_t sigOf(const omk::NodeTracks* t) {
    if (!t || !t->valid()) return 0;
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::uint64_t v) { h ^= v; h *= 1099511628211ull; };
    mix(static_cast<std::uint64_t>(t->count));
    mix(static_cast<std::uint64_t>(t->frames));
    for (int id : t->ids) mix(static_cast<std::uint64_t>(id + 2));
    const std::size_t n = t->quats.size() < 8 ? t->quats.size() : 8;
    for (std::size_t f = 0; f < n; ++f)
        for (const auto& q : t->quats[f])
            for (float c : {q.w, q.x, q.y, q.z}) {
                std::uint32_t b = 0; std::memcpy(&b, &c, 4);
                mix(b);
            }
    return h;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: bank_swap <gamedata>\n"); return 2; }
    const std::string fr = argv[1];
    const omk::DataFs fs(fr);
    const auto avntData = fs.read("ANIMS/H1AVNT.CTL");
    const auto cmbtData = fs.read("ANIMS/H1CMBT.CTL");
    const auto modData  = fs.read("MESHES/PERSOS/HO1_FN.3DO");
    const auto setData  = fs.read("MESHES/DECORS/ANEKBAH.3DO");
    const omk::CtlFile avnt = omk::readCtl(avntData);
    const omk::CtlFile cmbt = omk::readCtl(cmbtData);
    std::vector<omk::Mesh> meshes;
    if (const auto h = omk::readHeader(modData)) meshes = omk::readMeshes(modData, *h);
    const auto soup = omk::collisionSoup(setData, omk::SoupKind::Walkable);
    if (!avnt.valid || !cmbt.valid || meshes.empty() || soup.empty()) {
        std::printf("bank_swap: inputs missing\n"); return 1;
    }
    omk::PlayerController::Setup su;
    su.ctl = &avnt; su.ctlData = avntData; su.meshes = &meshes; su.soup = &soup;
    su.pos[0] = 1804.0f; su.pos[1] = 0.0f; su.pos[2] = -6890.0f;
    su.facing = 336.0f;
    omk::PlayerController pc(su);

    const int   c0 = pc.clip();  const std::uint64_t s0 = sigOf(pc.poseTracks());
    pc.setBank(cmbt, cmbtData);
    const int   c1 = pc.clip();  const std::uint64_t s1 = sigOf(pc.poseTracks());
    pc.setBank(avnt, avntData);
    const int   c2 = pc.clip();  const std::uint64_t s2 = sigOf(pc.poseTracks());

    std::printf("avnt clip %d sig %llu\n", c0, (unsigned long long)s0);
    std::printf("cmbt clip %d sig %llu\n", c1, (unsigned long long)s1);
    std::printf("back clip %d sig %llu\n", c2, (unsigned long long)s2);
    std::printf("swapped %d restored %d nonzero %d\n",
                (s1 != s0) ? 1 : 0, (s2 == s0) ? 1 : 0,
                (s0 && s1 && s2) ? 1 : 0);
    return 0;
}

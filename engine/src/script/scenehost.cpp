// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/scenehost.h"

#include "platform/datafs.h"
#include "script/world.h"

#include <cctype>
#include <vector>

namespace omk {
namespace {

constexpr std::uint8_t kSceneLoad = 71;
constexpr std::size_t  kAreaSetStem = 97, kAreaSetStemLen = 9;

std::int16_t i16(const std::vector<std::uint8_t>& v, std::size_t o) {
    if (o + 2 > v.size()) return 0;
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(v[o]) |
                                     (static_cast<std::uint16_t>(v[o + 1]) << 8));
}

void collect(std::span<const std::byte> file, ChunkKind kind,
             const OpcodeTable& table, std::map<int, int>& out) {
    const auto ar = IamArchive::open(file);
    for (std::size_t i = 0; i < ar.size(); ++i) {
        const auto body = ar.chunk(i);
        if (body.empty()) continue;
        // EVERY script the chunk owns, and the STARTUP one has to be in the
        // list. `chunkSlots` enumerates the zone records and the message
        // subscriptions - the 5785 - and nothing in that walk reaches the
        // chunk's own `+4`, which is the trap CLAUDE.md 6 records: a negative
        // result over a corpus is only as strong as the enumeration behind it.
        //
        // It bit here exactly as it bit there. **Scene 55's only `scene.load`
        // is in AREA 118's STARTUP script** (`scene.load 222, 55`, the
        // instruction after the intro's `area.goto`), so a slots-only scan
        // leaves scene 55 out of the map, `resolveScx` returns nothing for it,
        // `SceneRunner::load` fails, and the transition silently keeps the
        // OUTGOING scene resident. The Impasse's sixteen beats then address
        // objects that only exist in `Impasse.SCX` while `Grid.SCX` is still
        // loaded, all sixteen miss, and the first `scx.play.player.wait` parks
        // its context on a program that will never exist - a black screen with
        // the music still playing.
        std::vector<std::size_t> at;
        if (const auto s0 = startupScript(body)) at.push_back(s0);
        for (const auto& s : chunkSlots(body, kind)) at.push_back(s.offset);
        for (const auto off : at) {
            const auto d = decodeScript(body, off, body.size(), table);
            if (d.status != DecodeStatus::Ok) continue;
            for (const auto& in : d.code) {
                if (in.op != kSceneLoad || in.operand.size() < 4) continue;
                const int area  = i16(in.operand, 0);
                const int scene = i16(in.operand, 2);
                out.emplace(scene, area);      // first writer wins, as the
            }                                  // reference's setdefault does
        }
    }
}

}  // namespace

std::map<int, int> sceneToArea(std::span<const std::byte> areaFile,
                               std::span<const std::byte> sceneFile,
                               const OpcodeTable& table) {
    std::map<int, int> out;
    collect(areaFile,  ChunkKind::Area,  table, out);
    collect(sceneFile, ChunkKind::Scene, table, out);
    return out;
}

std::string resolveScx(const DataFs& scptdata,
                       std::span<const std::byte> areaFile,
                       std::span<const std::byte> sceneFile,
                       const OpcodeTable& table, ChunkKind kind, int chunk) {
    int area = chunk;
    if (kind == ChunkKind::Scene) {
        const auto map = sceneToArea(areaFile, sceneFile, table);
        const auto it = map.find(chunk);
        if (it == map.end()) return {};
        area = it->second;
    }
    const auto ar = IamArchive::open(areaFile);
    const auto b = ar.chunk(static_cast<std::size_t>(area));
    if (b.size() < kAreaSetStem + kAreaSetStemLen) return {};
    std::string stem;
    for (std::size_t k = 0; k < kAreaSetStemLen; ++k) {
        const auto c = static_cast<unsigned char>(b[kAreaSetStem + k]);
        if (!c) break;
        stem.push_back(static_cast<char>(c));
    }
    if (stem.empty()) return {};
    // DataFs resolves the case, which is the whole reason it exists: the stem
    // is authored in whatever case the designer typed and the file on the disc
    // is in whatever case the artist saved it.
    const auto hit = scptdata.resolve(stem + ".SCX");
    if (!hit) return {};
    const auto slash = hit->find_last_of('/');
    return slash == std::string::npos ? *hit : hit->substr(slash + 1);
}

}  // namespace omk

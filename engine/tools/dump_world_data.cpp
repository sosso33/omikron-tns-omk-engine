// SPDX-License-Identifier: GPL-3.0-or-later
// The three world-DATA tables a replica needs before any of it can be played:
// the message subscriptions, the objects, and the object-combination recipes.
//
//     dump_world_data <gamedata> <out.bin>
//
// Written as one tool because they interlock - the recipes name objects, and
// whether a recipe's product is reachable is a question about the world
// scripts - and a differential that checks them separately cannot ask that.
//
// out.bin: int32 subscriptions, withScript, badPointer, highestMessage,
//          int32 objects, idsMatchingSlot,
//          int32 recipes, idsNamingARealObject, gatesWanting8, spellItemObject
#include "formats/iam.h"
#include "platform/datafs.h"
#include "script/globaldata.h"
#include "script/objects.h"
#include "script/script.h"

#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_world_data <gamedata> <out.bin>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);

    // --- the subscriptions -------------------------------------------------
    //
    // The same 8-byte table `chunkSlots` mines for script offsets, read as the
    // records `Message_RunHandlers` walks.  A subscription with script 0 is a
    // real row, not a parse failure: 16 of the 154 have no handler.
    int subs = 0, withScript = 0, bad = 0, highest = -1;
    const auto count = [&](std::span<const std::byte> body,
                           const std::vector<omk::Subscription>& v) {
        for (const auto& s : v) {
            ++subs;
            if (s.script > 0) {
                ++withScript;
                if (static_cast<std::size_t>(s.script) >= body.size()) ++bad;
            }
            if (s.message > highest) highest = s.message;
        }
    };
    for (const char* nm : {"IAM/AREA", "IAM/SCENE"}) {
        const auto raw = fs.read(nm);
        const auto kind = nm[4] == 'A' ? omk::ChunkKind::Area : omk::ChunkKind::Scene;
        const auto ar = omk::IamArchive::open(raw);
        for (std::size_t i = 0; i < ar.size(); ++i) {
            const auto body = ar.chunk(i);
            if (body.empty()) continue;
            count(body, omk::chunkSubscriptions(body, kind));
        }
    }
    const auto glob = fs.read("IAM/GLOBAL");
    count(glob, omk::globalSubscriptions(glob));

    // --- the objects -------------------------------------------------------
    const auto objs = omk::loadObjects(fs);
    int idOk = 0;
    for (const auto& o : objs)
        if (o.id == o.index) ++idOk;
    const auto named = [&](int id) {
        return id >= 0 && static_cast<std::size_t>(id) < objs.size() &&
               !objs[static_cast<std::size_t>(id)].name.empty();
    };

    // --- the recipes -------------------------------------------------------
    const auto rec = omk::globalRecipes(glob);
    int realIds = 0, want8 = 0;
    for (const auto& r : rec) {
        for (int id : {static_cast<int>(r.a), static_cast<int>(r.b),
                       static_cast<int>(r.product)})
            if (named(id)) ++realIds;
        if (r.gate == 8) ++want8;
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(subs); put32(withScript); put32(bad); put32(highest);
    put32(static_cast<std::int32_t>(objs.size())); put32(idOk);
    put32(static_cast<std::int32_t>(rec.size())); put32(realIds); put32(want8);
    put32(omk::globalSpellItem(glob));
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));
    std::printf("%d subscriptions, %d with a script, %d with an invalid "
                "pointer, highest message id %d; %zu objects, %d whose id is "
                "their slot; %zu recipes, %d ids naming a real object, %d "
                "gates wanting 8, GLOBAL +64 = object %d (%s)\n",
                subs, withScript, bad, highest, objs.size(), idOk, rec.size(),
                realIds, want8, omk::globalSpellItem(glob),
                named(omk::globalSpellItem(glob))
                    ? objs[static_cast<std::size_t>(omk::globalSpellItem(glob))]
                          .name.c_str()
                    : "?");
    return 0;
}

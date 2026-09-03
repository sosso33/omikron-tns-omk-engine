// SPDX-License-Identifier: GPL-3.0-or-later
// `Game_HandleEvent` 25..42 over the shipped data - the inventory channel.
//
//     dump_inventory <gamedata> <out.bin>
//
// The interface never touches an object list: it raises an event with one
// argument block and reads the answer back. So the whole of the inventory's
// behaviour is decidable from the object records and the game DB, and this
// exercises the decidable cases over the corpus rather than over one item.
//
// out.bin: int32 startCarried, startSecond, startMemos,
//          int32 carried[2], second[2],
//          int32 priced, quantityFlagged, guns,
//          int32 sellHalfExact, sellClamped, maxPrice, maxSell,
//          int32 combinableGate0, combinableGate8, deadRecipes
#include "platform/datafs.h"
#include "script/inventory.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dump_inventory <gamedata> <out.bin>\n");
        return 2;
    }
    const std::string root = argv[1];
    const omk::DataFs fs(root);
    const auto objects = omk::loadObjects(fs);
    const auto glob = fs.read("IAM/GLOBAL");
    const auto recipes = omk::globalRecipes(glob);
    const omk::Inventory inv(objects, recipes);

    auto state = omk::GameState::fromFile(root + "/IAM/START");
    const auto carried = omk::objectList(state, omk::ObjectList::Carried);
    const auto second  = omk::objectList(state, omk::ObjectList::Second);
    const auto memos   = omk::objectList(state, omk::ObjectList::Memos);

    // the records, as the cases read them
    long priced = 0, quantified = 0, guns = 0;
    long halfExact = 0, clamped = 0;
    int maxPrice = 0, maxSell = 0;
    for (const auto& o : objects) {
        if (o.price > 0) {
            ++priced;
            maxPrice = std::max(maxPrice, static_cast<int>(o.price));
            const int sv = inv.sellValue(o.index);
            maxSell = std::max(maxSell, sv);
            // SELL credits half, clamped at 0xFFFF. Exact halving is the
            // common case; the clamp is what a 16-bit purse needs.
            if (sv == o.price / 2) ++halfExact;
            if (o.price / 2 > 0xFFFF) ++clamped;
        }
        if (o.flags & 0x20) ++quantified;
        if (o.kind >= 2 && o.kind <= 6) ++guns;
    }

    // the recipes, by their gate. The engine writes 1, 0 and -1 into the gate
    // it compares against - never 8 - so every recipe wanting 8 is dead.
    long g0 = 0, g8 = 0;
    for (const auto& r : recipes) (r.gate == 8 ? g8 : g0) += 1;
    // and combine() must refuse them at the gates the engine can produce
    long dead = 0;
    for (const auto& r : recipes) {
        bool reachable = false;
        for (int gate : {1, 0, -1})
            if (inv.combine(r.a, r.b, gate) == r.product) reachable = true;
        if (!reachable) ++dead;
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(static_cast<std::int32_t>(carried.size()));
    put32(static_cast<std::int32_t>(second.size()));
    put32(static_cast<std::int32_t>(memos.size()));
    // the ids themselves, not just the counts: `IAM\START` ships carried
    // [6, 171] and second [176, 163], which is what `object lists` asserts
    for (int v : {carried.empty() ? -1 : carried[0],
                  carried.size() > 1 ? carried[1] : -1,
                  second.empty() ? -1 : second[0],
                  second.size() > 1 ? second[1] : -1}) put32(v);
    for (long v : {priced, quantified, guns, halfExact, clamped})
        put32(static_cast<std::int32_t>(v));
    put32(maxPrice); put32(maxSell);
    put32(static_cast<std::int32_t>(g0));
    put32(static_cast<std::int32_t>(g8));
    put32(static_cast<std::int32_t>(dead));
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));

    std::printf("IAM\\START's lists: carried %zu %s, second %zu, memos %zu\n",
                carried.size(),
                carried.empty() ? "" :
                  ("[" + std::to_string(carried.front()) + ".." +
                   std::to_string(carried.back()) + "]").c_str(),
                second.size(), memos.size());
    std::printf("objects: %ld priced (max %d, max sell %d), %ld halving "
                "exactly, %ld needing the 0xFFFF clamp; %ld carrying a "
                "quantity, %ld guns\n",
                priced, maxPrice, maxSell, halfExact, clamped, quantified, guns);
    std::printf("recipes: %zu, %ld gated on a value the engine writes and "
                "%ld on 8 which it never does; %ld unreachable at any gate\n",
                recipes.size(), g0, g8, dead);
    return 0;
}

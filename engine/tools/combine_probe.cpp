// SPDX-License-Identifier: GPL-3.0-or-later
// `Utiliser sur` - the COMBINE mode, and the gate that is dead from both ends.
//
//     combine_probe <gamedata> <tables>
//
// `sub_49BF30` does not use an object: it opens a mode. The chosen row goes
// into one of two slots - `sub_42B520` deciding which, by raising event 37,
// whose first arm compares the object with `u16(GLOBAL, 64)` and sets the
// recipe gate `dword_4E6C70` to 1 for it and 0 for anything else - the verb
// list is disabled, and the next row confirm supplies the second object.
// `sub_49BC60`'s `loc_49BDD6` then runs case 37's second arm, which refuses
// unless the recipe's `+6` equals the gate.
//
// The shipped table makes one arm fruitless: 11 recipes carrying gate 0 five
// times and gate 8 six times, NEVER 1. So the six gate-8 recipes can never
// fire, and a combine begun with the spell item (gate 1) matches nothing
// either - two dead arms in one table, from opposite ends.
//
// One line per fact, `key ...`:
//   gates      the histogram, and the spell item GLOBAL +64 names
//   combine    a real gate-0 recipe applied through the mode
//   refused    the same pair at gate 1 - the spell arm, which cannot fire
#include "platform/datafs.h"
#include "script/globaldata.h"
#include "script/inventory.h"
#include "script/objects.h"

#include <cstdio>
#include <map>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: combine_probe <gamedata> <tables>\n"); return 2; }
    const omk::DataFs fs(argv[1]);
    const auto blob = fs.read("IAM/GLOBAL");
    const auto recipes = omk::globalRecipes(blob);
    const auto objects = omk::loadObjects(fs);
    omk::Inventory inv(objects, recipes);

    std::map<int, int> hist;
    for (const auto& r : recipes) ++hist[r.gate];
    std::printf("gates spell_item %d recipes %zu", omk::globalSpellItem(blob),
                recipes.size());
    for (const auto& [g, n] : hist) std::printf(" gate%d=%d", g, n);
    std::printf(" gate1=%d\n", hist.count(1) ? hist[1] : 0);

    // the first gate-0 recipe, applied the way the mode applies it
    const omk::Recipe* r0 = nullptr;
    for (const auto& r : recipes) if (r.gate == 0) { r0 = &r; break; }
    if (!r0) { std::printf("combine NO GATE-0 RECIPE\n"); return 1; }
    std::printf("combine %d + %d gate 0 -> %d (want %d)\n", r0->a, r0->b,
                inv.combine(r0->a, r0->b, 0), r0->product);
    // ...and the same pair through the SPELL arm, where the gate is 1
    std::printf("refused %d + %d gate 1 -> %d (a combine begun with object "
                "%d can never fire)\n", r0->a, r0->b,
                inv.combine(r0->a, r0->b, 1), omk::globalSpellItem(blob));
    return 0;
}

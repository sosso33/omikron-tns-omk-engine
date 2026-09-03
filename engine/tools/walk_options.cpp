// SPDX-License-Identifier: GPL-3.0-or-later
// Screen 35's page tree, walked - the differential against `sim: options`.
//
//     walk_options <tables/ui_widgets.json> <tables/ui.json> <out.bin>
//
// The root page's four selectable rows, the page each opens and that page's
// first row; then one choice row cycled RIGHT past its wrap and LEFT back
// over it. The wrap is the part worth driving: it is what says the choice
// index is being stepped modulo the list rather than clamped.
#include "platform/datafs.h"
#include "ui/options.h"

#include <cstdio>
#include <tuple>
#include <fstream>
#include <string>
#include <vector>

namespace {
void put32(std::vector<std::uint8_t>& o, std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
}
void putStr(std::vector<std::uint8_t>& o, const std::string& s) {
    put32(o, static_cast<std::int32_t>(s.size()));
    for (char c : s) o.push_back(static_cast<std::uint8_t>(c));
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: walk_options <ui_widgets.json> "
                             "<ui.json> <out.bin>\n");
        return 2;
    }
    const auto t = omk::OptionTree::loadJson(argv[1], argv[2]);
    std::vector<std::uint8_t> o;

    // the root page's selectable rows, and where each goes
    omk::OptionsWalk w(t);
    w.open(1);
    std::vector<std::string> rootLabels;
    std::vector<std::tuple<int, std::string, int>> entered;
    {
        omk::OptionsWalk probe(t);
        probe.open(1);
        const int firstRow = probe.currentRow();
        int row = firstRow;
        do {
            rootLabels.push_back(probe.label());
            omk::OptionsWalk in(t);
            in.open(1);
            while (in.currentRow() != row) in.press(omk::kUiDown);
            in.press(omk::kUiConfirm);
            const int page = in.currentPage();
            // the reference reads `rows()[0]` - the first BOUND row, which
            // may be an unselectable caption - not the first selectable one
            std::string firstLabel;
            if (const auto* pg = t.page(in.currentPage()))
                if (!pg->bind.empty()) {
                    const int item = pg->bind.begin()->second.first;
                    const auto r = t.rows.find(item);
                    if (r != t.rows.end()) firstLabel = r->second.label;
                }
            // and the page `Retour` comes back to
            omk::OptionsWalk back(t);
            back.open(page);
            int backTo = -1;
            for (int k = 0; k < 20; ++k) {
                if (back.label() == "Retour") { back.press(omk::kUiConfirm);
                                                backTo = back.currentPage(); break; }
                back.press(omk::kUiDown);
            }
            entered.push_back({page, firstLabel, backTo});
            probe.press(omk::kUiDown);
            row = probe.currentRow();
        } while (row != firstRow);
    }

    // the clipping row: RIGHT past the wrap, then LEFT back over it
    std::vector<std::pair<std::string, int>> cycled;
    std::string clipLabel;
    {
        omk::OptionsWalk c(t);
        c.open(2);
        for (int k = 0; k < 20 && c.label() != "Distance de clipping"; ++k)
            c.press(omk::kUiDown);
        clipLabel = c.label();
        std::string cap; int v = 0;
        for (int k = 0; k < 6; ++k) {
            c.press(omk::kUiRight);
            if (c.value(cap, v)) cycled.push_back({cap, v});
        }
        for (int k = 0; k < 2; ++k) {
            c.press(omk::kUiLeft);
            if (c.value(cap, v)) cycled.push_back({cap, v});
        }
    }

    put32(o, static_cast<std::int32_t>(rootLabels.size()));
    for (const auto& l : rootLabels) putStr(o, l);
    put32(o, static_cast<std::int32_t>(entered.size()));
    for (const auto& [pg, lbl, back] : entered) {
        put32(o, pg); putStr(o, lbl); put32(o, back);
    }
    putStr(o, clipLabel);
    put32(o, static_cast<std::int32_t>(cycled.size()));
    for (const auto& [cap, v] : cycled) { putStr(o, cap); put32(o, v); }
    put32(o, w.approximate() ? 1 : 0);
    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));

    std::printf("root page rows:");
    for (const auto& l : rootLabels) std::printf(" %s", l.c_str());
    std::printf("\n");
    for (const auto& [pg, lbl, back] : entered)
        std::printf("    -> page %d, first row \"%s\", Retour goes to page %d\n",
                    pg, lbl.c_str(), back);
    std::printf("\"%s\" cycled:", clipLabel.c_str());
    for (const auto& [cap, v] : cycled) std::printf(" %s/%d", cap.c_str(), v);
    std::printf("\n");
    return 0;
}

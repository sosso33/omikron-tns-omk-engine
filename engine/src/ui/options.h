// SPDX-License-Identifier: GPL-3.0-or-later
// Screen 35 - the options tree, which is not built like the other screens.
//
// Its "panel" is one of THIRTEEN page records, and every page fills the SAME
// sixteen row widgets. So what a page shows is not in the row: it is in the
// calls its builder makes to `Opt_BindRow(row, item, page)`, which
// `tools/exetables.py` recovers from the builder's bytes into
// `tables/ui_widgets.json`.
//
// **That recovery has the hazard this port already paid for once.** A builder
// has branches, so a row bound twice with different items was seen on two arms
// and a linear scan cannot tell which one runs. Twelve rows are like that -
// `tools/sim/ui.py` documents exactly one of them, `Opt_PageRoot`'s row 4,
// bound to "Retour" only when the screen parameter is 1 - and a walker must
// treat all twelve as unknown rather than take the last binding. Reaching one
// marks the walk approximate.
//
// The value rules are `sub_492DA0`, the live page's own input hook:
//
//     type 0 / 4  (choice, device)  LEFT steps back, RIGHT and CONFIRM
//                                   forward, both wrapping
//     type 1      (slider)          LEFT -10 (floor 0), RIGHT +10
//     type 2      (header)          CONFIRM enters its page
//     type 5      (defaults)        CONFIRM restores from a compiled table
//     type 6      (back)            CONFIRM at the root returns to screen 29
#pragma once

#include "ui/widgets.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace omk {

// One row of the 74-row option table, out of tables/ui.json.
struct OptionRow {
    int         index = 0;
    std::string label;
    std::string kind;                 // "choice" | "slider" | "header" | ...
    std::vector<std::pair<std::string, int>> choices;   // caption, value
};

// {page -> {row -> (option index, page behind it)}}, plus the branch rows.
struct OptionTree {
    struct Page {
        int page = 0;
        std::map<int, std::pair<int, int>> bind;
        std::set<int> branch;
    };
    std::vector<Page> pages;
    std::map<int, OptionRow> rows;    // by option index
    int rowWidgets = 16;

    static OptionTree loadJson(const std::string& widgets,
                               const std::string& uiTable);
    const Page* page(int p) const;
};

class OptionsWalk {
public:
    explicit OptionsWalk(const OptionTree& t) : t_(&t) {}

    bool open(int page = 1);
    bool press(std::uint32_t bits);

    int  currentPage() const { return page_; }
    int  currentRow() const { return sel_; }
    int  selectedOption() const;
    std::string label() const;
    // The chosen caption and value of the selected row, when it has any.
    bool value(std::string& caption, int& v) const;
    bool approximate() const { return approx_; }
    const std::vector<std::string>& log() const { return log_; }

    std::vector<int> selectableRows() const;

private:
    bool selectable(int row) const;
    void first();
    // Enter page `k`, following page 0's TRAMPOLINE.
    //
    // Page 0 has no rows. Its builder (0x00492A70) is
    // `if (dword_9103C8) word_4DD3B2 = 1; else Ui_GoToPanel(screen, page 1)`,
    // so it bounces straight to the root - unless a setting has been changed,
    // `dword_9103C8` being the DIRTY LATCH, in which case it raises a prompt
    // that is not modelled. **Every sub-page's "Retour" binds page 0, not
    // page 1**, so without this the walk lands on an empty page and looks
    // like a disagreement.
    void gotoPage(int k);

    const OptionTree* t_;
    int  page_ = -1, sel_ = 0;
    std::map<int, int> chosen_;       // option index -> chosen choice
    bool approx_ = false;
    std::vector<std::string> log_;
};

}  // namespace omk

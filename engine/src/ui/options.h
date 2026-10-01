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

#include "script/savefile.h"
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
    int         type = 0;             // +0: 0 choice 1 slider 2 header 3 keybind 4 device 5 defaults 6 back
    int         labelId = -1;         // +24, an index into `IAM\Options`
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

// ======================================================================
// SCREEN 35 LIVE - the menu a player drives (`todo/options-menu.md`).
//
// `OptionsWalk` above is the MODEL the simulator is checked against; this is
// the screen itself: the 74 rows' current values, read from and applied to a
// `SettingsBlock` through the rows' own hooks, page 0's save prompt, and the
// state each of the sixteen row widgets is left in by `Opt_BindRow` - which
// is what the draw hook 0x00493380 reads.
//
// What it does NOT do, labelled: the keybinding rows (type 3) read no key
// and the defaults row (type 5) restores nothing - their value column needs
// the key-name table at 0x004D10CC, which is not lifted - and the panels do
// not SLIDE in and out (`Ui_SlidePanelFrom`), they appear.
class OptionsMenu {
public:
    explicit OptionsMenu(const OptionTree& t);

    // `IAM\Options`, the screen's own strings: the row labels by `+24`, and
    // the prompt's 71 / 61 / 60 and Accel 3D's 68.
    void setText(std::vector<std::string> t) { text_ = std::move(t); }
    const std::string& str(int id) const;

    // Screen 35's OPEN callback (0x00490D50). `param` is 0 when the SNEAK
    // hosts it and 1 when the START MENU does. It sets bit 2 of every row's
    // `+132`, so from here on a change APPLIES at once.
    void open(int param, const SettingsBlock& s);
    bool isOpen() const { return open_; }
    int  param() const { return param_; }
    // `UI_FocusScreen(35)` - the start menu's Options panel enter hook
    // (0x0047BB40). The page in place stays; a first focus builds the root.
    void focus();
    bool focused() const { return focused_; }
    // ...and back: page 1's hook on BACK (0x00492AD0) and the root's Retour
    // (`Opt_RowInput` case 6) both `UI_FocusScreen(29)`.
    void unfocus() { focused_ = false; }

    // One frame of `Ui_DispatchInput` on the focused screen: the page 1 panel
    // hook, then `Opt_RowInput` / the prompt's list. -> consumed.
    bool press(std::uint32_t bits);
    // Did the last press MOVE the selection (the move sound), and was it a
    // confirm or back that did something (theirs)?
    bool moved() const { return moved_; }

    // THE TWO DEVICE ROWS (type 4): row 2's display modes and row 8's
    // drivers. The engine enumerates them itself (`sub_43AE70`,
    // `sub_43A3F0`); a port has to be told. `current` is what is running.
    void setDevices(int item, std::vector<std::string> names, int current);
    int  current(int item) const;
    int  choices(int item) const;
    int  sliderValue(int item) const;     // +92[current] of a slider row

    // What the menu has written. Every apply hook writes this block - the
    // global `byte_90E180` - and nothing else; the caller reads it.
    const SettingsBlock& settings() const { return s_; }
    // Items whose APPLY hook ran since the last call, in order.
    std::vector<int> takeApplied() { auto a = std::move(applied_); applied_.clear(); return a; }
    // `Oui` on the save prompt - `sub_4092A0`, the settings-only save.
    bool takeSaveRequest() { const bool r = saveReq_; saveReq_ = false; return r; }
    // `dword_9103C8`.
    bool dirty() const { return dirty_; }
    // Under the SNEAK, page 1's BACK or CLOSE is `UI_CloseScreen(9)` - the
    // host closes, and the caller has to do it.
    bool takeHostClose() { const bool r = hostClose_; hostClose_ = false; return r; }

    // ---- what the draw hook reads -------------------------------------
    //
    // One entry per row widget the page bound (empty rows are left out, as
    // `Opt_LayOutPage` leaves them unplaced), or, on page 0, the prompt's
    // three items.
    struct Widget {
        int  row = -1;            // which of the sixteen (0..15), or the prompt item
        int  item = -1;           // option index, -1 on the prompt
        int  type = -1;           // the option's +0; -1 on the prompt (plain text)
        std::string label;        // +24 resolved, or the prompt item's string
        std::string value;        // the choice / device caption
        int  slider = -1;         // 0..100, a slider row
        int  x = 0, y = 0, w = 500, h = 20;   // list-relative x (item 0), absolute y
        char font = 'J';          // +36: 'S' (83) or 'J' (74)
        bool white = true;        // bank C 0x80000001
        bool alwaysLit = false;   // bank B 0x40000008
        int  align = 4;           // Text_DrawBlock style: 2 left, 4 right, 8 centred
        bool selectable = true;   // NOT bank A 0x20000004
        bool selected = false;
    };
    std::vector<Widget> widgets() const;
    int  page() const { return page_; }
    int  listX() const;           // the list's x, set by the open callback
    // The list colour, `sub_4296D0` in the open callback: (255, 100, 70) for
    // the rows, white for the start menu's prompt.
    void listColour(std::uint8_t rgb[3]) const;

private:
    struct RowState { int current = 0; std::vector<int> value; int flags = 0; };
    int  rowAt(int widget) const;              // the item bound to a row widget, or -1
    int  rowPage(int widget) const;            // the page behind it (+44), or -1
    bool selectableRow(int widget) const;
    void build(int page);                      // the page builder
    void gotoPage(int page);
    void readBack(int item);                   // the row's +16 hook
    void apply(int item);                      // the row's +12 hook
    void changed(int item);                    // Opt_RowInput's tail
    bool moveVertical(int step);

    const OptionTree* t_;
    std::vector<std::string> text_;
    std::vector<RowState> rows_;               // 74
    std::map<int, std::vector<std::string>> devices_;
    SettingsBlock s_{};
    std::map<int, int> pageSel_;               // each page's +80
    std::vector<int> applied_;
    int  param_ = 1, page_ = 1, sel_ = 0;
    bool open_ = false, focused_ = false, built_ = false;
    bool dirty_ = false, saveReq_ = false, moved_ = false, hostClose_ = false;
};

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
// OMK'S OWN OPTIONS PAGES - "Graphismes (OMK)" on screen 35's root
// (`todo/graphics-menu.md` A2).
//
// Not the game's: `OptionsMenu` (`ui/options.h`) is the exe's screen 35, its
// 74 rows and 13 pages out of the tables, and stays that. This is THREE PAGES
// of this port's - numbered 100 up, past any page record - holding the
// `[Enhancements]` and `[Remaster]` keys and the language of these rows, which
// `OptionsMenu` hands its input and asks for widgets when one of them is up.
// They draw through the same row hook as the game's (`drawOptions`), with the
// same four row types: a choice (0), a header (2), the defaults (5), Retour (6).
//
//     100  Graphismes (OMK)   Améliorations > · Remaster > · Langue · Retour
//     101  Améliorations      13 [Enhancements] rows · Valeurs par défaut · Retour
//     102  Remaster           [Remaster] mode and 11 strengths · défaut · Retour
//
// What a row offers is what can run: a page or a row the BUILD lacks is not
// there (no Remaster page without OMK_REMASTER), and a row - or one choice of
// it - the running RENDERER cannot draw is left out (`Renderer::takes`).
//
// A change is WRITTEN to the ini (`takeEdits`, the caller's `writeOptionsFile`)
// and, where the running game can take it, APPLIED at once (`takeChanged`,
// `settle`); the others carry a `*` and the page's heading says they apply at
// the next start. A row a
// command-line flag holds says so and cannot move - the flag is the most
// recent word, as for the game's rows. "Valeurs par défaut" UNSETS the page's
// keys (commented out in the file), so each falls back to what the reader
// gives without it - the engine's default, or the remaster preset's.
#pragma once

#include "platform/options.h"
#include "ui/omktext.h"
#include "ui/options.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace omk {

class OmkOptions {
public:
    static constexpr int kPageGraphics = 100, kPageEnhancements = 101, kPageRemaster = 102;

    explicit OmkOptions(bool remasterBuilt);
    void setText(const OmkText* t) { text_ = t; }
    // `Renderer::takes()` of the renderer running (`kTakes*`)
    void setTakes(unsigned mask) { takes_ = mask; }

    // A row's state, by `section.key` ("enhancements.antialiasing",
    // "remaster.bloom", "omk.language"): the value it RUNS at, the value an
    // unset key gives (what the reset returns to), and the flag holding it.
    // `setValue` also makes it the row's starting value - the one a later
    // change is pending against.
    void setValue(const std::string& key, double v);
    // ...and a row whose FILE says other than what runs (a change waiting for
    // the next start, the menu reopened): `running` is its start, `configured`
    // its current value - shown with its `*`
    void setValue(const std::string& key, double running, double configured);
    void setDefault(const std::string& key, double v);
    void setHeld(const std::string& key, const std::string& flag);
    double value(const std::string& key) const;
    // the ini value string of the row's current choice ("trilinear", "1.6")
    std::string iniValue(const std::string& key) const;

    static bool isPage(int p) { return p >= kPageGraphics && p <= kPageRemaster; }
    // where BACK goes: the graphics page to screen 35's root, the two others
    // to the graphics page - never through page 0's prompt, which is about
    // the game's rows (OMK's are written as they change)
    static int parent(int page) { return page == kPageGraphics ? 1 : kPageGraphics; }

    bool selectable(int page, int w) const;
    // how many widgets the page has (`widgets(page).size()`)
    int count(int page) const { return static_cast<int>(shownRows(page).size()); }
    // the page's widgets, unplaced (`OptionsMenu` lays them out)
    std::vector<OptionsMenu::Widget> widgets(int page, int sel) const;
    // LEFT / RIGHT / CONFIRM on widget `w` of `page` (UP, DOWN and BACK are
    // the menu's). -> consumed; `gotoPage` the page a header or Retour opens.
    bool press(int page, int w, std::uint32_t bits, int& gotoPage);

    // what changed since the last call: the ini edits, and the keys whose
    // value moved - the caller tries each on the running game
    std::vector<OptionsEdit> takeEdits();
    std::vector<std::string> takeChanged();
    // ...and says which it APPLIED: the row's value is then what runs, and it
    // loses its `*`. One not settled waits for the next start. Whether a
    // change can apply is the running game's to say, not the row's - it can
    // depend on the value (mapped shadows only where the shadow map was
    // built) and on what the start loaded (`todo/graphics-menu.md` 3a).
    void settle(const std::string& key);
    // set `key` to the choice the file would write as `ini` (a number may be
    // one the list lacks), as LEFT / RIGHT would - the `--omk-set` harness.
    // -> false for a row there is not, or one a flag holds.
    bool choose(const std::string& key, const std::string& ini);

    // a text in the chosen language (the root's header, "page.graphics")
    std::string text(const std::string& id) const { return tx(id); }

    // every text id the rows can ask for - a check holds them against the file
    std::vector<std::string> textIds() const;

private:
    struct Choice {
        std::string ini;          // what the file says
        double v = 0.0;           // the number the row runs at
        std::string caption;      // a text id, or "" for the number itself
        unsigned needs = 0;       // `kTakes*` this choice wants
    };
    struct Row {
        int type = 0;             // 0 choice, 2 header / caption, 5 defaults, 6 Retour
        std::string label;        // text id
        std::string section, key; // the ini's
        int page = -1;            // a header's page
        std::vector<Choice> choices;
        unsigned needs = 0;       // `kTakes*` the row wants
        bool remaster = false;    // only with OMK_REMASTER
        // state
        int current = 0, start = 0, def = 0;
        std::string held;
    };
    std::vector<Row> rows_;                    // all three pages
    std::map<int, std::vector<int>> pages_;    // page -> rows_ indices, in order
    const OmkText* text_ = nullptr;
    unsigned takes_ = 0;
    bool remasterBuilt_ = false;
    std::vector<OptionsEdit> edits_;
    std::vector<std::string> changed_;

    Row* find(const std::string& key);
    const Row* find(const std::string& key) const;
    bool shown(const Row& r) const;
    bool shown(const Row& r, const Choice& c) const;
    std::vector<int> shownRows(int page) const;   // rows_ indices, one per widget
    int  nearest(const Row& r, double v) const;   // the choice nearest v, adding none
    int  step(const Row& r, int from, int dir) const;   // next SHOWN choice
    std::string caption(const Row& r, const Choice& c) const;
    std::string tx(const std::string& id) const;
    void change(Row& r, int to);
};

}  // namespace omk

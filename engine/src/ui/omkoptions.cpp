// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/omkoptions.h"

#include "o3de/renderer.h"
#include "ui/widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace omk {

namespace {

// a number as the file writes it: "%g" - 1.6, 0.75, 15
std::string iniNumber(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

// the sections as the template spells them; a row's id is `section.key` in
// lower case ("enhancements.antialiasing")
constexpr const char* kEnh = "Enhancements";
constexpr const char* kRem = "Remaster";
constexpr const char* kOmk = "OMK";
constexpr const char* kRestored = "Restored";   // what the 1999 game planned and never shipped

std::string lowerId(const std::string& section, const std::string& key) {
    std::string s = section + "." + key;
    for (auto& c : s) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return s;
}

}  // namespace

OmkOptions::OmkOptions(bool remasterBuilt) : remasterBuilt_(remasterBuilt) {
    // the rows, page by page. A choice's caption is a text id, else
    // `<key>.<ini>` when the file has one, else the number itself.
    const auto caption = [](const char* id) {   // a page's heading: a header with no page
        Row r;
        r.type = 2;
        r.label = id;
        return r;
    };
    const auto header = [](const char* id, int page, bool remaster) {
        Row r;
        r.type = 2;
        r.label = id;
        r.page = page;
        r.remaster = remaster;
        return r;
    };
    const auto choice = [](const char* section, const char* key, std::vector<Choice> cs, unsigned needs,
                           bool remaster) {
        Row r;
        r.type = 0;
        r.label = std::string("row.") + key;
        r.section = section;
        r.key = key;
        r.choices = std::move(cs);
        r.needs = needs;
        r.remaster = remaster;
        return r;
    };
    // numbers, their captions the numbers themselves (0 is "Non")
    const auto numbers = [](std::initializer_list<double> vs, double off) {
        std::vector<Choice> cs;
        for (const double v : vs) cs.push_back({iniNumber(v), v, v == off ? "choice.off" : "", 0});
        return cs;
    };
    const auto twoState = [](const char* offIni, const char* onIni) {
        return std::vector<Choice>{{offIni, 0, "choice.off", 0}, {onIni, 1, "choice.on", 0}};
    };
    const auto add = [&](int page, Row r) {
        pages_[page].push_back(static_cast<int>(rows_.size()));
        rows_.push_back(std::move(r));
    };
    const auto tail = [&](int page) {
        if (page != kPageGraphics) {
            Row d;
            d.type = 5;
            d.label = "row.reset";
            add(page, d);
        }
        Row b;
        b.type = 6;
        b.label = "page.back";
        add(page, b);
    };

    // ---- 100, Graphismes (OMK)
    add(kPageGraphics, caption("page.graphics"));
    add(kPageGraphics, header("page.enhancements", kPageEnhancements, false));
    add(kPageGraphics, header("page.remaster", kPageRemaster, true));
    add(kPageGraphics, choice(kOmk, "language", {{"fr", 0, "language.fr", 0}, {"en", 1, "language.en", 0}},
                              0, false));
    // `[Restored] weather` (todo/restore-weather.md): neither an enhancement
    // nor the remaster's - its own section, on this first page
    add(kPageGraphics, choice(kRestored, "weather", twoState("0", "1"), 0, false));
    tail(kPageGraphics);

    // ---- 101, Améliorations: `[Enhancements]`, in `todo/enhancements.md`'s order
    add(kPageEnhancements, caption("page.enhancements"));
    add(kPageEnhancements, choice(kEnh, "all", twoState("off", "max"), 0, false));
    add(kPageEnhancements, choice(kEnh, "antialiasing", numbers({0, 2, 4, 8}, 0), kTakesMultisample, false));
    add(kPageEnhancements, choice(kEnh, "supersampling", numbers({1, 2, 3, 4}, 1), kTakesSupersample, false));
    add(kPageEnhancements, choice(kEnh, "texturefiltering",
                                  {{"nearest", 0, "", 0}, {"bilinear", 1, "", 0},
                                   {"trilinear", 2, "", kTakesTrilinear}},
                                  kTakesTextureFilter, false));
    add(kPageEnhancements, choice(kEnh, "anisotropy", numbers({1, 2, 4, 8, 16}, 1), kTakesAnisotropy, false));
    add(kPageEnhancements, choice(kEnh, "shadowquality",
                                  {{"classic", 0, "", 0}, {"fitted", 1, "", 0}, {"mapped", 2, "", kTakesShadowMap}},
                                  0, false));
    add(kPageEnhancements, choice(kEnh, "lighting",
                                  {{"pervertex", 0, "", 0}, {"perpixel", 1, "", kTakesPixelLights}}, 0, false));
    add(kPageEnhancements, choice(kEnh, "uiscaling", {{"nearest", 0, "", 0}, {"linear", 1, "", 0}}, 0, false));
    add(kPageEnhancements, choice(kEnh, "textscaling", {{"game", 0, "", 0}, {"fit", 1, "", 0}}, 0, false));
    add(kPageEnhancements, choice(kEnh, "radar", {{"game", 0, "", 0}, {"always", 1, "", 0}}, 0, false));
    add(kPageEnhancements, choice(kEnh, "framerate", numbers({30, 60, 120}, -1), 0, false));
    add(kPageEnhancements, choice(kEnh, "animation", {{"game", 0, "", 0}, {"smooth", 1, "", 0}}, 0, false));
    add(kPageEnhancements, choice(kEnh, "clipdistance", twoState("capped", "unlimited"), 0, false));
    tail(kPageEnhancements);

    // ---- 102, Remaster: the switch, then each strength (`settings.h`'s ranges)
    add(kPageRemaster, caption("page.remaster"));
    add(kPageRemaster, choice(kRem, "mode", {{"original", 0, "mode.original", 0}, {"remaster", 1, "mode.remaster", 0}},
                              0, true));
    add(kPageRemaster, choice(kRem, "bloom", numbers({0, 0.5, 1, 1.6, 2.5, 4}, 0), kTakesBloom, true));
    add(kPageRemaster, choice(kRem, "water", numbers({0, 0.3, 0.6, 1}, 0), kTakesWater, true));
    add(kPageRemaster, choice(kRem, "floors", numbers({0, 0.5, 1}, 0), kTakesWater, true));
    // how many floor levels mirror a frame: captions `reflections.N`
    add(kPageRemaster, choice(kRem, "reflections", {{"0", 0, "", 0}, {"1", 1, "", 0}, {"2", 2, "", 0}},
                              kTakesWater, true));
    add(kPageRemaster, choice(kRem, "wet", numbers({0, 0.5, 1}, 0), kTakesWater, true));
    add(kPageRemaster, choice(kRem, "rain3d", numbers({0, 0.5, 1}, 0), kTakesWater, true));
    add(kPageRemaster, choice(kRem, "mist", numbers({0, 0.25, 0.5, 1}, 0), kTakesMist, true));
    add(kPageRemaster, choice(kRem, "relief", numbers({0, 1, 2, 4}, 0), kTakesRelief, true));
    add(kPageRemaster, choice(kRem, "ao", numbers({0, 1, 2, 4}, 0), kTakesAmbientOcclusion, true));
    add(kPageRemaster, choice(kRem, "bodylight", numbers({0, 0.5, 1}, 0), kTakesBodyTint, true));
    add(kPageRemaster, choice(kRem, "fxlight", numbers({0, 1, 2, 4}, 0), kTakesEmitterLights, true));
    add(kPageRemaster, choice(kRem, "smooth", numbers({0, 0.5, 0.75, 1}, 0), 0, true));
    add(kPageRemaster, choice(kRem, "upscale", numbers({1, 1.5, 2, 3}, 1), kTakesUpscale, true));
    add(kPageRemaster, choice(kRem, "preload", numbers({0, 15, 25, 50}, 0), 0, true));
    tail(kPageRemaster);
    for (auto& r : rows_) r.remaster = r.remaster || r.section == kRem;
}

// ---- state -------------------------------------------------------------------

OmkOptions::Row* OmkOptions::find(const std::string& key) {
    for (auto& r : rows_)
        if (!r.key.empty() && lowerId(r.section, r.key) == key) return &r;
    return nullptr;
}

const OmkOptions::Row* OmkOptions::find(const std::string& key) const {
    return const_cast<OmkOptions*>(this)->find(key);
}

namespace {
// the choice whose number is `v`, a value the list lacks ADDED in its order
// (a hand-written `bloom = 1.3` is shown as 1.3, not rounded to a neighbour)
template <class Choices>
int choiceFor(Choices& cs, double v) {
    for (std::size_t i = 0; i < cs.size(); ++i)
        if (std::fabs(cs[i].v - v) < 1e-4) return static_cast<int>(i);
    std::size_t at = 0;
    while (at < cs.size() && cs[at].v < v) ++at;
    typename Choices::value_type c;
    c.ini = iniNumber(v);
    c.v = v;
    cs.insert(cs.begin() + static_cast<std::ptrdiff_t>(at), c);
    return static_cast<int>(at);
}
}  // namespace

void OmkOptions::setValue(const std::string& key, double v) {
    Row* r = find(key);
    if (!r) return;
    // a value inserted ahead of the others moves their indices
    const double def = r->choices.empty() ? 0.0 : r->choices[static_cast<std::size_t>(r->def)].v;
    r->current = r->start = choiceFor(r->choices, v);
    r->def = choiceFor(r->choices, def);
}

void OmkOptions::setValue(const std::string& key, double running, double configured) {
    setValue(key, running);
    Row* r = find(key);
    if (!r) return;
    const double st = r->choices[static_cast<std::size_t>(r->start)].v;
    const double def = r->choices[static_cast<std::size_t>(r->def)].v;
    r->current = choiceFor(r->choices, configured);
    r->start = choiceFor(r->choices, st);
    r->def = choiceFor(r->choices, def);
}

void OmkOptions::setDefault(const std::string& key, double v) {
    Row* r = find(key);
    if (!r) return;
    const double cur = r->choices[static_cast<std::size_t>(r->current)].v;
    const double st = r->choices[static_cast<std::size_t>(r->start)].v;
    r->def = choiceFor(r->choices, v);
    r->current = choiceFor(r->choices, cur);
    r->start = choiceFor(r->choices, st);
}

void OmkOptions::setHeld(const std::string& key, const std::string& flag) {
    if (Row* r = find(key)) r->held = flag;
}

double OmkOptions::value(const std::string& key) const {
    const Row* r = find(key);
    return r ? r->choices[static_cast<std::size_t>(r->current)].v : 0.0;
}

std::string OmkOptions::iniValue(const std::string& key) const {
    const Row* r = find(key);
    return r ? r->choices[static_cast<std::size_t>(r->current)].ini : std::string();
}

void OmkOptions::settle(const std::string& key) {
    if (Row* r = find(key)) r->start = r->current;
}

// ---- what is offered ---------------------------------------------------------

bool OmkOptions::shown(const Row& r) const {
    if (r.remaster && !remasterBuilt_) return false;
    if ((r.needs & takes_) != r.needs) return false;
    if (r.type != 0) return true;
    // a choice row needs two choices left to choose between
    int n = 0;
    for (const auto& c : r.choices) n += shown(r, c) ? 1 : 0;
    return n >= 2;
}

bool OmkOptions::shown(const Row& r, const Choice& c) const {
    // the value RUNNING is always shown, whatever the renderer - it is the
    // truth; a configured one the renderer cannot draw is not (the caller
    // reduces it, as the start does - `omkOptionsOpen`)
    return (c.needs & takes_) == c.needs || &c == &r.choices[static_cast<std::size_t>(r.start)];
}

std::vector<int> OmkOptions::shownRows(int page) const {
    std::vector<int> out;
    const auto p = pages_.find(page);
    if (p == pages_.end()) return out;
    for (const int i : p->second)
        if (shown(rows_[static_cast<std::size_t>(i)])) out.push_back(i);
    return out;
}

bool OmkOptions::selectable(int page, int w) const {
    const auto rows = shownRows(page);
    if (w < 0 || w >= static_cast<int>(rows.size())) return false;
    const Row& r = rows_[static_cast<std::size_t>(rows[static_cast<std::size_t>(w)])];
    if (r.type == 2) return r.page >= 0;       // a caption is not
    // a row a flag holds IS reached - it reads "fixé par --flag" - but its
    // value cannot move (`press`); skipping it left a page under
    // `--enhance-all` with one row to stand on (the reader, 2026-11-02)
    return true;
}

std::string OmkOptions::tx(const std::string& id) const {
    return text_ ? (*text_)(id) : id;
}

std::string OmkOptions::caption(const Row& r, const Choice& c) const {
    if (!c.caption.empty()) return tx(c.caption);
    const std::string own = r.key + "." + c.ini;
    if (text_ && text_->has(own)) return tx(own);
    // a unit the file gives (`preload.metres` = "%d m"), else the number with
    // the language's decimal separator
    const std::string unit = r.key + ".metres";
    if (text_ && text_->has(unit)) {
        std::string f = tx(unit);
        const auto at = f.find("%d");
        if (at != std::string::npos) f.replace(at, 2, std::to_string(static_cast<int>(std::lround(c.v))));
        return f;
    }
    std::string n = c.ini;
    const std::string dec = tx("decimal");
    for (std::size_t i = 0; i < n.size(); ++i)
        if (n[i] == '.') { n.replace(i, 1, dec); i += dec.size() - 1; }
    return n;
}

std::vector<OptionsMenu::Widget> OmkOptions::widgets(int page, int sel) const {
    std::vector<OptionsMenu::Widget> out;
    const auto rows = shownRows(page);
    // a change that waits for the next start shows a `*`, and the heading says so
    bool pending = false;
    for (const int i : rows) {
        const Row& r = rows_[static_cast<std::size_t>(i)];
        if (r.type == 0 && r.current != r.start) pending = true;
    }
    for (std::size_t k = 0; k < rows.size(); ++k) {
        const Row& r = rows_[static_cast<std::size_t>(rows[k])];
        OptionsMenu::Widget w;
        w.row = static_cast<int>(k);
        w.item = 1000 + rows[k];          // past the game's 74 - a log tells them apart
        w.type = r.type;
        w.label = tx(r.label);
        const bool captionRow = r.type == 2 && r.page < 0;
        if (captionRow && pending) w.label += " - * " + tx("msg.nextstart");
        // `Opt_BindRow`'s look: headings and Retour in SNEAK, centred; the
        // rest in JOURNAL, the label right-aligned before its value
        w.font = (r.type == 2 || r.type == 6) ? 'S' : 'J';
        w.align = (r.type == 2 || r.type == 5 || r.type == 6) ? 8 : 4;
        w.white = !captionRow;
        w.alwaysLit = captionRow;
        w.selectable = selectable(page, static_cast<int>(k));
        w.selected = static_cast<int>(k) == sel;
        if (r.type == 0) {
            w.value = caption(r, r.choices[static_cast<std::size_t>(r.current)]);
            if (r.current != r.start) w.value += " *";
            if (!r.held.empty()) {
                std::string h = tx("msg.heldby");
                const auto at = h.find("%s");
                if (at != std::string::npos) h.replace(at, 2, r.held);
                w.value += " " + h;   // short: it must fit enlarged text
            }
        }
        out.push_back(std::move(w));
    }
    return out;
}

// ---- input -------------------------------------------------------------------

int OmkOptions::step(const Row& r, int from, int dir) const {
    const int n = static_cast<int>(r.choices.size());
    int j = from;
    for (int t = 0; t < n; ++t) {
        j = ((j + dir) % n + n) % n;
        if ((r.choices[static_cast<std::size_t>(j)].needs & takes_) == r.choices[static_cast<std::size_t>(j)].needs)
            return j;
    }
    return from;
}

void OmkOptions::change(Row& r, int to) {
    if (to == r.current) return;
    r.current = to;
    edits_.push_back({r.section, r.key, r.choices[static_cast<std::size_t>(to)].ini});
    changed_.push_back(lowerId(r.section, r.key));
}

bool OmkOptions::press(int page, int w, std::uint32_t bits, int& gotoPage) {
    gotoPage = -1;
    const auto rows = shownRows(page);
    if (w < 0 || w >= static_cast<int>(rows.size())) return false;
    Row& r = rows_[static_cast<std::size_t>(rows[static_cast<std::size_t>(w)])];
    switch (r.type) {
    case 0:
        // `sub_492DA0`'s rule for a choice: LEFT back, RIGHT and CONFIRM on, wrapping
        if (!r.held.empty()) return false;
        if (bits & kUiLeft)                      change(r, step(r, r.current, -1));
        else if (bits & (kUiRight | kUiConfirm)) change(r, step(r, r.current, +1));
        else return false;
        return true;
    case 2:
        if (!(bits & kUiConfirm) || r.page < 0) return false;
        gotoPage = r.page;
        return true;
    case 5: {
        if (!(bits & kUiConfirm)) return false;
        // VALEURS PAR DÉFAUT: every key of the page unset - commented out in the
        // file - and shown at what the reader gives without it; a row a flag
        // holds stays as it is
        for (const int i : pages_[page]) {
            Row& o = rows_[static_cast<std::size_t>(i)];
            if (o.type != 0 || !o.held.empty()) continue;
            edits_.push_back({o.section, o.key, "", true});
            // a row not offered here (this renderer does not draw it) is unset
            // in the file all the same, and not applied - it would be refused
            if (o.current != o.def && shown(o)) {
                o.current = o.def;
                changed_.push_back(lowerId(o.section, o.key));
            }
        }
        return true;
    }
    case 6:
        if (!(bits & kUiConfirm)) return false;
        gotoPage = parent(page);
        return true;
    default:
        return false;
    }
}

bool OmkOptions::choose(const std::string& key, const std::string& ini) {
    Row* r = find(key);
    if (!r || r->type != 0 || !r->held.empty()) return false;
    for (std::size_t i = 0; i < r->choices.size(); ++i)
        if (r->choices[i].ini == ini) { change(*r, static_cast<int>(i)); return true; }
    char* end = nullptr;
    const double v = std::strtod(ini.c_str(), &end);
    if (end == ini.c_str() || *end) return false;
    // a number the list lacks: added in its order, as `setValue` adds one
    const double cur = r->choices[static_cast<std::size_t>(r->current)].v;
    const double st = r->choices[static_cast<std::size_t>(r->start)].v;
    const double def = r->choices[static_cast<std::size_t>(r->def)].v;
    const int to = choiceFor(r->choices, v);
    r->current = choiceFor(r->choices, cur);
    r->start = choiceFor(r->choices, st);
    r->def = choiceFor(r->choices, def);
    change(*r, to);
    return true;
}

std::vector<OptionsEdit> OmkOptions::takeEdits() {
    auto e = std::move(edits_);
    edits_.clear();
    return e;
}

std::vector<std::string> OmkOptions::takeChanged() {
    auto c = std::move(changed_);
    changed_.clear();
    return c;
}

std::vector<std::string> OmkOptions::textIds() const {
    std::vector<std::string> ids = {"msg.nextstart", "msg.heldby", "decimal"};
    for (const auto& r : rows_) {
        ids.push_back(r.label);
        for (const auto& c : r.choices)
            if (!c.caption.empty()) ids.push_back(c.caption);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/options.h"

#include "platform/json.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace omk {

OptionTree OptionTree::loadJson(const std::string& widgets,
                                const std::string& uiTable) {
    OptionTree t;
    // Hold both documents in locals. `Json::parseFile(p)["rows"]` returns a
    // reference INTO a temporary, and reference lifetime extension does not
    // reach through `operator[]` - binding it to a `const auto&` dangles at
    // the semicolon and reads as an empty table rather than crashing.
    const Json wdoc = Json::parseFile(widgets);
    const Json udoc = Json::parseFile(uiTable);
    const Json& w = wdoc["rows"];
    t.rowWidgets = static_cast<int>(w["optionRows"].i64(16));
    const auto& ps = w["optionPages"];
    for (std::size_t i = 0; i < ps.size(); ++i) {
        Page p;
        p.page = static_cast<int>(ps[i]["page"].i64());
        for (const auto& [row, v] : ps[i]["bind"].members())
            p.bind[std::atoi(row.c_str())] =
                {static_cast<int>(v[0].i64()), static_cast<int>(v[1].i64())};
        const auto& br = ps[i]["branch"];
        for (std::size_t k = 0; k < br.size(); ++k)
            p.branch.insert(static_cast<int>(br[k].i64()));
        t.pages.push_back(std::move(p));
    }
    const Json& os = udoc["rows"]["options"];
    for (std::size_t i = 0; i < os.size(); ++i) {
        OptionRow r;
        r.index = static_cast<int>(os[i]["index"].i64());
        r.label = os[i]["label"].str();
        r.kind  = os[i]["kind"].str();
        r.type  = static_cast<int>(os[i]["type"].i64());
        r.labelId = static_cast<int>(os[i]["label_id"].i64(-1));
        const auto& ch = os[i]["choices"];
        for (std::size_t k = 0; k < ch.size(); ++k)
            r.choices.push_back({ch[k][0].str(),
                                 static_cast<int>(ch[k][1].i64())});
        t.rows[r.index] = std::move(r);
    }
    return t;
}

const OptionTree::Page* OptionTree::page(int p) const {
    for (const auto& x : pages) if (x.page == p) return &x;
    return nullptr;
}

// ------------------------------------------------------------------ the walk

std::vector<int> OptionsWalk::selectableRows() const {
    std::vector<int> out;
    const auto* p = t_->page(page_);
    if (!p) return out;
    for (const auto& [row, b] : p->bind) { (void)b; out.push_back(row); }
    return out;
}

bool OptionsWalk::selectable(int row) const {
    // `Opt_BindRow` clears 0x20000004 for every real row and sets it for an
    // empty one and for a header with no page behind it - so a caption is
    // skipped and a submenu entry is not.
    const auto* p = t_->page(page_);
    if (!p) return false;
    const auto it = p->bind.find(row);
    if (it == p->bind.end()) return false;
    const auto [item, behind] = it->second;
    if (item < 0) return false;
    const auto r = t_->rows.find(item);
    if (r == t_->rows.end()) return false;
    return r->second.kind == "header" ? behind >= 0 : true;
}

void OptionsWalk::first() {
    for (int row : selectableRows())
        if (selectable(row)) { sel_ = row; return; }
}

bool OptionsWalk::open(int page) {
    page_ = page;
    sel_ = 0;
    approx_ = false;
    chosen_.clear();
    log_.clear();
    if (!t_->page(page)) { log_.push_back("no such page"); return false; }
    log_.push_back("open options");
    first();
    return true;
}

int OptionsWalk::selectedOption() const {
    const auto* p = t_->page(page_);
    if (!p) return -1;
    const auto it = p->bind.find(sel_);
    if (it == p->bind.end() || it->second.first < 0) return -1;
    return it->second.first;
}

std::string OptionsWalk::label() const {
    const int i = selectedOption();
    if (i < 0) return {};
    const auto r = t_->rows.find(i);
    return r == t_->rows.end() ? std::string() : r->second.label;
}

bool OptionsWalk::value(std::string& caption, int& v) const {
    const int i = selectedOption();
    if (i < 0) return false;
    const auto r = t_->rows.find(i);
    if (r == t_->rows.end() || r->second.choices.empty()) return false;
    const auto c = chosen_.find(i);
    const int k = c == chosen_.end() ? 0 : c->second;
    if (k < 0 || static_cast<std::size_t>(k) >= r->second.choices.size()) return false;
    caption = r->second.choices[static_cast<std::size_t>(k)].first;
    v       = r->second.choices[static_cast<std::size_t>(k)].second;
    return true;
}

void OptionsWalk::gotoPage(int k) {
    if (k == 0) {
        if (!chosen_.empty()) {      // a value was changed this run
            approx_ = true;
            log_.push_back("page 0: dirty latch, prompt not modelled");
        } else {
            k = 1;
        }
    }
    page_ = k;
    sel_ = 0;
    first();
}

bool OptionsWalk::press(std::uint32_t bits) {
    const auto* p = t_->page(page_);
    if (!p) return false;
    // A row a BRANCH binds cannot be resolved by the byte scan that produced
    // this table, so touching one makes the walk approximate rather than
    // silently taking whichever arm the scan happened to see last.
    if (p->branch.count(sel_)) approx_ = true;

    const auto rows = selectableRows();
    if (rows.empty()) return false;
    if (bits & (kUiUp | kUiDown)) {
        const int step = (bits & kUiUp) ? -1 : 1;
        const int n = static_cast<int>(rows.size());
        int j = static_cast<int>(
            std::find(rows.begin(), rows.end(), sel_) - rows.begin());
        for (int t = 0; t < n; ++t) {
            j = ((j + step) % n + n) % n;
            if (selectable(rows[static_cast<std::size_t>(j)])) break;
        }
        sel_ = rows[static_cast<std::size_t>(j)];
        log_.push_back("move");
        return true;
    }
    const int i = selectedOption();
    if (i < 0) return false;
    const auto r = t_->rows.find(i);
    if (r == t_->rows.end()) return false;
    const auto& o = r->second;

    if ((o.kind == "choice" || o.kind == "device") && !o.choices.empty()) {
        const int n = static_cast<int>(o.choices.size());
        int c = chosen_.count(i) ? chosen_[i] : 0;
        if (bits & kUiLeft)                        c = c ? c - 1 : n - 1;
        else if (bits & (kUiRight | kUiConfirm))   c = (c == n - 1) ? 0 : c + 1;
        else return false;
        chosen_[i] = c;
        log_.push_back("set");
        return true;
    }
    if (bits & kUiConfirm) {
        // the lift resolves a binding's page ADDRESS to the page INDEX, so
        // -1 is "no page behind this row"
        const int behind = p->bind.at(sel_).second;
        if (o.kind == "header" && behind >= 0) {
            gotoPage(behind);
            log_.push_back("enter page");
            return true;
        }
        if (o.kind == "back") {
            gotoPage(behind);
            log_.push_back("back to page");
            return true;
        }
        log_.push_back("unmodelled row");
    }
    return false;
}

// ================================================================ screen 35

namespace {

// The option rows this file applies, by index. Rows 10..12 are the three
// volumes, 23/24 the mouse sensitivities - the sliders; the rest choices.
constexpr int kRowResolution = 2, kRowClip = 3, kRowSky = 4, kRowShadows = 5,
              kRowStreet = 6, kRowDetail = 7, kRowAccel = 8,
              kRowVolDialogue = 10, kRowVolMusic = 11, kRowVolEffects = 12,
              kRowSound3d = 13, kRowSubtitles = 15, kRowFight = 16, kRowShoot = 17,
              kRowCombatCam = 18, kRowSensX = 23, kRowSensY = 24, kRowInvert = 25,
              kRowForceFeedback = 27, kRowBack = 72;

// The page records' `+0`, so BACK knows where it goes. Pages 2..5 return
// through page 0, the save prompt; the control-scheme pages through 5 and 6.
int parentPage(int p) {
    switch (p) {
    case 2: case 3: case 4: case 5: return 0;
    case 6: case 11: case 12:       return 5;
    case 7: case 8: case 9: case 10: return 6;
    default:                        return -1;
    }
}

// Page 0's three items, list 0x004DD3B0 in its item-array order: `Oui`
// (0x004DD310, callback 0x00492AB0), `Non` (0x004DD358, child the root) and
// the caption (0x004DD2C8, bank A 0x20000004 - never selectable).
// `ui.json` carries the captions as UTF-8, and the interface's fonts are
// indexed by the cp1252 byte the shipped `IAM\Options` holds - so "Très"
// would draw two wrong glyphs. Every caption is Latin-1, which maps 1:1.
std::string latin1(const std::string& u) {
    std::string o;
    for (std::size_t i = 0; i < u.size(); ++i) {
        const auto c = static_cast<unsigned char>(u[i]);
        if (c >= 0xC2 && c <= 0xC3 && i + 1 < u.size()) {
            o.push_back(static_cast<char>(((c & 0x03) << 6) |
                                          (static_cast<unsigned char>(u[i + 1]) & 0x3F)));
            ++i;
        } else {
            o.push_back(static_cast<char>(c));
        }
    }
    return o;
}

struct PromptItem { int string, y; bool selectable; };
constexpr PromptItem kPrompt[3] = {{61, 200, true}, {60, 240, true}, {71, 160, false}};

}  // namespace

OptionsMenu::OptionsMenu(const OptionTree& t) : t_(&t), rows_(74) {
    // `+92`, the values each choice's caption stands for
    for (auto& [i, r] : t.rows) {
        if (i < 0 || i >= 74) continue;
        auto& st = rows_[static_cast<std::size_t>(i)];
        st.value.assign(10, 0);
        for (std::size_t k = 0; k < r.choices.size() && k < 10; ++k)
            st.value[k] = r.choices[k].second;
    }
}

const std::string& OptionsMenu::str(int id) const {
    static const std::string none;
    return id >= 0 && static_cast<std::size_t>(id) < text_.size()
        ? text_[static_cast<std::size_t>(id)] : none;
}

void OptionsMenu::open(int param, const SettingsBlock& s) {
    param_ = param;
    s_ = s;
    // ...and the open callback's last two loops: clear bit 1, SET bit 2 on
    // every row, so `Opt_RowInput` applies each change as it is made
    for (auto& r : rows_) r.flags = (r.flags & ~1) | 2;
    open_ = true;
    focused_ = false;
    built_ = false;
    dirty_ = false;
    page_ = 1;
}

void OptionsMenu::focus() {
    if (!open_) return;
    focused_ = true;
    if (!built_) { built_ = true; build(1); }
}

void OptionsMenu::setDevices(int item, std::vector<std::string> names, int cur) {
    if (item < 0 || item >= 74) return;
    devices_[item] = std::move(names);
    rows_[static_cast<std::size_t>(item)].current = cur;
}

int OptionsMenu::choices(int item) const {
    if (item < 0 || item >= 74) return 0;
    const auto d = devices_.find(item);
    if (d != devices_.end()) return static_cast<int>(d->second.size());
    const auto r = t_->rows.find(item);
    return r == t_->rows.end() ? 0 : static_cast<int>(r->second.choices.size());
}

int OptionsMenu::current(int item) const {
    return item >= 0 && item < 74 ? rows_[static_cast<std::size_t>(item)].current : -1;
}

int OptionsMenu::sliderValue(int item) const {
    if (item < 0 || item >= 74) return 0;
    const auto& r = rows_[static_cast<std::size_t>(item)];
    const int c = std::clamp(r.current, 0, 9);
    return r.value[static_cast<std::size_t>(c)];
}

// ---- the rows' own hooks ---------------------------------------------------
//
// `+16`, the READ-BACK, sets the row's `+8` (and a slider's `+92`) from the
// global; `+12`, the APPLY, writes the global from them. Read out of
// 0x0048FA50..0x00490480. The choice rows all share one shape - the global
// byte, clamped to `count - 1` on the way in, the chosen `+92` value on the
// way out - and the exceptions are the clip distance (a scan, and a cap of
// 200), the volumes (an attenuation, `(100 - v) * 0.4`, and its inverse
// `100 - 2.5 a`, 0 past 40) and the sliders that are the value itself.
void OptionsMenu::readBack(int item) {
    if (item < 0 || item >= 74) return;
    auto& r = rows_[static_cast<std::size_t>(item)];
    const int n = choices(item);
    const auto clampCur = [&](int v) { r.current = (v >= n) ? n - 1 : v; };
    switch (item) {
    case kRowResolution: case kRowAccel:
        // `Opt_ReadResolution` / `Opt_ReadAccel3D` copy the device list's
        // count and current; the caller set both
        if (r.current >= n) r.current = n - 1;
        break;
    case kRowClip: {
        // 0x0048FB10: the first value the distance does not exceed
        int i = 0;
        while (i < n && s_.clipDistance > r.value[static_cast<std::size_t>(i)]) ++i;
        clampCur(i);
        break;
    }
    case kRowSky:          clampCur(s_.sky ? 1 : 0); break;
    case kRowShadows:      clampCur(s_.shadows ? 1 : 0); break;
    case kRowStreet:       clampCur(s_.streetActivity); break;
    case kRowDetail:       clampCur(s_.levelOfDetail); break;
    case kRowSound3d:      clampCur(s_.sound3d ? 1 : 0); break;
    case kRowSubtitles:    clampCur(s_.subtitles ? 1 : 0); break;
    case kRowFight:        clampCur(s_.fightDifficulty); break;
    case kRowShoot:        clampCur(s_.shootDifficulty); break;
    case kRowCombatCam:    clampCur(s_.combatCamera); break;
    case kRowInvert:       clampCur(s_.mouseInverted ? 1 : 0); break;
    case kRowForceFeedback: clampCur(s_.forceFeedback ? 1 : 0); break;
    case kRowVolDialogue: case kRowVolMusic: case kRowVolEffects: {
        const int a = item == kRowVolDialogue ? s_.volumeDialogue
                    : item == kRowVolMusic    ? s_.volumeMusic : s_.volumeEffects;
        r.current = 0;
        r.value[0] = a <= 40 ? static_cast<int>(100.0 - a * 2.5) : 0;
        break;
    }
    case kRowSensX: case kRowSensY:
        // 0x004903C0: `+8` = 0 when the row has any choices, the word into +92
        r.current = 0;
        r.value[0] = item == kRowSensX ? s_.mouseSensitivityX : s_.mouseSensitivityY;
        break;
    default: break;
    }
}

void OptionsMenu::apply(int item) {
    if (item < 0 || item >= 74) return;
    auto& r = rows_[static_cast<std::size_t>(item)];
    const int c = std::clamp(r.current, 0, 9);
    const int v = r.value[static_cast<std::size_t>(c)];
    switch (item) {
    case kRowResolution: {
        const auto d = devices_.find(item);
        if (d == devices_.end() || r.current < 0 ||
            r.current >= static_cast<int>(d->second.size())) return;
        int w = 0, h = 0;
        if (std::sscanf(d->second[static_cast<std::size_t>(r.current)].c_str(),
                        "%d x %d", &w, &h) == 2) { s_.screenX = w; s_.screenY = h; }
        break;
    }
    case kRowClip:         s_.clipDistance = std::min(v, 200); break;   // `jbe` - unsigned
    case kRowSky:          s_.sky = v != 0; break;
    case kRowShadows:      s_.shadows = v != 0; break;
    case kRowStreet:       s_.streetActivity = v; break;
    case kRowDetail:       s_.levelOfDetail = v; break;
    case kRowAccel:        break;   // the driver - the caller reads `current`
    case kRowSound3d:      s_.sound3d = v != 0; break;
    case kRowSubtitles:    s_.subtitles = v != 0; break;
    case kRowFight:        s_.fightDifficulty = v; break;
    case kRowShoot:        s_.shootDifficulty = v; break;
    case kRowCombatCam:    s_.combatCamera = v; break;
    case kRowInvert:       s_.mouseInverted = v != 0; break;
    case kRowForceFeedback: s_.forceFeedback = v != 0; break;
    case kRowVolDialogue: case kRowVolMusic: case kRowVolEffects: {
        // `_ftol((100 - v) * 0.4f)` - truncation, the float's own 0.4
        const int a = static_cast<int>((100 - v) * 0.4000000059604645);
        (item == kRowVolDialogue ? s_.volumeDialogue
         : item == kRowVolMusic ? s_.volumeMusic : s_.volumeEffects) = a;
        break;
    }
    case kRowSensX: s_.mouseSensitivityX = v; break;
    case kRowSensY: s_.mouseSensitivityY = v; break;
    default: return;
    }
    applied_.push_back(item);
}

// `Opt_RowInput`'s tail, after a value moved: with `+132 & 2` the apply hook
// runs and the dirty bit clears, otherwise the dirty bit is set. Either way
// `dword_9103C8`, the latch page 0 asks about.
void OptionsMenu::changed(int item) {
    auto& r = rows_[static_cast<std::size_t>(item)];
    if (r.flags & 2) { apply(item); r.flags &= ~1; }
    else             r.flags |= 1;
    dirty_ = true;
}

// ---- pages ------------------------------------------------------------------

int OptionsMenu::rowAt(int w) const {
    if (page_ == 0) return -1;
    // `Opt_PageRoot`'s branch: row 4 is `Retour` with no page behind it when
    // the START MENU hosts the screen, and empty under the sneak, whose own
    // tabs are the way out (0x004912F0)
    if (page_ == 1 && w == 4) return param_ == 1 ? kRowBack : -1;
    const auto* p = t_->page(page_);
    if (!p) return -1;
    const auto it = p->bind.find(w);
    return it == p->bind.end() ? -1 : it->second.first;
}

int OptionsMenu::rowPage(int w) const {
    if (page_ == 0 || (page_ == 1 && w == 4)) return -1;
    const auto* p = t_->page(page_);
    if (!p) return -1;
    const auto it = p->bind.find(w);
    return it == p->bind.end() ? -1 : it->second.second;
}

bool OptionsMenu::selectableRow(int w) const {
    if (page_ == 0) return w >= 0 && w < 3 && kPrompt[w].selectable;
    const int item = rowAt(w);
    if (item < 0) return false;
    const auto r = t_->rows.find(item);
    if (r == t_->rows.end()) return false;
    // `Opt_BindRow`: a header with no page behind it is a CAPTION
    return r->second.type == 2 ? rowPage(w) >= 0 : true;
}

void OptionsMenu::build(int p) {
    page_ = p;
    if (p == 0) {
        // 0x00492A70: the prompt only while a setting has changed; otherwise
        // straight on to the root, and leaving page 0 clears the latch
        if (dirty_) { sel_ = 1; return; }   // `word_4DD3B2 = 1` - `Non`
        dirty_ = false;
        build(1);
        return;
    }
    // `Opt_BindRow` calls each bound row's read-back
    for (int w = 0; w < t_->rowWidgets; ++w)
        if (const int item = rowAt(w); item >= 0) readBack(item);
    // the builders' selection: the page's saved `+80`, moved on past a row
    // that cannot take it to the first that can
    const auto saved = pageSel_.find(p);
    sel_ = saved == pageSel_.end() ? 0 : saved->second;
    if (!selectableRow(sel_))
        for (int w = 0; w < t_->rowWidgets; ++w)
            if (selectableRow(w)) { sel_ = w; break; }
}

void OptionsMenu::gotoPage(int k) {
    // the leave hooks: every row page saves its selection (0x00492A60);
    // page 0's clears the latch (0x00492AA0)
    if (page_ == 0) dirty_ = false;
    else            pageSel_[page_] = sel_;
    build(k);
}

bool OptionsMenu::moveVertical(int step) {
    const int n = page_ == 0 ? 3 : t_->rowWidgets;
    int j = sel_;
    for (int t = 0; t < n; ++t) {
        j = ((j + step) % n + n) % n;
        if (selectableRow(j)) break;
    }
    if (j == sel_ || !selectableRow(j)) return false;
    sel_ = j;
    return true;
}

bool OptionsMenu::press(std::uint32_t bits) {
    moved_ = false;
    if (!focused_ || !bits) return false;
    if (page_ == 0) {
        if (bits & (kUiUp | kUiDown)) { moved_ = moveVertical((bits & kUiUp) ? -1 : 1); return true; }
        if (bits & kUiConfirm) {
            if (sel_ == 0) saveReq_ = true;   // `Oui`: sub_4092A0, then the root
            gotoPage(1);
            return true;
        }
        return false;                         // page 0 has no parent and no hook
    }
    // page 1's panel hook comes FIRST (0x00492AD0)
    if (page_ == 1) {
        if (param_ == 1 && (bits & kUiBack)) { pageSel_[1] = sel_; unfocus(); return true; }
        // under the sneak: LEFT / RIGHT hand the keys back to it
        // (`UI_FocusScreen(9)`), BACK or CLOSE close it (`UI_CloseScreen(9)`)
        if (param_ == 0 && (bits & (kUiLeft | kUiRight))) {
            pageSel_[1] = sel_; unfocus(); return true;
        }
        if (param_ == 0 && (bits & (kUiBack | kUiClose))) {
            pageSel_[1] = sel_; unfocus(); hostClose_ = true; return true;
        }
    }
    // ...then the list's: `Ui_MoveSelectionVertical`, which falls through to
    // the confirm when nothing moved - so a row with a page behind it is
    // entered generically, without a case in `Opt_RowInput`
    if (bits & (kUiUp | kUiDown)) { moved_ = moveVertical((bits & kUiUp) ? -1 : 1); return true; }
    const int item = rowAt(sel_);
    if ((bits & kUiConfirm) && item >= 0 && rowPage(sel_) >= 0) {
        gotoPage(rowPage(sel_));
        return true;
    }
    if (bits & kUiBack) {
        if (const int up = parentPage(page_); up >= 0) { gotoPage(up); return true; }
        return false;
    }
    if (item < 0) return false;
    const auto ro = t_->rows.find(item);
    if (ro == t_->rows.end()) return false;
    auto& r = rows_[static_cast<std::size_t>(item)];
    switch (ro->second.type) {
    case 0: case 4: {
        const int n = choices(item);
        if (n <= 0) return false;
        if (bits & kUiLeft)                           r.current = r.current ? r.current - 1 : n - 1;
        else if (bits & (kUiRight | kUiConfirm))      r.current = r.current == n - 1 ? 0 : r.current + 1;
        else return false;
        if (r.current >= n) r.current = n - 1;
        changed(item);
        return true;
    }
    case 1: {
        auto& v = r.value[static_cast<std::size_t>(std::clamp(r.current, 0, 9))];
        if (bits & kUiLeft)                           v = std::max(0, v - 10);
        else if (bits & (kUiRight | kUiConfirm))      v = std::min(100, v + 10);
        else return false;
        changed(item);
        return true;
    }
    case 6:
        // the root's `Retour` - `UI_FocusScreen(29)` and the page slides out
        if ((bits & kUiConfirm) && page_ == 1) { pageSel_[1] = sel_; unfocus(); return true; }
        return false;
    default:
        return false;   // 3 keybind, 5 defaults: not modelled (see the class)
    }
}

// ---- what is drawn ----------------------------------------------------------

int OptionsMenu::listX() const {
    // the open callback's `sub_4295C0`: rows 70 / prompt 120 under the start
    // menu, 100 / 160 under the sneak
    if (page_ == 0) return param_ == 1 ? 120 : 160;
    return param_ == 1 ? 70 : 100;
}

void OptionsMenu::listColour(std::uint8_t rgb[3]) const {
    // `byte_4DE0D8..DA` for the rows, and for the prompt under the sneak;
    // white for the prompt under the start menu
    const bool white = page_ == 0 && param_ == 1;
    rgb[0] = 255;
    rgb[1] = white ? 255 : 100;
    rgb[2] = white ? 255 : 70;
}

std::vector<OptionsMenu::Widget> OptionsMenu::widgets() const {
    std::vector<Widget> out;
    if (page_ == 0) {
        for (int k = 0; k < 3; ++k) {
            Widget w;
            w.row = k;
            w.label = str(kPrompt[k].string);
            w.y = kPrompt[k].y;
            w.w = 400;
            w.font = 'S';
            w.align = 8;                       // bank C 0x80000011: white, centred
            w.alwaysLit = !kPrompt[k].selectable;
            w.selectable = kPrompt[k].selectable;
            w.selected = k == sel_;
            out.push_back(std::move(w));
        }
        return out;
    }
    for (int row = 0; row < t_->rowWidgets; ++row) {
        const int item = rowAt(row);
        if (item < 0) continue;
        const auto ro = t_->rows.find(item);
        if (ro == t_->rows.end()) continue;
        const auto& o = ro->second;
        Widget w;
        w.row = row;
        w.item = item;
        w.type = o.type;
        w.label = o.labelId >= 0 && !str(o.labelId).empty() ? str(o.labelId) : latin1(o.label);
        // `Opt_BindRow`: white and no alignment first, then by type
        const bool caption = o.type == 2 && rowPage(row) < 0;
        w.font = (o.type == 2 || o.type == 6) ? 'S' : 'J';
        w.align = (o.type == 2 || o.type == 5 || o.type == 6) ? 8 : 4;
        w.white = !caption;
        w.alwaysLit = caption;
        w.selectable = selectableRow(row);
        w.selected = row == sel_;
        const auto& st = rows_[static_cast<std::size_t>(item)];
        if (o.type == 0 && st.current >= 0 &&
            static_cast<std::size_t>(st.current) < o.choices.size())
            w.value = latin1(o.choices[static_cast<std::size_t>(st.current)].first);
        else if (o.type == 4) {
            const auto d = devices_.find(item);
            if (d != devices_.end() && st.current >= 0 &&
                static_cast<std::size_t>(st.current) < d->second.size())
                w.value = d->second[static_cast<std::size_t>(st.current)];
        } else if (o.type == 1)
            w.slider = sliderValue(item);
        out.push_back(std::move(w));
    }
    // `Opt_LayOutPage` (0x004910B0): spread the bound rows down 280 pixels -
    // 140 for three or fewer - from y 80 under the sneak, 120 under the menu
    const int n = static_cast<int>(out.size());
    const int extent = n > 3 ? 280 : 140;
    const int step = n <= 1 ? 0 : (extent - 20 * n) / (n - 1) + 20;
    int y = param_ == 0 ? 80 : 120;
    for (auto& w : out) { w.y = y; y += step; }
    return out;
}

}  // namespace omk

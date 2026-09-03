// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/options.h"

#include "platform/json.h"

#include <algorithm>
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

}  // namespace omk

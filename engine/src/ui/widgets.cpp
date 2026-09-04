// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets.h"

#include "platform/json.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <cstdlib>

namespace omk {
namespace {

std::vector<FlagOp> flagOps(const Json& a) {
    std::vector<FlagOp> out;
    for (std::size_t i = 0; i < a.size(); ++i)
        out.push_back({static_cast<std::uint32_t>(a[i][0].i64()),
                       a[i][1].boolean()});
    return out;
}

}  // namespace

void UiItem::effective(const std::vector<FlagOp>& broadcast,
                       std::uint32_t out[3]) const {
    for (int k = 0; k < 3; ++k) out[k] = flags[k];
    // A flag constant carries its BANK in the top bits, so which word a
    // broadcast lands in is decided by the constant, not by the caller.
    //
    // **There used to be a `conditional` guard here, refusing any flag the
    // lift recorded BOTH ways.** It existed because the lift scanned an open
    // callback's bytes linearly and `Ui_OpenShop` BRANCHES: the shops grey one
    // row and enable its neighbour on one arm and the reverse on the other, so
    // a linear scan saw `off, off, on, on` for one item and `on, on, off, off`
    // for the other, and applying that in address order picked whichever arm
    // came last. Refusing them left the static record standing - cruder, and
    // right for the same reason, which is what `tools/sim/ui.py` did too by
    // reading `+48` raw.
    //
    // Since 2026-09-01 the lift FOLLOWS that branch (`sim/ui.py: SHOP_TEST`),
    // so no contradictory edit is left in the tree - 0 of 411 items, where
    // there were 20 - and the guard became dead code that would now hide a
    // real per-screen difference. Removed, and the ten shops each show the
    // one of "Acheter" and "Vendre" their own arm enables.
    const auto apply = [&](const std::vector<FlagOp>& ops) {
        for (const auto& [flag, on] : ops) {
            const int bank = (flag & 0x20000000u) ? 0
                           : (flag & 0x40000000u) ? 1
                           : (flag & 0x80000000u) ? 2 : -1;
            if (bank < 0) continue;
            const std::uint32_t bit = flag & 0x1FFFFFFFu;
            out[bank] = on ? (out[bank] | bit) : (out[bank] & ~bit);
        }
    };
    apply(broadcast);
    apply(setFlag);
}

bool UiItem::selectable(const std::vector<FlagOp>& broadcast) const {
    std::uint32_t w[3];
    effective(broadcast, w);
    return (w[0] & kItemUnselectable) == 0;
}

bool NameField::type(char ch) {
    if (!ch) return false;
    const int c = static_cast<unsigned char>(ch);
    const auto it = sw_.find(c);
    const std::string kind = it == sw_.end() ? "insert" : it->second;
    if (kind == "ignore") return false;
    if (kind == "backspace") {
        if (!cursor_) return false;
        buf_.erase(static_cast<std::size_t>(cursor_ - 1), 1);
        --cursor_;
        return true;
    }
    if (kind == "return") {
        if (buf_.empty()) return false;      // an empty name is refused
        done_ = true;
        return true;
    }
    if (static_cast<int>(buf_.size()) >= cap_) return false;   // buffer full
    buf_.insert(static_cast<std::size_t>(cursor_), 1, ch);
    ++cursor_;
    return true;
}

UiWidgets UiWidgets::loadJson(const std::string& path) {
    UiWidgets w;
    const auto doc = Json::parseFile(path);
    const auto& rows = doc["rows"];
    w.gridHook_ = static_cast<std::uint32_t>(rows["gridHook"].i64());
    // 0 when the table predates the lift; `press` then never matches it
    // and the sneak's pages fall through to "unmodelled panel hook",
    // which is the honest behaviour for an older table.
    w.moveHook_ = static_cast<std::uint32_t>(rows["moveListsHook"].i64(0));
    w.nameHook_ = static_cast<std::uint32_t>(rows["nameHook"].i64());
    w.startHook_    = static_cast<std::uint32_t>(rows["startConfirmHook"].i64());
    w.startName_    = static_cast<std::uint32_t>(rows["startNameList"].i64());
    w.startButtons_ = static_cast<std::uint32_t>(rows["startButtonList"].i64());
    w.nameMax_ = static_cast<int>(rows["nameMax"].i64(20));
    for (const auto& [k, v] : rows["nameSwitch"].members())
        w.nameSwitch_[std::atoi(k.c_str())] = v.str();
    const auto& lp = rows["loadPanel"];
    w.load_.panel      = static_cast<std::uint32_t>(lp["panel"].i64());
    w.load_.slotList   = static_cast<std::uint32_t>(lp["slotList"].i64());
    w.load_.buttonList = static_cast<std::uint32_t>(lp["buttonList"].i64());
    w.load_.charger    = static_cast<std::uint32_t>(lp["charger"].i64());
    w.load_.nouvelle   = static_cast<std::uint32_t>(lp["nouvelle"].i64());
    w.load_.detruire   = static_cast<std::uint32_t>(lp["detruire"].i64());
    w.savesHeader_ = static_cast<std::size_t>(rows["savesHeader"].i64(3496));
    w.saveSlot_    = static_cast<std::size_t>(rows["saveSlot"].i64(32808));
    w.saveSlots_   = static_cast<int>(rows["saveSlots"].i64(256));
    const auto& as = rows["answers"];
    for (std::size_t i = 0; i < as.size(); ++i)
        w.answers_.push_back({static_cast<std::uint32_t>(as[i]["callback"].i64()),
                              static_cast<int>(as[i]["value"].i64()),
                              as[i]["needsName"].boolean()});
    const auto& ps = rows["panels"];
    for (std::size_t i = 0; i < ps.size(); ++i) {
        const auto& p = ps[i];
        UiPanel panel;
        panel.screen = static_cast<int>(p["screen"].i64(-1));
        panel.addr   = static_cast<std::uint32_t>(p["panel"].i64());
        panel.parent = static_cast<std::uint32_t>(p["parent"].i64());
        panel.hook   = static_cast<std::uint32_t>(p["hook"].i64());
        const auto& off = p["offset"];
        panel.offsetX = static_cast<int>(off[0].i64(0));
        panel.offsetY = static_cast<int>(off[1].i64(0));
        const auto& tl = p["tiles"];
        for (std::size_t t = 0; t < tl.size(); ++t)
            panel.tiles.push_back(static_cast<int>(tl[t].i64(0)));
        // -1 is the default and means "the open callback did not write it",
        // which is not the same as list -1: `settle` falls back on the move
        // rule for those and takes this value for the rest.
        panel.current = static_cast<int>(p["current"].i64(-1));
        panel.flags = static_cast<std::uint32_t>(p["flags"].i64(0));
        const auto& ls = p["lists"];
        for (std::size_t j = 0; j < ls.size(); ++j) {
            const auto& l = ls[j];
            UiList list;
            list.addr  = static_cast<std::uint32_t>(l["addr"].i64());
            list.hook  = static_cast<std::uint32_t>(l["hook"].i64());
            list.flags = static_cast<std::uint32_t>(l["flags"].i64());
            list.select = static_cast<int>(l["select"].i64(-1));
            list.flagsB = static_cast<std::uint32_t>(l["flagsB"].i64(0));
            list.broadcast = flagOps(l["broadcast"]);
            const auto& its = l["items"];
            for (std::size_t k = 0; k < its.size(); ++k) {
                const auto& it = its[k];
                UiItem item;
                item.addr     = static_cast<std::uint32_t>(it["addr"].i64());
                item.x        = static_cast<int>(it["x"].i64(0));
                item.y        = static_cast<int>(it["y"].i64(0));
                item.w        = static_cast<int>(it["w"].i64(0));
                item.h        = static_cast<int>(it["h"].i64(0));
                item.string   = static_cast<int>(it["string"].i64(-1));
                const auto& bd = it["bind"];
                item.bindString = static_cast<int>(bd["string"].i64(-1));
                item.bindTag    = static_cast<int>(bd["tag"].i64(-1));
                item.font   = static_cast<int>(it["font"].i64(255));
                item.rgb[0] = static_cast<int>(it["rgb"][0].i64(0));
                item.rgb[1] = static_cast<int>(it["rgb"][1].i64(0));
                item.rgb[2] = static_cast<int>(it["rgb"][2].i64(0));
                item.layer  = static_cast<int>(it["layer"].i64(0));
                item.text    = static_cast<std::uint32_t>(it["text"].i64(0));
                item.textFn  = static_cast<std::uint32_t>(it["textFn"].i64(0));
                item.textArg = static_cast<int>(it["textArg"].i64(-1));
                item.lit[0]   = static_cast<int>(it["lit"][0].i64(0));
                item.lit[1]   = static_cast<int>(it["lit"][1].i64(0));
                item.unlit[0] = static_cast<int>(it["unlit"][0].i64(0));
                item.unlit[1] = static_cast<int>(it["unlit"][1].i64(0));
                item.callback = static_cast<std::uint32_t>(it["callback"].i64());
                item.child    = static_cast<std::uint32_t>(it["child"].i64());
                for (int b = 0; b < 3; ++b)
                    item.flags[b] = static_cast<std::uint32_t>(it["flags"][static_cast<std::size_t>(b)].i64());
                item.setFlag = flagOps(it["setflag"]);
                list.items.push_back(std::move(item));
            }

            // THE OPEN CALLBACK'S LAYOUT PASS, applied over the records.
            //
            // The static x/y/h are not what the screen shows. Each screen's
            // open callback walks its lists through three helpers -
            // `sub_4295C0(list, x)`, `sub_429650(list, h)` and
            // `sub_429680(list, firstY, step)` - and the records ship values
            // those overwrite. The start menu is laid out to y 120 step 80
            // where its records say 150 step 60, and the engine's own capture
            // has the four labels at 127/208/290/370, which is the callback's
            // spacing. Its confirm dialog's `Confirmer` and `Annuler` both
            // ship at y=330 and are separated to 260 and 320; drawn from the
            // records alone they land on top of one another, which is what a
            // player saw.
            const Json& lay = l["layout"];
            if (!lay.isNull()) {
                if (!lay["x"].isNull())
                    for (auto& it2 : list.items)
                        it2.x = static_cast<int>(lay["x"].i64());
                if (!lay["h"].isNull())
                    for (auto& it2 : list.items)
                        it2.h = static_cast<int>(lay["h"].i64());
                if (!lay["firstY"].isNull()) {
                    int y = static_cast<int>(lay["firstY"].i64());
                    const int step = static_cast<int>(lay["stepY"].i64());
                    for (auto& it2 : list.items) { it2.y = y; y += step; }
                }
            }
            panel.lists.push_back(std::move(list));
        }
        w.panels_.push_back(std::move(panel));
    }
    return w;
}

void UiWidgets::loadScreens(const std::string& uiTable) {
    const Json doc = Json::parseFile(uiTable);      // held: a reference into a
    const Json& ss = doc["rows"]["screens"];        // temporary would dangle
    // The 45 interface sounds, id -> name. `docs/UI.md`: all 45 `.wav` ship
    // (61 do; 16 are never named), and the per-screen slots are POSITIONAL -
    // slot 0 is the selection move, slot 1 the confirm, slot 2 the screen
    // opening. Resolving a slot to a name is a ported DECISION and belongs
    // here; playing the file is the frontend's.
    const Json& sn = doc["rows"]["sounds"];
    for (const auto& [k, v] : sn.members())
        soundName_[std::stoi(k)] = v.str();

    for (std::size_t i = 0; i < ss.size(); ++i) {
        const int id = static_cast<int>(ss[i]["id"].i64(-1));
        textFile_[id] = ss[i]["text"].str();
        bitmap_[id]   = ss[i]["bitmap"].str();
        const Json& sl = ss[i]["sounds"];
        std::vector<int> slots;
        for (std::size_t k = 0; k < sl.size(); ++k)
            slots.push_back(static_cast<int>(sl[k].i64(-1)));
        screenSounds_[id] = slots;
    }
}

const std::string& UiWidgets::soundName(int screenId, int slot) const {
    static const std::string none;
    const auto s2 = screenSounds_.find(screenId);
    if (s2 == screenSounds_.end() || slot < 0 ||
        slot >= static_cast<int>(s2->second.size())) return none;
    const int id = s2->second[static_cast<std::size_t>(slot)];
    if (id < 0) return none;                    // -1: this screen has no such sound
    const auto n2 = soundName_.find(id);
    return n2 == soundName_.end() ? none : n2->second;
}

const std::string& UiWidgets::bitmap(int screenId) const {
    static const std::string none;
    const auto it = bitmap_.find(screenId);
    return it == bitmap_.end() ? none : it->second;
}

const std::string& UiWidgets::textFile(int screenId) const {
    static const std::string none;
    const auto it = textFile_.find(screenId);
    return it == textFile_.end() ? none : it->second;
}

const UiPanel* UiWidgets::screen(int id) const {
    for (const auto& p : panels_) if (p.screen == id) return &p;
    return nullptr;
}

const UiPanel* UiWidgets::at(std::uint32_t addr) const {
    for (const auto& p : panels_) if (p.addr == addr) return &p;
    return nullptr;
}

// ------------------------------------------------------------------- the walk

// `sub_49D4D0` (panel 0x004DEDE8 `+16`), read out of the image. It reads the
// screen's input word at `+0x6C` and writes `panel+24` - so it is a
// `Ui_MoveBetweenLists` specialised for this page rather than a call to the
// generic one. The transitions, with the panel's lists being
// [0] tabs 0x004DE210, [1] header 0x004DEA08, [2] rows 0x004DE6F0, [3] echo:
//
//   from 0 (tabs)    the 0x3 pair                  -> 1, if 1 is selectable
//   from 1 (header)  0x1 at its first item, or
//                    0x2 at its last               -> 0, if 0 is selectable
//   from 1 (header)  the 0xC pair                  -> 2, if 2 is selectable,
//                    0x4 selecting the LAST row and 0x8 the FIRST
//   from 2 (rows)    0x4 at the first row, or
//                    0x8 at the last               -> 1, if 1 is selectable
//   from 2 (rows)    the 0x3 pair                  -> 0, if 0 is selectable
//
// WHICH PAIR IS WHICH ON A KEYBOARD is taken from the port's existing
// binding rather than re-derived here: `sub_42A710` is
// `sub_42A5C0(screen, panel, 1, 2)`, and this port already drives that hook
// from LEFT/RIGHT (`press`, the `moveListsHook` arm) with all 31 screens
// agreeing with `tools/sim`. So the 0x3 pair is this file's Left/Right and
// the 0xC pair its Up/Down, and the two are named `listAxis` and `crossAxis`
// so that a later correction moves one line rather than five.
bool UiWalk::moveListsSlider(std::uint32_t bits) {
    if (!panel_) return false;
    const auto index = [&](std::uint32_t addr) -> int {
        for (std::size_t k = 0; k < panel_->lists.size(); ++k)
            if (panel_->lists[k].addr == addr) return static_cast<int>(k);
        return -1;
    };
    const int tabs = index(kListSneakTabs), head = index(kListSneakSliderHead),
              rows = index(kListSneakRows);
    if (tabs < 0 || head < 0 || rows < 0) return false;
    const auto ok = [&](int k) {
        return k >= 0 && usable(panel_->lists[static_cast<std::size_t>(k)]);
    };
    const auto go = [&](int k) { cur_ = k; log_.push_back("focus list"); return true; };
    const std::uint32_t listAxis  = bits & (kUiLeft | kUiRight);
    const std::uint32_t crossAxis = bits & (kUiUp | kUiDown);
    const UiList& here = panel_->lists[static_cast<std::size_t>(cur_)];
    const int sel = selectionOf(here);
    // The EDGES are `sub_429560` and `sub_429590`, the first and last
    // SELECTABLE rows - not the first and last widget. With three
    // destinations in nine widgets the raw last is 8, which the selection can
    // never reach, so the "at the end" transition would never fire and the
    // move wrapped round instead of stepping back to the header.
    const int first = firstPickable(here), last = lastPickable(here);

    if (cur_ == tabs) {
        if (listAxis && ok(head)) return go(head);
        return false;
    }
    if (cur_ == head) {
        if (((bits & kUiLeft) && sel == first) || ((bits & kUiRight) && sel == last))
            if (ok(tabs)) return go(tabs);
        if (crossAxis && ok(rows)) {
            const auto& r = panel_->lists[static_cast<std::size_t>(rows)];
            // `sub_429590` on one bit and `sub_429560` on the other - the
            // LAST and FIRST *selectable* row, not the last and first
            // widget. With three destinations bound into nine widgets the
            // raw index lands on one that draws nothing.
            const int j = (bits & kUiUp) ? lastPickable(r) : firstPickable(r);
            if (j >= 0) sel_[r.addr] = j;
            return go(rows);
        }
        return false;
    }
    if (cur_ == rows) {
        if (((bits & kUiUp) && sel == first) || ((bits & kUiDown) && sel == last))
            if (ok(head)) return go(head);
        if (listAxis && ok(tabs)) return go(tabs);
        return false;
    }
    return false;
}

bool UiWalk::moveLists(int step) {
    // `sub_42A5C0` (0x0042A5C0), the mover behind every panel hook that
    // steps between lists. It walks `panel+24` over the `panel+28` count,
    // skipping a list that is hidden or has nothing selectable in it, and
    // wraps unless `panel+72` carries 0x80000 - the same NOWRAP bit a list
    // uses for its own rows.
    //
    if (!panel_) return false;
    const int n = static_cast<int>(panel_->lists.size());
    if (n <= 0) return false;
    const int before = cur_;
    int k = cur_;
    for (int i = 0; i < n; ++i) {
        k += step;
        if (k < 0)       k = panel_->noWrap() ? 0 : n - 1;
        else if (k >= n) k = panel_->noWrap() ? n - 1 : 0;
        if (usable(panel_->lists[static_cast<std::size_t>(k)])) break;
    }
    cur_ = k;
    log_.push_back("focus list");
    return cur_ != before;
}

// `Ui_MoveSelection`'s test for one row, with the runtime bits the static
// record cannot carry: an item a builder switched off, or one the ROW BINDER
// put past the end of its list, is not selectable however its record reads.
// `sub_429560` / `sub_429590` - the FIRST and LAST selectable index of a
// list, or -1. Both walk the items testing bank A's `0x4`, the same
// not-selectable bit the row binder sets, so they skip a row that is past the
// end of its list rather than returning a widget that shows nothing.
int UiWalk::firstPickable(const UiList& l) const {
    for (std::size_t k = 0; k < l.items.size(); ++k)
        if (pickable(l, l.items[k])) return static_cast<int>(k);
    return -1;
}

int UiWalk::lastPickable(const UiList& l) const {
    for (std::size_t k = l.items.size(); k-- > 0;)
        if (pickable(l, l.items[k])) return static_cast<int>(k);
    return -1;
}

bool UiWalk::pickable(const UiList& l, const UiItem& it) const {
    return it.selectable(l.broadcast) && !itemOff(it.addr);
}

void UiWalk::bindRows(std::uint32_t list, int count) {
    bound_[list] = count;
    if (!panel_) return;
    for (const auto& l : panel_->lists) {
        if (l.addr != list) continue;
        for (std::size_t k = 0; k < l.items.size(); ++k) {
            if (static_cast<int>(k) < count) off_.erase(l.items[k].addr);
            else                             off_.insert(l.items[k].addr);
        }
        // ...and if the selection was left on a row that has just gone away,
        // pull it back to the last live one. `sub_42AAE0` cannot leave the
        // highlight past the end and neither may this.
        auto it = sel_.find(l.addr);
        if (it != sel_.end() && it->second >= count)
            it->second = count > 0 ? count - 1 : 0;
        return;
    }
}

bool UiWalk::usable(const UiList& l) const {
    // `Ui_MoveBetweenLists`'s own predicate: not hidden, and something in it
    // can be selected.
    if (l.hidden()) return false;
    if (listOff(l.addr)) return false;   // sub_4290D0 set 0x20000004 on it all
    for (const auto& it : l.items) if (pickable(l, it)) return true;
    return false;
}

void UiWalk::settle() {
    // `panel+24` on disk is not the answer - it is runtime state, and for
    // twenty-two of the screens the builder that writes it is native code
    // this port does not run. What IS in the data is the rule the engine uses
    // to move between lists, so that is the fallback: the first list that is
    // not hidden and has something selectable in it.
    //
    // **But for fifteen panels the OPEN CALLBACK writes it, and that is
    // readable.** `tables/ui_widgets.json` carries the write as `current`
    // (and the matching `list+2` as `select`) when there is one, -1 when
    // there is not - so this prefers the engine's own value and falls back
    // only where the engine really does leave it to the move rule. It is
    // what decides which page of the sneak comes up: without it screen 9
    // opens on the tab COLUMN with "Identite" lit, and the player is looking
    // at the inventory page with the highlight somewhere else.
    // **A LIST IS SHARED, AND ITS SELECTION IS LIVE.** `sel_` used to be
    // cleared here, so every panel change re-seeded every list from its
    // static `+2` - and the sneak's pages all carry the same tab column
    // (0x004DE210), so confirming a tab threw the column's selection back to
    // whatever that page's record happened to say. A player saw it as
    // "pressing enter on an item hovers the first item (character)".
    //
    // The engine has ONE list record per address; `Ui_DrawList` and
    // `Ui_MoveSelection` read and write its `+2` in place, and nothing
    // rewrites it on a panel change - only an OPEN CALLBACK does, which is
    // the `select` the tree carries. So a list already walked keeps what it
    // has, and only lists this walk has not seen yet are seeded.
    cur_ = 0;
    if (!panel_) return;
    if (panel_->current >= 0 &&
        static_cast<std::size_t>(panel_->current) < panel_->lists.size()) {
        cur_ = panel_->current;
    } else {
        for (std::size_t k = 0; k < panel_->lists.size(); ++k)
            if (usable(panel_->lists[k])) { cur_ = static_cast<int>(k); break; }
    }
    for (const auto& l : panel_->lists) {
        int j = -1;
        if (l.select >= 0 && static_cast<std::size_t>(l.select) < l.items.size())
            j = l.select;
        if (j < 0) {
            j = 0;
            for (std::size_t i = 0; i < l.items.size(); ++i)
                if (pickable(l, l.items[i])) { j = static_cast<int>(i); break; }
        }
        sel_.emplace(l.addr, j);      // keeps a live selection, seeds a new one
    }
}

// `sub_4296B0` (0x004296B0) - the item colour setter, three bytes:
//
//     mov [eax+8], cl / mov [eax+9], dl / mov [eax+0Ah], cl
//
// Three call sites, all three the same write: the sneak's clock item
// (0x004DEC08) back to black after the list it sits in has been recoloured.
void UiWalk::colourItem(std::uint32_t item, int r, int g, int b) {
    colour_[item] = {r, g, b};
}

// `sub_4296D0` (0x004296D0) - the same three bytes over EVERY item of a list.
// The count is the list's own `+0` and the item array its `+12`, which is the
// same pair `sub_42AAE0` walks; the loop is `jnz` on a decremented count, so a
// list of zero writes nothing.
void UiWalk::colourList(std::uint32_t list, int r, int g, int b) {
    if (!panel_) return;
    for (const auto& l : panel_->lists) {
        if (l.addr != list) continue;
        for (const auto& it : l.items) colourItem(it.addr, r, g, b);
        return;
    }
}

// The panel `+4` BUILDER, for the sneak family - and `panel+4` is a callback
// slot the walker did not read until now (it lifts `+16`, the input hook).
//
// Each sneak page is its own PANEL with its own builder, and the builder's
// last act is to copy the page's TAB ICON colour over the shared lists the
// page carries. The six builders, each named by its panel's `+4`:
//
//     0x0049B710  panel 0x004DEE50  Inventory   icon 0x004DE040
//     0x0049C100  panel 0x004DED80  Identity    icon 0x004DDFB0
//     0x0049D170  panel 0x004DEDE8  Slider      icon 0x004DDFF8
//     0x0049D750  panel 0x004DEF88  Memory      icon 0x004DE088
//     0x0049D8F0  panel 0x004DF058  Options     icon 0x004DE0D0
//     0x0049D980  panel 0x004DF0C0  Quit        icon 0x004DE118
//
// WHICH ICON BELONGS TO WHICH PAGE IS IN THE TREE, not in this file: the tab
// column (list 0x004DE210, shared by every page) is a list of items whose
// `child` IS the page it opens. So the mapping is read rather than tabulated,
// and a page the tree does not carry simply has no entry.
//
// WHICH LISTS EACH ONE COLOURS is the shipped data's own check, and it is
// exact. Three lists are shared between pages - the nine rows 0x004DE6F0,
// the three verbs 0x004DE318 and the echo bar 0x004DEC58 - and every builder
// colours PRECISELY the shared lists its own panel carries:
//
//     Identity   echo               <- builder colours echo
//     Slider     rows, echo         <- rows, echo
//     Inventory  verbs, rows, echo  <- verbs, rows, echo
//     Memory     rows, echo         <- rows, echo
//     Options    echo               <- echo
//     Quit       echo               <- echo
//
// Six of six, and the membership is lifted from the tree while the calls are
// read from the image, so the two could have disagreed. That is what makes
// this a rule rather than a table of addresses.
void UiWalk::buildPage(const UiPanel& p) {
    colour_.clear();
    off_.clear();
    offList_.clear();
    // `sub_4290D0(list, 0x20000004, value)` - the not-selectable bit over a
    // WHOLE list. The inventory page's builder (0x0049B710) runs it twice:
    //
    //     sub_4290D0(0x004DE6F0, 0x20000004, 0)   the rows,  selectable
    //     sub_4290D0(0x004DE318, 0x20000004, 1)   the verbs, NOT
    //
    // so the page opens with the verb bar visible and unreachable. What
    // enables it is confirming a row, which descends into panel 0x004DEEB8 -
    // whose own builder 0x0049B810 clears the bit on the verbs and SETS it
    // on the tabs, the previews and the rows. That panel is not in the tree
    // yet (nothing names it through an item `child`; `sub_42A370` names it
    // from code), so the descent is not modelled - but the disable is, and
    // without it the verbs are reachable with no object chosen.
    if (p.addr == kPanelSneakInventory) offList_.insert(kListSneakVerbs);
    // `sub_49B810`, the VERB panel's builder - the mirror of the line above,
    // and the reason the verbs become reachable at all:
    //
    //     sub_4290D0(0x004DE318, 0x20000004, 0)   the verbs,    selectable
    //     sub_4290D0(0x004DE210, 0x20000004, 1)   the tabs,     NOT
    //     sub_4290D0(0x004DE420, 0x20000004, 1)   the previews, NOT
    //     sub_4290D0(0x004DE6F0, 0x20000004, 1)   the rows,     NOT
    //
    // so while a verb is being chosen nothing else on the device can be
    // reached. It also marks the chosen row `0x40000008`, which keeps it lit
    // under the verb bar - not modelled, because that flag's drawing arm is
    // the lit/unlit ladder and this port has no runtime bit to put it in.
    if (p.addr == kPanelSneakVerbs) {
        offList_.insert(kListSneakTabs);
        offList_.insert(kListSneakPreviews);
        offList_.insert(kListSneakRows);
    }
    // `sub_49B950`, the EXAMINE page's builder. It disables the verb list and
    // sets `0x40000002` on item 0x004DE2C0 - "Examiner" itself - so the verb
    // you chose stays lit while its page is up. It then reads the row's tag,
    // asks `sub_42B330` for the object's KIND and sends kind 5 to
    // `sub_478EF0`, which is the document path; and it plays interface sound
    // 8. None of those three is modelled here - this is the navigation only.
    if (p.addr == kPanelSneakExamine) offList_.insert(kListSneakVerbs);
    // THE SLIDER PAGE'S TWO-STATE HEADER, and a builder doing more than
    // colour. `0x0049D170` opens with `cmp [arg0+4], 1` and both arms are
    // `sub_428FF0` calls on three items of list 0x004DEA08:
    //
    //   arm 1   hide 0x004DE920, show 0x004DE968 + 0x004DE9B0, select 1
    //   else    show 0x004DE920, hide 0x004DE968 + 0x004DE9B0, select 0
    //
    // and the tree says why there are two: 0x004DE920 (string 12) and
    // 0x004DE968 (string 13) are BOTH at (187, 30), 202x22 - alternatives at
    // one coordinate, never both drawn. Drawing both is what a player saw as
    // overlapping text.
    //
    // The ELSE arm is what a capture of the original shows - one wide bar
    // reading "Appel du slider", string 12 - so that is the state the page
    // opens in here. Which value of `arg0+4` selects the other arm is not
    // established: it is a message code the port does not deliver, and the
    // two-label state (string 13 "Automatique" beside 14 "Manuelle") is
    // recorded rather than reachable.
    if (p.addr == kPanelSneakSlider) {
        off_.insert(0x004DE968u);
        off_.insert(0x004DE9B0u);
    }
    // The page's own tab: the column item whose `child` is this panel.
    const UiItem* icon = nullptr;
    for (const auto& l : p.lists) {
        if (l.addr != kListSneakTabs) continue;
        for (const auto& it : l.items)
            if (it.child == p.addr) icon = &it;
        break;
    }
    if (!icon) return;                       // not a page of the sneak device
    for (std::uint32_t list : {kListSneakRows, kListSneakVerbs, kListSneakEcho})
        colourList(list, icon->rgb[0], icon->rgb[1], icon->rgb[2]);
    // ...and the CLOCK goes back to black on the pages whose builder says so.
    // `sub_4296B0(0x004DEC08, 0, 0, 0)` runs in the Memory, Options and Quit
    // builders and in neither Inventory nor Slider - which is why a capture
    // of the original shows the date on those two pages and not on Memory.
    if (p.addr == kPanelSneakMemory || p.addr == kPanelSneakOptions ||
        p.addr == kPanelSneakQuit)
        colourItem(kItemSneakClock, 0, 0, 0);
}

bool UiWalk::open(int screenId) {
    panel_ = w_->screen(screenId);
    approx_ = false;
    answer_ = -1;
    log_.clear();
    if (!panel_) { log_.push_back("no panel for this screen"); return false; }
    log_.push_back("open");
    buildPage(*panel_);
    settle();
    return true;
}

const UiList* UiWalk::curList() const {
    if (!panel_ || cur_ < 0 ||
        static_cast<std::size_t>(cur_) >= panel_->lists.size()) return nullptr;
    return &panel_->lists[static_cast<std::size_t>(cur_)];
}

int UiWalk::selectionOf(const UiList& l) const {
    const auto it = sel_.find(l.addr);
    return it == sel_.end() ? -1 : it->second;
}

int UiWalk::selection() const {
    const auto* l = curList();
    if (!l) return -1;
    const auto it = sel_.find(l->addr);
    return it == sel_.end() ? -1 : it->second;
}

const UiItem* UiWalk::selected() const {
    const auto* l = curList();
    if (!l) return nullptr;
    const int j = selection();
    if (j < 0 || static_cast<std::size_t>(j) >= l->items.size()) return nullptr;
    return &l->items[static_cast<std::size_t>(j)];
}

bool UiWalk::move(const UiList& l, std::uint32_t bits,
                  std::uint32_t back, std::uint32_t on) {
    // `sub_42A7E0(screen, list, a3, a4)` - `Ui_MoveSelection`, and the two
    // bits are PARAMETERS. The default dispatch passes `(4, 8)`, UP and DOWN;
    // `sub_42A930`, the hook the sneak's verb bar names, passes `(1, 2)` -
    // LEFT and RIGHT, because that row of buttons runs across the screen. One
    // function, two bindings, which is why this takes them rather than
    // hard-coding UP/DOWN as it did.
    if (l.items.empty()) return false;
    const int n = static_cast<int>(l.items.size());
    const int step = (bits & back) ? -1 : (bits & on) ? 1 : 0;
    if (step) {
        int j = sel_[l.addr];
        for (int t = 0; t < n; ++t) {              // skip unselectable items
            j += step;
            if (j < 0)       j = l.noWrap() ? 0 : n - 1;
            else if (j >= n) j = l.noWrap() ? n - 1 : 0;
            if (pickable(l, l.items[static_cast<std::size_t>(j)])) break;
        }
        sel_[l.addr] = j;
        log_.push_back("move");
        return true;
    }
    if (bits & kUiConfirm) return confirm();
    return false;
}

bool UiWalk::grid(const UiList& l, std::uint32_t bits) {
    int n = sel_[l.addr];
    const int before = n;
    if (bits & kUiUp)          n = n < 3 ? 6 : (n == 6 ? 4 : n - 3);
    else if (bits & kUiDown)   n = n < 3 ? n + 3 : (n == 6 ? 1 : 6);
    else if (bits & kUiLeft)  { if (n != 6) n = (n % 3) ? n - 1 : n + 2; }
    else if (bits & kUiRight) { if (n != 6) n = (n % 3 == 2) ? n - 2 : n + 1; }
    sel_[l.addr] = n;
    if (n != before) log_.push_back("move");
    if (bits & kUiConfirm) {
        // Confirm writes the answer ITSELF rather than going through an item
        // callback - `dword_930750 = slot - 1`, with slot 0 giving 6 - so the
        // answer is the slot rotated by one: slot 1 ("Niveau 0", the entrance)
        // answers 0. All 18 `ui.open 4` sites store it in variable 496.
        answer_ = n ? n - 1 : 6;
        log_.push_back("answer");
    }
    return n != before || (bits & kUiConfirm) != 0;
}

bool UiWalk::typeName(const std::string& text) {
    const auto* l = curList();
    if (!l || l->hook != w_->nameHook()) return false;
    NameField f(w_->nameSwitch(), w_->nameMax());
    f.enter(name_);
    f.enter(text);
    name_ = f.text();
    return true;
}

bool UiWalk::startConfirm(std::uint32_t bits) {
    const auto* l = curList();
    if (!l) return false;
    if (l->addr == w_->startNameList()) {
        if (bits & kUiDown) { cur_ = 1; log_.push_back("focus list 1"); return true; }
    } else if (l->addr == w_->startButtonList()) {
        // the name field must not be hidden, the selection must be on
        // "Confirmer", and the press must be UP
        const UiList* nf = nullptr;
        for (const auto& x : panel_->lists)
            if (x.addr == w_->startNameList()) nf = &x;
        if (nf && !nf->hidden() && sel_[l->addr] == 0 && (bits & kUiUp)) {
            cur_ = 0; log_.push_back("focus list 0"); return true;
        }
    }
    return false;
}

bool UiWalk::confirm() {
    const auto* it = selected();
    if (!it) return false;
    if (it->callback) {
        // TWO CALLBACKS THAT DESCEND. `Ui_ConfirmSelection` normally follows
        // an item's `+44`, but a callback can install a panel itself with
        // `sub_42A370(screen, panel)` - and the sneak's object flow is built
        // out of exactly that:
        //
        //   sub_49BC60  the ROW's confirm. Its plain arm `loc_49BE7B` is
        //               `sub_42A370(screen, off_4DEEB8)` - the VERB panel.
        //               (Its other arms are the "use on" pairing, gated on
        //               `dword_670BE0`, and the slider's travel; neither is
        //               modelled and both are recorded in docs/UI.md.)
        //   sub_49BFF0  "Examiner": `sub_42B420(tag, 4)` and then
        //               `sub_42A370(screen, off_4DEF20)` - the EXAMINE page.
        //
        // The channel calls each one makes are NOT modelled here; what is,
        // is the navigation, which is what a player is stopped by.
        if (it->callback == kCbSneakRowConfirm ||
            it->callback == kCbSneakExamine) {
            const std::uint32_t to = it->callback == kCbSneakExamine
                                   ? kPanelSneakExamine : kPanelSneakVerbs;
            if (const auto* kid = w_->at(to)) {
                panel_ = kid;
                buildPage(*panel_);
                settle();
                log_.push_back(it->callback == kCbSneakExamine
                               ? "enter examine page" : "enter verb panel");
                return true;
            }
        }
        for (const auto& a : w_->answers())
            if (a.callback == it->callback) {
                if (a.needsName && name_.empty()) {
                    // The engine's own first instruction. Nothing is written
                    // and the screen stays open, so this is NOT an answer -
                    // and not a refusal to model either. It is the modelled
                    // behaviour of confirming with an empty field.
                    log_.push_back("no answer: name field empty");
                    return true;
                }
                answer_ = a.value;
                log_.push_back("answer");
                return true;
            }
        approx_ = true;
        log_.push_back("unmodelled item callback");
        return true;
    }
    if (it->child) {
        if (const auto* kid = w_->at(it->child)) {
            panel_ = kid;
            buildPage(*panel_);
            settle();
            log_.push_back("enter child panel");
            return true;
        }
        approx_ = true;
        log_.push_back("child panel not in the table");
        return true;
    }
    return false;
}

bool UiWalk::press(std::uint32_t bits) {
    if (!panel_) return false;
    if (bits & kUiBack) {
        if (panel_->parent) {
            if (const auto* up = w_->at(panel_->parent)) {
                panel_ = up;
                buildPage(*panel_);
                settle();
                log_.push_back("back");
            } else {
                approx_ = true;
                log_.push_back("parent panel not in the table");
            }
        } else {
            panel_ = nullptr;
            log_.push_back("close");
        }
        return true;
    }
    if (panel_->hook) {
        if (panel_->hook == w_->startConfirmHook()) {
            if (startConfirm(bits)) return true;
        } else if (panel_->hook == kHookSneakSliderLists) {
            if (moveListsSlider(bits)) return true;
        } else if (panel_->hook == w_->moveListsHook()) {
            // `sub_42A710(screen, panel) = sub_42A5C0(screen, panel, 1, 2)` -
            // `Ui_MoveBetweenLists` with LEFT stepping back and RIGHT
            // stepping on. The sneak device's pages name it, and it is what
            // makes the tab column down their left reachable at all.
            if (bits & kUiLeft)  { if (moveLists(-1)) return true; }
            if (bits & kUiRight) { if (moveLists(1))  return true; }
        } else {
            // The engine falls through only when the hook returns 0, and this
            // cannot know which. Fall through, but say the run is no longer
            // exact so a caller can refuse to trust the answer.
            approx_ = true;
            log_.push_back("unmodelled panel hook");
        }
    }
    const auto* l = curList();
    if (!l) return false;
    if (l->hook == w_->gridHook()) return grid(*l, bits);
    if (l->hook == w_->nameHook()) {
        // The name field answers the CHARACTER channel (`sub_4397B0`), not the
        // input bits, and returns 0 for every bit - so the list does not
        // respond to a direction, and because the hook exists the default walk
        // is not reached either.
        log_.push_back("name field: no bit response");
        return false;
    }
    if (l->hook == kMoveSelectionLR) {
        // `sub_42A930(screen, list) = sub_42A7E0(screen, list, 1, 2)` - the
        // same selection mover the default path uses, bound to LEFT and RIGHT
        // instead of UP and DOWN. The sneak's verb bar ("Utiliser", "Utiliser
        // sur", "Examiner") and the slider page's row of buttons are what
        // name it, and both run across rather than down.
        return move(*l, bits, kUiLeft, kUiRight);
    }
    if (l->hook == kHookSneakRows) {
        // `sub_49C050` - the sneak's ROW list, and it is a thin wrapper:
        //
        //     if (sub_42AFF0(screen, list) != 1) return 0;
        //     if (dword_670CB8 == 2)                 // the memory page
        //         dword_4DEAD4 = selected_widget[+0x3C];
        //     return 1;
        //
        // `sub_42AFF0` is `Ui_MoveSelection` over a WINDOW: it moves the
        // selection, and when the list is longer than its nine widgets it
        // scrolls the window and marks the first and last widget with
        // `0x100000` / `0x200000`, the "more above" and "more below"
        // indicators.
        //
        // THE WINDOW IS NOT MODELLED, and this is the honest half: for a
        // list no longer than its widgets `sub_42AFF0` never scrolls and is
        // the ordinary move, which is every list the port reaches (one
        // carried object, three enabled destinations). Refusing the whole
        // hook - which is what this did - meant the selection never moved
        // inside the sneak's rows on ANY page, and a player reported it
        // twice: "I can move between Appel du slider and the first
        // destination but no more", "on inventory list I can go to the first
        // element but not the second".
        //
        // A list longer than nine will need `sub_42AFF0` read properly; it
        // is marked approximate here so that cannot pass unnoticed.
        for (const auto& lm : panel_->lists)
            if (lm.addr == l->addr &&
                static_cast<int>(lm.items.size()) < boundCount(l->addr)) {
                approx_ = true;
                log_.push_back("row window: list longer than its widgets");
            }
        return move(*l, bits);
    }
    if (l->hook) {
        approx_ = true;
        log_.push_back("unmodelled list hook");
        return false;
    }
    return move(*l, bits);
}

std::vector<SaveEntry> saveDirectory(const std::string& gamesPath,
                                     const UiWidgets& w) {
    std::vector<SaveEntry> out;
    std::ifstream f(gamesPath, std::ios::binary);
    if (!f) return out;
    std::vector<char> d((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    const auto u32 = [&d](std::size_t o) {
        std::uint32_t v = 0;
        for (int k = 3; k >= 0; --k)
            v = (v << 8) | static_cast<unsigned char>(d[o + static_cast<std::size_t>(k)]);
        return v;
    };
    for (int k = 0; k < w.saveSlots(); ++k) {
        const std::size_t s = w.savesHeader() + w.saveSlot() * static_cast<std::size_t>(k);
        if (s + 108 > d.size()) break;
        SaveEntry e;
        e.slot = k;
        for (std::size_t i = 0; i < 32 && d[s + i]; ++i) e.name.push_back(d[s + i]);
        e.day  = u32(s + 32);
        e.time = u32(s + 36);
        out.push_back(std::move(e));
    }
    return out;
}

int saveProfiles(const std::vector<SaveEntry>& dir) {
    std::vector<std::string> seen;
    for (const auto& e : dir)
        if (!e.name.empty() &&
            std::find(seen.begin(), seen.end(), e.name) == seen.end())
            seen.push_back(e.name);
    return static_cast<int>(seen.size());
}

LoadPanelState loadPanelFor(int profiles, const UiWidgets& w) {
    LoadPanelState s;
    s.empty = profiles == 0;
    s.focus = s.empty ? 1 : 0;
    s.mode  = s.empty ? 3 : 0;
    s.slotListHidden = s.empty;
    const auto& L = w.loadPanel();
    const auto* panel = w.at(L.panel);
    if (!panel) return s;
    for (const auto& l : panel->lists) {
        if (l.addr != L.buttonList) continue;
        for (const auto& it : l.items) {
            bool live = true;
            // "Nouvelle partie" is always hidden on screen 29
            if (it.addr == L.nouvelle) live = false;
            if (s.empty && (it.addr == L.charger || it.addr == L.detruire))
                live = false;
            s.buttons.push_back({it.addr, live});
        }
    }
    return s;
}

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets.h"

#include "script/savefile.h"

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
        panel.flagsB = static_cast<std::uint32_t>(p["flagsB"].i64(0));
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
                item.drawFn  = static_cast<std::uint32_t>(it["drawFn"].i64(0));
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
        // ...AND THE SCREEN'S OWN FLAG WORD, for the one bit that decides
        // whether the WORLD is drawn behind it. `UI_LoadScreen` (0x00429BB0):
        //
        //     mov eax, [ebx+70h]        ; the screen record's +112
        //     and eax, 40000h
        //     test eax, eax
        //     jnz  skip                 ; set -> the world stays live
        //     ...
        //     call sub_466B30           ; else: world OFF, sounds suspended,
        //                               ; and the player's +194 goes to 9
        //
        // and `Ui_CloseScreenDefault` (0x0042A150) calls the partner
        // `sub_466B60` to bring it back. Exactly THREE screens carry the bit -
        // PAUSE GAME, SHOOT MECA and SHOOT HUMAN - which are the three that
        // must show the live world behind them; every other screen, the sneak
        // included, turns it off (`docs/UI.md`).
        // ...and the record's `param`, which `UI_LoadScreen` copies into the
        // live slot's `+4` whenever it is not -1:
        //
        //     mov [ebx+4], ecx          ; the caller's argument first
        //     mov eax, [esi+8]          ; ...then the record's own param
        //     cmp eax, -1
        //     jz  short keep
        //     mov [ebx+4], eax          ; which OVERRIDES it
        //
        // and `sub_49BC60` branches on that slot field. It is what tells the
        // slider page which of its two meanings it has: screen 7 SLIDER
        // carries **1** and screen 9 SNEAK carries **0**, so the same page
        // TRAVELS when it is opened from inside the vehicle and CALLS one
        // when it is opened from the device.
        screenParam_[id] = static_cast<int>(ss[i]["param"].i64(-1));
        const Json& fl = ss[i]["flags"];
        std::uint32_t f = 0;
        for (std::size_t k = 0; k < fl.size(); ++k)
            f |= static_cast<std::uint32_t>(fl[k].i64(0));
        screenFlags_[id] = f;
    }
}

// `UI_LoadScreen`'s `and eax, 40000h` - see loadScreens. A screen the table
// does not know keeps the world, which is the safer default for a harness
// that opens a screen by hand.
bool UiWidgets::worldBehind(int screenId) const {
    const auto it = screenFlags_.find(screenId);
    return it == screenFlags_.end() || (it->second & 0x40000u) != 0;
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

const std::string& UiWidgets::soundNameById(int id) const {
    static const std::string none;
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

// THE ENGINE HAS ONE PANEL RECORD PER ADDRESS, and the lift can carry two.
// A panel reached both as a screen's top panel and as some item's `+44` child
// appears twice - the sneak's inventory page is 0x004DEE50 under screen 9 AND
// under four sibling pages - and only the screen-keyed row carries the
// `current` its open callback writes. Returning whichever came first meant
// `installPanel(kPanelSneakInventory)` (the tail of a use, and of leaving the
// examine page) landed on a record whose `current` is -1, so `settle` fell
// back to the first usable list - the tab column - and the highlight left the
// rows. Prefer the informative one.
//
// **BUT `current` BELONGS TO THE SCREEN WHOSE CALLBACK WROTE IT**, and taking
// it anywhere else is how the sneak's SLIDER page lost its navigation.
// `panel+24` is runtime state: an open callback writes it when ITS screen
// opens, and nothing else does. 0x004DEDE8 is both the sneak's slider page,
// entered as a child of the tab column, and **screen 7's own top panel**, the
// journey screen - so the lift carries `screen -1, current -1` for the first
// and `screen 7, current 1` for the second. Preferring "informative" without
// asking whose it is made a descent from the sneak adopt screen 7's entry
// list: the walk started on the HEADER instead of the tab column, and
// `sub_49D4D0`'s transitions - which all read "-> N, if N is selectable" from
// the list you are ON - then refused every step, so the player could not
// leave the column at all (`verify.py: sneak page colour` 9..12, and the
// fault `a48593f` was written to fix in the first place).
//
// So the screen-keyed row wins only for the screen it belongs to. A caller
// with no screen in hand, and a record whose `screen` is -1, are unchanged:
// this narrows the preference, it does not remove it.
//
// **AND A CHILD IS SHOWN WITH ITS SCREEN'S LISTS** (2026-09-14). A child is
// lifted once, as `screen -1`, carrying the open-callback edits of whichever
// screen reached it first - for the shops' Vente confirm and Analyser page
// that is screen 20, the BANK, so opening either from the pharmacy re-titled
// the whole screen "Banque - vente", lit Vente and hid Acheter. The engine
// has ONE record per list address: `Ui_OpenShop` writes it when a shop
// opens, and `sub_42A370` installing a child rewrites nothing. So a child
// that shares list addresses with the current screen's own top panel takes
// those lists from THAT screen's record, as a cached merged copy; and the
// screen's own record for an address wins outright, which the shop panel's
// own `-1` duplicate (reached through the Analyser box's `+44`) had been
// shadowing on the way back.
const UiPanel* UiWidgets::at(std::uint32_t addr, int screen) const {
    const UiPanel* own = nullptr;
    const UiPanel* preferred = nullptr;
    const UiPanel* first = nullptr;
    for (const auto& p : panels_) {
        if (p.addr != addr) continue;
        if (screen >= 0 && p.screen == screen) { own = &p; break; }
        if (!preferred && p.current >= 0 && p.screen < 0) preferred = &p;
        if (!first) first = &p;
    }
    if (own) return own;
    const UiPanel* base = preferred ? preferred : first;
    if (!base || base->screen >= 0 || screen < 0) return base;
    const UiPanel* top = this->screen(screen);
    if (!top || top->addr == addr) return base;
    bool shares = false;
    for (const auto& l : base->lists)
        for (const auto& t : top->lists)
            if (l.addr == t.addr) shares = true;
    if (!shares) return base;
    const auto key = std::make_pair(addr, screen);
    auto it = merged_.find(key);
    if (it == merged_.end()) {
        UiPanel m = *base;
        for (auto& l : m.lists)
            for (const auto& t : top->lists)
                if (l.addr == t.addr) l = t;
        it = merged_.emplace(key, std::move(m)).first;
    }
    return &it->second;
}

// ...and one LIST by its record address, wherever in the tree it lives. The
// sneak's row list is shared by five panels, and a caller on the verb or
// examine page has to reach the widgets of a list its own panel does not own.
const UiList* UiWidgets::listAt(std::uint32_t addr) const {
    for (const auto& p : panels_)
        for (const auto& l : p.lists) if (l.addr == addr) return &l;
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
            if (j >= 0) selMap()[r.addr] = j;
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

bool UiWalk::installPanel(std::uint32_t addr) {
    const auto* kid = w_->at(addr, screen_);
    if (!kid) return false;
    if (panel_) leavePage(*panel_);
    panel_ = kid;
    buildPage(*panel_);
    settle();
    log_.push_back("install panel");
    return true;
}

bool UiWalk::toParent() {
    if (!panel_) return false;
    if (!panel_->parent) {                 // the top panel: the screen closes
        panel_ = nullptr;
        log_.push_back("close");
        return true;
    }
    const auto* up = w_->at(panel_->parent, screen_);
    if (!up) {
        approx_ = true;
        log_.push_back("parent panel not in the table");
        return true;
    }
    leavePage(*panel_);
    panel_ = up;
    buildPage(*panel_);
    settle();
    log_.push_back("back");
    return true;
}

bool UiWalk::pickable(const UiList& l, const UiItem& it) const {
    return it.selectable(l.broadcast) && !itemOff(it.addr);
}

// `sub_42AAE0(list, window)`, and the SECOND ARGUMENT IS THE WINDOW - the
// first row the nine widgets show. `count` is the list's own `+24`, what the
// inventory channel reports (case 29).
//
//     for each widget k of the list:
//       if (k + window >= count) { item+0x3C = -1;                 // past the end
//                                  set 0x40000001 and 0x20000004; }  // not drawn,
//       else                     { item+0x3C = k + window;           // not selectable
//                                  clear both; }
void UiWalk::bindRows(std::uint32_t list, int count, int window) {
    state_->bound[list] = count;
    if (!panel_) return;
    for (const auto& l : panel_->lists) {
        if (l.addr != list) continue;
        if (window < 0) window = 0;
        // ...over the list's WIDGET COUNT, which is `word_4DE6F0` and not the
        // nine items the static record carries: the memory page's builder
        // sets it to 5. A widget past the count is not a row at all.
        const int widgets = (list == kListSneakRows)
            ? std::min<int>(static_cast<int>(l.items.size()), state_->rowWidgets)
            : static_cast<int>(l.items.size());
        for (std::size_t k = 0; k < l.items.size(); ++k) {
            const int row = static_cast<int>(k) + window;
            const bool live = row < count && static_cast<int>(k) < widgets;
            state_->rowTag[l.items[k].addr] = live ? row : -1;
            if (live) state_->itemOff.erase(l.items[k].addr);
            else      state_->itemOff.insert(l.items[k].addr);
        }
        // ...and if the selection was left on a widget that has just gone
        // away, pull it back to the last live one. `sub_42AAE0` cannot leave
        // the highlight past the end and neither may this. NOTE this clamps
        // to the WIDGET count now, not to the row count: with a window the
        // two are different numbers and clamping to the rows walked the
        // selection off the end of a scrolled list.
        const int live = std::min<int>(widgets, std::max(0, count - window));
        auto it = selMap().find(l.addr);
        if (it != selMap().end() && it->second >= live)
            it->second = live > 0 ? live - 1 : 0;
        return;
    }
}

// The comments on the declarations carry the evidence.
void UiWalk::beginCombine(int row, bool isSpellItem) {
    state_->combining = true;
    if (isSpellItem) { state_->combineA = row; state_->combineB = -1; }
    else             { state_->combineA = -1;       state_->combineB = row; }
    state_->combineC = -1;
    // `sub_49BF30`'s tail, and the port had only the first line of it:
    //     sub_4290D0(&word_4DE318, 0x20000004, 1);  // the VERBS off
    //     sub_4290D0(&word_4DE6F0, 0x20000004, 0);  // the ROWS back ON
    //     sub_428FF0(&unk_4DE278, 0x40000002, 1);   // light `Utiliser sur`
    //     dword_4DEED0 = 3;                          // panel+24: the ROWS
    // The player picks the second object WITHOUT leaving the panel, which is
    // what makes the mode usable at all - and leaving it is the cancel.
    setListOff(kListSneakVerbs, true);
    setListOff(kListSneakRows, false);
    state_->flagOn[kItemSneakUseOn] |= 0x40000002u;
    curFromBuilder_ = 3;
    settle();
    log_.push_back(isSpellItem ? "combine: opened with the spell item"
                               : "combine: opened");
}

bool UiWalk::takeCombine(int& a, int& b) {
    if (state_->readyA < 0 || state_->readyB < 0) return false;
    a = state_->readyA; b = state_->readyB;
    state_->readyA = state_->readyB = -1;
    return true;
}

void UiWalk::endCombine() {
    state_->combining = false;
    state_->combineA = state_->combineB = state_->combineC = -1;
    setListOff(kListSneakVerbs, false);
    // `sub_42A370(screen, unk_4DEE50)` - the inventory page comes back
    // whichever way the combine went.
    installPanel(kPanelSneakInventory);
    // ...and `sub_428FF0(&unk_4DE278, 0x40000002, 0)` right after it
    // (`loc_49BE51`): `Utiliser sur`'s FLASH goes off. `beginCombine` lit it
    // and nothing put it out, so the verb went on blinking after the combine
    // as if it were still the selection - a reader: *"the menu selection
    // should go back to the items list (instead of staying in the verb
    // sections)"*.
    state_->flagOn[kItemSneakUseOn] &= ~0x40000002u;
}

int UiWalk::rowWindow(std::uint32_t list) const {
    if (!panel_) return 0;
    for (const auto& l : panel_->lists) {
        if (l.addr != list || l.items.empty()) continue;
        const int t = rowOf(l.items[0].addr);
        return t < 0 ? 0 : t;
    }
    return 0;
}

// `sub_42AFF0` - `Ui_MoveSelection` OVER A WINDOW, and the whole of the
// sneak's row scrolling. Read from the listing 2026-09-04; before that the
// window was hardcoded 0 and a list longer than its nine widgets was
// truncated, so a tenth carried object could not be reached at all.
//
// It is a CENTRED window: the selection moves until it reaches the middle
// widget and then the window moves instead.
//
//     widgets = list+0,  sel = list+2,  count = list+24,  base = item[0]+0x3C
//     half    = widgets / 2
//
//     item[0]   .0x100000 = count > widgets && sel_item+0x3C > half
//     item[last].0x200000 = count > widgets && sel_item+0x3C < count-half-1
//
//     lastBound = the highest widget whose +0x3C is not -1, plus base
//
//     UP   : base > 0 && sel <= half  ->  bindRows(list, count, base - 1)
//            else if (sel_item+0x3C >  base)      --sel
//     DOWN : lastBound != count-1 && sel >= half  ->  bindRows(.., base + 1)
//            else if (sel_item+0x3C <  lastBound) ++sel
//
// and on ANY successful move it raises event 30 with the selected row's tag
// (`sub_4083F0(0x1E, &tag)`, guarded on the tag not being -1) - the inventory
// channel's 3D-preview load. So the preview follows the cursor because the
// MOVER fires it; a move that does not is a move that leaves it stale.
//
// -> whether anything moved, which is `sub_49C050`'s `!= 1` gate.
bool UiWalk::moveRowWindow(const UiList& l, std::uint32_t bits) {
    const int widgets = static_cast<int>(l.items.size());
    if (widgets <= 0) return false;
    const int count = boundCount(l.addr);
    const int half  = widgets / 2;
    const int base  = rowWindow(l.addr);
    auto& sel = selMap()[l.addr];
    if (sel < 0) sel = 0;
    if (sel >= widgets) sel = widgets - 1;
    const int selRow = rowOf(l.items[static_cast<std::size_t>(sel)].addr);

    // the two marks, on the first and last WIDGET
    const auto mark = [&](std::size_t k, std::uint32_t bit, bool on) {
        auto& f = state_->rowArrow[l.items[k].addr];
        f = on ? (f | bit) : (f & ~bit);
    };
    const bool longer = count > widgets;
    mark(0, 0x100000u, longer && selRow > half);
    mark(l.items.size() - 1, 0x200000u,
         longer && selRow < count - half - 1);

    // the highest widget still bound, as an absolute row
    int k = widgets - 1;
    while (k > 0 && rowOf(l.items[static_cast<std::size_t>(k)].addr) < 0) --k;
    const int lastBound = k + base;

    bool moved = false;
    if (bits & kUiUp) {
        if (base > 0 && sel <= half) {
            bindRows(l.addr, count, base - 1);
            moved = true;
        } else if (selRow > base) {
            --sel;
            moved = true;
        }
    } else if (bits & kUiDown) {
        if (lastBound != count - 1 && sel >= half) {
            bindRows(l.addr, count, base + 1);
            moved = true;
        } else if (selRow >= 0 && selRow < lastBound) {
            ++sel;
            moved = true;
        }
    }
    return moved;
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
    // **A LIST IS SHARED, AND ITS SELECTION IS LIVE.** `selMap()` used to be
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
    // A BUILDER'S `panel+24` WINS. `buildPage` runs before this and two of
    // the sneak's panels write their current list from code rather than from
    // the lifted `current` (the verb panel 2, the examine page 2), so a
    // wholesale reset here would throw both away.
    //
    // ...and it has to win OUTRIGHT, which it did not until 2026-09-07: the
    // value was assigned and then the move-rule fallback below overwrote it,
    // because that branch was reached for every `cur_` other than 0. The
    // verb panel survived by luck - its builder switches off all four of the
    // other lists, so the first usable one IS the verb list - and the EXAMINE
    // page did not: it switches off only the verbs, so the fallback landed on
    // the tab column at index 0 and the page's own text list was never
    // current. Nothing on that page then answered UP or DOWN, which is what a
    // player reported as "the scrolling of long text does not work".
    // `sub_49B950`'s own instruction is `mov dword_4DEF38, 2` - panel
    // 0x004DEF20 `+24`, list index 2, the scrolling text box.
    bool fromBuilder = false;
    if (curFromBuilder_ >= 0) {
        cur_ = curFromBuilder_;
        curFromBuilder_ = -1;
        fromBuilder = true;
    } else {
        cur_ = 0;
    }
    if (!panel_) return;
    if (fromBuilder &&
        static_cast<std::size_t>(cur_) >= panel_->lists.size())
        fromBuilder = false;                  // a value the panel cannot hold
    if (!fromBuilder) {
        if (cur_ == 0 && panel_->current >= 0 &&
            static_cast<std::size_t>(panel_->current) < panel_->lists.size()) {
            cur_ = panel_->current;
        } else {
            for (std::size_t k = 0; k < panel_->lists.size(); ++k)
                if (usable(panel_->lists[k])) { cur_ = static_cast<int>(k); break; }
        }
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
        // An open callback's write WINS - that is the engine overwriting the
        // record. Anything else only seeds a list this walk has not met, so a
        // selection the player left behind survives.
        // ...EXCEPT on a CHILD, for a list it shares with its screen's own top
        // panel: `sub_42A370` installing a child rewrites no record, so the
        // shared list's `+2` is whatever the player left there. Re-seeding it
        // put a shop's buttons back on the lifted value the moment its
        // Analyser page opened (the header then read *Vente* in a pharmacy).
        bool sharedWithTop = false;
        if (panel_->screen < 0)
            if (const UiPanel* top = w_->screen(screen_))
                for (const auto& t : top->lists)
                    if (t.addr == l.addr) sharedWithTop = true;
        if (!sharedWithTop && l.select >= 0 &&
            static_cast<std::size_t>(l.select) < l.items.size())
            selMap()[l.addr] = j;
        else
            selMap().emplace(l.addr, j);
        // ...AND A REMEMBERED SELECTION THAT IS NO LONGER PICKABLE MOVES.
        //
        // A list's selection outlives the screen (`list+2` is never reset),
        // and one LIST can serve two screens whose builders hide different
        // items. The load panel's button list is exactly that: `Charger une
        // partie` is live on screen 29 and hidden on screen 30, so a player
        // who used it on the start menu and then opened a save point had the
        // save panel sitting on a hidden `Charger` - and confirming it LOADED
        // instead of saving. A reader met that as "I pressed enter on a save
        // slot then the menu closed".
        //
        // `Ui_MoveSelection` steps over unselectable rows, so a selection can
        // never REACH one by moving; this is the same rule applied to one
        // arriving from somewhere else.
        const auto cur = selMap().find(l.addr);
        if (cur != selMap().end() && cur->second >= 0 &&
            static_cast<std::size_t>(cur->second) < l.items.size() &&
            !pickable(l, l.items[static_cast<std::size_t>(cur->second)])) {
            for (std::size_t i = 0; i < l.items.size(); ++i)
                if (pickable(l, l.items[i])) { cur->second = static_cast<int>(i); break; }
        }
    }
}

// `sub_4296B0` (0x004296B0) - the item colour setter, three bytes:
//
//     mov [eax+8], cl / mov [eax+9], dl / mov [eax+0Ah], cl
//
// Three call sites, all three the same write: the sneak's clock item
// (0x004DEC08) back to black after the list it sits in has been recoloured.
void UiWalk::colourItem(std::uint32_t item, int r, int g, int b) {
    state_->colour[item] = {r, g, b};
}

// `sub_4296D0` (0x004296D0) - the same three bytes over EVERY item of a list.
// The count is the list's own `+0` and the item array its `+12`, which is the
// same pair `sub_42AAE0` walks; the loop is `jnz` on a decremented count, so a
// list of zero writes nothing.
void UiWalk::multiplanColour(const UiPanel& p) {
    for (const auto& l : p.lists) {
        if (l.addr != kListMultiplanButtons || l.items.empty()) continue;
        const auto it = selMap().find(l.addr);
        int sel = it != selMap().end() ? it->second : 0;
        if (sel < 0 || static_cast<std::size_t>(sel) >= l.items.size()) sel = 0;
        const UiItem& b = l.items[static_cast<std::size_t>(sel)];
        colourList(kListMultiplanRows,   b.rgb[0], b.rgb[1], b.rgb[2]);
        colourList(kListMultiplanHeader, b.rgb[0], b.rgb[1], b.rgb[2]);
        break;
    }
}

void UiWalk::identitySwap(int sel) {
    if (sel == 0) {
        state_->itemOff.erase(kItemSneakIdentityText);
        state_->itemOff.insert(kItemSneakCharacteristics);
    } else if (sel == 1) {
        state_->itemOff.insert(kItemSneakIdentityText);
        state_->itemOff.erase(kItemSneakCharacteristics);
    }
}

void UiWalk::multiplanSetSource(int src) {
    state_->multiplanSource = src;                  // dword_68A610
    selMap()[kListMultiplanRows] = 0;               // sub_42ADD0's a2 = 0
    bindRows(kListMultiplanRows, boundCount(kListMultiplanRows), 0);
    log_.push_back(src ? "multiplan: source = the kiosk (list 1)"
                       : "multiplan: source = the sneak (list 0)");
}

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
void UiWalk::setListOff(std::uint32_t list, bool off) {
    if (off) state_->listOff.insert(list);
    else     state_->listOff.erase(list);
}

// `sub_428FF0(item, 0x40000001, on)` over ONE item - the same bit, one widget
// at a time. Both directions are needed and they are not symmetrical here:
// the drawer skips an item whose RECORD carries the bit unless something has
// switched it on, and skips one the runtime switched off whatever the record
// says, so a builder that means "drawn" has to say both.
void UiWalk::setItemOff(std::uint32_t item, bool off) {
    if (off) { state_->itemOff.insert(item); state_->itemShown.erase(item); }
    else     { state_->itemOff.erase(item);  state_->itemShown.insert(item); }
}

// THE PANEL'S `+8`, run on the way OUT. `sub_42A370(screen, panel)` calls the
// OLD panel's `+8` and then the new panel's `+4`, so a page that disabled
// something on the way in is expected to re-enable it on the way out - and
// `sub_49B810` / `sub_49B8A0` are exact mirrors, which is what says the
// records persist rather than being rebuilt.
//
//     sub_49B8A0   verbs NOT selectable; tabs, previews and rows selectable;
//                  clear 0x40000002 over the verb list; unlight the row
//     sub_49B9E0   clear 0x40000002 on item 0x004DE2C0
//
// The two lit flags are read and not modelled - this port has no runtime bit
// to put them in - so what is ported is the selectability, which is what a
// walk can feel.
void UiWalk::leavePage(const UiPanel& p) {
    // `sub_49B9E0` - the examine page's `+8`: `sub_428FF0(0x004DE2C0,
    // 0x40000002, 0)`, the exact mirror of what its `+4` set.
    if (p.addr == kPanelSneakExamine)
        state_->flagOn[kItemSneakExamine] &= ~0x40000002u;
    if (p.addr == kPanelSneakVerbs) {
        setListOff(kListSneakVerbs, true);
        setListOff(kListSneakTabs, false);
        setListOff(kListSneakPreviews, false);
        setListOff(kListSneakRows, false);
        // ...AND IT CANCELS AN OPEN COMBINE. `sub_49B8A0`'s tail:
        //
        //     if (dword_670BE0) { dword_670BE0 = 0;
        //                         670BE4 = 670BE8 = 670BEC = -1; }
        //
        // Leaving the verb panel is what ends `Utiliser sur` when the player
        // does not follow it through, and without it the mode is a ONE-WAY
        // DOOR: `combining` never clears, so every later row confirm feeds a
        // dead combine instead of opening the verbs, and the verb bar is
        // unreachable for the rest of the process. `combining` lives in the
        // same static record as the selections and the colours, so it
        // survives closing and reopening the device too. A player hit exactly
        // that within minutes of the mode landing.
        if (state_->combining) {
            state_->combining = false;
            state_->combineA = state_->combineB = state_->combineC = -1;
            state_->readyA = state_->readyB = -1;
            log_.push_back("combine: cancelled by leaving the verb panel");
        }
    }
}

void UiWalk::buildPage(const UiPanel& p) {
    // NOTHING IS CLEARED HERE. `+8/+9/+10` and the flag bits are fields of
    // STATIC records: a builder writes the ones it means to write, and every
    // other keeps what the last builder left. The verb panel has no colour of
    // its own, so the inventory page's must simply still be there - clearing
    // first is what made the device go back to its (255, 0, 0) placeholder
    // the moment an object was chosen.
    // `sub_47A050`, the confirm dialog's own `+4`, and its FIRST act:
    //
    //     mov ecx, 8; xor eax, eax; mov edi, offset byte_69BDA0; rep stosd
    //     mov dword_657994, ebx
    //
    // - 32 bytes of the name buffer zeroed and the cursor reset, every time
    // the panel is installed. So cancelling and coming back gives an EMPTY
    // field, not the name typed a moment ago; without this the second visit
    // opens with the first visit's text and `Confirmer`'s empty-field gate
    // cannot be reached again.
    if (p.addr == kPanelStartConfirm) { name_.clear(); nameCursor_ = 0; }
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
    if (p.addr == kPanelSneakInventory) {
        state_->rowKind = 0;              // `mov dword_670CB8, 0`
        state_->rowWidgets = 9;           // `mov word_4DE6F0, 9`
        setListOff(kListSneakRows, false);
        setListOff(kListSneakVerbs, true);
    }
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
    // `0x0049D750`, THE MEMORY PAGE'S BUILDER, transcribed:
    //
    //     if ([screen+0x20] != off_4DEFF0) panel+0x18 = 0;  // the TAB COLUMN,
    //                                                       // unless we came
    //                                                       // back from the
    //                                                       // READER page
    //     word_4DE6F0 = 5;                  // FIVE widgets here, not nine
    //     dword_670CB8 = 2;                 // rows from object list 2
    //     sub_42ADD0(0x004DE6F0, 0, 2);
    //     if (current == rows && dword_4DE708 > 0)
    //         dword_4DEAD4 = selected(rows)[+0x3C];    // the body box's TAG
    //     else if (dword_4DE708 == 0) dword_4DEAD4 = -1;
    //     sub_428FF0(0x004DEA98, 0x40000001, 1);       // ...and HIDE the box
    //
    // The last line is the one this port had BACKWARDS: the body is not shown
    // when the page opens. The panel hook lights it once the current list is
    // the rows, so the memo text appears when the player moves INTO the list.
    // THE MEMO READER `0x004DEFF0`. Its record ships `+24 = 2` and NOTHING
    // writes it - `sub_42A370` installs a panel without touching `+24`, and
    // the page has no builder instruction for it - so the lift carries
    // `current: -1` (it records only a callback's write) and the walk would
    // fall back to the move rule and land on the tab column. The shipped
    // value is the answer, exactly as it is for the shops' two children and
    // MULTIPLAN's: index 2 is `0x004DEAE8`, the body box, whose hook is the
    // scroller - which is the whole purpose of this page.
    if (p.addr == kPanelSneakReader) {
        state_->rowKind = 2;
        curFromBuilder_ = 2;
        return;
    }
    if (p.addr == kPanelSneakMemory) {
        state_->rowKind = 2;
        state_->rowWidgets = 5;
        // The builder's closing `sub_428FF0(0x004DEA98, 0x40000001, 1)` needs
        // nothing here: `memoBodyShown()` derives the flag from the current
        // list, and the builder has just put that back on the tab column, so
        // the box is hidden exactly as the instruction leaves it.
    }
    // The IDENTITY page's builder `0x0049C100`:
    //     sub_428FF0(0x004DE810, 0x40000001, 0);   word_4DE902 = 0;
    //     sub_428FF0(0x004DE858, 0x40000001, 1);   the echo bar's colour
    // - Identite drawn, Caracteristiques off, and the tab row back on its
    // first tab every time the page is entered. It does not write `+24`.
    if (p.addr == kPanelSneakIdentity) {
        selMap()[kListSneakIdentity] = 0;
        identitySwap(0);
    }
    if (p.addr == kPanelSneakVerbs) {
        setListOff(kListSneakVerbs, false);
        setListOff(kListSneakTabs, true);
        setListOff(kListSneakPreviews, true);
        setListOff(kListSneakRows, true);
        // `dword_4DEED0 = 2` - panel 0x004DEEB8 `+24`, the CURRENT LIST, and
        // index 2 of its five is the verb list. The builders write it and the
        // walker lifts it for the pages that have one; these two panels are
        // not in that lift because they are not reached through an item.
        curFromBuilder_ = 2;
    }
    // `sub_49B950`, the EXAMINE page's builder. It disables the verb list and
    // sets `0x40000002` on item 0x004DE2C0 - "Examiner" itself - so the verb
    // you chose stays lit while its page is up. It then reads the row's tag,
    // asks `sub_42B330` for the object's KIND and sends kind 5 to
    // `sub_478EF0`, which is the document path; and it plays interface sound
    // 8. None of those three is modelled here - this is the navigation only.
    if (p.addr == kPanelSneakExamine) {
        setListOff(kListSneakVerbs, true);
        // `sub_428FF0(0x004DE2C0, 0x40000002, 1)` - "Examiner" itself, so it
        // FLASHES while its page is up. `Ui_ItemTextStyle` reads bank B `0x2`
        // as "blink on oscillator 1 when selected", and bank C `0x1` on the
        // same item forces the colour to white - which is exactly what a
        // player described: "Examiner should flash in white when previewing
        // an object".
        state_->flagOn[kItemSneakExamine] |= 0x40000002u;
        // `dword_4DEF38 = 2` - panel 0x004DEF20 `+24`, and index 2 of its
        // four is 0x004DE760, the page's own model item. Without it the walk
        // falls back to the first usable list, which is the tab column, and
        // the highlight jumps to the identity icon - which a player saw as
        // "the selection goes to the character page button".
        curFromBuilder_ = 2;
    }
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
        state_->rowKind = 4;              // `mov dword_670CB8, 4`
        state_->rowWidgets = 9;           // `mov word_4DE6F0, 9`
        // `sub_49D170`'s `cmp [arg0+4], 1`: the live slot's +4, which is the
        // screen's own `param` - 1 from INSIDE the vehicle (screen 7), 0 from
        // the device (screen 9). Arm 1 hides "Appel du slider" and shows
        // "Automatique" + "Manuelle" with the selection on 1; the else arm
        // is the reverse. Both were recorded as "the other state is
        // unreachable" until the screen that reaches it was read.
        const bool aboard = w_->screenParam(screen_) == 1;
        if (aboard) {
            state_->itemOff.insert(kItemSliderCall);
            state_->itemOff.erase(kItemSliderAuto);
            state_->itemOff.erase(kItemSliderManual);
        } else {
            state_->itemOff.erase(kItemSliderCall);
            state_->itemOff.insert(kItemSliderAuto);
            state_->itemOff.insert(kItemSliderManual);
        }
    }
    // The page's own tab: the column item whose `child` is this panel.
    const UiItem* icon = nullptr;
    // `Ui_OpenShop` (0x004AE540), THE TEN SHOPS' ONE OPEN. After the title
    // and the buy/sell flag arms it paints the rows and the header in the
    // colour of the SELECTED BUTTON:
    //
    //     movsx eax, word_4E3372          // list 0x004E3370 +2, the selection
    //     mov   ecx, off_4E337C           // ...that list's item array
    //     mov   eax, [ecx+eax*4]          // the selected button
    //     sub_4296D0(0x004E3640, [eax+8], [eax+9], [eax+10])   the nine rows
    //     sub_4296D0(0x004E38E0, ...)                          the header
    //
    // and each arm writes that selection first - 1 at the bank, 0 in the
    // other nine - so a shop wears "Acheter"'s (150, 215, 250) and the bank
    // "Vente"'s (20, 165, 250). The ten screens share these ADDRESSES and
    // carry their own lifted `select`, so it is read off this panel's copy.
    // THE SHOP'S TWO CHILDREN come up on the list their RECORD names.
    // `sub_42A370` (the installer) saves the old panel, runs its leave hook,
    // makes the new one current and runs the new one's `+4` builder - and
    // never writes the new panel's `+24`. Neither child has a builder and
    // nothing moves between their lists, so the shipped `+24` stands on every
    // visit: 1, the Oui/Non row on the Vente confirm and the box on the
    // Examiner page. The walk's move-rule fallback would put both on the
    // buttons, where ENTER reaches a callback that answers nothing.
    if (p.addr == kPanelShopSellConfirm || p.addr == kPanelShopExamine) {
        curFromBuilder_ = 1;
        return;
    }
    // ---- THE HINT SHOP's two builders, transcribed ------------------------
    //
    // `sub_4AE120` (0x004E3018's `+4`) and `sub_4AE3A0` (0x004E3080's) are
    // the same shape with different numbers, and between them they write
    // every field the two pages differ in. Neither clears anything it does
    // not name, so a flag one sets stands until the other clears it - which
    // is how `Acheter`'s "Indice achete !" survives from the confirm back
    // onto the shop if you leave it up.
    //
    //   sub_4AE120                              sub_4AE3A0
    //     dword_6A17C0 = event 42 (the price)     -
    //     word_4E2D0C  = 6                        word_4E2D0C = 6
    //     body  0x40000080 -> 0                   body  0x40000080 -> 1
    //     body  0x40000001 -> 0  (drawn)          body  0x40000001 -> 1 (not)
    //     word_4E2B2E  = 0                        word_4E2B2E = 0
    //     word_4E2B12  = 250                      word_4E2B12 = 180
    //     price 0x40000001 -> 0                   list 0x004E2C18 -> 0 (both
    //     foot  0x40000001 -> 0                       buttons drawn)
    //     word_4E2CF2  = 400                      foot  0x40000001 -> 0
    //     done  0x40000001 -> 1                   word_4E2CF2 = 290
    //                                             panel+24 = 0
    //     word_4E2AF0 = 5; sub_42ADD0(rows, 0, 2)  ...the same two lines
    //     dword_4E2B4C = rows ? selected(rows)+0x3C : -1   ...the same
    //     if (!rows) panel+24 = 1                          ...the same
    //
    // `panel+24 = 1` is index 1 of either panel's lists, and on BOTH that is
    // 0x004E2B60, the body box - so with no hints at all the focus skips the
    // row list entirely and ENTER lands on `0x004AE260`, which rebuilds the
    // page. That is the whole of the no-hints state, and it is why the page
    // is still navigable when it has nothing to sell.
    if (p.addr == kPanelHints || p.addr == kPanelHintBuy) {
        const bool buy = p.addr == kPanelHintBuy;
        hintFooter_  = 6;
        hintSection_ = 0;
        hintBodyY_   = buy ? 180 : 250;
        hintFooterY_ = buy ? 290 : 400;
        setItemOff(kItemHintBody, buy);          // 0x40000001 on the body
        setItemOff(kItemHintFoot, false);
        if (buy) {
            // `sub_429140(&word_4E2C18, 0x40000001, 0)` - over the LIST, so
            // both buttons. The shop's own builder never touches them, which
            // is why they have to be switched back on here after `Acheter`
            // hid them.
            for (const auto& l : p.lists) {
                if (l.addr != kListHintBuy) continue;
                for (const auto& it : l.items) setItemOff(it.addr, false);
            }
        } else {
            setItemOff(kItemHintPrice, false);
            setItemOff(kItemHintDone, true);     // `Indice achete !` hidden
        }
        // `sub_42ADD0(&word_4E2AF0, 0, 2)`: open object list 2, five widgets,
        // selection back to 0, then the count from event 29. `hintRows_` is
        // that count, supplied by the caller (see `setHintRows`).
        //
        // The CONFIRM panel does not carry the row list - the shop's four
        // lists are rows / body / footer / backdrop and the confirm's five
        // are buttons / body / price / footer / backdrop - but the engine
        // binds it anyway, because `sub_42ADD0` takes the list RECORD and
        // not the panel. `bindRows` walks the current panel's lists, so on
        // the confirm it sets the count and the selection and leaves the
        // five widgets' tags alone; they already hold the same values the
        // shop's own build wrote, since both calls use window 0 and the same
        // count.
        selMap()[kListHintRows] = 0;
        bindRows(kListHintRows, hintRows_, 0);
        hintBodyRow_ = hintRows_ > 0 ? 0 : -1;
        if (hintRows_ <= 0) curFromBuilder_ = 1;   // `[panel+24] = 1`
        else if (buy)       curFromBuilder_ = 0;   // `mov [esi+18h], 0`
        return;
    }
    // MULTIPLAN's two children the same way: both records ship `+24` = 1
    // (the examine page's box, the destroy confirm's Oui/Non row), and
    // `sub_42A370` does not write it.
    if (p.addr == kPanelMultiplanExamine || p.addr == kPanelMultiplanDestroy) {
        curFromBuilder_ = 1;
        return;
    }
    // `sub_4B01F0`, MULTIPLAN's open, paints the rows and the header in the
    // SELECTED BUTTON's colour (`off_4E53AC[word_4E53A2] +8/+9/+10`) - the
    // shops' trick on another screen - and the button list's hook repaints
    // them on every move (`multiplanColour`).
    if (p.addr == kPanelMultiplan) {
        multiplanColour(p);
        return;
    }
    if (p.addr == kPanelShop) {
        for (const auto& l : p.lists) {
            if (l.addr != kListShopButtons) continue;
            if (l.select >= 0 && static_cast<std::size_t>(l.select) < l.items.size()) {
                const UiItem& b = l.items[static_cast<std::size_t>(l.select)];
                colourList(kListShopRows,   b.rgb[0], b.rgb[1], b.rgb[2]);
                colourList(kListShopHeader, b.rgb[0], b.rgb[1], b.rgb[2]);
            }
            break;
        }
        return;
    }
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
    screen_ = screenId;
    panel_ = w_->screen(screenId);
    approx_ = false;
    answer_ = -1;
    log_.clear();
    if (!panel_) { log_.push_back("no panel for this screen"); return false; }
    log_.push_back("open");
    // MULTIPLAN's open (`sub_4B01F0`) writes the source list, `dword_68A610 =
    // 0`, before it binds the rows: every visit starts on the sneak.
    if (panel_->addr == kPanelMultiplan) state_->multiplanSource = 0;
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
    const auto it = selMap().find(l.addr);
    return it == selMap().end() ? -1 : it->second;
}

// The verbs' own first four instructions - see the header. -1 when nothing is
// selected, when the widget is past the end of the list (the binder writes -1
// there) or when the rows have never been bound.
int UiWalk::selectedRow(std::uint32_t listAddr) const {
    const int sel = selectionOf(listAddr);
    if (sel < 0) return -1;
    const UiList* l = w_->listAt(listAddr);
    if (!l || static_cast<std::size_t>(sel) >= l->items.size()) return -1;
    return rowOf(l->items[static_cast<std::size_t>(sel)].addr);
}

int UiWalk::selection() const {
    const auto* l = curList();
    if (!l) return -1;
    const auto it = selMap().find(l->addr);
    return it == selMap().end() ? -1 : it->second;
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
        int j = selMap()[l.addr];
        for (int t = 0; t < n; ++t) {              // skip unselectable items
            j += step;
            if (j < 0)       j = l.noWrap() ? 0 : n - 1;
            else if (j >= n) j = l.noWrap() ? n - 1 : 0;
            if (pickable(l, l.items[static_cast<std::size_t>(j)])) break;
        }
        selMap()[l.addr] = j;
        log_.push_back("move");
        return true;
    }
    if (bits & kUiConfirm) return confirm();
    return false;
}

// `sub_4AFBE0` - DEN'S LOCKER, transcribed from the image. Four digit wheels
// and two arrow sprites on ONE hook, which both moves and answers: the screen
// has no item callback anywhere.
//
//     UP     digit == 0 -> 9, else digit - 1        (it wraps)
//     DOWN   digit >= 9 -> 0, else digit + 1
//     LEFT   the wheel under the hand, if it is not the first
//     RIGHT  ...if it is not the fourth (`[list+2] < 3`)
//
// A wheel shows its digit by its UNLIT SOURCE: the hook writes `digit * 46`
// into `+0x12`, which cuts a different figure out of the artwork's strip at
// x = 0 - and the four wheels' authored unlit source is (0, 0), digit zero.
//
// THE COMBINATION IS IN THE CODE: `dword_4E47E4 == 7 && dword_4E482C == 2 &&
// dword_4E4874 == 1 && dword_4E48BC == 3`, and those four addresses are the
// `+3C` of the four wheel items (0x4E47A8, 0x4E47F0, 0x4E4838, 0x4E4880, 0x48
// apart). So the locker opens on **7 2 1 3**, and it opens the moment the last
// wheel lands - there is no confirm. It then lights all four, plays interface
// sound 0x22 and writes the ANSWER 1.
//
// AND THE HAND IS THE LIST'S OWN SELECTION, which is what draws the wheels.
// `[esi+2]` - the field left/right move - is the list's `+2`, the row
// `Ui_DrawList` marks UIF_SELECTED, and `Ui_DrawItemSprite`'s last rung is
// "lit = SELECTED". The four wheels carry bank B `0x40000100` and none of
// `8/4/2`, so the selected wheel draws its LIT source - the item's own place
// in the artwork, which is the empty display - and every other wheel draws
// the UNLIT one, the digit. A hook that kept the hand in a private field
// left the selection at 0 for ever, and the first wheel never showed a digit
// at all: three figures where the game shows four. Seen in a rendered frame,
// not reasoned about.
//
// Beside it the hook moves a PULSE: `sub_428FF0(item, 0x40000084, 0)` on the
// wheel the hand leaves and `(.., 1)` on the one it lands on, so bank B `0x4`
// - `Ui_Oscillator(1)`, a 500 ms square wave - makes that wheel's digit
// blink. On the combination it sets `0x4` and clears `0x80` on ALL four, so
// they all blink together, and paints them `+8 = 0`, `+9 = 255`.
//
// NOT ported, labelled: the two sounds (`sub_482D90(0x22)`) and the 2000 ms
// oscillator 5 the success arm starts on the screen.
bool UiWalk::denDial(const UiList& l, std::uint32_t bits) {
    const int wasWheel = denWheel_, wasDigit = denDigit_[denWheel_ & 3];
    int& d = denDigit_[denWheel_ & 3];
    if (bits & kUiUp)         d = (d == 0) ? 9 : d - 1;
    else if (bits & kUiDown)  d = (d >= 9) ? 0 : d + 1;
    else if (bits & kUiLeft)  { if (denWheel_ != 0) --denWheel_; }
    else if (bits & kUiRight) { if (denWheel_ < 3)  ++denWheel_; }
    const bool moved = denWheel_ != wasWheel || denDigit_[denWheel_ & 3] != wasDigit;
    if (moved)
        log_.push_back("den locker: wheel " + std::to_string(denWheel_) + " = " +
                       std::to_string(denDigit_[denWheel_ & 3]));
    // the hand, expressed where the drawer reads it - and the pulse with it
    selMap()[l.addr] = denWheel_;
    for (std::size_t k = 0; k < 4 && k < l.items.size(); ++k) {
        std::uint32_t& f = state_->flagOn[l.items[k].addr];
        if (static_cast<int>(k) == denWheel_) f |= 0x40000084u;
        else                                  f &= ~0x40000084u;
    }
    if (denDigit_[0] == 7 && denDigit_[1] == 2 && denDigit_[2] == 1 && denDigit_[3] == 3) {
        if (answer_ != 1) log_.push_back("den locker: 7 2 1 3 - it opens");
        // `push esi/40000004h` then `push ebx/40000080h` on each of the four,
        // and `byte+8 = 0`, `byte+9 = 255` beside them
        for (std::size_t k = 0; k < 4 && k < l.items.size(); ++k) {
            std::uint32_t& f = state_->flagOn[l.items[k].addr];
            f |=  0x40000004u;
            f &= ~0x40000080u;
            // only `+8` and `+9` are written; `+10` keeps the record's
            state_->colour[l.items[k].addr] = {0, 255, l.items[k].rgb[2]};
        }
        answer_ = 1;
    }
    return moved;
}

// `sub_4AFE90` - THE GANDHAR DOOR'S CURSOR, transcribed from the image (no
// `proc` label of its own). The screen is a 6x6 grid of symbols with ONE
// selectable item walking it; the four items behind it are the MARKERS a press
// stamps.
//
//     UP     row != 0 -> row - 1        LEFT   col != 0 -> col - 1
//     DOWN   row <  5 -> row + 1        RIGHT  col <  5 -> col + 1
//
// then the cursor's x/y are rewritten `col * 63 + 135` and `row * 63 + 61` -
// which are the item's own authored (135, 61), so the tree places the grid's
// origin - and its `+3C` becomes `(row << 16) | col`. The hook returns 1 only
// when the cell moved, so a confirm falls through to the item's callback.
bool UiWalk::gandhar(const UiList& l, std::uint32_t bits) {
    (void)l;
    const int wasCol = gandCol_, wasRow = gandRow_;
    if (bits & kUiUp)         { if (gandRow_ != 0) --gandRow_; }
    else if (bits & kUiDown)  { if (gandRow_ < 5)  ++gandRow_; }
    else if (bits & kUiLeft)  { if (gandCol_ != 0) --gandCol_; }
    else if (bits & kUiRight) { if (gandCol_ < 5)  ++gandCol_; }
    const bool moved = gandCol_ != wasCol || gandRow_ != wasRow;
    if (moved)
        log_.push_back("gandhar: cell " + std::to_string(gandRow_) + "," +
                       std::to_string(gandCol_));
    if (bits & kUiConfirm) return confirm();
    return moved;
}

// `sub_4AF300` - THE TERMINAL FAMILY'S KEYPAD, transcribed from the image (it
// has no `proc` label of its own in the decompilation's index). Eleven items:
// 0..8 are the 3x3 pad, 9 the `0` cell under it and 10 the big button at
// (528, 398). The selection is the list's own word and the hook returns 1 only
// when it MOVED - so a confirm falls through to `Ui_ConfirmSelection` and the
// item's callback, which is `kCbTerminalCell`.
//
//     UP     10 -> 9; 9 -> 7; else nothing on the top row (n/3 == 0), else -3
//     DOWN   n/3 == 2 -> 9; 9 -> 10; 10 -> nothing; else +3
//     LEFT   n >= 9 nothing; else wrap inside the row: row*3 + (n + 2) % 3
//     RIGHT  n >= 9 nothing; else row*3 + (n + 1) % 3
//
// Without this the list's hook was unmodelled, the walk refused every press on
// it, and the terminal and the fight simulator could be opened and never used.
bool UiWalk::keypad(const UiList& l, std::uint32_t bits) {
    int n = selMap()[l.addr];
    if (n < 0) n = 0;
    const int before = n;
    const int row = n / 3;
    if (bits & kUiUp) {
        if (n == 10)      n = 9;
        else if (n == 9)  n = 7;
        else if (row != 0) n -= 3;
    } else if (bits & kUiDown) {
        if (n < 9 && row == 2) n = 9;
        else if (n == 9)       n = 10;
        else if (n != 10)      n += 3;
    } else if (bits & kUiLeft) {
        if (n < 9) n = row * 3 + (n + 2) % 3;
    } else if (bits & kUiRight) {
        if (n < 9) n = row * 3 + (n + 1) % 3;
    }
    if (n >= static_cast<int>(l.items.size())) n = before;
    selMap()[l.addr] = n;
    if (n != before) log_.push_back("keypad move");
    if (bits & kUiConfirm) return confirm();
    return n != before;
}

bool UiWalk::grid(const UiList& l, std::uint32_t bits) {
    int n = selMap()[l.addr];
    const int before = n;
    if (bits & kUiUp)          n = n < 3 ? 6 : (n == 6 ? 4 : n - 3);
    else if (bits & kUiDown)   n = n < 3 ? n + 3 : (n == 6 ? 1 : 6);
    else if (bits & kUiLeft)  { if (n != 6) n = (n % 3) ? n - 1 : n + 2; }
    else if (bits & kUiRight) { if (n != 6) n = (n % 3 == 2) ? n - 2 : n + 1; }
    selMap()[l.addr] = n;
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
    // SEEDED, not replayed. `f.enter(name_)` put the caret back at the end of
    // the buffer every frame, so BACKSPACE always deleted the last character
    // and the arrows' caret was thrown away between frames - and it is the
    // same buffer and the same `dword_657994` the field's DRAWER reads.
    f.seed(name_, nameCursor_);
    bool ate = false;
    for (char c : text) ate = f.type(c) || ate;
    name_ = f.text();
    nameCursor_ = f.cursor();
    // Case 13, RETURN: `mov ecx,[eax+1Ch]; mov dword ptr [ecx+18h], 1` -
    // `panel+24 = 1`, and list 1 of the dialog's four is the BUTTONS. The
    // hook refuses it on an empty buffer, which `NameField::type` already
    // models, so `done()` is only true when the engine would have moved.
    if (f.done()) { cur_ = 1; log_.push_back("focus list 1"); }
    return ate;
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
        if (nf && !nf->hidden() && selMap()[l->addr] == 0 && (bits & kUiUp)) {
            cur_ = 0; log_.push_back("focus list 0"); return true;
        }
    }
    return false;
}

bool UiWalk::confirm() {
    const auto* it = selected();
    if (!it) return false;
    if (it->callback) {
        // `sub_47A370` - ANNULER. Its whole body is
        // `sub_42A370(screen, screen->panel->parent)`, so it is the back bit
        // reached through a row: the dialog closes onto the start menu and
        // the frame is consumed. It is the only callback in the tree with
        // that body (`verify.py: start cancel`), so it is named by address
        // and not pattern-matched at run time.
        if (it->callback == kCbStartCancel) return toParent();
        // ---- THE TERMINAL FAMILY (`docs/UI.md` "a second `Ui_OpenShop`") --
        //
        // One callback, 0x004AF410, for seven screens - TERMINAL, FIGHT SIM,
        // MORGUE, ARCHIVES and the three SURV - switching on the screen's own
        // fixed parameter through the jump table at 0x004AF578. The parameters
        // are 0..6 with no gap and no repeat over exactly those seven, so the
        // case index IS the screen; the table is `docs/UI.md`'s, read there and
        // transcribed here:
        //
        //     1 FIGHT SIM   row + 1 for rows 0..2 (and interface sound 26)
        //     2 MORGUE      row + 1 for rows 0..4
        //     3 ARCHIVES    1, and only from row 2
        //     4 SURV ERROR  falls THROUGH into case 6 - `loc_4AF52F` ends in a
        //                   call with no jump, which a table read as seven
        //                   independent arms would miss
        //     6 SURV KIT    1, and only from row 0
        //     5 SURV NO KIT no answer at all
        //     0 TERMINAL    nothing here: its answer is written by 0x004AF0E0,
        //                   which is NOT ported - it answers a pair of booleans
        //                   (`2 + (dword_68A5FC != 0)` on one branch, the bare
        //                   test on the other, so 0..3) off a global this case
        //                   sets when row 3 or 4 is chosen. Left unported and
        //                   labelled rather than guessed, so the terminal shows
        //                   its dossiers and does not yet answer its script.
        // `sub_4AFF90` - THE GANDHAR DOOR'S PRESS, transcribed from the image.
        // It counts the press (`byte_68A608`), stamps a marker widget taken
        // from `off_4E4C80[count]` at the cell, and ORs one bit of
        // `byte_68A60C` when the cell is one of FOUR:
        //
        //     +3C == 5       -> bit 1   (row 0, col 5)
        //     +3C == 0x10003 -> bit 2   (row 1, col 3)
        //     +3C == 0x40002 -> bit 4   (row 4, col 2)
        //     +3C == 0x50004 -> bit 8   (row 5, col 4)
        //
        // At four presses it plays interface sound 0x26; when the mask reaches
        // 0x0F it writes the ANSWER 1 - the door opens - and plays 0x27. The
        // bits are ORed, so the symbols may be pressed in any order and the
        // same one twice does not count twice. NOT ported: the marker widgets'
        // placement (the viewer draws the cursor, not the stamps) and the two
        // sounds.
        // ---- XACHEN'S CARTRIDGES (`sub_4AF9D0`), read from the image ----
        //
        // Screen 14 is Dakobah's puzzle in front of Xendar's door: four
        // BUTTONS on the ordinary left/right mover (`0x0042A930`), and above
        // them four SYMBOL widgets in a list flagged `0x20000004` so nothing
        // can select them. A button's `+3C` is a POINTER to its symbol -
        // written by the screen's open callback, not by the record - and the
        // symbol's own `+3C` is its value.
        //
        // A press steps that value along `dword_4E42E8`, a fourteen-entry
        // RING that is not in numerical order, and writes the new symbol's
        // 51x23 cell into the widget's LIT source (`+0x0C`/`+0x0E`). The
        // symbols carry bank B `0x8`, always lit, so that source is what
        // draws; the buttons carry none of `8/4/2`, so the SELECTED button
        // draws `Xanoir1.bmp`'s (0, 0) and the other three draw their own
        // place, which is the plain artwork.
        //
        // When all four match `unk_4E4320` - 10, 14, 7, 9 - the hook lights
        // every button (lit source (0, 23) and `0x40000008` on each), plays
        // interface sound 0x2B, writes the ANSWER 1 and starts a 4000 ms
        // oscillator 5 on the screen. AREA 58's zone 1130 'Cartouches' is
        // what opens it, and on `Interface == 1` it opens the Xendar door and
        // plays the Dakobah/Xendar conversation (DIALOGS 227).
        //
        // NOT ported, labelled: the sound and the 4000 ms timer.
        if (it->callback == kCbXachenCartridge) {
            const UiList* l = curList();
            int idx = -1;
            if (l)
                for (std::size_t k = 0; k < l->items.size(); ++k)
                    if (l->items[k].addr == it->addr) idx = static_cast<int>(k);
            if (idx < 0 || idx > 3) return false;
            int& v = xachen_[idx];
            int at = -1;
            for (int k = 0; k < 14; ++k)
                if (kXachenRing[k] == v) { at = k; break; }
            const int next = kXachenRing[(at + 1) % 14];
            // `cmp eax, 0Eh / jg` and `cmp eax, 0 / jle` - the ring holds only
            // 1..14, so this can never refuse, and it is transcribed because
            // it is what makes 0 and 15 mean "no symbol" in the sprite table.
            if (next > 0 && next <= 14) v = next;
            log_.push_back("xachen: cartridge " + std::to_string(idx) + " = " +
                           std::to_string(v));
            if (xachenSolved()) {
                answer_ = 1;
                log_.push_back("xachen: 10 14 7 9 - the door opens");
            }
            return true;
        }
        if (it->callback == kCbGandharCell) {
            const unsigned cell = (unsigned(gandRow_) << 16) | unsigned(gandCol_);
            ++gandPresses_;
            unsigned bit = 0;
            if (cell == 0x5u)          bit = 1;
            else if (cell == 0x10003u) bit = 2;
            else if (cell == 0x40002u) bit = 4;
            else if (cell == 0x50004u) bit = 8;
            gandMask_ |= bit;
            // THE MARKER. `off_4E4C80[count]` is the widget this press stamps:
            // its x/y are written from the cell the same way the cursor's are,
            // and `sub_428FF0(marker, 0x40000001, 0)` clears the not-drawn bit
            // its record ships with. The table is the list's own items after
            // the cursor - four markers for four presses - and beyond the
            // fourth the engine walks off the end of it, which this does not
            // follow: it stamps nothing.
            if (const UiList* gl = curList()) {
                const std::size_t mk = static_cast<std::size_t>(gandPresses_);
                if (mk < gl->items.size() && gandPresses_ <= 4) {
                    state_->itemShown.insert(gl->items[mk].addr);
                    gandStamps_.push_back({gandCol_, gandRow_});
                }
            }
            log_.push_back("gandhar: press " + std::to_string(gandPresses_) +
                           (bit ? " - one of the four" : " - not in the code"));
            if (gandMask_ == 0x0Fu) {
                answer_ = 1;
                log_.push_back("gandhar: the door opens");
            }
            return true;
        }
        if (it->callback == kCbTerminalCell) {
            // the CELL's index in its own list - `Ui_ConfirmSelection` hands
            // the callback the item, and the engine's arms test the list's
            // selection word, not a row tag
            const int row = selection();
            int a = -1;
            // case 0, the TERMINAL: no answer here - it REMEMBERS which
            // protected dossier was opened and reports it on close (above)
            if (screen_ == 5) {
                if (row == 3) termRow3_ = true;
                if (row == 4) termRow4_ = true;
                if (row == 3 || row == 4)
                    log_.push_back("terminal: dossier " + std::to_string(row + 1) + " opened");
                return row == 3 || row == 4;
            }
            switch (screen_) {
                case 11: if (row >= 0 && row <= 2) a = row + 1; break;
                case 19: if (row >= 0 && row <= 4) a = row + 1; break;
                case 18: if (row == 2) a = 1; break;
                case 15: case 17: if (row == 0) a = 1; break;
                default: break;
            }
            if (a < 0) { log_.push_back("terminal cell: no answer"); return false; }
            answer_ = a;
            log_.push_back("terminal cell: answer");
            return true;
        }
        // THE SHOP'S THREE BUTTONS - Acheter, Vente, Examiner (0x004AED00,
        // 0x004AED30, 0x004AED60), one body each:
        //
        //     if (screen->panel == 0x004E3970 && dword_4E3658 > 0)
        //         { panel+24 = 1; return 1; }
        //     return 0;
        //
        // `dword_4E3658` is the rows list's `+24`, the count the binder
        // wrote, so confirming a button moves the focus into the stock - and
        // does nothing over an empty one (a bank visited with nothing to
        // sell). Which arm a row then takes is `word_4E3372`, the button
        // list's selection, which the confirm leaves where it is.
        if (it->callback == kCbShopBuy || it->callback == kCbShopSell ||
            it->callback == kCbShopExamine) {
            if (panel_->addr != kPanelShop || boundCount(kListShopRows) <= 0) {
                log_.push_back("shop button: nothing to choose");
                return false;                     // the callback's own `return 0`
            }
            for (std::size_t k = 0; k < panel_->lists.size(); ++k)
                if (panel_->lists[k].addr == kListShopRows) { cur_ = static_cast<int>(k); break; }
            log_.push_back("shop button: the rows take the focus");
            return true;
        }
        // MULTIPLAN's four buttons (0x004B0760 / 07E0 / 0860 / 0890). The two
        // transfer buttons first switch the source when it differs -
        //     "vers le multiplan": if (dword_68A610) { = 0; rebind }
        //     "vers le sneak":     if (dword_68A610 != 1) { = 1; rebind }
        // - and all four then
        //     if (screen->panel == 0x004E5930 && dword_4E5688 > 0)
        //         { panel+24 = 1; return 1; }
        //     return 0;
        if (it->callback == kCbMultiplanToKiosk || it->callback == kCbMultiplanToSneak ||
            it->callback == kCbMultiplanExamine || it->callback == kCbMultiplanDestroy) {
            if (panel_->addr != kPanelMultiplan) return false;
            if (it->callback == kCbMultiplanToKiosk && state_->multiplanSource != 0)
                multiplanSetSource(0);
            if (it->callback == kCbMultiplanToSneak && state_->multiplanSource != 1)
                multiplanSetSource(1);
            if (boundCount(kListMultiplanRows) <= 0) return false;
            focusList(kListMultiplanRows);
            log_.push_back("multiplan button: the rows take the focus");
            return true;
        }
        // THE STOCK ROW (0x004AEAA0) - one callback for the nine rows, and it
        // switches on the BUTTON list's selection `word_4E3372`:
        //
        //     0 Acheter:  tag = row+0x3C; if (tag == -1) return 0;
        //                 sub_42B3E0(tag) (event 41) refused -> sound 18;
        //                 else event 38 -> message 8/9 + sound 18, or rebind,
        //                 message 21 "Objet achete !" + sound 16. Stays here.
        //     1 Vente:    sub_42A370(screen, 0x004E3A40)   the confirm
        //     2 Examiner: tag == -1 -> 0; sub_42A370(screen, 0x004E39D8);
        //                 dword_4E393C = tag - which nothing in the image reads
        //
        // The purchase ends in `Game_HandleEvent`, which the walk does not
        // own, so it is RECORDED for the caller (`takeShop`) as the verbs are.
        if (it->callback == kCbShopRow) {
            const int tag = rowOf(it->addr);
            int button = -1;
            if (const UiList* b = w_->listAt(kListShopButtons)) button = selectionOf(*b);
            if (button == 1) {
                log_.push_back("shop row: Vente -> the confirm");
                return installPanel(kPanelShopSellConfirm);
            }
            if (tag < 0) return false;
            if (button == 0) {
                state_->pendingShopKind = 0;
                state_->pendingShopRow  = tag;
                log_.push_back("shop row: Acheter -> events 41, 38");
                return true;
            }
            if (button == 2) {
                log_.push_back("shop row: Examiner -> the page");
                return installPanel(kPanelShopExamine);
            }
            return false;
        }
        // MULTIPLAN's ROW (0x004B05C0) - a jump table on the BUTTON list's
        // selection `word_4E53A2`:
        //
        //     0 vers le multiplan: tag = row+0x3C; if (tag == -1) return 0;
        //         event 36 request 7; 1 -> sub_42ADD0(rows, -1, -1), and
        //         `if (!dword_4E5688) panel+24 = 0`, message 8; else message 7
        //     1 vers le sneak:     the same with request 8; refusal message 6
        //     2 Examiner / 3 Detruire: the child pages (step 4)
        //
        // Both transfers end in `Game_HandleEvent`, so they are RECORDED for
        // the caller (`takeMultiplan`), as the shop's purchase is.
        if (it->callback == kCbMultiplanRow) {
            const int tag = rowOf(it->addr);
            int button = -1;
            if (const UiList* b = w_->listAt(kListMultiplanButtons)) button = selectionOf(*b);
            if ((button == 0 || button == 1) && tag >= 0) {
                state_->pendingMultiplanRequest = button == 0 ? 7 : 8;
                state_->pendingMultiplanRow = tag;
                log_.push_back(button == 0 ? "multiplan row: event 36 request 7"
                                           : "multiplan row: event 36 request 8");
                return true;
            }
            // 2: `dword_4E581C = tag` (the page's box item's own +0x3C),
            // `sub_42A370(screen, 0x004E5998)`, `sub_42B420(tag, 4)` - event
            // 30 then 43, which posts world MESSAGE 4 with the object. The
            // message is the caller's (`takeMultiplan`, request 4).
            if (button == 2 && tag >= 0) {
                state_->pendingMultiplanRequest = 4;
                state_->pendingMultiplanRow = tag;
                // the page's builder `0x004B0510` opens `mov dword_6A5090, 0`:
                // a new object is read from the top
                state_->textScroll = 0;
                log_.push_back("multiplan row: Examiner -> the page, message 4");
                return installPanel(kPanelMultiplanExamine);
            }
            // 3: `sub_42A370(screen, 0x004E5A00)` - no tag test on this arm.
            if (button == 3) {
                log_.push_back("multiplan row: Detruire -> the confirm");
                return installPanel(kPanelMultiplanDestroy);
            }
            return false;
        }
        // THE DESTROY CONFIRM's two answers (0x004B08C0 / 0x004B0980):
        //
        //     Oui: tag = sub_428EF0(0x004E5670)+0x3C   the SELECTED row's tag
        //          if (dword_68A610 != 1)  message 4             (only from the kiosk)
        //          else if (tag != -1) {
        //              event 36 request 6;
        //              1 -> sub_42ADD0(rows, -1, -1); if (!dword_4E5688) panel+24 = 0
        //              else message 4 "Impossible de detruire cet objet"
        //          }
        //          sub_42A370(screen, 0x004E5930); return 0
        //     Non: sub_42A370(screen, 0x004E5930); return 1
        //
        // Request 6 ends in `Game_HandleEvent`, so Oui is RECORDED for the
        // caller (request 6, the tag - or -1, which the caller still has to
        // see because the source test comes first). As on the shop's
        // confirm, `sub_42A370` leaves the parent's +24 alone, so the kiosk
        // comes back on its rows.
        if (it->callback == kCbMultiplanDestroyYes || it->callback == kCbMultiplanDestroyNo) {
            if (it->callback == kCbMultiplanDestroyYes) {
                int tag = -1;
                if (const UiList* r = w_->listAt(kListMultiplanRows)) {
                    const int s = selectionOf(*r);
                    if (s >= 0 && static_cast<std::size_t>(s) < r->items.size())
                        tag = rowOf(r->items[static_cast<std::size_t>(s)].addr);
                }
                state_->pendingMultiplanRequest = 6;
                state_->pendingMultiplanRow = tag;
                log_.push_back("multiplan confirm: Oui -> event 36 request 6");
            } else {
                log_.push_back("multiplan confirm: Non");
            }
            const bool back = installPanel(kPanelMultiplan);
            if (back) focusList(kListMultiplanRows);
            return back;
        }
        // `Oui` on the Vente confirm (0x004AEC00): the SELECTED STOCK ROW's tag
        // (`sub_428EF0(0x004E3640)+0x3C`), the sale through event 36 request
        // 10, the price and event 39 - recorded for the caller - and then the
        // shop panel again, whatever the channel answered.
        if (it->callback == kCbShopSellYes || it->callback == kCbShopSellNo) {
            if (it->callback == kCbShopSellYes) {
                int tag = -1;
                if (const UiList* r = w_->listAt(kListShopRows)) {
                    const int s = selectionOf(*r);
                    if (s >= 0 && static_cast<std::size_t>(s) < r->items.size())
                        tag = rowOf(r->items[static_cast<std::size_t>(s)].addr);
                }
                if (tag >= 0) {
                    state_->pendingShopKind = 1;
                    state_->pendingShopRow  = tag;
                }
                log_.push_back("shop confirm: Oui -> events 36, 39");
            } else {
                log_.push_back("shop confirm: Non");
            }
            // ...and the shop panel comes back on the ROWS. `sub_42A370` never
            // writes the incoming panel's `+24`, which still holds the 1 the
            // button's confirm wrote before Vente was chosen; only the sale
            // clears it, and only when the list is left empty (the caller's
            // `focusList`). `installPanel` re-settles on the lifted `current`,
            // the open callback's 0, so the rows are put back here.
            const bool back = installPanel(kPanelShop);
            if (back) focusList(kListShopRows);
            return back;
        }
        // `Charger une partie` (0x0047AC90).  It does NOT load: it resolves
        // the row to a slot, refuses an empty one, stores the index in
        // `dword_4C09B4` and closes the screen (`screen[+8] = 3`).  The load
        // happens later, at the top of the next script pump - `sub_408410`,
        // one-shot, with a 15-frame fade in from white (GAME_STATE 8).  So
        // the walk's job is to record the request and close; the caller
        // consumes it.
        // `Sauvegarde` on screen 30 (0x004AE060). It installs the shared slot
        // panel - except when the player has NO RINGS, where it installs
        // 0x004E2FB0 instead, which is the screen's own
        // `Je n'ai pas assez d'Anneaux pour faire ca !`:
        //
        //     if (screen[+78] && sub_42B1C0(5) == 0)
        //         sub_42A370(screen, off_4E2FB0);   // the refusal
        //     else
        //         sub_42A370(screen, off_4CF2E8);   // the slots
        //
        // `sub_42B1C0(5)` is the anneaux - property 5, the same one the save
        // point's script tests before it opens this screen at all. So a save
        // is refused twice over, once in the world and once here.
        if (it->callback == kCbSaveSauvegarde) {
            if (rings_ == 0) {
                log_.push_back("sauvegarde: no anneaux -> the refusal panel");
                return installPanel(kPanelSaveNoRings);
            }
            if (load_) load_->mode = 1;        // `word_4CEA9A = 1`, saving
            return installPanel(kPanelLoadSlots);
        }
        // ---- THE HINT SHOP's three callbacks ------------------------
        //
        // A ROW (0x004AE220). Six instructions, and the refusal does NOT
        // install a panel the way `Sauvegarde`'s does - it rewrites the
        // footer item's string id in place and returns, so the page stays
        // put with `Je n'ai pas assez d'Anneaux pour faire ca !` under it.
        //
        //     push 5; call sub_42B1C0        ; Actor_GetProperty(5)
        //     mov  ecx, dword_6A17C0         ; the price
        //     cmp  eax, ecx
        //     jge  loc_4AE243
        //       mov word_4E2D0C, 8 ; mov eax, 1 ; retn
        //     loc_4AE243:
        //       sub_42A370(screen, off_4E3080) ; mov eax, 1 ; retn
        if (it->callback == kCbHintRow) {
            if (rings_ < hintPrice_) {
                hintFooter_ = 8;
                log_.push_back("indices: " + std::to_string(rings_) +
                               " anneaux against a price of " +
                               std::to_string(hintPrice_) + " - refused");
                return true;
            }
            return installPanel(kPanelHintBuy);
        }
        // THE BODY BOX (0x004AE260) - `sub_42A370(screen, off_4E3018)` then
        // `sub_42ADD0(&word_4E2AF0, 0, 2)`, which `buildPage` does for it.
        if (it->callback == kCbHintBody) return installPanel(kPanelHints);
        // `Acheter` (0x004AE480). The event is the PAYMENT and its refusal
        // arm is a plain `return 1` with nothing changed, so a purchase the
        // player cannot afford leaves the confirm exactly as it was.
        if (it->callback == kCbHintBuy) {
            if (!panel_) return false;
            if (rings_ < hintPrice_) {
                log_.push_back("indices: the purchase is refused - " +
                               std::to_string(rings_) + " anneaux");
                return true;
            }
            rings_ -= hintPrice_;          // `u16(player + 174) -= price`
            hintPaid_ = hintPrice_;        // ...for the caller to write back
            hintSection_ = 1;              // `word_4E2B2E = 1` - THE CLUE
            for (const auto& l : panel_->lists) {
                if (l.addr != kListHintBuy) continue;
                for (const auto& b : l.items) setItemOff(b.addr, true);
            }
            setItemOff(kItemHintBody, false);
            setItemOff(kItemHintFoot, true);
            setItemOff(kItemHintPrice, true);
            setItemOff(kItemHintDone, false);
            cur_ = 1;                      // `[screen->panel + 0x18] = 1`
            log_.push_back("indices: bought for " + std::to_string(hintPrice_) +
                           ", " + std::to_string(rings_) + " anneaux left");
            return true;
        }
        // `Annuler` on the save screen (0x0042A990): it closes.
        if (it->callback == kCbSaveAnnuler) {
            log_.push_back("annuler: the screen closes");
            panel_ = nullptr;
            return true;
        }
        // `Detruire` (0x0047AE90): it refuses a row outside 0..count and
        // otherwise opens its own confirm.
        if (it->callback == kCbLoadDetruire) {
            if (!load_) return false;
            const int n = static_cast<int>(load_->rows().size());
            if (load_->row < 0 || load_->row >= n) {
                log_.push_back("detruire: no row");
                return true;
            }
            return installPanel(kPanelLoadDestroy);
        }
        // ...and that confirm's `Oui` (0x0047B800): ONE slot cleared, through
        // the listed profile. Not the profile-wide `SaveDir_Delete`.
        if (it->callback == kCbDestroyYes) {
            if (!load_) return false;
            const auto rows = load_->rows();
            if (load_->row < 0 || load_->row >= static_cast<int>(rows.size())) {
                log_.push_back("detruire: no row to clear");
                return true;
            }
            pendingClear_ = rows[static_cast<std::size_t>(load_->row)].slot;
            log_.push_back("detruire: clearing slot " + std::to_string(pendingClear_));
            return installPanel(kPanelLoadSlots);      // back to the list
        }
        // The confirm panel's `Oui` (0x0047BA30), the save arm. The delete
        // arm - screen 29's `Detruire` - is read and not modelled: it calls
        // `SaveDir_Delete`, which empties every slot of a profile, and that
        // is not a thing to wire on a guess.
        if (it->callback == kCbConfirmYes) {
            if (!load_ || !panel_) return false;
            // THE SCREEN, not the panel's - the confirm and the slot panel
            // are both CHILD panels and carry `screen == -1`, so testing the
            // panel sent every confirm down the delete arm and `Oui` did
            // nothing. `sub_47BA30` tests `*a1`, the screen the walk was
            // opened with.
            if (screen_ != 30) {
                approx_ = true;
                log_.push_back("oui: the DELETE arm is not modelled");
                return true;
            }
            const auto rows = load_->rows();
            const int row = overwriteRow_ >= 0 ? overwriteRow_ : load_->row;
            if (row < 0 || row >= static_cast<int>(rows.size())) {
                log_.push_back("oui: no row to overwrite");
                return true;
            }
            pendingSave_ = rows[static_cast<std::size_t>(row)].slot;
            overwriteRow_ = -1;
            log_.push_back("oui: overwriting slot " + std::to_string(pendingSave_));
            panel_ = nullptr;
            return true;
        }
        // ---- SCREEN 31, `PAUSE GAME` ---------------------------------
        //
        // Four callbacks, each four instructions, all read out of the image
        // at the addresses the widget tree names (widgets.h has the bytes).
        // None is a guess and none needs state this walk does not own.
        if (it->callback == kCbPauseResume) {
            log_.push_back("pause: reprendre le jeu");
            panel_ = nullptr;                  // `mov [screen+8], 3`
            return true;
        }
        if (it->callback == kCbPauseQuit) {
            log_.push_back("pause: quitter le jeu -> the confirm");
            return installPanel(kPanelPauseConfirm);
        }
        if (it->callback == kCbPauseQuitNo) {
            // `sub_42A370(screen, off_4E26C8)`. NOT `toParent()`: the confirm
            // panel's `+0` is 0, so the back bit closes the screen and only
            // this callback returns to the pause page.
            log_.push_back("pause: non -> back to the pause page");
            return installPanel(kPanelPause);
        }
        if (it->callback == kCbPauseQuitYes) {
            // `sub_409090()` - `mov dword_4E6C9C, 1` - and then the close.
            // The caller serves the request between pumps.
            quitRequest_ = true;
            log_.push_back("pause: oui -> quit requested (dword_4E6C9C)");
            panel_ = nullptr;                  // `mov [screen+8], 3`
            return true;
        }
        // THE SNEAK'S QUIT TAB (todo/sneak.md §5d), three callbacks:
        //
        //     0x0049DBF0  sub_428FF0(0x004DEBA0, 0x40000001, 0)  show Oui/Non
        //                 word_4DEBA2 = 1                        on "Non"
        //                 screen->panel[+24] = 1                 its list 1
        //     0x0049DBC0  sub_428FF0(0x004DEBA0, 0x40000001, 1)  hide it
        //                 screen->panel[+24] = 0
        //     0x0049DBA0  sub_409090() then screen[+8] = 3       the request
        //
        // `+24` is written on WHATEVER PANEL IS CURRENT - the icon's own page
        // `0x004DF0C0` is never installed (`Ui_ConfirmSelection` prefers a
        // callback over a `+44` child, and nothing else installs it), and the
        // Oui/Non list belongs to that page alone, so on the inventory page
        // this shows a list nothing draws and moves the focus to the page's
        // own list 1. Transcribed as it is, including that.
        // `Lire plan`, the third inventory tile - `sub_42A370(screen,
        // off_4DF190)`, seven instructions, and then the PANEL'S OWN `+4`
        // decides whether the page survives. `sub_49D9E0` bounces straight
        // back to `0x004DEE50` when `Images\<set>.bmp` cannot be opened, so
        // the tile does nothing at all outside the four cities - which is not
        // a refusal the callback makes, but one its page makes on arrival.
        //
        // `setCityMap` is what the caller has resolved: whether the bitmap
        // opened. Without it the walk cannot know, so it installs the page and
        // says the bitmap was not tested rather than guessing either way.
        if (it->callback == kCbSneakMapOpen) {
            if (!installPanel(kPanelSneakMap)) return false;
            if (!cityMapKnown_) {
                approx_ = true;
                log_.push_back("sneak map: installed, the bitmap untested");
                return true;
            }
            if (!cityMapAvailable_) {
                log_.push_back("sneak map: no Images bitmap - bounced back");
                return installPanel(kPanelSneakInventory);
            }
            log_.push_back("sneak map: the city map page");
            return true;
        }
        // `0x0049BC30`, the ANNEAUX tile: `mov eax, 1; retn`. It consumes the
        // confirm so `Ui_ConfirmSelection` does not descend into the item's
        // `+44`, and does nothing else. Transcribed BECAUSE it is inert - the
        // default below would descend, which is the behaviour it exists to
        // prevent.
        if (it->callback == kCbSneakRingsInert) {
            log_.push_back("sneak: anneaux tile - inert (mov eax,1; retn)");
            return true;
        }
        if (it->callback == kCbSneakQuitShow) {
            setListOff(kListSneakQuit, false);
            selMap()[kListSneakQuit] = 1;                  // `Non`
            if (panel_ && panel_->lists.size() > 1) cur_ = 1;
            log_.push_back("sneak quit: Oui/Non shown, Non selected");
            return true;
        }
        if (it->callback == kCbSneakQuitNo) {
            setListOff(kListSneakQuit, true);
            cur_ = 0;
            log_.push_back("sneak quit: Non - the pair hidden");
            return true;
        }
        if (it->callback == kCbSneakQuitYes) {
            quitRequest_ = true;
            log_.push_back("sneak quit: Oui -> quit requested (dword_4E6C9C)");
            panel_ = nullptr;                  // `mov [screen+8], 3`
            return true;
        }
        // `Sauvegarde` on the slot panel (0x0047ADB0), read whole:
        //
        //     if (row < dword_657968) { sub_42A370(screen, off_4CF3B8); return; }
        //     if (row == -1) return;
        //     slot = row == count ? firstFree(dir)
        //                         : indexOf(recordAt(dir, profile, row));
        //     if (screen[+78] == -1) charge one ring (events 44/45, prop 5)
        //     Game_WriteSave(slot); screen[+8] = 3;
        //
        // So a NEW save writes at once into the first free slot and closes;
        // an EXISTING row goes through the overwrite confirm first.
        if (it->callback == kCbSaveDo) {
            if (!load_) { approx_ = true; log_.push_back("save: no directory"); return false; }
            const auto rows = load_->rows();
            const int n = static_cast<int>(rows.size());
            if (load_->row < 0) { log_.push_back("save: no row chosen"); return true; }
            if (load_->row < n) {
                // an EXISTING row is confirmed before it is overwritten:
                // `off_4CF3B8`, the screen's `Ecraser ce fichier ?`
                pendingSave_ = -1;
                overwriteRow_ = load_->row;
                log_.push_back("save: row " + std::to_string(load_->row) +
                               " -> the overwrite confirm");
                return installPanel(kPanelSaveOverwrite);
            }
            // the `Nouvelle sauvegarde` row: `sub_408AA0`'s first free slot
            int slot = -1;
            for (int k = 0; k < 256; ++k) {          // `sub_408AA0`'s 256
                bool used = false;
                for (const auto& e : load_->dir) if (e.slot == k) { used = true; break; }
                if (!used) { slot = k; break; }
            }
            if (slot < 0) { log_.push_back("save: every slot is taken"); return true; }
            pendingSave_ = slot;
            log_.push_back("save: slot " + std::to_string(slot) + " requested");
            panel_ = nullptr;                  // `screen[+8] = 3`
            return true;
        }
        if (it->callback == kCbLoadCharger) {
            if (!load_) { approx_ = true; log_.push_back("charger: no directory"); return false; }
            const int slot = loadPanelCharger(*load_);
            if (slot < 0) {
                // the callback's own `if (!dl) return` - nothing chosen, or
                // an empty slot: the frame is consumed and the screen stays
                log_.push_back("charger: no slot on this row");
                return true;
            }
            pendingLoad_ = slot;
            // ...AND IT ANSWERS 0. `mov dword_930750, 0` sits between the
            // slot store and the close, so the parked script resumes with
            // **0** and not with the -1 a plain close leaves.
            //
            // That is the whole loading sequence. AREA 118's startup script
            // is parked at `ui.open 29, -1, -> var 19`, and its next act is
            // `if (var19 == 0)`: that arm is `fade.from_color`, cameras
            // 2152/2153/2154/2158 over `scx.play 20` (`Wait5sec`) and a
            // `media.play 753`, and then it ENDS - no dialogue, no
            // `area.goto`. The other arm, which -1 takes, shows character
            // 310, runs `dialog.start 272` and walks on into the Impasse.
            //
            // So the engine plays the Grid fly-through over a load by
            // ANSWERING A DIFFERENT NUMBER, and a port that closes the
            // screen without one gets the new game's opening instead. A
            // reader saw exactly that - the Kay'l dialogue before the
            // apartment - and it is what sent me back to this callback.
            answer_ = 0;
            log_.push_back("charger: slot " + std::to_string(slot) +
                           " requested, answering 0");
            panel_ = nullptr;                  // `screen[+8] = 3` - it closes
            return true;
        }
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
        // THE ROW'S CONFIRM DISPATCHES ON THE SOURCE KIND. `sub_49BC60`
        // opens `mov eax, dword_670CB8` and subtracts its way down three
        // arms - 0 the inventory, 2 the memory page, 4 the slider - and the
        // row list is SHARED, so every page's rows carry this one callback.
        // Sending them all to the verb panel confirms a slider destination
        // as if it were a carried object, which a player saw as "press enter
        // on a line of the slider list redirects to the inventory".
        //
        // Only the inventory arm is ported. Kind 4 resolves the chosen
        // destination through `sub_40E630` and calls `sub_452570` - the
        // TRAVEL - and kind 2 is the memory page's; neither is modelled, and
        // saying so is better than descending into the wrong page.
// THE TWO REMAINING VERBS. `sub_49BEA0` ("Utiliser") and `sub_49BF30`
        // ("Utiliser sur") both start the same way - read the row list's
        // selection, take its `+0x3C` tag, refuse on -1 - and then:
        //
        //     sub_42B420(tag, 20)   announce: event 30 to resolve the object,
        //                           then event 43 with (action, object)
        //     sub_42B470(tag)       event 35 - MAY it be used? The record's
        //                           `+4 & 1`, which is this port's `usable()`.
        //                           Yes: `Object_ApplyEffect(rec, the player)`.
        //                           No: interface sound 13, and nothing else.
        //
        // The walk cannot do either - both end in `Game_HandleEvent` - so it
        // records which verb was chosen and the caller carries it out.
        if (it->callback == kCbSneakUse || it->callback == kCbSneakUseOn) {
            state_->pendingVerb = (it->callback == kCbSneakUse) ? 0 : 1;
            log_.push_back(state_->pendingVerb == 0 ? "verb: Utiliser"
                                                    : "verb: Utiliser sur");
            return true;
        }
        // `sub_49BC60`'s `loc_49BDD6`, and it comes BEFORE the descent into
        // the verb panel: while the combine mode is open a row confirm feeds
        // the mode instead of opening the verbs.
        //
        //     if (!dword_670BE0) -> the ordinary arms
        //     if (dword_670BE8 == -1) { dword_670BE8 = obj; return 0; }
        //     dword_670BEC = obj;
        //     if (sub_42B4D0(dword_670BE8, obj)) { reset the rows; sound 12 }
        //     else                               { text 35 }
        //     sub_42A370(screen, unk_4DEE50);              // the inventory page
        //
        // so the SECOND object completes it and the first slot is whichever
        // of A/B `sub_42B520` chose. The caller does the combine itself -
        // the recipe table and the lists are the Session's - and then calls
        // `endCombine`.
        if (it->callback == kCbSneakRowConfirm && state_->combining) {
            // The slots hold ROW INDICES, not object ids: `sub_49BC60`
            // reads `[edi+3Ch]` - the widget's row tag - and case 37 maps it
            // through `ObjectList_Header`. So the caller resolves them.
            const int obj = rowOf(it->addr);
            if (obj < 0) { log_.push_back("combine: no row there"); return true; }
            if (state_->combineB == -1) {
                state_->combineB = obj;
                log_.push_back("combine: second slot filled, waiting");
                return true;
            }
            state_->combineC = obj;
            state_->readyA = state_->combineB;
            state_->readyB = obj;
            log_.push_back("combine: due");
            return true;
        }
        // ---- THE SLIDER PAGE'S DESTINATION -----------------------
        //
        // `sub_49BC60`'s kind-4 arm, and it reads the ROW TAG like every
        // other arm of this function:
        //
        //     tag = item[+0x3C]
        //     if (screen[+4] == 1) { rec = sub_40E630(tag);
        //                            point = rec[0], rec[4], rec[8]; }
        //     else                 { point = player[+0xF4/F8/FC]; }
        //     if (sub_452570(&point)) { screen[+8] = 3;
        //                               dword_6A17CC = tag; }
        //     else                    { show text 42 }
        //
        // `sub_40E630` is not a lookup - it counts ENABLED destinations to
        // `tag`, loads the record's `+2` AREA if it is not the resident one,
        // and returns the ADDRESS in that area whose `+14` is the record's
        // own bit. So the walk records the tag and the caller does all three,
        // exactly as it does for a verb: the area, the address and the fade
        // are the Session's, not a widget walker's.
        // ---- THE SLIDER PAGE'S HEADER, three buttons ---------------
        //
        // Keyed on the items' own callbacks, read 2026-09-08 (widgets.h has
        // the three). "Appel du slider" is `sub_452570` on the player's own
        // position with `dword_6A17CC` untouched - a call, no destination.
        if (it->callback == kCbSliderCall) {
            state_->pendingCallHere = true;
            log_.push_back("slider: Appel du slider - call one here, no destination");
            panel_ = nullptr;                  // `screen[+8] = 3`
            return true;
        }
        // "Automatique" is five instructions: `panel+24 = 2`, the rows.
        if (it->callback == kCbSliderAuto) {
            cur_ = 2;
            log_.push_back("slider: Automatique - focus the destinations");
            return true;
        }
        // "Manuelle" is `sub_457040(slider, player)`: the manual drive.
        if (it->callback == kCbSliderManual) {
            state_->pendingManual = true;
            log_.push_back("slider: Manuelle - take the controls");
            panel_ = nullptr;
            return true;
        }
        if (it->callback == kCbSneakRowConfirm && state_->rowKind == 4) {
            const int row = rowOf(it->addr);
            if (row < 0) { log_.push_back("slider: no destination there");
                           return true; }
            // WHICH OF THE PAGE'S TWO MEANINGS, and it is the slot's `+4`:
            //
            //     if (screen[+4] == 1) { rec = sub_40E630(tag);        // TRAVEL
            //                            point = rec[0], rec[4], rec[8]; }
            //     else                 { point = player[+0xF4/F8/FC]; } // CALL
            //
            // and that field is the screen record's own `param`, which
            // `UI_LoadScreen` copies in. Screen **7 SLIDER** carries 1 and
            // screen **9 SNEAK** carries 0 - so the same page TRAVELS from
            // inside the vehicle and CALLS one to where he stands from the
            // device. `MDSLIDIN` is what opens screen 7 (`UI_OpenScreen(7,
            // -1, -1, -1)`, "cant find slider interface !"), so a journey is
            // something you ask for once you are ABOARD.
            //
            // The port teleported on this row whatever the screen, which is
            // the arrive arm and belongs to neither: a reader met it as
            // "calling a slider with the sneak teleports me".
            state_->pendingTravel = row;
            state_->travelToDestination = w_->screenParam(screen_) == 1;
            log_.push_back(state_->travelToDestination
                           ? "slider: travel to destination row " + std::to_string(row)
                           : "slider: CALL one here, remembering row " + std::to_string(row));
            panel_ = nullptr;                  // `screen[+8] = 3`
            return true;
        }
        // ---- THE MEMO READER, `sub_49BC60`'s KIND-2 ARM ------------------
        //
        // Two instructions, and the port refused them for want of reading:
        //
        //     loc_49BDCC: push offset off_4DEFF0
        //                 jmp  loc_49BE80      ; sub_42A370(screen, panel)
        //
        // ENTER on a memo installs the READER PAGE. Its record ships
        // `+24 = 2`, so the page comes up STANDING IN list 0x004DEAE8 - the
        // body box - whose hook is the scroller `0x0042A9A0`, and that is
        // what a reader found in the original: "pressing entree allows to
        // use the scroll bar of the text zone". Its `+16` hook is 0, so
        // nothing moves between its lists and BACK is the way out, through
        // the leave `sub_49D890`.
        //
        // `sub_49D870`, its builder: `dword_6A5090 = 0` - the scroll offset,
        // zeroed so a memo is read from the top, exactly as the examine
        // page's own open does - and `sub_428FF0(0x004DEA98, 0x40400080, 1)`,
        // which the leave clears. Those two bits are NOT modelled: the word
        // appears at exactly two sites in the image, this builder and that
        // leave, so nothing tests it by literal and what it gates is unread.
        if (it->callback == kCbSneakRowConfirm && state_->rowKind == 2) {
            const auto* kid = w_->at(kPanelSneakReader, screen_);
            if (!kid) {                       // not lifted: say so, don't guess
                approx_ = true;
                log_.push_back("memo reader: 0x004DEFF0 is not in the table");
                return true;
            }
            leavePage(*panel_);
            panel_ = kid;
            state_->textScroll = 0;           // `mov dword_6A5090, 0`
            buildPage(*panel_);
            settle();
            log_.push_back("enter memo reader");
            return true;
        }
        if (it->callback == kCbSneakRowConfirm && state_->rowKind != 0) {
            approx_ = true;
            log_.push_back("row confirm: this source kind's arm is not modelled");
            return true;
        }
        if (it->callback == kCbSneakRowConfirm ||
            it->callback == kCbSneakExamine) {
            const std::uint32_t to = it->callback == kCbSneakExamine
                                   ? kPanelSneakExamine : kPanelSneakVerbs;
            if (const auto* kid = w_->at(to, screen_)) {
                leavePage(*panel_);
                panel_ = kid;
                // `sub_49B950`'s first instruction is `mov dword_6A5090, 0` -
                // a new object is read from the TOP, whatever the last one
                // was scrolled to.
                if (it->callback == kCbSneakExamine) state_->textScroll = 0;
                // ...and its last call, `sub_42B420(tag, 4)`: message 4.
                if (it->callback == kCbSneakExamine) state_->pendingExamineMessage = true;
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
        if (const auto* kid = w_->at(it->child, screen_)) {
            leavePage(*panel_);
            panel_ = kid;
            buildPage(*panel_);
            settle();
            // The Examiner page's box (0x004E3900) names the SHOP panel as its
            // `+44`, and the descent installs it without writing its `+24` -
            // which still holds the rows the button's confirm chose. The
            // lifted `current` is the open callback's 0, so put the rows back.
            if (it->addr == 0x004E3900u && kid->addr == kPanelShop)
                focusList(kListShopRows);
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
    // `Ui_DispatchInput` (0x0042A430), its head, on the latched word:
    //
    //     if (panel+24 == -1) return 0;
    //     if ((input & 0x2000) && (panel+72 & 0x20))  screen+8 = 3;  // TAB CLOSES
    //     if (input & 0x20) {                                         // BACK
    //         if (panel+0) { if (!(panel+72 & 0x80)) sub_42A370(screen, parent); }
    //         else if (panel+72 & 0x10) screen+8 = 3;
    //     }
    //
    // and only a frame nothing above consumed reaches the hooks. So TAB closes
    // a screen whose panel carries 0x20 - 26 of the 31 top screens, every
    // shop among them, and none of the start menu, SAVE GAME, PAUSE GAME,
    // OPTIONS or the two input-less panels - and BACK is gated too: a child
    // carrying 0x80 (the start menu's confirm) refuses it, and a top panel
    // closes on it only with 0x10. This took BACK unconditionally, and TAB
    // not at all, until 2026-09-14, when a shop would not close on TAB.
    if ((bits & kUiClose) && (panel_->flags & 0x20u)) {
        // THE TERMINAL ANSWERS ON THE WAY OUT, and it is the only screen of
        // the family that does. `sub_4AF0E0` (read from the image):
        //
        //     if (dword_68A600) answer = 2 + (dword_68A5FC != 0);
        //     else              answer =     (dword_68A5FC != 0);
        //     dword_930750 = answer; Ui_CloseScreenDefault(screen);
        //
        // so it reports WHICH of the two protected dossiers were opened - 0
        // neither, 1 the fifth, 2 the fourth, 3 both. AREA 179's script wants
        // 1 for `1-A-CS SecretFile` (the voice-over, `DATA MEMORIZED` and memo
        // 003) and branches again on 2. Which row sets which global is
        // `docs/UI.md`'s reading of case 0, not a fresh transcription - its
        // own `asmfn` snaps to the neighbour here (CLAUDE.md 1's trap) - and
        // is labelled as resting on it.
        if (screen_ == 5) {
            answer_ = (termRow3_ ? 2 : 0) + (termRow4_ ? 1 : 0);
            log_.push_back("terminal close: answer " + std::to_string(answer_));
        }
        panel_ = nullptr;
        log_.push_back("close (TAB)");
        return true;
    }
    if (bits & kUiBack) {
        if (panel_->parent) {
            if (!(panel_->flags & 0x80u)) return toParent();
        } else if (panel_->flags & 0x10u) {
            return toParent();                       // the top panel: it closes
        }
        // refused: nothing above consumed the frame, so the hooks see it
    }
    if (panel_->hook) {
        if (panel_->hook == w_->startConfirmHook()) {
            if (startConfirm(bits)) return true;
        } else if (panel_->hook == kHookSneakSliderLists) {
            if (moveListsSlider(bits)) return true;
        } else if (panel_->hook == kHookLoadPanel) {
            // `Ui_LoadPanelInput` (0x0047ABA0), and its first line is the one
            // that matters:
            //
            //     if (dword_4CEBAC == -1) return 0;
            //     result = Ui_MoveListsLeftRight(screen, panel);
            //
            // So while NO ROW is chosen the panel hook declines and LEFT and
            // RIGHT fall through to the slot list's own hook, which changes
            // the PROFILE.  Once a row is chosen they move between the lists
            // instead - which is the only way to reach `Charger`, `Detruire`
            // and `Annuler` at all.  Without this the buttons are
            // unreachable, which is exactly how it behaved before: the walk
            // could pick a row and then had nowhere to go.
            if (load_ && load_->row >= 0) {
                if (bits & kUiLeft)  { if (moveLists(-1)) return true; }
                if (bits & kUiRight) { if (moveLists(1))  return true; }
            }
        } else if (panel_->hook == kHookShopPanel) {
            // THE SHOPS' panel hook (0x004AEE00), and the only way from the
            // buttons to the stock rows:
            //
            //     if (panel+24 == 0 && word_4E3372 == 3) return 0;
            //     return sub_42A710(screen, panel);
            //
            // `panel+24` 0 is the BUTTON list and `word_4E3372` its
            // selection, so with "Back" (button 3) chosen the hook declines
            // and LEFT/RIGHT fall through to the buttons' own mover;
            // anywhere else they step between lists like any `sub_42A710`
            // panel. Unmodelled, the walk could never leave the buttons and
            // no row of a shop could be chosen.
            bool declines = false;
            if (cur_ == 0 && !panel_->lists.empty())
                declines = selectionOf(panel_->lists.front()) == 3;
            if (!declines) {
                if (bits & kUiLeft)  { if (moveLists(-1)) return true; }
                if (bits & kUiRight) { if (moveLists(1))  return true; }
            }
        } else if (panel_->hook == kHookMultiplanPanel) {
            // MULTIPLAN's panel hook (0x004B0B00), whole:
            //
            //     if (!(input & 0x3)) return 0;                    // LEFT/RIGHT
            //     if (current == 0x004E53A0 && dword_4E5688) { panel+24 = 1; return 1; }
            //     if (current == 0x004E5670)                 { panel+24 = 0; return 1; }
            //     return 0;
            //
            // `dword_4E5688` is the rows' bound count, so the focus reaches an
            // empty list only by the rows' own absence of anything to pick.
            if (bits & (kUiLeft | kUiRight)) {
                const UiList* cl = curList();
                if (cl && cl->addr == kListMultiplanButtons &&
                    boundCount(kListMultiplanRows) != 0) {
                    focusList(kListMultiplanRows);
                    log_.push_back("multiplan: the rows take the focus");
                    return true;
                }
                if (cl && cl->addr == kListMultiplanRows) {
                    focusList(kListMultiplanButtons);
                    log_.push_back("multiplan: the buttons take the focus");
                    return true;
                }
            }
        } else if (panel_->hook == kHookSneakIdentityPanel) {
            // THE IDENTITY PAGE's panel hook (0x0049C1D0), whole:
            //
            //     old = word_4DE902;                          // the tab row's selection
            //     if (panel+24 == 1) {                        // ON the tab row
            //         if (old == 0 && (input & 1)) return sub_42A710(screen, panel);
            //         if (old == 1 && (input & 2)) return sub_42A710(screen, panel);
            //         return 0;                               // the row's own hook moves
            //     }
            //     if (sub_42A710(screen, panel) != 1) return 0;
            //     if (panel+24 == 1) {                        // ARRIVED on the tab row
            //         if (input & 1) word_4DE902 = 1; else if (input & 2) word_4DE902 = 0;
            //         if (word_4DE902 != old) swap the two contents;
            //     }
            //     return 1;
            //
            // So LEFT off Identite or RIGHT off Caracteristiques leaves the row,
            // and arriving from the RIGHT (a LEFT press) lands on the far tab.
            const auto moveLR = [&]() -> bool {
                if (bits & kUiLeft)  return moveLists(-1);
                if (bits & kUiRight) return moveLists(1);
                return false;
            };
            const UiList* cl = curList();
            if (cl && cl->addr == kListSneakIdentity) {
                const int old = selectionOf(*cl);
                if ((old == 0 && (bits & kUiLeft)) || (old == 1 && (bits & kUiRight))) {
                    if (moveLR()) return true;
                }
            } else if (bits & (kUiLeft | kUiRight)) {
                const UiList* before = cl;
                const int old = [&] {
                    const auto s = selMap().find(kListSneakIdentity);
                    return s != selMap().end() ? s->second : 0;
                }();
                if (moveLR()) {
                    const UiList* now = curList();
                    if (now && now != before && now->addr == kListSneakIdentity) {
                        int sel = old;
                        if (bits & kUiLeft) sel = 1; else if (bits & kUiRight) sel = 0;
                        selMap()[kListSneakIdentity] = sel;
                        if (sel != old) identitySwap(sel);
                    }
                    return true;
                }
            }
        } else if (panel_->hook == kHookSneakMemoryPanel) {
            // THE MEMORY PAGE'S panel hook (0x0049D8B0), whole:
            //
            //     sub_428FF0(0x004DEA98, 0x40000001,
            //                sub_428F30(panel) == &word_4DE6F0 ? 0 : 1);
            //     return sub_42A710(screen, panel);
            //
            // - the body box is shown when the CURRENT list is the rows and
            // hidden otherwise, and then the generic mover runs. The flag is
            // NOT written here: a panel hook is the panel's per-frame tick,
            // so it re-evaluates that test every frame, and `memoBodyShown()`
            // derives it from the current list instead. Writing it here, from
            // the list current BEFORE the move, made the preview appear one
            // press late - which is what a reader saw the original not do.
            if (bits & kUiLeft)  { if (moveLists(-1)) return true; }
            if (bits & kUiRight) { if (moveLists(1))  return true; }
        } else if (panel_->hook == kHookHighScorePage) {
            // `sub_4ADA80`: LEFT and RIGHT step the SCREEN's `+4`, wrapping
            // through 0..3, and both arms return 1. Nothing else on the
            // screen moves - its one item is a draw hook.
            if (bits & kUiLeft)  { if (--hsPage_ < 0) hsPage_ = 3; return true; }
            if (bits & kUiRight) { if (++hsPage_ == 4) hsPage_ = 0; return true; }
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
        // The name field answers the CHARACTER channel (`sub_4397B0`) and not
        // the input bits - with ONE exception this used to miss. `loc_47A499`,
        // the arm taken when no character is waiting, reads `screen+0x6C` -
        // the live input word `sub_482FE0` reads - and moves the CARET: bit 1
        // (LEFT) steps it back when it is not already at 0, bit 2 (RIGHT)
        // steps it on while there is a character under it. Both return 1, so
        // the frame is consumed; every other bit returns 0 and the default
        // walk is not reached either, because the hook exists.
        const int before = nameCursor_;
        if (bits & kUiLeft) {
            if (nameCursor_ > 0) --nameCursor_;
        } else if (bits & kUiRight) {
            // `cl = byte_69BDA0[cursor]; if (!cl) return 0` - RIGHT stops at
            // the end of the buffer rather than running past it.
            if (nameCursor_ < static_cast<int>(name_.size())) ++nameCursor_;
        }
        if (nameCursor_ != before) { log_.push_back("caret"); return true; }
        log_.push_back("name field: no bit response");
        return false;
    }
    if (l->hook == kScrollTextBox) {
        // `sub_42A9A0(list, a2)` - the long-text SCROLLER, and it is not a
        // selection mover at all:
        //
        //     if (dword_4E9720) {              // the screen's repeat word,
        //         dword_4E9720 = 0;            // set to 0x203F on open
        //         sub_42B6A0(&unk_4C3F90, 500, list);   // arm a 500 ms timer
        //         return 0;                    // ...and eat this frame
        //     }
        //     eax = list[+0x6C];               // the live input word
        //     if (eax & 4) dword_6A5090 -= 8;  // UP
        //     if (eax & 8) dword_6A5090 += 8;  // DOWN
        //     return sub_42A750(list, a2);     // then the default
        //
        // The first arm is the one-shot on the frame a screen opens; it is
        // not modelled, because the timer it arms has no consumer this port
        // reaches and eating one frame of input is invisible. The step is
        // what matters, and there is NO CLAMP here - `Ui_ItemTextStyle` does
        // that, against a height only the draw knows.
        int moved = 0;
        if (bits & kUiUp)   { state_->textScroll -= 8; moved = 1; }
        if (bits & kUiDown) { state_->textScroll += 8; moved = 1; }
        if (moved) {
            log_.push_back("text scroll: " + std::to_string(state_->textScroll));
            return true;
        }
        // `sub_42A750` - the default, so a confirm or a back still works on
        // a page whose only list is a text box.
        if (bits & kUiConfirm) return confirm();
        return false;
    }
    if (l->hook == kHookMultiplanButtons) {
        // MULTIPLAN's button list (0x004B09A0):
        //
        //     if (sub_42A910(screen, list) != 1) return 0;   // UP/DOWN, or the confirm
        //     sub_4296D0(rows / header, the selected button's colour);
        //     if (selected == "vers le multiplan") dword_68A610 = 0;   // the sneak
        //     else                                 dword_68A610 = 1;   // the kiosk
        //     sub_42ADD0(rows, 0, dword_68A610);                       // rebind at 0
        //
        // So the rows always list what the chosen button acts ON.
        if (!move(*l, bits)) return false;
        if (!panel_ || panel_->addr != kPanelMultiplan) return true;   // a confirm left
        multiplanColour(*panel_);
        const auto s = selMap().find(l->addr);
        const int sel = s != selMap().end() ? s->second : 0;
        const std::uint32_t chosen =
            (sel >= 0 && static_cast<std::size_t>(sel) < l->items.size())
                ? l->items[static_cast<std::size_t>(sel)].addr : 0u;
        multiplanSetSource(chosen == kItemMultiplanToKiosk ? 0 : 1);
        return true;
    }
    if (l->hook == kHookSneakIdentityTabs) {
        // THE IDENTITY PAGE's tab row (0x0049C160):
        //     if (sub_42A930(screen, list) != 1) return 0;
        //     switch (list+2) { case 0: 810 on, 858 off; case 1: 810 off, 858 on; }
        //     return 1;
        if (!move(*l, bits, kUiLeft, kUiRight)) return false;
        identitySwap(selectionOf(*l));
        return true;
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
        // `sub_42AFF0` IS PORTED NOW (2026-09-04). Before that the window
        // was hardcoded 0 and this fell through to the ordinary move, which
        // is right for a list no longer than its nine widgets - every list
        // the port could reach at the time - and silently truncating for
        // anything longer. A player carrying ten things could not reach the
        // tenth.
        //
        // Refusing the whole hook, which is what it did before THAT, meant
        // the selection never moved inside the sneak's rows on any page, and
        // a player reported it twice.
        if (moveRowWindow(*l, bits)) {
            // The tail: `sub_4083F0(0x1E, &tag)` on any successful move,
            // guarded on the tag not being -1 - event 30, the inventory
            // channel's 3D-preview load. Recorded rather than raised,
            // because the Session owns the channel and the walk does not;
            // a caller reads `rowOf(selected())` and asks for the preview.
            log_.push_back("row move: event 30 for the selected row");
            return true;
        }
        // `sub_42AFF0` is only the MOVER. A confirm on a row is the item's
        // own callback (`kCbSneakRowConfirm` -> the verb panel), dispatched
        // as for every other list - and this branch swallowed it from
        // 2026-09-04's scrolling commit until a player found every verb
        // dead: "I can't use Utiliser, Utiliser sur or Examiner when I
        // select an item". `engine: sneak` was red on main for the same
        // reason.
        if (bits & kUiConfirm) return confirm();
        return false;
    }
    if (l->hook == kHookRowWindow) {
        // A list whose hook is `sub_42AFF0` DIRECTLY - the shops' nine stock
        // rows (0x004E3640). The same centred window the sneak's rows reach
        // through `sub_49C050`, without that wrapper's memory-page tail: move
        // the selection, scroll once it passes the middle widget, raise
        // event 30 on a move. Unmodelled, the walk reached the first row of
        // a shop and no further, and a library of sixteen books showed nine.
        if (moveRowWindow(*l, bits)) {
            log_.push_back("row move: event 30 for the selected row");
            return true;
        }
        // ...and a confirm is still the ROW's own callback (the shop's
        // `0x004AEAA0`, buy or sell), not the mover's.
        if (bits & kUiConfirm) return confirm();
        return false;
    }
    if (l->hook == kHookLoadSlotList) {
        // The load panel's slot list (0x0047AEC0).  The panel's own state
        // lives outside the walk, because it is the SAVE DIRECTORY's - a
        // caller attaches one with `attachLoadPanel` and reads it back.
        if (!load_) {
            approx_ = true;
            log_.push_back("load panel: no directory attached");
            return false;
        }
        if (loadPanelInput(*load_, bits)) {
            log_.push_back(load_->row < 0
                ? "load panel: profile " + std::to_string(load_->profile)
                : "load panel: row " + std::to_string(load_->row));
            return true;
        }
        // A CONFIRM ON A ROW MOVES TO THE BUTTONS. The hook's own tail,
        // which this used to fall through:
        //
        //     if (!(bits & 0x10)) return 0;
        //     if (row == -1)      return 0;
        //     panel[+0x18] = 1;                 // the current LIST
        //     if (screen == 29) word_4CEA9A = 0;
        //     if (screen == 30) word_4CEA9A = 1;
        //     return 1;
        //
        // `panel+24` is the current list and 1 is the button list, so ENTER
        // on a slot focuses `Charger`/`Sauvegarde`, `Detruire` and
        // `Annuler` - and sets the load/save mode on the way. Without it the
        // only way across was LEFT/RIGHT through the panel hook, which a
        // reader noticed was missing.
        if ((bits & kUiConfirm) && load_->row >= 0) {
            load_->mode = screen_ == 30 ? 1 : 0;   // `word_4CEA9A`
            for (std::size_t k = 0; k < panel_->lists.size(); ++k)
                if (panel_->lists[k].addr == 0x004CEA98u) { cur_ = static_cast<int>(k); break; }
            log_.push_back("slot confirmed: the buttons take the focus");
            return true;
        }
        return false;
    }
    if (l->hook == kHookTerminalPad) return keypad(*l, bits);
    if (l->hook == kHookGandharGrid) return gandhar(*l, bits);
    if (l->hook == kHookDenDial)     return denDial(*l, bits);
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
        // the directory's fourth field: `qmemcpy(v7, v3 + 19, 0x20)` with v3
        // at slot + 32, so +76 bytes FROM v3 - slot + 108, the player
        // record's +8, the character's name (GAME_STATE 8)
        for (std::size_t i = 0; i < 32 && s + 108 + i < d.size() && d[s + 108 + i]; ++i)
            e.character.push_back(d[s + 108 + i]);
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

// --------------------------------------------------------- the LOAD PANEL

// On the SAVE screen the list carries one row more than the profile has
// slots - `Nouvelle sauvegarde`, the screen's string 15 - unless all 256 are
// taken, where `Ui_BuildLoadPanel` takes it away again:
//
//     if (total == 256) { dword_657964 = dword_657968 - 1; ...hide it... }
//     else              { dword_657964 = dword_657968; }
//
// `dword_657964` is the last row INDEX, so `== count` is one past the last
// slot: the new-save row.
bool LoadPanel::hasNewRow() const { return mode == 1 && dir.size() < 256; }

std::vector<SaveEntry> LoadPanel::rows() const {
    // `SaveDir_CountByName` / `SaveDir_RecordAt`: the slots whose PROFILE name
    // matches the one being listed, in directory order.
    std::vector<SaveEntry> out;
    if (profile < 0 || profile >= static_cast<int>(profiles.size())) return out;
    for (const auto& e : dir)
        if (!e.name.empty() && e.name == profiles[static_cast<std::size_t>(profile)])
            out.push_back(e);
    return out;
}

int LoadPanel::slotOfRow() const {
    const auto r = rows();
    if (row < 0 || row >= static_cast<int>(r.size())) return -1;
    return r[static_cast<std::size_t>(row)].slot;
}

std::string LoadPanel::rowLabel(const SaveEntry& e) const {
    return e.character + " - " + formatDate(static_cast<int>(e.day)) + " - " +
           formatTime(static_cast<int>(e.time));
}

// `cmp ecx, 9 / jl` twice: nine widgets, and the indicator is set only once
// there are more rows than fit.  The window keeps four rows above the
// selection and four below.
bool LoadPanel::moreAbove() const {
    return static_cast<int>(rows().size()) >= 9 && row >= 4;
}
bool LoadPanel::moreBelow() const {
    const int n = static_cast<int>(rows().size());
    return n >= 9 && row < n - 4;
}

LoadPanel buildLoadPanel(const std::vector<SaveEntry>& dir) {
    LoadPanel p;
    for (const auto& e : dir) {
        if (e.name.empty()) continue;          // an empty slot is not listed
        p.dir.push_back(e);
        if (std::find(p.profiles.begin(), p.profiles.end(), e.name) == p.profiles.end())
            p.profiles.push_back(e.name);
    }
    // `if (v3) { ... byte_657970 = SaveDir_NameAt(dir, 0); dword_4CEBAC = -1 }
    //  else { word_4CEA9A = 3; ... }` - with nothing to list the panel goes to
    // mode 3, hides the slot list and disables Charger and Detruire, which is
    // what `loadPanelFor` already models from the other end.
    p.profile = p.profiles.empty() ? -1 : 0;
    p.row = -1;
    p.mode = p.profiles.empty() ? 3 : 0;
    return p;
}

void applyLoadPanelLayout(UiWidgets& w, int screen) {
    const auto item = [&w](std::uint32_t addr) -> UiItem* {
        for (auto& p : w.panels_)
            for (auto& l : p.lists)
                for (auto& it : l.items)
                    if (it.addr == addr) return &it;
        return nullptr;
    };
    // The builder sets TWO flags per button, and only one of them hides it:
    //
    //     I2D_SetFlag(item, 0x20000004, on)   bank A - not NAVIGABLE
    //     I2D_SetFlag(item, 0x40000001, on)   bank B - not DRAWN
    //
    // `Ui_DrawItem`'s gate is the second (`eff0[1] & 1` in `screendraw`), so
    // setting only the first moves a hidden button on top of a visible one
    // and draws both - which is what the first version of this did, and it
    // showed as two words superimposed on the top row.
    const auto place = [&](std::uint32_t addr, int y, bool hidden, int str = -1) {
        UiItem* it = item(addr);
        if (!it) return;
        if (!hidden && y >= 0) it->y = y;   // y < 0 = leave it where it is,
                                            // and a hidden one is never moved
        if (hidden) { it->flags[0] |= 0x20000004u; it->flags[1] |= 1u; }
        else        { it->flags[0] &= ~0x20000004u; it->flags[1] &= ~1u; }
        // ...AND THE STRING ID, which the builder rewrites into each item's
        // `+28` because ONE panel serves two screens with two text files:
        //
        //     screen 29   word_4CEA14 = 2    Detruire   word_4CEA5C = 9  Annuler
        //     screen 30   word_4CEA14 = 11   Detruire   word_4CEA5C = 3  Annuler
        //
        // Without it the save screen drew `Charger une partie`'s neighbours
        // out of `IAM\Save`, where 1, 2 and 9 are `Indices`, `Cet indice te
        // coutera :` and `Indice achete !` - which is what a reader saw.
        if (str >= 0) it->bindString = str;
    };
    const auto& L = w.loadPanel();
    // REPARENT THE SHARED PANEL, which is the half of this that is not
    // cosmetic. `Ui_BuildLoadPanel` writes the panel's own parent field and
    // `Annuler`'s child together:
    //
    //     screen 29   *panel = &unk_4CF218;   off_4CEA6C = &unk_4CF218;
    //     screen 30   *panel = &unk_4E2ED8;   off_4CEA6C = &unk_4E2ED8;
    //
    // Without it the slot panel still names the START MENU as its parent on
    // screen 30, so the composer's parent chain draws the start menu's items
    // over the save screen - its name field included, which is why a reader
    // saw `Non : zfe` and the hints strings piled on one frame - and BACK
    // leaves to the wrong panel.
    const std::uint32_t owner = screen == 30 ? 0x004E2ED8u : 0x004CF218u;
    for (auto& p : w.panels_)
        if (p.addr == kPanelLoadSlots) p.parent = owner;
    for (auto& p : w.panels_)
        if (p.addr == kPanelLoadSlots)
            for (auto& l : p.lists)
                if (l.addr == 0x004CEA98u && l.items.size() >= 4)
                    l.items[3].child = owner;      // `Annuler` goes home
    if (screen == 30) {
        // the SAVE panel: `Sauvegarde` in the top slot, Charger hidden
        place(L.nouvelle, 266, false, 0);
        place(L.charger,  266, true);
        place(L.detruire, 326, false, 11);
    } else {
        place(L.charger,  266, false, 1);
        place(L.nouvelle, 266, true);
        place(L.detruire, 326, false, 2);
    }
    // THE CONFIRM PANEL IS BUILT PER SCREEN TOO - `sub_47B850`, which is the
    // same shape as the slot panel's builder and which a reader met as
    // "texts on the confirm panel are wrong":
    //
    //     screen 29   parent = off_4CF280   question 10, yes 6,  no 7
    //     screen 30   parent = off_4CF2E8   question 16, yes 12, no 13
    //
    // The three item addresses are the symbols' own arithmetic: `word_4CED6C`
    // is item 0x004CED50 + 0x1C, `word_4CEDB4` is 0x004CED98 + 0x1C and
    // `word_4CF0E4` is 0x004CF0C8 + 0x1C - each one an item's `+28`, the
    // string id. On screen 30 that reads `Ecraser ce fichier ?` over `Oui`
    // and `Non`; unported it drew strings 6, 7 and 10, which there are
    // `Anneaux en votre possession :`, the save question and
    // `Votre ame va etre sauvegardee !`.
    place(0x004CED50u, -1, false, screen == 30 ? 12 : 6);    // Oui
    place(0x004CED98u, -1, false, screen == 30 ? 13 : 7);    // Non
    place(0x004CF0C8u, -1, false, screen == 30 ? 16 : 10);   // the question
    // `sub_47B710`, the DESTROY confirm's builder - the same shape again:
    //     screen 29   question 2,  oui 6,  no 7
    //     screen 30   question 11, oui 12, no 13
    // and the symbols are those items' own `+0x1C`: `word_4CECB4` is
    // 0x004CEC98 + 0x1C, `word_4CECFC` is 0x004CECE0 + 0x1C and `word_4CF074`
    // is 0x004CF058 + 0x1C.
    place(0x004CEC98u, -1, false, screen == 30 ? 12 : 6);    // Oui
    place(0x004CECE0u, -1, false, screen == 30 ? 13 : 7);    // Non
    place(0x004CF058u, -1, false, screen == 30 ? 11 : 2);    // the question
    // ...and BOTH buttons' `+44`, the child they return to. The builder's
    // `off_4CED7C` and `dword_4CEDC4` are `0x004CED50 + 0x2C` and
    // `0x004CED98 + 0x2C` - the child field, not the string. The table ships
    // `Non`'s as 0, so unported it went nowhere at all, which is what a
    // reader met: "pressing enter on oui or non does nothing".
    const std::uint32_t back = screen == 30 ? kPanelLoadSlots : 0x004CF280u;
    for (auto& p : w.panels_)
        if (p.addr == kPanelSaveOverwrite)
            for (auto& l : p.lists)
                for (auto& it : l.items)
                    if (it.addr == 0x004CED50u || it.addr == 0x004CED98u)
                        it.child = back;
    for (auto& p : w.panels_)
        if (p.addr == kPanelSaveOverwrite)
            p.parent = screen == 30 ? kPanelLoadSlots : 0x004CF280u;
    // Annuler is the fourth item of the button list, the one whose child is
    // the start menu; the table names the other three.
    for (auto& p : w.panels_)
        if (p.addr == 0x004CF2E8u)
            for (auto& l : p.lists)
                if (l.addr == 0x004CEA98u && l.items.size() >= 4) {
                    l.items[3].y = 386;
                    l.items[3].bindString = screen == 30 ? 3 : 9;   // Annuler
                }
}

bool loadPanelInput(LoadPanel& p, std::uint32_t bits) {
    const int n = static_cast<int>(p.rows().size());
    // AT ROW -1, LEFT AND RIGHT CHANGE THE PROFILE.  The hook's own guard is
    // `screen == 29 && dword_4CEBAC == -1 && dword_65796C != 0`, and each arm
    // steps `list + 0x18` with a wrap - `jns` back to count-1 going down,
    // `cmp/jl` back to 0 going up - then re-reads the name and recounts.
    if (p.row == -1 && !p.profiles.empty()) {
        const int m = static_cast<int>(p.profiles.size());
        if (bits & kUiLeft) {
            if (--p.profile < 0) p.profile = m - 1;
            return true;
        }
        if (bits & kUiRight) {
            if (++p.profile >= m) p.profile = 0;
            return true;
        }
    }
    // ...and UP/DOWN move the row, WITH A WRAP, between two limits the
    // builder set:
    //
    //     if (bits & 4)       row = row > dword_657990 ? row - 1 : dword_657964;
    //     else if (bits & 8)  row = row < dword_657964 ? row + 1 : dword_657990;
    //
    // On screen 29 `dword_657990` is **-1** and `dword_657964` is count - 1,
    // so the "nothing chosen" position is part of the cycle: DOWN off the
    // last row returns to -1, where LEFT and RIGHT change the profile again.
    // (Screen 30 sets the low limit to 0 and has no such position.)
    //
    // Getting this wrong is easy and quiet - a first version here clamped
    // instead of wrapping and could never leave the list once entered.
    // ...and on the save screen the ring is one longer: `dword_657964` is
    // `count` rather than `count - 1`, the `Nouvelle sauvegarde` row.
    const int lo = -1, hi = n - 1 + (p.hasNewRow() ? 1 : 0);
    if (bits & kUiUp)   { p.row = p.row > lo ? p.row - 1 : hi; return true; }
    if (bits & kUiDown) { p.row = p.row < hi ? p.row + 1 : lo; return true; }
    return false;
}

int loadPanelCharger(const LoadPanel& p) {
    const int slot = p.slotOfRow();
    if (slot < 0) return -1;
    // the callback's own guard: `dl = dir[72*idx]; if (!dl) return` - an empty
    // slot answers nothing rather than loading zeroes
    for (const auto& e : p.dir)
        if (e.slot == slot) return e.name.empty() ? -1 : slot;
    return -1;
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

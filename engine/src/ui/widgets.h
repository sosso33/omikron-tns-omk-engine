// SPDX-License-Identifier: GPL-3.0-or-later
// The interface's widget tree, and the input walk over it.
//
// A screen's open callback installs a PANEL, a panel names LISTS, a list names
// ITEMS - and every one of those records is `.data` in `Runtime 2.exe`, not in
// `gamedata/`. So this reads `tables/ui_widgets.json`, lifted by
// `tools/exetables.py`, for the same reason the VM opcode table is lifted: a
// replica that asks the user for their own game files has nowhere else to get
// it.
//
// **The tree links downward through the ITEM.** `panel+0` is the parent, which
// is why a first reading of this recorded "top panels only" as a limit - but
// `Ui_ConfirmSelection` descends into an item's `+44` when it has no `+40`
// callback, and following those transitively reaches 7 child panels, the start
// menu's confirm dialog and its name field among them. 28 screens share only
// 13 distinct top panels, so a record per screen is not a record per address.
//
// What still needs native code is a panel a CALLBACK installs rather than a
// `+44`, and the answers those callbacks write.
//
// The dispatch is `Ui_DispatchInput` (docs/UI.md 3c): the back and close bits
// first, then the panel's own hook, then the current list's `+4` hook, then
// `Ui_MoveSelection` as the default - which steps over every UIF_UNSELECTABLE
// item and, when no direction was pressed, falls through to
// `Ui_ConfirmSelection`.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace omk {

// The 14 binding slots the interface reads, edge-filtered by mask 0x203F.
inline constexpr std::uint32_t kUiLeft = 1, kUiRight = 2, kUiUp = 4, kUiDown = 8;
inline constexpr std::uint32_t kUiConfirm = 0x10, kUiBack = 0x20, kUiClose = 0x2000;

inline constexpr std::uint32_t kItemUnselectable = 4;      // flags A bit 2
inline constexpr std::uint32_t kListHidden       = 4;      // list flags A bit 2
inline constexpr std::uint32_t kListNoWrap       = 0x80000;

using FlagOp = std::pair<std::uint32_t, bool>;

struct UiItem {
    std::uint32_t addr = 0;
    int           x = 0, y = 0, w = 0, h = 0;   // its own coordinate and size
    int           string = -1;       // index into the screen's own IAM text file
    // ...and what the screen's OPEN callback WRITES into `+28`/`+60`. The
    // record ships `string = -1` and `tag = 0` for most rows, so a reader
    // that trusts it finds a screen with no labels - which is exactly what
    // this composer drew until the bindings were wired in: 0 rows on both
    // screens. `docs/UI.md` 3d; lifted as each item's `bind`.
    int           bindString = -1, bindTag = -1;
    // The id to draw with: the binding when the callback wrote one, else the
    // record's own.
    int           label() const { return bindString >= 0 ? bindString : string; }
    std::uint32_t callback = 0;
    // `Ui_ConfirmSelection` takes the `+40` callback when there is one and
    // otherwise DESCENDS into `+44`. So the tree links downward through the
    // item, not through the panel - `panel+0` is only the parent.
    std::uint32_t child = 0;
    std::uint32_t flags[3] = {0, 0, 0};
    std::vector<FlagOp> setFlag;     // I2D_SetFlag calls on this item

    // The three banks with the open callback's changes applied. The static
    // record is NOT what the screen shows: the start menu centres its four
    // buttons with one broadcast rather than storing the flag, so a reader
    // that trusts the record alone gets the alignment wrong. A flag constant
    // carries its bank in the TOP bits, which is what picks the word.
    void effective(const std::vector<FlagOp>& broadcast, std::uint32_t out[3]) const;
    bool selectable(const std::vector<FlagOp>& broadcast) const;
};

struct UiList {
    std::uint32_t addr = 0;
    std::uint32_t hook = 0;          // 0 = Ui_MoveSelection, the default walk
    std::uint32_t flags = 0;
    std::vector<FlagOp> broadcast;
    std::vector<UiItem> items;

    bool hidden() const { return (flags & kListHidden) != 0; }
    bool noWrap() const { return (flags & kListNoWrap) != 0; }
};

struct UiPanel {
    int           screen = -1;
    std::uint32_t addr = 0, parent = 0, hook = 0;
    // `Ui_ItemScreenX/Y` add this to every row's own coordinate: the panel
    // slides, the items do not. Lifted 2026-09-01 - the tree modelled the
    // WALK, which needs no geometry, so nothing here could place a row.
    int           offsetX = 0, offsetY = 0;
    // `Ui_DrawPanelBack`'s 80 tile ids, or empty for the full-sheet path.
    // `panel + 20` is a POINTER to them and they are read SIGNED; reading
    // the array in place gives ids like 240, which cannot index a 10-wide
    // grid.
    std::vector<int> tiles;
    std::vector<UiList> lists;
};

// `sub_47A390` - the new-game panel's name entry.
//
// BACKSPACE deletes before the cursor and shifts the tail left; TAB and ESC
// are ignored; RETURN moves focus to the button list, but ONLY when the buffer
// is not empty; anything else inserts, refused once the buffer holds 20. The
// cap is the buffer itself (0x0069BDA0..0x0069BDB4) and the save slot that
// receives the name has room for 32 - the field is the tighter of the two.
class NameField {
public:
    NameField(const std::map<int, std::string>& sw, int cap)
        : sw_(sw), cap_(cap) {}

    bool type(char c);                 // -> consumed?
    void enter(const std::string& s) { for (char c : s) type(c); }

    const std::string& text() const { return buf_; }
    int  cursor() const { return cursor_; }
    bool done() const { return done_; }   // RETURN accepted: focus moves on

private:
    std::map<int, std::string> sw_;
    int  cap_ = 20;
    std::string buf_;
    int  cursor_ = 0;
    bool done_ = false;
};

class UiWidgets {
public:
    static UiWidgets loadJson(const std::string& path);
    // The screens table lives in tables/ui.json; loading it is optional and
    // only affects `textFile`.
    void loadScreens(const std::string& uiTable);
    bool valid() const { return !panels_.empty(); }

    const UiPanel* screen(int id) const;
    // The screen's own `IAM\<name>` text file, from the 37-record table's
    // `+16`. Empty for the five screens that name none.
    const std::string& textFile(int screenId) const;
    // The `.wav` stem for one of a screen's twelve sound slots, or empty when
    // the screen does not name that slot (the record ships -1).
    //
    // **WHICH SLOT IS WHICH comes from `sub_482FE0` (0x00482FE0), not from the
    // family pattern.** That function reads the live input word at screen +108
    // and picks the slot by BIT: `0x10` (confirm) plays slot 0, `0x20` (back)
    // slot 1, `0xF` (the four directions) slot 2, and `0x2000` (close) slot 3.
    //
    // `docs/UI.md` said slot 0 was the selection move, 1 the confirm and 2 the
    // screen opening. Its evidence established only that slots 0/1/2 always
    // hold the 002/003/001 of one family - the MEANINGS were an inference the
    // evidence did not support, and all three were wrong. There is no screen-
    // opening sound at all. Corrected 2026-09-01 after a player said the menu
    // played its validation sound when the selection MOVED.
    const std::string& soundName(int screenId, int slot) const;
    static constexpr int kSoundConfirm = 0, kSoundBack = 1,
                         kSoundMove    = 2, kSoundClose = 3;
    // The screen's artwork, opened as `I2d\\bitmaps\\%s` - one 640x480 sheet
    // for all eleven, which is what lets the tile map serve every screen.
    const std::string& bitmap(int screenId) const;
    const UiPanel* at(std::uint32_t addr) const;
    const std::vector<UiPanel>& all() const { return panels_; }
    std::uint32_t gridHook() const { return gridHook_; }
    std::uint32_t nameHook() const { return nameHook_; }
    std::uint32_t startConfirmHook() const { return startHook_; }
    std::uint32_t startNameList() const { return startName_; }
    std::uint32_t startButtonList() const { return startButtons_; }
    int nameMax() const { return nameMax_; }
    const std::map<int, std::string>& nameSwitch() const { return nameSwitch_; }

    // Item callbacks whose effect on the ANSWER has been read. Everything
    // else is native code the port does not run, and is logged rather than
    // guessed. `needsName` carries the gate: `Confirmer`'s first instruction
    // tests the name cursor and jumps to the ret when it is zero, writing
    // neither the answer nor the screen's state word - so confirming with an
    // empty field leaves the calling script suspended for ever.
    struct Answer { std::uint32_t callback; int value; bool needsName; };
    const std::vector<Answer>& answers() const { return answers_; }

    // The load panel's item addresses, and the save file's geometry.
    struct LoadPanelDef {
        std::uint32_t panel = 0, slotList = 0, buttonList = 0;
        std::uint32_t charger = 0, nouvelle = 0, detruire = 0;
    };
    const LoadPanelDef& loadPanel() const { return load_; }
    std::size_t savesHeader() const { return savesHeader_; }
    std::size_t saveSlot() const { return saveSlot_; }
    int saveSlots() const { return saveSlots_; }

private:
    std::vector<UiPanel> panels_;
    std::map<int, std::string> textFile_, bitmap_;
    std::map<int, std::string> soundName_;          // the 45, by id
    std::map<int, std::vector<int>> screenSounds_;  // each screen's 12 slots
    std::uint32_t gridHook_ = 0, nameHook_ = 0, startHook_ = 0;
    std::uint32_t startName_ = 0, startButtons_ = 0;
    int nameMax_ = 20;
    std::map<int, std::string> nameSwitch_;
    std::vector<Answer> answers_;
    LoadPanelDef load_;
    std::size_t savesHeader_ = 3496, saveSlot_ = 32808;
    int saveSlots_ = 256;
};

// One open screen, driven by input words.
class UiWalk {
public:
    explicit UiWalk(const UiWidgets& w) : w_(&w) {}

    bool open(int screenId);
    // One frame of `Ui_DispatchInput`. -> true if the frame was consumed.
    bool press(std::uint32_t bits);

    // Type into the name field, when the current list is one.
    bool typeName(const std::string& text);
    const std::string& name() const { return name_; }

    // The panel the walk is ON, which is not always the screen's own: an item
    // with a `child` descends into one, and that is how the start menu's
    // confirm dialog and its name field appear. A drawer that only ever draws
    // `screen(id)` shows the menu and nothing else, so pressing confirm
    // changes NOTHING on screen - which is exactly what a player reported.
    const UiPanel* panel() const { return panel_; }

    int  currentList() const { return cur_; }
    int  selection() const;
    const UiItem* selected() const;
    bool closed() const { return panel_ == nullptr; }

    // True once the walk has passed through something it does not model - an
    // unread hook or callback. A caller must not trust an answer after this,
    // and saying so is the point: the alternative is guessing.
    bool approximate() const { return approx_; }

    // The answer a screen produced, when it produced one. `ui.open` suspends
    // the calling script and the screen answers it; -1 means it has not - and
    // "has not" is a real outcome, not a failure: confirming with an empty
    // name field leaves the script suspended for ever.
    int   answer() const { return answer_; }
    const std::vector<std::string>& log() const { return log_; }

private:
    void settle();
    bool move(const UiList& l, std::uint32_t bits);
    // `UI_GridMenuInput` (0x004B00D0) - the LIFT's floor panel, and the one
    // list hook in the image with a single reference. Six slots in a 3-wide,
    // 2-deep grid plus one standing apart at 6, which the ITEM COORDINATES
    // confirm independently: x 278/321/370 on rows at y 194 and 241, then
    // slot 6 alone at (325, 288) under the middle column. The `% 3` is the
    // three columns.
    //
    // Confirm writes the answer itself rather than going through an item
    // callback - `dword_930750 = slot - 1`, with slot 0 giving 6 - so the
    // answer is the slot rotated by one: slot 1 ("Niveau 0", the entrance)
    // answers 0. All 18 `ui.open 4` sites store it in variable 496, `Etage`.
    bool grid(const UiList& l, std::uint32_t bits);
    // `sub_0047A230` - the start menu's confirm dialog moves between its two
    // lists with UP and DOWN, not left and right.
    bool startConfirm(std::uint32_t bits);
    bool confirm();
    bool usable(const UiList& l) const;
    const UiList* curList() const;

    const UiWidgets* w_;
    const UiPanel*   panel_ = nullptr;
    int  cur_ = 0;
    std::map<std::uint32_t, int> sel_;   // list address -> selected index
    bool approx_ = false;
    int  answer_ = -1;
    std::string name_;
    std::vector<std::string> log_;
};

// The start menu's "Charger une partie" panel.
//
// Its shape depends on the save directory and on nothing else: with profiles
// it focuses the slot list and leaves "Charger" and "Detruire" live; with
// none it focuses the BUTTONS, hides the slot list, and makes both
// unselectable. "Nouvelle partie" is hidden either way on screen 29 - it
// belongs to screen 30, the SAVE panel, which shares this panel and is told
// apart by `word_4CEA9A`.
struct SaveEntry {
    int slot = 0;
    std::string name;
    std::uint32_t day = 0, time = 0;
};

// `sub_408A10` - the 72-byte directory entries, read out of `IAM\GAMES`:
// the profile name from the slot's +0, the day and time from +32/+36, and 32
// bytes lifted from the slot's DB at +36 whose purpose is not read.
std::vector<SaveEntry> saveDirectory(const std::string& gamesPath,
                                     const UiWidgets& w);

// `sub_408B50` - how many DISTINCT non-empty profile names it holds. The
// panel branches on this and nothing else.
int saveProfiles(const std::vector<SaveEntry>& dir);

struct LoadPanelState {
    bool empty = true;
    int  focus = 1;          // panel+24
    int  mode  = 3;          // word_4CEA9A
    bool slotListHidden = true;
    std::vector<std::pair<std::uint32_t, bool>> buttons;  // item, selectable
};

LoadPanelState loadPanelFor(int profiles, const UiWidgets& w);

}  // namespace omk

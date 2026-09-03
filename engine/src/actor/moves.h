// SPDX-License-Identifier: GPL-3.0-or-later
// `tab_special_move[]` - the 66 engine callbacks a `.CTL` entry can name.
//
// An entry whose flags carry `0x10` has a 12-byte MOVE NAME after its record
// (`formats/ctl.h`, `moveName`), and `Cef_QueueSpecialMove` looks that name up
// in a 66-row table compiled into `Runtime 2.exe` - `{char[12] name, void
// (*fn)()}` - and calls the row's function. 209 of 209 shipped sites resolve
// (CLAUDE.md 4), so the name is never a dead string.
//
// The table is `.data` in the executable and cannot be read out of
// `gamedata/`, which is why it is lifted to `tables/special_moves.json` the
// way the VM opcode table and the widget tree are. What is in the GAME DATA -
// which entry names which move, and on what input - is read from the `.CTL`
// itself and is not here.
//
// **This file resolves a name to a row. It does not decide what a row DOES.**
// The handlers are the engine's; a caller runs the ones it has read and says
// so for the ones it has not. Two consumers so far, and they arrived from
// opposite ends of the same table within hours of each other:
//
//   rows 3..7   MDACTION, MDGETOBJ, MDLETOBJ, MDPUTSNK, MDNOTAKE - the world
//               TAKE (`todo/omk-play.md` 66): scan within 150 cm, link the
//               object to the hand, bank it, or put it back.
//   row 0       MDSNEAK0 -> `sub_0046ADF0`, which opens the sneak.
//
// Both were reached through `PlayerController::specialMoves()`, and the whole
// gap they closed was one unconsumed event: `channel.cpp` has emitted
// `ChannelEvent::Kind::Move` carrying the name since it was written and
// nothing read it, so all 66 callbacks were inert.
//
// **Why a table rather than a string compare.** A dispatch that tests
// `name == "MDACTION"` works and says nothing; resolving through the row
// gives the caller the INDEX and the HANDLER ADDRESS, so a log line can read
//
//     MDACTION (tab_special_move[3] = 0x0046aec0) fired
//
// and name the function to go and read. It also fails loudly on a name the
// table does not have, where an if-chain silently falls through - and the
// shipped `.CTL` files use 54 distinct names against these 66 rows, so there
// is plenty of room to fall through into.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace omk {

class SpecialMoves {
public:
    static SpecialMoves loadJson(const std::string& path);
    bool valid() const { return !rows_.empty(); }
    std::size_t size() const { return rows_.size(); }

    struct Row {
        int index = -1;
        std::string name;          // the 12-byte name the .CTL entry carries
        std::uint32_t handler = 0; // the engine callback, for attribution
    };

    // -> the row of that name, or nullptr. The match is on the NAME because
    // that is what the file carries; the index is the table's own order.
    const Row* find(const std::string& name) const;
    const std::vector<Row>& all() const { return rows_; }

private:
    std::vector<Row> rows_;
    std::map<std::string, std::size_t> byName_;
};

// ---------------------------------------------------------------- the sneak
//
// Row 0. `sub_0046ADF0` is short enough to quote whole, and the binary names
// it itself through its own failure string:
//
//     v1 = Actor_Index(a1);
//     if (sub_41A350(v1) != -1)            // something pending at actor+164
//         return sub_41C720(g_Player);     // ...use THAT instead, event 10
//     Game_RaiseEvent(25, 0);              // open object list 0 - the carried
//     sub_41E040(byte_53B084);
//     if (!UI_OpenScreen(9, -1, -1, -1)) { // SNEAK
//         Game_RaiseEvent(26, 0);
//         return Dbg_Printf("cant start sneak");
//     }
//
// The chain that gets here is entirely in the shipped data, and every link of
// it was read rather than assumed:
//
//   TAB                       scan code 15 = `tables/key_bindings.json`
//                             group 0 ("Aventure") action 13, "Ouvrir sneak",
//                             bit 0x2000
//   -> H1Avnt/F1Avnt.CTL      group 0 entry id 16862575, `+4` input 0x00002000,
//                             flags 0x05000003 - bit 2 redirects through GoTo
//   -> its GoTo               group 6's default entry `H_SNKON`
//   -> that entry's child     flags 0x25000013, bit 0x10 -> move `MDSNEAK0`
//   -> tab_special_move[0]    sub_0046ADF0, above
//
// Both adventure banks carry it and no other `.CTL` does, so the sneak opens
// in adventure mode and nowhere else - which is the game's behaviour, arrived
// at from the data rather than from playing.
//
// **It has no waiting script.** The `-1` is `UI_OpenScreen`'s waiting-context
// argument, so `dword_930744` is never written and nothing is parked at
// status 6 - closing the sneak answers nobody, where leaving a `ui.open`
// screen IS an answer of -1.
inline constexpr const char* kMoveOpenSneak = "MDSNEAK0";
inline constexpr int kScreenSneak = 9;      // the screen table's row 9
// `Game_HandleEvent` 25 and 26, the two ends of the inventory data channel's
// session (docs/UI.md "the inventory data channel").
inline constexpr int kEventSneakOpen = 25, kEventSneakClose = 26;
// The device's INVENTORY page and its nine row widgets. Both are addresses in
// the widget tree rather than fields, the way `gridHook` and `nameHook` are -
// the page because `Ui_OpenSneakFamily` installs it for parameter 0, the list
// because it is the one whose rows come from the object list rather than from
// `IAM\Sneak`. Several of the device's pages carry the SAME list, which is why
// a drawer has to key on the page as well.
inline constexpr std::uint32_t kPanelSneakInventory = 0x004DEE50u;
inline constexpr std::uint32_t kListSneakRows       = 0x004DE6F0u;

}  // namespace omk

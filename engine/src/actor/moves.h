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

}  // namespace omk

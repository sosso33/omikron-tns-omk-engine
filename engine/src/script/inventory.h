// SPDX-License-Identifier: GPL-3.0-or-later
// `Game_HandleEvent` 25..42 - the inventory screen's DATA CHANNEL.
//
// The interface never touches an object list. It raises an event with one
// argument block and reads the answer back out of it:
//
//     +0   the value, in and out - usually an item index
//     +4   the RESULT code; and for case 36, the request code on the way in
//     +8   a caller-supplied buffer, for the cases that return text
//
// So the whole of the inventory's behaviour is decidable from the object
// records and the game DB, both of which are already ported - this is the
// wiring between them. The cases that are pure data decisions are here; the
// ones that load a model, print into a buffer or raise a message are not,
// and say so.
#pragma once

#include "script/gamestate.h"
#include "script/globaldata.h"
#include "script/objects.h"

#include <string>
#include <vector>

namespace omk {

// The three lists `State_Apply` hands to `ObjectList_SetCapacity`. A fourth -
// list 3, a shop's stock - is sized to 16 and `ObjectList_Load`ed per area
// when case 25 opens it, and is never stored in the DB.
enum class ObjectList { Carried = 0, Second = 1, Memos = 2 };

std::vector<int> objectList(const GameState& s, ObjectList which);

// The result the event writes back at `+4`.
enum class InvResult { Refused = 0, Ok = 1, Two = 2, Three = 3 };

class Inventory {
public:
    Inventory(const std::vector<ObjectRecord>& objects,
              const std::vector<Recipe>& recipes)
        : objects_(&objects), recipes_(&recipes) {}

    const ObjectRecord* record(int id) const;

    // ---- cases 25 and 26: WHICH LIST IS OPEN ---------------------------
    //
    // `dword_4C0B64`, and it is the one piece of state the rest of the family
    // reads: every other case returns result 3 while it is -1. Opening the
    // sneak is `Game_RaiseEvent(25, 0)` - list 0, the carried items - which
    // is what `sub_0046ADF0` does before it opens screen 9 (`actor/moves.h`).
    //
    //     case 25: dword_4C0B64 = a2;
    //              if (a2 != 3) return 1;
    //              ...size list 3 to 16 and ObjectList_Load it
    //     case 26: if (!dword_4C0B64) dword_4C0B64 = -1;
    //              ...free the 3D preview slot
    //
    // Two things to keep exactly as they are. Case 25 stores the argument
    // whatever it is, so "open" is not a boolean; and case 26 only clears the
    // global when the open list is **0** - closing while list 1 or 2 is up
    // leaves it open, which is the engine's own asymmetry and not a slip
    // here. Freeing the preview object (`dword_4C0608`) is case 26's other
    // half and is NOT modelled: it needs case 30's model slot.
    void openList(int list) { openList_ = list; }
    void closeList() { if (openList_ == 0) openList_ = -1; }
    int  openedList() const { return openList_; }

    // case 33 - the display name, with `" - N"` appended when the record's
    // flag 0x20 says it carries a quantity. The count comes from the PLAYER
    // record for kinds 2..6 and from the item's own `+12` for kinds 7..11.
    std::string displayName(int id, int playerCount) const;

    // case 34 - the price, straight out of the record.
    int price(int id) const;

    // case 39 - SELL: credit HALF the price, clamped at 0xFFFF.
    int sellValue(int id) const;

    // case 38 - BUY: refused when the carried list is full or the price
    // exceeds the player's money.
    InvResult canBuy(int id, int money, int carried, int capacity) const;

    // case 41 - may this shop item be bought at all? Refused when its kind is
    // 2..6 and the player already holds one.
    InvResult shopAllows(int id, const std::vector<int>& carried) const;

    // case 37 - COMBINE, through `GLOBAL +12`. Matched SYMMETRICALLY, so the
    // order the player picks the two items in does not matter; and gated on a
    // value the engine only ever writes 1, 0 or -1 - never the 8 that six of
    // the eleven recipes want.
    int combine(int a, int b, int gate) const;

private:
    const std::vector<ObjectRecord>* objects_;
    const std::vector<Recipe>* recipes_;
    int openList_ = -1;              // dword_4C0B64
};

}  // namespace omk

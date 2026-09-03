// SPDX-License-Identifier: GPL-3.0-or-later
// The golden-trace differential: replay what the ENGINE was recorded doing.
//
// `tools/goldentrace.py` runs `gamedata/Runtime 2.exe` under CrossOver and captures
// the operand log the engine writes about itself - every announcing VM handler
// passes its operand to `GetPrivateProfileStringA` on an `IAM\*.TAG` file, and
// that call sits before the debug window's `if (hWnd)`, so it happens in the
// shipped build with no shim, patch or debugger.
//
// This is the only oracle in the project that is not the project checking
// itself. A capture is a whole playthrough and a replay is one entry point, so
// a head-to-head count would be meaningless; what IS comparable is a single
// script. The capture says slot X ran, the port runs slot X, and the two
// operand sequences either agree or they do not.
//
// Two limits, both of which manufacture false disagreements if forgotten:
//
//   * the engine runs scripts CONCURRENTLY, so one script's events are not
//     contiguous in the capture. The match is an ordered SUBSEQUENCE, not a
//     block.
//   * a replay starts from a known state and the game was mid-playthrough, so
//     a script whose branches read the DB can be anchored on a path this state
//     never reaches. That is a missing anchor, not a disagreement, and it is
//     counted separately.
#pragma once

#include "script/gamestate.h"
#include "script/interp.h"
#include "script/script.h"

#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace omk {

// One (domain, value) pair, as the capture records it and as a handler
// announces it.
struct TagEvent {
    std::string domain;
    int         value = 0;
    bool operator<(const TagEvent& o) const {
        return domain != o.domain ? domain < o.domain : value < o.value;
    }
    bool operator==(const TagEvent& o) const {
        return domain == o.domain && value == o.value;
    }
};

// Which operand each handler narrates, from tables/vm_announce.json - DATA,
// because a hand-written map was wrong three ways within an hour.
class AnnounceMap {
public:
    static AnnounceMap loadJson(const std::string& path);
    bool valid() const { return !rows_.empty(); }

    // What this instruction hands the logger, with `Dbg_LogTagged`'s own three
    // filters applied: no -1, no CHARACTERS, no VALUES. Nothing when the
    // handler is silent - which 104 of the 153 opcodes are, and silence has to
    // be read as silence rather than as a default.
    std::optional<TagEvent> of(std::uint8_t op,
                               std::span<const std::uint8_t> operand) const;

private:
    struct Row { std::string domain; int field = 0; };
    std::map<std::uint8_t, Row> rows_;
};

// -> the (domain, key) pairs of a capture, in execution order. Only reads of
// a `.TAG` file count: a Wine process reads other .ini files, CrossOver's own
// licence check among them, and it calls this very API.
std::vector<TagEvent> parseCapture(const std::string& path);

// Every executable world script, named the way the reference names it -
// `"<arch> <chunk> rec <rec> +<field> @<offset>"`.
//
// The OFFSET is part of the key, and that is not tidiness. A chunk's zone
// records and its message-subscription table are walked into the same
// (rec, field) space, so `AREA 222 rec 1 +0` names two different scripts and a
// key without the offset silently drops one. A key that is not unique does not
// fail loudly; it just quietly verifies less.
struct WorldSlot {
    std::string name;
    std::span<const std::byte> code;
    std::size_t at = 0;
    bool replayable = true;   // false for a DIALOG branch script - see below
};
std::vector<WorldSlot> worldSlots(std::span<const std::byte> areaFile,
                                  std::span<const std::byte> sceneFile,
                                  std::span<const std::byte> globalFile);

// The 612 conversation branch scripts, appended so they can DISAMBIGUATE.
//
// They are not world slots and are never replayed - a conversation runs inside
// the dialogue UI, not the script pump - but leaving them out of the index is
// not neutral. A pair that a conversation script could also announce is not
// unique, and an index that cannot see them calls it unique and anchors on a
// world slot that may not be the emitter. So they are indexed and marked
// `replayable = false`; anchoring on one counts as "not replayable", which is
// the honest answer, rather than as agreement or disagreement.
void appendDialogScripts(std::span<const std::byte> dialogFile,
                         std::vector<WorldSlot>& out);

// The TIGHT index: pair -> the slots that could announce it, from a STATIC
// decode (the union over a script's branches). Tight because it is built from
// the single pair each instruction really hands the logger; anchoring off a
// superset made 5 of 10 anchors false on one capture and every one of them
// reported as a mismatch.
std::map<TagEvent, std::set<std::size_t>>
tightIndex(const std::vector<WorldSlot>& slots, const OpcodeTable& table,
           const AnnounceMap& ann);

// Execute one slot and record what it WOULD have announced, in order.
std::vector<TagEvent> replaySlot(const WorldSlot& s, GameState& state,
                                 const OpcodeTable& table,
                                 const AnnounceMap& ann);

struct DiffResult {
    int events = 0, anchors = 0, ok = 0, bad = 0, skipped = 0, unreached = 0;
    std::vector<std::string> mismatched;
};

DiffResult diffCapture(const std::string& capturePath,
                       const std::vector<WorldSlot>& slots,
                       const std::map<TagEvent, std::set<std::size_t>>& tight,
                       GameState state, const OpcodeTable& table,
                       const AnnounceMap& ann);

}  // namespace omk

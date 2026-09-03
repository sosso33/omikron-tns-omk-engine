// SPDX-License-Identifier: GPL-3.0-or-later
// The world scripts: finding them in an AREA/SCENE/GLOBAL chunk, and decoding
// the VM bytecode they hold.
//
// The two halves are separate on purpose. Enumeration is a property of the
// CHUNK layout - where the trigger records live and which of their fields are
// script offsets. Decoding is a property of the VM - one opcode byte, then as
// many operand bytes as the dispatch table says.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

// ------------------------------------------------------------ the VM table

// Operand lengths, indexed by opcode. Loaded from tables/vm_opcodes.json -
// the table lifted out of the executable, kept as DATA rather than baked into
// code so it can still be diffed against the extraction (engine/README.md).
//
// NOTE the file carries two lengths a side. `table_says` is the executable's
// own second dword; `length` is what a decoder must actually use, because
// **17 of them are wrong** - recovered from the handlers' assembly and
// corpus-confirmed. A decoder that trusts the raw table desynchronises on the
// world scripts. This loads `length`.
class OpcodeTable {
public:
    static OpcodeTable loadJson(const std::string& path);
    bool valid() const { return !len_.empty(); }
    std::size_t size() const { return len_.size(); }
    // -1 when the opcode has no entry, which makes it an invalid instruction
    int operandLength(std::uint8_t op) const {
        return op < len_.size() ? len_[op] : -1;
    }
private:
    std::vector<int> len_;
};

// ------------------------------------------------------------- the decoder

struct Instruction {
    std::size_t  pc = 0;
    std::uint8_t op = 0;
    std::vector<std::uint8_t> operand;
};

enum class DecodeStatus {
    Ok,               // reached `end` (opcode 3)
    RanOffTheEnd,
    InvalidOpcode,
    OperandsOffTheEnd,
    Runaway,          // > 20000 instructions: a loop, not a script
};

struct Decoded {
    std::vector<Instruction> code;
    DecodeStatus status = DecodeStatus::RanOffTheEnd;
};

// Decode from `start` until `end` (opcode 3) or a failure. `limit` is the
// chunk's own length - a script may not run past it.
Decoded decodeScript(std::span<const std::byte> b, std::size_t start,
                     std::size_t limit, const OpcodeTable& table);

// ------------------------------------------------------- the slot enumerator

// Where one script lives, and what named it.
struct Slot {
    int         record = 0;   // -1 for the second table and the startup script
    int         field  = 0;
    std::size_t offset = 0;
};

enum class ChunkKind { Area, Scene };

// The trigger records and the second script table of one AREA/SCENE chunk.
//
// Both loaders (sub_40CC90 for AREA, sub_40C120 for SCENE) share one header,
// AREA's shifted 32 bytes on by a leading run of eight -1:
//
//     SCENE   68-byte records at +16, int16 count at +44;
//             8-byte second table at +36, int16 count at +54
//     AREA    68-byte records at +48, int16 count at +76;
//             8-byte second table at +68, int16 count at +86
//
// A record's first three int32 (+0/+4/+8) are relocated from file offsets to
// pointers on load, which is what makes them the script slots.
//
// -> the slots, in the reference reader's order: records first, then the
// second table. Empty when the chunk does not match the layout - count 0 is a
// real, empty area and not a failure, which is the loader's own `if (n > 0)`.
std::vector<Slot> chunkSlots(std::span<const std::byte> b, ChunkKind kind);

// The script slots of IAM\GLOBAL, which is **not an archive**: sub_40DE60
// fopen's it and reads a fixed header -
//
//     +8   int32  script table, file-relative
//     +20  int32  record array, 44 bytes each
//     +24  int16  script count
//     +30  int16  record count
//
// The table is 8 bytes an entry with the offset at +0, and +20 plus 44*count
// lands exactly on the file size. Reading it as an archive instead - which an
// earlier pass did - finds a plausible chunk, then has to guess where the
// table ends; that guess overran by one entry and lost two scripts
// (CLAUDE.md 1). The IamArchive reader still "finds one chunk" here, which is
// why `verify.py: engine IAM` names it as a known false positive.
std::vector<Slot> globalSlots(std::span<const std::byte> d);

// The chunk's MESSAGE SUBSCRIPTIONS - the same 8-byte table `chunkSlots` reads
// script offsets out of, read the other way. `Message_RunHandlers` walks it:
//
//     +0  int32  the handler script, 0 for a subscription with none
//     +4  int16  the message id
//
// So one table serves two purposes, which is why AREA 118's `+68` looked like
// a script pointer: its subscription table is EMPTY and based at the start of
// the code after it (CLAUDE.md 6). A reader that assumed a non-zero pointer
// there meant "a script lives at +68" would be right by coincidence.
struct Subscription {
    std::int32_t script = 0;
    std::int16_t message = 0;
};

std::vector<Subscription> chunkSubscriptions(std::span<const std::byte> b,
                                             ChunkKind kind);
std::vector<Subscription> globalSubscriptions(std::span<const std::byte> d);

}  // namespace omk

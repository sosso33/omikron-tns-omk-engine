// SPDX-License-Identifier: GPL-3.0-or-later
// The 8192-byte game database - one Mem_Calloc(0x2000, 1), and IAM\START is
// that block as shipped (the new-game save, not a container).
//
// Six relocated arrays hang off the header; each is a pointer at +8 + 4*k and
// a count at +32 + 2*k, and `bits` is how wide one entry is. The accessors
// that establish each are named beside them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

inline constexpr std::size_t kGameDbSize = 0x2000;

enum class StateArray {
    Variables      = 0,   // int32  Var_Get / Var_Set        0x0040E510
    SceneOfArea    = 1,   // int16  Area_GetLoadedScene      0x0040B140
    PropState      = 2,   // 2 bits ObjectState_Get          0x0040B010
    ObjectShown    = 3,   // 1 bit  State_SetBit             0x0040AF30
    AddressEnabled = 4,   // 1 bit  Address_SetEnabled       0x0040B090
    ZoneState      = 5,   // 1 bit  Zone_StateBit            0x0040D500
};

class GameState {
public:
    // Load the block from a file (IAM\START). Empty/short input is padded to
    // 0x2000, which is what the engine's calloc gives.
    static GameState fromFile(const std::string& path);
    static GameState fromBytes(std::span<const std::byte> d);

    std::uint32_t offset(StateArray a) const;
    std::uint16_t count(StateArray a) const;

    std::int32_t  var(int i) const;
    void          setVar(int i, std::int32_t v);

    // One bit of a 1-bit map, read-modify-write.
    //
    // NOTE Zone_SetStateBit's DECOMPILATION is wrong - it reads as a bare OR,
    // which could never clear a bit and so could never retire a spent trigger.
    // The assembly read-modify-writes, and that is what this does.
    int  bit(StateArray a, int i) const;
    void setBit(StateArray a, int i, int value);

    // Written by opcode 71 `scene.load`, whose handler ends in
    // Area_SetLoadedScene: which scene is over an area is game STATE, not a
    // property of the area, and Area_TickLoad reads it straight back.
    void setSceneOfArea(int area, std::int16_t scene);
    std::int16_t sceneOfArea(int area) const;

    // Where the player IS. `State_Save` writes the live area and scene back to
    // the header at +1414/+1416 before it copies the block, so a save states
    // its own location - and the block on disk is the only place it is
    // written down.
    std::int16_t currentArea() const;
    std::int16_t currentScene() const;

    // The three object lists `State_Apply` hands to `ObjectList_SetCapacity`,
    // in that order: (offset, capacity).  A fourth - the shop stock - is
    // rebuilt from the resident area and never stored.
    static constexpr int kListOffset[3]   = {848, 884, 1396};
    static constexpr int kListCapacity[3] = {18, 256, 9};
    static constexpr int kLists = 3;

    // ---------------------------------------------------------- OBJECT LISTS
    //
    // A list is `capacity` int16 object ids terminated by -1, and NO count is
    // stored: `ObjectList_SetCapacity` (0x00409B00) recovers it by scanning
    // for the terminator, stopping at the capacity -
    //
    //     result = 0;
    //     while (*v3 != 0xFFFF && result < a2) { ++result; ++v3; }
    //
    // which is what `listCount` is.  Everything else here is one of the four
    // inventory opcodes, transcribed from its handler.
    //
    // The engine keeps a fourth PARALLEL array per list (`dword_69BD68`, 56
    // bytes an entry, copied out of `IAM\OBJECT` on insert) and shifts it in
    // step with the ids.  That array is a runtime cache, not state: it is not
    // in the DB, it is not saved, and the port rebuilds what it holds from the
    // object table by id.  So only the id array is modelled.
    int          listCount(int list) const;
    std::int16_t listAt(int list, int i) const;      // -1 past the end
    // Op 49 `var.set.has_object` (0x0040A440): field 0 selects the LIST and
    // field 1 is the object - `lea eax,[esi+esi*2]` indexes the list table
    // with the first fetch, and the second is what is announced to `OBJECTS`
    // and compared.  (Issue 29 states the two the other way round.)
    bool listHas(int list, int id) const;
    // Op 50 `inventory.add` (0x0040A4D0) -> `Inventory_Insert` (0x004098E0)
    // -> `ObjectList_InsertFront` (0x00409CB0).
    //
    // TWO things about this are not what "add" suggests, and both are the
    // engine's:
    //   * it inserts at the FRONT.  `memmove(base + 2, base, 2*cap - 2)` then
    //     `base[0] = id`, so the newest item is index 0 and the terminator
    //     slides up.  The last slot is dropped, which is safe because
    //   * it REFUSES when the list is full - `if (capacity == count) return 0`
    //     is the first line of `ObjectList_InsertFront`.
    // `noDuplicate` is the handler's own `cmp edi,3 / cmp edi,2` arm: lists 2
    // and 3 refuse an id they already hold, lists 0 and 1 do not.  Use
    // `listRefusesDuplicates(list)` to reproduce that.  Returns whether the
    // list changed.
    //
    // NOT modelled here: `Inventory_Insert`'s kind arithmetic, which merges
    // ammunition and money into an existing slot's quantity (kinds 2..6 and
    // 7..11) instead of taking a slot.  That lives in the 56-byte cache above
    // and belongs with the inventory channel, not with the stored state.
    static constexpr bool listRefusesDuplicates(int list) {
        return list == 2 || list == 3;
    }
    bool listAdd(int list, int id, bool noDuplicate = false);
    // Op 51 `inventory.remove` (0x0040A5A0): find the id, shift the tail down
    // one slot and write 0xFFFF into the LAST slot - which is why the count
    // stays recoverable.  Removes ONE copy; false when the id is not there.
    bool listRemove(int list, int id);
    // Op 52 `inventory.remove_all` (0x0040A6A0), main path: the same removal,
    // repeated until the id is gone.  Returns how many went.  (The handler's
    // id == -1 path removes every entry whose 56-byte cache record has bit 1
    // of `+0x24` set, which needs the array this does not model.)
    int  listRemoveAll(int list, int id);
    // PORT-SIDE ONLY - no engine function empties a list.  This is the state a
    // fresh DB ships with (every slot 0xFFFF), for tests and resets.
    void listClear(int list);

    // ------------------------------------------------------------ PROP STATE
    //
    // `ObjectState_Get` (0x0040B010) / `ObjectState_Set` (0x0040AFC0): two
    // bits per prop in `StateArray::PropState`, index from the prop record's
    // `+0x16`.  Ops 68 `object.release` and 76 `object.show` read bit 0 and
    // write bit 1 through them.
    //
    // `propState` transcribes the getter INCLUDING its sign extension, which
    // is a real quirk of the shipped build and not a decompiler artefact: the
    // byte is loaded `movsx`, the mask `3 << 2*(i%4)` is built in a byte
    // register and also `movsx`'d, and the result is `sar`'d.  For i % 4 == 3
    // the mask is 0xC0, so state 2 reads back as **-2** and state 3 as **-1**.
    // Every shipped consumer masks the low byte (`test al,1`, `or al,2`), so
    // nothing in the game can see the difference - use `propStateBits` for the
    // plain 0..3 value.
    int  propState(int index) const;
    int  propStateBits(int index) const;
    void setPropState(int index, int value);          // value & 3, as the setter

    // ------------------------------------------------------- CLOCK AND TIMER
    //
    // Neither is in the 8192-byte image: they are engine globals
    // (`g_ClockTime` = dword_4C2BD0, `g_ClockDay`, and the timer's
    // dword_930760/64/68).  They are here because they are game state a script
    // reads and writes, and because the clock is the only thing the timer
    // counts against.  `raw()`, `walk()` and the save round trip are
    // unaffected; the save carries day and time BESIDE the DB (savefile.h's
    // 32 + 4 + 4 header), and the timer is not saved at all.
    //
    // THE UNIT IS A MILLISECOND, derived rather than assumed: `Clock_Tick`
    // (0x0041E600) accumulates the frame delta - which `Game_Frame` sets to
    // 30/fps, so 1.0 is 1/30 s at any frame rate - and every 5.0 of those
    // (1/6 s) adds **166**.  That is 996 units a real second.  The corroborations
    // agree: `Timer_Format` divides the same quantity by 1000 for seconds and
    // takes `% 1000 / 10` for hundredths, and op 113 multiplies its operand by
    // 1000, so `timer.set 900` is the bomb's 15 minutes.
    static constexpr int kClockUnitsPerDay = 3600000;   // and 21 hours in one

    std::int32_t clock() const { return clockTime_; }
    std::int32_t clockDay() const { return clockDay_; }
    // `Clock_SetTime` (0x0041E750) / `Clock_SetDay`.  `Game_NewGame` sets
    // 2000000 and day 52; a save restores the pair it stored.  The divisors
    // `Clock_SetTime` also computes are display-only (`Clock_FormatTime`).
    void setClock(std::int32_t t) { clockTime_ = t; clockAccum_ = 0.0f; }
    void setClockDay(std::int32_t d) { clockDay_ = d; }
    // `Clock_Tick`, in FRAME units (1.0 = one frame at 30 Hz).
    void clockTick(float frames);

    // The timer flags, `g_TimerFlags`.  Bit 0 is the one every entry point
    // tests; the rest come from `Timer_Format` / `sub_41E480`.
    enum TimerFlag {
        kTimerStopped   = 0x01,   // set = halted; `Timer_Elapsed` reads 0 when
                                  //   the flags are EXACTLY 1
        kTimerCountdown = 0x04,   // `Timer_Format` shows value - elapsed
        kTimerVisible   = 0x08,   // `sub_41E480` draws the HH:MM:SS readout
        kTimerExpired   = 0x10,   // frozen at the value; set by the expiry
    };

    // THE OPCODE NAMES IN `tables/vm_opcodes.json` ARE THE WRONG WAY ROUND.
    // The mechanism, from the two label-less functions the handlers jump to:
    //
    //   op 110  0x00405340 -> sub_41E260   flags = 1                (unnamed,
    //                                                            0 shipped sites)
    //   op 111  0x00405350 -> loc_41E2B0   needs !(flags & 1); flags |= 1
    //   op 112  0x00405360 -> loc_41E2D0   needs   flags & 1;
    //                                      start = clock; flags &= ~0x11
    //
    // so 111 HALTS and 112 RUNS, while the table calls 111 `timer.start` and
    // 112 `timer.stop`.  The shipped scripts settle it: the Tetra bombs do
    // `timer.mode 12` (countdown|visible), `timer.set 900`, then op 112 - and
    // `Timer_SetValue`/`Timer_SetMode` both refuse unless `flags & 1`, so the
    // configure pair can only precede the start; and the shooting range does
    // `shoot.end`, op 111, then `var.set.timer` into a `TEMPS n` variable it
    // shows with `ui.highscore`, which is a stop followed by reading the time.
    // 12 of each of mode/set (all `12` and `900`), 13 of 111, 15 of 112.
    //
    // These are named for what they DO.  Wire op 111 to `timerStop` and op 112
    // to `timerStart`.
    void timerReset();                       // op 110
    bool timerStop();                        // op 111
    bool timerStart();                       // op 112
    // op 113 `timer.set` -> `Timer_SetValue` (0x0041E270).  The handler passes
    // `field * 1000` (its `lea` triple is x125 then x8), so THIS takes
    // milliseconds and the opcode does the multiply.  Refused unless stopped.
    bool timerSet(std::int32_t ms);
    // op 114 `timer.mode` -> `Timer_SetMode` (0x0041E290): `flags = mode | 1`,
    // refused unless stopped.  Shipped mode is always 12.
    bool timerMode(int mode);
    // op 115 `var.set.timer` -> `Timer_Elapsed` (0x0041E430).  Writes the
    // value when expired, 0 when the flags are exactly 1, else clock - start;
    // returns whether the timer was actually running, which the opcode
    // ignores - it stores `out` either way.  Note `Timer_Elapsed` does NOT
    // apply the countdown flag; only `Timer_Format`'s display does.
    bool timerElapsed(std::int32_t& out) const;
    // The tail of `sub_41E480`, which the frame calls: nothing happens while
    // the timer is stopped; otherwise, once the clock passes start + value,
    // the timer freezes (flags |= 0x10), rebases and the caller raises event
    // 43 with parameter 18.  True on the one frame that happens.
    bool timerCheckExpiry();

    int          timerFlags() const { return timerFlags_; }
    std::int32_t timerValue() const { return timerValue_; }
    std::int32_t timerBase()  const { return timerStart_; }   // g_TimerStart
    // The player's character record and the two bio strings its +0 and +4
    // point at.
    static constexpr int kPlayerRecord = 60, kPlayerRecordSize = 276;
    static constexpr int kBio[2] = {336, 592}, kBioSize = 256;

    // How many BYTES the k'th array occupies, from its own count and the
    // entry width - which is what makes the walk a test: the segments have to
    // tile the image with only alignment padding between them.
    std::size_t arrayBytes(StateArray a) const;

    struct Segment { const char* name; std::size_t begin, end; };

    // Every segment of the image, in file order.  The fixed part is laid out
    // by `State_Apply` itself; the six arrays sit where the header says and
    // are as long as its counts say.
    std::vector<Segment> walk() const;

    // `State_Apply`: file offsets -> absolute pointers.  Over a base of 0 the
    // six offset fields are unchanged, so what this really models is the rest:
    // the two bio pointers planted in the player record, and the scene the
    // current area is holding.
    void relocate();
    // `State_Save`: the header fields the serializer rewrites from the live
    // game before it copies the block back out.
    void unrelocate();

    std::size_t imageSize() const { return imageSize_; }

    std::span<const std::byte> raw() const { return {raw_.data(), raw_.size()}; }
    // The same block, WRITABLE, for the two engine paths that copy whole
    // records into it - `player.become`'s bio strings and player record
    // (`rep movsd` into `dword_69BC6C`, area.cpp) - rather than a field the
    // setters above name. Bounds are the caller's to respect.
    std::span<std::byte> rawMutable() { return {raw_.data(), raw_.size()}; }

private:
    std::vector<std::byte> raw_;
    std::size_t imageSize_ = 0;      // what the FILE held, before the pad
    // The clock and the timer - engine globals, NOT part of the image above.
    // `g_TimerFlags` starts at 1: `Game_Init`'s teardown/reset path writes
    // exactly that (04_sys.c:4592), and 1 is the state the two configure calls
    // demand.
    std::int32_t clockTime_ = 0, clockDay_ = 0;
    float        clockAccum_ = 0.0f;         // dword_4E7E88, in frame units
    std::int32_t timerFlags_ = kTimerStopped, timerValue_ = 0, timerStart_ = 0;

    // A list's slot address, or npos when the list or the index is out of range.
    std::size_t listSlot(int list, int i) const;
    std::int16_t listRead(std::size_t o) const;
    std::uint32_t u32(std::size_t o) const;
    void put32(std::size_t o, std::int32_t v);
    void put16(std::size_t o, std::int16_t v);
    std::uint16_t u16(std::size_t o) const;
};

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
// `.CTL` control files - a character's animation state machine.
//
// Magic "CE70", loaded by InitCEFFile (the engine's own name for it, from its
// error strings, which also call the file a "bank list"). The whole file is a
// **byte walk**: a fixed header, then entries, then nine variable-length
// sections in a fixed order, each present only when a flag on the entry says
// so. Nothing carries a pointer to the next section - the reader has to add up
// the sizes, which is why the walk landing exactly on the file size is the
// invariant that matters.
//
//     +12          uint32 groupCount
//     +88          group[groupCount], 32 bytes: +4 is its entry count
//     then         entry[...], 88 bytes each, groups in order
//     then, per entry and in THIS order, when its flag is set:
//         name       12 B   unless flags & 0x8002   (an unnamed junction)
//         children    4 B x entry[+87]
//         parents     4 B x entry[+86]
//         turn       24 B   when flags & 0x140
//         rootShift  20 B   when flags & 0x280
//         moveName   12 B   when entry[+8] & 0x10
//         combat     40 B   when flags & 0x2000000
//     then         the fight-AI table: 156 B a profile, 12 situation slots
//                  each of 16 B an item, then the items' input words
//     then         per entry with entry[+76] & 8: a 32-byte-strided block
//     then         the clips: uint32 length + payload, one per distinct name
//
// The trailing pointer fields the format seems to carry are DEAD: InitCEFFile
// never follows them, it recomputes them. Chasing them is what made this look
// unsolvable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace omk {

struct CtlClip {
    std::string name;
    std::size_t offset = 0;
    std::size_t length = 0;
    // How long the clip runs, which the live machine needs on every
    // transition: `GoToMove` writes it into the channel's `+8` and
    // `Cef_TickChannel` compares the frame against it to decide the state is
    // over. A `.CTL` clip is an `.ani` DESCRIPTOR with no "3.0V" wrapper -
    // `sub_45D1F0` hands each block straight to `Anim_RegisterClip` - so this
    // is `animDescriptor(...).frames`, read through the same code path the
    // libraries go through rather than a second guess at the layout.
    std::int32_t frames = 0;
};

// The COMBAT block - 40 bytes, present when the entry's flags carry
// 0x2000000. `Fight_ResolveHit` (0x0049A960) is what names the fields: it
// takes the damage, checks the hit window against the attacker's clock, and
// puts the victim into a reaction state named by the LOW SIXTEEN BITS of a
// state id.
//
// Read as ten floats by the walk, because that is how it is stored - but
// three of them are integers wearing a float's clothing, which the corpus
// settles: damage runs 1..25 over 116 blocks and the two reaction refs
// resolve to a real state in 232 of 232.
struct CtlCombat {
    float raw[10] = {};
    std::int32_t damage() const { return asInt(1); }
    std::int32_t reactionA() const { return asInt(5); }   // -1 = none
    std::int32_t reactionB() const { return asInt(6); }
    std::int32_t asInt(int i) const;
};

struct CtlState {
    std::size_t  offset = 0;
    int          group  = 0;
    std::uint32_t id    = 0;
    std::string  name;
    std::uint32_t flags = 0;
    std::vector<std::uint32_t> parents, children;
    std::uint32_t gotoId = 0;
    int          clip = -1;
    bool childOk = true, parentOk = true, gotoOk = true;

    // What `Cef_FindTransition` consumes, in the record itself.
    //
    // `+4` is an INPUT BITFIELD an entry waits for. `0x80000000` is the
    // "no input" sentinel and is NOT the queue's idle word 0x40000000 -
    // `Cef_InputMatches` ends `if (a1 == 0x80000000) return a2 <= 0x2000`, so
    // the idle word matches no idle edge (docs/ASSETS.md).
    std::uint32_t inputCode = 0;         // +4
    // `+12`'s low half is a ROLE code. Six of them - 3, 4, 5, 9, 18, 20 - are
    // what `Fight_Begin` caches, and every combat file carries exactly one
    // entry of each.
    std::uint16_t role = 0;              // +12
    // The CANCEL WINDOW, a pair of times in frames within which a transition
    // may interrupt this entry. 168 entries carry one and none is malformed.
    float cancelFrom = 0.0f, cancelTo = 0.0f;   // +16, +20
    std::uint16_t priority = 0;          // +84, only ever 0, 1 or 2
    bool hasCombat = false;
    CtlCombat combat;

    // ---- what the LIVE machine reads, beyond the data half above --------
    //
    // These are the fields `GoToMove` (0x004A7B80) and `Cef_TickChannel`
    // (0x004A8160) consume on every transition. They were skipped by the walk
    // before because nothing consumed them; `actor/channel.cpp` does.
    std::uint32_t flags12 = 0;           // +12; the low half is `role` above,
                                         //      0x10000000 = knockdown
    float startFrame = 0.0f;             // +24 where playback begins when the
                                         //     state is entered through an
                                         //     alias (flag 2) or a junction
    std::uint16_t playBits = 0;          // +76 bit 3 = has effect records,
                                         //     0x20 = the group default AND
                                         //     scan children in reverse,
                                         //     high nibble = the play mode
    std::uint16_t blendFrames = 0;       // +78 frames to blend on a 0x8000 exit
    std::uint16_t phaseOffset = 0;       // +80 added when 0x10000 phase-matches
    bool hasEffects = false;             // +76 bit 3
    bool hasTurn = false;                // flags & 0x140  -> +44, 24 B
    bool hasShift = false;               // flags & 0x280  -> +48, 20 B
    float turn[6]  = {};                 // start, end, dX, dY, dZ, ?
    float shift[5] = {};                 // start, end, dx, dy, dz
    std::string moveName;                // flags & 0x10 -> tab_special_move[]

    // The edges, RESOLVED to indices into `CtlFile::states`. The file stores
    // ids, and `InitCEFFile`'s link pass turns them into pointers - within the
    // state's own group for parents and children, file-wide for the GoTo. A
    // runtime that re-resolved them per tick would be doing the load pass
    // every frame; more to the point, resolving once is what lets the sweep
    // assert that every landing is a real entry.
    std::vector<int> childIdx, parentIdx;
    int gotoIdx = -1;
};

// One move set. `Cef_FindGroupById` matches `id` - the engine asks for 2
// (falling), 45 (get up), 100 (locomotion, the default), 200 (shoot stance),
// 300 (ladder) and 400 (dialogue stance) by number - and `Cef_DefaultGroup`
// picks the one whose `flags` bit 0 is set, of which there is exactly one per
// file. The combat files keep stale authoring ids and are reached through the
// graph instead.
struct CtlGroup {
    std::int32_t  id = 0;
    std::uint32_t flags = 0;
    int first = 0;      // index into CtlFile::states
    int count = 0;
    int defaultEntry = -1;   // the flag-0x20 entry Cef_DefaultEntry returns
};

// The FIGHT AI, out of the `+76`/`+80` table the walk used to only step over.
//
// One 156-byte profile per difficulty level, `+0` being the level plus one -
// which is how `sub_45DCB0` picks one.  A "move" is not a single button: it is
// a SEQUENCE of input words that `Fight_TickAI` pushes into the actor's own
// input queue with `Perso_InjectInput`, so the AI drives the same `.CTL` state
// machine the player does, by pressing buttons.  There is no separate AI
// animation path, which is why this belongs in the format reader and not in a
// combat module.
//
//     +0        int32  the id: difficulty level + 1
//     +4,+6     uint16 the delay range entering a fight, in ms
//     +8,+10    uint16 the delay range between moves
//     +16+12k   {int32 count, ptr, int32}  twelve SITUATION slots
//
// Slot 7 is the one `Fight_TickAI` reads at `+100`/`+104` in its state-6/21
// branch, and that is what fixes both the 12-byte slot stride and the order of
// the two fields.
//
// `0x40000000` is the queue's IDLE word - `Perso_SetInputEnabled` resets the
// queue to exactly it - interleaved between presses so the machine sees a
// release.  It is not the `0x80000000` "no input" edge code: `Cef_InputMatches`
// ends `if (a1 == 0x80000000) return a2 <= 0x2000`, so the idle word matches no
// idle edge.
struct CtlAiSlot {
    std::vector<std::vector<std::uint32_t>> moves;  // each move a word sequence
};

struct CtlAiProfile {
    std::uint32_t id = 0;                 // difficulty level + 1
    std::uint16_t enterDelay[2] = {0, 0};
    std::uint16_t moveDelay[2]  = {0, 0};
    CtlAiSlot slots[12];
};

struct CtlFile {
    bool valid = false;
    std::size_t size = 0;
    std::size_t end  = 0;      // where the walk finished
    bool exact = false;        // end == size: the invariant
    int  groups = 0;
    std::vector<CtlGroup> groupList;
    std::vector<CtlClip>  clips;
    std::vector<CtlState> states;
    std::vector<CtlAiProfile> ai;
};

CtlFile readCtl(std::span<const std::byte> d);

}  // namespace omk

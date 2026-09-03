// SPDX-License-Identifier: GPL-3.0-or-later
// `IAM\OBJECT` - the inventory objects.
//
// NOT a directory archive, despite the `IAM\` path: `Archive_ReadChunk(path,
// index, 1304, 2048)` reads a fixed slot, `offset = index * 2048`, of which the
// first 1304 bytes are the record.  1002 slots ship.  Trying to walk it as an
// archive finds a directory that is really the first record's fields.
//
//     +0    int16     id - equals the slot number in all 1002
//     +2    int16     kind   -1 and 0 carry no inventory behaviour (the 561
//                            ZVO voice rows are 526 of kind 0, 34 of -1 and
//                            one of 16); 1..6 guns, 7..11 their ammunition,
//                            12 seteks (money), 13 rings, 15/16 documents.
//                            `media.play` (op 92) branches on 16: a kind-16
//                            record is a full-screen `IMAGES\<stem>.BMP`
//                            rather than a `VOICEOFF\<stem>.ADP`.
//                            1..6 guns, 7..11 their ammunition,
//                            12 seteks (money), 13 rings, 15/16 documents
//     +4    int16     flags  bit 0 usable, bit 1 lost on reincarnation,
//                            bit 3 spell, bit 4 document, bit 5 counted valuable
//     +6    int16     effect - which actor property a consumable restores
//     +8    int16     amount - added to it on use
//     +10   int16     price in seteks
//     +12   int16     quantity - ammo per pack, seteks value, ring count
//     +14   char[10]  asset stem: MESHES\OBJETS\<stem>.3DO, and <stem>.ADP
//     +24   char[32]  display name
//     +280  char[1024] description - the pickup toast
//
// `Object_ApplyEffect` is what settles `+6`/`+8`: it maps the effect onto an
// `Actor_SetProperty` id and adds the amount.  The map is small enough to be
// data, so it is data here.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace omk {

class DataFs;

struct ObjectRecord {
    int index = 0;
    std::int16_t id = 0, kind = 0, flags = 0, effect = 0, amount = 0;
    std::int16_t price = 0, quantity = 0;
    std::string stem, name, description;

    bool usable()   const { return (flags & 0x01) != 0; }
    bool spell()    const { return (flags & 0x08) != 0; }
    bool document() const { return (flags & 0x10) != 0; }
    bool valuable() const { return (flags & 0x20) != 0; }
};

// The whole file, one entry per slot.  A slot the file does not reach is not
// invented: the vector is as long as the file allows.
std::vector<ObjectRecord> loadObjects(std::span<const std::byte> file);
std::vector<ObjectRecord> loadObjects(const DataFs& fs);

// `Object_ApplyEffect`'s consumable map: effect -> actor property, or 0 for an
// effect the function does not handle as a plain consumable.
int effectProperty(int effect);

}  // namespace omk

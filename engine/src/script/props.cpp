// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/props.h"

namespace omk {
namespace {

std::uint32_t u32(std::span<const std::byte> b, std::size_t o) {
    if (o + 4 > b.size()) return 0;
    return static_cast<std::uint32_t>(b[o])       |
           static_cast<std::uint32_t>(b[o + 1]) <<  8 |
           static_cast<std::uint32_t>(b[o + 2]) << 16 |
           static_cast<std::uint32_t>(b[o + 3]) << 24;
}
std::uint16_t u16(std::span<const std::byte> b, std::size_t o) {
    if (o + 2 > b.size()) return 0;
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(b[o]) |
                                      static_cast<std::uint16_t>(b[o + 1]) << 8);
}
std::int16_t i16(std::span<const std::byte> b, std::size_t o) {
    return static_cast<std::int16_t>(u16(b, o));
}
void put16(std::span<std::byte> b, std::size_t o, std::uint16_t v) {
    if (o + 2 > b.size()) return;
    b[o]     = static_cast<std::byte>(v & 0xFF);
    b[o + 1] = static_cast<std::byte>(v >> 8);
}

// One table of `stride`-byte records at `ptr`/`count`, the loaders' own
// `if (n > 0)` and the walk refusing a table that runs off the chunk.
template <class F>
std::optional<std::size_t> walk(std::span<const std::byte> b, std::size_t po,
                                std::size_t co, std::size_t stride, F&& match) {
    if (b.size() < co + 2) return std::nullopt;
    const std::size_t p = u32(b, po);
    const int n = i16(b, co);
    if (n <= 0 || p + stride * static_cast<std::size_t>(n) > b.size()) return std::nullopt;
    for (int i = 0; i < n; ++i) {
        const std::size_t o = p + stride * static_cast<std::size_t>(i);
        if (match(o)) return o;
    }
    return std::nullopt;
}

PropRecord propAt(std::span<const std::byte> b, std::size_t o) {
    PropRecord r;
    r.offset = o;
    r.slot = i16(b, o);
    r.id = i16(b, o + 2);
    r.stateIndex = i16(b, o + 22);
    return r;
}

}  // namespace

std::optional<PropRecord> findPropById(std::span<const std::byte> chunk,
                                       ChunkKind kind, int id) {
    const std::size_t po = kind == ChunkKind::Area ? 44 : 12;
    const std::size_t co = kind == ChunkKind::Area ? 74 : 42;
    const auto o = walk(chunk, po, co, 24,
                        [&](std::size_t r) { return i16(chunk, r + 2) == id; });
    if (!o) return std::nullopt;
    return propAt(chunk, *o);
}

std::optional<PropRecord> findPropBySlot(std::span<const std::byte> chunk,
                                         ChunkKind kind, int slot) {
    const std::size_t po = kind == ChunkKind::Area ? 44 : 12;
    const std::size_t co = kind == ChunkKind::Area ? 74 : 42;
    const auto o = walk(chunk, po, co, 24,
                        [&](std::size_t r) { return i16(chunk, r) == slot; });
    if (!o) return std::nullopt;
    return propAt(chunk, *o);
}

std::optional<CharacterRecord> findCharacterRecord(std::span<const std::byte> chunk,
                                                   ChunkKind kind, int id) {
    const std::size_t po = kind == ChunkKind::Area ? 40 : 8;
    const std::size_t co = kind == ChunkKind::Area ? 72 : 40;
    const auto o = walk(chunk, po, co, 20,
                        [&](std::size_t r) { return i16(chunk, r + 2) == id; });
    if (!o) return std::nullopt;
    CharacterRecord c;
    c.offset = *o;
    c.index = i16(chunk, *o);
    c.id = i16(chunk, *o + 2);
    c.stateBit = i16(chunk, *o + 18);
    return c;
}

std::optional<std::size_t> findActorRecord(std::span<const std::byte> chunk,
                                           ChunkKind kind, int id) {
    const std::size_t po = kind == ChunkKind::Area ? 56 : 24;
    const std::size_t co = kind == ChunkKind::Area ? 80 : 48;
    return walk(chunk, po, co, kActorRecordSize,
                [&](std::size_t r) { return i16(chunk, r + 272) == id; });
}

bool readActorProperty(std::span<const std::byte> record, int property,
                       std::int32_t& out) {
    if (record.size() < kActorRecordSize) return false;
    switch (property) {
        case 1:    out = i16(record, 170); return true;
        case 2:    out = i16(record, 156); return true;
        case 3:    out = i16(record, 158); return true;
        case 4:    out = u16(record, 172); return true;   // `movzx`: Argent is unsigned
        case 5:    out = i16(record, 174); return true;
        case 7:    out = static_cast<std::int32_t>(u32(record, 176)); return true;
        case 8:    out = i16(record, 154); return true;
        case 16:   out = i16(record, 160); return true;
        case 17:   out = i16(record, 162); return true;
        case 18:   out = i16(record, 164); return true;
        case 19:   out = i16(record, 166); return true;
        case 20:   out = i16(record, 168); return true;
        case 0x17: out = i16(record, 200); return true;
        case 0x18: out = i16(record, 198); return true;
        case 0x19: out = i16(record, 192); return true;
        case 0x1A: out = i16(record, 180); return true;
        case 0x1B: out = i16(record, 186); return true;
        case 0x1C: out = i16(record, 184); return true;
        case 0x1D: out = i16(record, 182); return true;
        case 0x1E: out = i16(record, 188); return true;
        case 0x1F: out = i16(record, 190); return true;
        case 0x20: out = i16(record, 196); return true;
        case 0x21: out = i16(record, 194); return true;
        case 0x25: out = i16(record, 250); return true;
        default:   return false;   // pointer slot, or an input-indexed case
    }
}

bool writeActorProperty(std::span<std::byte> record, int property,
                        std::int32_t value) {
    if (record.size() < kActorRecordSize) return false;
    // `mov esi, [eax+8] ; cmp esi, 0C8h ; jbe` - the value is compared as an
    // UNSIGNED dword, so -5 is above 200 and lands at 200.
    auto v = static_cast<std::uint32_t>(value);
    const auto clamp200 = [&] { if (v > 0xC8u) v = 200u; };
    std::size_t off = 0;
    switch (property) {
        case 1:  clamp200(); off = 170; break;
        case 2:  clamp200(); off = 156; break;
        case 3:  clamp200(); off = 158; break;
        case 4:  if (v > 0xFFFFu) v = 0xFFFFu; off = 172; break;
        case 5:  off = 174; break;
        case 16: clamp200(); off = 160; break;
        case 17: clamp200(); off = 162; break;
        case 18: clamp200(); off = 164; break;
        case 19: clamp200(); off = 166; break;
        case 20: clamp200(); off = 168; break;
        case 0x23: {
            // `u16(Actor_FindById(v2) + 2 * HIWORD(v3), 260) = v3`: the
            // ammunition array at +260, the slot in the high word. +260 +
            // 2*k must stay inside the record, which the engine does not test.
            const std::size_t k = v >> 16;
            if (260 + 2 * k + 2 > kActorRecordSize) return false;
            off = 260 + 2 * k;
            break;
        }
        default: return false;
    }
    put16(record, off, static_cast<std::uint16_t>(v & 0xFFFFu));
    return true;
}

int heldObjectOf(std::span<const std::byte> record) {
    if (record.size() < kActorRecordSize) return -1;
    return i16(record, 270);
}

void setHeldObjectOf(std::span<std::byte> record, int objectId) {
    if (record.size() < kActorRecordSize) return;
    put16(record, 270, static_cast<std::uint16_t>(objectId & 0xFFFF));
}

}  // namespace omk

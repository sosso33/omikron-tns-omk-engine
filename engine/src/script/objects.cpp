// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/objects.h"

#include "platform/datafs.h"

namespace omk {
namespace {

constexpr std::size_t kSlot = 2048, kUsed = 1304;

std::int16_t i16(std::span<const std::byte> d, std::size_t o) {
    if (o + 2 > d.size()) return 0;
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(d[o]) |
                                     (static_cast<std::uint16_t>(d[o + 1]) << 8));
}

// cp1252 bytes up to the first NUL, kept as bytes - the caller decides the
// encoding, and every comparison this engine makes is byte-for-byte.
std::string str(std::span<const std::byte> d, std::size_t o, std::size_t n) {
    std::string s;
    for (std::size_t k = 0; k < n && o + k < d.size(); ++k) {
        const auto c = static_cast<unsigned char>(d[o + k]);
        if (!c) break;
        s.push_back(static_cast<char>(c));
    }
    return s;
}

}  // namespace

std::vector<ObjectRecord> loadObjects(std::span<const std::byte> f) {
    std::vector<ObjectRecord> out;
    for (std::size_t i = 0; (i + 1) * kSlot <= f.size() ||
                            i * kSlot + kUsed <= f.size(); ++i) {
        const std::size_t o = i * kSlot;
        if (o + kUsed > f.size()) break;
        ObjectRecord r;
        r.index       = static_cast<int>(i);
        r.id          = i16(f, o + 0);
        r.kind        = i16(f, o + 2);
        r.flags       = i16(f, o + 4);
        r.effect      = i16(f, o + 6);
        r.amount      = i16(f, o + 8);
        r.price       = i16(f, o + 10);
        r.quantity    = i16(f, o + 12);
        r.stem        = str(f, o + 14, 10);
        r.name        = str(f, o + 24, 32);
        r.description = str(f, o + 280, 1024);
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<ObjectRecord> loadObjects(const DataFs& fs) {
    return loadObjects(fs.read("IAM/OBJECT"));
}

int effectProperty(int effect) {
    switch (effect) {
        case 1: return 2;   // Mana
        case 2: return 3;   // Carac Speed
        case 3: return 16;  // Carac Attack
        case 4: return 17;  // Carac Body Shield
        case 5: return 18;  // Carac Dodge
        case 6: return 1;   // Vie
        case 7: return 19;  // Carac Fight Experience
        default: return 0;
    }
}

}  // namespace omk

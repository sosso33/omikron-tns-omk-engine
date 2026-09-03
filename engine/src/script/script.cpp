// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/script.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace omk {
namespace {

std::int32_t i32(std::span<const std::byte> d, std::size_t o) {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(d[o    ])       |
        static_cast<std::uint32_t>(d[o + 1]) <<  8 |
        static_cast<std::uint32_t>(d[o + 2]) << 16 |
        static_cast<std::uint32_t>(d[o + 3]) << 24);
}

std::int16_t i16(std::span<const std::byte> d, std::size_t o) {
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>(d[o]) |
        static_cast<std::uint16_t>(d[o + 1]) << 8);
}

}  // namespace

// ------------------------------------------------------------ the VM table

OpcodeTable OpcodeTable::loadJson(const std::string& path) {
    OpcodeTable t;
    std::ifstream f(path);
    if (!f) return t;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();

    // A deliberately small scan rather than a JSON library: the file is ours,
    // its shape is fixed by tools/exetables.py, and a dependency here would
    // have to be justified for one table. Each row carries `"op": N` followed
    // by `"length": M`, and `length` may be null for an opcode with no entry.
    std::size_t i = 0;
    while ((i = s.find("\"op\":", i)) != std::string::npos) {
        i += 5;
        const int op = std::atoi(s.c_str() + i);
        const auto lp = s.find("\"length\":", i);
        if (lp == std::string::npos) break;
        const auto vp = lp + 9;
        int len = -1;
        std::size_t k = vp;
        while (k < s.size() && (s[k] == ' ' || s[k] == '\t')) ++k;
        if (k < s.size() && (std::isdigit(static_cast<unsigned char>(s[k])) || s[k] == '-'))
            len = std::atoi(s.c_str() + k);
        if (op >= 0 && op < 256) {
            if (t.len_.size() <= static_cast<std::size_t>(op)) t.len_.resize(op + 1, -1);
            t.len_[static_cast<std::size_t>(op)] = len;
        }
        i = vp;
    }
    return t;
}

// ------------------------------------------------------------- the decoder

Decoded decodeScript(std::span<const std::byte> b, std::size_t start,
                     std::size_t limit, const OpcodeTable& table) {
    Decoded out;
    std::size_t pc = start;
    for (;;) {
        if (pc >= limit) { out.status = DecodeStatus::RanOffTheEnd; return out; }
        const auto op = static_cast<std::uint8_t>(b[pc]);
        const int n = table.operandLength(op);
        if (n < 0) { out.status = DecodeStatus::InvalidOpcode; return out; }
        if (pc + 1 + static_cast<std::size_t>(n) > limit) {
            out.status = DecodeStatus::OperandsOffTheEnd; return out;
        }
        Instruction ins;
        ins.pc = pc;
        ins.op = op;
        ins.operand.reserve(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k)
            ins.operand.push_back(static_cast<std::uint8_t>(b[pc + 1 + static_cast<std::size_t>(k)]));
        out.code.push_back(std::move(ins));
        pc += 1 + static_cast<std::size_t>(n);
        if (op == 3) { out.status = DecodeStatus::Ok; return out; }   // `end`
        if (out.code.size() > 20000) { out.status = DecodeStatus::Runaway; return out; }
    }
}

// ------------------------------------------------------- the slot enumerator

std::vector<Slot> chunkSlots(std::span<const std::byte> b, ChunkKind kind) {
    std::vector<Slot> out;
    const std::size_t recPtr = kind == ChunkKind::Area ? 48u : 16u;
    const std::size_t recCnt = kind == ChunkKind::Area ? 76u : 44u;
    const std::size_t tabPtr = kind == ChunkKind::Area ? 68u : 36u;
    const std::size_t tabCnt = kind == ChunkKind::Area ? 86u : 54u;

    // the 68-byte trigger records: fields +0, +4 and +8 are script offsets
    if (b.size() >= recCnt + 2) {
        const auto lo = i32(b, recPtr);
        const auto n  = i16(b, recCnt);
        // count 0 is a real, empty area - the loader guards with `if (n > 0)`
        if (n >= 0 && lo > 0 &&
            static_cast<std::size_t>(lo) + 68u * static_cast<std::size_t>(n) <= b.size()) {
            for (int i = 0; i < n; ++i) {
                const auto o = static_cast<std::size_t>(lo) + 68u * static_cast<std::size_t>(i);
                for (int field : {0, 4, 8}) {
                    const auto fo = o + static_cast<std::size_t>(field);
                    if (fo + 4 > b.size()) return out;
                    const auto p = i32(b, fo);
                    if (p > 0 && static_cast<std::size_t>(p) < b.size())
                        out.push_back(Slot{i, field, static_cast<std::size_t>(p)});
                }
            }
        } else if (lo <= 0 || n < 0) {
            return out;                    // the chunk does not match the layout
        }
    }

    // the second table: 8 bytes an entry, the offset at +0. AREA's holds six
    // dialog.start sites the record walk alone never sees.
    if (b.size() >= tabCnt + 2) {
        const auto lo = i32(b, tabPtr);
        const auto n  = i16(b, tabCnt);
        if (n > 0 && lo > 0 &&
            static_cast<std::size_t>(lo) + 8u * static_cast<std::size_t>(n) <= b.size()) {
            for (int i = 0; i < n; ++i) {
                const auto o = static_cast<std::size_t>(lo) + 8u * static_cast<std::size_t>(i);
                if (o + 4 > b.size()) break;
                const auto p = i32(b, o);
                if (p > 0 && static_cast<std::size_t>(p) < b.size())
                    out.push_back(Slot{i, 0, static_cast<std::size_t>(p)});
            }
        }
    }
    return out;
}

std::vector<Slot> globalSlots(std::span<const std::byte> d) {
    std::vector<Slot> out;
    if (d.size() < 32) return out;
    const auto tbl = i32(d, 8);
    const auto n   = i16(d, 24);
    if (tbl <= 0 || n <= 0) return out;
    for (int i = 0; i < n; ++i) {
        const auto o = static_cast<std::size_t>(tbl) + 8u * static_cast<std::size_t>(i);
        if (o + 4 > d.size()) break;
        const auto p = i32(d, o);
        if (p > 0 && static_cast<std::size_t>(p) < d.size())
            out.push_back(Slot{i, 0, static_cast<std::size_t>(p)});
    }
    return out;
}

std::vector<Subscription> chunkSubscriptions(std::span<const std::byte> b,
                                             ChunkKind kind) {
    std::vector<Subscription> out;
    const std::size_t tabPtr = kind == ChunkKind::Area ? 68u : 36u;
    const std::size_t tabCnt = kind == ChunkKind::Area ? 86u : 54u;
    if (b.size() < tabCnt + 2) return out;
    const auto lo = i32(b, tabPtr);
    const auto n  = i16(b, tabCnt);
    if (n <= 0 || lo <= 0 ||
        static_cast<std::size_t>(lo) + 8u * static_cast<std::size_t>(n) > b.size())
        return out;
    for (int i = 0; i < n; ++i) {
        const auto o = static_cast<std::size_t>(lo) + 8u * static_cast<std::size_t>(i);
        out.push_back({i32(b, o), i16(b, o + 4)});
    }
    return out;
}

std::vector<Subscription> globalSubscriptions(std::span<const std::byte> d) {
    std::vector<Subscription> out;
    if (d.size() < 32) return out;
    const auto lo = i32(d, 8);
    const auto n  = i16(d, 24);
    if (lo <= 0 || n <= 0) return out;
    for (int i = 0; i < n; ++i) {
        const auto o = static_cast<std::size_t>(lo) + 8u * static_cast<std::size_t>(i);
        if (o + 8 > d.size()) break;
        out.push_back({i32(d, o), i16(d, o + 4)});
    }
    return out;
}

}  // namespace omk

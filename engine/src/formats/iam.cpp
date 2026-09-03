// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/iam.h"

namespace omk {
namespace {

std::uint32_t u32(std::span<const std::byte> d, std::size_t o) {
    return static_cast<std::uint32_t>(d[o    ])       |
           static_cast<std::uint32_t>(d[o + 1]) <<  8 |
           static_cast<std::uint32_t>(d[o + 2]) << 16 |
           static_cast<std::uint32_t>(d[o + 3]) << 24;
}

}  // namespace

IamArchive IamArchive::open(std::span<const std::byte> data) {
    IamArchive a;
    a.data_ = data;
    const std::size_t n = data.size();

    // Pass one: find where the payloads start, which is what bounds the
    // directory. An entry is only evidence if it is in range - a hole is
    // (0,0) and garbage past the end must not drag the boundary down.
    std::size_t first = 0;
    bool haveFirst = false;
    for (std::size_t i = 0; i + 8 <= n; ++i) {
        const auto off = u32(data, 8 * i);
        const auto sz  = u32(data, 8 * i + 4);
        if (off != 0 && sz != 0 &&
            static_cast<std::size_t>(off) + sz <= n) {
            if (!haveFirst || off < first) { first = off; haveFirst = true; }
        }
        // stop once the next entry would sit inside the payload region
        if (haveFirst && 8 * (i + 1) > first) break;
        if (8 * (i + 1) + 8 > n) break;
    }
    if (!haveFirst) return a;

    a.entries_.resize(first / 8);
    for (std::size_t i = 0; i < a.entries_.size(); ++i) {
        const auto off = u32(data, 8 * i);
        const auto sz  = u32(data, 8 * i + 4);
        // size >= 4 mirrors the reference reader: a shorter "chunk" is not one
        if (off != 0 && sz >= 4 && static_cast<std::size_t>(off) + sz <= n)
            a.entries_[i] = IamEntry{off, sz};
    }
    return a;
}

std::span<const std::byte> IamArchive::chunk(std::size_t i) const {
    const auto e = entry(i);
    if (!e.present()) return {};
    return data_.subspan(e.offset, e.size);
}

std::size_t IamArchive::populated() const {
    std::size_t n = 0;
    for (const auto& e : entries_) n += e.present();
    return n;
}

}  // namespace omk

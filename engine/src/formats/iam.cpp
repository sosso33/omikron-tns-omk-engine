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
    return parse(data, data.size(), true);
}

IamArchive IamArchive::directory(std::span<const std::byte> head, std::size_t fileSize) {
    return parse(head, fileSize, false);
}

// `bytes` is the whole file (`whole`) or its first bytes, and `n` the file's
// size either way: every bound below is the FILE's, so a directory read from
// the head finds exactly the entries the whole file gives.
IamArchive IamArchive::parse(std::span<const std::byte> data, std::size_t n, bool whole) {
    IamArchive a;
    if (whole) a.data_ = data;
    const std::size_t have = data.size();

    // Pass one: find where the payloads start, which is what bounds the
    // directory. An entry is only evidence if it is in range - a hole is
    // (0,0) and garbage past the end must not drag the boundary down.
    std::size_t first = 0;
    bool haveFirst = false;
    for (std::size_t i = 0; i + 8 <= n; ++i) {
        if (8 * i + 8 > have) { a.need_ = n; return a; }   // the head ran out: read more
        const auto off = u32(data, 8 * i);
        const auto sz  = u32(data, 8 * i + 4);
        // `off <= n && sz <= n - off`, not `off + sz <= n`: with a 32-bit
        // size_t (PowerPC Mac OS X) the sum WRAPS for a garbage entry, which
        // then passed and pointed a chunk outside the file
        if (off != 0 && sz != 0 && off <= n && sz <= n - off) {
            if (!haveFirst || off < first) { first = off; haveFirst = true; }
        }
        // stop once the next entry would sit inside the payload region
        if (haveFirst && 8 * (i + 1) > first) break;
        if (8 * (i + 1) + 8 > n) break;
    }
    if (!haveFirst) return a;
    if (first > have) { a.need_ = first; return a; }

    a.entries_.resize(first / 8);
    for (std::size_t i = 0; i < a.entries_.size(); ++i) {
        const auto off = u32(data, 8 * i);
        const auto sz  = u32(data, 8 * i + 4);
        // size >= 4 mirrors the reference reader: a shorter "chunk" is not one
        if (off != 0 && sz >= 4 && off <= n && sz <= n - off)
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

// SPDX-License-Identifier: GPL-3.0-or-later
// IAM archives - the container everything script-shaped arrives in.
//
// Files directly under gamedata/IAM/ with no extension (DIALOG, AREA, GLOBAL, ...)
// are flat archives, read by Archive_ReadChunk (0x0040FF90):
//
//     offset 0   directory: 8-byte entries
//                  uint32 offset   absolute, from the start of the file
//                  uint32 size     in bytes
//                  (0,0) means "no chunk at this index"
//     offset N   the payloads
//
// The directory ends where the first payload begins, so **its length is
// implied rather than stored** - which is the only subtle thing here, and the
// reason the reader has to find the lowest payload offset before it can know
// how many entries to trust.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace omk {

struct IamEntry {
    std::uint32_t offset = 0;
    std::uint32_t size   = 0;
    bool present() const { return offset != 0 && size != 0; }
};

class IamArchive {
public:
    // Parse `data` as an archive. Never throws: a file that is not one comes
    // back empty, because these are user-supplied files.
    static IamArchive open(std::span<const std::byte> data);
    // The DIRECTORY alone, from the file's first bytes and its size - what
    // `Archive_ReadChunk` reads before a chunk. The same entries `open` finds
    // on the whole file as long as `head` reaches the first payload; `need()`
    // says how many bytes that is (0 when `head` was enough). `chunk()` is
    // empty on it: read the entry's bytes yourself.
    static IamArchive directory(std::span<const std::byte> head, std::size_t fileSize);
    std::size_t need() const { return need_; }

    // How many directory entries the implied directory length gives.
    std::size_t size() const { return entries_.size(); }

    // Entry `i`, or an absent one when out of range.
    IamEntry entry(std::size_t i) const {
        return i < entries_.size() ? entries_[i] : IamEntry{};
    }

    // Chunk `i`'s bytes, or an empty span when absent or out of range.
    std::span<const std::byte> chunk(std::size_t i) const;

    // How many entries carry data - the count worth reporting, since a
    // directory is mostly holes (DIALOG: 512 entries, 420 with data).
    std::size_t populated() const;

private:
    static IamArchive parse(std::span<const std::byte> bytes, std::size_t n, bool whole);
    std::span<const std::byte> data_;
    std::vector<IamEntry>      entries_;
    std::size_t                need_ = 0;
};

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/tex3dt.h"

#include "formats/mesh3do.h"

namespace omk {

std::vector<std::uint8_t> decodeImage(std::span<const std::byte> data,
                                      std::size_t want, bool& exact) {
    std::vector<std::uint8_t> out;
    out.reserve(want);

    const std::size_t n = data.size();
    std::size_t i = 0;
    if (n != 0) {                       // the first byte is always a literal
        out.push_back(static_cast<std::uint8_t>(data[0]));
        i = 1;
    }

    // A back-reference may read a byte this run has just written, so the copy
    // is one at a time rather than a block move - runs that overlap their own
    // source are how the format expresses a repeating pattern.
    const auto copyBack = [&out](std::size_t off, int size) {
        for (int k = 0; k < size; ++k)
            out.push_back(off != 0 && off <= out.size()
                              ? out[out.size() - off]
                              : std::uint8_t{0});
    };

    while (i < n && out.size() < want) {
        const auto group = static_cast<std::uint8_t>(data[i++]);
        for (int bit = 0; bit < 8; ++bit) {
            if (out.size() >= want || i >= n) break;

            if (((group << bit) & 0x80) == 0) {          // a literal
                out.push_back(static_cast<std::uint8_t>(data[i++]));
                continue;
            }

            const auto token = static_cast<std::uint8_t>(data[i++]);
            const int  size  = (token & 0xFC) / 4 + 3;
            switch (token & 3) {
                case 0:                                  // repeat the last
                    if (!out.empty()) {
                        const auto last = out.back();
                        out.insert(out.end(), static_cast<std::size_t>(size - 1), last);
                    }
                    break;
                case 1: {                                // 8-bit offset
                    if (i >= n) { i = n; break; }
                    const std::size_t off = static_cast<std::uint8_t>(data[i++]) + 1u;
                    copyBack(off, size);
                    break;
                }
                case 2: {                                // 16-bit, big-endian
                    if (i + 1 >= n) { i = n; break; }
                    const std::size_t hi = static_cast<std::uint8_t>(data[i++]);
                    const std::size_t lo = static_cast<std::uint8_t>(data[i++]);
                    copyBack(hi * 256u + lo + 1u, size);
                    break;
                }
                default: {                               // offset * 256
                    if (i >= n) { i = n; break; }
                    const std::size_t off = 256u * static_cast<std::uint8_t>(data[i++]);
                    copyBack(off, size);
                    break;
                }
            }
        }
    }

    exact = out.size() == want;
    if (out.size() > want) {
        out.resize(want);
    } else if (out.size() < want) {
        const std::uint8_t pad = out.empty() ? std::uint8_t{0} : out.back();
        out.resize(want, pad);
    }
    return out;
}

std::vector<Texture> textures(std::span<const std::byte> d,
                              std::span<const std::byte> t) {
    std::vector<Texture> out;
    const auto header = readHeader(d);
    if (!header) return out;

    std::size_t off = 0;
    for (int i = 0; i < header->materials; ++i) {
        const auto mat = readMaterial(d, *header, i);
        if (!mat) break;

        Texture tx;
        tx.name   = mat->name;
        tx.width  = mat->width;
        tx.height = mat->height;
        tx.bpp    = mat->bpp;

        const std::size_t ncol = (mat->bpp == 4) ? 16u : 256u;
        const std::size_t palBytes = ncol * 3u;

        // The walk consumes palette then data and must land on t.size(); a
        // short tail means the pair does not belong together, so stop rather
        // than read past it.
        if (off + palBytes > t.size()) break;
        const auto pal = t.subspan(off, palBytes);
        off += palBytes;

        const auto want = static_cast<std::size_t>(mat->width) *
                          static_cast<std::size_t>(mat->height);
        const auto dataSize = static_cast<std::size_t>(mat->dataSize < 0 ? 0 : mat->dataSize);
        if (off + dataSize > t.size()) break;
        const auto raw = t.subspan(off, dataSize);
        off += dataSize;

        std::vector<std::uint8_t> idx;
        if (dataSize == 65536) {                 // stored raw, 256x256
            idx.assign(raw.size() >= want ? want : raw.size(), 0);
            for (std::size_t k = 0; k < idx.size(); ++k)
                idx[k] = static_cast<std::uint8_t>(raw[k]);
            tx.exact = raw.size() >= want;
            idx.resize(want, 0);
        } else {
            idx = decodeImage(raw, want, tx.exact);
        }

        tx.rgb.assign(want * 3u, 0);
        for (std::size_t k = 0; k < want && k < idx.size(); ++k) {
            const std::size_t c = static_cast<std::size_t>(idx[k]) * 3u;
            if (c + 2 < pal.size()) {
                tx.rgb[k * 3    ] = static_cast<std::uint8_t>(pal[c    ]);
                tx.rgb[k * 3 + 1] = static_cast<std::uint8_t>(pal[c + 1]);
                tx.rgb[k * 3 + 2] = static_cast<std::uint8_t>(pal[c + 2]);
            }
        }
        out.push_back(std::move(tx));
    }
    return out;
}

}  // namespace omk

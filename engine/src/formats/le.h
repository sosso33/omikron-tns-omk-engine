// SPDX-License-Identifier: GPL-3.0-or-later
// formats/le.h - reading the game's LITTLE-ENDIAN data on any host.
//
// Every shipped file is little-endian (the game ran on x86). Most readers
// here assemble their values byte by byte and are endian-neutral already; a
// handful copied the bytes straight into an int or a float, which is right on
// x86 and ARM and silently wrong on a big-endian CPU. Run on PowerPC (Mac OS
// X 10.4 under QEMU, 2026-10-02) those reads gave 0 actor records for 1032,
// 16 map lines for 79, and a traffic circuit that would not load - with no
// error anywhere (todo/classic-mac-port-1999.md §3a).
//
// loadLE<T>(p) reads the sizeof(T) bytes at p as a little-endian T, for any
// 1/2/4/8-byte integer or float. It does no bounds check: the caller keeps
// its own, as before. Byte by byte, so the address need not be aligned.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace omk {

template <typename T>
T loadLE(const std::byte* p) {
    static_assert(std::is_arithmetic_v<T> && (sizeof(T) == 1 || sizeof(T) == 2 ||
                                              sizeof(T) == 4 || sizeof(T) == 8),
                  "loadLE reads 1, 2, 4 or 8-byte integers and floats");
    using U = std::conditional_t<sizeof(T) == 1, std::uint8_t,
              std::conditional_t<sizeof(T) == 2, std::uint16_t,
              std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
    U u = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
        u |= static_cast<U>(static_cast<U>(std::to_integer<std::uint8_t>(p[i])) << (8 * i));
    return std::bit_cast<T>(u);
}

// writeLE(out, p, n) writes n elements of p to a stream as little-endian -
// what the tools' result files are, and what verify.py reads them as. On a
// little-endian host it is one write, the same bytes as before; on a big-endian
// one each element is swapped. Templated on the stream so this header needs
// no <ostream>.
template <typename Stream, typename T>
void writeLE(Stream& out, const T* p, std::size_t n) {
    static_assert(std::is_arithmetic_v<T>, "writeLE writes integers and floats");
    if constexpr (std::endian::native == std::endian::little) {
        out.write(reinterpret_cast<const char*>(p), static_cast<long long>(n * sizeof(T)));
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            using U = std::conditional_t<sizeof(T) == 1, std::uint8_t,
                      std::conditional_t<sizeof(T) == 2, std::uint16_t,
                      std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;
            const U u = std::bit_cast<U>(p[i]);
            char b[sizeof(T)];
            for (std::size_t k = 0; k < sizeof(T); ++k) b[k] = static_cast<char>((u >> (8 * k)) & 0xFF);
            out.write(b, sizeof(T));
        }
    }
}

}  // namespace omk

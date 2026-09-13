// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/depthtie.h"

#include <algorithm>
#include <cstring>

namespace omk {

namespace {

using P = std::array<std::uint32_t, 3>;

inline std::uint32_t bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

inline P posOf(const Geometry& g, std::size_t c) {
    const auto& p = g.corners[c];
    return P{bits(p.x), bits(p.y), bits(p.z)};
}

inline std::uint64_t mixP(const P& p) {
    std::uint64_t h = static_cast<std::uint64_t>(p[0]) * 0x9E3779B97F4A7C15ull ^
                      static_cast<std::uint64_t>(p[1]) * 0xC2B2AE3D27D4EB4Full ^
                      static_cast<std::uint64_t>(p[2]) * 0x165667B19E3779F9ull;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 32;
    return h;
}

// An ORDER-FREE hash of a key's positions: a sum and an xor of each position's
// mix, both commutative, so the same multiset hashes the same in any order.
inline std::uint64_t keyHash(const P* ps, int n) {
    std::uint64_t sum = 0, x = 0;
    for (int k = 0; k < n; ++k) {
        const std::uint64_t h = mixP(ps[k]);
        sum += h;
        x ^= h * 0x94D049BB133111EBull;
    }
    std::uint64_t h = sum ^ (x >> 17) ^ (x << 23);
    h ^= h >> 31;
    h *= 0xD6E8FEB86659FD93ull;
    return h ^ (h >> 32);
}

// Whether two keys are the same MULTISET of positions - what the sorted array
// compared equal meant.
inline bool sameKey(const std::uint32_t* stored, const P* ps, int n) {
    P a[4], b[4];
    for (int k = 0; k < n; ++k) {
        a[k] = P{stored[3 * k], stored[3 * k + 1], stored[3 * k + 2]};
        b[k] = ps[k];
    }
    std::sort(a, a + n);
    std::sort(b, b + n);
    return std::equal(a, a + n, b);
}

}  // namespace

void DepthTie::Claimed::reset(std::size_t capacityFor) {
    std::size_t want = 16;
    while (want < capacityFor + capacityFor / 4 + 1) want <<= 1;
    keys.clear();                         // keeps its capacity: nothing freed
    count = 0;
    // A cell is (generation 16 | fingerprint 16 | key number + 1 32): the
    // generation is 16 bits, so it wraps after 65535 revisions into a clear.
    if (cell.size() != want) {
        cell.assign(want, 0);
        gen = 1;
    } else if (++gen > 0xFFFF) {
        std::fill(cell.begin(), cell.end(), 0);
        gen = 1;
    }
    mask = want - 1;
}

void DepthTie::resolve(const Geometry& g, std::size_t start, std::size_t count, bool writes,
                       std::vector<std::size_t>& losers) {
    const std::size_t ntri = g.corners.size() / 3;
    if (revision_ != g.revision || done_.size() != ntri) {
        revision_ = g.revision;
        done_.assign(ntri, 0);
        quads_.reset(ntri);
        tris_.reset(ntri);
        dropped = 0;
        touched = false;
    }
    const std::size_t t0 = start / 3, t1 = std::min(ntri, (start + count) / 3);
    if (t0 >= t1) return;
    // Nothing claimed yet and this draw claims nothing: every face in it is a
    // lookup that finds nothing and none is inserted, so the walk's only effect
    // is to mark the range handled - which is all this does.
    if (!writes && quads_.empty() && tris_.empty()) {
        std::fill(done_.begin() + static_cast<long>(t0), done_.begin() + static_cast<long>(t1),
                  std::uint8_t{1});
        return;
    }
    const auto samePos = [&](std::size_t a, std::size_t b) {
        const auto& p = g.corners[a]; const auto& q = g.corners[b];
        return bits(p.x) == bits(q.x) && bits(p.y) == bits(q.y) && bits(p.z) == bits(q.z);
    };
    // Found -> true. Not found -> false, and a writing draw claims it.
    const auto claimedOrClaim = [&](Claimed& s, const P* ps) {
        const int n = s.corners;
        const std::uint64_t h = keyHash(ps, n);
        // THE FINGERPRINT: 16 bits of the hash kept in the cell, so a probe that
        // walks past OTHER keys (linear probing clusters) rejects them without
        // sorting and comparing their positions; only a fingerprint match goes
        // on to the exact multiset compare, which alone decides.
        const std::uint64_t fp = (h >> 48) & 0xFFFFull;
        for (std::uint64_t i = h & s.mask;; i = (i + 1) & s.mask) {
            const std::uint64_t cell = s.cell[static_cast<std::size_t>(i)];
            if ((cell >> 48) != s.gen) {                     // an empty slot: absent
                if (writes) {
                    const std::size_t k = s.keys.size() / (3 * static_cast<std::size_t>(n));
                    for (int v = 0; v < n; ++v)
                        s.keys.insert(s.keys.end(), ps[v].begin(), ps[v].end());
                    s.cell[static_cast<std::size_t>(i)] =
                        (static_cast<std::uint64_t>(s.gen) << 48) | (fp << 32) |
                        (static_cast<std::uint64_t>(k) + 1);
                    ++s.count;
                }
                return false;
            }
            if (((cell >> 32) & 0xFFFFull) != fp) continue;  // a different key
            const std::size_t k = static_cast<std::size_t>(cell & 0xFFFFFFFFull) - 1;
            if (sameKey(s.keys.data() + k * 3 * static_cast<std::size_t>(n), ps, n)) return true;
        }
    };
    for (std::size_t tri = t0; tri < t1; ++tri) {
        if (done_[tri]) continue;
        const std::size_t c = 3 * tri;
        const bool quad = tri + 1 < t1 && !done_[tri + 1] &&
                          samePos(c, c + 3) && samePos(c + 2, c + 4);
        if (quad) {
            const P ps[4] = {posOf(g, c), posOf(g, c + 1), posOf(g, c + 2), posOf(g, c + 5)};
            done_[tri] = done_[tri + 1] = 1;
            if (claimedOrClaim(quads_, ps)) { losers.push_back(tri); losers.push_back(tri + 1); }
            ++tri;
        } else {
            const P ps[3] = {posOf(g, c), posOf(g, c + 1), posOf(g, c + 2)};
            done_[tri] = 1;
            if (claimedOrClaim(tris_, ps)) losers.push_back(tri);
        }
    }
}

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/depthtie.h"

#include <algorithm>
#include <cstring>

namespace omk {

void DepthTie::resolve(const Geometry& g, std::size_t start, std::size_t count, bool writes,
                       std::vector<std::size_t>& losers) {
    const std::size_t ntri = g.corners.size() / 3;
    if (revision_ != g.revision || done_.size() != ntri) {
        revision_ = g.revision;
        done_.assign(ntri, 0);
        quads_.clear();   // keeps the buckets: the next revision is the same size
        tris_.clear();
        dropped = 0;
        touched = false;
    }
    const std::size_t t0 = start / 3, t1 = std::min(ntri, (start + count) / 3);
    if (t0 >= t1) return;
    // Nothing claimed yet and this draw claims nothing: every face in it is a
    // lookup in an empty set and none is inserted, so the walk's only effect is
    // to mark the range handled - which is all this does.
    if (!writes && quads_.empty() && tris_.empty()) {
        std::fill(done_.begin() + static_cast<long>(t0), done_.begin() + static_cast<long>(t1),
                  std::uint8_t{1});
        return;
    }
    const auto bits = [](float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; };
    const auto samePos = [&](std::size_t a, std::size_t b) {
        const auto& p = g.corners[a]; const auto& q = g.corners[b];
        return bits(p.x) == bits(q.x) && bits(p.y) == bits(q.y) && bits(p.z) == bits(q.z);
    };
    using P = std::array<std::uint32_t, 3>;
    const auto pos = [&](std::size_t c) {
        const auto& p = g.corners[c]; return P{bits(p.x), bits(p.y), bits(p.z)};
    };
    if (writes) {
        if (tris_.bucket_count() < ntri) tris_.reserve(ntri);
    }
    for (std::size_t tri = t0; tri < t1; ++tri) {
        if (done_[tri]) continue;
        const std::size_t c = 3 * tri;
        const bool quad = tri + 1 < t1 && !done_[tri + 1] &&
                          samePos(c, c + 3) && samePos(c + 2, c + 4);
        if (quad) {
            std::array<P, 4> ps{pos(c), pos(c + 1), pos(c + 2), pos(c + 5)};
            std::sort(ps.begin(), ps.end());
            std::array<std::uint32_t, 12> key;
            for (int k = 0; k < 4; ++k) for (int j = 0; j < 3; ++j) key[3 * k + j] = ps[k][j];
            done_[tri] = done_[tri + 1] = 1;
            if (quads_.count(key)) { losers.push_back(tri); losers.push_back(tri + 1); }
            else if (writes) quads_.insert(key);
            ++tri;
        } else {
            std::array<P, 3> ps{pos(c), pos(c + 1), pos(c + 2)};
            std::sort(ps.begin(), ps.end());
            std::array<std::uint32_t, 9> key;
            for (int k = 0; k < 3; ++k) for (int j = 0; j < 3; ++j) key[3 * k + j] = ps[k][j];
            done_[tri] = 1;
            if (tris_.count(key)) losers.push_back(tri);
            else if (writes) tris_.insert(key);
        }
    }
}

}  // namespace omk

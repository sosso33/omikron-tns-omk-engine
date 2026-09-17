// SPDX-License-Identifier: GPL-3.0-or-later
#include "o3de/depthtie.h"

#include <algorithm>
#include <cstring>
#include <type_traits>

namespace omk {

namespace {

using P = std::array<std::uint32_t, 3>;
constexpr std::uint32_t kNone = 0xFFFFFFFFu;

inline std::uint32_t bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

inline P posOf(const Geometry& g, std::size_t c) {
    const auto& p = g.corners[c];
    return P{bits(p.x), bits(p.y), bits(p.z)};
}

inline bool samePos(const Geometry& g, std::size_t a, std::size_t b) {
    const auto& p = g.corners[a]; const auto& q = g.corners[b];
    return bits(p.x) == bits(q.x) && bits(p.y) == bits(q.y) && bits(p.z) == bits(q.z);
}

// Whether triangles `tri` and `tri + 1` are keyed as one quad - the positional
// half of the walk's pairing test.
inline bool pairsAsQuad(const Geometry& g, std::size_t tri) {
    const std::size_t c = 3 * tri;
    return samePos(g, c, c + 3) && samePos(g, c + 2, c + 4);
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

inline bool sameKeyP(const P* x, const P* ps, int n) {
    P a[4], b[4];
    for (int k = 0; k < n; ++k) { a[k] = x[k]; b[k] = ps[k]; }
    std::sort(a, a + n);
    std::sort(b, b + n);
    return std::equal(a, a + n, b);
}

// A unit's key positions as the walk takes them: (c, c+1, c+2) for a
// triangle, and c+5 as the fourth corner of a quad.
inline int unitKeyOf(const Geometry& g, std::uint32_t tri, bool quad, P* ps) {
    const std::size_t c = 3 * static_cast<std::size_t>(tri);
    ps[0] = posOf(g, c); ps[1] = posOf(g, c + 1); ps[2] = posOf(g, c + 2);
    if (!quad) return 3;
    ps[3] = posOf(g, c + 5);
    return 4;
}

// One table holds both kinds, so a quad's hash is set apart from a triangle's.
inline std::uint64_t unitHashOf(const P* ps, int n) {
    const std::uint64_t h = keyHash(ps, n);
    return n == 4 ? h ^ 0xA0761D6478BD642Full : h;
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

// ---- the vertex buffer's degenerated triangles -------------------------------

// Measured, not derived: `capacity` a vector at a time. See the declaration.
DepthTie::Bytes DepthTie::bytes() const {
    const auto vec = [](const auto& v) {
        return v.capacity() * sizeof(typename std::decay_t<decltype(v)>::value_type);
    };
    Bytes b;
    for (const Claimed* c : {&quads_, &tris_}) b.claimed += vec(c->keys) + vec(c->cell);
    b.perTri = vec(done_) + vec(applied_) + vec(appliedList_) + vec(unitOfTri_);
    b.log = vec(units_) + vec(unitHash_);
    for (const auto& c : calls_) b.log += sizeof(c) + vec(c.losers);
    b.log += (calls_.capacity() - calls_.size()) * sizeof(Call);
    b.table = vec(table_);
    b.scratch = vec(stampTri_) + vec(stampUnit_) + vec(stampChg_) + vec(orig_) +
                vec(affected_) + vec(changed_) + vec(marked_) + vec(prefix_) + vec(scratch_);
    return b;
}

void DepthTie::vboReplaced() {
    for (const std::uint32_t t : appliedList_)
        if (t < applied_.size()) applied_[t] = 0;
    appliedList_.clear();
    appliedCount_ = 0;
}

void DepthTie::markApplied(const std::vector<std::size_t>& losers, std::size_t from) {
    for (std::size_t k = from; k < losers.size(); ++k) {
        const std::size_t t = losers[k];
        if (t < applied_.size() && !applied_[t]) {
            applied_[t] = 1;
            appliedList_.push_back(static_cast<std::uint32_t>(t));
            ++appliedCount_;
        }
    }
}

// ---- the entry point ----------------------------------------------------------

void DepthTie::resolve(const Geometry& g, std::size_t start, std::size_t count, bool writes,
                       std::vector<std::size_t>& losers, std::vector<std::size_t>& restore) {
    const std::size_t ntri = g.corners.size() / 3;
    if (applied_.size() != ntri) {        // a new size is a new buffer: nothing degenerated
        applied_.assign(ntri, 0);
        appliedList_.clear();
        appliedCount_ = 0;
    }
    const std::size_t from = losers.size();
    if (revision_ != g.revision || done_.size() != ntri) {
        if (!prepareReplay(g, ntri, restore)) resetFor(g, ntri, restore);
    }
    if (replaying_) {
        if (cursor_ < ncalls_) {
            const Call& cl = calls_[cursor_];
            if (cl.start == start && cl.count == count && cl.writes == writes) {
                ++cursor_;
                for (const std::uint32_t u : cl.losers) {
                    losers.push_back(units_[u].tri);
                    if (units_[u].quad) losers.push_back(units_[u].tri + 1);
                }
                markApplied(losers, from);
                return;
            }
            fallback(g, restore);
        } else {
            // Every logged draw arrived: the state is exactly a full walk's, so
            // a further draw of this revision just continues it.
            replaying_ = false;
        }
    }
    if (tracked_) walkTracked(g, start, count, writes, losers);
    else walkFlat(g, start, count, writes, losers);
    markApplied(losers, from);
}

void DepthTie::resetFor(const Geometry& g, std::size_t ntri, std::vector<std::size_t>& restore) {
    revision_ = g.revision;
    done_.assign(ntri, 0);
    dropped = 0;
    touched = false;
    // everything degenerated goes back; this revision's losers are decided afresh
    for (const std::uint32_t t : appliedList_)
        if (t < applied_.size() && applied_[t]) { applied_[t] = 0; restore.push_back(t); }
    appliedList_.clear();
    appliedCount_ = 0;
    replaying_ = false;
    cursor_ = ncalls_ = 0;
    anyWriter_ = false;
    tracked_ = g.dirtyTo != 0 && g.dirtyTo == g.revision;
    if (tracked_) {
        resetTracked(ntri);
        ++walks;
    } else {
        quads_.reset(ntri);
        tris_.reset(ntri);
        if (units_.capacity()) {          // no longer tracked: let the log go
            std::vector<Unit>().swap(units_);
            std::vector<std::uint64_t>().swap(unitHash_);
            std::vector<std::uint32_t>().swap(unitOfTri_);
            std::vector<Call>().swap(calls_);
            std::vector<std::uint64_t>().swap(table_);
        }
    }
}

void DepthTie::resetTracked(std::size_t ntri) {
    units_.clear();
    unitHash_.clear();
    unitOfTri_.assign(ntri, kNone);
    ncalls_ = 0;
    tableReset(ntri);
}

// ---- the untracked walk: step 7b's, unchanged ----------------------------------

void DepthTie::walkFlat(const Geometry& g, std::size_t start, std::size_t count, bool writes,
                        std::vector<std::size_t>& losers) {
    const std::size_t ntri = done_.size();
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
    facesWalked += static_cast<long>(t1 - t0);
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
        const bool quad = tri + 1 < t1 && !done_[tri + 1] && pairsAsQuad(g, tri);
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

// ---- the key table of the tracked walk -------------------------------------------

void DepthTie::tableReset(std::size_t units) {
    std::size_t want = 16;
    while (want < 2 * units + 16) want <<= 1;
    if (table_.size() != want) {
        table_.assign(want, 0);
        tgen_ = 1;
    } else if (++tgen_ > 0xFFFF) {
        std::fill(table_.begin(), table_.end(), 0);
        tgen_ = 1;
    }
    tmask_ = want - 1;
    tombs_ = 0;
}

// Re-insert every chain head under a fresh generation: clears the deleted cells.
void DepthTie::tableRebuild() {
    if (++tgen_ > 0xFFFF) {
        std::fill(table_.begin(), table_.end(), 0);
        tgen_ = 1;
    }
    tombs_ = 0;
    for (std::uint32_t u = 0; u < units_.size(); ++u) {
        if (!units_[u].head) continue;
        const std::uint64_t h = unitHash_[u];
        std::uint64_t i = h & tmask_;
        while ((table_[static_cast<std::size_t>(i)] >> 48) == tgen_) i = (i + 1) & tmask_;
        table_[static_cast<std::size_t>(i)] = (static_cast<std::uint64_t>(tgen_) << 48) |
                                              (((h >> 48) & 0xFFFFull) << 32) | (static_cast<std::uint64_t>(u) + 1);
    }
}

void DepthTie::setCell(std::size_t slot, std::uint64_t h, std::uint32_t head) {
    const std::uint64_t old = table_[slot];
    if ((old >> 48) == tgen_ && (old & 0xFFFFFFFFull) == 0) --tombs_;
    table_[slot] = (static_cast<std::uint64_t>(tgen_) << 48) | (((h >> 48) & 0xFFFFull) << 32) |
                   (static_cast<std::uint64_t>(head) + 1);
}

// The head of the chain whose key is `ps`, or kNone; `slot` is its cell, or the
// cell a new head would take.
std::uint32_t DepthTie::findHead(const Geometry& g, std::uint64_t h, const P* ps, int n,
                                 std::size_t& slot) const {
    const std::uint64_t fp = (h >> 48) & 0xFFFFull;
    std::size_t firstFree = static_cast<std::size_t>(-1);
    for (std::uint64_t i = h & tmask_;; i = (i + 1) & tmask_) {
        const std::uint64_t cell = table_[static_cast<std::size_t>(i)];
        if ((cell >> 48) != tgen_) {
            slot = firstFree != static_cast<std::size_t>(-1) ? firstFree : static_cast<std::size_t>(i);
            return kNone;
        }
        const std::uint64_t hp1 = cell & 0xFFFFFFFFull;
        if (hp1 == 0) {
            if (firstFree == static_cast<std::size_t>(-1)) firstFree = static_cast<std::size_t>(i);
            continue;
        }
        if (((cell >> 32) & 0xFFFFull) != fp) continue;
        const std::uint32_t hd = static_cast<std::uint32_t>(hp1 - 1);
        const Unit& H = units_[hd];
        if ((H.quad != 0) != (n == 4)) continue;
        P hs[4];
        unitKeyOf(g, H.tri, H.quad != 0, hs);
        if (sameKeyP(hs, ps, n)) {
            slot = static_cast<std::size_t>(i);
            return hd;
        }
    }
}

// ---- the tracked walk: the same decisions, logged ----------------------------------

void DepthTie::walkTracked(const Geometry& g, std::size_t start, std::size_t count, bool writes,
                           std::vector<std::size_t>& losers) {
    const std::size_t ntri = done_.size();
    if (ncalls_ == calls_.size()) calls_.emplace_back();
    const std::uint32_t ci = static_cast<std::uint32_t>(ncalls_++);
    Call& cl = calls_[ci];
    cl.start = start; cl.count = count; cl.writes = writes;
    cl.losers.clear();
    const std::size_t t0 = start / 3, t1 = std::min(ntri, (start + count) / 3);
    if (t0 >= t1) return;
    if (!writes && !anyWriter_) {        // walkFlat's shortcut: no face here can lose
        std::fill(done_.begin() + static_cast<long>(t0), done_.begin() + static_cast<long>(t1),
                  std::uint8_t{1});
        return;
    }
    facesWalked += static_cast<long>(t1 - t0);
    for (std::size_t tri = t0; tri < t1; ++tri) {
        if (done_[tri]) continue;
        const bool open = tri + 1 < t1 && !done_[tri + 1];
        const bool quad = open && pairsAsQuad(g, tri);
        const std::uint32_t u = static_cast<std::uint32_t>(units_.size());
        P ps[4];
        const int n = unitKeyOf(g, static_cast<std::uint32_t>(tri), quad, ps);
        const std::uint64_t h = unitHashOf(ps, n);
        std::size_t slot = 0;
        const std::uint32_t hd = findHead(g, h, ps, n, slot);
        Unit U;
        U.tri = static_cast<std::uint32_t>(tri);
        U.call = ci;
        U.next = kNone;
        U.tail = u;
        U.quad = quad;
        U.cand = open && !quad;
        U.writes = writes;
        if (hd == kNone) {
            U.head = 1;
            U.hasWriter = writes;
            setCell(slot, h, u);
        } else {
            Unit& H = units_[hd];
            U.loser = H.hasWriter;
            units_[H.tail].next = u;
            H.tail = u;
            if (writes) H.hasWriter = 1;
        }
        units_.push_back(U);
        unitHash_.push_back(h);
        done_[tri] = 1;
        unitOfTri_[tri] = u;
        if (quad) { done_[tri + 1] = 1; unitOfTri_[tri + 1] = u; }
        if (writes) anyWriter_ = true;
        if (U.loser) {
            cl.losers.push_back(u);
            losers.push_back(tri);
            if (quad) losers.push_back(tri + 1);
        }
        if (quad) ++tri;
    }
}

// ---- the replay ------------------------------------------------------------------

void DepthTie::noteChange(std::uint32_t u) {
    if (stampChg_[u] == stamp_) return;
    stampChg_[u] = stamp_;
    orig_[u] = units_[u].loser;
    changed_.push_back(u);
}

void DepthTie::recomputeChain(std::uint32_t head) {
    bool seen = false;
    std::uint32_t last = head;
    for (std::uint32_t x = head; x != kNone; x = units_[x].next) {
        Unit& X = units_[x];
        const std::uint8_t l = seen ? 1 : 0;
        if (X.loser != l) { noteChange(x); X.loser = l; }
        if (X.writes) seen = true;
        last = x;
    }
    units_[head].hasWriter = seen;
    units_[head].tail = last;
}

// Re-decide every chain whose key hashes to a marked value.
void DepthTie::recomputeMarked() {
    std::sort(marked_.begin(), marked_.end());
    marked_.erase(std::unique(marked_.begin(), marked_.end()), marked_.end());
    for (const std::uint64_t h : marked_) {
        const std::uint64_t fp = (h >> 48) & 0xFFFFull;
        for (std::uint64_t i = h & tmask_;; i = (i + 1) & tmask_) {
            const std::uint64_t cell = table_[static_cast<std::size_t>(i)];
            if ((cell >> 48) != tgen_) break;
            const std::uint64_t hp1 = cell & 0xFFFFFFFFull;
            if (hp1 == 0 || ((cell >> 32) & 0xFFFFull) != fp) continue;
            const std::uint32_t hd = static_cast<std::uint32_t>(hp1 - 1);
            if (unitHash_[hd] == h) recomputeChain(hd);
        }
    }
    marked_.clear();
}

// Take unit `u` out of its chain, found by identity under its logged hash - no
// position is compared, so it is right while other moved units still sit
// under their old keys.
void DepthTie::unlinkUnit(std::uint32_t u) {
    const std::uint64_t h = unitHash_[u];
    const std::uint64_t fp = (h >> 48) & 0xFFFFull;
    for (std::uint64_t i = h & tmask_;; i = (i + 1) & tmask_) {
        const std::uint64_t cell = table_[static_cast<std::size_t>(i)];
        if ((cell >> 48) != tgen_) return;             // not in the table: cannot happen
        const std::uint64_t hp1 = cell & 0xFFFFFFFFull;
        if (hp1 == 0 || ((cell >> 32) & 0xFFFFull) != fp) continue;
        const std::uint32_t hd = static_cast<std::uint32_t>(hp1 - 1);
        if (unitHash_[hd] != h) continue;
        std::uint32_t prev = kNone;
        for (std::uint32_t x = hd; x != kNone; prev = x, x = units_[x].next) {
            if (x != u) continue;
            Unit& U = units_[u];
            if (prev == kNone) {
                if (U.next == kNone) {
                    table_[static_cast<std::size_t>(i)] = static_cast<std::uint64_t>(tgen_) << 48;
                    ++tombs_;
                } else {
                    table_[static_cast<std::size_t>(i)] = (static_cast<std::uint64_t>(tgen_) << 48) | (fp << 32) |
                                                          (static_cast<std::uint64_t>(U.next) + 1);
                    units_[U.next].head = 1;
                    units_[U.next].tail = U.tail;
                    marked_.push_back(h);
                }
            } else {
                units_[prev].next = U.next;
                if (units_[hd].tail == u) units_[hd].tail = prev;
                marked_.push_back(h);
            }
            U.next = kNone;
            U.head = 0;
            U.tail = u;
            return;
        }
    }
}

// Put unit `u` back under the key its corners have now, in walk order.
void DepthTie::linkUnit(const Geometry& g, std::uint32_t u) {
    P ps[4];
    const int n = unitKeyOf(g, units_[u].tri, units_[u].quad != 0, ps);
    const std::uint64_t h = unitHashOf(ps, n);
    unitHash_[u] = h;
    std::size_t slot = 0;
    const std::uint32_t hd = findHead(g, h, ps, n, slot);
    Unit& U = units_[u];
    if (hd == kNone) {
        U.head = 1; U.next = kNone; U.tail = u;
        setCell(slot, h, u);
    } else if (u < hd) {
        U.head = 1; U.next = hd; U.tail = units_[hd].tail;
        units_[hd].head = 0;
        table_[slot] = (static_cast<std::uint64_t>(tgen_) << 48) | (((h >> 48) & 0xFFFFull) << 32) |
                       (static_cast<std::uint64_t>(u) + 1);
    } else {
        std::uint32_t x = hd;
        while (units_[x].next != kNone && units_[x].next < u) x = units_[x].next;
        U.next = units_[x].next;
        units_[x].next = u;
        if (U.next == kNone) units_[hd].tail = u;
    }
    marked_.push_back(h);
}

bool DepthTie::prepareReplay(const Geometry& g, std::size_t ntri, std::vector<std::size_t>& restore) {
    if (!tracked_ || (replaying_ && cursor_ != ncalls_) || g.dirtyTo == 0 || g.dirtyTo != g.revision ||
        g.dirtyFrom != revision_ || done_.size() != ntri || unitOfTri_.size() != ntri)
        return false;
    if (++stamp_ == 0) {
        std::fill(stampTri_.begin(), stampTri_.end(), 0);
        std::fill(stampUnit_.begin(), stampUnit_.end(), 0);
        std::fill(stampChg_.begin(), stampChg_.end(), 0);
        stamp_ = 1;
    }
    if (stampTri_.size() != ntri) stampTri_.assign(ntri, 0);
    if (stampUnit_.size() != units_.size()) stampUnit_.assign(units_.size(), 0);
    if (stampChg_.size() != units_.size()) stampChg_.assign(units_.size(), 0);
    if (orig_.size() != units_.size()) orig_.assign(units_.size(), 0);
    // The moved units, once each - and nothing changes until every one of them
    // is known to pair exactly as it was walked.
    affected_.clear();
    for (const std::uint32_t c : g.dirtyCorners) {
        const std::size_t t = c / 3;
        if (t >= ntri) return false;
        if (stampTri_[t] == stamp_) continue;
        stampTri_[t] = stamp_;
        const std::uint32_t u = unitOfTri_[t];
        if (u != kNone) {
            const Unit& U = units_[u];
            if (U.quad ? !pairsAsQuad(g, U.tri) : (U.cand && pairsAsQuad(g, U.tri))) return false;
            if (stampUnit_[u] != stamp_) { stampUnit_[u] = stamp_; affected_.push_back(u); }
        }
        if (t > 0) {                     // the single before it, open to pair with it
            const std::uint32_t v = unitOfTri_[t - 1];
            if (v != kNone && v != u && !units_[v].quad && units_[v].cand && pairsAsQuad(g, units_[v].tri))
                return false;
        }
    }
    changed_.clear();
    marked_.clear();
    for (const std::uint32_t u : affected_) unlinkUnit(u);
    recomputeMarked();
    if (tombs_ > (tmask_ + 1) / 8) tableRebuild();
    for (const std::uint32_t u : affected_) linkUnit(g, u);
    recomputeMarked();
    for (const std::uint32_t u : changed_) {
        const Unit& U = units_[u];
        if (U.loser == orig_[u]) continue;
        auto& L = calls_[U.call].losers;
        const auto it = std::lower_bound(L.begin(), L.end(), u);
        if (U.loser) {
            L.insert(it, u);
        } else {
            if (it != L.end() && *it == u) L.erase(it);
            for (std::uint32_t t = U.tri; t <= U.tri + (U.quad ? 1u : 0u); ++t)
                if (applied_[t]) { applied_[t] = 0; --appliedCount_; restore.push_back(t); }
        }
    }
    if (appliedList_.size() > 2 * appliedCount_ + 64) {
        // Drop the entries restored since. A triangle restored and degenerated
        // again has two entries, so the flag is raised to 2 on the first kept
        // and the second is dropped, then lowered back.
        std::size_t k = 0;
        for (const std::uint32_t t : appliedList_)
            if (applied_[t] == 1) { applied_[t] = 2; appliedList_[k++] = t; }
        appliedList_.resize(k);
        for (const std::uint32_t t : appliedList_) applied_[t] = 1;
        appliedCount_ = k;
    }
    revision_ = g.revision;
    replaying_ = true;
    cursor_ = 0;
    dropped = 0;
    touched = false;
    ++replays;
    return true;
}

// A draw that is not the logged one: walk the draws already answered afresh -
// their losers are what the replay gave, since it gave a full walk's - and put
// back what is degenerated but is no longer a loser of them.
void DepthTie::fallback(const Geometry& g, std::vector<std::size_t>& restore) {
    ++fallbacks;
    prefix_.clear();
    for (std::size_t k = 0; k < cursor_; ++k)
        prefix_.push_back({calls_[k].start, calls_[k].count, calls_[k].writes ? 1u : 0u});
    const std::size_t ntri = done_.size();
    done_.assign(ntri, 0);
    replaying_ = false;
    cursor_ = 0;
    anyWriter_ = false;
    resetTracked(ntri);
    scratch_.clear();
    for (const auto& c : prefix_) walkTracked(g, c[0], c[1], c[2] != 0, scratch_);
    std::size_t k = 0;
    for (const std::uint32_t t : appliedList_) {
        if (applied_[t] != 1) continue;             // restored since, or a duplicate already kept
        const std::uint32_t u = unitOfTri_[t];
        if (u == kNone || !units_[u].loser) {
            applied_[t] = 0;
            restore.push_back(t);
        } else {
            applied_[t] = 2;
            appliedList_[k++] = t;
        }
    }
    appliedList_.resize(k);
    for (const std::uint32_t t : appliedList_) applied_[t] = 1;
    appliedCount_ = k;
}

}  // namespace omk

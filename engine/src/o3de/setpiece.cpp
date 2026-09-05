// SPDX-License-Identifier: GPL-3.0-or-later
// The set pieces. See `setpiece.h` for which function each rule is.
#include <cstdlib>
#include "o3de/setpiece.h"

#include <cmath>

namespace omk {

void headingMatrix(const float dir[3], float rollDeg, float m[9]) {
    // `sub_442400`: yaw from the heading's x/z, pitch from what is left,
    // then `sub_441FF0(pitch, yaw, roll)`.
    const double dx = dir[0], dy = dir[1], dz = dir[2];
    const double h = std::sqrt(dx * dx + dz * dz);
    double yaw = 0.0;
    if (h >= 0.00001) {
        yaw = std::acos(dz / h);
        if (dx < 0.0) yaw = -yaw;            // the `fcom` c0 arm: a1 < 0
    }
    const double s = std::sin(yaw), c = std::cos(yaw);
    const double v23 = c * dx - s * dz;
    const double v24 = s * dx + c * dz;
    const double len = std::sqrt(v23 * v23 + dy * dy + v24 * v24);
    double pitch = 0.0;
    if (len > 1e-9) {
        pitch = std::acos(v24 / len);
        if (dy > 0.0) pitch = -pitch;        // `!(c0 | c3)`: a2 > 0
    } else {
        // a zero heading: the engine divides by zero here. Identity.
        for (int i = 0; i < 9; ++i) m[i] = (i % 4 == 0) ? 1.0f : 0.0f;
        return;
    }
    const double roll = rollDeg * 0.0174532925199433;
    // `sub_441FF0(a1 = pitch, a2 = yaw, a3 = roll)`, verbatim: f32(a4, 4k)
    // is m[k].
    const double sr = std::sin(roll), sp = std::sin(pitch), cp = std::cos(pitch);
    const double sy = std::sin(yaw), cr = std::cos(roll), cy = std::cos(yaw);
    const double v11 = sr * sy;
    m[0] = static_cast<float>(sp * v11 + cr * cy);
    m[3] = static_cast<float>(sr * cp);
    m[6] = static_cast<float>(sr * sp * cy - sy * cr);
    m[1] = static_cast<float>(sy * sp * cr - sr * cy);
    m[4] = static_cast<float>(cr * cp);
    m[7] = static_cast<float>(sp * cr * cy + v11);
    m[2] = static_cast<float>(sy * cp);
    m[5] = static_cast<float>(-sp);
    m[8] = static_cast<float>(cy * cp);
}

void rotateT(const float v[3], const float m[9], float out[3]) {
    out[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
    out[1] = m[3] * v[0] + m[4] * v[1] + m[5] * v[2];
    out[2] = m[6] * v[0] + m[7] * v[1] + m[8] * v[2];
}

void SetPieceRunner::attach(const SfxFile* sfx) {
    sfx_ = sfx;
    st_.clear();
    registered_ = 0;
    shows_ = 0;
    if (!sfx_) return;
    st_.resize(sfx_->pieces.size());
    for (std::size_t i = 0; i < st_.size(); ++i) {
        const auto& p = sfx_->pieces[i];
        // The shipped +72 is what `sub_451220` reads at the first Show.
        st_[i].reversed = (p.flags & 0x8u) != 0;
        for (int k = 0; k < 3; ++k) st_[i].pos[k] = p.pos[k];
    }
}

int SetPieceRunner::rowById(std::int32_t id) const {
    // `sub_450FC0` case 1: the row whose +0 is the id, or nothing for -1.
    if (!sfx_ || id == -1) return -1;
    for (std::size_t i = 0; i < sfx_->pieces.size(); ++i)
        if (sfx_->pieces[i].id == id) return static_cast<int>(i);
    return -1;
}

float SetPieceRunner::blockDuration(int row) const {
    const auto& parts = sfx_->pieces[static_cast<std::size_t>(row)].parts;
    const std::size_t n = parts.size();
    if (n == 0) return 0.0f;
    if (n == 1) return parts[0].dur;
    float d = 0.0f;
    for (std::size_t j = 0; j + 1 < n; ++j) d += parts[j].dur;
    return d;
}

void SetPieceRunner::recordPos(int row, int rec, float out[3], int depth) const {
    // `sub_450330(row, record, out)`.
    const auto& p = sfx_->pieces[static_cast<std::size_t>(row)];
    if (rec < 0 || rec >= static_cast<int>(p.parts.size())) {
        for (int k = 0; k < 3; ++k) out[k] = st_[static_cast<std::size_t>(row)].pos[k];
        return;
    }
    const auto& r = p.parts[static_cast<std::size_t>(rec)];
    int type; std::uint32_t id;
    if (p.linkType != 0 && r.linkType != 0) { type = p.linkType; id = p.linkId; }
    else                                     { type = r.linkType; id = r.linkId; }
    PieceLink L;
    bool ok = false;
    if (type == 0) {
        ok = false;
    } else if (type == 1) {
        const int o = rowById(static_cast<std::int32_t>(id));
        if (o >= 0 && o != row && depth < 8) {
            for (int k = 0; k < 3; ++k) L.pos[k] = st_[static_cast<std::size_t>(o)].pos[k];
            float dir[3];
            rowHeading(o, dir, depth + 1);
            headingMatrix(dir, 0.0f, L.mat);   // roll under 0x80: unported
            L.hasMatrix = true;
            ok = true;
        }
    } else if (links_ && links_(type, id, L)) {
        if (!L.hasMatrix) headingMatrix(L.dir, L.roll, L.mat);
        ok = true;
    }
    if (!ok) {
        for (int k = 0; k < 3; ++k) out[k] = r.pos[k];
        return;
    }
    float v[3];
    rotateT(r.pos, L.mat, v);
    for (int k = 0; k < 3; ++k) out[k] = v[k] + L.pos[k];
}

void SetPieceRunner::rowHeading(int row, float dir[3], int depth) const {
    // `sub_450940`, the plain arm: the vector from the current record to the
    // one the walk is heading for. (Its flag-0x40 smoothing is unported.)
    const auto& p = sfx_->pieces[static_cast<std::size_t>(row)];
    const auto& s = st_[static_cast<std::size_t>(row)];
    dir[0] = dir[1] = dir[2] = 0.0f;
    const int n = static_cast<int>(p.parts.size());
    if (n <= 1) return;
    const int a = s.cur;
    const int b = s.reversed ? a - 1 : a + 1;
    if (a < 0 || a >= n || b < 0 || b >= n) return;
    float pa[3], pb[3];
    recordPos(row, a, pa, depth);
    recordPos(row, b, pb, depth);
    for (int k = 0; k < 3; ++k) dir[k] = pb[k] - pa[k];
}

void SetPieceRunner::init(int row, int depth) {
    // `sub_451220`.
    if (!sfx_ || row < 0 || row >= static_cast<int>(st_.size()) || depth > 8) return;
    const auto& p = sfx_->pieces[static_cast<std::size_t>(row)];
    auto& s = st_[static_cast<std::size_t>(row)];
    s.chain = -1;
    if (p.linkType == 1) {
        s.chain = rowById(static_cast<std::int32_t>(p.linkId));
        if (s.chain == row) s.chain = -1;
        if (s.chain >= 0) init(s.chain, depth + 1);
    }
    for (const auto& r : p.parts)
        if (r.linkType == 1) {
            const int o = rowById(static_cast<std::int32_t>(r.linkId));
            if (o >= 0 && o != row) init(o, depth + 1);
        }
    const int n = static_cast<int>(p.parts.size());
    s.cur = (s.reversed && n >= 2) ? n - 2 : 0;
    if (n > 0) recordPos(row, s.cur, s.pos, depth);
}

void SetPieceRunner::show(int row) {
    // `SetPiece_Show`.
    if (!sfx_ || row < 0 || row >= static_cast<int>(st_.size())) return;
    init(row, 0);
    const auto& p = sfx_->pieces[static_cast<std::size_t>(row)];
    auto& s = st_[static_cast<std::size_t>(row)];
    if (p.parts.empty()) return;              // `if (i32(v1, 8) > 0)`
    s.shown = true;
    ++shows_;
    if (p.linkType == 1 && s.chain >= 0 && !st_[static_cast<std::size_t>(s.chain)].shown)
        show(s.chain);
    for (const auto& r : p.parts)
        if (r.linkType == 1) {
            const int o = rowById(static_cast<std::int32_t>(r.linkId));
            if (o >= 0 && o != row && !st_[static_cast<std::size_t>(o)].shown) show(o);
        }
    s.clock = 0.0f;
    s.reversed = (p.flags & 0x4u) != 0;       // bit 8 is taken from bit 4
    s.pingpong = (p.flags & 0x10u) != 0;
    s.iter = 1;
    s.waiting = p.delay != 0.0f;              // bit 0x20
    s.duration = blockDuration(row);
}

void SetPieceRunner::showKeyed(int a1, int a2) {
    if (!sfx_) return;
    for (std::size_t i = 0; i < sfx_->pieces.size(); ++i)
        if (sfx_->pieces[i].key0 == a1 && sfx_->pieces[i].key1 == a2)
            show(static_cast<int>(i));
}

void SetPieceRunner::advance(int row) {
    // `sub_450E50`: which segment the clock is in, and where along it.
    const auto& p = sfx_->pieces[static_cast<std::size_t>(row)];
    auto& s = st_[static_cast<std::size_t>(row)];
    const int n = static_cast<int>(p.parts.size());
    if (n == 0) return;
    if (n == 1) {
        s.cur = 0;
        recordPos(row, 0, s.pos, 0);
        return;
    }
    const float t = s.clock;
    int k, next;
    float acc = 0.0f, seg;
    if (s.reversed) {
        k = n - 1;
        seg = p.parts[static_cast<std::size_t>(k - 1)].dur;
        while (acc + seg < t && k > 1) {
            --k; acc += seg;
            seg = p.parts[static_cast<std::size_t>(k - 1)].dur;
        }
        next = k - 1;
    } else {
        k = 0;
        seg = p.parts[0].dur;
        while (acc + seg < t && k < n - 2) {
            ++k; acc += seg;
            seg = p.parts[static_cast<std::size_t>(k)].dur;
        }
        next = k + 1;
    }
    const float frac = seg <= 0.0f ? 0.0f : (t - acc) / seg;
    float a[3], b[3];
    s.cur = k;
    recordPos(row, k, a, 0);
    recordPos(row, next, b, 0);
    for (int i = 0; i < 3; ++i) s.pos[i] = (b[i] - a[i]) * frac + a[i];
}

bool SetPieceRunner::endTest(int row, float dt) {
    // `sub_450D60`. -> false hides the row.
    const auto& p = sfx_->pieces[static_cast<std::size_t>(row)];
    auto& s = st_[static_cast<std::size_t>(row)];
    s.clock += dt;
    if (s.waiting) {
        if (s.clock >= p.delay) { s.clock = 0.0f; s.waiting = false; }
        return true;
    }
    if (s.clock >= s.duration) {
        if (s.iter >= p.loops) return false;
        if (s.pingpong) {
            s.reversed = !s.reversed;
            s.duration = blockDuration(row);   // recomputed, the same sum
        }
        s.clock = 0.0f;
        if (p.loops != 999) ++s.iter;
    }
    return true;
}

int SetPieceRunner::indirectEffect(int row) const {
    // `sub_451600`'s `v7 <= 0` arm: the current record's +4, or the record
    // before it when the walk runs backwards (flag 8).
    const auto& p = sfx_->pieces[static_cast<std::size_t>(row)];
    const auto& s = st_[static_cast<std::size_t>(row)];
    const int n = static_cast<int>(p.parts.size());
    const int k = s.reversed ? s.cur - 1 : s.cur;
    if (k < 0 || k >= n) return 0;
    return p.parts[static_cast<std::size_t>(k)].effect;
}

void SetPieceRunner::tick(float dt, ParticleField& fx) {
    // `sub_451600`.
    registered_ = 0;
    if (!sfx_) return;
    for (std::size_t i = 0; i < st_.size(); ++i) {
        auto& s = st_[i];
        if (!s.shown) continue;
        const auto& p = sfx_->pieces[i];
        s.effectLast = 0;
        if (!s.waiting) {
            advance(static_cast<int>(i));
            const int id = p.effectId > 0 ? p.effectId : indirectEffect(static_cast<int>(i));
            // A DIAGNOSTIC, not a port: `OMK_SKIP_EFFECT=n` leaves effect n
            // unspawned, so a picture can be argued with one ingredient out.
            static const int skip = std::getenv("OMK_SKIP_EFFECT") ? std::atoi(std::getenv("OMK_SKIP_EFFECT")) : -1;
            // AN UNDEFINED ANCHOR. A type-1 link puts this row's records in
            // the frame of another row, whose heading `sub_450940` builds
            // from its current and NEXT record. For a row with ONE record
            // the function zeroes the heading and then - `cmp [eax+8], 1 /
            // jg` falls through, no return - computes it anyway from the
            // record 36 bytes past the only one, which is the next block's
            // 16-byte header and the front of its first record: an
            // orientation out of stack garbage. Impasse's two `ttt` star
            // rings are linked that way (row 2, one record), and the
            // reader's frames of the original show no ring on the portal,
            // where the intro's ring - GRID's `ttt`, anchored to a
            // three-record row - is the capture's dark spiky ring. So: a
            // ring on an undefined anchor is not drawn. A RECONSTRUCTION
            // settled by the capture, not a value the engine computes.
            const bool undefinedAnchor = p.linkType == 1 && s.chain >= 0 &&
                sfx_->pieces[static_cast<std::size_t>(s.chain)].parts.size() <= 1;
            if (id > 0 && id != skip && !undefinedAnchor)
                if (const FxEffect* e = sfx_->byId(id)) {
                    fx.spawn(*e, s.pos);
                    ++registered_;
                    s.effectLast = id;
                }
        }
        if (!endTest(static_cast<int>(i), dt)) s.shown = false;
    }
}

int SetPieceRunner::shownCount() const {
    int n = 0;
    for (const auto& s : st_) n += s.shown ? 1 : 0;
    return n;
}

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/adpcm.h"
#include "platform/datafs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace omk {
namespace {

// The same values as tables/adpcm.json, kept as a fallback for a caller that
// has no data directory. `AdpcmTables::loadJson` is the normal path, and
// `verify.py: engine ADPCM` asserts the two agree - a baked-in copy that
// drifted from the extraction would otherwise be invisible.
constexpr std::int32_t kStep[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr std::int32_t kIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

std::int32_t clampSample(std::int32_t v) {
    return v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
}

struct Channel {
    std::int32_t pred = 0;
    std::int32_t idx = 0;           // the step is `step()[idx]`, in the table
};

}  // namespace

void AdpcmTables::buildLut() {
    lut_.clear();
    if (!valid()) return;
    lut_.resize(89 * 16);
    for (std::int32_t idx = 0; idx < 89; ++idx) {
        const std::int32_t step = step_[static_cast<std::size_t>(idx)];
        for (int nib = 0; nib < 16; ++nib) {
            // the delta, WITHOUT IMA's `step >> 3` bias term - the same sum,
            // the same shift, the same sign as the branching law this replaces
            std::int32_t d = 0;
            if (nib & 4) d  = 4 * step;
            if (nib & 2) d += 2 * step;
            if (nib & 1) d += step;
            d >>= 2;
            std::int32_t next = idx + index_[static_cast<std::size_t>(nib)];
            next = next < 0 ? 0 : (next > 88 ? 88 : next);
            lut_[static_cast<std::size_t>(idx) * 16u + static_cast<std::size_t>(nib)] =
                Nibble{(nib & 8) ? -d : d, next};
        }
    }
}

AdpcmTables AdpcmTables::builtin() {
    AdpcmTables t;
    t.step_.assign(std::begin(kStep), std::end(kStep));
    t.index_.assign(std::begin(kIndex), std::end(kIndex));
    t.buildLut();
    return t;
}

AdpcmTables AdpcmTables::loadJson(const std::string& path) {
    AdpcmTables t;
    const std::string s = readTextFile(path);
    if (s.empty()) return t;
    const auto grab = [&](const char* key, std::vector<std::int32_t>& out) {
        const auto k = s.find(key);
        if (k == std::string::npos) return;
        const auto lb = s.find('[', k);
        const auto rb = s.find(']', lb);
        if (lb == std::string::npos || rb == std::string::npos) return;
        std::size_t i = lb + 1;
        while (i < rb) {
            while (i < rb && !(std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-')) ++i;
            if (i >= rb) break;
            out.push_back(std::atoi(s.c_str() + i));
            while (i < rb && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-')) ++i;
        }
    };
    grab("\"index\"", t.index_);
    grab("\"step\"", t.step_);
    t.buildLut();
    return t;
}

std::vector<std::int16_t> adpcmDecode(std::span<const std::byte> in, bool stereo,
                                      const AdpcmTables& t) {
    std::vector<std::int16_t> out;
    if (!t.valid()) return out;
    // two samples a byte, known before the first: grown by doubling, a
    // three-minute track asked for 4, 8 and then 16 MB in one piece
    out.reserve(in.size() * 2);
    const int nch = stereo ? 2 : 1;
    Channel ch[2];

    // one nibble, through the table (`AdpcmTables::nibble`): the delta
    // WITHOUT IMA's `step >> 3` bias term, then the next index
    const auto nibble = [&](Channel& c, int nib) {
        const AdpcmTables::Nibble& e = t.nibble(c.idx, nib);
        c.pred = clampSample(c.pred + e.delta);
        c.idx = e.next;
        return static_cast<std::int16_t>(c.pred);
    };

    for (std::size_t i = 0; i < in.size(); ++i) {
        const auto byte = static_cast<std::uint8_t>(in[i]);
        // The HIGH nibble first - one of the two differences from textbook
        // IMA. In STEREO the split is inside the byte, not between bytes:
        // sub_483340 gives the high nibble to the left channel and the low to
        // the right, so each byte is one interleaved frame. Alternating whole
        // BYTES between channels decodes without complaint and produces
        // garbage, which is why this is written out rather than assumed.
        out.push_back(nibble(ch[0], (byte >> 4) & 0xF));
        out.push_back(nibble(ch[stereo ? 1 : 0], byte & 0xF));
    }
    return out;
}

void AdpcmStereoStream::reset() {
    for (int c = 0; c < 2; ++c) {
        pred_[c] = 0;
        idx_[c] = 0;
    }
}

// `adpcmDecode`'s own nibble law, a channel a nibble, through the table.
// Tables that are not valid have no table and decode SILENCE - the branching
// version read `index()` / `step()` out of range there, and `MusicPlayer`
// refuses such tables before it ever builds a stream.
void AdpcmStereoStream::frame(std::byte b, std::int16_t& left, std::int16_t& right) {
    if (!t_->valid()) { left = right = 0; return; }
    const auto byte = static_cast<std::uint8_t>(b);
    const AdpcmTables::Nibble& l = t_->nibble(idx_[0], (byte >> 4) & 0xF);
    const AdpcmTables::Nibble& r = t_->nibble(idx_[1], byte & 0xF);
    pred_[0] = clampSample(pred_[0] + l.delta);
    pred_[1] = clampSample(pred_[1] + r.delta);
    idx_[0] = l.next;
    idx_[1] = r.next;
    left = static_cast<std::int16_t>(pred_[0]);
    right = static_cast<std::int16_t>(pred_[1]);
}

}  // namespace omk

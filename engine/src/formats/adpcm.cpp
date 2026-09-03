// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/adpcm.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

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
    std::int32_t idx = 0;
    std::int32_t step = 7;
};

}  // namespace

AdpcmTables AdpcmTables::builtin() {
    AdpcmTables t;
    t.step_.assign(std::begin(kStep), std::end(kStep));
    t.index_.assign(std::begin(kIndex), std::end(kIndex));
    return t;
}

AdpcmTables AdpcmTables::loadJson(const std::string& path) {
    AdpcmTables t;
    std::ifstream f(path);
    if (!f) return t;
    std::stringstream ss; ss << f.rdbuf();
    const std::string s = ss.str();
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
    return t;
}

std::vector<std::int16_t> adpcmDecode(std::span<const std::byte> in, bool stereo,
                                      const AdpcmTables& t) {
    std::vector<std::int16_t> out;
    if (!t.valid()) return out;
    const int nch = stereo ? 2 : 1;
    Channel ch[2];
    for (int c = 0; c < nch; ++c) ch[c].step = t.step()[0];

    const auto nibble = [&](Channel& c, int nib) {
        // the delta, WITHOUT IMA's `step >> 3` bias term
        std::int32_t d = 0;
        if (nib & 4) d  = 4 * c.step;
        if (nib & 2) d += 2 * c.step;
        if (nib & 1) d += c.step;
        d >>= 2;
        c.pred = clampSample(c.pred + ((nib & 8) ? -d : d));
        c.idx += t.index()[static_cast<std::size_t>(nib)];
        c.idx = c.idx < 0 ? 0 : (c.idx > 88 ? 88 : c.idx);
        c.step = t.step()[static_cast<std::size_t>(c.idx)];
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

}  // namespace omk

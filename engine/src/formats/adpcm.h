// SPDX-License-Identifier: GPL-3.0-or-later
// OTNS ADPCM - the codec every voice line and scene sound is stored in.
//
// Transcribed from the game's own decoder, `sub_483200` (mono) and
// `sub_483340` (stereo), using the step and index tables at dword_4BCC50 and
// dword_4BCC10 - both lifted to `tables/adpcm.json`.
//
// It is IMA-ADPCM with **two differences from the textbook version, and both
// matter**:
//
//   * the HIGH nibble of each byte is decoded first, not the low one;
//   * the delta is `(4*b2 + 2*b1 + b0) * step >> 2` - IMA's unconditional
//     `step >> 3` bias term is ABSENT. Leaving it in makes the predictor
//     drift, about -9000 DC over a line, with the audio buried under it.
//
// The engine writes each decoded sample twice, upsampling to its mixer rate.
// That is dropped here; the useful rate is 22050 Hz.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

inline constexpr int kAdpcmRate = 22050;

// The tables are DATA - loaded from tables/adpcm.json rather than baked in, so
// they can still be diffed against the extraction (engine/README.md).
class AdpcmTables {
public:
    static AdpcmTables loadJson(const std::string& path);
    static AdpcmTables builtin();          // for a caller with no data dir
    bool valid() const { return step_.size() == 89 && index_.size() == 16; }
    const std::vector<std::int32_t>& step() const { return step_; }
    const std::vector<std::int32_t>& index() const { return index_; }
private:
    std::vector<std::int32_t> step_, index_;
};

// -> interleaved 16-bit PCM. `stereo` decodes two independent channels.
std::vector<std::int16_t> adpcmDecode(std::span<const std::byte> in,
                                      bool stereo, const AdpcmTables& t);

}  // namespace omk

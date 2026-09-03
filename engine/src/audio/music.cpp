// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/music.h"

#include <cstdio>

namespace omk {

bool MusicPlayer::play(const DataFs& fs, const AdpcmTables& tables,
                       int track, bool loop) {
    // `Music_PlayTrack` returns without playing for anything below 2, which is
    // why 3 of the 521 shipped `music.play` sites name track 0 and are not a
    // decode gap (docs/SCRIPT_VM 103). Treat it as the STOP it is.
    if (track < 2) { stop(); return false; }

    char rel[64];
    std::snprintf(rel, sizeof rel, "TRACKS/%d.ADP", track);
    const auto path = fs.resolve(rel);
    if (!path) { stop(); return false; }
    const auto raw = adpcmDecode(DataFs::readPath(*path), true, tables);
    if (raw.empty()) { stop(); return false; }

    // The tracks are 22050 stereo and the device is rarely. Nearest, for the
    // same reason the interface sounds are resampled that way: what
    // DirectSound's own resampler sounded like is the driver's and has no
    // reachable tier (`PORTING` B5's argument, one level down).
    const double step = static_cast<double>(kAdpcmRate) / rate_;
    const std::size_t frames = raw.size() / 2;
    pcm_.clear();
    pcm_.reserve(static_cast<std::size_t>(frames / step) * 2);
    for (std::size_t i = 0;; ++i) {
        const std::size_t sf = static_cast<std::size_t>(i * step);
        if (sf >= frames) break;
        pcm_.push_back(raw[sf * 2] / 32768.0f);
        pcm_.push_back(raw[sf * 2 + 1] / 32768.0f);
    }
    pos_ = 0;
    track_ = track;
    loop_ = loop;
    return true;
}

void MusicPlayer::pull(std::vector<float>& out, std::size_t frames) {
    if (pcm_.empty()) return;
    for (std::size_t f = 0; f < frames; ++f) {
        if (pos_ + 1 >= pcm_.size()) {
            // THE LOOP, and it is field 1 of `music.play` - the script's
            // decision, honoured here rather than by whoever owns the device.
            if (!loop_) { pcm_.clear(); pos_ = 0; return; }
            pos_ = 0;
        }
        out.push_back(pcm_[pos_++]);
        out.push_back(pcm_[pos_++]);
    }
}

}  // namespace omk

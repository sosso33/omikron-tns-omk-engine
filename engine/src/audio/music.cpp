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
    std::vector<std::byte> raw = DataFs::readPath(*path);
    if (raw.empty() || !tables.valid()) { stop(); return false; }

    // The tracks are 22050 stereo and the device is rarely. Nearest, for the
    // same reason the interface sounds are resampled that way: what
    // DirectSound's own resampler sounded like is the driver's and has no
    // reachable tier (`PORTING` B5's argument, one level down).
    const double step = static_cast<double>(kAdpcmRate) / rate_;
    const std::size_t frames = raw.size();          // a byte is a stereo frame
    // The resampled length, counted the way the resampling loop counted it:
    // output frame i exists while `size_t(i * step)` is still inside the track.
    std::size_t n = 0;
    while (static_cast<std::size_t>(n * step) < frames) ++n;
    adpcm_ = std::move(raw);
    tables_ = tables;
    stream_ = std::make_unique<AdpcmStereoStream>(tables_);
    decoded_ = 0;
    cur_[0] = cur_[1] = 0;
    srcFrames_ = frames;
    step_ = step;
    outFrames_ = n;
    outPos_ = 0;
    track_ = track;
    loop_ = loop;
    return true;
}

void MusicPlayer::pull(std::vector<float>& out, std::size_t frames) {
    if (outFrames_ == 0) return;
    for (std::size_t f = 0; f < frames; ++f) {
        if (outPos_ >= outFrames_) {
            // THE LOOP, and it is field 1 of `music.play` - the script's
            // decision, honoured here rather than by whoever owns the device.
            if (!loop_) { adpcm_.clear(); adpcm_.shrink_to_fit(); outFrames_ = 0; outPos_ = 0; return; }
            outPos_ = 0;
        }
        const std::size_t sf = static_cast<std::size_t>(outPos_ * step_);
        // decode forward to frame `sf`; the loop's wrap is the one way back
        if (sf + 1 < decoded_) { stream_->reset(); decoded_ = 0; }
        while (decoded_ <= sf) stream_->frame(adpcm_[decoded_++], cur_[0], cur_[1]);
        out.push_back(cur_[0] / 32768.0f);
        out.push_back(cur_[1] / 32768.0f);
        ++outPos_;
    }
}

}  // namespace omk

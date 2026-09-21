// SPDX-License-Identifier: GPL-3.0-or-later
// THE MUSIC PLAYER - `Music_PlayTrack` (0x0041E110) and its loop.
//
// `docs/SCRIPT_VM.md` 103: the VM's `music.play` builds `TRACKS\%d.ADP` from
// field 0, **sets the loop pointer from field 1**, and streams the file; the
// handler skips the whole thing when field 0 already equals `g_MusicTrack`,
// which it then sets. `Area_Load`'s case 9 does the same from the area
// header's `+142`, and `Game_Close` calls `Music_PlayTrack(0, 1)`.
//
// So WHICH track and WHETHER it loops are the script's decisions, and the
// streaming that honours them is the engine's. `PORTING` A2 puts that split
// here rather than in a backend: the frontend is a device and must not be
// deciding when a track restarts. An earlier version had the SDL file topping
// the queue up when it ran low, which produced the right sound and put a
// script's decision in the one place nothing can check it.
//
// **The decode is not streamed.** `Audio_SetStreamMode` selects a buffer size
// through `music.play`'s field 2 and the original reads the file as it goes;
// this decodes the whole track once. For the menu's 109 that is 155 seconds -
// about 27 MB of interleaved float at 44100 - which is worth knowing and is
// not worth fixing until something needs the memory back.
#pragma once

#include "formats/adpcm.h"
#include "platform/datafs.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <vector>

namespace omk {

class MusicPlayer {
public:
    // The device's own rate, since the tracks are 22050 and a host is rarely.
    explicit MusicPlayer(int deviceRate = 44100) : rate_(deviceRate) {}

    // `Music_PlayTrack(track, loop)`. Anything below 2 is the engine's own
    // STOP - it returns without playing - and is not a missing file.
    // -> false when the track could not be played, which includes that stop.
    bool play(const DataFs& fs, const AdpcmTables& tables, int track, bool loop);

    int  track() const { return track_; }
    bool looping() const { return loop_; }
    bool playing() const { return outFrames_ > 0; }
    // Seconds of the track, for a caller that wants to report it.
    double seconds() const { return outFrames_ == 0 ? 0.0
                                                    : (2.0 * outFrames_) / 2.0 / rate_; }

    // Take the next `frames` stereo frames, wrapping when the track loops and
    // going silent when it does not. Appends to `out` interleaved.
    void pull(std::vector<float>& out, std::size_t frames);

    void stop() { adpcm_.clear(); adpcm_.shrink_to_fit(); stream_.reset(); outFrames_ = 0; outPos_ = 0; track_ = -1; }

private:
    int    rate_;
    int    track_ = -1;
    bool   loop_ = false;
    // THE TRACK AS THE FILE'S OWN BYTES, decoded as it plays (2026-09-21). It
    // was first resampled whole to the device's rate as floats (64 MB for a
    // three-minute track), then kept as the decoder's 16-bit stereo (16 MB,
    // todo/optimization.md step 5) - and that 16 MB, asked for in ONE piece
    // while a city loads, is what a Vita's heap refused with 106 MB free. A
    // stereo stream is a byte a frame with no header, so the 4 MB of ADPCM is
    // kept instead and `pull` decodes forward to the frame the same nearest
    // index (`size_t(i * step)`) names; a loop restarts the decoder. The
    // samples that leave are the ones that left before
    // (`verify.py: engine: music storage`).
    std::vector<std::byte> adpcm_;        // the file: one stereo frame a byte
    AdpcmTables   tables_;
    std::size_t   decoded_ = 0;           // frames decoded so far; `cur_` is frame decoded_ - 1
    std::int16_t  cur_[2] = {0, 0};
    std::unique_ptr<AdpcmStereoStream> stream_;
    std::size_t srcFrames_ = 0;
    double      step_ = 0.0;              // kAdpcmRate / rate_
    std::size_t outFrames_ = 0;           // the resampled track's length, in frames
    std::size_t outPos_ = 0;              // the next output FRAME
};

}  // namespace omk

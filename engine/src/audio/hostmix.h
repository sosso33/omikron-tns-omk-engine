// SPDX-License-Identifier: GPL-3.0-or-later
// THE HOST'S MIX - what a frontend sums into its audio device: the STREAM
// (the films' soundtrack, the game's music) at the music gain, and up to
// eight ONE-SHOTS (interface blips, scene sounds, a line's voice) each at its
// own gain, looping or not. Moved out of `backends/sdl/sdlfront.cpp`
// unchanged (2026-10-03) so the Carbon frontend sums the same way.
//
// Not the engine's mixer: there is none to port - `Sound_Init` sets a
// DirectSound primary buffer and DirectSound sums into it (`audio/mixer.h`).
// This stands where DirectSound stood, and its attenuation and pan law are
// this port's (PORTING, the audio row). It takes NO lock: a frontend whose
// device pulls from another thread (SDL) holds its own around every call; one
// that refills from the main thread (Carbon) needs none.
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace omk {

class HostMixer {
public:
    // Device-rate interleaved float samples onto the end of the stream.
    void queue(std::span<const float> s) { stream_.insert(stream_.end(), s.begin(), s.end()); }
    // A one-shot over the stream; the oldest is dropped past eight (a cap: a
    // held key would otherwise stack voices without end). -> its handle for
    // `stop`. The samples are SHARED (`Sound_Play3D` plays a duplicate of the
    // bank's buffer: same memory, a second voice). What the cap pushes out is
    // moved to `*dropped` when given, so a caller under a lock frees it after.
    int play(std::shared_ptr<const std::vector<float>> s, bool loop, float gain,
             std::shared_ptr<const std::vector<float>>* dropped = nullptr);
    void stop(int handle);
    void flush() { stream_.clear(); head_ = 0; }              // the stream only
    void clear() { flush(); shots_.clear(); }                 // everything
    void setMusicGain(float g) { musicGain_ = g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g); }
    // Stream samples still to play (all channels counted).
    std::size_t queued() const { return stream_.size() - head_; }
    // `n` output samples, interleaved: the stream at the music gain plus every
    // shot at its gain, clamped to [-1, 1]; a finished shot is dropped, a
    // looping one wraps until `stop`.
    void mix(float* dst, std::size_t n);

private:
    struct Shot { std::shared_ptr<const std::vector<float>> pcm; std::size_t pos; int id;
                  bool loop = false; float gain = 1.0f; };
    int nextShot_ = 1;
    float musicGain_ = 1.0f;     // Music_SetVolume, applied to the stream
    std::vector<float> stream_;
    std::size_t head_ = 0;
    std::vector<Shot> shots_;
};

}  // namespace omk

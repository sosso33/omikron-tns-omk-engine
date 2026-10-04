// SPDX-License-Identifier: GPL-3.0-or-later
// THE HOST'S MIX - what a frontend sums into its audio device: the STREAM
// (the films' soundtrack, the game's music) at the music gain, and up to
// eight ONE-SHOTS (interface blips, scene sounds, a line's voice) each at its
// own gain, looping or not. Moved out of `backends/sdl/sdlfront.cpp`
// unchanged (2026-10-03) so the Carbon frontend sums the same way.
//
// THE STREAM IS A RING (2026-10-04), as the original's is: `Morph_Open`
// plays the music through a DirectSound buffer of a FIXED eight seconds
// (bytes/s x 8.0, `dbl_4BC298`), refilled every 15 ms. This kept every
// played sample until 2^20 floats had gone by and only then compacted -
// 5.4 MB held for a one-second queue, which the profiler showed doubling
// (`todo/debug-tools.md`, the first full capture). Now nothing played is
// kept: the ring holds what is queued, grows only to the most ever queued
// at once, and `clear()` gives it back (the films queue more than the world).
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
    void queue(std::span<const float> s);
    // A one-shot over the stream; the oldest is dropped past eight (a cap: a
    // held key would otherwise stack voices without end). -> its handle for
    // `stop`. The samples are SHARED (`Sound_Play3D` plays a duplicate of the
    // bank's buffer: same memory, a second voice). What the cap pushes out is
    // moved to `*dropped` when given, so a caller under a lock frees it after.
    int play(std::shared_ptr<const std::vector<float>> s, bool loop, float gain,
             std::shared_ptr<const std::vector<float>>* dropped = nullptr);
    void stop(int handle);
    void flush() { head_ = 0; count_ = 0; }                   // the stream only
    // everything, and the ring's memory back: the next device's rate and
    // queue need not be the last one's (the films at 44100, the world at 22050)
    void clear() { flush(); std::vector<float>().swap(ring_); shots_.clear(); }
    void setMusicGain(float g) { musicGain_ = g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g); }
    // Stream samples still to play (all channels counted).
    std::size_t queued() const { return count_; }
    // The ring's size in floats - what the stream holds in memory (a check).
    std::size_t ringCapacity() const { return ring_.size(); }
    // `n` output samples, interleaved: the stream at the music gain plus every
    // shot at its gain, clamped to [-1, 1]; a finished shot is dropped, a
    // looping one wraps until `stop`.
    void mix(float* dst, std::size_t n);

private:
    struct Shot { std::shared_ptr<const std::vector<float>> pcm; std::size_t pos; int id;
                  bool loop = false; float gain = 1.0f; };
    int nextShot_ = 1;
    float musicGain_ = 1.0f;     // Music_SetVolume, applied to the stream
    std::vector<float> ring_;    // a power of two in size, or empty
    std::size_t head_ = 0;       // the next sample to play
    std::size_t count_ = 0;      // samples queued, from head_ on, wrapping
    std::vector<Shot> shots_;
};

}  // namespace omk

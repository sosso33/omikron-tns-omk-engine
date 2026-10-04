// SPDX-License-Identifier: GPL-3.0-or-later
// THE MIXER'S STREAM, driven as the music drives it (`verify.py: engine:
// music ring`): a quarter second queued whenever less than a second is left,
// mixed in 512-frame device callbacks, for 60 seconds at 22050 Hz stereo.
// Every sample must come out in the order it went in, and the ring must stay
// near the queue's size - the original's music buffer is a fixed 8 seconds
// (`audio/hostmix.h`); the stream this replaced held 5.4 MB.
//
//     hostmix_probe            -> one line: samples, mismatches, ring bytes
#include "audio/hostmix.h"

#include <cstdio>
#include <vector>

int main() {
    omk::HostMixer mix;
    const std::size_t rate = 22050, ch = 2;
    std::size_t made = 0, played = 0, bad = 0, maxQueued = 0, maxRing = 0;
    std::vector<float> out(512 * ch);
    // a sample's value is its index, scaled into [-1, 1) and exact in a float
    auto value = [](std::size_t i) { return static_cast<float>(i % 65536) / 65536.0f; };
    while (played < 60 * rate * ch) {
        if (mix.queued() < rate * ch) {
            std::vector<float> chunk(rate * ch / 4);
            for (auto& v : chunk) v = value(made++);
            mix.queue(chunk);
        }
        if (mix.queued() > maxQueued) maxQueued = mix.queued();
        if (mix.ringCapacity() > maxRing) maxRing = mix.ringCapacity();
        mix.mix(out.data(), out.size());
        for (const float v : out) if (v != value(played++)) ++bad;
    }
    // and a flush, then the clear that gives the memory back
    mix.flush();
    const bool flushed = mix.queued() == 0;
    mix.clear();
    std::printf("hostmix: %zu samples played, %zu out of order; queued at most %zu floats, "
                "ring at most %zu floats (%zu KB); flush %s, clear leaves %zu\n",
                played, bad, maxQueued, maxRing, maxRing * sizeof(float) / 1024,
                flushed ? "empties" : "DOES NOT empty", mix.ringCapacity());
    return 0;
}

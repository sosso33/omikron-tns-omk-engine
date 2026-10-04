// SPDX-License-Identifier: GPL-3.0-or-later
// The host's mix - see hostmix.h.
#include "audio/hostmix.h"

#include <algorithm>

namespace omk {

int HostMixer::play(std::shared_ptr<const std::vector<float>> s, bool loop, float gain,
                    std::shared_ptr<const std::vector<float>>* dropped) {
    if (!s || s->empty()) return -1;
    if (shots_.size() >= 8) {
        if (dropped) *dropped = std::move(shots_.front().pcm);
        shots_.erase(shots_.begin());
    }
    const int id = nextShot_++;
    shots_.push_back({std::move(s), 0, id, loop, gain});
    return id;
}

void HostMixer::queue(std::span<const float> s) {
    if (s.empty()) return;
    if (count_ + s.size() > ring_.size()) {
        // grow to the next power of two that holds everything queued, the
        // queued samples moved to the front in order
        std::size_t cap = ring_.empty() ? 4096 : ring_.size();
        while (cap < count_ + s.size()) cap *= 2;
        std::vector<float> next(cap);
        for (std::size_t i = 0; i < count_; ++i) next[i] = ring_[(head_ + i) & (ring_.size() - 1)];
        ring_.swap(next);
        head_ = 0;
    }
    const std::size_t mask = ring_.size() - 1;
    std::size_t tail = (head_ + count_) & mask;
    for (const float v : s) { ring_[tail] = v; tail = (tail + 1) & mask; }
    count_ += s.size();
}

void HostMixer::stop(int handle) {
    if (handle < 0) return;
    std::erase_if(shots_, [handle](const Shot& o) { return o.id == handle; });
}

void HostMixer::mix(float* dst, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        float v = 0.0f;
        if (count_) {
            v += ring_[head_] * musicGain_;
            head_ = (head_ + 1) & (ring_.size() - 1);
            --count_;
        }
        for (auto& one : shots_) {
            const std::vector<float>& pcm = *one.pcm;
            if (one.pos >= pcm.size()) {
                if (!one.loop || pcm.empty()) continue;
                one.pos = 0;                  // a looping shot wraps
            }
            v += pcm[one.pos++] * one.gain;
        }
        dst[i] = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    }
    std::erase_if(shots_, [](const Shot& o) {
        return !o.loop && o.pos >= o.pcm->size();  // a loop ends only on stop
    });
}

}  // namespace omk

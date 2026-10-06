// SPDX-License-Identifier: GPL-3.0-or-later
// The host's mix - see hostmix.h.
#include "audio/hostmix.h"

#include <algorithm>

namespace omk {

int HostMixer::add(Shot s, std::shared_ptr<const void>* dropped) {
    if (shots_.size() >= 8) {
        if (dropped) {
            if (shots_.front().pcm) *dropped = std::move(shots_.front().pcm);
            else                    *dropped = std::move(shots_.front().snd);
        }
        shots_.erase(shots_.begin());
    }
    s.id = nextShot_++;
    shots_.push_back(std::move(s));
    return shots_.back().id;
}

int HostMixer::play(std::shared_ptr<const std::vector<float>> s, bool loop, float gain,
                    std::shared_ptr<const void>* dropped) {
    if (!s || s->empty()) return -1;
    return add({std::move(s), 0, 0, loop, gain}, dropped);
}

int HostMixer::play(std::shared_ptr<const DeviceSound> s, bool loop, float gain,
                    std::shared_ptr<const void>* dropped) {
    if (!s || !s->size) return -1;
    return add({nullptr, 0, 0, loop, gain, std::move(s)}, dropped);
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

void HostMixer::setGain(int handle, float gain) {
    if (handle < 0) return;
    for (Shot& o : shots_)
        if (o.id == handle) o.gain = gain;
}

void HostMixer::stop(int handle) {
    if (handle < 0) return;
    std::erase_if(shots_, [handle](const Shot& o) { return o.id == handle; });
}

long long HostMixer::played(int handle) const {
    if (handle < 0) return -1;
    for (const Shot& o : shots_)
        if (o.id == handle) return static_cast<long long>(o.pos);
    return -1;
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
            const std::size_t size = one.size();
            if (one.pos >= size) {
                if (!one.loop || !size) continue;
                one.pos = 0;                  // a looping shot wraps
            }
            v += (one.pcm ? (*one.pcm)[one.pos] : one.snd->at(one.pos)) * one.gain;
            ++one.pos;
        }
        dst[i] = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    }
    std::erase_if(shots_, [](const Shot& o) {
        return !o.loop && o.pos >= o.size();  // a loop ends only on stop
    });
}

}  // namespace omk

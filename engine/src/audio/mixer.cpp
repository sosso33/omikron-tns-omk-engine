// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace omk::audio {
namespace {

std::uint16_t rd16(std::span<const std::byte> b, std::size_t o) {
    return std::uint16_t(std::uint8_t(b[o]) | (std::uint8_t(b[o + 1]) << 8));
}
std::uint32_t rd32(std::span<const std::byte> b, std::size_t o) {
    return std::uint32_t(std::uint8_t(b[o])) | (std::uint32_t(std::uint8_t(b[o + 1])) << 8) |
           (std::uint32_t(std::uint8_t(b[o + 2])) << 16) |
           (std::uint32_t(std::uint8_t(b[o + 3])) << 24);
}
bool tag(std::span<const std::byte> b, std::size_t o, const char* s) {
    for (int i = 0; i < 4; ++i)
        if (char(b[o + i]) != s[i]) return false;
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
WavLoad loadWav(std::span<const std::byte> file) {
    WavLoad r;
    // `fread(header, 0x14, 1)` - the whole 20 bytes or nothing.
    if (file.size() < 20) { r.reject = WavReject::Header; return r; }
    if (!tag(file, 0, "RIFF")) { r.reject = WavReject::NotRiff; return r; }
    // strncmp(aWavefmt, byte_67A050, 8) - "WAVEfmt " at +8, eight characters.
    static const char kWaveFmt[8] = {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '};
    for (int i = 0; i < 8; ++i)
        if (char(file[8 + i]) != kWaveFmt[i]) { r.reject = WavReject::NotWaveFmt; return r; }

    // `fread(&fmt, 0x10, 1)` - SIXTEEN bytes, whatever the fmt chunk's own
    // size field said. The size field at +16 is read as part of the header
    // above and never looked at.
    if (file.size() < 36) { r.reject = WavReject::PcmHeader; return r; }
    r.fmt.tag        = rd16(file, 20);
    r.fmt.channels   = rd16(file, 22);
    r.fmt.rate       = rd32(file, 24);
    r.fmt.avgBytes   = rd32(file, 28);
    r.fmt.blockAlign = rd16(file, 32);
    r.fmt.bits       = rd16(file, 34);
    if (r.fmt.tag != 1) { r.reject = WavReject::NotPcm; return r; }

    // The two-byte peek. `fread(&dummy, 2, 1)`; if it is non-zero, seek back
    // over it. A 16-byte fmt chunk is followed by the next chunk id, whose
    // first two bytes here are always 'd','a' - so on every shipped file the
    // rewind fires and nothing is consumed. An 18-byte fmt chunk with
    // cbSize == 0 would be the other arm; NO shipped file takes it.
    std::size_t off = 36;
    if (file.size() < off + 2) { r.reject = WavReject::DummyByte; return r; }
    const std::uint16_t dummy = rd16(file, off);
    off += 2;
    if (dummy != 0) { r.rewoundDummy = true; off -= 2; }

    // Then 8-byte chunk headers until `data` (0x61746164).
    for (;;) {
        if (off + 8 > file.size()) { r.reject = WavReject::DataHeader; return r; }
        const bool isData = tag(file, off, "data");
        const std::uint32_t sz = rd32(file, off + 4);
        if (isData) {
            r.dataBytes  = sz;
            r.dataOffset = off + 8;
            if (r.dataOffset + sz > file.size()) { r.reject = WavReject::TooShort; return r; }
            return r;
        }
        ++r.skippedChunks;
        off += 8 + sz;
    }
}

// ---------------------------------------------------------------------------
std::uint32_t lengthMs(std::uint32_t bytes, const WaveFormat& fmt,
                       std::uint32_t frequency) {
    const std::uint32_t d = std::uint32_t(fmt.bits / 8) * frequency * fmt.channels;
    if (!d) return 0;
    return std::uint32_t(std::uint64_t(bytes) * 1000u / d);
}

int volumeFromPercent(int percent) {
    int p = percent;
    if (p > 100) p = 100;          // the clamp is one-sided in the engine too
    return -10000 * p / 100;
}

// ---------------------------------------------------------------------------
int Mixer::createBuffer(const WaveFormat& fmt, std::uint32_t bytes,
                        std::span<const std::int16_t> pcm) {
    int i = 0;
    for (; i < kMaxBuffers; ++i)
        if (!buffers_[i].used) break;
    if (i == kMaxBuffers) return -1;      // `if (i == 160) return 0;`
    SoundBuffer& b = buffers_[i];
    b.used      = true;
    b.fmt       = fmt;
    b.bytes     = bytes;
    b.frequency = fmt.rate;
    b.volume    = 0;
    b.pcm.assign(pcm.begin(), pcm.end());
    return i;
}

bool Mixer::freeBuffer(int id) {
    if (id < 0 || id >= kMaxBuffers || !buffers_[id].used) return false;
    // The walk `Sound_FreeBuffer` does BEFORE releasing: every voice whose +52
    // names this buffer is stopped and its record reset. A voice is a
    // DuplicateSoundBuffer of the original, so releasing the original under a
    // live duplicate is what this exists to prevent.
    for (int v = 0; v < kMaxVoices; ++v) {
        if (voices_[v].soundId == id) {
            if (voices_[v].used) stopVoice(v);
            voices_[v].soundId = -1;
            voices_[v].owner   = 0;
        }
    }
    buffers_[id] = SoundBuffer{};
    return true;
}

int Mixer::liveBuffers() const {
    int n = 0;
    for (const auto& b : buffers_) n += b.used;
    return n;
}

const SoundBuffer* Mixer::buffer(int id) const {
    if (id < 0 || id >= kMaxBuffers || !buffers_[id].used) return nullptr;
    return &buffers_[id];
}

bool Mixer::setVolume(int id, int hundredthsDb) {
    if (id < 0 || id >= kMaxBuffers || !buffers_[id].used) return false;
    buffers_[id].volume = hundredthsDb;
    return true;
}

bool Mixer::setFrequency(int id, std::uint32_t hz) {
    if (id < 0 || id >= kMaxBuffers || !buffers_[id].used) return false;
    buffers_[id].frequency = hz;
    return true;
}

std::uint32_t Mixer::getFrequency(int id) const {
    const SoundBuffer* b = buffer(id);
    return b ? b->frequency : 0;
}

std::uint32_t Mixer::lengthMs(int id) const {
    const SoundBuffer* b = buffer(id);
    if (!b) return 0;
    return omk::audio::lengthMs(b->bytes, b->fmt, b->frequency);
}

// ---------------------------------------------------------------------------
int Mixer::play3d(int soundId, bool loop, const Sound3D* place,
                  std::uint32_t owner) {
    if (soundId < 0 || soundId >= kMaxBuffers || !buffers_[soundId].used) return -1;
    int slot = 0;
    for (; slot < kMaxVoices; ++slot)
        if (!voices_[slot].used) break;
    if (slot == kMaxVoices) return -1;

    Voice& v = voices_[slot];
    v = Voice{};
    v.used  = true;                       // the DuplicateSoundBuffer succeeded
    v.flags = 0;
    if (place) {
        for (int k = 0; k < 3; ++k) { v.pos[k] = place->pos[k]; v.vel[k] = place->vel[k]; }
        v.maxDistance = place->maxDistance;   // +32 <- a3[7]
        v.minDistance = place->minDistance;   // +36 <- a3[6]
        v.unknown40   = -1.0f;                // 0xBF800000, written and not read
        v.frequency   = float(buffers_[soundId].frequency);
        v.flags &= ~kVoiceNo3d;               // `v19 & 0xFB` - clear bit 2
    } else {
        v.flags = kVoiceNo3d;                 // DS3DMODE_DISABLE
    }
    // Then, on both arms: |3 when looping, |1 when not. The engine writes the
    // owner and the id twice on the second arm; the value is the same.
    v.flags |= loop ? (kVoiceInUse | kVoiceLooping) : kVoiceInUse;
    v.owner   = owner;
    v.soundId = soundId;
    cursor_[slot] = 0.0;
    return slot;
}

bool Mixer::setVoice3d(int slot, const Sound3D& place) {
    if (slot < 0 || slot >= kMaxVoices || !voices_[slot].used) return false;
    Voice& v = voices_[slot];
    for (int k = 0; k < 3; ++k) { v.pos[k] = place.pos[k]; v.vel[k] = place.vel[k]; }
    v.minDistance = place.minDistance;
    v.maxDistance = place.maxDistance;
    return true;
}

int Mixer::findVoice(int soundId, std::uint32_t owner) const {
    // `Sound_FindVoice` counts as it walks and gives up at 16, so a table with
    // no match returns -1 rather than running off the end.
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices_[i].used && voices_[i].soundId == soundId && voices_[i].owner == owner)
            return i;
    return -1;
}

bool Mixer::stopVoice(int slot) {
    if (slot < 0 || slot >= kMaxVoices) return false;
    Voice& v = voices_[slot];
    if (!v.used) { v.soundId = -1; v.owner = 0; return false; }   // the else arm
    v = Voice{};
    v.soundId = -1;
    cursor_[slot] = 0.0;
    return true;
}

int Mixer::liveVoices() const {
    int n = 0;
    for (const auto& v : voices_) n += v.used;
    return n;
}

void Mixer::setListener(const Listener& l) {
    listener_ = l;
    // The 2D fallback basis: the FRONT vector normalised, with the engine's own
    // guard. `if (len <= 0.0001) basis = 0` - not a division by a clamped
    // length, a zero.
    const float x = l.front[0], y = l.front[1], z = l.front[2];
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len <= 0.0001f) { basis_[0] = basis_[1] = basis_[2] = 0.0f; }
    else                { basis_[0] = x / len; basis_[1] = y / len; basis_[2] = z / len; }
}

// ---------------------------------------------------------------------------
void Mixer::render(std::int16_t* out, int frames) {
    if (out) std::memset(out, 0, std::size_t(frames) * kMixChannels * sizeof(std::int16_t));

    for (int s = 0; s < kMaxVoices; ++s) {
        Voice& v = voices_[s];
        if (!v.used || v.soundId < 0) continue;
        const SoundBuffer& b = buffers_[v.soundId];
        if (!b.used || b.pcm.empty()) continue;

        const int    ch    = b.fmt.channels ? b.fmt.channels : 1;
        const std::size_t nFrames = b.pcm.size() / std::size_t(ch);
        const double step  = double(b.frequency) / double(kMixRate);
        // DirectSound's volume is hundredths of a dB; 0 is unity.
        const double gain  = (b.volume >= 0) ? 1.0 : std::pow(10.0, double(b.volume) / 2000.0);
        // The 3D law is DirectSound's, and it is DOCUMENTED, not read: inverse
        // distance, flat inside minDistance, and no further attenuation past
        // maxDistance. Nothing here asserts it.
        double atten = 1.0;
        if (!(v.flags & kVoiceNo3d) && v.minDistance > 0.0f) {
            const double dx = double(v.pos[0]) - listener_.pos[0];
            const double dy = double(v.pos[1]) - listener_.pos[1];
            const double dz = double(v.pos[2]) - listener_.pos[2];
            double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (v.maxDistance > 0.0f && d > v.maxDistance) d = v.maxDistance;
            atten = (d <= v.minDistance) ? 1.0 : double(v.minDistance) / d;
        }
        const double g = gain * atten;

        for (int f = 0; f < frames; ++f) {
            std::size_t idx = std::size_t(cursor_[s]);
            if (idx >= nFrames) {
                if (!(v.flags & kVoiceLooping)) { v.used = false; break; }
                cursor_[s] = 0.0;
                idx = 0;
            }
            if (out) {
                const std::int16_t l = b.pcm[idx * std::size_t(ch)];
                const std::int16_t r = (ch == 2) ? b.pcm[idx * 2 + 1] : l;
                const double ml = out[f * 2 + 0] + double(l) * g;
                const double mr = out[f * 2 + 1] + double(r) * g;
                out[f * 2 + 0] = std::int16_t(std::clamp(ml, -32768.0, 32767.0));
                out[f * 2 + 1] = std::int16_t(std::clamp(mr, -32768.0, 32767.0));
            }
            cursor_[s] += step;
        }
    }
}

}  // namespace omk::audio

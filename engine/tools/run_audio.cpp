// SPDX-License-Identifier: GPL-3.0-or-later
// THE AUDIO PATH, RUN - the `.wav` acceptance path over every shipped file,
// the bank and the voice pool exercised to their limits, and the reference
// mixer shown to be transparent.
//
//     run_audio <game dir> <tables/ui.json> <out.bin>
//
// What this exists to test, in the order the numbers come out:
//
//  1. `Wav_LoadToBuffer` (0x0049F830) run over all 61 shipped
//     `gamedata/I2D/sounds/*.wav`. This is the tier-2 half: the data can fail it,
//     and it reports which of the loader's arms the corpus actually reaches -
//     the answer is that TWO of them are never taken by any shipped file.
//  2. `Sound_LengthMs` (0x0046CC70) per file, which `verify.py` re-derives
//     from the same files in Python. A differential, not a self-check.
//  3. The interface's own sound table: 45 names, every one resolving to a
//     shipped file - and a 32-slot cache, so 13 of the 45 cannot be resident
//     at once. The engine's loader returns SILENTLY when the cache is full.
//  4. The 160-buffer bank and the 16-voice pool, filled until they refuse.
//  5. The voice flag word on all four arms of `Sound_Play3D`.
//  6. `Sound_FreeBuffer` killing the voices that play it.
//  7. The listener basis and its `<= 0.0001` guard.
//  8. The volume law - a percentage that is an ATTENUATION.
//  9. The one claim the reference mixer makes about a waveform: a mono voice,
//     not 3D, at full volume and at the mix rate comes out sample-identical in
//     both channels. `pause.wav` is the control - it ships at 22080 Hz, so it
//     resamples and must NOT be identical.
#include "audio/mixer.h"
#include "platform/datafs.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace omk::audio;

namespace {

std::uint32_t fnv(const void* p, std::size_t n) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    return h;
}

// The 45 names out of tables/ui.json - `"sounds": { "0": "men001", ... }`.
// A one-purpose scan rather than the JSON reader, because this file wants only
// the values of one flat object and the order they are numbered in.
std::vector<std::string> readSoundNames(const std::string& path) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<std::string> out;
    // Every one of the 37 screen rows carries a `"sounds"` ARRAY of its twelve
    // slots, so the first match is the wrong one; the table wanted is the
    // OBJECT. Taking the first hit gave 0 names and a silently empty check.
    std::size_t key = 0, i = std::string::npos;
    for (;;) {
        key = s.find("\"sounds\"", key);
        if (key == std::string::npos) return out;
        std::size_t c = s.find(':', key);
        if (c == std::string::npos) return out;
        while (c + 1 < s.size() && std::isspace((unsigned char)s[c + 1])) ++c;
        if (c + 1 < s.size() && s[c + 1] == '{') { i = c + 1; break; }
        key += 8;
    }
    const std::size_t end = s.find('}', i);
    for (int id = 0;; ++id) {
        const std::string want = "\"" + std::to_string(id) + "\"";
        const auto k = s.find(want, i);
        if (k == std::string::npos || k > end) break;
        const auto c = s.find(':', k);
        const auto q1 = s.find('"', c);
        const auto q2 = s.find('"', q1 + 1);
        if (q1 == std::string::npos || q2 == std::string::npos || q2 > end) break;
        out.push_back(s.substr(q1 + 1, q2 - q1 - 1));
    }
    return out;
}

std::vector<std::int16_t> samples(std::span<const std::byte> file, const WavLoad& w) {
    std::vector<std::int16_t> pcm(w.dataBytes / 2);
    for (std::size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = std::int16_t(std::uint16_t(std::uint8_t(file[w.dataOffset + i * 2])) |
                              (std::uint16_t(std::uint8_t(file[w.dataOffset + i * 2 + 1])) << 8));
    return pcm;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: run_audio <game dir> <ui.json> <out.bin>\n");
        return 2;
    }
    omk::DataFs fs(argv[1]);
    const auto names = readSoundNames(argv[2]);

    std::vector<std::int32_t> out;
    std::vector<std::uint32_t> table;   // per-file dataBytes, lengthMs

    // ---- 1/2: every shipped .wav through the engine's own acceptance -----
    // `list` matches the extension without regard to case and hands back real
    // paths, which is what keeps `cptrebour02..wav` - a shipped double dot -
    // from needing a special case here.
    const auto wavs = fs.list("I2D/sounds", ".wav");
    int found = 0, accepted = 0, rewound = 0, skipped = 0, extendedFmt = 0;
    for (const auto& e : wavs) {
        const auto raw = omk::DataFs::readPath(e);
        if (raw.empty()) continue;
        ++found;
        const WavLoad w = loadWav(raw);
        if (w.reject != WavReject::Ok) continue;
        ++accepted;
        rewound += w.rewoundDummy;
        skipped += w.skippedChunks;
        extendedFmt += !w.rewoundDummy;      // the arm that eats the two bytes
        table.push_back(w.dataBytes);
        table.push_back(lengthMs(w.dataBytes, w.fmt, w.fmt.rate));
    }
    out.insert(out.end(), {found, accepted, rewound, skipped, extendedFmt});
    std::printf("shipped .wav: %d found, %d accepted by the engine's own path; "
                "%d rewound the two-byte peek, %d ate it, %d chunks skipped\n",
                found, accepted, rewound, extendedFmt, skipped);

    // ---- 3: the interface's 45 names, and its 32-slot cache --------------
    // `Ui_LoadSound` (0x00482D00) takes the FIRST slot of 32 whose flag bit 0
    // is clear, and returns without loading when there is none - no eviction,
    // no error. So the 45 named sounds cannot all be resident.
    constexpr int kUiCacheSlots = 32;      // unk_657B40 .. dword_657D40, 16 bytes each
    int resolved = 0, cached = 0, dropped = 0;
    for (const auto& n : names) {
        const bool ok = fs.exists("i2d/sounds/" + n + ".wav");
        resolved += ok;
        if (cached < kUiCacheSlots && ok) ++cached; else ++dropped;
    }
    out.insert(out.end(), {int(names.size()), resolved, kUiCacheSlots, cached, dropped});
    std::printf("interface sounds: %zu named, %d resolve, %d fit the %d-slot "
                "cache, %d cannot be resident\n",
                names.size(), resolved, cached, kUiCacheSlots, dropped);

    // ---- 4: the bank and the pool, filled until they refuse --------------
    Mixer m(true);
    WaveFormat mono{1, 1, kMixRate, kMixRate * 2, 2, 16};
    std::vector<std::int16_t> tiny(16, 1000);
    int made = 0;
    while (m.createBuffer(mono, std::uint32_t(tiny.size() * 2), tiny) >= 0) ++made;
    const int overflow = m.createBuffer(mono, 32, tiny);
    out.insert(out.end(), {made, overflow, m.liveBuffers()});

    int voices = 0;
    while (m.play3d(0, false, nullptr, 7) >= 0) ++voices;
    const int voiceOverflow = m.play3d(0, false, nullptr, 7);
    out.insert(out.end(), {voices, voiceOverflow, m.liveVoices()});
    std::printf("bank: %d buffers then %d; voices: %d then %d\n",
                made, overflow, voices, voiceOverflow);

    // ---- 5: the flag word on all four arms -------------------------------
    for (int s = 0; s < kMaxVoices; ++s) m.stopVoice(s);
    Sound3D place{};
    place.pos[0] = 10; place.minDistance = 39.0f; place.maxDistance = 585.0f;
    const int f2d   = m.play3d(0, false, nullptr, 1); const auto v2d   = m.voice(f2d).flags;
    const int f2dL  = m.play3d(0, true,  nullptr, 1); const auto v2dL  = m.voice(f2dL).flags;
    const int f3d   = m.play3d(0, false, &place,  1); const auto v3d   = m.voice(f3d).flags;
    const int f3dL  = m.play3d(0, true,  &place,  1); const auto v3dL  = m.voice(f3dL).flags;
    out.insert(out.end(), {int(v2d), int(v2dL), int(v3d), int(v3dL)});
    // and the block reached the record intact
    const bool placed = m.voice(f3d).minDistance == 39.0f &&
                        m.voice(f3d).maxDistance == 585.0f &&
                        m.voice(f3d).unknown40   == -1.0f;
    out.push_back(placed ? 1 : 0);

    // ---- 6: findVoice, and freeBuffer killing what plays it ---------------
    out.push_back(m.findVoice(0, 1));                 // the first of the four
    out.push_back(m.findVoice(0, 99));                // no such owner
    const int before = m.liveVoices();
    m.freeBuffer(0);
    out.insert(out.end(), {before, m.liveVoices()});
    std::printf("freeBuffer(0): %d voices before, %d after\n", before, m.liveVoices());

    // ---- 7: the listener --------------------------------------------------
    Listener L{};
    L.front[0] = 0; L.front[1] = 0; L.front[2] = 3;   // length 3, not 1
    m.setListener(L);
    const bool unit = std::abs(m.listenerBasis()[2] - 1.0f) < 1e-6f &&
                      m.listenerBasis()[0] == 0 && m.listenerBasis()[1] == 0;
    Listener Z{};
    Z.front[2] = 0.00001f;                            // inside the guard
    m.setListener(Z);
    const bool zeroed = m.listenerBasis()[0] == 0 && m.listenerBasis()[1] == 0 &&
                        m.listenerBasis()[2] == 0;
    out.insert(out.end(), {unit ? 1 : 0, zeroed ? 1 : 0});

    // ---- 8: the volume law ------------------------------------------------
    out.insert(out.end(), {volumeFromPercent(0), volumeFromPercent(50),
                           volumeFromPercent(100), volumeFromPercent(200)});

    // ---- 9: the mixer is transparent, and pause.wav is the control --------
    // men001 is 22050 Hz mono: at the mix rate, no resample, so the reference
    // mixer must be the identity. pause.wav is 22080 Hz, so it must not be.
    auto identity = [&](const std::string& file, int* mismatches, int* frames,
                        std::uint32_t* hSrc, std::uint32_t* hOut) {
        *mismatches = -1; *frames = 0; *hSrc = *hOut = 0;
        const auto raw = fs.read("i2d/sounds/" + file);
        if (raw.empty()) return;
        const WavLoad w = loadWav(raw);
        if (w.reject != WavReject::Ok) return;
        const auto pcm = samples(raw, w);
        Mixer mm(true);
        const int id = mm.createBuffer(w.fmt, w.dataBytes, pcm);
        if (id < 0) return;
        if (mm.play3d(id, false, nullptr, 0) < 0) return;
        const int n = int(pcm.size());
        std::vector<std::int16_t> mix(std::size_t(n) * 2, 0);
        mm.render(mix.data(), n);
        int bad = 0;
        for (int i = 0; i < n; ++i)
            if (mix[std::size_t(i) * 2] != pcm[std::size_t(i)] ||
                mix[std::size_t(i) * 2 + 1] != pcm[std::size_t(i)]) ++bad;
        *mismatches = bad;
        *frames = n;
        *hSrc = fnv(pcm.data(), pcm.size() * 2);
        std::vector<std::int16_t> left(std::size_t(n), 0);
        for (int i = 0; i < n; ++i) left[std::size_t(i)] = mix[std::size_t(i) * 2];
        *hOut = fnv(left.data(), left.size() * 2);
    };
    int bad1 = 0, n1 = 0, bad2 = 0, n2 = 0;
    std::uint32_t hs1 = 0, ho1 = 0, hs2 = 0, ho2 = 0;
    identity("men001.wav", &bad1, &n1, &hs1, &ho1);
    identity("pause.wav",  &bad2, &n2, &hs2, &ho2);
    // `ho1` is the FNV of the LEFT channel the reference mixer produced.
    // `verify.py` recomputes it from `men001.wav`'s data chunk in Python, so
    // the identity is asserted ACROSS the two implementations rather than
    // inside this one - the distinction CLAUDE.md section 5 exists for.
    out.insert(out.end(), {n1, bad1, n2, bad2,
                           int(hs1 == ho1 ? 1 : 0), int(hs2 == ho2 ? 1 : 0),
                           int(ho1), int(hs1)});
    std::printf("mixer identity: men001 %d frames, %d mismatched; "
                "pause (22080 Hz) %d frames, %d mismatched\n", n1, bad1, n2, bad2);

    // ---- the caps words, stated so a mutation of them is visible ----------
    out.insert(out.end(), {int(Mixer(true).capsWord()), int(Mixer(false).capsWord()),
                           kMixRate, kMixChannels, kMixBits, kMixBlockAlign,
                           kMixBytesPerSec});

    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream o(argv[3], std::ios::binary);
    const std::int32_t nScalars = std::int32_t(out.size());
    o.write(reinterpret_cast<const char*>(&nScalars), 4);
    o.write(reinterpret_cast<const char*>(out.data()),
            std::streamsize(out.size() * 4));
    const std::int32_t nFiles = std::int32_t(table.size() / 2);
    o.write(reinterpret_cast<const char*>(&nFiles), 4);
    o.write(reinterpret_cast<const char*>(table.data()),
            std::streamsize(table.size() * 4));
    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// THE MUSIC KEPT AT ITS OWN RATE PLAYS THE SAME SAMPLES - bit for bit.
//
//     music_equiv <gamedata> <tables> <track>...
//
// `MusicPlayer` used to resample a whole track to the device's rate as
// interleaved floats when it started - 64 MB for a three-minute track, the
// largest allocation in the process. It now keeps the decoder's 16-bit stereo
// at 22050 Hz and resamples in `pull` (todo/optimization.md step 5). This keeps
// the old player verbatim as `OldMusic` and plays every named track through
// both, looped and not, pulling in irregular chunks (1, 3, 777, 44100, 100000
// and 132300 frames) until a looped track has wrapped twice or an unlooped one
// has ended, and compares every sample bit for bit, plus `play`'s answer and
// `playing()` / `seconds()` after every pull.
//
// One line a track, then `mismatches <total>`. Prints only; writes nothing.
#include "audio/music.h"
#include "formats/adpcm.h"
#include "platform/datafs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// `MusicPlayer` as it stood before 2026-09-13, verbatim but for the name.
class OldMusic {
public:
    explicit OldMusic(int deviceRate) : rate_(deviceRate) {}
    bool play(const omk::DataFs& fs, const omk::AdpcmTables& tables, int track, bool loop) {
        if (track < 2) { stop(); return false; }
        char rel[64];
        std::snprintf(rel, sizeof rel, "TRACKS/%d.ADP", track);
        const auto path = fs.resolve(rel);
        if (!path) { stop(); return false; }
        const auto raw = omk::adpcmDecode(omk::DataFs::readPath(*path), true, tables);
        if (raw.empty()) { stop(); return false; }
        const double step = static_cast<double>(omk::kAdpcmRate) / rate_;
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
    void pull(std::vector<float>& out, std::size_t frames) {
        if (pcm_.empty()) return;
        for (std::size_t f = 0; f < frames; ++f) {
            if (pos_ + 1 >= pcm_.size()) {
                if (!loop_) { pcm_.clear(); pos_ = 0; return; }
                pos_ = 0;
            }
            out.push_back(pcm_[pos_++]);
            out.push_back(pcm_[pos_++]);
        }
    }
    bool playing() const { return !pcm_.empty(); }
    double seconds() const { return pcm_.empty() ? 0.0 : pcm_.size() / 2.0 / rate_; }
    void stop() { pcm_.clear(); pos_ = 0; track_ = -1; }
    std::size_t bytes() const { return pcm_.capacity() * sizeof(float); }

private:
    int rate_;
    int track_ = -1;
    bool loop_ = false;
    std::size_t pos_ = 0;
    std::vector<float> pcm_;
};

bool sameD(double a, double b) { return std::memcmp(&a, &b, sizeof a) == 0; }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: music_equiv <gamedata> <tables> <track>...\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    const auto tables = omk::AdpcmTables::loadJson(std::string(argv[2]) + "/adpcm.json");
    if (!tables.valid()) { std::fprintf(stderr, "no ADPCM tables in %s\n", argv[2]); return 1; }
    const std::size_t chunks[] = {44100, 1, 777, 100000, 3, 132300};
    long total = 0;
    for (int a = 3; a < argc; ++a) {
        const int track = std::atoi(argv[a]);
        long mismatches = 0, pulls = 0;
        std::size_t samples = 0, oldBytes = 0, srcBytes = 0;
        double secs = 0.0;
        for (const bool loop : {true, false}) {
            OldMusic o(44100);
            omk::MusicPlayer m(44100);
            const bool po = o.play(fs, tables, track, loop);
            const bool pm = m.play(fs, tables, track, loop);
            if (po != pm || o.playing() != m.playing() || !sameD(o.seconds(), m.seconds())) ++mismatches;
            if (!po) continue;
            if (loop) {
                oldBytes = o.bytes();
                secs = o.seconds();
                if (const auto path = fs.resolve("TRACKS/" + std::to_string(track) + ".ADP"))
                    srcBytes = omk::adpcmDecode(omk::DataFs::readPath(*path), true, tables).size() * 2;
            }
            const std::size_t target = static_cast<std::size_t>(o.seconds() * 44100.0 * (loop ? 2.5 : 1.2)) + 1;
            std::size_t pulled = 0;
            std::vector<float> vo, vm;
            for (int k = 0; pulled < target; ++k) {
                const std::size_t n = chunks[k % 6];
                vo.clear(); vm.clear();
                o.pull(vo, n);
                m.pull(vm, n);
                ++pulls;
                if (vo.size() != vm.size() ||
                    (!vo.empty() && std::memcmp(vo.data(), vm.data(), vo.size() * sizeof(float)) != 0) ||
                    o.playing() != m.playing() || !sameD(o.seconds(), m.seconds()))
                    ++mismatches;
                samples += vo.size();
                pulled += n;
                if (!loop && !o.playing() && !m.playing()) break;
            }
        }
        std::printf("track %d seconds %.2f | old buffer %zu KB, source %zu KB | pulls %ld samples %zu mismatches %ld\n",
                    track, secs, oldBytes / 1024, srcBytes / 1024, pulls, samples, mismatches);
        total += mismatches;
    }
    std::printf("mismatches %ld\n", total);
    return total == 0 ? 0 : 3;
}

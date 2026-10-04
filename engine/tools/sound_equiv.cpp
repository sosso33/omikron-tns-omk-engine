// SPDX-License-Identifier: GPL-3.0-or-later
// A SOUND KEPT AS ITS FILE HOLDS IT plays the same floats (`verify.py:
// engine: device sounds`, todo/ram-vs-original.md tier B). For every shipped
// `.wav` - the interface's and every one inside every `.SCX` - the compact
// `DeviceSound` against the float copy `wavToDevice` made until it: the same
// length and every sample bitwise equal; then each mixed through the host
// mixer both ways, looped past its end, and the mixes compared bitwise.
//
//     sound_equiv <gamedata>   -> one line a section, then the total
#include "app/playhelpers.h"
#include "audio/hostmix.h"
#include "platform/datafs.h"
#include "script/program.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kRate = 22050;   // the device rate (`kDeviceRate`)

struct Tally { long sounds = 0, samples = 0, bad = 0, mixBad = 0; std::size_t floatBytes = 0, keptBytes = 0; };

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

void compare(std::span<const std::byte> wav, Tally& t) {
    const std::vector<float> ref = omk::wavToDevice(wav, kRate);
    const auto snd = omk::wavToDeviceSound(wav, kRate);
    const std::size_t n = snd ? snd->size : 0;
    ++t.sounds;
    if (n != ref.size()) { ++t.bad; return; }
    if (!n) return;
    t.samples += static_cast<long>(n);
    t.floatBytes += ref.size() * sizeof(float);
    t.keptBytes += snd->pcm.size() * sizeof(std::int16_t);
    for (std::size_t i = 0; i < n; ++i)
        if (!sameBits(snd->at(i), ref[i])) { ++t.bad; return; }
    // the mixer, both ways: looping, at a gain, past one wrap
    omk::HostMixer a, b;
    a.play(std::make_shared<const std::vector<float>>(ref), true, 0.7f);
    b.play(snd, true, 0.7f);
    std::vector<float> oa(n + n / 2 + 7), ob(oa.size());
    a.mix(oa.data(), oa.size());
    b.mix(ob.data(), ob.size());
    for (std::size_t i = 0; i < oa.size(); ++i)
        if (!sameBits(oa[i], ob[i])) { ++t.mixBad; break; }
}

void report(const char* what, const Tally& t) {
    std::printf("%s: %ld sounds, %ld samples, %ld differ, %ld mixes differ; "
                "%zu KB as floats, %zu KB kept\n", what, t.sounds, t.samples, t.bad,
                t.mixBad, t.floatBytes / 1024, t.keptBytes / 1024);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: sound_equiv <gamedata>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    Tally ui, scx;
    for (const std::string& path : fs.list("I2D/sounds", "wav"))
        compare(omk::DataFs::readPath(path), ui);
    long files = 0;
    for (const std::string& path : fs.list("SCPTDATA", "SCX")) {
        const omk::ScxRuntime rt(omk::DataFs::readPath(path));
        if (!rt.valid()) continue;
        ++files;
        for (int i = 0; i < rt.wavCount(); ++i) compare(rt.wavData(i), scx);
    }
    report("interface .wav", ui);
    std::printf("scenes: %ld .SCX files\n", files);
    report("scene sounds", scx);
    const long total = ui.bad + ui.mixBad + scx.bad + scx.mixBad;
    std::printf("total differing %ld\n", total);
    return total ? 1 : 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
// THE PERFORMANCE SWEEP (`--bench F[:SETTLE:MEASURE]`) - an instrument for a
// console the reader runs and Claude cannot (`todo/switch-port.md` S5/S6, the
// reader's "a measuring program that takes the readings by itself",
// 2026-11-26). At the place the run starts in, once its load is done:
//
//   1. THE FILE I/O, probed: the pack's folder (thousands of files in one
//      directory), a small one beside it and the game's set folder - the
//      listing, an open-and-close a file, a whole file read (MB/s) - so a
//      slow place change says whether it is the opens, the reads or neither;
//   2. THE SETTINGS, in turn: the original mode, the remaster as set (its
//      first frame is the pack's load), each remaster strength off, every
//      one off (the pack alone), each enhancement down, the upscale's
//      factors, the WEATHER where the place has one (the shower held at its
//      full 200 drops, with the remaster, its 3D rain or its wet floors off,
//      and in the original - the reader's "measure the weather's impact
//      too", 2026-11-26; every other step holds it away), the original with
//      the enhancements off, the remaster again.
//      Each step starts from the run's own settings, applied LIVE as the
//      options menu applies them (`omkApplyLive`, the remaster's
//      strengths), SETTLE frames left to settle, MEASURE frames measured:
//      the work a frame (the pacer's own, the SLOW FRAME line's) - median,
//      90th percentile, max - and the sections it went to.
//
// The report goes to F and to the log (`bench:` lines, `bench.*` events);
// then the run ends. Nothing is written to the settings or the save.
#include "playstate.h"

#include "platform/datafs.h"
#include "platform/env.h"
#include "platform/events.h"
#include "platform/hostfs.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

double clockMs() {
    using C = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(C::now().time_since_epoch()).count();
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t k = std::min(v.size() - 1, static_cast<std::size_t>(p * static_cast<double>(v.size() - 1) + 0.5));
    return v[k];
}

// THE WEATHER'S PARTS, each turned off for a step of its own under the held
// weather by its instrument (`envOverride`): which of them the frame pays for
// (the Switch's Anekbah, 2026-11-26: raining 40.2 ms, the 3D rain off 23.1)
struct WeatherPart { const char* label; const char* env; unsigned kinds; };   // kinds: 2 rain, 4 snow, 8 sand
constexpr WeatherPart kWeatherParts[] = {
    {"its strips (the drops, flakes or grains)", "OMK_NO_RAIN_STRIPS", 2u | 4u | 8u},
    {"its splashes", "OMK_NO_SPLASHES", 2u},
    {"its veils", "OMK_NO_VEIL", 2u},
    {"its halos", "OMK_NO_HALO", 2u},
    {"the lights' streaks", "OMK_NO_STREAKS", 2u},
    {"the wet bodies", "OMK_NO_WET_BODIES", 2u},
    {"the puddles", "OMK_NO_PUDDLES", 2u},
    {"the drips", "OMK_NO_DRIPS", 2u},
    {"the dust", "OMK_NO_DUST", 8u},
};

// the strengths a step turns off, each where the run has it on
constexpr const char* kStrengths[] = {"bloom", "water", "floors", "wet", "rain3d", "mist",
                                      "relief", "ao", "bodylight", "fxlight"};

}  // namespace

struct PlayState::BenchRun {
    struct Step {
        std::string name;
        std::function<bool()> apply;   // false: not this run's (skipped, said)
    };
    std::FILE* out = nullptr;
    std::vector<Step> steps;
    std::size_t at = 0;
    int left = 0;            // frames left in this step's stage
    bool measuring = false;
    bool first = false;      // the step's first frame: the switch's own cost
    // the switch: the apply's own time (a texture pool put back on the GPU
    // runs inside it, outside any frame the pacer measures) and the frame after
    double applyMs = 0.0, switchMs = 0.0, settleMax = 0.0, settleSum = 0.0;
    std::vector<double> work;
    std::vector<double> gpu;   // the renderer's GPU time a frame, where it has a timer (`gpuFrameMs`)
    std::map<std::string, double> sections;
    // the run's own settings, every step's start
    std::map<std::string, float> strengths;
    bool switchable = false;
    bool weather = false;    // this place rains: its view bits, option row 7 at 2
    int drops = 0;           // the most drops a measured frame had - the rain said, not assumed
    bool raining = false;    // a rain step: the shower held again every frame (a roof's ray ends it)
    std::map<std::string, std::string> envWas;   // the parts' switches as the run had them ("" unset... and set)
    std::map<std::string, bool> envSet;
    int lighting = 0, shadow = 0, aniso = 0, filter = 0;
    bool unlimited = false;
    float upscale = 1.0f;

    void say(const char* fmt, ...) {
        char b[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(b, sizeof b, fmt, ap);
        va_end(ap);
        std::printf("bench: %s\n", b);
        if (out) { std::fprintf(out, "%s\n", b); std::fflush(out); }
    }
};

// ONE FOLDER'S I/O: the listing, opens spread over it (twice - a cache
// shows as a faster second pass), whole files read
void PlayState::benchProbeDir(const std::string& dir, const char* what) {
    auto& b = *bench_;
    if (dir.empty() || !omk::hostfs::isDirectory(dir)) { b.say("io %s: %s - not there", what, dir.c_str()); return; }
    double t = clockMs();
    std::vector<std::string> names = omk::hostfs::listNames(dir);
    const double listMs = clockMs() - t;
    std::vector<std::string> pick;
    const std::size_t want = std::min<std::size_t>(40, names.size());
    for (std::size_t k = 0; k < want; ++k) pick.push_back(dir + "/" + names[k * names.size() / std::max<std::size_t>(want, 1)]);
    double opens[2] = {0, 0};
    int opened = 0;
    for (int pass = 0; pass < 2; ++pass) {
        t = clockMs();
        for (const auto& p : pick) {
            long h = -1;
            void* f = nullptr;
            if (omk::hostfs::openRead(p, h, f)) { omk::hostfs::closeFile(h, f); opened += pass == 0 ? 1 : 0; }
        }
        opens[pass] = pick.empty() ? 0.0 : (clockMs() - t) / static_cast<double>(pick.size());
    }
    // whole files: the last ten of the pick (opened above, so the reads are
    // the reads), their bytes over their time
    std::size_t bytes = 0;
    int files = 0;
    t = clockMs();
    for (std::size_t k = pick.size() > 10 ? pick.size() - 10 : 0; k < pick.size(); ++k) {
        bytes += omk::readWholeFile(pick[k]).size();
        ++files;
    }
    const double readMs = clockMs() - t;
    const double mbs = readMs > 0.0 ? static_cast<double>(bytes) / 1048576.0 / (readMs / 1000.0) : 0.0;
    b.say("io %s: %s - %zu names listed in %.1f ms; open+close %.2f ms a file (%d of %zu), again %.2f; "
          "%d whole files, %.0f KB, %.1f ms a file, %.1f MB/s",
          what, dir.c_str(), names.size(), listMs, opens[0], opened, pick.size(), opens[1], files,
          static_cast<double>(bytes) / 1024.0, files ? readMs / files : 0.0, mbs);
    omk::event("bench.io", {{"what", what}, {"names", static_cast<long>(names.size())}, {"list_ms", listMs},
                            {"open_ms", opens[0]}, {"open2_ms", opens[1]}, {"read_ms", files ? readMs / files : 0.0},
                            {"mbs", mbs}});
}

// the run's own settings put back: every step starts from them
void PlayState::benchRestore() {
    auto& b = *bench_;
    for (const auto& [k, v] : b.strengths)
        if (float* p = remasterStrength(k)) *p = v;
    if (lighting != b.lighting) omkApplyLive("enhancements.lighting", b.lighting);
    if (shadowQuality != b.shadow) omkApplyLive("enhancements.shadowquality", b.shadow);
    if (unlimitedClip != b.unlimited) omkApplyLive("enhancements.clipdistance", b.unlimited ? 1 : 0);
    if (texAniso != b.aniso) omkApplyLive("enhancements.anisotropy", b.aniso);
    if (texFilter != b.filter) omkApplyLive("enhancements.texturefiltering", b.filter);
    if (std::max(upscale, 1.0f) != b.upscale) omkApplyLive("remaster.upscale", b.upscale);
    if (b.switchable && !remasterShown_) remasterShow(true, "the bench");
    b.raining = false;
    if (b.weather) weather.holdForBench(false, static_cast<long>(weatherClockMs));
    for (const auto& [name, was] : b.envWas) omk::envOverride(name.c_str(), b.envSet[name] ? was.c_str() : nullptr);
}

void PlayState::benchSetup() {
    bench_ = std::make_shared<BenchRun>();
    auto& b = *bench_;
    b.out = std::fopen(opt.benchPath.c_str(), "w");
    b.say("the performance sweep - area %d (%s), %dx%d, %d frames settled and %d measured a step, "
          "the pacer at %d fps%s", session_ ? session_->activeArea() : -1, worldSet.c_str(), dispW, dispH,
          opt.benchSettle, opt.benchMeasure, frameRate, b.out ? "" : " (the report file could not be opened)");
    // 1. THE FILE I/O
    const std::string remDir = !opt.remasterFlag.empty() ? opt.remasterFlag : settings.remasterDir;
    const std::string packDir = !settings.texturePack.empty() ? settings.texturePack
                              : !remDir.empty() ? remDir + "/pack" : std::string();
    benchProbeDir(packDir, "the texture pack");
    benchProbeDir(remDir.empty() ? std::string() : remDir + "/grades", "a small folder");
    benchProbeDir(dataRoot + "/MESHES/DECORS", "the game's sets");
    // 2. THE SETTINGS - the run's own first
    // the strengths the remaster RUNS with - the start's (`remasterArmed`)
    // while it is hidden, where its own read 0 (`[Remaster] mode = original`:
    // the reader's first console sweep skipped every one of them)
    for (const char* k : kStrengths)
        if (const float* p = remasterStrength(k)) b.strengths[k] = remasterShown_ ? *p : remasterArmed(k);
    b.switchable = remasterSwitchable();
    b.weather = weatherBitsTold != 0 && weatherBitsTold != 0xFFFFu && shadowDetail == 2;
    b.lighting = lighting; b.shadow = shadowQuality; b.unlimited = unlimitedClip;
    b.aniso = texAniso; b.filter = texFilter; b.upscale = std::max(upscale, 1.0f);
    std::string had;
    for (const auto& [k, v] : b.strengths) if (v > 0.0f) had += " " + k + " " + std::to_string(v).substr(0, 4);
    b.say("the run's settings: remaster %s;%s; lighting %d, shadows %d, unlimited distance %d, anisotropy %d, "
          "filter %d, upscale %.2f, aa %d; weather %s (view bits 0x%x, detail %d)",
          b.switchable ? "armed" : "not armed", had.c_str(), lighting, shadowQuality, unlimitedClip ? 1 : 0,
          texAniso, texFilter, b.upscale, aaSamples, b.weather ? "here" : "none", weatherBitsTold, shadowDetail);
    const auto add = [&](std::string name, std::function<bool()> fn) { b.steps.push_back({std::move(name), std::move(fn)}); };
    add("original", [this] { return remasterShow(false, "the bench") || !bench_->switchable; });
    add("remaster", [this] { return bench_->switchable; });
    for (const auto& [k, v] : b.strengths) {
        if (v <= 0.0f) continue;
        const std::string key = k;
        add("remaster, " + key + " off", [this, key] { *remasterStrength(key) = 0.0f; return true; });
    }
    add("remaster, every effect off (the pack alone)", [this] {
        bool any = false;
        for (const auto& [k, v] : bench_->strengths)
            if (v > 0.0f) { *remasterStrength(k) = 0.0f; any = true; }
        return any;
    });
    if (lighting > 0) add("lighting per vertex", [this] { return omkApplyLive("enhancements.lighting", 0); });
    if (shadowQuality >= 2) add("shadows not mapped", [this] { return omkApplyLive("enhancements.shadowquality", 1); });
    if (unlimitedClip) add("draw distance the options'", [this] { return omkApplyLive("enhancements.clipdistance", 0); });
    if (texAniso > 1) add("anisotropy off", [this] { return omkApplyLive("enhancements.anisotropy", 1); });
    if (texFilter >= 2) add("bilinear, not trilinear", [this] { return omkApplyLive("enhancements.texturefiltering", 1); });
    for (const float u : {1.0f, 1.5f, 2.0f}) {
        if (!b.switchable || u == b.upscale) continue;
        char n[32];
        std::snprintf(n, sizeof n, "remaster, upscale %.1f", static_cast<double>(u));
        add(n, [this, u] { return omkApplyLive("remaster.upscale", u); });
    }
    add("original, enhancements off", [this] {
        remasterShow(false, "the bench");
        if (lighting > 0) omkApplyLive("enhancements.lighting", 0);
        if (shadowQuality >= 2) omkApplyLive("enhancements.shadowquality", 1);
        if (unlimitedClip) omkApplyLive("enhancements.clipdistance", 0);
        if (texAniso > 1) omkApplyLive("enhancements.anisotropy", 1);
        if (texFilter >= 2) omkApplyLive("enhancements.texturefiltering", 1);
        return true;
    });
    add("remaster, again", [this] { return bench_->switchable; });
    if (b.weather) {
        // THE KIND, by the view's bits (`weatherBitsOfAreaType`): 2 the
        // rain (Anekbah, the Impasse, Qalisar and Lahoreh restored), 4 the
        // snow (Mahaleel, 145 - it never ends), 8 the sand (Jaunpur and the
        // docks, restored)
        const std::string kind = (weatherBitsTold & 4u) ? "snow" : (weatherBitsTold & 8u) ? "sand" : "rain";
        // LAST: the floors stay wet and dry after a shower, which would
        // weigh on any step after it
        const auto rain = [this] { bench_->raining = true; weather.holdForBench(true, static_cast<long>(weatherClockMs)); };
        add(kind, [this, rain] { rain(); return true; });
        for (const char* k : {"rain3d", "wet"}) {
            if (std::string(k) == "wet" && kind != "rain") continue;   // only the rain wets
            const std::string key = k;
            const auto at = b.strengths.find(key);
            if (at == b.strengths.end() || at->second <= 0.0f) continue;
            add(kind + ", " + key + " off", [this, rain, key] { rain(); *remasterStrength(key) = 0.0f; return true; });
        }
        // ...and each of its PARTS off, the remaster on
        const unsigned kindBit = kind == "snow" ? 4u : kind == "sand" ? 8u : 2u;
        for (const WeatherPart& part : kWeatherParts) {
            if (!(part.kinds & kindBit) || !b.switchable) continue;
            const char* was = omk::envStr(part.env);
            b.envSet[part.env] = was != nullptr;
            b.envWas[part.env] = was ? was : "";
            const std::string env = part.env;
            add(kind + ", " + part.label + " off", [this, rain, env] {
                rain();
                return omk::envOverride(env.c_str(), "1");
            });
        }
        if (b.switchable) add(kind + ", original", [this, rain] { rain(); return remasterShow(false, "the bench"); });
    }
    // `OMK_BENCH_STEPS=2,19,23`: only those steps (their numbers in the full
    // list) - a quick reading of a few. ...or by NAME, the names separated by
    // `|` (`OMK_BENCH_STEPS=original|original, enhancements off`): a number
    // moves with the run's own settings and the place's weather, a name not
    if (const char* only = omk::envStr("OMK_BENCH_STEPS"); only && *only) {
        const std::string spec = only;
        std::vector<BenchRun::Step> kept;
        if (spec.find('|') != std::string::npos || spec.find_first_not_of("0123456789,") != std::string::npos) {
            const std::string names = "|" + spec + "|";
            for (const auto& st : b.steps)
                if (names.find("|" + st.name + "|") != std::string::npos) kept.push_back(st);
        } else {
            const std::string list = "," + spec + ",";
            for (std::size_t k = 0; k < b.steps.size(); ++k)
                if (list.find("," + std::to_string(k + 1) + ",") != std::string::npos) kept.push_back(b.steps[k]);
        }
        b.steps = std::move(kept);
    }
    // `OMK_BENCH_VARY=NAME=v1,v2,...`: the steps again under each value of an
    // instrument's variable (`omk::envOverride`), in one launch
    if (const char* vary = omk::envStr("OMK_BENCH_VARY"); vary && std::strchr(vary, '=')) {
        const std::string spec = vary;
        const std::string name = spec.substr(0, spec.find('='));
        std::vector<std::string> values;
        std::string v;
        for (const char c : spec.substr(spec.find('=') + 1) + ",") {
            if (c != ',') { v += c; continue; }
            if (!v.empty()) values.push_back(v);
            v.clear();
        }
        std::vector<BenchRun::Step> varied;
        for (const auto& value : values)
            for (const auto& s : b.steps)
                varied.push_back({s.name + " [" + name + "=" + value + "]", [name, value, fn = s.apply] {
                                      omk::envOverride(name.c_str(), value.c_str());
                                      return fn();
                                  }});
        b.steps = std::move(varied);
    }
    b.say("%zu steps", b.steps.size());
}

// EACH FRAME, from the pacer: the work this frame took
void PlayState::benchFrame(double workMs) {
    if (benchDone_) return;
    if (!bench_) {
        // the place loaded and a second of it run, first - and under a forced
        // adventure start (`--area`), the PLAYER handed over a second ago: until
        // then the camera is the place's own, not the one the game is played
        // through (the reader's first console sweep, 2026-11-26: without a save
        // the player has no model, the handover never came, and Qalisar was
        // measured looking at a wall). Past a minute, it starts anyway, said.
        if (frameNo < 60 || !session_ || session_->loading()) return;
        const bool handedOver = playerReady && frameNo >= handoverFrame + 60;
        if (forceAdventure && !handedOver && frameNo < 1800) return;
        if (forceAdventure && !handedOver)
            std::printf("bench: NO PLAYER handed over in 1800 frames - measured from the place's own camera "
                        "(a forced start needs --save with a player record)\n");
        benchSetup();
        bench_->left = 0;
        bench_->measuring = true;   // so that the first step begins below
        bench_->at = static_cast<std::size_t>(-1);
    }
    auto& b = *bench_;
    if (b.raining) weather.holdForBench(true, static_cast<long>(weatherClockMs));
    if (b.at != static_cast<std::size_t>(-1) && b.at < b.steps.size()) {
        if (b.first) {
            b.switchMs = b.applyMs + workMs;
            b.first = false;
        } else if (!b.measuring) {
            b.settleMax = std::max(b.settleMax, workMs);
            b.settleSum += workMs;
        } else {
            b.work.push_back(workMs);
            b.drops = std::max(b.drops, weather.count());
            if (world_ && world_->gpuFrameMs() >= 0.0) b.gpu.push_back(world_->gpuFrameMs());
            for (std::size_t k = 1; k < phases.marks.size(); ++k)
                b.sections[phases.marks[k].first] += (phases.marks[k].second - phases.marks[k - 1].second) * 1000.0;
        }
    }
    if (--b.left > 0) return;
    if (b.at != static_cast<std::size_t>(-1) && b.at < b.steps.size() && !b.measuring) {
        b.measuring = true;
        b.left = opt.benchMeasure;
        return;
    }
    // the step measured: its line
    if (b.at != static_cast<std::size_t>(-1) && b.at < b.steps.size() && !b.work.empty()) {
        std::vector<std::pair<double, std::string>> secs;
        for (const auto& [n, ms] : b.sections) secs.emplace_back(ms / static_cast<double>(b.work.size()), n);
        std::sort(secs.begin(), secs.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
        std::string top;
        for (std::size_t k = 0; k < secs.size() && k < 6; ++k) {
            char s[96];
            std::snprintf(s, sizeof s, "%s %.1f, ", secs[k].second.c_str(), secs[k].first);
            top += s;
        }
        const double med = percentile(b.work, 0.5), p90 = percentile(b.work, 0.9), mx = percentile(b.work, 1.0);
        char gpuText[48] = "";
        if (!b.gpu.empty())
            std::snprintf(gpuText, sizeof gpuText, "; GPU %.1f ms (p90 %.1f)", percentile(b.gpu, 0.5), percentile(b.gpu, 0.9));
        b.say("step %zu \"%s\": work median %.1f ms (%.0f fps), p90 %.1f, max %.1f%s; its switch %.0f ms, "
              "settling %.0f ms in all (max %.0f); weather drops %d; sections (ms a frame): %s",
              b.at + 1, b.steps[b.at].name.c_str(), med, med > 0.0 ? 1000.0 / med : 0.0, p90, mx, gpuText, b.switchMs,
              b.settleSum, b.settleMax, b.drops, top.c_str());
        omk::event("bench.step", {{"step", b.steps[b.at].name}, {"median", med}, {"p90", p90}, {"max", mx},
                                  {"switch", b.switchMs}, {"settle", b.settleSum}, {"drops", b.drops}});
        // `OMK_BENCH_SHOTS=<dir>`: each step's last frame, raw LE RGB565
        // (`step-NN-WxH.bin`), to lay two steps' pictures side by side
        if (const char* dir = omk::envStr("OMK_BENCH_SHOTS"); dir && *dir && world_) {
            const omk::Surface& s = world_->readback();
            char name[64];
            std::snprintf(name, sizeof name, "/step-%02zu-%dx%d.bin", b.at + 1, s.w, s.h);
            const std::string path = std::string(dir) + name;
            if (s.w > 0 && omk::safeOutputPath(path)) {
                std::ofstream o(path, std::ios::binary);
                for (const auto v : s.px) {
                    const char b2[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
                    o.write(b2, 2);
                }
            }
        }
    }
    // the next step that applies
    for (;;) {
        b.at = b.at == static_cast<std::size_t>(-1) ? 0 : b.at + 1;
        if (b.at >= b.steps.size()) {
            b.say("done");
            omk::event("bench.done", {{"steps", static_cast<long>(b.steps.size())}});
            if (b.out) std::fclose(b.out);
            b.out = nullptr;
            benchDone_ = true;
            return;
        }
        const double t0 = clockMs();
        benchRestore();
        const bool applies = b.steps[b.at].apply();
        b.applyMs = clockMs() - t0;
        if (applies) break;
        b.say("step %zu \"%s\": not this run's - skipped", b.at + 1, b.steps[b.at].name.c_str());
    }
    b.work.clear();
    b.gpu.clear();
    b.sections.clear();
    b.drops = 0;
    b.switchMs = b.settleMax = b.settleSum = 0.0;
    b.first = true;
    b.measuring = false;
    b.left = opt.benchSettle + 1;
}

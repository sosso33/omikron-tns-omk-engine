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
//      factors, the original with the enhancements off, the remaster again.
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
#include "platform/events.h"
#include "platform/hostfs.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <functional>
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
    std::map<std::string, double> sections;
    // the run's own settings, every step's start
    std::map<std::string, float> strengths;
    bool switchable = false;
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
    for (const char* k : kStrengths)
        if (const float* p = remasterStrength(k)) b.strengths[k] = *p;
    b.switchable = remasterSwitchable();
    b.lighting = lighting; b.shadow = shadowQuality; b.unlimited = unlimitedClip;
    b.aniso = texAniso; b.filter = texFilter; b.upscale = std::max(upscale, 1.0f);
    std::string had;
    for (const auto& [k, v] : b.strengths) if (v > 0.0f) had += " " + k + " " + std::to_string(v).substr(0, 4);
    b.say("the run's settings: remaster %s;%s; lighting %d, shadows %d, unlimited distance %d, anisotropy %d, "
          "filter %d, upscale %.2f, aa %d", b.switchable ? "armed" : "not armed", had.c_str(), lighting,
          shadowQuality, unlimitedClip ? 1 : 0, texAniso, texFilter, b.upscale, aaSamples);
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
    b.say("%zu steps", b.steps.size());
}

// EACH FRAME, from the pacer: the work this frame took
void PlayState::benchFrame(double workMs) {
    if (benchDone_) return;
    if (!bench_) {
        // the place loaded and a second of it run, first
        if (frameNo < 60 || !session_ || session_->loading()) return;
        benchSetup();
        bench_->left = 0;
        bench_->measuring = true;   // so that the first step begins below
        bench_->at = static_cast<std::size_t>(-1);
    }
    auto& b = *bench_;
    if (b.at != static_cast<std::size_t>(-1) && b.at < b.steps.size()) {
        if (b.first) {
            b.switchMs = b.applyMs + workMs;
            b.first = false;
        } else if (!b.measuring) {
            b.settleMax = std::max(b.settleMax, workMs);
            b.settleSum += workMs;
        } else {
            b.work.push_back(workMs);
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
        b.say("step %zu \"%s\": work median %.1f ms (%.0f fps), p90 %.1f, max %.1f; its switch %.0f ms, "
              "settling %.0f ms in all (max %.0f); sections (ms a frame): %s",
              b.at + 1, b.steps[b.at].name.c_str(), med, med > 0.0 ? 1000.0 / med : 0.0, p90, mx, b.switchMs,
              b.settleSum, b.settleMax, top.c_str());
        omk::event("bench.step", {{"step", b.steps[b.at].name}, {"median", med}, {"p90", p90}, {"max", mx},
                                  {"switch", b.switchMs}, {"settle", b.settleSum}});
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
    b.sections.clear();
    b.switchMs = b.settleMax = b.settleSum = 0.0;
    b.first = true;
    b.measuring = false;
    b.left = opt.benchSettle + 1;
}

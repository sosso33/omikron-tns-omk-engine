// SPDX-License-Identifier: GPL-3.0-or-later
// The SCX object interpreter, run over the corpus and over the alternation
// test - the differential against tools/sim/scene.py and c_script_programs.
//
//     run_programs <gamedata/SCPTDATA> <out.bin>
//
// Two halves, and they check different things.  The CORPUS half asserts the
// runtime fields really are authored repeat counts: run counter 0 on disk,
// repeat limit small or -1, object loop count only 1 or -1.  The RUN half
// ticks `Re14.SCX`'s `Telis_eat` 400 frames and asserts it alternates its two
// clips for ever - a program that mishandles the repeat limit, the loop count
// or the busy window collapses to one clip or simply ends.
#include "formats/scx.h"
#include "platform/datafs.h"
#include "script/program.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: run_programs <gamedata/SCPTDATA> <out.bin>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    auto files = fs.list(".", ".SCX");
    std::sort(files.begin(), files.end());

    long fns = 0, objs = 0;
    std::map<std::int32_t, long> limits, loops;
    std::set<std::int32_t> runCounts;
    for (const auto& p : files) {
        const auto sc = omk::readScx(omk::DataFs::readPath(p));
        for (const auto& o : sc.objects) {
            ++objs;
            ++loops[o.loop];
            for (const auto& f : o.functions) {
                ++fns;
                ++limits[f.repeat];
                runCounts.insert(f.runs);
            }
        }
    }
    long badLimits = 0;
    for (const auto& [lim, n] : limits)
        if (lim != -1 && !(lim >= 1 && lim <= 32)) badLimits += n;

    // --- the alternation test ---------------------------------------------
    const omk::ScxRuntime rt(omk::DataFs::readPath(
        fs.resolve("Re14.SCX").value_or(std::string(argv[1]) + "/Re14.SCX")));
    int distinct = 0, alternates = 1, stillRunning = 0, restarts = 0;
    int traceLen = 0;
    std::string names;
    const auto* obj = rt.byName("Telis_eat");
    std::int32_t loop = 0;
    if (obj) {
        loop = obj->loop;
        omk::Program pr(rt, *obj);
        for (int i = 0; i < 400; ++i) pr.tick(1.0f);
        stillRunning = pr.running() ? 1 : 0;
        restarts = pr.restarts();
        const auto& t = pr.animTrace();
        const std::set<std::string> uniq(t.begin(), t.end());
        distinct = static_cast<int>(uniq.size());
        traceLen = static_cast<int>(t.size());
        for (const auto& n : uniq) names += (names.empty() ? "" : ",") + n;
        for (std::size_t i = 0; i + 1 < t.size(); ++i)
            if (t[i] == t[i + 1]) alternates = 0;
        if (t.size() < 2) alternates = 0;
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    put32(static_cast<std::int32_t>(fns));
    put32(static_cast<std::int32_t>(objs));
    put32(static_cast<std::int32_t>(runCounts.size()));
    put32(runCounts.empty() ? -1 : *runCounts.begin());
    put32(static_cast<std::int32_t>(limits[1]));
    put32(static_cast<std::int32_t>(limits[-1]));
    put32(static_cast<std::int32_t>(badLimits));
    put32(static_cast<std::int32_t>(loops.size()));
    put32(static_cast<std::int32_t>(loops[1]));
    put32(static_cast<std::int32_t>(loops[-1]));
    put32(loop); put32(distinct); put32(alternates);
    put32(stillRunning); put32(restarts); put32(traceLen);
    put32(static_cast<std::int32_t>(names.size()));
    for (char c : names) o.push_back(static_cast<std::uint8_t>(c));
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream f(argv[2], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));

    std::printf("%ld function records over %ld objects; run counters on disk: "
                "%zu distinct (%d); repeat limit 1 in %ld, -1 in %ld, outside "
                "1..32 or -1 in %ld; loop counts: %zu distinct, 1 in %ld, -1 "
                "in %ld\n",
                fns, objs, runCounts.size(),
                runCounts.empty() ? -1 : *runCounts.begin(),
                limits[1], limits[-1], badLimits, loops.size(), loops[1],
                loops[-1]);
    std::printf("Telis_eat: loop %d, %d distinct clips, alternating: %s, "
                "still running after 400 frames: %s, %d restarts, %d clips "
                "begun (%s)\n",
                loop, distinct, alternates ? "yes" : "NO",
                stillRunning ? "yes" : "NO", restarts, traceLen, names.c_str());
    return 0;
}

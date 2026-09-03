// SPDX-License-Identifier: GPL-3.0-or-later
// The shoot AI's four callbacks, run - and measured against the shipped data.
//
//     run_shoot_ai <gamedata> <tables/shoot_ai.json> <out.bin> <tables/vm_opcodes.json>
//
// **The standard here is lower than the actor state machine's, and the sweep
// is written so the difference is visible in the numbers rather than only in
// the prose.** Three things are genuinely checkable and are checked:
//
//   1. **the dispatch is TOTAL over the shipped data.** Every one of the 1032
//      character records in `IAM\AREA` and `IAM\SCENE` has a type at `+176`,
//      and every type - including the 330 that ship as 0xFFFFFFFF - must
//      select one of the four arms. `default:` is not defensive here, it is
//      the arm a third of the corpus lands on;
//   2. **which arm the game actually runs.** Every `shoot.actor.enter` (op 82)
//      operand is resolved against its own chunk's actor table and pushed
//      through the dispatch. The answer is lopsided and is the reason the
//      generic shooter is the one worth the most care - and it says that
//      **`nullsub_9` is unreachable**: no shipped record carries type 7;
//   3. **Gandhar's machine is table-driven, so running it IS running the
//      shipped script.** The sweep walks all three behaviour scripts to
//      completion and asserts the sequence of actions is exactly the table's,
//      that every action code resolves to a handler row, and that a change of
//      health band restarts the walk rather than resuming it.
//
// What is NOT checked, because nothing here can: whether Astaroth's or the
// generic shooter's state graphs are complete or their constants right. Those
// are transcriptions of code with no data behind them and no oracle over them.
// The sweep asserts only that the runtime stays inside the state sets that
// were read, which catches a port that wanders and nothing else.
#include "actor/shoot.h"
#include "formats/iam.h"
#include "platform/datafs.h"
#include "platform/json.h"
#include "script/script.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

std::uint32_t rd32(const std::vector<std::byte>& b, std::size_t o) {
    if (o + 4 > b.size()) return 0;
    return static_cast<std::uint32_t>(b[o]) |
           static_cast<std::uint32_t>(b[o + 1]) << 8 |
           static_cast<std::uint32_t>(b[o + 2]) << 16 |
           static_cast<std::uint32_t>(b[o + 3]) << 24;
}
std::int16_t rd16(const std::vector<std::byte>& b, std::size_t o) {
    if (o + 2 > b.size()) return 0;
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(b[o]) |
           (static_cast<std::uint16_t>(b[o + 1]) << 8));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: run_shoot_ai <gamedata> <tables/shoot_ai.json> "
                             "<out.bin> <tables/vm_opcodes.json>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);

    // ---- the lifted tables -------------------------------------------------
    const auto doc = omk::Json::parseFile(argv[2]);
    const auto& rows = doc["rows"];

    omk::ShootAi::Tables T;
    long typeNameAgree = 0;
    {
        const auto& ct = rows["character_types"];
        for (std::size_t i = 0; i < ct.size(); ++i) {
            const int v = static_cast<int>(ct[i]["value"].i64());
            if (ct[i]["name"].str() == omk::charTypeName(v)) ++typeNameAgree;
        }
        const auto& acts = rows["actions"];
        for (std::size_t i = 0; i < acts.size(); ++i) {
            omk::ShootAction r;
            r.code = static_cast<int>(acts[i]["code"].i64());
            r.row  = static_cast<int>(acts[i]["row"].i64());
            const auto& ctp = acts[i]["clip_type"];
            r.clipType = ctp.isNull() ? -1 : static_cast<int>(ctp.i64());
            r.setsFlag = acts[i]["sets_flag_0x800"].boolean();
            T.actions.push_back(r);
        }
        const auto& sc = rows["behaviour_scripts"];
        for (std::size_t i = 0; i < sc.size(); ++i) {
            const auto nm = sc[i]["name"].str();
            auto* into = nm == "healthy"  ? &T.healthy
                       : nm == "wounded"  ? &T.wounded
                       : nm == "critical" ? &T.critical : nullptr;
            if (!into) continue;
            const auto& es = sc[i]["entries"];
            for (std::size_t k = 0; k < es.size(); ++k) {
                omk::ShootScriptStep st;
                st.action  = static_cast<int>(es[k]["action"].i64());
                st.repeats = static_cast<int>(es[k]["repeats"].i64());
                st.rewind  = es[k]["end"].boolean();
                into->push_back(st);
            }
        }
    }

    // ---- 1. the dispatch, over every shipped character record -------------
    long records = 0, dispatched = 0, unsetType = 0;
    long perType[16] = {};
    long perBrain[4] = {};
    // and, the number that actually decides how much each arm matters: which
    // brain every `shoot.actor.enter` (op 82) in the world scripts selects,
    // resolved against its OWN chunk's actor table. A character id is
    // chunk-local - `Actor_FindById` scans AREA +56 / SCENE +24 for the id at
    // +272 - so resolving one anywhere else would be counting collisions, the
    // trap CLAUDE.md records for `scx.play`.
    long enterSites = 0, enterResolved = 0;
    long enterBrain[4] = {};
    {
        const auto table = omk::OpcodeTable::loadJson(std::string(argv[4]));
        for (const char* arch : {"AREA", "SCENE"}) {
            const auto raw = fs.read(std::string("IAM/") + arch);
            const auto ar = omk::IamArchive::open(raw);
            const auto kind = std::strcmp(arch, "AREA") == 0 ? omk::ChunkKind::Area
                                                            : omk::ChunkKind::Scene;
            const std::size_t pAt = kind == omk::ChunkKind::Area ? 56 : 24;
            const std::size_t cAt = kind == omk::ChunkKind::Area ? 80 : 48;
            for (std::size_t ci = 0; ci < ar.size(); ++ci) {
                const auto span = ar.chunk(ci);
                if (span.empty()) continue;
                std::vector<std::byte> b(span.begin(), span.end());
                if (b.size() < cAt + 2) continue;
                const auto lo = static_cast<std::int32_t>(rd32(b, pAt));
                const int n = rd16(b, cAt);
                std::map<int, std::uint32_t> byId;
                if (n > 0 && lo > 0 &&
                    static_cast<std::size_t>(lo) + 276u * static_cast<std::size_t>(n) <= b.size()) {
                    for (int i = 0; i < n; ++i) {
                        const std::size_t o = static_cast<std::size_t>(lo)
                                            + 276u * static_cast<std::size_t>(i);
                        const auto type = rd32(b, o + 176);
                        ++records;
                        if (type == 0xFFFFFFFFu) ++unsetType;
                        else if (type < 16) ++perType[type];
                        ++dispatched;
                        ++perBrain[static_cast<int>(omk::shootBrainFor(type))];
                        byId[rd16(b, o + 272)] = type;
                    }
                }
                for (const auto& sl : omk::chunkSlots(span, kind)) {
                    const auto d = omk::decodeScript(span, sl.offset, span.size(), table);
                    if (d.status != omk::DecodeStatus::Ok) continue;
                    for (const auto& in : d.code) {
                        if (in.op != 82 || in.operand.size() < 2) continue;
                        ++enterSites;
                        const auto id = static_cast<std::int16_t>(
                            static_cast<std::uint16_t>(in.operand[0]) |
                            (static_cast<std::uint16_t>(in.operand[1]) << 8));
                        const auto it = byId.find(id);
                        if (it == byId.end()) continue;
                        ++enterResolved;
                        ++enterBrain[static_cast<int>(omk::shootBrainFor(it->second))];
                    }
                }
            }
        }
    }

    // ---- 3. Gandhar's script walk -----------------------------------------
    //
    // Walk each script twice round and assert the action sequence is exactly
    // the table's, expanded by its repeat counts. This is the one arm where
    // running the port and reading the data are the same act.
    long scriptSteps = 0, scriptWrong = 0, bandResets = 0;
    {
        // the health each script is REACHED at, not an arbitrary number:
        // > 100 healthy, <= 100 wounded, <= 50 critical. A first version drove
        // all three at 200, so all three ran the healthy script and 90 of 144
        // steps disagreed - the check catching the driver rather than the port,
        // which is the right way round.
        const std::vector<std::pair<const std::vector<omk::ShootScriptStep>*, int>> runs = {
            {&T.healthy, 200}, {&T.wounded, 100}, {&T.critical, 50}};
        for (const auto& [script, hp] : runs) {
            // the expected expansion, derived from the table not the runtime
            std::vector<int> want;
            for (int lap = 0; lap < 2; ++lap)
                for (const auto& st : *script) {
                    if (st.rewind) break;
                    for (int k = 0; k < st.repeats; ++k) want.push_back(st.action);
                }
            omk::ShootAi ai(T, 10);
            ai.rec().health = hp;
            std::vector<int> got;
            for (std::size_t k = 0; k < want.size(); ++k) {
                ai.signalActionComplete();
                ai.tick(1.0f);
                got.push_back(ai.rec().state);
            }
            for (std::size_t k = 0; k < want.size(); ++k) {
                ++scriptSteps;
                if (k >= got.size() || got[k] != want[k]) ++scriptWrong;
            }
        }
        // the band reset: hurt him mid-routine and he must restart, not resume
        omk::ShootAi ai(T, 10);
        ai.rec().health = 200;
        for (int k = 0; k < 5; ++k) { ai.signalActionComplete(); ai.tick(1.0f); }
        const int midStep = ai.rec().scriptStep;
        ai.rec().health = 40;                       // straight to critical
        ai.signalActionComplete(); ai.tick(1.0f);
        if (midStep > 0 && ai.rec().scriptStep == 0 && ai.rec().band == 2) ++bandResets;
    }

    // ---- the three arms stay inside the state sets that were read ---------
    long strayStates = 0, ticks = 0;
    for (std::uint32_t type : {7u, 10u, 13u, 3u, 0xFFFFFFFFu}) {
        omk::ShootAi ai(T, type);
        ai.rec().health = 200;
        const auto& set = (type == 13) ? omk::astarothStates() : omk::genericStates();
        for (int k = 0; k < 400; ++k) {
            ai.signalActionComplete();
            ai.tick(1.0f);
            ++ticks;
            const int st = ai.rec().state;
            if (type == 13 || (type != 7 && type != 10)) {
                if (std::find(set.begin(), set.end(), st) == set.end()) ++strayStates;
            } else if (type == 10) {
                if (!T.byCode(st)) ++strayStates;      // must be a real action
            } else if (st != 0) {
                ++strayStates;                          // nullsub_9 moves nothing
            }
        }
    }

    std::vector<std::uint8_t> o;
    const auto put = [&o](long v) {
        const auto u = static_cast<std::uint32_t>(static_cast<std::int32_t>(v));
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    for (long v : {typeNameAgree, static_cast<long>(T.actions.size()),
                   static_cast<long>(T.healthy.size()),
                   static_cast<long>(T.wounded.size()),
                   static_cast<long>(T.critical.size()),
                   records, dispatched, unsetType,
                   perBrain[static_cast<int>(omk::ShootBrain::Inert)],
                   perBrain[static_cast<int>(omk::ShootBrain::Gandhar)],
                   perBrain[static_cast<int>(omk::ShootBrain::Astaroth)],
                   perBrain[static_cast<int>(omk::ShootBrain::Generic)],
                   perType[7], perType[10], perType[13],
                   enterSites, enterResolved,
                   enterBrain[static_cast<int>(omk::ShootBrain::Inert)],
                   enterBrain[static_cast<int>(omk::ShootBrain::Gandhar)],
                   enterBrain[static_cast<int>(omk::ShootBrain::Astaroth)],
                   enterBrain[static_cast<int>(omk::ShootBrain::Generic)],
                   scriptSteps, scriptWrong, bandResets, ticks, strayStates})
        put(v);
    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream out(argv[3], std::ios::binary);
    out.write(reinterpret_cast<const char*>(o.data()),
              static_cast<std::streamsize>(o.size()));

    std::printf("tables: %ld of 14 type names agree with the lifted JSON; %zu "
                "actions; scripts %zu/%zu/%zu\n",
                typeNameAgree, T.actions.size(), T.healthy.size(),
                T.wounded.size(), T.critical.size());
    std::printf("dispatch: %ld character records, %ld dispatched, %ld with an "
                "UNSET type (-1) which reaches the generic arm through "
                "`default:`\n", records, dispatched, unsetType);
    std::printf("  by arm: inert %ld, Gandhar %ld, Astaroth %ld, generic %ld\n",
                perBrain[0], perBrain[1], perBrain[2], perBrain[3]);
    std::printf("  records of type 7 / 10 / 13: %ld / %ld / %ld  - type 7 is "
                "X-Tech, and nothing in the game is one\n",
                perType[7], perType[10], perType[13]);
    std::printf("shoot.actor.enter: %ld sites, %ld resolving in their own "
                "chunk; inert %ld, Gandhar %ld, Astaroth %ld, generic %ld\n",
                enterSites, enterResolved, enterBrain[0], enterBrain[1],
                enterBrain[2], enterBrain[3]);
    std::printf("Gandhar: %ld script steps walked, %ld disagreeing with the "
                "table, %ld band resets\n", scriptSteps, scriptWrong, bandResets);
    std::printf("arms: %ld ticks, %ld states outside the set that was read\n",
                ticks, strayStates);
    return 0;
}

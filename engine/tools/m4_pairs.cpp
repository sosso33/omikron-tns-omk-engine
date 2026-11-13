// SPDX-License-Identifier: GPL-3.0-or-later
// THE SHARED ACTOR IDS of two AREA tables (`todo/drift-audit.md` M4): for
// each pair of areas named, every actor id both place, with both records'
// positions and how far apart they stand - two records at one spot draw as
// one body in the engine too; apart, the engine draws two while both areas
// are resident and the port one.
//
//     m4_pairs <gamedata> <areaA> <areaB> [<areaA> <areaB> ...]
//     m4_pairs <gamedata> --census <vm_opcodes.json>
//     m4_pairs <gamedata> --zones <area>
//
// `--census`: the ids placed by more than one AREA table, and for each VM op
// that addresses a character by id, how many shipped sites (every script of
// every AREA and SCENE chunk) name one of them - which of the port's
// per-actor systems a second copy of an id can reach at all.
#include "formats/iam.h"
#include "formats/placements.h"
#include "platform/datafs.h"
#include "script/opcodes.h"
#include "script/script.h"
#include "script/world.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>

int census(const omk::DataFs& fs, const char* opcodes) {
    const auto table = omk::OpcodeTable::loadJson(opcodes);
    if (!table.valid()) return 1;
    const auto areaRaw = fs.read("IAM/AREA");
    const auto areas = omk::IamArchive::open(areaRaw);
    std::map<int, std::set<std::size_t>> placedBy;
    for (std::size_t a = 0; a < areas.size(); ++a)
        for (const auto& r : omk::readPlacements(areas.chunk(a), omk::ChunkKind::Area))
            placedBy[r.actor].insert(a);
    std::set<int> shared;
    for (const auto& [id, as] : placedBy) if (as.size() > 1) shared.insert(id);
    namespace op = omk::op;
    const std::map<int, const char*> addressing = {
        {op::CharacterShow, "78 character.show"}, {op::CharacterHide, "79 character.hide"},
        {op::PlayerBecome, "56 player.become"}, {op::ScxPlayActor, "59 scx.play.actor"},
        {op::ScxPlayActorWait, "60 scx.play.actor.wait"}, {op::FightBegin, "62 fight.begin"},
        {op::ObjectHoldActor, "67 object.hold.actor"}, {op::ObjectReleaseActor, "69 object.release.actor"},
        {op::ShootActorEnter, "82 shoot.actor.enter"}, {op::ShootActorAction, "84 shoot.actor.action"},
        {op::VarSetActorStat, "86 var.set.actor_stat"}, {op::ActorStatSet, "93 actor.stat.set"},
        {op::MorphPlay, "144 morph.play"}, {op::CharacterLookAtPlayer, "138 character.look_at_player"},
        {op::CharacterLookAway, "139 character.look_away"}};
    std::map<int, std::pair<int, int>> sites;   // op -> (all, naming a shared id)
    for (const char* arch : {"AREA", "SCENE"}) {
        const auto raw = fs.read(std::string("IAM/") + arch);
        const auto ar = omk::IamArchive::open(raw);
        const auto kind = std::strcmp(arch, "AREA") == 0 ? omk::ChunkKind::Area : omk::ChunkKind::Scene;
        for (std::size_t ci = 0; ci < ar.size(); ++ci) {
            const auto span = ar.chunk(ci);
            if (span.empty()) continue;
            for (const auto& sl : omk::chunkSlots(span, kind)) {
                const auto d = omk::decodeScript(span, sl.offset, span.size(), table);
                if (d.status != omk::DecodeStatus::Ok) continue;
                for (const auto& in : d.code) {
                    const auto it = addressing.find(in.op);
                    if (it == addressing.end() || in.operand.size() < 2) continue;
                    const auto id = static_cast<std::int16_t>(static_cast<std::uint16_t>(in.operand[0]) |
                                                              (static_cast<std::uint16_t>(in.operand[1]) << 8));
                    auto& c = sites[in.op];
                    ++c.first;
                    if (shared.count(id)) ++c.second;
                }
            }
        }
    }
    std::printf("census: %zu ids placed by more than one AREA table\n", shared.size());
    for (const auto& [o, name] : addressing) {
        const auto c = sites[o];
        std::printf("census: op %s - %d sites, %d naming a shared id\n", name, c.first, c.second);
    }
    return 0;
}

// `--zones <area>`: each zone's id, its three script offsets and its centre
int zones(const omk::DataFs& fs, int area) {
    const auto raw = fs.read("IAM/AREA");
    const auto ar = omk::IamArchive::open(raw);
    for (const auto& z : omk::zonesOf(ar.chunk(static_cast<std::size_t>(area)), omk::ChunkKind::Area))
        std::printf("zone %d: scripts %d %d %d at %.0f %.0f\n", z.stateBit(), z.scripts[0], z.scripts[1],
                    z.scripts[2], (z.quad[0][0] + z.quad[2][0]) / 2, (z.quad[0][2] + z.quad[2][2]) / 2);
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[2], "--census") == 0) return census(omk::DataFs(argv[1]), argv[3]);
    if (argc == 4 && std::strcmp(argv[2], "--zones") == 0) return zones(omk::DataFs(argv[1]), std::atoi(argv[3]));
    if (argc < 4 || (argc - 2) % 2 != 0) {
        std::fprintf(stderr, "usage: m4_pairs <gamedata> <areaA> <areaB> [...]\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    const auto raw = fs.read("IAM/AREA");
    const auto ar = omk::IamArchive::open(raw);
    const auto table = [&](int a) {
        if (a < 0 || static_cast<std::size_t>(a) >= ar.size()) return std::vector<omk::Placement>{};
        return omk::readPlacements(ar.chunk(static_cast<std::size_t>(a)), omk::ChunkKind::Area);
    };
    for (int k = 2; k + 1 < argc; k += 2) {
        const int a = std::atoi(argv[k]), b = std::atoi(argv[k + 1]);
        const auto ta = table(a), tb = table(b);
        int shared = 0, apart = 0;
        for (const auto& ra : ta)
            for (const auto& rb : tb) {
                if (ra.actor != rb.actor) continue;
                ++shared;
                const double d = std::hypot(ra.pos[0] - rb.pos[0], ra.pos[1] - rb.pos[1], ra.pos[2] - rb.pos[2]);
                if (d > 1.0) ++apart;
                std::printf("pair %d %d: actor %d - %d at %.0f %.0f %.0f (bit %d), %d at %.0f %.0f %.0f (bit %d), "
                            "%.0f apart\n", a, b, ra.actor, a, ra.pos[0], ra.pos[1], ra.pos[2], ra.bit, b,
                            rb.pos[0], rb.pos[1], rb.pos[2], rb.bit, d);
            }
        std::printf("pair %d %d: %d shared ids, %d of them at two spots\n", a, b, shared, apart);
    }
    return 0;
}

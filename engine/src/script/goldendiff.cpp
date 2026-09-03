// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/goldendiff.h"

#include "formats/iam.h"
#include "script/dialogue.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace omk {
namespace {

std::int16_t i16(std::span<const std::uint8_t> v, std::size_t o) {
    if (o + 2 > v.size()) return 0;
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(v[o]) |
                                     (static_cast<std::uint16_t>(v[o + 1]) << 8));
}

std::uint32_t u32b(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    std::uint32_t v = 0;
    for (int k = 3; k >= 0; --k)
        v = (v << 8) | static_cast<std::uint32_t>(d[o + static_cast<std::size_t>(k)]);
    return v;
}

}  // namespace

AnnounceMap AnnounceMap::loadJson(const std::string& path) {
    AnnounceMap m;
    std::ifstream f(path);
    if (!f) return m;
    std::stringstream ss; ss << f.rdbuf();
    const std::string s = ss.str();
    std::size_t i = 0;
    while ((i = s.find("\"op\":", i)) != std::string::npos) {
        i += 5;
        const int op = std::atoi(s.c_str() + i);
        const auto dp = s.find("\"domain\":", i);
        const auto fp = s.find("\"field\":", i);
        if (dp == std::string::npos || fp == std::string::npos) break;
        const auto q1 = s.find('"', dp + 9);
        const auto q2 = (q1 == std::string::npos) ? q1 : s.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        const std::string domain = s.substr(q1 + 1, q2 - q1 - 1);
        const int field = std::atoi(s.c_str() + fp + 8);
        if (op >= 0 && op < 256 && !domain.empty())
            m.rows_[static_cast<std::uint8_t>(op)] = Row{domain, field};
        i = fp;
    }
    return m;
}

std::optional<TagEvent> AnnounceMap::of(std::uint8_t op,
                                        std::span<const std::uint8_t> operand) const {
    const auto it = rows_.find(op);
    if (it == rows_.end()) return std::nullopt;
    // `Dbg_LogTagged`'s own filters, and they are the engine's, not
    // conveniences: it drops the CHARACTERS domain (resolved through
    // Actor_FindById and printed from the actor instead) and VALUES (a bare
    // number with no table). A diff that does not apply them reports the
    // logger's own filter as a disagreement.
    if (it->second.domain == "CHARACTERS" || it->second.domain == "VALUES")
        return std::nullopt;
    const auto f = static_cast<std::size_t>(it->second.field);
    if (2 * (f + 1) > operand.size()) return std::nullopt;
    const int v = i16(operand, 2 * f);
    if (v == -1) return std::nullopt;             // the logger's first test
    return TagEvent{it->second.domain, v};
}

std::vector<TagEvent> parseCapture(const std::string& path) {
    std::vector<TagEvent> out;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    // The relay line, matched without a regex because its shape is fixed:
    //   Call KERNEL32.GetPrivateProfileStringA(... "<section>","<key>",
    //        "<default>",buf,size,"<filename>")
    // Only a `.TAG` filename counts - other .ini reads in a Wine process are
    // not the engine's operand log.
    const std::string tag = "GetPrivateProfileStringA(";
    while (std::getline(f, line)) {
        const auto at = line.find(tag);
        if (at == std::string::npos) continue;
        std::vector<std::string> q;
        for (std::size_t i = at; i < line.size() && q.size() < 4; ++i) {
            if (line[i] != '"') continue;
            const auto e = line.find('"', i + 1);
            if (e == std::string::npos) break;
            q.push_back(line.substr(i + 1, e - i - 1));
            i = e;
        }
        if (q.size() < 4) continue;
        std::string fn = q[3];
        for (auto& c : fn) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (fn.size() < 4 || fn.compare(fn.size() - 4, 4, ".TAG") != 0) continue;
        out.push_back({q[0], std::atoi(q[1].c_str())});
    }
    return out;
}

std::vector<WorldSlot> worldSlots(std::span<const std::byte> areaFile,
                                  std::span<const std::byte> sceneFile,
                                  std::span<const std::byte> globalFile) {
    std::vector<WorldSlot> out;
    const char* names[2] = {"AREA", "SCENE"};
    std::span<const std::byte> files[2] = {areaFile, sceneFile};
    const ChunkKind kinds[2] = {ChunkKind::Area, ChunkKind::Scene};
    for (int a = 0; a < 2; ++a) {
        const auto ar = IamArchive::open(files[a]);
        for (std::size_t c = 0; c < ar.size(); ++c) {
            const auto b = ar.chunk(c);
            if (b.empty()) continue;
            char buf[96];
            // the chunk's own startup script at +4, which is in no record
            // table - leaving it out is what made "no shipped script starts
            // the Impasse's beats" look true. Reported as `rec -1 +4`.
            const auto at = u32b(b, 4);
            if (at > 0 && at < b.size()) {
                std::snprintf(buf, sizeof buf, "%s %zu rec -1 +4 @%u",
                              names[a], c, at);
                out.push_back({buf, b, at});
            }
            for (const auto& s : chunkSlots(b, kinds[a])) {
                std::snprintf(buf, sizeof buf, "%s %zu rec %d +%d @%zu",
                              names[a], c, s.record, s.field, s.offset);
                out.push_back({buf, b, s.offset});
            }
        }
    }
    for (const auto& s : globalSlots(globalFile)) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "GLOBAL 0 rec %d +%d @%zu",
                      s.record, s.field, s.offset);
        out.push_back({buf, globalFile, s.offset});
    }
    return out;
}

void appendDialogScripts(std::span<const std::byte> dialogFile,
                         std::vector<WorldSlot>& out) {
    const auto ar = IamArchive::open(dialogFile);
    for (std::size_t i = 0; i < ar.size(); ++i) {
        const auto b = ar.chunk(i);
        if (b.empty()) continue;
        const auto conv = parseConversation(static_cast<int>(i), b);
        for (std::size_t n = 0; n < conv.nodes.size(); ++n) {
            for (int k = 0; k < 8; ++k) {
                const auto at = conv.nodes[n].ptr[k];
                if (at == 0 || at >= b.size()) continue;
                char buf[96];
                std::snprintf(buf, sizeof buf, "DIALOG %zu node %zu %s%d",
                              i, n, k < 4 ? "cond" : "act", k % 4);
                out.push_back({buf, b, at, false});
            }
        }
    }
}

std::map<TagEvent, std::set<std::size_t>>
tightIndex(const std::vector<WorldSlot>& slots, const OpcodeTable& table,
           const AnnounceMap& ann) {
    std::map<TagEvent, std::set<std::size_t>> idx;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const auto d = decodeScript(slots[i].code, slots[i].at,
                                    slots[i].code.size(), table);
        for (const auto& in : d.code)
            if (const auto e = ann.of(in.op, in.operand)) idx[*e].insert(i);
    }
    return idx;
}

std::vector<TagEvent> replaySlot(const WorldSlot& s, GameState& state,
                                 const OpcodeTable& table,
                                 const AnnounceMap& ann) {
    Interpreter vm(state, table);
    vm.setRecordAll(true);
    // **The park stays ON here**, and the reference now models it too.
    //
    // A replay that runs past `ui.open` predicts announcements the engine
    // never made. That closes one of `resto-387.log`'s six disagreements:
    // `AREA 157 rec 60 +4` opens screen 4 - the LIFT - at instruction 3 of 37
    // and branches on variable 496, `Etage`, so without the park the replay
    // ran the remaining 33 instructions and predicted BOTH arms of the floor
    // switch. Six become five, and nothing else moves.
    //
    // This is what the second implementation was for: the port modelled the
    // park, disagreed with the reference, and the disagreement was the
    // reference's.
    const auto r = vm.run(s.code, s.at);
    std::vector<TagEvent> out;
    for (const auto& c : r.calls) {
        // rebuild the operand bytes the announce map indexes into
        std::vector<std::uint8_t> raw;
        raw.reserve(c.fields.size() * 2);
        for (auto f : c.fields) {
            const auto u = static_cast<std::uint16_t>(f);
            raw.push_back(static_cast<std::uint8_t>(u));
            raw.push_back(static_cast<std::uint8_t>(u >> 8));
        }
        if (const auto e = ann.of(c.op, raw)) out.push_back(*e);
    }
    return out;
}

DiffResult diffCapture(const std::string& capturePath,
                       const std::vector<WorldSlot>& slots,
                       const std::map<TagEvent, std::set<std::size_t>>& tight,
                       GameState state, const OpcodeTable& table,
                       const AnnounceMap& ann) {
    DiffResult r;
    const auto got = parseCapture(capturePath);
    r.events = static_cast<int>(got.size());
    if (got.empty()) return r;

    // An anchor is an event only ONE slot in the whole corpus could announce.
    // Consecutive events naming the same slot collapse to one anchor.
    std::vector<std::pair<std::size_t, std::size_t>> anchors;
    long last = -1;
    for (std::size_t i = 0; i < got.size(); ++i) {
        const auto it = tight.find(got[i]);
        if (it == tight.end() || it->second.size() != 1) continue;
        const auto slot = *it->second.begin();
        if (static_cast<long>(slot) == last) continue;
        anchors.push_back({i, slot});
        last = static_cast<long>(slot);
    }
    r.anchors = static_cast<int>(anchors.size());

    for (const auto& [i, si] : anchors) {
        // The state is carried FORWARD across the capture: each replayed
        // script writes into it in capture order, which tracks the playthrough
        // without needing a save. It is only as good as the attribution - a
        // script the capture never names never runs, so its writes are missing.
        if (!slots[si].replayable) { ++r.skipped; continue; }
        const auto pred = replaySlot(slots[si], state, table, ann);
        if (pred.empty()) { ++r.skipped; continue; }
        if (std::find(pred.begin(), pred.end(), got[i]) == pred.end()) {
            // The anchor came from a static decode, which walks every branch;
            // the replay takes one. So the anchoring event sits on a path this
            // state does not reach - a missing anchor, not a disagreement.
            ++r.unreached;
            continue;
        }
        // Concurrency: a script's emissions are NOT contiguous in the capture,
        // so what must hold is that the prediction appears as an ordered
        // subsequence with other scripts' events allowed in between.
        const std::size_t lo = i > 2 * pred.size() ? i - 2 * pred.size() : 0;
        const std::size_t hi = std::min(got.size(), i + 4 * pred.size() + 2);
        const bool truncated = hi >= got.size();
        std::size_t k = 0, w = lo;
        for (const auto& e : pred) {
            bool hit = false;
            while (w < hi) {
                if (got[w++] == e) { hit = true; break; }
            }
            if (!hit) break;
            ++k;
        }
        // A capture stopped mid-script leaves a PREFIX of the prediction, and
        // that is agreement: the events that exist all match, in order, and
        // the rest were simply never recorded.
        if (k == pred.size() || (truncated && k > 0)) ++r.ok;
        else { ++r.bad; r.mismatched.push_back(slots[si].name); }
    }
    return r;
}

}  // namespace omk

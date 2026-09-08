// SPDX-License-Identifier: GPL-3.0-or-later
// FIND A .CTL STATE BY NAME, with its group and its edges.
//
//     ctl_find <file.CTL> <substring>
//
// Prints every state whose name or move name contains the substring, then
// every state of each group those states live in - the graph a move sits in
// is what says how it is REACHED, and that is what a name alone cannot.
#include "formats/ctl.h"

#include <cstdio>
#include <fstream>
#include <set>
#include <string>
#include <vector>

static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: ctl_find <file.CTL> <substring>\n"); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), {});
    const auto f = omk::readCtl(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(raw.data()), raw.size()));
    const std::string key = lower(argv[2]);
    std::set<int> groups;
    // ...and any state whose GoTo lands on a matching state, because that is
    // the edge INTO the group and it lives elsewhere.
    std::set<std::uint32_t> targets;
    for (const auto& s : f.states)
        if (lower(s.name).find(key) != std::string::npos ||
            lower(s.moveName).find(key) != std::string::npos) {
            groups.insert(s.group);
            targets.insert(s.id);
        }
    for (const auto& s : f.states)
        if (s.gotoId && targets.count(s.gotoId)) groups.insert(s.group);
    auto nameOf = [&](std::uint32_t id) -> std::string {
        for (const auto& s : f.states) if (s.id == id) return s.name;
        return "#" + std::to_string(id);
    };
    for (int g : groups) {
        const auto& G = f.groupList[(std::size_t)g];
        std::printf("GROUP %d (index %d) flags %08x default %d\n", G.id, g, G.flags, G.defaultEntry);
        for (int i = G.first; i < G.first + G.count; ++i) {
            const auto& s = f.states[(std::size_t)i];
            std::printf("  [%d] id %u %-14s flags %08x input %08x role %u clip %d goto %s move %s\n",
                        i, s.id, s.name.c_str(), s.flags, s.inputCode, s.role, s.clip,
                        s.gotoId ? nameOf(s.gotoId).c_str() : "-",
                        s.moveName.empty() ? "-" : s.moveName.c_str());
            std::printf("       parents:");
            for (auto p : s.parents) std::printf(" %s", nameOf(p).c_str());
            std::printf("  children:");
            for (auto c : s.children) std::printf(" %s", nameOf(c).c_str());
            std::printf("\n");
        }
    }
    return 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "formats/ctl.h"

#include "formats/anim.h"

#include <cctype>
#include <cstring>
#include <map>
#include <set>

namespace omk {
namespace {

std::uint32_t u32(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    return static_cast<std::uint32_t>(d[o])       |
           static_cast<std::uint32_t>(d[o + 1]) <<  8 |
           static_cast<std::uint32_t>(d[o + 2]) << 16 |
           static_cast<std::uint32_t>(d[o + 3]) << 24;
}

float f32(std::span<const std::byte> d, std::size_t o) {
    const auto u = u32(d, o);
    float v;
    std::memcpy(&v, &u, 4);
    return v;
}

std::uint16_t u16(std::span<const std::byte> d, std::size_t o) {
    if (o + 2 > d.size()) return 0;
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(d[o]) |
                                      (static_cast<std::uint16_t>(d[o + 1]) << 8));
}

std::uint8_t u8(std::span<const std::byte> d, std::size_t o) {
    return o < d.size() ? static_cast<std::uint8_t>(d[o]) : 0;
}

std::string name12(std::span<const std::byte> d, std::size_t o, bool upper) {
    std::string s;
    for (std::size_t k = 0; k < 12 && o + k < d.size(); ++k) {
        const auto c = static_cast<char>(d[o + k]);
        if (c == '\0') break;
        s.push_back(upper ? static_cast<char>(std::toupper(
            static_cast<unsigned char>(c))) : c);
    }
    return s;
}

}  // namespace

CtlFile readCtl(std::span<const std::byte> d) {
    CtlFile f;
    f.size = d.size();
    if (d.size() < 92 || std::memcmp(d.data(), "CE70", 4) != 0) return f;

    const auto groupCount = u32(d, 12);
    f.groups = static_cast<int>(groupCount);

    // groups, then their entries laid out in order
    struct Group { std::size_t at; std::uint32_t n; std::size_t base; };
    std::vector<Group> groups;
    std::size_t off = 32u * groupCount + 88u;
    for (std::uint32_t i = 0; i < groupCount; ++i) {
        const auto g = 88u + 32u * i;
        const auto n = u32(d, g + 4);
        groups.push_back({g, n, off});
        off += 88u * n;
    }
    std::size_t v5 = off;

    std::vector<std::size_t> ents;
    std::vector<int> groupOf;
    for (std::size_t gi = 0; gi < groups.size(); ++gi)
        for (std::uint32_t k = 0; k < groups[gi].n; ++k) {
            ents.push_back(groups[gi].base + 88u * k);
            groupOf.push_back(static_cast<int>(gi));
        }

    // The nine sections, each a separate pass over every entry - the order is
    // the file's, and getting it wrong desynchronises everything after it.
    std::map<std::size_t, std::size_t> named, childOff, parentOff;
    for (auto e : ents)
        if (!(u32(d, e + 8) & 0x8002u)) { named[e] = v5; v5 += 12; }
    for (auto e : ents)
        if (u8(d, e + 87)) { childOff[e] = v5; v5 += 4u * u8(d, e + 87); }
    for (auto e : ents)
        if (u8(d, e + 86)) { parentOff[e] = v5; v5 += 4u * u8(d, e + 86); }
    // The turn, the root shift and the move name were stepped over here for
    // as long as nothing consumed them. The live channel does - `GoToMove`
    // applies a turn or a shift whole when it LEAVES a state carrying 0x100 /
    // 0x200, and queues the move name on the state it enters - so the walk
    // now records where each landed. The arithmetic is unchanged, which is
    // what keeps the walk landing exactly on the file size.
    std::map<std::size_t, std::size_t> turnOff, shiftOff, moveOff;
    for (auto e : ents)
        if (u32(d, e + 8) & 0x140u)     { turnOff[e]  = v5; v5 += 24; }
    for (auto e : ents)
        if (u32(d, e + 8) & 0x280u)     { shiftOff[e] = v5; v5 += 20; }
    for (auto e : ents)
        if (u8(d, e + 8) & 0x10u)       { moveOff[e]  = v5; v5 += 12; }
    // the combat block, whose OFFSET the walk used only to step over
    std::map<std::size_t, std::size_t> combatOff;
    for (auto e : ents)
        if (u32(d, e + 8) & 0x2000000u) { combatOff[e] = v5; v5 += 40; }

    // the fight AI: 156 bytes a profile, twelve situation slots of {count,
    // ptr, _}, then each slot's items, then every item's input words
    const auto tableCount = u32(d, 76);
    std::size_t pos = v5 + 156u * tableCount;
    std::vector<std::pair<std::uint32_t, std::size_t>> slots;
    for (std::uint32_t i = 0; i < tableCount; ++i) {
        const auto rec = v5 + 156u * i;
        CtlAiProfile p;
        p.id = u32(d, rec);
        for (int k = 0; k < 2; ++k) {
            p.enterDelay[k] = u16(d, rec + 4u + 2u * static_cast<std::size_t>(k));
            p.moveDelay[k]  = u16(d, rec + 8u + 2u * static_cast<std::size_t>(k));
        }
        for (int k = 0; k < 12; ++k) {
            const auto cnt = u32(d, rec + 16u + 12u * static_cast<std::size_t>(k));
            slots.push_back({cnt, pos});
            pos += 16u * cnt;
        }
        f.ai.push_back(std::move(p));
    }
    for (const auto& [cnt, blk] : slots)
        for (std::uint32_t j = 0; j < cnt; ++j)
            pos += 4u * u32(d, blk + 16u * j + 4u);

    // A second pass, because a move's own input words live after EVERY slot's
    // item records - the file is laid out level by level, not profile by
    // profile, so the words cannot be read while the slots are being counted.
    {
        std::size_t ip = v5 + 156u * tableCount;
        for (const auto& [cnt, blk] : slots) (void)blk, ip += 16u * cnt;
        std::size_t si = 0;
        for (auto& p : f.ai)
            for (auto& sl : p.slots) {
                const auto [cnt, blk] = slots[si++];
                for (std::uint32_t j = 0; j < cnt; ++j) {
                    const auto n = u32(d, blk + 16u * j + 4u);
                    std::vector<std::uint32_t> mv;
                    mv.reserve(n);
                    for (std::uint32_t t = 0; t < n; ++t) mv.push_back(u32(d, ip + 4u * t));
                    ip += 4u * n;
                    sl.moves.push_back(std::move(mv));
                }
            }
    }

    for (auto e : ents)                                   // entry +28
        if (u8(d, e + 76) & 8u) pos = 32u * u32(d, pos) + pos + 8u;

    // the clips: one per DISTINCT name, in entry order
    std::set<std::string> seen;
    std::map<std::string, int> clipOf;
    for (auto e : ents) {
        const auto it = named.find(e);
        if (it == named.end()) continue;
        const auto nm = name12(d, it->second, true);
        if (nm.empty() || seen.count(nm)) continue;
        seen.insert(nm);
        if (pos + 4 > d.size()) break;
        const auto length = u32(d, pos);
        if (length == 0 || pos + 4 + length > d.size()) break;
        clipOf[nm] = static_cast<int>(f.clips.size());
        CtlClip c{nm, pos + 4, length, 0};
        // A `.CTL` clip is a bare `.ani` descriptor - no "3.0V" wrapper - so
        // its frame count comes from the same reader the libraries use.
        if (const auto desc = animDescriptor(d, pos + 4)) c.frames = desc->frames;
        f.clips.push_back(std::move(c));
        pos += 4u + length;
    }

    f.end = pos;
    f.exact = (pos == d.size());

    // the state graph. InitCEFFile resolves parents and children within the
    // state's OWN group and a GoTo across the whole file, so the two lookups
    // are different questions and are checked separately.
    std::set<std::pair<int, std::uint32_t>> byGroupId;
    std::set<std::uint32_t> anyId;
    for (std::size_t i = 0; i < ents.size(); ++i) {
        byGroupId.insert({groupOf[i], u32(d, ents[i])});
        anyId.insert(u32(d, ents[i]));
    }
    for (std::size_t i = 0; i < ents.size(); ++i) {
        const auto e = ents[i];
        CtlState s;
        s.offset = e;
        s.group  = groupOf[i];
        s.id     = u32(d, e);
        s.flags  = u32(d, e + 8);
        const auto it = named.find(e);
        if (it != named.end()) s.name = name12(d, it->second, true);
        if (const auto c = childOff.find(e); c != childOff.end())
            for (int k = 0; k < u8(d, e + 87); ++k)
                s.children.push_back(u32(d, c->second + 4u * static_cast<std::size_t>(k)));
        if (const auto p = parentOff.find(e); p != parentOff.end())
            for (int k = 0; k < u8(d, e + 86); ++k)
                s.parents.push_back(u32(d, p->second + 4u * static_cast<std::size_t>(k)));
        s.gotoId = u32(d, e + 40);
        s.inputCode  = u32(d, e + 4);
        s.role       = static_cast<std::uint16_t>(u32(d, e + 12) & 0xFFFFu);
        s.cancelFrom = f32(d, e + 16);
        s.cancelTo   = f32(d, e + 20);
        s.priority   = u16(d, e + 84);
        s.flags12    = u32(d, e + 12);
        s.startFrame = f32(d, e + 24);
        s.playBits   = u16(d, e + 76);
        s.blendFrames = u16(d, e + 78);
        s.phaseOffset = u16(d, e + 80);
        s.hasEffects = (u8(d, e + 76) & 8u) != 0;
        if (const auto t = turnOff.find(e); t != turnOff.end()) {
            s.hasTurn = true;
            for (int k = 0; k < 6; ++k)
                s.turn[k] = f32(d, t->second + 4u * static_cast<std::size_t>(k));
        }
        if (const auto sh = shiftOff.find(e); sh != shiftOff.end()) {
            s.hasShift = true;
            for (int k = 0; k < 5; ++k)
                s.shift[k] = f32(d, sh->second + 4u * static_cast<std::size_t>(k));
        }
        if (const auto mv = moveOff.find(e); mv != moveOff.end())
            s.moveName = name12(d, mv->second, true);
        if (const auto cb = combatOff.find(e); cb != combatOff.end()) {
            s.hasCombat = true;
            for (int k = 0; k < 10; ++k)
                s.combat.raw[k] = f32(d, cb->second + 4u * static_cast<std::size_t>(k));
        }
        if (!s.name.empty() && clipOf.count(s.name)) s.clip = clipOf[s.name];
        for (auto c : s.children) if (!byGroupId.count({s.group, c})) s.childOk = false;
        for (auto p : s.parents)  if (!byGroupId.count({s.group, p})) s.parentOk = false;
        if (s.gotoId && !anyId.count(s.gotoId)) s.gotoOk = false;
        f.states.push_back(std::move(s));
    }

    // The link pass, as INDICES. `InitCEFFile` resolves a parent or a child
    // inside the state's own group and a GoTo across the whole file, and
    // refuses to load the file if any of them misses - which is why the
    // engine can dereference them without checking, and why the replica can
    // index them without checking once this has run. Resolving here rather
    // than per tick is also what lets the sweep assert that every landing is
    // a real entry: an unresolved edge is a -1 the runtime can count.
    std::map<std::pair<int, std::uint32_t>, int> byGroup;
    std::map<std::uint32_t, int> byFile;
    for (std::size_t i = 0; i < f.states.size(); ++i) {
        byGroup.emplace(std::make_pair(f.states[i].group, f.states[i].id),
                        static_cast<int>(i));
        byFile.emplace(f.states[i].id, static_cast<int>(i));
    }
    for (auto& s : f.states) {
        for (auto c : s.children) {
            const auto it = byGroup.find({s.group, c});
            s.childIdx.push_back(it == byGroup.end() ? -1 : it->second);
        }
        for (auto p : s.parents) {
            const auto it = byGroup.find({s.group, p});
            s.parentIdx.push_back(it == byGroup.end() ? -1 : it->second);
        }
        if (s.gotoId) {
            const auto it = byFile.find(s.gotoId);
            s.gotoIdx = (it == byFile.end()) ? -1 : it->second;
        }
    }

    // the groups themselves, each with the one flag-0x20 default it carries
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        CtlGroup g;
        g.id    = static_cast<std::int32_t>(u32(d, groups[gi].at));
        g.flags = u32(d, groups[gi].at + 8);
        g.count = static_cast<int>(groups[gi].n);
        for (std::size_t i = 0; i < f.states.size(); ++i)
            if (f.states[i].group == static_cast<int>(gi)) {
                g.first = static_cast<int>(i);
                break;
            }
        // Cef_DefaultEntry (0x0047DD40) scans the group's entries in order for
        // the first with flag 0x20 and returns nothing when there is none.
        for (int k = 0; k < g.count; ++k)
            if (f.states[static_cast<std::size_t>(g.first + k)].flags & 0x20u) {
                g.defaultEntry = g.first + k;
                break;
            }
        f.groupList.push_back(g);
    }

    f.valid = true;
    return f;
}

std::int32_t CtlCombat::asInt(int i) const {
    // Three of the ten are integers stored in a float's bytes - the damage and
    // the two reaction ids. Reinterpreting rather than converting is the whole
    // point: 25.0f is not 25, and the corpus says the field really is 25.
    if (i < 0 || i >= 10) return 0;
    std::int32_t v;
    std::memcpy(&v, &raw[i], 4);
    return v;
}

}  // namespace omk

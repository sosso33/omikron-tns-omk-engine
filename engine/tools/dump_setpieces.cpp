// SPDX-License-Identifier: GPL-3.0-or-later
// THE `.SFX`'s SECTION E - what fires a scene's effects, and when.
//
//     dump_setpieces <gamedata/SCPTDATA> <file.sfx> <out.txt>
//
// Starting a scene object does two things, adjacent in all four of the
// handlers that do it (`sub_41B6F0`, `sub_41BA80`, `sub_41D9C0`, `sub_41DC10`):
//
//     call sub_44A7E0          ; Script_StartScript(instance)
//     ...
//     call sub_44CD40          ; the instance's own id
//     and  eax, 0FFFFh
//     push eax                 ; a2
//     push 0                   ; a1
//     call sub_451470          ; show the set pieces keyed (a1, a2)
//
// and `sub_451470` walks section E - 76-byte rows at `dword_536BAC[slot]`,
// count `dword_536B54[slot]` - showing every row whose `+8` equals a1 and
// `+12` equals a2. So **section E binds an effect to an object-START event**,
// which is the trigger the scene-sprite path never had.
//
// For `grid.sfx` that is rows keyed to objects 1 (`1KaylArrives`), 8
// (`3KaylLeaves`) and 20 (`Wait5sec`) - all three started by AREA 118's own
// script, which is why the portal appears with the arrival. The four `fx*`
// objects nothing starts are a different mechanism and are not the portal.
//
// What is NOT established: the remaining fields of a row. `+16` runs
// 0,0,0,1,2,2,3,3,3,3,4 over the eleven, which is index-shaped but could be a
// group rather than the effect, and this tool reports it without naming it.
#include "platform/datafs.h"
#include "formats/sfx.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static std::uint32_t u32(const std::vector<std::byte>& d, std::size_t o) {
    return static_cast<std::uint32_t>(d[o]) |
           (static_cast<std::uint32_t>(d[o + 1]) << 8) |
           (static_cast<std::uint32_t>(d[o + 2]) << 16) |
           (static_cast<std::uint32_t>(d[o + 3]) << 24);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: dump_setpieces <dir> <file.sfx> <out.txt>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    const auto d = fs.read(argv[2]);
    if (!omk::safeOutputPath(argv[3])) return 2;
    if (d.size() < 8 || std::memcmp(d.data(), "5.0V", 4) != 0) {
        std::fprintf(stderr, "not a .SFX\n"); return 1;
    }
    // The walk `Sfx_LoadFile` performs: A x40, B x44, C x80, D x16, E x76.
    const std::uint32_t A = u32(d, 4);
    std::size_t o = 8 + 40u * A;
    const std::uint32_t B = u32(d, o);        o += 4 + 44u * B;
    const std::uint32_t C = u32(d, o);        const std::size_t cAt = o + 4;
    o = cAt + 80u * C;
    const std::uint32_t D = u32(d, o);        o += 4 + 16u * D;
    const std::uint32_t E = u32(d, o);        const std::size_t eAt = o + 4;

    // ...and the same file through the port's own reader, so what this
    // prints is what the runtime uses.
    const omk::SfxFile sf = omk::readSfx(d);

    std::ofstream f(argv[3]);
    f << "counts " << A << ' ' << B << ' ' << C << ' ' << D << ' ' << E << '\n';
    for (const auto& e : sf.effects)
        f << "effect " << e.id << ' ' << (e.name.empty() ? "-" : e.name)
          << " sprite " << e.sprite << " mode " << int(e.mode)
          << " life " << e.life << " count " << e.count
          << " scale " << e.scale << " cone " << e.cone << " spin " << e.spin
          << " drift " << e.drift
          << " v " << e.vx << ',' << e.vy << ',' << e.vz
          << " flags 0x" << std::hex << e.flags
          << " col " << e.colour0 << "->" << e.colour1 << std::dec << '\n';
    for (std::size_t i = 0; i < sf.pieces.size(); ++i) {
        const auto& p = sf.pieces[i];
        const omk::FxEffect* e = sf.byId(p.effectId);
        f << "piece " << i << " key (" << p.key0 << ", " << p.key1 << ")"
          << " effect " << p.effectId << ' '
          << (e ? (e->name.empty() ? "-" : e->name) : "?")
          << " sprite " << (e ? int(e->sprite) : -1)
          << " at " << p.pos[0] << ' ' << p.pos[1] << ' ' << p.pos[2]
          // the state-machine fields (setpiece.h): the row's link, its delay,
          // loop count and flags, and the block it walks
          << " link " << p.linkType << ' ' << p.linkId
          << " delay " << p.delay << " loops " << p.loops
          << " flags 0x" << std::hex << p.flags << std::dec
          << " block " << p.block << " records " << p.parts.size() << '\n';
        for (std::size_t j = 0; j < p.parts.size(); ++j) {
            const auto& r = p.parts[j];
            f << "record " << i << ' ' << j << " effect " << r.effect
              << " at " << r.pos[0] << ' ' << r.pos[1] << ' ' << r.pos[2]
              << " dur " << r.dur << " link " << r.linkType << ' ' << r.linkId << '\n';
        }
    }
    std::printf("%s: %u effects, %zu set pieces\n", argv[2], C, sf.pieces.size());
    for (std::size_t i = 0; i < sf.pieces.size(); ++i) {
        const auto& p = sf.pieces[i];
        const omk::FxEffect* e = sf.byId(p.effectId);
        std::printf("   piece %2zu  key (%d, %d)  effect %d %-10s sprite %d"
                    "  at %.0f %.0f %.0f\n", i, p.key0, p.key1, p.effectId,
                    e ? e->name.c_str() : "?", e ? int(e->sprite) : -1,
                    p.pos[0], p.pos[1], p.pos[2]);
    }
    return 0;
}

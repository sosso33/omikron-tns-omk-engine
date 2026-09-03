// SPDX-License-Identifier: GPL-3.0-or-later
// The ENVIRONMENT's own animations: objects the AREA's startup script starts,
// as opposed to a cutscene's beats. AREA 222's `+4` script opens with
// `scx.play 20, 0, 0` - `Ventilo`, the Impasse's fan, the only object in
// `Impasse.SCX` with loopCount -1 (run for ever). It must be running after the
// area loads, and must still be running once the scene plays over it.
#include "script/area.h"
#include "platform/datafs.h"
#include <cstdio>
#include <cmath>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
            "usage: env_anim <gamedata> <vm_opcodes.json> <START> <SCPTDATA> <out.bin>\n");
        return 2;
    }
    if (!omk::safeOutputPath(argv[5])) return 2;
    const std::string data = argv[1];
    const auto table = omk::OpcodeTable::loadJson(argv[2]);
    auto state = omk::GameState::fromFile(argv[3]);
    omk::Session s(data + "/IAM", state, table);
    s.loadScene(argv[4], omk::ChunkKind::Area, 222);
    s.loadArea(222);
    for (int i = 0; i < 64 && s.loading(); ++i) s.frame();
    std::printf("after load: area %d, scx '%s', programs %zu running %d\n",
                s.currentArea(), s.scene().file().c_str(),
                s.scene().programCount(), s.scene().programsRunning());
    for (int f = 0; f < 20; ++f) s.frame();
    std::printf("after 20 frames: running %d\n", s.scene().programsRunning());
    std::printf("started objects:\n");
    for (const auto& st : s.scene().started())
        std::printf("   object %d  actor %d  clip %d  path %d\n",
                    st.object, st.actor, st.clip, st.path);
    // Object 20 is `Ventilo`, started by AREA 222's `+4` script, the only
    // loopCount -1 object in the file. Its node must TURN and must not MOVE.
    int started20 = 0;
    for (const auto& st : s.scene().started()) if (st.object == 20) started20 = 1;
    float p0[3] = {0, 0, 0}, q0[4] = {1, 0, 0, 0};
    float posSpread = 0.0f, quatSpread = 0.0f;
    int rotated = 0, isEpale = 0, samples = 0;
    std::printf("motions over the next 12 frames:\n");
    for (int f = 0; f < 12; ++f) {
        s.frame();
        for (const auto& m : s.scene().motions()) {
            if (m.name != "Epale20") continue;
            isEpale = 1;
            if (m.rotated) rotated = 1;
            if (samples == 0) {
                for (int c = 0; c < 3; ++c) p0[c] = m.pos[c];
                for (int c = 0; c < 4; ++c) q0[c] = m.quat[c];
            } else {
                for (int c = 0; c < 3; ++c)
                    posSpread = std::fmax(posSpread, std::fabs(m.pos[c] - p0[c]));
                for (int c = 0; c < 4; ++c)
                    quatSpread = std::fmax(quatSpread, std::fabs(m.quat[c] - q0[c]));
            }
            ++samples;
        }
    }
    for (int f = 0; f < 0; ++f) {
        for (const auto& m : s.scene().motions())
            std::printf("  f%-3d node '%s'  t=%7.2f  pos %9.2f %9.2f %9.2f  quat %6.3f %6.3f %6.3f %6.3f  rot %d\n",
                        f, m.name.c_str(), m.t, m.pos[0], m.pos[1], m.pos[2],
                        m.quat[0], m.quat[1], m.quat[2], m.quat[3], (int)m.rotated);
    }
    std::printf("MISSED scx.play (no such object resident): %zu\n",
                s.scene().missed().size());
    for (const auto& m : s.scene().missed()) std::printf("   %d\n", m);
    std::printf("Ventilo: started %d, node Epale20 %d, samples %d, rotated %d, "
                "posSpread %.4f, quatSpread %.4f\n",
                started20, isEpale, samples, rotated, posSpread, quatSpread);
    const std::int32_t out[8] = {
        started20, s.scene().programsRunning(), isEpale, samples, rotated,
        static_cast<std::int32_t>(posSpread * 100.0f + 0.5f),
        static_cast<std::int32_t>(quatSpread * 1000.0f + 0.5f),
        static_cast<std::int32_t>(s.scene().missed().size()),
    };
    std::ofstream of(argv[5], std::ios::binary);
    of.write(reinterpret_cast<const char*>(out), sizeof out);
    return 0;
}

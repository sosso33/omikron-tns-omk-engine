// SPDX-License-Identifier: GPL-3.0-or-later
// WHERE THE PLAYER STANDS TO OPEN THE SLIDER'S DOOR - `MDACTION`'s slider arm.
//
//     slider_door <gamedata>
//
// `MDACTION` (0x0046AEC0, loc_46AFF8) snaps the actor to the door before the
// door clip plays:
//
//     off   = R_slider . (root0(Cef_DefaultClip(group 60)) - root0(slf_112.3da))
//     actor = slider + off, y - 33.149605
//
// where `root0` is `sub_471100`: the first position key of the first track
// that has any (key 0, the rest key). Group 60 is `H_SLDIN`, the door clip;
// `ANIMS\slf_112.3da` is a reference clip `Game_Init` loads into
// `dword_90EF28` (05_sys.c 1842). This prints both roots and the offset, in
// the character's frame, so the number the viewer uses is one a person can
// read off. Group 61 (`H_SLDOUT`) is printed beside it for the exit.
#include "formats/ctl.h"
#include "formats/anim.h"
#include "actor/pose.h"
#include "platform/datafs.h"

#include <cstdio>
#include <cstring>

static bool root0(const std::vector<std::byte>& data, const omk::CtlFile& f, int clip, float out[3]) {
    if (clip < 0 || (std::size_t)clip >= f.clips.size()) return false;
    const auto d = omk::animDescriptor(data, f.clips[(std::size_t)clip].offset);
    if (!d) return false;
    for (const auto& t : d->tracks) {                 // sub_471100: first track with keys
        if (!t.posOffset || t.posKeys <= 0) continue;
        if (t.posOffset + 12 > data.size()) return false;
        std::memcpy(out, data.data() + t.posOffset, 12);
        return true;
    }
    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: slider_door <gamedata>\n"); return 2; }
    const omk::DataFs fs(argv[1]);
    const auto data = fs.read("ANIMS/H1Avnt.CTL");
    if (data.empty()) { std::printf("H1Avnt.CTL MISSING\n"); return 1; }
    const auto f = omk::readCtl(data);
    // TWO reference clips, and they are NOT interchangeable: `Game_Init`
    // loads `anims\\slf_112.3da` into `dword_90EF28` and `anims\\slf_113.3da`
    // into `dword_9103D8` (05_sys.c 1842-1844), the ENTRY (`MDACTION`) reads
    // the first and the EXIT (`sub_468FA0`) reads the second. Measuring group
    // 61 against 112, as this did until 2026-09-08, gives an offset that is
    // wrong by the difference between the two references.
    int fails = 0;
    float r112[3] = {0, 0, 0}, r113[3] = {0, 0, 0};
    const auto ref112 = fs.read("ANIMS/slf_112.3da");
    const auto ref113 = fs.read("ANIMS/slf_113.3da");
    const bool have112 = !ref112.empty() && omk::clipRootStart(ref112, r112);
    const bool have113 = !ref113.empty() && omk::clipRootStart(ref113, r113);
    std::printf("slf_112.3da root0 %s %.3f %.3f %.3f\n", have112 ? "ok" : "MISSING",
                r112[0], r112[1], r112[2]);
    std::printf("slf_113.3da root0 %s %.3f %.3f %.3f\n", have113 ? "ok" : "MISSING",
                r113[0], r113[1], r113[2]);
    if (!have112) ++fails;
    if (!have113) ++fails;
    for (int gid : {60, 61}) {
        const float* r0 = (gid == 60) ? r112 : r113;
        int g = -1;
        for (std::size_t i = 0; i < f.groupList.size(); ++i) if ((int)f.groupList[i].id == gid) g = (int)i;
        if (g < 0) { std::printf("group %d MISSING\n", gid); ++fails; continue; }
        const auto& G = f.groupList[(std::size_t)g];
        if (G.defaultEntry < 0) { std::printf("group %d no default entry\n", gid); ++fails; continue; }
        const auto& s = f.states[(std::size_t)G.defaultEntry];
        float c0[3] = {0, 0, 0};
        const bool ok = root0(data, f, s.clip, c0);
        const int frames = s.clip >= 0 ? f.clips[(std::size_t)s.clip].frames : 0;
        std::printf("group %d default %s clip %d (%s, %d frames) root0 %s %.3f %.3f %.3f  off %.3f %.3f %.3f\n",
                    gid, s.name.c_str(), s.clip, s.clip >= 0 ? f.clips[(std::size_t)s.clip].name.c_str() : "-",
                    frames, ok ? "ok" : "MISSING", c0[0], c0[1], c0[2],
                    c0[0] - r0[0], c0[1] - r0[1], c0[2] - r0[2]);
        if (!ok) ++fails;
        // the travel: sum of keys 1..k
        if (s.clip >= 0) {
            const auto d = omk::animDescriptor(data, f.clips[(std::size_t)s.clip].offset);
            if (d) for (const auto& t : d->tracks) {
                if (!t.posOffset || t.posKeys <= 0) continue;
                float acc[3] = {0, 0, 0};
                std::printf("   travel:");
                for (int k = 1; k < t.posKeys && t.posOffset + 12u * (std::size_t)k + 12 <= data.size(); ++k) {
                    float v[3]; std::memcpy(v, data.data() + t.posOffset + 12u * (std::size_t)k, 12);
                    for (int c = 0; c < 3; ++c) acc[c] += v[c];
                    if (k % 18 == 0 || k == t.posKeys - 1)
                        std::printf("  f%d (%.1f %.1f %.1f)", k, acc[0], acc[1], acc[2]);
                }
                std::printf("\n");
                break;
            }
        }
    }
    return fails ? 1 : 0;
}
// ---- appended 2026-09-08: the clips' own ROOT TRAVEL ---------------------
// `Anim_RootDelta` sums position keys 1..N (key 0 is the rest). Printed at a
// few frames so the carry-in can be checked against the door offset: from
// the door, the entry clip should bring the root back onto the slider.

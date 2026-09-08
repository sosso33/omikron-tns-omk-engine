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
    const auto ref = fs.read("ANIMS/slf_112.3da");
    float r0[3] = {0, 0, 0};
    const bool haveRef = !ref.empty() && omk::clipRootStart(ref, r0);
    std::printf("slf_112.3da root0 %s %.3f %.3f %.3f\n", haveRef ? "ok" : "MISSING", r0[0], r0[1], r0[2]);
    int fails = haveRef ? 0 : 1;
    for (int gid : {60, 61}) {
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
    }
    return fails ? 1 : 0;
}

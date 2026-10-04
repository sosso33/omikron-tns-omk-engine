// SPDX-License-Identifier: GPL-3.0-or-later
// WHAT A SCENE KEEPS OF ITS .SCX (`verify.py: engine: scx kept`,
// todo/ram-vs-original.md tier C). `ScxRuntime` keeps the clips' and the
// sounds' bytes and drops the file, as `Scene_LoadSCX` does. For every
// `.SCX`: each clip's bytes and frame count, and each sound's bytes, through
// the runtime against the same read straight from the file (the stream's own
// offsets, the descriptor over the whole file) - and the bytes kept against
// the file's size.
//
//     scx_kept <gamedata>   -> one line, then the total
#include "formats/anim.h"
#include "formats/scx.h"
#include "platform/datafs.h"
#include "script/program.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
bool same(std::span<const std::byte> a, std::span<const std::byte> b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size()) == 0);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: scx_kept <gamedata>\n");
        return 2;
    }
    const omk::DataFs fs(argv[1]);
    auto files = fs.list("SCPTDATA", "SCX");
    std::sort(files.begin(), files.end());
    long scenes = 0, clips = 0, sounds = 0, bad = 0;
    std::size_t fileBytes = 0, keptBytes = 0;
    for (const std::string& path : files) {
        const auto d = omk::DataFs::readPath(path);
        const omk::ScxRuntime rt(d);
        if (!rt.valid()) continue;
        ++scenes;
        fileBytes += d.size();
        const auto st = omk::readScxStream(d);
        for (std::size_t i = 0; i < st.anims.size(); ++i) {
            const auto& a = st.anims[i];
            const std::span<const std::byte> want =
                a.offset + a.size <= d.size() ? std::span<const std::byte>(d).subspan(a.offset, a.size)
                                              : std::span<const std::byte>();
            int frames = 0;
            if (const auto ds = omk::animDescriptor(d, a.offset)) frames = std::max<std::int32_t>(1, ds->frames);
            ++clips;
            keptBytes += rt.clipData(static_cast<int>(i)).size();
            if (!same(rt.clipData(static_cast<int>(i)), want) ||
                rt.clipFrames(static_cast<int>(i)) != frames) ++bad;
        }
        for (std::size_t i = 0; i < st.wavs.size(); ++i) {
            const auto& w = st.wavs[i];
            const std::span<const std::byte> want =
                w.offset + w.size <= d.size() ? std::span<const std::byte>(d).subspan(w.offset, w.size)
                                              : std::span<const std::byte>();
            ++sounds;
            keptBytes += rt.wavData(static_cast<int>(i)).size();
            if (!same(rt.wavData(static_cast<int>(i)), want) ||
                rt.wavId(static_cast<int>(i)) != w.id) ++bad;
        }
    }
    std::printf("scx: %ld scenes, %ld clips, %ld sounds, %ld differ; %zu KB kept of %zu KB of files\n",
                scenes, clips, sounds, bad, keptBytes / 1024, fileBytes / 1024);
    return bad ? 1 : 0;
}

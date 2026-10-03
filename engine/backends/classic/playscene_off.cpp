// SPDX-License-Identifier: GPL-3.0-or-later
// The `--scene` set viewer is an SDL-side instrument (`backends/sdl/
// playscene.cpp`, its own window and events); a Carbon build has no SDL, so
// it says so instead.
#include <cstdio>
#include <string>

int sceneViewer(const std::string&, const std::string&, int, const float*, const float*,
                float, bool, int, const std::string&, bool, bool, int, int, int, int, bool) {
    std::fprintf(stderr, "--scene: the set viewer needs SDL; not in the classic Mac build\n");
    return 1;
}

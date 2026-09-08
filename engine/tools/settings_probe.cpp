// SPDX-License-Identifier: GPL-3.0-or-later
// Resolve the game settings from their three sources and print them.
//
//     settings_probe [--config <ini>] [--save <file>]
//
// The order is the engine's: defaults (`sub_41F4C0`), then `[Preferences]`,
// then the save file's 3496-byte header - which is what the options MENU last
// wrote and so wins.  `verify.py: settings resolve` drives it.
#include "platform/datafs.h"
#include "platform/options.h"
#include "platform/settings.h"

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

int main(int argc, char** argv) {
    std::string cfg, save;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--config") && i + 1 < argc) cfg = argv[++i];
        else if (!std::strcmp(argv[i], "--save") && i + 1 < argc) save = argv[++i];
    }
    const omk::OptionsFile ini = cfg.empty() ? omk::OptionsFile{} : omk::loadOptionsFile(cfg);
    std::optional<omk::SettingsBlock> hdr;
    if (!save.empty()) hdr = omk::readSettingsBlock(omk::DataFs::readPath(save));

    std::printf("ini %d  header %d\n", ini.loaded ? 1 : 0, hdr ? 1 : 0);
    const omk::Settings s = omk::resolveSettings(ini, hdr);
    const auto& v = s.v;
    std::printf("version %u\n", v.version);
    std::printf("clip %d %s\n", v.clipDistance, omk::sourceName(s.clipDistance));
    std::printf("sky %d %s\n", v.sky ? 1 : 0, omk::sourceName(s.sky));
    std::printf("shadows %d %s\n", v.shadows ? 1 : 0, omk::sourceName(s.shadows));
    // The ENHANCEMENTS, so a check can see what `all = max` did without
    // needing a window - `omk-play` reports them too, but it needs SDL.
    std::printf("enh aa %d %s\n", s.antiAliasing, omk::sourceName(s.antiAliasingSource));
    std::printf("enh filter %d %s\n", s.textureFilter, omk::sourceName(s.textureFilterSource));
    std::printf("enh aniso %d %s\n", s.anisotropy, omk::sourceName(s.anisotropySource));
    std::printf("enh shadowquality %d %s\n", s.shadowQuality,
                omk::sourceName(s.shadowQualitySource));
    std::printf("enh lighting %d %s\n", s.lighting, omk::sourceName(s.lightingSource));
    std::printf("enh all %d\n", s.enhanceAll ? 1 : 0);
    std::printf("screen %dx%d %s\n", v.screenX, v.screenY, omk::sourceName(s.screen));
    std::printf("density %d %s\n", v.streetActivity, omk::sourceName(s.streetActivity));
    std::printf("detail %d %s\n", v.levelOfDetail, omk::sourceName(s.levelOfDetail));
    std::printf("mouse %d %d %s\n", v.mouseSensitivityX, v.mouseSensitivityY,
                omk::sourceName(s.mouseSensitivity));
    std::printf("volumes %d %d %d %s\n", v.volumeDialogue, v.volumeMusic, v.volumeEffects,
                omk::sourceName(s.volumes));
    std::printf("flags %d %d %d %d %d %d\n", v.sound3d ? 1 : 0, v.subtitles ? 1 : 0,
                v.fightDifficulty, v.shootDifficulty, v.combatCamera, v.mouseInverted ? 1 : 0);
    // The derived world-unit values: the clip distance in inches and the three
    // things it sizes - the two bucket splits and the linear fog's range.
    std::printf("derived %.4f %.4f %.4f %.4f %.4f\n", s.clipInches(), s.nearSplit(),
                s.farSplit(), s.fogStart(), s.fogEnd());
    // The three control-scheme tables, as a checksum plus three named cells:
    // Aventure/Avancer (group 0 action 2), Combat/Coup de pied 1 (3, 6) and
    // Tirer/Tir (2, 4) on the keyboard.
    unsigned long sum = 0;
    for (auto k : v.keyboard) sum += k;
    for (auto k : v.mouse) sum += k;
    for (auto k : v.joystick) sum += k;
    std::printf("bindings %lu %u %u %u %s\n", sum, v.keyboard[2], v.keyboard[3 * 14 + 6],
                v.keyboard[2 * 14 + 4], omk::sourceName(s.bindings));
    return 0;
}

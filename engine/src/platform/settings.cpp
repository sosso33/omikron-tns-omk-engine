// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/settings.h"

namespace omk {
namespace {

// The engine's own section name for the 65 keys.
constexpr const char* kPrefs = "preferences";
// This port's own addition, for the two rows the ini cannot say.
constexpr const char* kOptions = "options";
// And its section for what the original never had (settings.h).
constexpr const char* kEnhancements = "enhancements";

}  // namespace

const char* sourceName(Settings::Source s) {
    switch (s) {
        case Settings::Source::Ini:  return "config";
        case Settings::Source::Save: return "save";
        default:                     return "default";
    }
}

Settings resolveSettings(const OptionsFile& ini,
                         const std::optional<SettingsBlock>& save) {
    Settings s;
    s.v = defaultSettingsBlock();

    // ---- 1. the ini's [Preferences] -------------------------------------
    if (ini.loaded) {
        auto takeInt = [&](const char* sect, const char* key, int& dst,
                           Settings::Source& src) {
            const std::string* raw = ini.find(sect, key);
            if (!raw) return;
            dst = ini.integer(sect, key, dst);
            src = Settings::Source::Ini;
        };
        auto takeBool = [&](const char* sect, const char* key, bool& dst,
                            Settings::Source& src) {
            const std::string* raw = ini.find(sect, key);
            if (!raw) return;
            dst = ini.boolean(sect, key, dst);
            src = Settings::Source::Ini;
        };

        takeInt (kPrefs, "clipdistance",   s.v.clipDistance,      s.clipDistance);
        takeBool(kPrefs, "displaysky",     s.v.sky,               s.sky);
        takeBool(kPrefs, "displayshadows", s.v.shadows,           s.shadows);
        takeInt (kPrefs, "screen_x",       s.v.screenX,           s.screen);
        takeInt (kPrefs, "screen_y",       s.v.screenY,           s.screen);
        takeInt (kPrefs, "MouseSensX",     s.v.mouseSensitivityX, s.mouseSensitivity);
        takeInt (kPrefs, "MouseSensY",     s.v.mouseSensitivityY, s.mouseSensitivity);
        // The three volumes are ATTENUATIONS in the ini (0 full, 100 silent),
        // which is the same sense the header's fields carry - `Music_SetVolume`
        // sums them, and `sub_41F4C0`'s default is 0 for all three.
        takeInt (kPrefs, "music",             s.v.volumeMusic,    s.volumes);
        takeInt (kPrefs, "DialogAttenuation", s.v.volumeDialogue, s.volumes);
        takeInt (kPrefs, "FxAttenuation",     s.v.volumeEffects,  s.volumes);

        // ---- this port's own [Options], for the two rows with no ini key --
        takeInt(kOptions, "streetactivity", s.v.streetActivity, s.streetActivity);
        takeInt(kOptions, "levelofdetail",  s.v.levelOfDetail,  s.levelOfDetail);

        // ---- [Enhancements], off unless written -------------------------
        takeInt(kEnhancements, "antialiasing", s.antiAliasing, s.antiAliasingSource);
    }

    // ---- 2. the save header, which is later and therefore wins -----------
    if (save) {
        const SettingsBlock& h = *save;
        s.v.version           = h.version;
        s.v.clipDistance      = h.clipDistance;      s.clipDistance = Settings::Source::Save;
        s.v.sky               = h.sky;               s.sky          = Settings::Source::Save;
        s.v.shadows           = h.shadows;           s.shadows      = Settings::Source::Save;
        s.v.screenX           = h.screenX;
        s.v.screenY           = h.screenY;           s.screen       = Settings::Source::Save;
        s.v.streetActivity    = h.streetActivity;    s.streetActivity = Settings::Source::Save;
        s.v.levelOfDetail     = h.levelOfDetail;     s.levelOfDetail  = Settings::Source::Save;
        s.v.mouseSensitivityX = h.mouseSensitivityX;
        s.v.mouseSensitivityY = h.mouseSensitivityY; s.mouseSensitivity = Settings::Source::Save;
        s.v.volumeDialogue    = h.volumeDialogue;
        s.v.volumeMusic       = h.volumeMusic;
        s.v.volumeEffects     = h.volumeEffects;     s.volumes      = Settings::Source::Save;
        s.v.sound3d           = h.sound3d;
        s.v.subtitles         = h.subtitles;
        s.v.fightDifficulty   = h.fightDifficulty;
        s.v.shootDifficulty   = h.shootDifficulty;
        s.v.combatCamera      = h.combatCamera;
        s.v.mouseInverted     = h.mouseInverted;
        s.v.forceFeedback     = h.forceFeedback;
        s.v.keyboard          = h.keyboard;
        s.v.mouse             = h.mouse;
        s.v.joystick          = h.joystick;          s.bindings     = Settings::Source::Save;
    }

    // Clamp the two the engine's own consumers would index with.
    if (s.v.streetActivity < 0) s.v.streetActivity = 0;
    if (s.v.streetActivity > 4) s.v.streetActivity = 4;
    if (s.v.levelOfDetail  < 0) s.v.levelOfDetail  = 0;
    if (s.v.levelOfDetail  > 2) s.v.levelOfDetail  = 2;
    if (s.v.clipDistance   < 1) s.v.clipDistance   = 1;
    s.antiAliasing = msaaSamples(s.antiAliasing);
    return s;
}

}  // namespace omk

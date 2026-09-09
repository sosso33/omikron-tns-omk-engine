// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/settings.h"

#include <cstdio>

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
        //
        // `all` is read LAST but applied at the END of the section, so a
        // specific key beats it whatever order the two appear in the file.
        bool wantAll = false;
        if (const std::string* w = ini.find(kEnhancements, "all")) {
            std::string v = *w;
            for (auto& c : v) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            wantAll = v == "max" || v == "1" || v == "on" || v == "yes";
            if (!wantAll && v != "0" && v != "off" && v != "no" && v != "none")
                std::fprintf(stderr, "settings: all = %s is not max|off - ignored\n",
                             w->c_str());
        }
        takeInt(kEnhancements, "antialiasing", s.antiAliasing, s.antiAliasingSource);
        if (const std::string* w = ini.find(kEnhancements, "texturefiltering")) {
            const int m = textureFilterMode(*w);
            if (m >= 0) { s.textureFilter = m; s.textureFilterSource = Settings::Source::Ini; }
            else std::fprintf(stderr, "settings: texturefiltering = %s is not a mode "
                                      "(nearest|bilinear|trilinear) - ignored\n", w->c_str());
        }
        if (const std::string* w = ini.find(kEnhancements, "uiscaling")) {
            const int m = uiScalingMode(*w);
            if (m >= 0) { s.uiScaling = m; s.uiScalingSource = Settings::Source::Ini; }
            else std::fprintf(stderr, "settings: uiscaling = %s is not a mode "
                                      "(nearest|linear) - ignored\n", w->c_str());
        }
        takeInt(kEnhancements, "anisotropy", s.anisotropy, s.anisotropySource);
        takeInt(kEnhancements, "supersampling", s.supersample, s.supersampleSource);
        if (const std::string* w = ini.find(kEnhancements, "lighting")) {
            const int m = lightingMode(*w);
            if (m >= 0) { s.lighting = m; s.lightingSource = Settings::Source::Ini; }
            else std::fprintf(stderr, "settings: lighting = %s is not a mode "
                                      "(pervertex|perpixel) - ignored\n", w->c_str());
        }
        if (const std::string* w = ini.find(kEnhancements, "shadowquality")) {
            const int m = shadowQualityMode(*w);
            if (m >= 0) { s.shadowQuality = m; s.shadowQualitySource = Settings::Source::Ini; }
            else std::fprintf(stderr, "settings: shadowquality = %s is not a mode "
                                      "(classic|fitted|mapped) - ignored\n", w->c_str());
        }
        // ...applied LAST, so every specific key above has already claimed its
        // field and `applyMaxEnhancements` leaves it alone. That is what makes
        // `all` a base rather than an override, whatever order the file is in.
        if (wantAll) { s.enhanceAll = true; applyMaxEnhancements(s); }
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
    if (s.anisotropy < 1)  s.anisotropy = 1;
    if (s.anisotropy > 16) s.anisotropy = 16;
    return s;
}

}  // namespace omk

// SPDX-License-Identifier: GPL-3.0-or-later
// OMK'S OWN OPTIONS PAGES, in the viewer (`todo/graphics-menu.md` A2-A4): the
// rows given the values this run resolved, their changes written to the
// config and the live ones applied. The pages themselves are
// `ui/omkoptions.h`; screen 35 hosts them (`OptionsMenu::attachOmk`).
#include "playstate.h"

#include "platform/events.h"
#include "platform/options.h"
#include "platform/settings.h"
#include "actor/pose.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
// the reader's copy of the strings, beside the config (`ui/omktext.h`)
std::string textOverride(const std::string& configFile) {
    const auto cut = configFile.find_last_of("/\\");
    return (cut == std::string::npos ? std::string() : configFile.substr(0, cut + 1)) + "omk-menu.ini";
}
}  // namespace

// Once, beside screen 35 (`setupOptions`): the texts in `[OMK] language`, and
// the pages - the Remaster page only in a build that has the remaster.
void PlayState::omkOptionsSetup() {
    const bool over = omkText_.load(settings.language, textOverride(configFile));
    auto& o = omkOpt_.emplace(remasterBuilt());
    o.setText(&omkText_);
    optMenu_->attachOmk(&o);
    std::printf("options: OMK's pages - language %s%s, %s\n", omkText_.language().c_str(),
                over ? " (the texts beside the config read over the compiled ones)" : "",
                remasterBuilt() ? "the Enhancements and Remaster pages" : "the Enhancements page (a build without the remaster)");
}

// Each time screen 35 opens: what the renderer can draw, and each row's
// value as this run resolved it, its default, and the flag that holds it.
void PlayState::omkOptionsOpen() {
    if (!omkOpt_) return;
    auto& o = *omkOpt_;
    omkOpened_ = true;
    o.setTakes(world_ ? world_->takes() : 0u);
    const omk::Settings d;   // the reader's defaults - what a reset key gives
    // THE FILE, as the start read it and as it is now: a row whose file has
    // not moved shows what RUNS (a device may have reduced it - 8x MSAA run
    // at 4x); one whose file has moved - a change waiting for the next start,
    // the menu reopened - shows the file's, with its `*`
    const omk::Settings f0 = omk::resolveSettings(ini, std::nullopt);
    const omk::Settings f1 = omk::resolveSettings(omk::loadOptionsFile(configFile), std::nullopt);
    const auto row = [&](const std::string& k, double running, double conf0, double conf1) {
        if (conf1 == conf0) o.setValue(k, running);
        else                o.setValue(k, running, conf1);
    };
    // ---- [Enhancements]: a flag holds its own row, and `--enhance-all` every
    // row it raises that has no flag of its own (`enh` in playstate.cpp)
    const auto enh = [&](const char* key, double running, double def, int flag, const char* flagName,
                         double v0, double v1, double top, bool underAll = true) {
        const std::string k = std::string("enhancements.") + key;
        const auto conf = [&](double fileV) {
            return omkReduce(k, flag >= 0 ? flag : (underAll && enhanceAll) ? top : fileV);
        };
        row(k, running, conf(v0), conf(v1));
        o.setDefault(k, def);
        o.setHeld(k, flag >= 0 ? flagName : (underAll && enhanceAll) ? "--enhance-all" : "");
    };
    enh("all", (enhanceAll || settings.enhanceAll) ? 1 : 0, 0, enhanceAll ? 1 : -1, "--enhance-all",
        f0.enhanceAll, f1.enhanceAll, 1, false);
    enh("antialiasing", aaSamples, d.antiAliasing, aaFlag, "--aa", f0.antiAliasing, f1.antiAliasing,
        omk::kMaxAntiAliasing);
    enh("supersampling", ssaa, d.supersample, ssaaFlag, "--ssaa", f0.supersample, f1.supersample, 1, false);
    enh("texturefiltering", texFilter, d.textureFilter, filterFlag, "--filter", f0.textureFilter,
        f1.textureFilter, omk::kMaxTextureFilter);
    enh("anisotropy", texAniso, d.anisotropy, anisoFlag, "--anisotropy", f0.anisotropy, f1.anisotropy,
        omk::kMaxAnisotropy);
    enh("shadowquality", shadowQuality, d.shadowQuality, shadowQFlag, "--shadow-quality", f0.shadowQuality,
        f1.shadowQuality, omk::kAllShadowQuality);
    enh("lighting", lighting, d.lighting, lightingFlag, "--lighting", f0.lighting, f1.lighting,
        omk::kAllLighting);
    enh("uiscaling", uiScaling, d.uiScaling, uiScaleFlag, "--ui-scaling", f0.uiScaling, f1.uiScaling,
        omk::kMaxUiScaling);
    enh("textscaling", textScaling, d.textScaling, textScaleFlag, "--text-scaling", f0.textScaling,
        f1.textScaling, omk::kMaxTextScaling);
    enh("radar", radarAlways ? 1 : 0, d.radarAlways ? 1 : 0, radarFlag, "--radar", f0.radarAlways ? 1 : 0,
        f1.radarAlways ? 1 : 0, omk::kMaxRadar);
    enh("framerate", frameRate, d.frameRate, frameRateFlag, "--framerate", f0.frameRate, f1.frameRate,
        omk::kAllFrameRate);
    enh("animation", smoothAnim ? 1 : 0, d.smoothAnimation ? 1 : 0, smoothAnimFlag, "--smooth-anim",
        f0.smoothAnimation ? 1 : 0, f1.smoothAnimation ? 1 : 0, omk::kMaxAnimation);
    enh("clipdistance", unlimitedClip ? 1 : 0, 0, clipFlag ? (clipArg <= 0 ? 1 : 0) : -1, "--clip",
        f0.unlimitedDrawDistance ? 1 : 0, f1.unlimitedDrawDistance ? 1 : 0, omk::kAllUnlimitedDraw);
    // ---- [Remaster]: what the remaster mode runs with - the flag, the key,
    // the preset's - from the file, since `mode = original` left this run's
    // settings without them; what runs is this run's strength
    if (remasterBuilt()) {
        const bool preset0 = !remasterFlag.empty() || !f0.remasterDir.empty();
        const bool preset1 = !remasterFlag.empty() || !f1.remasterDir.empty();
        const auto rem = [&](const char* key, float running, float flag, const char* flagName,
                             float omk::Settings::*field, omk::Settings::Source omk::Settings::*src,
                             float off = 0.0f) {
            const std::string k = std::string("remaster.") + key;
            const bool presetKey = std::string(key) != "upscale";
            const auto conf = [&](const omk::Settings& f, bool preset) {
                const float def = preset && presetKey ? omk::remasterPresetValue(key) : off;
                return flag >= 0.0f ? flag : f.*src == omk::Settings::Source::Ini ? f.*field : def;
            };
            // what the remaster mode runs - held while the original shows (part B)
            row(k, remasterAtStart_.empty() ? conf(f1, preset1) : remasterRunning(key, running), conf(f0, preset0),
                conf(f1, preset1));
            o.setDefault(k, preset1 && presetKey ? omk::remasterPresetValue(key) : off);
            o.setHeld(k, flag >= 0.0f ? flagName : "");
        };
        using S = omk::Settings;
        row("remaster.mode", remasterShown_ ? 1 : 0, f0.remasterOn ? 1 : 0, f1.remasterOn ? 1 : 0);
        o.setDefault("remaster.mode", 1);
        rem("bloom", bloomStrength, bloomFlag, "--bloom", &S::bloom, &S::bloomSource);
        rem("water", waterStrength, waterFlag, "--water", &S::water, &S::waterSource);
        rem("floors", floorStrength, floorsFlag, "--floors", &S::floors, &S::floorsSource);
        rem("reflections", reflectLevels, reflectionsFlag, "--reflections", &S::reflections, &S::reflectionsSource,
            1.0f);
        rem("wet", wetStrength, wetFlag, "--wet", &S::wet, &S::wetSource);
        rem("rain3d", rain3dStrength, rain3dFlag, "--rain3d", &S::rain3d, &S::rain3dSource);
        rem("mist", mistStrength, mistFlag, "--mist", &S::mist, &S::mistSource);
        rem("relief", relief, reliefFlag, "--relief", &S::relief, &S::reliefSource);
        rem("ao", aoStrength, aoFlag, "--ao", &S::ao, &S::aoSource);
        rem("bodylight", bodyLight, bodyLightFlag, "--body-light", &S::bodyLight, &S::bodyLightSource);
        rem("fxlight", fxLight, fxLightFlag, "--fx-light", &S::fxLight, &S::fxLightSource);
        rem("smooth", smoothBodies, smoothFlag, "--smooth", &S::smooth, &S::smoothSource);
        rem("upscale", upscale > 1.0f ? upscale : 1.0f, upscaleFlag, "--upscale", &S::upscale, &S::upscaleSource,
            1.0f);
        rem("preload", preloadMetres, preloadFlag, "--preload", &S::preload, &S::preloadSource);
    }
    row("omk.language", settings.language == "en" ? 1 : 0, f0.language == "en" ? 1 : 0,
        f1.language == "en" ? 1 : 0);
    o.setDefault("omk.language", 0);
    // ---- [Restored]: the restored weather - its flag holds the row; reset
    // gives what the start gives, on under the remaster preset
    row("restored.weather", restoredWeather ? 1 : 0, f0.restoredWeather, f1.restoredWeather);
    o.setDefault("restored.weather", (!remasterFlag.empty() || !settings.remasterDir.empty()) && remasterBuilt() ? 1 : 0);
    o.setHeld("restored.weather", restoreWeatherFlag >= 0 ? "--restore-weather" : "");
}

// Each frame screen 35 is up: the changes written to the config, and the
// rows that can apply at once applied - the others wait for the next start.
void PlayState::omkOptionsServe() {
    if (!omkOpt_) return;
    auto& o = *omkOpt_;
    const auto edits = o.takeEdits();
    if (!edits.empty()) {
        const bool ok = omk::writeOptionsFile(configFile, edits);
        std::printf("options: OMK - %zu edit(s) %s %s\n", edits.size(), ok ? "written to" : "NOT written to",
                    configFile.c_str());
        omk::event("options.omk.write", {{"path", configFile}, {"edits", static_cast<long>(edits.size())},
                                         {"ok", ok}});
    }
    for (const std::string& key : o.takeChanged()) {
        const bool applied = omkApplyLive(key, o.value(key));
        if (applied) o.settle(key);
        omk::event("options.omk", {{"key", key}, {"value", o.iniValue(key)}, {"live", applied}});
        std::printf("options: %s = %s - %s\n", key.c_str(), o.iniValue(key).c_str(),
                    applied ? "applied" : "at the next start");
    }
}

// ONE CHANGE ON THE RUNNING GAME (`todo/graphics-menu.md` A4, 3a) - true when
// it is drawn from the next frame on, false when it waits for the next start.
// Each is what the start does with the same value, re-applied; a value the
// renderer cannot draw now is refused, as the start refuses it
// (`playsetup_bodies.cpp`'s lighting and shadow checks).
bool PlayState::omkApplyLive(const std::string& key, double v) {
    if (key == "omk.language") {
        settings.language = v > 0.5 ? "en" : "fr";
        omkText_.load(settings.language, textOverride(configFile));
    } else if (key == "enhancements.uiscaling") {
        uiScaling = static_cast<int>(v);
        comp_->setScaling(uiScaling);
        applyTextScale(dispW, dispH);
    } else if (key == "enhancements.textscaling") {
        textScaling = static_cast<int>(v);
        applyTextScale(dispW, dispH);
    } else if (key == "enhancements.radar") {
        radarAlways = v > 0.5;
    } else if (key == "enhancements.framerate") {
        frameRate = static_cast<int>(v);
    } else if (key == "enhancements.animation") {
        smoothAnim = v > 0.5;
        omk::setPoseSmoothing(smoothAnim);
    } else if (key == "enhancements.clipdistance") {
        // as `--clip 0` / the key does at start: no distance, and the fog,
        // whose range it is, off - or back to the options row's
        unlimitedClip = v > 0.5;
        clipInches = unlimitedClip ? std::numeric_limits<double>::infinity() : settings.clipInches();
        clipReport = true;
    } else if (key == "enhancements.shadowquality" || key == "enhancements.lighting") {
        // The blobs (classic, fitted) are the frontend's; per-pixel lighting
        // and the mapped pass are the renderer's. Vulkan builds both always;
        // GLES links them into its programs, so the set a START with the new
        // values would link is asked for - relinked (3b), the plain set again
        // when neither is wanted - and the change refused if it did not link.
        const bool isLight = key == "enhancements.lighting";
        const int toLighting = isLight ? static_cast<int>(v) : lighting;
        const int toShadow = isLight ? shadowQuality : static_cast<int>(v);
        const bool wantPix = toLighting > 0, wantMap = toShadow >= 2;
        if (world_ && (wantPix != world_->drawsPixelLights() || wantMap != world_->drawsShadowMap()))
            gpuRelinkLighting(wantPix, wantMap);   // false where there is nothing to relink
        if (wantPix && !(world_ && world_->drawsPixelLights())) return false;
        if (wantMap && !(world_ && world_->drawsShadowMap())) return false;
        lighting = toLighting;
        shadowQuality = toShadow;
    } else if (key == "remaster.ao" && !remasterShown_) {
        return remasterApplyLive("ao", static_cast<float>(v));   // held for the remaster's return (part B)
    } else if (key == "enhancements.antialiasing" || key == "enhancements.supersampling" ||
               key == "remaster.upscale" || (key == "remaster.ao" && !remasterApplyLive("ao", static_cast<float>(v)))) {
        // THE TARGET's (3c): MSAA, the supersample, the upscale - and the AO
        // when it was off at start (its strength alone moves without) - asked
        // again together, the target remade; refused where it cannot be
        // (Vulkan's MSAA: its pipelines are compiled for the sample count)
        const int toAa = key == "enhancements.antialiasing" ? static_cast<int>(v) : aaSamples;
        const int toSs = key == "enhancements.supersampling" ? static_cast<int>(v) : ssaa;
        const float toUp = key == "remaster.upscale" ? static_cast<float>(v) : std::max(upscale, 1.0f);
        const float toAo = key == "remaster.ao" ? static_cast<float>(v) : aoStrength;
        if (!gpuRetarget(toAa, toSs, toUp, toAo)) return false;
        aaSamples = toAa;
        ssaa = toSs;
        upscale = toUp;
        aoStrength = toAo;
    } else if (key == "enhancements.all") {
        // THE BASE under every enhancement key without its own (`all = max`,
        // applied last by the reader): the file as it is now resolved again,
        // and each value it moved applied as its own row would be - the change
        // applied only if every one of them was; the rows refreshed, so one
        // that waits keeps its `*`
        const omk::Settings f = omk::resolveSettings(omk::loadOptionsFile(configFile), std::nullopt);
        bool every = true;
        const unsigned takes = world_ ? world_->takes() : 0u;
        const auto to = [&](const char* k, int flag, double fileV, double running) {
            const std::string key = std::string("enhancements.") + k;
            // a key this renderer does not draw at all - its row not offered -
            // is the start's to ignore too
            const unsigned needs = key == "enhancements.antialiasing" ? omk::kTakesMultisample
                                 : key == "enhancements.anisotropy" ? omk::kTakesAnisotropy
                                 : key == "enhancements.texturefiltering" ? omk::kTakesTextureFilter : 0u;
            if ((takes & needs) != needs) return;
            fileV = omkReduce(key, fileV);   // what the start would run here
            if (flag >= 0 || fileV == running) return;   // a flag holds it, or nothing moves
            every = omkApplyLive(key, fileV) && every;
        };
        to("antialiasing", aaFlag, f.antiAliasing, aaSamples);
        to("texturefiltering", filterFlag, f.textureFilter, texFilter);
        to("anisotropy", anisoFlag, f.anisotropy, texAniso);
        to("shadowquality", shadowQFlag, f.shadowQuality, shadowQuality);
        to("lighting", lightingFlag, f.lighting, lighting);
        to("uiscaling", uiScaleFlag, f.uiScaling, uiScaling);
        to("textscaling", textScaleFlag, f.textScaling, textScaling);
        to("radar", radarFlag, f.radarAlways ? 1 : 0, radarAlways ? 1 : 0);
        to("framerate", frameRateFlag, f.frameRate, frameRate);
        to("animation", smoothAnimFlag, f.smoothAnimation ? 1 : 0, smoothAnim ? 1 : 0);
        to("clipdistance", clipFlag ? 1 : -1, f.unlimitedDrawDistance ? 1 : 0, unlimitedClip ? 1 : 0);
        settings.enhanceAll = f.enhanceAll;
        omkOptionsOpen();
        return every;
    } else if (key == "enhancements.texturefiltering" || key == "enhancements.anisotropy") {
        // THE TEXTURES' (3d): the filter and the anisotropy are baked into the
        // uploads on Vulkan and GLES - `retexture` drops them, and the pool is
        // handed over again this frame as a new composition (GL1 and the 3DS
        // re-filter in place)
        const int toFilter = key == "enhancements.texturefiltering" ? static_cast<int>(v) : texFilter;
        const int toAniso = key == "enhancements.anisotropy" ? static_cast<int>(v) : texAniso;
        if (!world_ || !world_->retexture(toFilter, toAniso)) return false;
        texFilter = toFilter;
        texAniso = toAniso;
        ++texPool.composition;
    } else if (key == "remaster.ao") {
        // its strength moved where the target already reads the depth
    } else if (key.rfind("remaster.", 0) == 0) {
        return remasterApplyLive(key.substr(9), static_cast<float>(v));
    } else {
        return false;
    }
    return true;
}

// A CONFIGURED VALUE THE RENDERER CANNOT DRAW, reduced as the start reduces it
// (`playsetup_bodies.cpp`: mapped shadows to fitted, per-pixel lighting to per
// vertex; GL1 and the 3DS draw trilinear bilinear) - by what the renderer can
// draw at all (`Renderer::takes`), so the menu shows what WILL run, and `all`
// applies it
double PlayState::omkReduce(const std::string& key, double v) const {
    const unsigned t = world_ ? world_->takes() : 0u;
    if (key == "enhancements.shadowquality" && v > 1.5 && !(t & omk::kTakesShadowMap)) return 1;
    if (key == "enhancements.lighting" && v > 0.5 && !(t & omk::kTakesPixelLights)) return 0;
    if (key == "enhancements.texturefiltering" && v > 1.5 && !(t & omk::kTakesTrilinear))
        return (t & omk::kTakesTextureFilter) ? 1 : v;
    return v;
}

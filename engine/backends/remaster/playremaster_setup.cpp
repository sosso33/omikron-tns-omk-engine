// SPDX-License-Identifier: GPL-3.0-or-later
// THE REMASTER'S SETUP (`todo/remaster.md`) - the packs, the strengths and
// the `--remaster` preset, resolved once from the flags and `[Remaster]`
// keys; moved out of `playsetup_session.cpp` so a build without
// OMK_REMASTER carries none of it (`backends/sdl/playremaster_off.cpp`).
#if OMK_REMASTER

#include "../sdl/playstate.h"

#include "o3de/colourgrade.h"
#include "o3de/emitterlight.h"
#include "o3de/texpack.h"
#include "platform/events.h"
#include "platform/filmpack.h"
#include "platform/hostfs.h"
#include "ui/fontpack.h"
#include "ui/uipack.h"

#include <cstdio>
#include <string>

// OMK's options menu offers its Remaster page (`playomkoptions.cpp`)
bool PlayState::remasterBuilt() const { return true; }

void PlayState::remasterSetup() {
    // `[Remaster] mode = original` (OMK's options menu, `todo/graphics-menu.md`
    // B): everything ARMED as the remaster mode would, and the original's look
    // shown - the packs hidden from here, before the first bitmap and pool;
    // the strengths put away at the first frame, once the renderer has built
    // their passes (`remasterModeFrame`), so F10 brings them back. A remaster
    // flag on the command line is the most recent word: it shows the remaster.
    if (!settings.remasterOn && !opt.anyRemaster()) {
        remasterStartHidden_ = true;
        omk::texturePack().setShown(false);
        omk::uiPack().setShown(false);
        omk::fontPack().setShown(false);
        std::printf("remaster: [Remaster] mode = original - the remaster armed and hidden (F10 or a pad's SELECT shows it)\n");
    } else if (!settings.remasterOn) {
        settings.remasterOn = true;
        std::printf("remaster: [Remaster] mode = original, but a remaster flag on the command line - "
                    "the remaster shown\n");
    }
    // the preset's strengths (`settings.h` kRemasterPreset), and as text for its line
    const auto pv = [](const char* key) { return omk::remasterPresetValue(key); };
    const auto pvText = [](const char* key) {
        char b[16];
        std::snprintf(b, sizeof b, "%g", static_cast<double>(omk::remasterPresetValue(key)));
        return std::string(b);
    };
    // THE TEXTURE PACK (`o3de/texpack.h`, `todo/remaster.md` B4) - a REMASTER
    // feature, not an enhancement: it replaces authored pixels. Opened once,
    // here, before any pool reaches a renderer; the GPU backends consult it.
    {
        // THE REMASTER PRESET (`--remaster DIR`, `[Remaster] dir`): a BASE
        // under the three options - a specific flag, then a specific key,
        // then the preset's folder
        const std::string preset = !remasterFlag.empty() ? remasterFlag : settings.remasterDir;
        std::string presetPack, presetGrades;
        if (!preset.empty()) {
            for (const char* sub : {"/pack", "/full/pack"})
                if (presetPack.empty() && omk::hostfs::isDirectory(preset + sub)) presetPack = preset + sub;
            if (omk::hostfs::isDirectory(preset + "/grades")) presetGrades = preset + "/grades";
            std::printf("remaster: %s - the preset: texture pack %s, bloom %s, grades %s, water %s, body light %s, "
                        "fx light %s, relief %s, ao %s, floors %s, wet %s, rain3d %s, mist %s, smooth %s (a specific flag or key beats each)\n", preset.c_str(),
                        presetPack.empty() ? "none found" : presetPack.c_str(),
                        bloomFlag >= 0.0f || settings.bloomSource == omk::Settings::Source::Ini ? "as set" : pvText("bloom").c_str(),
                        presetGrades.empty() ? "none found" : presetGrades.c_str(),
                        waterFlag >= 0.0f || settings.waterSource == omk::Settings::Source::Ini ? "as set" : pvText("water").c_str(),
                        bodyLightFlag >= 0.0f || settings.bodyLightSource == omk::Settings::Source::Ini ? "as set" : pvText("bodylight").c_str(),
                        fxLightFlag >= 0.0f || settings.fxLightSource == omk::Settings::Source::Ini ? "as set" : pvText("fxlight").c_str(),
                        reliefFlag >= 0.0f || settings.reliefSource == omk::Settings::Source::Ini ? "as set" : pvText("relief").c_str(),
                        aoFlag >= 0.0f || settings.aoSource == omk::Settings::Source::Ini ? "as set" : pvText("ao").c_str(),
                        floorsFlag >= 0.0f || settings.floorsSource == omk::Settings::Source::Ini ? "as set" : pvText("floors").c_str(),
                        wetFlag >= 0.0f || settings.wetSource == omk::Settings::Source::Ini ? "as set" : pvText("wet").c_str(),
                        rain3dFlag >= 0.0f || settings.rain3dSource == omk::Settings::Source::Ini ? "as set" : pvText("rain3d").c_str(),
                        mistFlag >= 0.0f || settings.mistSource == omk::Settings::Source::Ini ? "as set" : pvText("mist").c_str(),
                        smoothFlag >= 0.0f || settings.smoothSource == omk::Settings::Source::Ini ? "as set" : pvText("smooth").c_str());
            omk::event("remaster.preset", {{"dir", preset}, {"pack", presetPack}, {"grades", presetGrades}});
        }
        const std::string packDir = !texturePackFlag.empty() ? texturePackFlag
                                  : !settings.texturePack.empty() ? settings.texturePack : presetPack;
        if (!packDir.empty()) omk::openTexturePack(packDir);
        // THE INTERFACE PACK (`ui/uipack.h`): its flag, its key, the preset's
        // `ui/pack` - opened before the first bitmap loads (the splash's)
        {
            std::string uiDir = !uiPackFlag.empty() ? uiPackFlag : settings.uiPack;
            if (uiDir.empty() && !preset.empty() && omk::hostfs::isDirectory(preset + "/ui/pack"))
                uiDir = preset + "/ui/pack";
            if (!uiDir.empty()) {
                const bool ok = omk::uiPack().open(uiDir);
                std::printf("ui pack: %s - %s\n", uiDir.c_str(),
                            ok ? (std::to_string(omk::uiPack().files()) + " upscaled bitmaps, a REMASTER feature").c_str()
                               : "no <key>.png there - off");
                omk::event("ui.pack", {{"dir", uiDir}, {"files", static_cast<long>(omk::uiPack().files())}});
            }
        }
        // THE FILM PACK (`platform/filmpack.h`): its flag, its key, the
        // preset's `films/pack` - the intro films played from it
        {
            std::string filmDir = !filmPackFlag.empty() ? filmPackFlag : settings.filmPack;
            if (filmDir.empty() && !preset.empty() && omk::hostfs::isDirectory(preset + "/films/pack"))
                filmDir = preset + "/films/pack";
            if (!filmDir.empty()) {
                const bool ok = omk::filmPack().open(filmDir);
                std::printf("film pack: %s - %s\n", filmDir.c_str(),
                            ok ? (std::to_string(omk::filmPack().files()) + " upscaled films, a REMASTER feature").c_str()
                               : "no <key>.mpg there - off");
            }
        }
        // THE FONT PACK (`ui/fontpack.h`): its flag, its key, the preset's
        // `fonts/pack` - drawn only where `textscaling fit` enlarges the text
        {
            std::string fontDir = !fontPackFlag.empty() ? fontPackFlag : settings.fontPack;
            if (fontDir.empty() && !preset.empty() && omk::hostfs::isDirectory(preset + "/fonts/pack"))
                fontDir = preset + "/fonts/pack";
            if (!fontDir.empty()) {
                const bool ok = omk::fontPack().open(fontDir);
                std::printf("font pack: %s - %s\n", fontDir.c_str(),
                            ok ? (std::to_string(omk::fontPack().files()) + " faces drawn finer when the text is "
                                  "enlarged, a REMASTER feature").c_str()
                               : "no <key>.png there - off");
                omk::event("font.pack", {{"dir", fontDir}, {"files", static_cast<long>(omk::fontPack().files())}});
            }
        }
        // ...and its post-pass: the bloom from the pack's light maps, the
        // grade per city (`o3de/colourgrade.h`), each its own option
        bloomStrength = bloomFlag >= 0.0f ? bloomFlag
                      : settings.bloomSource == omk::Settings::Source::Ini ? settings.bloom
                      : !preset.empty() ? pv("bloom") : 0.0f;
        // the light maps decoded with the colours only for the bloom (the
        // surface lights read them on their own)
        omk::texturePack().setLights(bloomStrength > 0.0f);
        if (bloomStrength > 0.0f)
            std::printf("bloom: %.2f - a REMASTER feature: a glow from the texture pack's light maps "
                        "(%zu of them), drawn by Vulkan and GLES only\n",
                        static_cast<double>(bloomStrength), omk::texturePack().lightMaps());
        // the MIST (`View::mist`): its flag, its key, the preset's 0.5 (the
        // reader's, 2026-10-21, from a sheet of 0, 0.5 and 1 in three cities)
        mistStrength = mistFlag >= 0.0f ? mistFlag
                     : settings.mistSource == omk::Settings::Source::Ini ? settings.mist
                     : !preset.empty() ? pv("mist") : 0.0f;
        // THE RESTORED WEATHER on under the preset (the reader's "enable it by
        // default in remaster mode, with the possibility to disable it",
        // 2026-10-27) - unless `--restore-weather` or [Restored] weather says
        if (!preset.empty() && restoreWeatherFlag < 0 &&
            settings.restoredWeatherSource != omk::Settings::Source::Ini && !restoredWeather) {
            restoredWeather = true;
            std::printf("remaster: the preset turns the RESTORED weather on - Qalisar's and Lahoreh's showers, "
                        "the sand; --restore-weather 0 or [Restored] weather = 0 turns it off\n");
        }
        // THE GAME'S OWN *Niveau de detail* at 2 under the preset (the reader's
        // "ok, continue", 2026-10-27): the city weather runs only there
        // (`cmp byte ptr [0x90E727], 2`, docs/ASSETS.md 4e) and the boot
        // default is 1, so the remaster's rain - its wet floors, its 3D rain,
        // its sky - would never be seen. Unless `--detail` or the ini's
        // [Options] levelofdetail names it; option row 7 still lowers it in
        // play, live. It is the original's option at its top, not a look:
        // F10's original mode keeps it, and with it the original's 2D rain.
        if (!preset.empty() && detailFlag < 0 && settings.levelOfDetail != omk::Settings::Source::Ini &&
            settings.v.levelOfDetail < 2) {
            std::printf("remaster: the preset raises Niveau de detail %d -> 2 (option row 7), where the game's "
                        "own weather runs; --detail or [Options] levelofdetail holds it\n",
                        settings.v.levelOfDetail);
            settings.v.levelOfDetail = 2;
            shadowDetail = 2;
            omk::event("remaster.detail", {{"level", 2}});
        }
        if (mistStrength > 0.0f)
            std::printf("mist: %.2f - a REMASTER feature: a height fog in the set's fog colour, drawn by "
                        "Vulkan and GLES\n", static_cast<double>(mistStrength));
        // THE PRELOAD RADIUS (`backends/sdl/playpreload.cpp`): its flag, its
        // key, the preset's 15 (a memory's choice, but the remaster's packs
        // are what make an arrival cost - `--preload 0` gives it back; 25
        // until the measurement of 2026-10-26, `todo/remaster.md`: the M1
        // prepares and uploads a set in ~0.5 s, and 15 m leaves 1.1-1.9 s of
        // it spare at a run)
        preloadMetres = preloadFlag >= 0.0f ? preloadFlag
                      : settings.preloadSource == omk::Settings::Source::Ini ? settings.preload
                      : !preset.empty() ? pv("preload") : 0.0f;
        if (preloadMetres > 0.0f)
            std::printf("preload: %.0f m - a REMASTER feature: the sets behind the doors within %.0f m (and behind "
                        "the airlock or corridor each opens on) prepared on a thread and their textures put on the "
                        "GPU ahead, released past %.0f m\n", static_cast<double>(preloadMetres),
                        static_cast<double>(preloadMetres), static_cast<double>(preloadMetres) + 10.0);
        // THE UPSCALE (`Renderer::setUpscale`): its flag, its key - not the
        // preset's look, a frame rate's choice: the PLATFORM's under the
        // preset (`kPresetUpscale`, 1.5 on the Switch, 1 elsewhere)
        upscale = upscaleFlag >= 0.0f ? upscaleFlag
                : settings.upscaleSource == omk::Settings::Source::Ini ? settings.upscale
                : !preset.empty() ? omk::kPresetUpscale : 1.0f;
        if (upscale > 1.0f)
            std::printf("upscale: x%.2f - a REMASTER feature: the frame drawn smaller and brought up by "
                        "AMD FSR 1, Vulkan and GLES\n", static_cast<double>(upscale));
        // the REFLECTING FLOORS (`o3de/floorreflect.h`): its flag, its key, the
        // preset's 1 - each floor's own strength is the list's
        floorStrength = floorsFlag >= 0.0f ? floorsFlag
                      : settings.floorsSource == omk::Settings::Source::Ini ? settings.floors
                      : !preset.empty() ? pv("floors") : 0.0f;
        // ...and how many LEVELS mirror a frame (`chooseFloorSecond`): its
        // flag, its key, the preset's 2 - else 1, one plane as before
        reflectLevels = reflectionsFlag >= 0.0f ? reflectionsFlag
                      : settings.reflectionsSource == omk::Settings::Source::Ini ? settings.reflections
                      : !preset.empty() ? pv("reflections") : 1.0f;
        if (reflectLevels != 1.0f)
            std::printf("reflections: %.0f - a REMASTER feature: %s\n", static_cast<double>(reflectLevels),
                        reflectLevels >= 2.0f ? "a second floor level in view mirrors too, its own pass"
                                              : "no floor mirrors (a wet one keeps its film)");
        // the WET FLOORS (`o3de/wetfloor.h`): its flag, its key, the preset's 1 -
        // no list: every level floor under the open sky, in a rain area
        wetStrength = wetFlag >= 0.0f ? wetFlag
                    : settings.wetSource == omk::Settings::Source::Ini ? settings.wet
                    : !preset.empty() ? pv("wet") : 0.0f;
        if (wetStrength > 0.0f)
            std::printf("wet: %.2f - a REMASTER feature: the game's rain wets the floors under the open sky, "
                        "drawn by Vulkan and GLES only; the rain needs option row 7 at 2\n",
                        static_cast<double>(wetStrength));
        // THE RAIN IN 3D (`playremaster_rain.cpp`): its flag, its key, the preset's 1
        rain3dStrength = rain3dFlag >= 0.0f ? rain3dFlag
                       : settings.rain3dSource == omk::Settings::Source::Ini ? settings.rain3d
                       : !preset.empty() ? pv("rain3d") : 0.0f;
        if (rain3dStrength > 0.0f)
            std::printf("rain3d: %.2f - a REMASTER feature: the game's rain drawn in the scene, hidden by walls "
                        "and stopped by roofs, Vulkan and GLES only; the rain needs option row 7 at 2\n",
                        static_cast<double>(rain3dStrength));
        // the WATER reflection: its flag, its key, then the preset's 0.6 (0.35 before the Fresnel, 2026-10-20)
        waterStrength = waterFlag >= 0.0f ? waterFlag
                      : settings.waterSource == omk::Settings::Source::Ini ? settings.water
                      : !preset.empty() ? pv("water") : 0.0f;
        if (waterStrength > 0.0f)
            std::printf("water: %.2f - a REMASTER feature: the water reflects the scene, drawn by "
                        "Vulkan and GLES only\n", static_cast<double>(waterStrength));
        // the BODY LIGHT (`o3de/lightprobe.h`): its flag, its key, the preset's 1 (the reader's, 2026-10-20)
        bodyLight = bodyLightFlag >= 0.0f ? bodyLightFlag
                  : settings.bodyLightSource == omk::Settings::Source::Ini ? settings.bodyLight
                  : !preset.empty() ? pv("bodylight") : 0.0f;
        // the EMITTERS' LIGHT (`o3de/emitterlight.h`): its flag, its key, the preset's 1
        fxLight = fxLightFlag >= 0.0f ? fxLightFlag
                : settings.fxLightSource == omk::Settings::Source::Ini ? settings.fxLight
                : !preset.empty() ? pv("fxlight") : 0.0f;
        // the RELIEF (`View::relief`): its flag, its key, the preset's 2 (the
        // reader's, 2026-10-21: 1 on the brightness beside 0, 2 and 4; 2 once
        // the pack's generated normal maps came, "relief 2 is better")
        relief = reliefFlag >= 0.0f ? reliefFlag
               : settings.reliefSource == omk::Settings::Source::Ini ? settings.relief
               : !preset.empty() ? pv("relief") : 0.0f;
        // the ROUNDED BODIES (`o3de/smoothbody.h`): its flag, its key, the
        // preset's 0.75 (the reader's "do it by default", 2026-10-22 - every
        // character not yet seen, so --smooth-skip stays the way out)
        smoothBodies = smoothFlag >= 0.0f ? smoothFlag
                     : settings.smoothSource == omk::Settings::Source::Ini ? settings.smooth
                     : !preset.empty() ? pv("smooth") : 0.0f;
        if (smoothBodies > 0.0f)
            std::printf("smooth: %.2f - a REMASTER feature: the characters rounded, each triangle split 3x3 "
                        "(Phong tessellation), every backend\n", static_cast<double>(smoothBodies));
        // ...and the models it leaves alone: its flag, its key - a list of
        // names, commas or spaces between, any case
        {
            const std::string& list = !smoothSkipFlag.empty() ? smoothSkipFlag : settings.smoothSkip;
            std::string name;
            for (std::size_t i = 0; i <= list.size(); ++i) {
                const char ch = i < list.size() ? list[i] : ',';
                if (ch == ',' || ch == ' ' || ch == '\t') {
                    if (!name.empty()) smoothSkip.insert(name);
                    name.clear();
                } else {
                    name += static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
                }
            }
            if (smoothBodies > 0.0f && !smoothSkip.empty()) {
                std::printf("smooth: left as they are -");
                for (const auto& n : smoothSkip) std::printf(" %s", n.c_str());
                std::printf("\n");
            }
        }
        // the AMBIENT OCCLUSION: its flag, its key, the preset's 2 (the
        // reader's, 2026-10-21: confirmed in play at 2, then "add ao 2 to the
        // preset")
        aoStrength = aoFlag >= 0.0f ? aoFlag
                   : settings.aoSource == omk::Settings::Source::Ini ? settings.ao
                   : !preset.empty() ? pv("ao") : 0.0f;
        if (aoStrength > 0.0f)
            std::printf("ao: %.2f - a REMASTER feature: ambient occlusion at contacts, from the frame's depth, "
                        "drawn by Vulkan only\n", static_cast<double>(aoStrength));
        if (relief > 0.0f) {
            // ...from the pack's NORMAL MAPS where it has them (B3, `<key>_n.png`),
            // the brightness elsewhere
            omk::texturePack().setNormals(true);
            std::printf("relief: %.2f - a REMASTER feature: the textures' relief under the emitters' lights, "
                        "%zu normal maps in the pack (the brightness elsewhere), drawn by Vulkan only\n",
                        static_cast<double>(relief), omk::texturePack().normalMaps());
        }
        if (fxLight > 0.0f)
            std::printf("fx light: %.2f - a REMASTER feature: the set's flames and glows light the bodies "
                        "near them, drawn by Vulkan and GLES only\n", static_cast<double>(fxLight));
        if (bodyLight > 0.0f)
            std::printf("body light: %.2f - a REMASTER feature: the bodies take the set's painted light "
                        "around them, drawn by Vulkan and GLES only\n", static_cast<double>(bodyLight));
        const std::string gradeDir = !gradeFlag.empty() ? gradeFlag
                                   : !settings.grade.empty() ? settings.grade : presetGrades;
        if (!gradeDir.empty() && omk::cityGrades().open(gradeDir))
            std::printf("grade: %s - %zu city grades, a REMASTER feature, drawn by Vulkan and GLES only; "
                        "off while the greyscale bank is on\n", gradeDir.c_str(), omk::cityGrades().size());
        // ...and beside them the CURATED LIGHT SOURCES (`o3de/emitterlight.h`
        // SurfaceCuration): which textures, which parts of them, cast light
        // ...and the FLOORS that reflect (`floors.txt`, the same folder): no
        // list, no floors
        if (floorStrength > 0.0f) {
            if (!gradeDir.empty() && omk::floorList().open(gradeDir + "/floors.txt"))
                std::printf("floors: %.2f - a REMASTER feature: %zu floors of %s/floors.txt reflect the scene, "
                            "drawn by Vulkan and GLES only\n", static_cast<double>(floorStrength),
                            omk::floorList().size(), gradeDir.c_str());
            else {
                std::printf("floors: no floors.txt beside the grades - nothing reflects\n");
                floorStrength = 0.0f;
            }
        }
        if (fxLight > 0.0f && !gradeDir.empty() && omk::surfaceCuration().open(gradeDir + "/lights.txt"))
        {
            std::printf("fx light: %s/lights.txt - %zu curated light sources\n", gradeDir.c_str(),
                        omk::surfaceCuration().size());
            // ...which BLOOM too, where the light maps have nothing for them
            if (bloomStrength > 0.0f) omk::texturePack().setLightHook(&omk::curateBloom, &omk::bloomCurated);
        }
    }
    // each strength as the start set it - what OMK's options menu may move live
    // (`remasterApplyLive`) and the switch brings back: only one ON here had
    // its resources loaded
    remasterAtStart_ = {{"bloom", bloomStrength}, {"water", waterStrength}, {"floors", floorStrength},
                        {"wet", wetStrength}, {"rain3d", rain3dStrength}, {"mist", mistStrength}, {"relief", relief}, {"ao", aoStrength},
                        {"bodylight", bodyLight}, {"fxlight", fxLight}};
}

#endif  // OMK_REMASTER

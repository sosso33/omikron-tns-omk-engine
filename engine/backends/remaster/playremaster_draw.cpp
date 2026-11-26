// SPDX-License-Identifier: GPL-3.0-or-later
// THE REMASTER'S DRAW (`todo/remaster.md`) - the mist, the emitters' point
// lights (A5b/A5c) with the night and the relief, and the water and floor
// reflections (A3); cut out of `playframe_world_draw.cpp`'s phases so a build
// without OMK_REMASTER carries none of it (`backends/sdl/playremaster_off.cpp`).
#if OMK_REMASTER

#include "../sdl/playstate.h"

#include "o3de/colourgrade.h"
#include "o3de/emitterlight.h"
#include "o3de/floorreflect.h"
#include "o3de/wetfloor.h"
#include "o3de/lighttiles.h"
#include "platform/env.h"
#include "platform/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
constexpr float kShowerMist = 0.1f;        // the mist the full shower adds (0.3 too dark in Anekbah's own fog)
// the veils' and the dust's colour lit as the set's ambient: this much at
// none, `kAirAmbientGain` more per unit of its grey
constexpr float kAirAmbientMin = 0.25f, kAirAmbientGain = 1.6f;
constexpr float kVeilGrey[3] = {0.62f, 0.66f, 0.72f};   // the rain's veils, a light cold grey
constexpr double kVeilFallPerMs = 1.5;     // ...falling 1500 units a second
constexpr double kVeilFallWrap = 1.4e6;    // ...wrapped here, so the float keeps its precision
constexpr float kVeilScale = 2500.0f;      // ...63% thick at full shower this far off
constexpr float kDustOchre[3] = {0.80f, 0.64f, 0.42f};  // the sand's dust
// THE SPLASHES OFF THE BODIES (`remasterWetBodies`, the weather list's 5)
constexpr double kBodyTall = 70.0;         // a body posed on the GPU: its head this far above its feet (Kay'l 71)
constexpr double kBodyFloorReach = 200.0;  // ...its feet the floor found this far below its root
constexpr float kBodyStrideJump = 60.0f;   // ...moved more in a frame: placed, not walked
constexpr double kBodySplashStepMax = 100.0;   // the most ms of splashes one frame spawns (a hitch)
constexpr double kBodyShowerFull = 200.0;  // the full shower's drops (`o3de/weather.h`): the head's rate's 1
constexpr float kFootStride = 26.0f;       // a crown at the feet every this far walked (a step)
constexpr float kFootSide = 4.5f;          // ...this far either side of the way, foot by foot
constexpr float kFootAhead = 0.0f;         // ...and this far ahead of the root (6: in front of the foot, the leg hid it from behind)
constexpr float kFootSplashSize = 2.5f;    // ...wider than the foot, so it shows past it from behind
constexpr float kFootSplashGlow = 3.0f;    // ...and brighter (a drop's own glow, and 2: too faint on a bright wet floor)
constexpr double kHeadSplashRate = 10.0;   // the drops bursting on a head a second at the full shower
constexpr float kHeadSplashSpread = 7.0f;  // ...within this of the body's middle
constexpr float kHeadSplashDrop = 10.0f;   // ...from its top to this far down (the shoulders)
constexpr float kHeadSplashSize = 0.45f;   // ...a drop's crown this much its size
constexpr float kHeadSplashGlow = 1.5f;    // ...and this much its brightness
constexpr double kDustDriftPerMs = 0.70;   // ...drifting 700 units a second with the wind (300 read as still)
constexpr double kDustRisePerMs = 0.04;    // ...and slowly across
constexpr double kDustDriftWrap = 4e5;     // ...wrapped here
constexpr float kDustScale = 1100.0f;      // ...63% at full storm this far (1400 before it came nearer)
constexpr float kStreakCutFade = 0.35f;    // the lights' streaks faded over this share of the cut's distance
}  // namespace

// THE MIST (`View::mist`): on the shown set's ground
void PlayState::remasterViewMist(omk::View& v) {
    // ...thickened by the SHOWER while the 3D rain shows (the shower's sky,
    // `remasterGrade`): the city's fog colour closing in as it rains
    v.mist = std::min(1.0f, mistStrength + (rain3dStrength > 0.0f && remasterShown_
                                                ? kShowerMist * std::clamp(weatherRain * rain3dStrength, 0.0f, 1.0f) : 0.0f));
    v.mistGround = worldSlots[static_cast<std::size_t>(session_->shownSlot() & 1)].ground;
    // ...and the RAIN's ripples on the wet floors (`o3de/wetfloor.h`): the
    // shower's strength, which is 0 outside a rain area
    v.rain = wetStrength > 0.0f ? weatherRain : 0.0f;
    // ...and THE SAND'S DUST (the restored weather's, with the 3D weather -
    // the reader's "a moving fog effect and small particles", 2026-10-27):
    // as thick as the storm is strong, its clouds drifting with the wind
    // (-x, the arm's) on the weather's own clock - held while paused - in an
    // ochre lit as the set's ambient
    // ...and THE RAIN'S VEILS (with the 3D weather - the reader's "more
    // realistic rain", 2026-10-28): far rain as falling sheets, as thick as
    // the shower, falling 1500 units a second on the weather's clock (held
    // while paused), a light grey lit as the set's ambient
    // THE PUDDLES on the wet floors - `OMK_NO_PUDDLES=1`, an INSTRUMENT, an
    // even film instead (`verify.py: engine: wet floors` part 9)
    const bool noPuddles = omk::envFlag("OMK_NO_PUDDLES");   // read each frame: the sweep switches it
    v.puddles = noPuddles ? 0.0f : 1.0f;
    // ...and THE LIGHTS' STREAKS on them (the reader's "go with 1", 2026-10-28):
    // their lights picked with the emitters' (`remasterPointLights`), which
    // run under per-pixel light alone - `OMK_NO_STREAKS=1`, an INSTRUMENT,
    // none (`verify.py: engine: wet floors` part 14)
    const bool noStreaks = omk::envFlag("OMK_NO_STREAKS");   // read each frame: the sweep switches it
    v.streaks = !noStreaks && remasterShown_ ? wetStrength : 0.0f;
    v.streakLights.clear();
    v.veil = 0.0f;
    v.veilGust = 0.0f;
    // `OMK_NO_VEIL=1`, an INSTRUMENT: the veils alone left out
    // (`verify.py: engine: rain 3d` part 13)
    const bool noVeil = omk::envFlag("OMK_NO_VEIL");   // read each frame: the sweep switches it
    if (!noVeil && rain3dStrength > 0.0f && remasterShown_ && weatherRain > 0.0f) {
        const WorldSlot& ws = worldSlots[static_cast<std::size_t>(session_->shownSlot() & 1)];
        const float amb = std::clamp(kAirAmbientMin + kAirAmbientGain * static_cast<float>(ws.ambientGrey) / 255.0f,
                                     kAirAmbientMin, 1.0f);
        v.veil = std::clamp(weatherRain * rain3dStrength, 0.0f, 1.0f);
        v.veilFall = static_cast<float>(std::fmod(weatherClockMs * kVeilFallPerMs, kVeilFallWrap));
        v.veilScale = kVeilScale;
        for (int k = 0; k < 3; ++k) v.veilColour[k] = kVeilGrey[k] * amb;
        v.veilGust = remasterGust(weatherClockMs, v.cam.eye[0], weatherRain);   // the gust where the eye is
    }
    // ...and THE HALOS in the rain (with the 3D weather - the reader's "go" on
    // the weather list's 2, 2026-10-28): the lights' bloom spread by the wet
    // air, as strong as the shower. `OMK_NO_HALO=1`, an INSTRUMENT, leaves
    // the bloom as it is dry (`verify.py: engine: rain 3d` part 15)
    const bool noHalo = omk::envFlag("OMK_NO_HALO");   // read each frame: the sweep switches it
    v.halo = !noHalo && rain3dStrength > 0.0f && remasterShown_
        ? std::clamp(weatherRain * rain3dStrength, 0.0f, 1.0f) : 0.0f;
    v.dust = 0.0f;
    // `OMK_NO_DUST=1`, an INSTRUMENT: the dust fog alone left out, the grains
    // and the grade kept (`verify.py: engine: rain 3d` part 11)
    const bool noDust = omk::envFlag("OMK_NO_DUST");   // read each frame: the sweep switches it
    if (!noDust && restoredWeather && rain3dStrength > 0.0f && remasterShown_ && weatherSandLevel > 0.0f) {
        const WorldSlot& ws = worldSlots[static_cast<std::size_t>(session_->shownSlot() & 1)];
        const float amb = std::clamp(kAirAmbientMin + kAirAmbientGain * static_cast<float>(ws.ambientGrey) / 255.0f,
                                     kAirAmbientMin, 1.0f);
        v.dust = std::clamp(weatherSandLevel * rain3dStrength, 0.0f, 1.0f);
        v.dustDrift[0] = static_cast<float>(std::fmod(weatherClockMs * kDustDriftPerMs, kDustDriftWrap));
        v.dustDrift[1] = static_cast<float>(std::fmod(weatherClockMs * kDustRisePerMs, kDustDriftWrap));
        v.dustScale = kDustScale;
        for (int k = 0; k < 3; ++k) v.dustColour[k] = kDustOchre[k] * amb;
    }
}

// THE EMITTERS' LIGHTS under per-pixel lighting: the night, the relief, the
// reach - `fxPix` whether they light at all, `kFxView` how far they are taken;
// -> the slots they take among the frame's lights (0 when TILED: they go to
// `View::points` instead)
int PlayState::remasterViewLights(omk::View& v, bool& fxPix, float& kFxView) {
    // they light the SET too (mode 3, `remasterLitSet`), so they are chosen by
    // the CAMERA - the nearest slots within the reach, faded in over its last
    // quarter so none pops - and the set's own lights, nearest the player as
    // before, take the slots left
    fxPix = fxLight > 0.0f && world_ && world_->drawsEmitterLights();
    // ...and THE NIGHT (`CityGrades::nightOf`): how much of the set's painted
    // light stays under them
    v.setDim = fxPix && omk::cityGrades().active() ? omk::cityGrades().nightOf(worldSet) : 1.0f;
    if (v.setDim != nightWas) {
        nightWas = v.setDim;
        std::printf("frame %ld: the night %.2f for %s - the set's painted light kept under the emitters' "
                    "lights (`night.txt`)\n", frameNo, static_cast<double>(v.setDim), worldSet.c_str());
        omk::event("remaster.night", {{"set", worldSet}, {"dim", static_cast<double>(v.setDim)}});
    }
    // ...and on a renderer that TILES them (`o3de/lighttiles.h`), every one
    // within the game's CLIP DISTANCE (the reader, 2026-10-20: "set it
    // depending of the game distance culling setting"; 200 m at most, the
    // unlimited clip's), faded over its last fifth - no slots, no cut
    // (`OMK_NO_TILES=1`, an instrument: the nearest-slots way on a renderer
    // that tiles - to set Vulkan beside GLES, which does not yet)
    static const bool noTiles = omk::envFlag("OMK_NO_TILES");
    fxTiled = fxPix && world_->tilesLights() && !noTiles;
    v.relief = fxTiled ? relief : 0.0f;   // the relief rides the tiled module
    kFxView = fxTiled ? std::min(20000.0f, static_cast<float>(clipInches)) : 3000.0f;
    v.points.clear();
    // the point lights' slots: what the renderer has past four of the set's
    // own (60 of 64 on Vulkan and the GLES variant - 12 of 16 left most of a
    // neon street's signs unlit, "as if turned off", 2026-10-20)
    return fxTiled ? 0 : std::max(0, world_->maxPixelLights() - 4);
}

// ...and the ones picked, nearest the camera first: TILED all of them into
// `View::points` (faded over the reach's last fifth); else the nearest
// `kFxSlots`, `fxCut` the first left out
void PlayState::remasterPointLights(omk::View& v, std::vector<std::pair<float, const omk::Light3do*>>& fx,
                                    float kFxView, int kFxSlots, float& fxCut) {
    const auto gpu = [](const omk::Light3do& l, float fade) {
        omk::View::GpuLight g{};
        for (int k = 0; k < 3; ++k) { g.pos[k] = l.pos[k]; g.dir[k] = l.dir[k]; }
        g.radiusA = l.radiusA; g.radiusB = l.radiusB;
        g.colour[0] = static_cast<float>((l.colour >> 16) & 0xFF) / 255.0f;
        g.colour[1] = static_cast<float>((l.colour >> 8) & 0xFF) / 255.0f;
        g.colour[2] = static_cast<float>(l.colour & 0xFF) / 255.0f;
        g.intensity = l.f32 * fade;
        return g;
    };
    // THE LIGHTS' STREAKS on the wet floors: the nearest the camera, faded
    // over the reach's last fifth - and, past `kMaxStreakLights`, against the
    // FIRST ONE LEFT OUT, as the emitters' slots are below: ranked by
    // distance alone a light entering the nearest came in whole, "from nothing
    // to a huge reflection in one step" (the reader, 2026-10-28). The frame's
    // wet floors decide whether anything draws them
    if (v.streaks > 0.0f) {
        const std::size_t cap = static_cast<std::size_t>(omk::View::kMaxStreakLights);
        // (the emitters' own reach, `kFxView`, may be the nearer: 3000 untiled)
        const float far = std::min(omk::kStreakFar, kFxView);
        float cut = far;
        if (fx.size() > cap) cut = std::min(cut, std::sqrt(fx[cap].first));
        for (const auto& [e2, l] : fx) {
            if (v.streakLights.size() >= cap || e2 >= cut * cut) break;
            const float d = std::sqrt(e2);
            const float fade = std::min(std::clamp((far - d) / (0.2f * far), 0.0f, 1.0f),
                                        std::clamp((cut - d) / (kStreakCutFade * cut), 0.0f, 1.0f));
            const auto g = gpu(*l, fade);
            if (g.intensity > 0.0f) v.streakLights.push_back(g);
        }
    }
    if (fxTiled) {
        for (const auto& [e2, l] : fx) {
            if (v.points.size() >= static_cast<std::size_t>(omk::kMaxPoints)) break;
            const auto g = gpu(*l, std::clamp((kFxView - std::sqrt(e2)) / (0.2f * kFxView), 0.0f, 1.0f));
            if (g.intensity > 0.0f) v.points.push_back(g);
        }
        fx.clear();
    }
    // THE CUT, without a pop: a light fades by its distance against the FIRST
    // ONE LEFT OUT - that distance moves continuously with the camera, so a
    // light leaving the nearest `kFxSlots` has faded to nothing by then
    // (ranked by distance alone, they appeared and vanished as he walked -
    // the reader, 2026-10-20)
    if (fx.size() > static_cast<std::size_t>(kFxSlots)) {
        fxCut = std::min(fxCut, std::sqrt(fx[static_cast<std::size_t>(kFxSlots)].first));
        fx.resize(static_cast<std::size_t>(kFxSlots));
    }
}

// ...and the first frame an EMITTER's light is among them (the remaster's,
// `o3de/emitterlight.h` - a point light)
void PlayState::remasterLightsTold(const omk::View& v, const std::vector<std::pair<float, const omk::Light3do*>>& near,
                                   const float at[3]) {
    if (!fxLightsTold) {
        int points = 0;
        for (const auto& [d2, l] : near) points += omk::pointLight(*l) ? 1 : 0;
        // ...or, TILED, those of `View::points` whose reach takes in the player
        for (const auto& g : v.points) {
            const float dx = at[0] - g.pos[0], dy = at[1] - g.pos[1], dz = at[2] - g.pos[2];
            points += dx * dx + dy * dy + dz * dz <= g.radiusA * g.radiusA ? 1 : 0;
        }
        if (points > 0) {
            fxLightsTold = true;
            std::printf("frame %ld: per-pixel lighting - %d of the %zu are emitters' lights\n",
                        frameNo, points, v.lights.size());
            omk::event("remaster.fxlight", {{"frame", frameNo}, {"points", points}});
        }
    }
}

// THE EMITTERS' LIGHTS ON THE SET (`o3de/emitterlight.h`): under per-pixel
// lighting the set's OPAQUE draws take the frame's point lights on top of
// their baked colour - mode 3, which skips the set's own lights (they are in
// the bake already)
void PlayState::remasterLitSet(std::vector<omk::Draw>& list) {
    if (lighting > 0 && fxLight > 0.0f && world_ && world_->drawsEmitterLights()) {
        for (auto& d : list)
            if (d.lit == 0 && d.blend == omk::Blend::Opaque && !d.meshPose &&
                (d.geo == &worldSlots[0].geo || d.geo == &worldSlots[1].geo))
                d.lit = 3;
    }
}

// THE WATER REFLECTION (`todo/remaster.md` A3), when asked for and the camera
// is above the shown slot's water; where it does not, the REFLECTING FLOORS
// (`o3de/floorreflect.h`) through the same pass - one reflection a frame.
// -> whether either drew the frame (the set's own mirror then does not)
bool PlayState::remasterDrawReflected(omk::Renderer& world, std::span<const omk::Draw> list, const omk::View& drawn) {
    auto& session = *session_;
    const WorldSlot& shownSlot = worldSlots[static_cast<std::size_t>(session.shownSlot() & 1)];
    const bool watered = waterStrength > 0.0f &&
        omk::drawWithWater(world, list, drawn, &shownSlot.geo, shownSlot.water, waterStrength);
    if (watered != waterLive) {
        waterLive = watered;
        std::printf("frame %ld: the water %s\n", frameNo,
                    watered ? "REFLECTS - the scene through the camera mirrored in its plane"
                            : "does not reflect here (no water, the camera below it, or no pass)");
        omk::event("remaster.water", {{"frame", frameNo}, {"live", watered},
                                      {"y", static_cast<double>(shownSlot.water.y)}});
    }
    // ...and where it does not, the REFLECTING FLOORS (`o3de/floorreflect.h`):
    // the listed floor's level below the eye with the most of it near, through
    // the same pass - one reflection a frame. And the WET ones
    // (`o3de/wetfloor.h`): in a RAIN area, the floors under the open sky, as
    // wet as the game's own shower is strong and drying after it
    const float wet = wetStrength > 0.0f ? wetness.step(weatherRain, frameSec * 1000.0) : 0.0f;
    const bool rainArea = weatherBitsTold != 0xFFFFu && (weatherBitsTold & 2u) != 0;
    const float wetReflect = rainArea ? wetStrength * wet * omk::kWetReflect : 0.0f;
    const float wetDarken = rainArea ? wetStrength * wet * omk::kWetDarken : 0.0f;
    const bool wetOn = wetReflect > 0.0f && shownSlot.wet.any();
    const bool listOn = floorStrength > 0.0f && shownSlot.floors.any();
    // `[Remaster] reflections`: 0 no floor mirrors (the wet film is laid
    // without the pass, `remasterSnowCover`), 1 one level, 2 a second too
    const bool mirrors = reflectLevels >= 0.5f;
    omk::WaterPlane floorPlane;
    if (!watered && listOn && mirrors)
        floorPlane = omk::chooseFloor(shownSlot.floors, drawn.cam.eye);
    if (!watered && wetOn && mirrors) {
        // the wet level nearest the height the camera looks at - the street
        // the player stands in - takes the reflection (its puddles mirror)
        const omk::WaterPlane wp = omk::chooseFloorNear(shownSlot.wet.planes, drawn.cam.eye, drawn.cam.at[1]);
        if (wp.found) floorPlane = wp;
    }
    // ...and THE SECOND LEVEL (`chooseFloorSecond`, the weather list's 9): of
    // the wet levels and the listed ones, the one with the most floor ahead
    omk::SecondPlane second;
    if (floorPlane.found && reflectLevels >= 1.5f) {
        if (wetOn) second.plane = omk::chooseFloorSecond(shownSlot.wet.planes, drawn.cam.eye, drawn.cam.at, floorPlane.y);
        if (listOn) {
            const omk::WaterPlane lp = omk::chooseFloorSecond(shownSlot.floors, drawn.cam.eye, drawn.cam.at, floorPlane.y);
            if (lp.found && lp.area > second.plane.area) second.plane = lp;
        }
    }
    // with the rain, the pass at 1 and each run its own: the listed floors'
    // strength times `floors`, the wet ones' `wetReflect`
    static std::vector<float> listed;
    const std::vector<float>* listStrength = listOn ? &shownSlot.floors.strength : nullptr;
    float passStrength = floorStrength;
    if (wetOn) {
        passStrength = 1.0f;
        if (listOn) {
            listed.assign(shownSlot.floors.strength.begin(), shownSlot.floors.strength.end());
            for (auto& s : listed) s *= floorStrength;
            listStrength = &listed;
        }
    }
    // `OMK_SWAP_LEVELS=1`, an INSTRUMENT: the two levels swapped, so the
    // frame's own floor reflects from the SECOND quarter - the same picture
    // as one plane, pixel for pixel, when that pass is right (`verify.py:
    // engine: floors` part 4)
    static const bool swapLevels = omk::envFlag("OMK_SWAP_LEVELS");
    if (swapLevels && second.plane.found) std::swap(floorPlane, second.plane);
    // ...SEEN, or no pass (the occlusion test, `Renderer::secondLevelSeen`):
    // the last frame's query round its floors - a level hidden behind a wall
    // costs no pass, its floors still drawn and queried; one seen again fades
    // in over `kLevel2FadeSec` (the query is a frame late: no pop). A new
    // level starts at 0, queried before it is drawn. `OMK_NO_LEVEL_QUERY=1`,
    // an INSTRUMENT, takes every level as seen (`engine: floor levels` part 6)
    static const bool noQuery = omk::envFlag("OMK_NO_LEVEL_QUERY");
    if (second.plane.found) {
        const int seen = noQuery ? 1 : world.secondLevelSeen();
        if (second.plane.y != level2Y) level2Fade = 0.0f;
        else if (seen == 0) level2Fade = 0.0f;
        else level2Fade = std::min(1.0f, level2Fade + static_cast<float>(frameSec) / kLevel2FadeSec);
        level2Y = second.plane.y;
        second.fade = level2Fade;
    } else {
        level2Fade = 0.0f;
        level2Y = 1e30f;
    }
    const bool floored = floorPlane.found &&
        omk::drawWithWater(world, list, drawn, &shownSlot.geo, floorPlane, passStrength, listStrength,
                           wetOn ? &shownSlot.wet.rained : nullptr, wetReflect, wetDarken, &second);
    if (wetStrength > 0.0f && (wetOn != wetTold || frameNo % 30 == 0)) {
        if (wetOn != wetTold)
            std::printf("frame %ld: the wet floors %s (wetness %.2f, reflection %.2f)\n", frameNo,
                        wetOn ? "REFLECT - the rain has wet the floors under the open sky"
                              : "are dry (no rain here, or it has dried)",
                        static_cast<double>(wet), static_cast<double>(wetReflect));
        wetTold = wetOn;
        omk::event("remaster.wet", {{"frame", frameNo}, {"rain", weatherRain}, {"wetness", wet},
                                    {"reflect", wetReflect}, {"live", wetOn && floored},
                                    {"y", static_cast<double>(floorPlane.y)}});
    }
    const float y2 = second.drawn ? second.plane.y : 0.0f;
    if (floored != floorLive || (floored && floorPlane.y != floorLiveY) || second.drawn != floor2Live ||
        (second.drawn && y2 != floor2LiveY)) {
        floorLive = floored;
        floorLiveY = floorPlane.y;
        floor2Live = second.drawn;
        floor2LiveY = y2;
        std::printf("frame %ld: the floors %s (y %.0f)%s\n", frameNo,
                    floored ? "REFLECT - the scene mirrored in the level below the eye"
                            : "do not reflect here (none listed near, the water reflecting, or no pass)",
                    static_cast<double>(floorPlane.y),
                    second.drawn ? (" and a SECOND level, its own pass (y " + std::to_string(std::lround(y2)) + ")").c_str()
                                 : "");
        omk::event("remaster.floors", {{"frame", frameNo}, {"live", floored},
                                       {"y", static_cast<double>(floorPlane.y)}, {"second", second.drawn},
                                       {"y2", static_cast<double>(y2)}});
    }
    return watered || floored;
}

// THE SNOW LYING (`o3de/wetfloor.h` coverRuns, `[Remaster] wet` - the weather
// on the ground): where the game's SNOW falls - Mahaleel; its cycle never stops
// (docs/ASSETS.md 4e) and needs option row 7 at 2 - the set's floors under the
// open sky turn white under their own light, the corners' open sky blending
// the edge. No reflection pass: the frame's own draws, split into runs.
// ...and with `[Remaster] reflections = 0`, the RAIN's film the same way: no
// level mirrors, so no pass lays it (`drawWithWater`), and a wet floor keeps
// its darkening, its ripples and the lights' streaks - how wet, the frame
// before's (`wetness`, stepped by `remasterDrawReflected`).
void PlayState::remasterSnowCover(std::vector<omk::Draw>& list) {
    if (wetStrength <= 0.0f || !remasterShown_ || !world_ || !(world_->takes() & omk::kTakesWater)) return;
    if (!weatherSnowing) {
        const bool rainArea = weatherBitsTold != 0xFFFFu && (weatherBitsTold & 2u) != 0;
        const WorldSlot& slot = worldSlots[static_cast<std::size_t>(session_->shownSlot() & 1)];
        const float dark = wetStrength * wetness.value() * omk::kWetDarken;
        if (reflectLevels < 0.5f && rainArea && dark > 0.0f && !slot.wet.rained.empty()) {
            const std::size_t covered = omk::coverRuns(list, &slot.geo, slot.wet.rained, dark, false);
            if (frameNo % 30 == 0)
                omk::event("remaster.wetfilm", {{"frame", frameNo}, {"covered", static_cast<long>(covered)}});
        }
        return;
    }
    const WorldSlot& shownSlot = worldSlots[static_cast<std::size_t>(session_->shownSlot() & 1)];
    if (shownSlot.wet.open.empty()) return;
    const std::size_t covered = omk::coverRuns(list, &shownSlot.geo, shownSlot.wet.open,
                                               std::clamp(wetStrength, 0.0f, 1.0f) * omk::kSnowCover);
    if (frameNo % 30 == 0) {
        std::printf("frame %ld: the snow lies on %zu floor triangles under the open sky\n", frameNo, covered);
        omk::event("remaster.snow", {{"frame", frameNo}, {"covered", static_cast<long>(covered)}});
    }
}

// THE WET BODIES (the reader's "wet walls and characters", 2026-10-28): in a
// rain area while the floors are wet, every body the renderer poses that
// stands under the open sky - the roof ray from `kWetBodyHead` above its root
// through the shot soup, the wet floors' own - is drawn wet (`Draw::wetBody`),
// as wet as the floors. Each body keeps its own wetness (`bodyWet`, by its
// pose): it soaks in the open and dries slowly under cover, so a body that
// steps under an awning stays wet a while (`stepBodyWet`).
// `OMK_NO_WET_BODIES=1`, an INSTRUMENT, leaves them dry (`verify.py: engine:
// wet floors` part 11); `OMK_WET_BODY_COVER_AT=N`, another, has every body
// under cover from frame N - the drying seen with no roof to walk under
// (part 12).
void PlayState::remasterWetBodies(std::vector<omk::Draw>& list) {
    const bool noBodies = omk::envFlag("OMK_NO_WET_BODIES");   // read each frame: the sweep switches it
    static const long coverAt = static_cast<long>(omk::envNum("OMK_WET_BODY_COVER_AT", 0.0));
    const bool rainArea = weatherBitsTold != 0xFFFFu && (weatherBitsTold & 2u) != 0;
    const float floors = wetness.value();
    if (noBodies || !rainArea || !(wetStrength * floors > 0.0f) || !remasterShown_ || !world_ ||
        !(world_->takes() & omk::kTakesWater)) {
        bodyWet.clear();
        bodyStride.clear();
        return;
    }
    const WorldSlot& shownSlot = worldSlots[static_cast<std::size_t>(session_->shownSlot() & 1)];
    if (shownSlot.shotSoup.empty()) return;
    // the bodies POSED ON THE CPU draw their own posed copy, with no pose to
    // place them by (the player with a smoothed body on GLES, any body on a
    // renderer that poses none): known by that copy
    std::vector<const omk::Geometry*> cpuPosed{&playerBody.posed};
    for (const auto& up : staged) cpuPosed.push_back(&up->posed);
    for (const auto& up : crowd.staged) cpuPosed.push_back(&up->posed);
    for (const auto& up : traffic.staged) cpuPosed.push_back(&up->posed);
    // one ray a body: its draws share the pose, or the posed copy
    const void* lastBody = nullptr;
    bool lastOpen = false;
    float lastWet = 0.0f;
    int bodies = 0, wetBodies = 0, drying = 0;
    int playerOpen = -1;   // the player's own roof answer, when posed on the CPU (-1 not seen)
    std::unordered_map<const void*, float> seen;   // the bodies drawn this frame: the rest forgotten
    std::unordered_map<const void*, BodyStride> strode;   // ...and their strides
    // THE SPLASHES OFF THE BODIES (the weather list's 5, 2026-10-29): in the
    // 3D rain's shower, a body in the open splashes at its feet a step at a
    // time as it walks, and drops burst on its head
    const bool noSplashes = omk::envFlag("OMK_NO_SPLASHES");   // read each frame: the sweep switches it
    const bool noBodySplashes = omk::envFlag("OMK_NO_BODY_SPLASHES");   // read each frame: the sweep switches it
    const bool splashing = !noSplashes && !noBodySplashes && rain3dStrength > 0.0f && !rainDrops.empty() &&
                           (rainBits & 2u) != 0 && splashClock >= 0.0;
    const double shower = std::min(1.0, static_cast<double>(rainDrops.size()) / kBodyShowerFull);
    const auto rnd = [&] {
        bodySplashSeed = bodySplashSeed * 1664525u + 1013904223u;
        return static_cast<float>(bodySplashSeed >> 8) / 16777216.0f;
    };
    static long footSplashes = 0, headSplashes = 0;   // (since the start: the event's)
    for (auto& d : list) {
        const bool gpu = d.meshPose && d.meshPoses != 0;
        if (!gpu && std::find(cpuPosed.begin(), cpuPosed.end(), d.geo) == cpuPosed.end()) continue;
        const void* body = gpu ? static_cast<const void*>(d.meshPose) : static_cast<const void*>(d.geo);
        if (body != lastBody) {
            lastBody = body;
            // its root, or the posed copy's lowest corner (Y is down) below its middle
            double at[3] = {0.0, 0.0, 0.0};
            double top = 0.0;   // ...and its head's height (Y down: above `at`)
            if (gpu) {
                // its ROOT is not where it stands - Kay'l's at -63.6 over the
                // street at 0, near his head (2026-10-29) - so the floor under
                // it, `kBodyFloorReach` down through the shot soup, is
                at[0] = d.meshPose[3]; at[1] = d.meshPose[7]; at[2] = d.meshPose[11];
                const double down[3] = {0.0, kBodyFloorReach, 0.0};   // Y points down
                if (const auto fl = omk::sweepSphere(shownSlot.shotSoup, shownSlot.shotRunBoxes, at, down, 0.0))
                    at[1] += kBodyFloorReach * fl->t;
                top = at[1] - kBodyTall;
            } else if (!d.geo->corners.empty()) {
                float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
                for (const auto& c : d.geo->corners) {
                    lo[0] = std::min(lo[0], c.x); hi[0] = std::max(hi[0], c.x);
                    lo[1] = std::min(lo[1], c.y); hi[1] = std::max(hi[1], c.y);
                    lo[2] = std::min(lo[2], c.z); hi[2] = std::max(hi[2], c.z);
                }
                at[0] = 0.5 * (lo[0] + hi[0]); at[1] = hi[1]; at[2] = 0.5 * (lo[2] + hi[2]);
                top = lo[1];
            } else {
                lastWet = 0.0f;
                continue;
            }
            const double up[3] = {0.0, -omk::kWetSkyRay, 0.0};   // Y points down
            const double p0[3] = {at[0], at[1] - omk::kWetBodyHead, at[2]};
            lastOpen = !omk::sweepSphere(shownSlot.shotSoup, shownSlot.shotRunBoxes, p0, up, 0.0).has_value() &&
                       !(coverAt > 0 && frameNo >= coverAt);
            const auto had = bodyWet.find(body);
            lastWet = omk::stepBodyWet(had == bodyWet.end() ? 0.0f : had->second, floors, lastOpen,
                                       frameSec * 1000.0);
            seen[body] = lastWet;
            ++bodies;
            wetBodies += lastOpen ? 1 : 0;
            if (body == &playerBody.posed) playerOpen = lastOpen ? 1 : 0;
            drying += !lastOpen && lastWet > 0.0f ? 1 : 0;
            // ...its STRIDE: how far it moved since last frame (a jump past
            // `kBodyStrideJump` is a placement, not a step) and on the
            // weather's clock (none while paused)
            BodyStride st{{static_cast<float>(at[0]), static_cast<float>(at[1]), static_cast<float>(at[2])}, 0.0f, 0,
                          weatherClockMs};
            float moved = 0.0f, dir[2] = {0.0f, 0.0f};
            double dt = 0.0;
            if (const auto was = bodyStride.find(body); was != bodyStride.end()) {
                const float dx = st.at[0] - was->second.at[0], dz = st.at[2] - was->second.at[2];
                const float m = std::hypot(dx, dz);
                if (m < kBodyStrideJump) {
                    moved = m;
                    if (m > 0.0f) { dir[0] = dx / m; dir[1] = dz / m; }
                }
                st.owed = was->second.owed;
                st.side = was->second.side;
                dt = std::clamp(weatherClockMs - was->second.clock, 0.0, kBodySplashStepMax);
            }
            if (splashing && lastOpen) {
                // a crown in the puddle at each step, foot by foot either side
                // of the way it walks - its own puddle, bigger than a drop's
                st.owed += moved;
                for (; st.owed >= kFootStride; st.owed -= kFootStride) {
                    const float sd = st.side ? kFootSide : -kFootSide;
                    st.side ^= 1;
                    const float f[3] = {st.at[0] - dir[1] * sd + dir[0] * kFootAhead, st.at[1],
                                        st.at[2] + dir[0] * sd + dir[1] * kFootAhead};
                    rainSplashes.push_back({{f[0], f[1], f[2]}, weatherClockMs - rnd() * dt, kFootSplashSize,
                                            kFootSplashGlow});
                    ++footSplashes;
                }
                // ...and the drops bursting on its head and shoulders, as many
                // as the shower is strong (none while the clock stands)
                for (double owe = kHeadSplashRate * shower * dt / 1000.0; owe > 0.0; owe -= 1.0) {
                    if (owe < 1.0 && rnd() >= owe) break;
                    const float a = rnd() * 6.2831853f, r = kHeadSplashSpread * std::sqrt(rnd());
                    const float y = static_cast<float>(top) + kHeadSplashDrop * rnd();   // the head, or a shoulder below it
                    rainSplashes.push_back({{st.at[0] + std::cos(a) * r, y, st.at[2] + std::sin(a) * r},
                                            weatherClockMs - rnd() * dt, kHeadSplashSize, kHeadSplashGlow});
                    ++headSplashes;
                }
            } else {
                st.owed = 0.0f;   // a step's worth carried only while it splashes
            }
            strode[body] = st;
        }
        if (!(lastWet > 0.0f)) continue;
        d.wet = std::min(wetStrength * lastWet, 1.0f);
        d.wetBody = true;
    }
    bodyWet.swap(seen);
    bodyStride.swap(strode);
    if (frameNo % 30 == 0) {
        std::printf("frame %ld: %d of %d bodies wet in the rain, %d drying under cover\n", frameNo, wetBodies, bodies,
                    drying);
        omk::event("remaster.wetbodies", {{"frame", frameNo}, {"bodies", bodies}, {"wet", wetBodies},
                                          {"drying", drying}, {"playerOpen", playerOpen},
                                          {"footSplashes", footSplashes}, {"headSplashes", headSplashes}});
    }
}

#endif  // OMK_REMASTER

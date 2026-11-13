// SPDX-License-Identifier: GPL-3.0-or-later
// THE RAIN IN 3D - the remaster's (`todo/remaster.md`, "The rain in 3D").
// NOT THE ORIGINAL'S DRAW: `sub_4815D0` projects its 200 drops and draws each
// as a 2D grey line over the picture (docs/ASSETS.md 4e), through walls and
// under roofs alike. Here the SAME drops - the game's cycle, its 200, its box
// ahead of the eye and its wind, all `o3de/weather.h`'s, untouched - are
// strips IN the scene: the depth test hides them behind a wall, and a drop
// with a roof over it (straight up through the set's SHOT soup, the world
// `sub_481200`'s roof ray uses) is not drawn. Each strip runs from the drop to
// its tail as the line did, ADDITIVE, from the scene's fog colour at the drop
// to 0xC0C0C0 at the tail times `[Remaster] rain3d`, as wide as the
// original's 1-pixel line is at 640x480 (scaled with the frame's height).
// The snow keeps the original's sprites. Vulkan and GLES (`kTakesWater`'s
// backends). Compiled empty without OMK_REMASTER (`playremaster_off.cpp`).
//
// DENSER AND FAINTER than the original's lines (the reader, on the comparison
// picture, 2026-10-27: half the original's box lies below the street, so the
// 3D rain showed far fewer streaks): each drop brings `kRainCopies` more at
// fixed offsets inside the same box, wrapped as the box wraps - they move with
// it, so the cycle's count and its ramp hold - and every strip is `kRainGlow`
// as bright, fading in over the first `kRainNear` units from the eye and out
// over the box's far end: rain is seen through, not drawn over.
//
// ...and SOFT (the reader's "do 1 and 2", 2026-10-27, after the hard 1-pixel
// lines read as scratches near the eye): each strip carries a generated
// STREAK (`rainStreakTexture`: soft across, even along with soft ends - its
// first bright head read as a shooting star) three times the line's width at
// 480 lines, wider as the root of the frame's height, so its edges are the
// texture's, not the raster's; and it is `kRainLength` long in the WORLD - a
// camera's shutter open half a frame on a drop falling 64 units a frame - so
// perspective makes a near drop's streak long and a far one's short, where
// the original's 12 units drew every drop alike.
//
// ...and LIT (the reader's "option 3", 2026-10-27): each drop takes the light
// of the set's own lights that reach it - and the emitters' (the flames, the
// glows) when the remaster's emitters' light put them in the slot's table -
// on the engine's own rule (`o3de/vertexlight.h` `lightReach`: the squared
// reach, the linear falloff, `intensity * 256`), as a surface facing each
// light would (a streak has none), its colour through the engine's ramp
// `(t * c) >> 8`: rain glints orange by the burner and in a lamp's pool, and
// is dimmer where nothing lights it.
//
// ...and THE SNOW too (the reader's "ok, continue", 2026-10-27): Mahaleel's
// 200 flakes (`sub_481D90`'s bit-4 arm - the box of 400, the drift, all the
// original's) as quads facing the eye, the original's 8 units across, with
// its own `Images\snow.bmp` - a 64x64 8-bit picture keyed on BLACK, which an
// additive draw adds nothing of, so the key holds by itself - lit and faded
// as the rain, three copies a flake. A flake under a roof is not drawn: the
// ice cave's flakes, which the original re-scatters every frame there once it
// has snowed (docs/ASSETS.md 4e - a static), are gone in the remaster.
//
// ...and THE RESTORED SAND (`[Restored] weather`, todo/restore-weather.md):
// type 3's drops, one grain of `Images\sand.bmp` each - cut from the field,
// as a small soft dot of that grain's own picture, filtered and additive, lit
// by the set's lights, faded in as the snow, three copies a drop - in the
// DUST, a fog of drifting ochre clouds (`View::dust`, the scene shaders) under
// a lightly warmed grade (`remasterGrade`): dots alone read as snow, streaks
// as rain (the reader, 2026-10-27).
//
// ...and THE SPLASHES (the reader's "more realistic rain", 3 of 3,
// 2026-10-28): where the rain meets open ground ahead of the eye, a drop's
// CROWN - an upright soft sprite of a generated picture (`splashTexture`: a
// bright rim at the foot, two walls flaring out, beads thrown off their top)
// turned to face the eye, rising and fading in `kSplashLife` on the weather's
// clock (held while paused), additive and lit as the drops. They land at
// random in the half-disc ahead (`kSplashNear`..`kSplashFar`, the view's
// width), on the floor a ray down through the shot soup meets, and only
// where the roof ray up from there meets nothing; `kSplashRate` a second at
// the full shower, fewer as its count ramps. Their own sequence, not
// `rand()`, so the backends draw the same ones. `OMK_NO_SPLASHES=1` (an
// instrument) leaves them out.
#if OMK_REMASTER

#include "../sdl/playstate.h"
#include "o3de/collision.h"
#include "o3de/raster.h"
#include "o3de/vertexlight.h"
#include "platform/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr double kRainRoofRay = 4000.0;   // how far up a roof stops a drop (`kWetSkyRay`'s)
constexpr float kRainBox = 1000.0f;       // the rain's box (`dword_4D0780`, `o3de/weather.cpp`)
constexpr int kRainCopies = 3;            // the drops each original one brings
constexpr float kRainGlow = 0.4f;         // a strip's brightness against the original line's (0.55: solid)
constexpr float kRainOwnMin = 0.35f;      // ...times each drop's own, from this to 1
// ...faded in from `kRainNearFrom` to `kRainNear` from the eye, smoothly: a drop
// a step away is a long bright streak across the frame (the reader's "fade the
// near streaks", 2026-10-27 - it was 0..80, linear)
constexpr float kRainNearFrom = 40.0f;
constexpr float kRainNear = 260.0f;
constexpr float kRainLength = 32.0f;      // a streak's length in the world: half a frame's fall
constexpr float kRainHalfPx = 1.5f;       // ...its half width in pixels at 480 lines, the texture's soft edge in it,
                                          // growing as the root of the frame's height (in step: 10 px wide at 1600)
constexpr int kStreakW = 8, kStreakH = 32;   // the streak texture, across and along
constexpr float kStreakEnds = 0.25f;      // ...soft over this much of its length at each end
constexpr float kRainUnlit = 0.35f;       // a drop no light reaches, against the line's grey
constexpr float kRainNeon = 2.0f;         // ...and the emitters' (the neon), four times the set's lights
constexpr float kRainLit = 0.5f;          // ...and the lights' colour added on it - Anekbah's
                                          // street has two fills reaching everywhere, ~0.8 together
// THE GUSTS (the remaster's, the weather list's 7, 2026-10-29): the drawn
// slant of the rain sways with the wind - the original's drops, their step and
// their roof test untouched, only the streak's lean and the veils' sheets
// (`remasterGust`). Three slow waves on the weather's clock, coming and going
// in bursts, a gust front travelling down the wind
constexpr double kGustShear = 0.35;       // the most a gust adds to the slant's tangent (the original's 0.577)
constexpr double kGustPeriod[3] = {13000.0, 6100.0, 2300.0};   // ...its waves, ms (4300 and 1700: a nervous flutter)
constexpr double kGustWeight[3] = {0.6, 0.3, 0.1};
constexpr double kGustPhase[3] = {0.0, 1.3, 0.4};
constexpr double kGustLull = 0.35;        // ...a lull takes this much of a gust's lean away (the slant below the original's)
constexpr double kGustBurstPeriod = 23000.0;   // the gusty spells come and go this often
constexpr double kGustCalm = 0.25;        // ...the gusts' share between them
constexpr double kGustSpeed = 0.9;        // a gust front's speed down the wind, units a ms

// THE STREAK, generated: a grey texel per palette index (index = brightness)
// - across, a soft bell; along it EVEN, a drop's motion blur, soft at both
// ends (the first one's bright head fading behind read as a shooting star,
// the reader, 2026-10-28)
omk::Texture rainStreakTexture() {
    omk::Texture t;
    t.name = "OMK_RAIN_STREAK";
    t.width = kStreakW;
    t.height = kStreakH;
    t.bpp = 8;
    t.exact = true;
    t.pal.assign(768, 0);
    std::uint8_t* pal = t.pal.mutableData();
    for (int i = 0; i < 256; ++i) pal[3 * i] = pal[3 * i + 1] = pal[3 * i + 2] = static_cast<std::uint8_t>(i);
    t.idx.assign(static_cast<std::size_t>(kStreakW) * kStreakH, 0);
    std::uint8_t* px = t.idx.mutableData();
    for (int y = 0; y < kStreakH; ++y) {
        const float along = (static_cast<float>(y) + 0.5f) / kStreakH;   // 0 the leading end
        const float end = std::clamp(std::min(along, 1.0f - along) / kStreakEnds, 0.0f, 1.0f);
        const float l = end * end * (3.0f - 2.0f * end);
        for (int x = 0; x < kStreakW; ++x) {
            const float a = (static_cast<float>(x) + 0.5f) / kStreakW * 2.0f - 1.0f;   // -1..1 across
            const float across = std::pow(std::max(0.0f, 1.0f - a * a), 2.0f);
            px[y * kStreakW + x] = static_cast<std::uint8_t>(std::lround(255.0f * l * across));
        }
    }
    return t;
}
// the copies' offsets in the box, fixed (any spread that is not a lattice)
constexpr float kRainOffsets[kRainCopies][3] = {
    {317.0f, -251.0f, 173.0f}, {-211.0f, 389.0f, -443.0f}, {463.0f, 127.0f, -307.0f}};
constexpr float kSnowBox = 400.0f;        // the snow's box (`sub_481D90`'s bit-4 arm)
constexpr float kSandBox = 300.0f;        // ...and the sand's (its else branch)
constexpr float kSandGrain3d = 0.8f;      // ...a 4-texel grain this many units across in the scene (1.8: "really too big")
constexpr float kSandGrainPx = 1.5f;      // ...and no more than this many pixels each side at 480 lines, near the eye
constexpr float kSandGlow = 0.45f;        // ...its brightness, additive - its see-through (0.9: too solid, the reader)
constexpr float kFlakeHalf = 4.0f;        // ...a flake's half size, the original's 8 units across
constexpr float kSnowNearFrom = 20.0f, kSnowNear = 80.0f;   // a flake's fade in from the eye

constexpr double kSplashRate = 700.0;     // the splashes a second at the full shower (its 200 drops)
constexpr double kSplashLife = 240.0;     // ...each one's life, ms on the weather's clock
constexpr float kSplashNear = 60.0f, kSplashFar = 1100.0f;   // ...landing this far ahead of the eye
constexpr float kSplashHalfAngle = 0.9f;  // ...within this many radians of the view's heading
constexpr float kSplashW = 4.5f, kSplashH = 6.0f;   // ...a crown's half width and height at its fullest (7, 9: icons)
constexpr float kSplashGlow = 0.6f;       // ...its brightness, additive
constexpr float kSplashSizeMin = 0.6f, kSplashSizeMax = 1.2f;   // ...each its own size, between these
constexpr float kSplashBornW = 0.55f, kSplashBornH = 0.35f;     // ...born this much of its full width and height
constexpr float kSplashFoot = 0.5f;       // ...its foot this far above the floor
constexpr float kSplashFarFade = 0.3f;    // ...faded out over this much of `kSplashFar`'s end
constexpr double kSplashFloorNy = -0.7;   // a floor faces up at least this much (Y down: -1 straight up)
constexpr double kSplashSkyFrom = 2.0;    // the roof ray starts this far above the floor
constexpr double kSplashStepMax = 100.0;  // the most ms of splashes one frame spawns (a hitch, not a shower)
constexpr double kRainFull = 200.0;       // the full shower's drops (`o3de/weather.h`): the rate's 1
constexpr int kSplashTex = 32;            // the crown texture's side
// THE DRIPS off the roofs' edges (`o3de/wetfloor.h` DripPoint): each point's
// drops fall under gravity, one at a time, every `kDripPeriod..` seconds plus
// its own fall, and land in a splash
constexpr float kDripFar = 900.0f;        // drawn within this of the eye (~23 m; 1500: far ones read as dots)
constexpr double kDripG = 386.0;          // gravity, units a second squared (an inch a unit)
constexpr double kDripPeriodMin = 0.35, kDripPeriodMax = 1.2;   // seconds between drops, past the fall
constexpr double kDripStreakSec = 0.07;   // a drop's streak: its fall over this long (a frame's blur and more)
constexpr float kDripLenMin = 16.0f, kDripLenMax = 80.0f;       // ...no shorter, no longer
constexpr float kDripGlow = 2.0f;         // its brightness against a rain drop's (fewer, bigger drops)
constexpr float kDripWide = 1.3f;         // ...and its width (2: short wide ones read as squares)

// THE CROWN, generated as the streak: a grey texel per palette index. Its
// foot at the bottom row: a faint rim (the ring seen edge on, brighter at its
// ends), two thin walls flaring out and up from it, a soft spray between them
// and four beads thrown off above - then blurred twice, a SPLASH and not a
// drawn crown (the sharp one read as an icon)
omk::Texture splashTexture() {
    omk::Texture t;
    t.name = "OMK_RAIN_SPLASH";
    t.width = t.height = kSplashTex;
    t.bpp = 8;
    t.exact = true;
    t.pal.assign(768, 0);
    std::uint8_t* pal = t.pal.mutableData();
    for (int i = 0; i < 256; ++i) pal[3 * i] = pal[3 * i + 1] = pal[3 * i + 2] = static_cast<std::uint8_t>(i);
    const float beads[4][4] = {{-0.78f, 0.62f, 0.09f, 0.55f}, {-0.22f, 0.86f, 0.08f, 0.45f},
                               {0.30f, 0.74f, 0.07f, 0.5f}, {0.82f, 0.56f, 0.09f, 0.6f}};
    const auto bell = [](float d, float r) { return std::exp(-(d * d) / (2.0f * r * r)); };
    std::vector<float> l(static_cast<std::size_t>(kSplashTex) * kSplashTex, 0.0f);
    for (int y = 0; y < kSplashTex; ++y)
        for (int x = 0; x < kSplashTex; ++x) {
            const float X = (static_cast<float>(x) + 0.5f) / kSplashTex * 2.0f - 1.0f;   // -1..1 across
            const float Y = 1.0f - (static_cast<float>(y) + 0.5f) / kSplashTex;          // 0 the foot
            float v = std::clamp((0.95f - std::abs(X)) / 0.2f, 0.0f, 1.0f) * bell(Y - 0.06f, 0.04f) *
                      (0.35f + 0.65f * X * X);
            if (Y > 0.04f && Y < 0.55f) {   // the walls: from (+-0.5, 0.05) out to (+-0.8, 0.5)
                const float s = (Y - 0.05f) / 0.45f;
                v = std::max(v, 0.6f * (1.0f - s) * bell(std::abs(X) - (0.5f + 0.3f * s), 0.06f));
            }
            v = std::max(v, 0.3f * bell(X, 0.25f) * bell(Y - 0.3f, 0.18f));   // the spray
            for (const auto& b : beads) v = std::max(v, b[3] * bell(std::hypot(X - b[0], Y - b[1]), b[2]));
            l[static_cast<std::size_t>(y) * kSplashTex + x] = v;
        }
    for (int pass = 0; pass < 2; ++pass) {   // a 3x3 box, twice
        std::vector<float> o(l.size(), 0.0f);
        for (int y = 0; y < kSplashTex; ++y)
            for (int x = 0; x < kSplashTex; ++x) {
                float sum = 0.0f;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx >= 0 && yy >= 0 && xx < kSplashTex && yy < kSplashTex)
                            sum += l[static_cast<std::size_t>(yy) * kSplashTex + xx];
                    }
                o[static_cast<std::size_t>(y) * kSplashTex + x] = sum / 9.0f;
            }
        l.swap(o);
    }
    t.idx.assign(l.size(), 0);
    std::uint8_t* px = t.idx.mutableData();
    float top = 0.0f;
    for (float v : l) top = std::max(top, v);
    for (std::size_t i = 0; i < l.size(); ++i)
        px[i] = static_cast<std::uint8_t>(std::lround(255.0f * std::clamp(l[i] / top, 0.0f, 1.0f)));
    return t;
}

// `Images\snow.bmp`, the original's flake: an 8-bit BMP, bottom-up, its
// palette as stored - an empty Texture on anything else
omk::Texture snowTexture(std::span<const std::byte> d) {
    omk::Texture t;
    const auto u8 = [&](std::size_t o) { return o < d.size() ? static_cast<std::uint8_t>(d[o]) : std::uint8_t{0}; };
    const auto u32 = [&](std::size_t o) { return u8(o) | u8(o + 1) << 8 | u8(o + 2) << 16 | static_cast<std::uint32_t>(u8(o + 3)) << 24; };
    if (d.size() < 54 || u8(0) != 'B' || u8(1) != 'M' || (u8(28) | u8(29) << 8) != 8) return t;
    const std::uint32_t off = u32(10), hdr = u32(14);
    const int w = static_cast<int>(u32(18)), h = static_cast<int>(u32(22));
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return t;
    const std::size_t stride = (static_cast<std::size_t>(w) + 3) & ~std::size_t{3};
    if (off + stride * h > d.size()) return t;
    t.name = "IMAGES/SNOW.BMP";
    t.width = w; t.height = h; t.bpp = 8; t.exact = true;
    t.pal.assign(768, 0);
    std::uint8_t* pal = t.pal.mutableData();
    for (int i = 0; i < 256; ++i) {
        const std::size_t e = 14 + hdr + 4u * static_cast<std::size_t>(i);   // B, G, R, 0
        if (e + 3 > off) break;
        pal[3 * i] = u8(e + 2); pal[3 * i + 1] = u8(e + 1); pal[3 * i + 2] = u8(e);
    }
    t.idx.assign(static_cast<std::size_t>(w) * h, 0);
    std::uint8_t* px = t.idx.mutableData();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            px[static_cast<std::size_t>(y) * w + x] = u8(off + stride * static_cast<std::size_t>(h - 1 - y) + x);
    return t;
}
}  // namespace

// the streak's texture into the frame's pool, after the sprites, while the
// rain in 3D was armed at the start
void PlayState::remasterPoolRain(std::vector<omk::Texture>& slots) {
    rainTexSlot = -1;
    splashTexSlot = -1;
    if (remasterArmed("rain3d") <= 0.0f) return;
    static const omk::Texture streak = rainStreakTexture();
    rainTexSlot = static_cast<int>(slots.size());
    slots.push_back(streak);
    // ...and the splashes' crown
    static const omk::Texture crown = splashTexture();
    splashTexSlot = static_cast<int>(slots.size());
    slots.push_back(crown);
    // ...and the snow's flake, the original's own picture
    static const omk::Texture flake = snowTexture(fs_->read("IMAGES/snow.bmp"));
    snowTexSlot = -1;
    if (flake.hasPixels()) {
        snowTexSlot = static_cast<int>(slots.size());
        slots.push_back(flake);
    }
    // ...and the restored sand's field, cut into its grains (a texel is solid
    // where its palette colour is not black - the key)
    sandTexSlot = -1;
    if (restoredWeather) {
        static const omk::Texture field = snowTexture(fs_->read("IMAGES/sand.bmp"));
        if (field.hasPixels()) {
            sandTexSlot = static_cast<int>(slots.size());
            slots.push_back(field);
            if (sandGrains3d.empty())
                sandGrains3d = omk::grainsOf(field.width, field.height, [&](int x, int y) {
                    const std::uint8_t* c = field.pal.data() + 3u * field.idx.data()[y * field.width + x];
                    return c[0] != 0 || c[1] != 0 || c[2] != 0;
                });
        }
    }
}

// THE GUST at `ms` on the weather's clock, `along` units down the original
// wind (its x): what it adds to the rain's lean, the tangent of its slant -
// times the shower's strength (`shower` 0..1). `OMK_NO_GUSTS=1`, an
// INSTRUMENT: none (`verify.py: engine: rain 3d` part 18)
float PlayState::remasterGust(double ms, float along, float shower) const {
    static const bool noGusts = omk::envFlag("OMK_NO_GUSTS");
    if (noGusts || !(shower > 0.0f)) return 0.0f;
    const double t = ms - static_cast<double>(along) / kGustSpeed;
    double wave = 0.0;
    for (int k = 0; k < 3; ++k) wave += kGustWeight[k] * std::sin(6.283185307 * t / kGustPeriod[k] + kGustPhase[k]);
    const double spell = 0.5 + 0.5 * std::sin(6.283185307 * ms / kGustBurstPeriod);
    const double burst = kGustCalm + (1.0 - kGustCalm) * spell * spell * (3.0 - 2.0 * spell);
    const double lean = wave >= 0.0 ? wave : wave * kGustLull;
    return static_cast<float>(kGustShear * burst * lean * std::clamp(static_cast<double>(shower), 0.0, 1.0));
}

// whether the cycle keeps a shower going under a roof: the 3D weather shows
// (it stops the drops at the roof itself)
bool PlayState::remasterWeatherRoofless() const {
    return rain3dStrength > 0.0f && remasterShown_ && world_ && (world_->takes() & omk::kTakesWater);
}

bool PlayState::remasterRainTake(const std::vector<omk::WeatherDrop>& drops, unsigned viewBits) {
    // the rain (bit 2) or the snow (bit 4); the third type, the sand (bit 8),
    // with the restored weather alone
    const bool sandToo = (viewBits & 8u) && restoredWeather;
    if (!((viewBits & 6u) || sandToo) || rain3dStrength <= 0.0f || !remasterShown_ || !world_ ||
        !(world_->takes() & omk::kTakesWater))
        return false;
    rainDrops = drops;
    rainBits = viewBits;
    return true;
}

void PlayState::remasterRainDraws(std::vector<omk::Draw>& list, const omk::View& v) {
    rainGeo.corners.clear();
    rainGeo.batches.clear();
    if (rainDrops.empty()) {
        rainSplashes.clear();
        splashClock = -1.0;
        return;
    }
    // the roofs: the shown set's shot soup, as the weather's own ray
    const omk::TriangleSoup* shot = nullptr;
    const omk::SoupRunBoxes* boxes = nullptr;
    const std::vector<omk::DripPoint>* drips = nullptr;
    std::span<const omk::Light3do> lights;
    std::size_t setLightCount = 0;   // ...the set's own first, then the emitters' (the neon, the flames)
    for (const auto& ws : worldSlots)
        if (!ws.stem.empty() && ws.stem == worldSet) {
            shot = &ws.shotSoup;
            boxes = &ws.shotRunBoxes;
            drips = &ws.wet.drips;
            lights = ws.lights;
            setLightCount = std::min(ws.setLights, ws.lights.size());
        }
    static std::vector<float> reach;
    int litDrops = 0, neonDrops = 0;   // ...lit, and lit by an emitter (the neon) past a glint
    const bool snow = (rainBits & 2u) == 0 && (rainBits & 4u) != 0;   // the snow's arm: bit 4 alone
    const bool sand = (rainBits & 6u) == 0;                            // the restored sand: bit 8
    if (sand && (sandTexSlot < 0 || sandGrains3d.empty())) return;
    const float box = snow ? kSnowBox : sand ? kSandBox : kRainBox;
    const float s = std::clamp(rain3dStrength, 0.0f, 1.0f) * (snow ? 1.0f : kRainGlow);
    // the box's centre as the weather places it: a third of its size ahead
    const float dv[3] = {v.cam.at[0] - v.cam.eye[0], v.cam.at[1] - v.cam.eye[1], v.cam.at[2] - v.cam.eye[2]};
    const float dl = std::sqrt(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
    if (!(dl > 0.0f)) return;
    float centre[3];
    for (int k = 0; k < 3; ++k) centre[k] = v.cam.eye[k] + dv[k] / dl * box / 3.0f;
    // the original's drops and their copies, wrapped into the box
    static std::vector<omk::WeatherDrop> all;
    all.assign(rainDrops.begin(), rainDrops.end());
    for (const auto& d : rainDrops)
        for (const auto& o : kRainOffsets) {
            omk::WeatherDrop c = d;
            for (int k = 0; k < 3; ++k) {
                float rel = d.p[k] - centre[k] + o[k] * box / kRainBox;
                rel -= box * std::floor((rel + box * 0.5f) / box);
                c.p[k] = centre[k] + rel;
                c.q[k] = c.p[k] + (d.q[k] - d.p[k]);
            }
            all.push_back(c);
        }
    // the streak three times the original's 1-pixel line at 640x480, scaled
    // with the frame - its texture's soft edge inside that
    const float halfPx = kRainHalfPx * std::sqrt(std::max(1.0f, static_cast<float>(fb.h) / 480.0f));
    const float shower = std::min(1.0f, static_cast<float>(rainDrops.size()) / static_cast<float>(kRainFull));
    int roofed = 0;
    const auto corner = [](const float p[3], const float rgb[3], float u, float tv) {
        omk::Corner c{};
        c.x = p[0]; c.y = p[1]; c.z = p[2];
        c.u = u; c.v = tv;
        c.r = rgb[0]; c.g = rgb[1]; c.b = rgb[2];
        c.phase = -1.0f;
        return c;
    };
    // the light at `p`: `base` grey, and the set's own lights that reach it
    // added - each as `ramp[c][t]` with `t` its whole strength - then the
    // EMITTERS' - the neon, the glowing surfaces, the flames, in the slot's
    // table when the remaster's emitters' light put them there - weighed
    // `kRainNeon` and taking the grey base's place as they grow: a drop by a
    // pink sign glows pink (the reader's "rain effect on neon", 2026-10-27).
    // Whether the set's lights reached it, and the emitters' luminance
    const auto lightAt = [&](const float p[3], float base, float rgb[3]) {
        bool lit = false;
        float neon[3] = {0.0f, 0.0f, 0.0f};
        for (int c = 0; c < 3; ++c) rgb[c] = base;
        for (int part = 0; part < 2; ++part) {
            const std::span<const omk::Light3do> ls = part == 0 ? lights.first(setLightCount)
                                                                : lights.subspan(setLightCount);
            reach.clear();
            if (ls.empty() || omk::lightReach(p, ls, reach) == 0) continue;
            if (part == 0) lit = true;
            for (std::size_t i = 0; i + 7 < reach.size(); i += 8) {
                const float k = std::sqrt(reach[i] * reach[i] + reach[i + 1] * reach[i + 1] + reach[i + 2] * reach[i + 2]);
                const int t = static_cast<int>(std::min(255.0f, k));
                for (int c = 0; c < 3; ++c) {
                    const float add = omk::lightRamp(static_cast<std::uint8_t>(reach[i + 4 + c]), t) / 255.0f;
                    (part == 0 ? rgb[c] : neon[c]) += (part == 0 ? kRainLit : kRainNeon) * add;
                }
            }
        }
        const float neonL = std::min(1.0f, 0.299f * neon[0] + 0.587f * neon[1] + 0.114f * neon[2]);
        for (int c = 0; c < 3; ++c) rgb[c] = rgb[c] * (1.0f - 0.7f * neonL) + neon[c];
        return std::pair<bool, float>{lit, neonL};
    };
    for (const auto& d : all) {
        if (shot && boxes) {
            const double p0[3] = {d.p[0], d.p[1], d.p[2]};
            const double up[3] = {0.0, -kRainRoofRay, 0.0};   // Y points down
            if (omk::sweepSphere(*shot, *boxes, p0, up, 0.0).has_value()) { ++roofed; continue; }
        }
        if (sand) {
            // THE GRAIN: a soft dot facing the eye - the grain's own picture in
            // the field, filtered, additive - small, in the dust (the reader:
            // streaks read as rain, dots alone as snow; with the dust fog,
            // fine particles of the texture, 2026-10-27)
            const std::size_t gi = static_cast<std::size_t>(&d - all.data()) % sandGrains3d.size();
            const omk::GrainRect& gr = sandGrains3d[gi];
            const float fwd[3] = {dv[0] / dl, dv[1] / dl, dv[2] / dl};
            const float upW[3] = {0.0f, -1.0f, 0.0f};
            float r[3] = {fwd[1] * upW[2] - fwd[2] * upW[1], fwd[2] * upW[0] - fwd[0] * upW[2],
                          fwd[0] * upW[1] - fwd[1] * upW[0]};
            const float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
            if (!(rl > 0.0f)) continue;
            for (float& k : r) k /= rl;
            const float u[3] = {r[1] * fwd[2] - r[2] * fwd[1], r[2] * fwd[0] - r[0] * fwd[2],
                                r[0] * fwd[1] - r[1] * fwd[0]};
            const omk::Projected pf = omk::project(v.cam, d.p);
            if (pf.z <= 0.0f) continue;
            const float nearT = std::clamp((pf.z - kSnowNearFrom) / (kSnowNear - kSnowNearFrom), 0.0f, 1.0f);
            const float fade = nearT * nearT * (3.0f - 2.0f * nearT) *
                               std::clamp((box - pf.z) / (box * 0.4f), 0.0f, 1.0f);
            if (!(fade > 0.0f)) continue;
            float rgb[3] = {kRainUnlit * 1.6f, kRainUnlit * 1.6f, kRainUnlit * 1.6f};
            reach.clear();
            if (!lights.empty() && omk::lightReach(d.p, lights, reach) > 0) {
                ++litDrops;
                for (std::size_t i = 0; i + 7 < reach.size(); i += 8) {
                    const float k = std::sqrt(reach[i] * reach[i] + reach[i + 1] * reach[i + 1] + reach[i + 2] * reach[i + 2]);
                    const int t = static_cast<int>(std::min(255.0f, k));
                    for (int c = 0; c < 3; ++c)
                        rgb[c] += kRainLit * omk::lightRamp(static_cast<std::uint8_t>(reach[i + 4 + c]), t) / 255.0f;
                }
            }
            const float top = std::max({rgb[0], rgb[1], rgb[2], 1.0f});
            for (float& c : rgb) c = c / top * std::clamp(rain3dStrength, 0.0f, 1.0f) * kSandGlow * fade;
            // the grain and a texel of black round it, so the filter softens its
            // edge - no bigger on screen than `kSandGrainPx` (what one unit
            // across measures at its depth)
            float hx = kSandGrain3d * (gr.w + 2) / 8.0f, hy = kSandGrain3d * (gr.h + 2) / 8.0f;
            {
                const float off[3] = {d.p[0] + r[0], d.p[1] + r[1], d.p[2] + r[2]};
                const omk::Projected po = omk::project(v.cam, off);
                const float ppu = std::hypot(po.x - pf.x, po.y - pf.y);
                const float capW = kSandGrainPx * std::max(1.0f, static_cast<float>(fb.h) / 480.0f);
                if (ppu > 0.0f && hx * ppu > capW) { const float k = capW / (hx * ppu); hx *= k; hy *= k; }
            }
            float q[4][3];
            for (int k = 0; k < 3; ++k) {
                q[0][k] = d.p[k] - r[k] * hx - u[k] * hy;
                q[1][k] = d.p[k] + r[k] * hx - u[k] * hy;
                q[2][k] = d.p[k] + r[k] * hx + u[k] * hy;
                q[3][k] = d.p[k] - r[k] * hx + u[k] * hy;
            }
            const float u0 = static_cast<float>(gr.x - 1), u1 = static_cast<float>(gr.x + gr.w + 1);
            const float v0 = static_cast<float>(gr.y - 1), v1 = static_cast<float>(gr.y + gr.h + 1);
            const omk::Corner c0 = corner(q[0], rgb, u0, v1), c1 = corner(q[1], rgb, u1, v1);
            const omk::Corner c2 = corner(q[2], rgb, u1, v0), c3 = corner(q[3], rgb, u0, v0);
            rainGeo.corners.insert(rainGeo.corners.end(), {c0, c1, c2, c0, c2, c3});
            continue;
        }
        if (snow) {
            // THE FLAKE: a quad facing the eye, the original's 8 units across
            const float fwd[3] = {dv[0] / dl, dv[1] / dl, dv[2] / dl};
            const float upW[3] = {0.0f, -1.0f, 0.0f};   // Y points down
            float r[3] = {fwd[1] * upW[2] - fwd[2] * upW[1], fwd[2] * upW[0] - fwd[0] * upW[2],
                          fwd[0] * upW[1] - fwd[1] * upW[0]};
            const float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
            if (!(rl > 0.0f)) continue;
            for (float& k : r) k /= rl;
            const float u[3] = {r[1] * fwd[2] - r[2] * fwd[1], r[2] * fwd[0] - r[0] * fwd[2],
                                r[0] * fwd[1] - r[1] * fwd[0]};
            const omk::Projected pf = omk::project(v.cam, d.p);
            if (pf.z <= 0.0f) continue;
            const float nearT = std::clamp((pf.z - kSnowNearFrom) / (kSnowNear - kSnowNearFrom), 0.0f, 1.0f);
            const float fade = nearT * nearT * (3.0f - 2.0f * nearT) *
                               std::clamp((box - pf.z) / (box * 0.4f), 0.0f, 1.0f);
            if (!(fade > 0.0f)) continue;
            float rgb[3] = {kRainUnlit, kRainUnlit, kRainUnlit};
            reach.clear();
            if (!lights.empty() && omk::lightReach(d.p, lights, reach) > 0) {
                ++litDrops;
                for (std::size_t i = 0; i + 7 < reach.size(); i += 8) {
                    const float k = std::sqrt(reach[i] * reach[i] + reach[i + 1] * reach[i + 1] + reach[i + 2] * reach[i + 2]);
                    const int t = static_cast<int>(std::min(255.0f, k));
                    for (int c = 0; c < 3; ++c)
                        rgb[c] += kRainLit * omk::lightRamp(static_cast<std::uint8_t>(reach[i + 4 + c]), t) / 255.0f;
                }
            }
            const float top = std::max({rgb[0], rgb[1], rgb[2], 1.0f});
            for (float& c : rgb) c = c / top * s * fade;
            const float h = kFlakeHalf, T = 64.0f;
            float q[4][3];
            for (int k = 0; k < 3; ++k) {
                q[0][k] = d.p[k] - r[k] * h - u[k] * h;
                q[1][k] = d.p[k] + r[k] * h - u[k] * h;
                q[2][k] = d.p[k] + r[k] * h + u[k] * h;
                q[3][k] = d.p[k] - r[k] * h + u[k] * h;
            }
            const omk::Corner c0 = corner(q[0], rgb, 0.0f, T), c1 = corner(q[1], rgb, T, T);
            const omk::Corner c2 = corner(q[2], rgb, T, 0.0f), c3 = corner(q[3], rgb, 0.0f, 0.0f);
            rainGeo.corners.insert(rainGeo.corners.end(), {c0, c1, c2, c0, c2, c3});
            continue;
        }
        // the streak: from the drop's leading end - the original line's bright
        // one, its tail ahead along the wind - back `kRainLength` against it
        float a[3] = {d.q[0] - d.p[0], d.q[1] - d.p[1], d.q[2] - d.p[2]};
        float al = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        if (!(al > 0.0f)) continue;
        for (float& k : a) k /= al;
        // ...leaning with the GUST where it is (Y down: the fall is +y, the
        // lean along the original wind's x)
        a[0] += remasterGust(weatherClockMs, d.p[0], shower) * a[1];
        al = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        for (float& k : a) k /= al;
        const float lead[3] = {d.q[0], d.q[1], d.q[2]};
        const float trail[3] = {lead[0] - a[0] * kRainLength, lead[1] - a[1] * kRainLength,
                                lead[2] - a[2] * kRainLength};
        // the strip's side: across the streak and the eye's line to it
        const float e[3] = {v.cam.eye[0] - lead[0], v.cam.eye[1] - lead[1], v.cam.eye[2] - lead[2]};
        float side[3] = {a[1] * e[2] - a[2] * e[1], a[2] * e[0] - a[0] * e[2], a[0] * e[1] - a[1] * e[0]};
        const float sl = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (!(sl > 0.0f)) continue;
        for (float& k : side) k /= sl;
        // ...as wide as `halfPx` there: what one unit across measures on screen
        const float off[3] = {lead[0] + side[0], lead[1] + side[1], lead[2] + side[2]};
        const omk::Projected pa = omk::project(v.cam, lead), pb = omk::project(v.cam, off);
        if (pa.z <= 0.0f || pb.z <= 0.0f) continue;   // behind the eye
        const float ppu = std::hypot(pb.x - pa.x, pb.y - pa.y);
        if (!(ppu > 0.0f)) continue;
        const float w = halfPx / ppu;
        // seen through: faint near the eye and far off
        const float nearT = std::clamp((pa.z - kRainNearFrom) / (kRainNear - kRainNearFrom), 0.0f, 1.0f);
        const float fade = nearT * nearT * (3.0f - 2.0f * nearT) *
                           std::clamp((box - pa.z) / (box * 0.4f), 0.0f, 1.0f);
        if (!(fade > 0.0f)) continue;
        // the original line's grey at its bright end (0xC0), dimmed where
        // nothing lights it, and the lights that reach the drop added; the
        // streak's texture shaping it all
        float rgb[3];
        const auto [lit, neonL] = lightAt(lead, 192.0f / 255.0f * kRainUnlit, rgb);
        if (lit) ++litDrops;
        if (neonL > 0.05f) ++neonDrops;
        // past white, scaled down whole rather than clipped per channel: the
        // burner's drops stay orange instead of going white - and a drop in a
        // neon's light catches it, brighter (up to 1.8x)
        const float top = std::max({rgb[0], rgb[1], rgb[2], 1.0f});
        // ...and each drop its own brightness, `kRainOwnMin`..1, kept as it falls (a
        // drop is its index): rain is many faint drops and a few catching
        // the light, not one even field of bright ones
        std::uint32_t hv = static_cast<std::uint32_t>(&d - all.data()) * 0x9E3779B1u;
        hv ^= hv >> 15;
        const float own = kRainOwnMin + (1.0f - kRainOwnMin) * static_cast<float>(hv & 0xFFu) / 255.0f;
        for (float& c : rgb) c = std::min(1.0f, c / top * s * fade * own * (1.0f + 0.8f * neonL));
        const float W = static_cast<float>(kStreakW), H = static_cast<float>(kStreakH);
        const float l0[3] = {lead[0] - side[0] * w, lead[1] - side[1] * w, lead[2] - side[2] * w};
        const float l1[3] = {lead[0] + side[0] * w, lead[1] + side[1] * w, lead[2] + side[2] * w};
        const float t0[3] = {trail[0] - side[0] * w, trail[1] - side[1] * w, trail[2] - side[2] * w};
        const float t1[3] = {trail[0] + side[0] * w, trail[1] + side[1] * w, trail[2] + side[2] * w};
        const omk::Corner c0 = corner(l0, rgb, 0.0f, 0.0f), c1 = corner(l1, rgb, W, 0.0f);
        const omk::Corner c2 = corner(t0, rgb, 0.0f, H), c3 = corner(t1, rgb, W, H);
        rainGeo.corners.insert(rainGeo.corners.end(), {c0, c1, c3, c0, c3, c2});
    }
    const std::size_t drawnDrops = rainGeo.corners.size() / 6;
    // THE DRIPS off the roofs' edges (the reader's "ok for the drips",
    // 2026-10-28): from each `DripPoint` near the eye a drop at a time falls
    // under gravity, a streak as long as its speed blurs it, lit as a rain
    // drop, more of them the heavier the shower - and lands in a splash. On
    // the weather's clock (held while paused). `OMK_NO_DRIPS=1`, an
    // INSTRUMENT, none (`verify.py: engine: rain 3d` part 16)
    static const bool noDrips = omk::envFlag("OMK_NO_DRIPS");
    const bool noSplashSpawn = omk::envFlag("OMK_NO_SPLASHES");   // (the splashes' own instrument)
    int drawnDrips = 0;
    const double dripNow = weatherClockMs / 1000.0;
    const double dripWas = dripClock < 0.0 || weatherClockMs < dripClock ? -1.0 : dripClock / 1000.0;
    dripClock = weatherClockMs;
    if (!snow && !sand && !noDrips && drips && !drips->empty()) {
        for (std::size_t i = 0; i < drips->size(); ++i) {
            const omk::DripPoint& dp = (*drips)[i];
            const float ex = dp.x - v.cam.eye[0], ez = dp.z - v.cam.eye[2];
            if (ex * ex + ez * ez > kDripFar * kDripFar) continue;
            std::uint32_t h = static_cast<std::uint32_t>(i) * 0x9E3779B1u;
            h ^= h >> 15; h *= 0x85EBCA6Bu; h ^= h >> 13;
            if (static_cast<float>(h & 0xFFu) / 255.0f > shower) continue;   // fewer in a light shower
            const double fall = static_cast<double>(dp.floor - dp.top);
            const double tf = std::sqrt(2.0 * fall / kDripG);
            const double period = tf + kDripPeriodMin +
                                  (kDripPeriodMax - kDripPeriodMin) * static_cast<double>((h >> 8) & 0xFFu) / 255.0;
            const double off = period * static_cast<double>((h >> 16) & 0xFFFFu) / 65535.0;
            const double ph = std::fmod(dripNow + off, period);
            // ...landed since the last frame: its splash
            if (dripWas >= 0.0 && !noSplashSpawn) {
                const double was = std::fmod(dripWas + off, period);
                if ((was < tf && ph >= tf) || (ph < was && was < tf))
                    rainSplashes.push_back({{dp.x, dp.floor, dp.z}, weatherClockMs});
            }
            if (ph >= tf) continue;   // between its drops
            const float y = dp.top + static_cast<float>(0.5 * kDripG * ph * ph);
            const float len = std::clamp(static_cast<float>(kDripG * ph * kDripStreakSec), kDripLenMin, kDripLenMax);
            const float lead[3] = {dp.x, y, dp.z};
            const float trail[3] = {dp.x, std::max(dp.top, y - len), dp.z};   // above it (Y down)
            const float a[3] = {0.0f, 1.0f, 0.0f};   // falling straight down
            const float e[3] = {v.cam.eye[0] - lead[0], v.cam.eye[1] - lead[1], v.cam.eye[2] - lead[2]};
            float side[3] = {a[1] * e[2] - a[2] * e[1], a[2] * e[0] - a[0] * e[2], a[0] * e[1] - a[1] * e[0]};
            const float sl = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
            if (!(sl > 0.0f)) continue;
            for (float& k : side) k /= sl;
            const float offp[3] = {lead[0] + side[0], lead[1] + side[1], lead[2] + side[2]};
            const omk::Projected pa = omk::project(v.cam, lead), pb = omk::project(v.cam, offp);
            if (pa.z <= 0.0f || pb.z <= 0.0f) continue;
            const float ppu = std::hypot(pb.x - pa.x, pb.y - pa.y);
            if (!(ppu > 0.0f)) continue;
            const float w = kDripWide * halfPx / ppu;
            const float nearT = std::clamp((pa.z - kRainNearFrom) / (kRainNear - kRainNearFrom), 0.0f, 1.0f);
            const float fade = nearT * nearT * (3.0f - 2.0f * nearT) *
                               std::clamp((kDripFar - pa.z) / (kDripFar * 0.3f), 0.0f, 1.0f);
            if (!(fade > 0.0f)) continue;
            float rgb[3];
            lightAt(lead, 192.0f / 255.0f * kRainUnlit, rgb);
            const float top = std::max({rgb[0], rgb[1], rgb[2], 1.0f});
            for (float& c : rgb) c = std::min(1.0f, c / top * s * fade * kDripGlow);
            const float W = static_cast<float>(kStreakW), H = static_cast<float>(kStreakH);
            const float l0[3] = {lead[0] - side[0] * w, lead[1] - side[1] * w, lead[2] - side[2] * w};
            const float l1[3] = {lead[0] + side[0] * w, lead[1] + side[1] * w, lead[2] + side[2] * w};
            const float t0[3] = {trail[0] - side[0] * w, trail[1] - side[1] * w, trail[2] - side[2] * w};
            const float t1[3] = {trail[0] + side[0] * w, trail[1] + side[1] * w, trail[2] + side[2] * w};
            const omk::Corner c0 = corner(l0, rgb, 0.0f, 0.0f), c1 = corner(l1, rgb, W, 0.0f);
            const omk::Corner c2 = corner(t0, rgb, 0.0f, H), c3 = corner(t1, rgb, W, H);
            rainGeo.corners.insert(rainGeo.corners.end(), {c0, c1, c3, c0, c3, c2});
            ++drawnDrips;
        }
    }
    // THE SPLASHES - the rain's alone: spawned as the weather's clock runs
    // (so none while paused, and none from the skip's jump), aged on it
    const std::size_t rainCorners = rainGeo.corners.size();
    int splashRoofed = 0;
    static const bool noSplashes = omk::envFlag("OMK_NO_SPLASHES");
    if (snow || sand || noSplashes || !shot || !boxes) {
        rainSplashes.clear();
        splashClock = -1.0;
    } else {
        const double now = weatherClockMs;
        const double dt = splashClock < 0.0 ? 0.0 : std::clamp(now - splashClock, 0.0, kSplashStepMax);
        splashClock = now;
        std::erase_if(rainSplashes, [&](const RainSplash& sp) { return now - sp.born >= kSplashLife; });
        splashOwed += kSplashRate * dt / 1000.0 * std::min(1.0, static_cast<double>(rainDrops.size()) / kRainFull);
        const auto rnd = [&] {
            splashSeed = splashSeed * 1664525u + 1013904223u;
            return static_cast<float>(splashSeed >> 8) / 16777216.0f;
        };
        const float heading = std::atan2(dv[2], dv[0]);
        for (; splashOwed >= 1.0; splashOwed -= 1.0) {
            // uniform over the half-disc's area, then down to the floor
            const float r = std::sqrt(kSplashNear * kSplashNear +
                                      rnd() * (kSplashFar * kSplashFar - kSplashNear * kSplashNear));
            const float a = heading + (rnd() * 2.0f - 1.0f) * kSplashHalfAngle;
            const double bornAt = now - rnd() * dt;   // spread over the frame, so they do not pulse
            const double p0[3] = {v.cam.eye[0] + std::cos(a) * r, v.cam.eye[1], v.cam.eye[2] + std::sin(a) * r};
            const double down[3] = {0.0, kRainRoofRay, 0.0};   // Y points down
            const auto ground = omk::sweepSphere(*shot, *boxes, p0, down, 0.0);
            if (!ground || ground->n[1] > kSplashFloorNy) continue;   // nothing below, or a wall's slope
            const double fy = p0[1] + down[1] * ground->t;
            const double from[3] = {p0[0], fy - kSplashSkyFrom, p0[2]};
            const double up[3] = {0.0, -kRainRoofRay, 0.0};
            if (omk::sweepSphere(*shot, *boxes, from, up, 0.0).has_value()) { ++splashRoofed; continue; }
            rainSplashes.push_back({{static_cast<float>(p0[0]), static_cast<float>(fy), static_cast<float>(p0[2])},
                                    bornAt});
        }
        // each a crown standing on the floor, turned to the eye, rising as it fades
        const float glow = std::clamp(rain3dStrength, 0.0f, 1.0f) * kSplashGlow;
        const float S = static_cast<float>(kSplashTex);
        for (const auto& sp : rainSplashes) {
            const float t = static_cast<float>(std::clamp((now - sp.born) / kSplashLife, 0.0, 1.0));
            const float ex = v.cam.eye[0] - sp.p[0], ez = v.cam.eye[2] - sp.p[2];
            const float el = std::hypot(ex, ez);
            if (!(el > 1.0f)) continue;
            const float rx = ez / el, rz = -ex / el;   // across the eye's line, level
            const float fade = std::clamp((kSplashFar - el) / (kSplashFar * kSplashFarFade), 0.0f, 1.0f) *
                               (1.0f - t) * (1.0f - t);
            if (!(fade > 0.0f)) continue;
            // each its own size and way round, from where it fell
            std::uint32_t h = static_cast<std::uint32_t>(static_cast<std::int32_t>(sp.p[0] * 7.0f)) * 73856093u ^
                              static_cast<std::uint32_t>(static_cast<std::int32_t>(sp.p[2] * 7.0f)) * 19349663u;
            h ^= h >> 13; h *= 0x5BD1E995u; h ^= h >> 15;
            const float size = sp.size * (kSplashSizeMin + (kSplashSizeMax - kSplashSizeMin) *
                                                static_cast<float>(h & 0xFFu) / 255.0f);
            const bool mirror = (h & 0x100u) != 0;
            const float rise = std::sqrt(t);
            const float hw = size * kSplashW * (kSplashBornW + (1.0f - kSplashBornW) * rise);
            const float hh = size * kSplashH * (kSplashBornH + (1.0f - kSplashBornH) * rise);
            const float mid[3] = {sp.p[0], sp.p[1] - hh * 0.5f, sp.p[2]};
            float rgb[3];
            lightAt(mid, kRainUnlit, rgb);
            const float top = std::max({rgb[0], rgb[1], rgb[2], 1.0f});
            for (float& c : rgb) c = std::min(1.0f, c / top * glow * sp.glow * fade);
            const float foot = sp.p[1] - kSplashFoot, head = sp.p[1] - hh;
            const float q0[3] = {sp.p[0] - rx * hw, foot, sp.p[2] - rz * hw};
            const float q1[3] = {sp.p[0] + rx * hw, foot, sp.p[2] + rz * hw};
            const float q2[3] = {sp.p[0] + rx * hw, head, sp.p[2] + rz * hw};
            const float q3[3] = {sp.p[0] - rx * hw, head, sp.p[2] - rz * hw};
            const float uA = mirror ? S : 0.0f, uB = S - uA;
            const omk::Corner c0 = corner(q0, rgb, uA, S), c1 = corner(q1, rgb, uB, S);
            const omk::Corner c2 = corner(q2, rgb, uB, 0.0f), c3 = corner(q3, rgb, uA, 0.0f);
            rainGeo.corners.insert(rainGeo.corners.end(), {c0, c1, c2, c0, c2, c3});
        }
    }
    const std::size_t drawnSplashes = (rainGeo.corners.size() - rainCorners) / 6;
    if (frameNo % 30 == 0) {
        std::printf("frame %ld: the %s in the scene - %zu of %zu %s drawn (the weather's %zu and %d each "
                    "more), %d under a roof, %d lit by the set's lights; %zu splashes on the floor; %d drips "
                    "off the roofs\n", frameNo,
                    snow ? "snow" : sand ? "sand" : "rain", drawnDrops, all.size(),
                    snow ? "flakes" : sand ? "grains" : "drops", rainDrops.size(), kRainCopies, roofed,
                    litDrops, drawnSplashes, drawnDrips);
        omk::event("remaster.rain3d", {{"frame", frameNo}, {"drops", static_cast<long>(rainDrops.size())},
                                       {"drawn", static_cast<long>(drawnDrops)}, {"roofed", roofed},
                                       {"gust", static_cast<long>(std::lround(
                                                    1000.0f * remasterGust(weatherClockMs, v.cam.eye[0], shower)))},
                                       {"streak", (snow ? snowTexSlot : rainTexSlot) >= 0}, {"lit", litDrops},
                                       {"snow", snow}, {"sand", sand}, {"neon", neonDrops},
                                       {"splashes", static_cast<long>(drawnSplashes)},
                                       {"splashRoofed", splashRoofed}, {"drips", drawnDrips}});
    }
    if (rainGeo.corners.empty()) return;
    ++rainGeo.revision;
    // the streak's pool slot (`remasterPoolRain`) - or, before the pool has
    // it, a slot past every texture, which binds the backends' white; the
    // additive state bits as the mesh path's (`keyOf`, 0x2100). The drops
    // first, then the splashes with their crown
    const auto add = [&](int texSlot, std::size_t start, std::size_t count) {
        if (count == 0) return;
        omk::Batch b;
        b.material = -1;
        b.blend = omk::Blend::Add;   // the rain, the snow and the sand's streaks additive, the splashes too
        b.start = start;
        b.count = count;
        rainGeo.batches.push_back(b);
        const std::uint32_t slot = texSlot >= 0 ? static_cast<std::uint32_t>(texSlot) : 0xFFFEu;
        omk::Draw dr;
        dr.bucketKey = omk::withPoolSlot(0x2100u | (slot & 0x3Fu), slot);
        dr.geo = &rainGeo;
        dr.start = start;
        dr.count = count;
        dr.blend = omk::Blend::Add;
        list.push_back(dr);
    };
    // `OMK_NO_RAIN_STRIPS=1`, an INSTRUMENT: the drops' strips left out of
    // the draw (built and counted all the same), the splashes kept - what
    // the strips alone cost the GPU
    static const bool noStrips = omk::envFlag("OMK_NO_RAIN_STRIPS");
    if (!noStrips) add(snow ? snowTexSlot : sand ? sandTexSlot : rainTexSlot, 0, rainCorners);
    add(splashTexSlot, rainCorners, rainGeo.corners.size() - rainCorners);
}

#endif  // OMK_REMASTER

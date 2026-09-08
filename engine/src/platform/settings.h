// SPDX-License-Identifier: GPL-3.0-or-later
// The resolved game settings, and where each one came from.
//
// The game keeps its options in TWO places, and a port has to know which is
// which (`todo/options-config.md`, GAME_STATE 8a):
//
//   * the `.ini` - 65 `[Preferences]` keys read with `GetPrivateProfileStringA`
//     at boot, written by the setup dialog `Runtime.exe CONFIG` opens;
//   * the SAVE FILE's 3496-byte header - the global `byte_90E180`, which
//     `Game_WriteSave` rewrites on every slot save and `SaveDir_Load` reads
//     back.  This is what the options MENU edits, and it carries all 74 rows'
//     state including the three control-scheme tables.
//
// They overlap on three of the five graphical rows (`clipdistance`,
// `displaysky`, `displayshadows`).  The ini is what the game boots with and
// the header is what the menu last wrote, so the header is the LATER of the
// two and wins for anything the player has touched:
//
//     defaults (sub_41F4C0)  <-  [Preferences]  <-  the save header
//
// `Source` records which of the three actually supplied each field, because a
// setting that silently came from the wrong place is the kind of error that
// only shows up as "the config file does nothing".
//
// TWO SETTINGS HAVE NO INI KEY: the crowd density (row 6) and the level of
// detail (row 7).  They are in the save header, so a player who has saved has
// them - but a fresh tree with only a config file has no way to say them.
// This port adds an `[Options]` section of ITS OWN for those two, which is
// marked as this port's addition wherever it appears; it sits at the ini's
// precedence level, so a save still wins.
//
// AND A THIRD SECTION, `[Enhancements]`, FOR WHAT THE ORIGINAL NEVER HAD.
// `[Options]` stands in for real menu rows; `[Enhancements]` is for options
// the 1999 renderer has no state for at all - the first is anti-aliasing,
// which `sub_4638C0` explicitly turns OFF (`docs/ASSETS.md` 4). Everything
// in it defaults to OFF, because the replica is judged against the original
// and an enhancement on by default would make every comparison a comparison
// against something the game never drew. The save header cannot carry them,
// so the ini and the command line are their only sources.
#pragma once

#include "platform/options.h"
#include "script/savefile.h"

#include <optional>
#include <string>

namespace omk {

// The world unit is an INCH and the clip distance is in METRES - the engine
// converts with this exact literal (`04_sys.c`, `05_sys.c`), which is 1/0.0254.
inline constexpr double kInchesPerMetre = 39.37007874015748;

struct Settings {
    enum class Source { Default, Ini, Save };

    SettingsBlock v{};
    // Per field, whichever source last wrote it.  Only the fields either the
    // ini or the header can carry are listed.
    Source clipDistance = Source::Default;
    Source sky = Source::Default;
    Source shadows = Source::Default;
    Source screen = Source::Default;
    Source streetActivity = Source::Default;
    Source levelOfDetail = Source::Default;
    Source mouseSensitivity = Source::Default;
    Source volumes = Source::Default;
    Source bindings = Source::Default;

    // ---- [Enhancements] - OFF unless the file or a flag says so ----------
    // `antialiasing = N`: MSAA samples per pixel, 0 off, else 2/4/8. Only the
    // Vulkan backend honours it; the software reference stands where D3D
    // stood and draws what the original drew.
    int    antiAliasing = 0;
    Source antiAliasingSource = Source::Default;
    // `texturefiltering = nearest|bilinear|trilinear` (or 0|1|2): the
    // original is point-sampled (MAG/MIN POINT, MIP NONE), so 0 is the
    // game's picture; 2 is a generated mip chain.
    int    textureFilter = 0;
    Source textureFilterSource = Source::Default;
    // `anisotropy = N` (1..16, 1 off): with trilinear only.
    int    anisotropy = 1;
    Source anisotropySource = Source::Default;
    // `shadowquality = classic|fitted|mapped` (or 0|1|2), and it is
    // SUBORDINATE to the game's own option row 5: `v.shadows` says whether a
    // character casts one at all, this says how it is drawn. 0 is what the
    // engine draws - a flat quad per bone laid at the height probed under the
    // bone's own centre - so the default changes nothing.
    int    shadowQuality = 0;
    Source shadowQualitySource = Source::Default;
    // `lighting = pervertex|perpixel` (or 0|1). 0 is what the engine does -
    // the `.3DO` lights applied per VERTEX and to the procedural crowd alone,
    // which is where all eight of `sub_4380B0`'s call sites are. 1 evaluates
    // the same law per fragment and lets every character receive it.
    int    lighting = 0;
    Source lightingSource = Source::Default;

    // ---- what the clip distance DERIVES, all in world units (inches) ----
    //
    // `sub_440BE0(scene, D, 1)` writes three floats on the scene, and D is
    // `clipdistance * kInchesPerMetre`:
    //
    //     +340 = D            the far distance
    //     +328 = D * 0.25     `bucketKey`'s nearSplit, and the FOG START
    //     +332 = D * 0.95     `bucketKey`'s farSplit
    //
    // The per-set path (`05_sys.c`) shows the option is a CAP rather than a
    // value: a set's own clip distance is used unless it is 0 or exceeds the
    // option, in which case the option wins.  So this can only ever reduce.
    double clipInches() const { return static_cast<double>(v.clipDistance) * kInchesPerMetre; }
    double nearSplit()  const { return clipInches() * 0.25; }
    double farSplit()   const { return clipInches() * 0.95; }
    // The fog is LINEAR (`D3DRENDERSTATE_FOGTABLEMODE` 3) with density 1.0,
    // from `+328` to `+340` - so it ENDS at the clip distance, not at the
    // 0.95 split.  Not drawn yet; step 4 of `todo/options-config.md`.
    double fogStart()   const { return nearSplit(); }
    double fogEnd()     const { return clipInches(); }
};

// An anti-aliasing request folded onto what a backend can be asked for:
// 0 (or 1) is off, anything else the largest of 2/4/8 not above it.
inline int msaaSamples(int n) { return n < 2 ? 0 : n < 4 ? 2 : n < 8 ? 4 : 8; }

// A texture-filter word onto the mode a backend is asked for: 0 nearest,
// 1 bilinear, 2 trilinear; -1 for a word that is none of them, so a typo
// does not silently become the default.
inline int textureFilterMode(std::string w) {
    for (auto& c : w) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    while (!w.empty() && (w.back() == ' ' || w.back() == '\t')) w.pop_back();
    while (!w.empty() && (w.front() == ' ' || w.front() == '\t')) w.erase(w.begin());
    if (w == "0" || w == "nearest" || w == "point" || w == "off") return 0;
    if (w == "1" || w == "bilinear" || w == "linear") return 1;
    if (w == "2" || w == "trilinear" || w == "mipmap" || w == "mipmaps") return 2;
    return -1;
}
inline const char* textureFilterName(int m) {
    return m <= 0 ? "nearest" : m == 1 ? "bilinear" : "trilinear";
}

// A shadow-quality word onto its mode: 0 classic, 1 fitted, 2 mapped; -1 for
// a word that is none of them, so a typo does not silently become classic.
inline int shadowQualityMode(std::string w) {
    for (auto& c : w) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    while (!w.empty() && (w.back() == ' ' || w.back() == '\t')) w.pop_back();
    while (!w.empty() && (w.front() == ' ' || w.front() == '\t')) w.erase(w.begin());
    if (w == "0" || w == "classic" || w == "off" || w == "original") return 0;
    if (w == "1" || w == "fitted" || w == "ground") return 1;
    if (w == "2" || w == "mapped" || w == "shadowmap") return 2;
    return -1;
}
inline const char* shadowQualityName(int m) {
    return m <= 0 ? "classic" : m == 1 ? "fitted" : "mapped";
}

// A lighting word onto its mode: 0 per vertex (the engine's), 1 per pixel;
// -1 for a word that is neither.
inline int lightingMode(std::string w) {
    for (auto& c : w) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    while (!w.empty() && (w.back() == ' ' || w.back() == '\t')) w.pop_back();
    while (!w.empty() && (w.front() == ' ' || w.front() == '\t')) w.erase(w.begin());
    if (w == "0" || w == "pervertex" || w == "vertex" || w == "classic") return 0;
    if (w == "1" || w == "perpixel" || w == "pixel") return 1;
    return -1;
}
inline const char* lightingName(int m) { return m <= 0 ? "pervertex" : "perpixel"; }

// Resolve the three sources in order.  Either may be absent.
Settings resolveSettings(const OptionsFile& ini,
                         const std::optional<SettingsBlock>& save);

const char* sourceName(Settings::Source s);

}  // namespace omk

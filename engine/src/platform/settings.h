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

// Resolve the three sources in order.  Either may be absent.
Settings resolveSettings(const OptionsFile& ini,
                         const std::optional<SettingsBlock>& save);

const char* sourceName(Settings::Source s);

}  // namespace omk

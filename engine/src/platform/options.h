// SPDX-License-Identifier: GPL-3.0-or-later
// The game's own configuration file - `[Preferences]`, and the 65 keys the
// engine reads out of it.
//
// `GetPrivateProfileStringA` is not only the `.TAG` logger the golden-trace
// rig leans on. The engine reads a `[Preferences]` section through the same
// API, and also `[Debug]` and `[User]` - the last carrying an `id` and the
// developers' own names (ANTOINE, CHRISTOPHE, FABIEN, FRANCOIS, GUEST, MANU,
// OLIVIER), so a build could carry per-person overrides.
//
// **No `.ini` ships in `gamedata/`**: the file is written by the setup dialog,
// which is what `Runtime.exe CONFIG` opens - `WinMain`'s third switch, after
// WINDOW and NOFMV, running `DialogBoxParamA` on template 0x68. So there is
// nothing here to read off a real install; what the binary gives is the KEY
// NAMES, and those are what this file uses rather than anything invented.
//
// The names had to come out of the DATA section, not from IDA's labels, which
// truncate at 14 characters and mislead: `aDisplaypassers` is really
// `displaypassersclock` (a debug readout, NOT the crowd density) and
// `aAmbientattenua` is `AmbientAttenuation`.
//
// **Two of the graphical settings have no key here at all** - the crowd
// density (options row 6, *Niveau d'activité dans les rues*) and the level of
// detail (row 7). Those are menu-only: their apply-hooks write globals, and
// density's is the one `Slider_Init` reads as `39 * (5 - density) * h[3]`.
// This port carries them in its own `[Options]` section, which is marked as
// this port's addition wherever it appears.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace omk {

// The 65 `[Preferences]` keys, exactly as the binary spells them.
const std::vector<std::string>& preferenceKeys();

struct OptionsFile {
    bool loaded = false;
    std::string path;
    // section -> key -> value, both folded to lower case for lookup, because
    // the Win32 profile API matches case-insensitively.
    std::map<std::string, std::map<std::string, std::string>> sections;
    // keys under [Preferences] that the engine does NOT read - a typo in a
    // hand-written file is otherwise silent.
    std::vector<std::string> unknown;

    const std::string* find(const std::string& section, const std::string& key) const;
    int  integer(const std::string& section, const std::string& key, int fallback) const;
    bool boolean(const std::string& section, const std::string& key, bool fallback) const;
};

// Parse an ini. A missing file is not an error: `loaded` stays false and every
// lookup falls back, which is what the engine does when a key is absent.
OptionsFile loadOptionsFile(const std::string& path);

}  // namespace omk

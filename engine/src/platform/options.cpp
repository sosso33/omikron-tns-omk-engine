// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/options.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

namespace omk {

namespace {
std::string lower(std::string v) {
    for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v;
}
std::string trim(const std::string& v) {
    const auto a = v.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const auto b = v.find_last_not_of(" \t\r\n");
    return v.substr(a, b - a + 1);
}
}  // namespace

const std::vector<std::string>& preferenceKeys() {
    static const std::vector<std::string> k = {
    "AmbientAttenuation", "DialogAttenuation", "FlagNoCD",
    "FxAttenuation", "GFXCard", "MouseSensX",
    "MouseSensY", "SoftwareMode", "add_perso1",
    "add_perso10", "add_perso2", "add_perso3",
    "add_perso4", "add_perso5", "add_perso6",
    "add_perso7", "add_perso8", "add_perso9",
    "autocameraplayer", "camera_free", "clipdistance",
    "collidescale", "confirm_exit", "ctlinfos",
    "debug_graph_info", "debug_maps", "debug_script_info",
    "debug_sound", "debug_text_info", "debug_zones",
    "default_animbank", "default_background", "default_character",
    "default_passersbank", "default_passerspath", "default_scx",
    "default_sfx", "default_sfxscx", "disable_fight_ai",
    "disable_fight_damages", "disablebodymotion", "display_colliding_faces",
    "display_doors", "display_nearestperso", "display_path",
    "display_zone", "displaycameracone", "displaycodecount",
    "displayfightcol", "displayframerate", "displaypassersclock",
    "displayshadows", "displaysky", "displaytimetest",
    "exclusive_mouse", "lock_mouse", "music",
    "no_fight_guard", "nomotionfile", "screen_x",
    "screen_y", "ssdir", "tekkencam",
    "viewer", "window"
    };
    return k;
}

const std::string* OptionsFile::find(const std::string& section,
                                     const std::string& key) const {
    const auto s = sections.find(lower(section));
    if (s == sections.end()) return nullptr;
    const auto k = s->second.find(lower(key));
    return k == s->second.end() ? nullptr : &k->second;
}

int OptionsFile::integer(const std::string& section, const std::string& key,
                         int fallback) const {
    const std::string* v = find(section, key);
    if (!v || v->empty()) return fallback;
    // `atoi`, which is what the engine calls on the returned string - a
    // trailing word or a bad value gives 0 rather than an error.
    return std::atoi(v->c_str());
}

bool OptionsFile::boolean(const std::string& section, const std::string& key,
                          bool fallback) const {
    const std::string* v = find(section, key);
    if (!v || v->empty()) return fallback;
    const std::string s = lower(trim(*v));
    if (s == "oui" || s == "yes" || s == "true" || s == "on") return true;
    if (s == "non" || s == "no" || s == "false" || s == "off") return false;
    return std::atoi(s.c_str()) != 0;
}

OptionsFile loadOptionsFile(const std::string& path) {
    OptionsFile o;
    o.path = path;
    std::ifstream in(path);
    if (!in) return o;                  // absent is not an error
    o.loaded = true;
    std::string line, section;
    while (std::getline(in, line)) {
        // a `;` or `#` comment, and the Win32 parser's own tolerance of blanks
        const auto cut = line.find_first_of(";#");
        if (cut != std::string::npos) line = line.substr(0, cut);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string val = trim(line.substr(eq + 1));
        if (key.empty()) continue;
        o.sections[section][key] = val;
    }
    // a key under [Preferences] the engine never reads is almost certainly a
    // typo, and silence is the worst answer to that
    const auto pref = o.sections.find("preferences");
    if (pref != o.sections.end()) {
        for (const auto& kv : pref->second) {
            bool known = false;
            for (const auto& k : preferenceKeys())
                if (lower(k) == kv.first) { known = true; break; }
            if (!known) o.unknown.push_back(kv.first);
        }
        std::sort(o.unknown.begin(), o.unknown.end());
    }
    return o;
}

}  // namespace omk

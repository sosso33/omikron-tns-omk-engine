// SPDX-License-Identifier: GPL-3.0-or-later
// The LOAD PANEL walked - `Charger une partie` driven with the engine's own
// input words, against a real save file.
//
//     load_panel_walk <IAM/GAMES> <tables dir> <out.bin>
//
// `Ui_BuildLoadPanel` (0x0047A6D0) opens the panel with the FIRST profile
// listed and **no row chosen** (`dword_4CEBAC = -1`), and the slot list's hook
// (0x0047AEC0) then gives LEFT/RIGHT a different job from UP/DOWN: at row -1
// they change the PROFILE, which is what the `Joueur :` heading names. So the
// first choice the panel offers is whose saves to look at, and only then which.
#include "platform/datafs.h"
#include "script/savefile.h"
#include "ui/widgets.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: load_panel_walk <IAM/GAMES> <tables> <out.bin>\n");
        return 2;
    }
    const auto w = omk::UiWidgets::loadJson(std::string(argv[2]) + "/ui_widgets.json");
    const auto dir = omk::saveDirectory(argv[1], w);
    auto p = omk::buildLoadPanel(dir);

    const int profiles = static_cast<int>(p.profiles.size());
    const int rows0 = static_cast<int>(p.rows().size());

    // the three rows the panel would draw, as it draws them
    std::vector<std::string> labels;
    for (const auto& e : p.rows()) labels.push_back(p.rowLabel(e));

    // THE CYCLE, recorded rather than counted: the row after each press, for
    // one and a bit turns of the wheel.  -1 is in it, which is the whole
    // point - DOWN off the last row returns to "nothing chosen".
    std::vector<int> down, up;
    for (int i = 0; i < rows0 + 3; ++i) {
        omk::loadPanelInput(p, omk::kUiDown);
        down.push_back(p.row);
    }
    p.row = -1;
    for (int i = 0; i < rows0 + 3; ++i) {
        omk::loadPanelInput(p, omk::kUiUp);
        up.push_back(p.row);
    }

    // Charger on row 0, and on row -1 (nothing chosen), and past the end
    p.row = 0;   const int chargeFirst = omk::loadPanelCharger(p);
    p.row = -1;  const int chargeNone  = omk::loadPanelCharger(p);
    p.row = rows0 - 1; const int chargeLast = omk::loadPanelCharger(p);

    // and the profile wheel at row -1
    p.row = -1;
    int wheel = 0;
    for (int i = 0; i < profiles + 1; ++i)
        if (omk::loadPanelInput(p, omk::kUiRight)) ++wheel;
    const int wrapped = p.profile;

    std::vector<std::byte> o;
    const auto put32 = [&o](std::int32_t v) {
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::byte>((v >> (8 * k)) & 0xFF));
    };
    const auto putStr = [&o, &put32](const std::string& s) {
        put32(static_cast<std::int32_t>(s.size()));
        for (char c : s) o.push_back(static_cast<std::byte>(c));
    };
    put32(static_cast<std::int32_t>(dir.size()));
    put32(profiles); put32(rows0); put32(p.mode);
    putStr(profiles ? p.profiles[0] : std::string());
    put32(static_cast<std::int32_t>(labels.size()));
    for (const auto& L : labels) putStr(L);
    put32(static_cast<std::int32_t>(down.size()));
    for (int v : down) put32(v);
    put32(static_cast<std::int32_t>(up.size()));
    for (int v : up) put32(v);
    put32(chargeFirst); put32(chargeNone); put32(chargeLast);
    put32(wheel); put32(wrapped);

    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));

    std::printf("load panel: %zu directory entries, %d profile(s), %d row(s), "
                "mode %d, listing \"%s\"\n", dir.size(), profiles, rows0, p.mode,
                profiles ? p.profiles[0].c_str() : "-");
    for (const auto& L : labels) std::printf("    %s\n", L.c_str());
    std::printf("walk: DOWN visits");
    for (int v : down) std::printf(" %d", v);
    std::printf(", UP visits");
    for (int v : up) std::printf(" %d", v);
    std::printf("\n      Charger answers %d on row 0, %d on row -1, %d on the "
                "last row; RIGHT at row -1 stepped the profile %d times, "
                "wrapping to %d\n", chargeFirst, chargeNone, chargeLast,
                wheel, wrapped);
    return 0;
}

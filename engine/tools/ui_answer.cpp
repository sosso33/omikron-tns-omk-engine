// SPDX-License-Identifier: GPL-3.0-or-later
// The start menu ANSWERING - the value `boot_intro` has been supplied.
//
//     ui_answer <tables/ui_widgets.json> <out.bin>
//
// `ui.open` suspends the calling script and the screen answers it. The
// simulator used to supply that answer as a literal, which tested the
// suspend/resume mechanism and nothing else; `tools/sim/ui.py` derives it by
// pressing buttons, and this does the same in the port.
//
// Screen 29 is the start menu: confirm on "Nouvelle partie" enters the confirm
// dialog through the item's `+44`, DOWN moves off the name field onto the
// buttons (the dialog's own panel hook moves lists with up/down, not left and
// right), and confirm on "Confirmer" runs the one item callback whose effect
// has been read - it writes 1, which is what the shipped save records for the
// intro's `Interface`.
//
// The name matters. That callback opens by testing the name field's cursor and
// returns without writing anything when it is empty, so the walk must TYPE
// one - and walking it without gives no answer at all, which is asserted here
// as its own case.
//
// out.bin: int32 answerWithName, approxWithName,
//          int32 answerWithoutName, approxWithoutName,
//          int32 nameLen, cappedLen, backspaceLen, returnOnEmpty, returnOnName
#include "platform/datafs.h"
#include "ui/widgets.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ui_answer <ui_widgets.json> <out.bin>\n");
        return 2;
    }
    const auto w = omk::UiWidgets::loadJson(argv[1]);
    if (!w.valid()) { std::fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }

    const auto walkStart = [&](const std::string& name, int& answer, int& approx) {
        omk::UiWalk u(w);
        if (!u.open(29)) { answer = -1; approx = 1; return; }
        const std::uint32_t seq[3] = {omk::kUiConfirm, omk::kUiDown, omk::kUiConfirm};
        for (int i = 0; i < 3; ++i) {
            u.press(seq[i]);
            if (i == 0 && !name.empty()) u.typeName(name);
        }
        answer = u.answer();
        approx = u.approximate() ? 1 : 0;
    };
    int a1 = -1, x1 = 0, a2 = -1, x2 = 0;
    walkStart("Kay'l", a1, x1);
    walkStart("", a2, x2);

    // the field itself, independent of the panel
    omk::NameField f(w.nameSwitch(), w.nameMax());
    f.enter("Kay'l");
    const int nameLen = static_cast<int>(f.text().size());
    omk::NameField g(w.nameSwitch(), w.nameMax());
    g.enter("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123");        // 30 characters
    const int capped = static_cast<int>(g.text().size());
    omk::NameField h(w.nameSwitch(), w.nameMax());
    h.enter("Kay'l");
    h.type('\b');
    const int afterBs = static_cast<int>(h.text().size());
    omk::NameField e1(w.nameSwitch(), w.nameMax());
    const int retEmpty = e1.type('\r') ? 1 : 0;       // RETURN refused on empty
    omk::NameField e2(w.nameSwitch(), w.nameMax());
    e2.enter("Kay'l");
    const int retNamed = e2.type('\r') ? 1 : 0;

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    for (int v : {a1, x1, a2, x2, nameLen, capped, afterBs, retEmpty, retNamed})
        put32(v);
    if (!omk::safeOutputPath(argv[2])) return 2;
    std::ofstream fo(argv[2], std::ios::binary);
    fo.write(reinterpret_cast<const char*>(o.data()),
             static_cast<std::streamsize>(o.size()));

    std::printf("start menu with a name typed: answer %d (approximate: %s)\n",
                a1, x1 ? "yes" : "no");
    std::printf("start menu with NO name:      answer %d (approximate: %s)\n",
                a2, x2 ? "yes" : "no");
    std::printf("name field: \"Kay'l\" is %d chars, 30 typed caps at %d, "
                "backspace leaves %d; RETURN on empty %s, on a name %s\n",
                nameLen, capped, afterBs,
                retEmpty ? "ACCEPTED" : "refused",
                retNamed ? "accepted" : "REFUSED");
    return 0;
}

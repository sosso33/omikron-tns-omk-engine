// SPDX-License-Identifier: GPL-3.0-or-later
// THE SWITCH'S ON-SCREEN KEYBOARD, for the start menu's NAME FIELD
// (`todo/switch-port.md` S3) - the Vita's `backends/vita/ime.cpp` re-said
// for libnx.
//
// A new game will not start without a name: `Confirmer` refuses an empty
// field (docs/UI.md 3f). The field stays the engine's - it takes characters
// through `UiWalk::typeName` - so the system keyboard only PRODUCES them:
// `playframe_modes_parts.cpp` opens it when the field takes the focus and
// feeds the answer back in as typing.
//
// NOT SDL's text input. devkitPro's SDL2 shows libnx's INLINE keyboard for
// as long as text input is on (`SWITCH_StartTextInput`), knows nothing of
// the field's focus, and hands back only what was typed after the name
// already there - the first reader report (2026-11-25): the keyboard came up
// by itself at launch and never again. This is the library applet, MODAL:
// the game waits while it is up, as the original's menu does nothing either
// while a name is typed.
#include <switch.h>

#include <string>
#include <vector>

namespace omk::nx {

// Latin-1 <-> UTF-8: the field's font draws Latin-1 only, the keyboard
// speaks UTF-8. Code points below 256 are the same in both.
static std::string latin1ToUtf8(const std::string& s) {
    std::string u;
    for (unsigned char c : s) {
        if (c < 0x80) {
            u.push_back(static_cast<char>(c));
        } else {
            u.push_back(static_cast<char>(0xC0 | (c >> 6)));
            u.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return u;
}
// ...and back: anything outside Latin-1 is dropped, as the Vita's is
static std::string utf8ToLatin1(const char* u) {
    std::string s;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(u); *p;) {
        unsigned cp = 0;
        int more = 0;
        if (*p < 0x80) { cp = *p; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; more = 1; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; more = 2; }
        else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; more = 3; }
        else { ++p; continue; }   // a stray continuation byte
        ++p;
        for (; more > 0 && (*p & 0xC0) == 0x80; --more, ++p) cp = (cp << 6) | (*p & 0x3F);
        if (more == 0 && cp < 0x100 && cp >= 0x20) s.push_back(static_cast<char>(cp));
    }
    return s;
}

// Open the system keyboard with `initial` in it, at most `maxLen`
// characters, and wait for it. -> true with `out` set when the player
// confirmed, false when they cancelled (or the applet would not start).
bool imeEdit(const char* title, const std::string& initial, int maxLen, std::string& out) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);          // QWERTY, cursor at the end
    swkbdConfigSetHeaderText(&kbd, title);
    swkbdConfigSetGuideText(&kbd, title);
    swkbdConfigSetInitialText(&kbd, latin1ToUtf8(initial).c_str());
    if (maxLen > 0) swkbdConfigSetStringLenMax(&kbd, static_cast<u32>(maxLen));
    // UTF-8 takes up to 4 bytes a character; the applet writes a NUL
    std::vector<char> buf(static_cast<std::size_t>(maxLen > 0 ? maxLen : 32) * 4 + 1, 0);
    const Result rc = swkbdShow(&kbd, buf.data(), buf.size());   // cancel -> LibAppletBadExit
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return false;
    out = utf8ToLatin1(buf.data());
    if (maxLen > 0 && out.size() > static_cast<std::size_t>(maxLen)) out.resize(static_cast<std::size_t>(maxLen));
    return true;
}

}  // namespace omk::nx

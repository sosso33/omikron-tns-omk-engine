// SPDX-License-Identifier: GPL-3.0-or-later
// THE VITA'S ON-SCREEN KEYBOARD, for the start menu's NAME FIELD
// (`todo/vita-port.md` F4).
//
// A new game will not start without a name: `Confirmer` refuses an empty
// field (docs/UI.md 3f), and a Vita has no keyboard. The field itself is the
// engine's and stays so - it takes characters through its own switch
// (`UiWalk::typeName`) - so the system keyboard is only a way to PRODUCE those
// characters: `play.cpp` opens it when the field takes the focus and feeds its
// result back in as typing.
//
// SDL's own text input would raise the same dialog, but it does so for as
// long as text input is on, and knows nothing of the field's focus; the
// dialog also has to be drawn by vitaGL's `vglSwapBuffers(GL_TRUE)`, which
// SDL's swap does not do. So this is the Vita's common dialog, driven
// directly and MODALLY: the game waits while the keyboard is up, as the
// original's menu does nothing either while a name is typed.
#pragma once

#include <string>

namespace omk::vita {

// Open the system keyboard with `initial` in it, at most `maxLen`
// characters, and wait for it. -> true with `out` set when the player
// confirmed, false when they cancelled. Only the Latin-1 range comes back;
// anything outside it is dropped (the field's font draws nothing else).
bool imeEdit(const char* title, const std::string& initial, int maxLen, std::string& out);

}  // namespace omk::vita

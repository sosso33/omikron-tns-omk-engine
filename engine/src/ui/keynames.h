// SPDX-License-Identifier: GPL-3.0-or-later
// THE NAMES A KEYBINDING ROW SHOWS - `sub_490BB0` (0x00490BB0) over the 142
// records at 0x004D10C8, lifted to `tables/key_names.json`.
//
// A record is a code on one device: type 1 a keyboard scan code, 2 a joystick
// code, 3 a mouse button. A KEYBOARD name of one character or less is not the
// answer: the hook asks Windows (`MapVirtualKeyA`, then the layout table at
// 0x004DD9B8), because a scan code is a position and the letter on it depends
// on the keyboard - the table's `Q` is an A on the French one. A port asks its
// host the same question, through `KeyCap`.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>

namespace omk {

class Json;

class KeyNames {
public:
    enum Type { Keyboard = 1, Joystick = 2, Mouse = 3 };
    // A scan code -> the character on that key on this host's keyboard, or ""
    // when it cannot say (the table's own name is used).
    using KeyCap = std::function<std::string(int)>;

    static KeyNames fromJson(const Json& doc);
    static KeyNames loadJson(const std::string& path);
    bool valid() const { return !names_.empty(); }
    std::size_t size() const { return names_.size(); }

    void setKeyCap(KeyCap c) { cap_ = std::move(c); }
    // `sub_490BB0`. A code with no record leaves the buffer as it was, which
    // the draw hook initialised empty.
    std::string name(int type, int code) const;

private:
    std::map<std::pair<int, int>, std::string> names_;
    KeyCap cap_;
};

}  // namespace omk

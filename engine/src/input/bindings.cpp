// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/bindings.h"

#include <algorithm>

#include "platform/json.h"

namespace omk {
namespace {
const std::string kEmpty;

int cell(int group, int slot, Device d) {
    return (group * kSlots + slot) * kDevices + static_cast<int>(d);
}
}  // namespace

const std::vector<int>& DeviceState::of(Device d) const {
    switch (d) {
        case Device::Keyboard: return keyboard;
        case Device::Mouse:    return mouse;
        default:               return joystick;
    }
}

bool DeviceState::holds(Device d, int code) const {
    const auto& v = of(d);
    return std::find(v.begin(), v.end(), code) != v.end();
}

// ------------------------------------------------------------ the schemes

ControlSchemes ControlSchemes::loadJson(const std::string& path) {
    ControlSchemes s;
    const Json j = Json::parseFile(path);
    const Json& rows = j["rows"]["rows"];
    if (rows.size() == 0) return s;

    int seen = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Json& r = rows[i];
        const int g = static_cast<int>(r["group"].i64(-1));
        const int a = static_cast<int>(r["action"].i64(-1));
        if (g < 0 || g >= kGroups || a < 0 || a >= kSlots) return ControlSchemes();
        // The bit is not stored: `Input_Poll` maps slot k to `1 << k`, and the
        // table agreeing is a CHECK, not a lookup. A file that disagreed would
        // mean the slot-to-bit identity is wrong, so refuse it here.
        if (r["bit"].i64(-1) != (1LL << a)) return ControlSchemes();
        s.code_[static_cast<std::size_t>(cell(g, a, Device::Keyboard))] =
            static_cast<int>(r["keyboard"].i64(0));
        s.code_[static_cast<std::size_t>(cell(g, a, Device::Mouse))] =
            static_cast<int>(r["mouse"].i64(0));
        s.code_[static_cast<std::size_t>(cell(g, a, Device::Joystick))] =
            static_cast<int>(r["joystick"].i64(0));
        s.label_[static_cast<std::size_t>(g * kSlots + a)] = r["label"].str();
        ++seen;
    }
    const Json& gn = j["rows"]["groups"];
    for (const auto& kv : gn.members()) {
        const int g = std::atoi(kv.first.c_str());
        if (g >= 0 && g < kGroups) s.group_[static_cast<std::size_t>(g)] = kv.second.str();
    }
    s.valid_ = (seen == kGroups * kSlots);
    return s;
}

int ControlSchemes::code(int group, int slot, Device d) const {
    if (group < 0 || group >= kGroups || slot < 0 || slot >= kSlots) return 0;
    return code_[static_cast<std::size_t>(cell(group, slot, d))];
}

const std::string& ControlSchemes::label(int group, int slot) const {
    if (group < 0 || group >= kGroups || slot < 0 || slot >= kSlots) return kEmpty;
    return label_[static_cast<std::size_t>(group * kSlots + slot)];
}

const std::string& ControlSchemes::groupName(int group) const {
    if (group < 0 || group >= kGroups) return kEmpty;
    return group_[static_cast<std::size_t>(group)];
}

// ------------------------------------------------------------ the live path

Input::Input(ControlSchemes s) : schemes_(std::move(s)) {
    for (int g = 0; g < kGroups; ++g)
        for (int a = 0; a < kSlots; ++a)
            for (int d = 0; d < kDevices; ++d)
                bound_[static_cast<std::size_t>(cell(g, a, static_cast<Device>(d)))] =
                    schemes_.code(g, a, static_cast<Device>(d));

    // The live tables before any install. This is `dword_4C65B8`'s own static
    // initialiser and its two neighbours', and it is what `docs/UI.md`'s
    // "default" column was reading: arrows, E R D F, LCTRL, SPACE, G H,
    // LSHIFT, TAB. `Game_Init` overwrites it, so it is never live - which is
    // exactly why it is reproduced here rather than skipped. A port that
    // starts from the installed scheme cannot show that the initialiser goes
    // away, because it was never there.
    static const int kKeyboardInit[kSlots] = {203, 205, 200, 208, 18, 19, 32,
                                              33,  29,  57,  34,  35, 42, 15};
    for (int a = 0; a < kSlots; ++a) {
        liveTable_[static_cast<std::size_t>(a * kDevices + 0)] = kKeyboardInit[a];
        liveTable_[static_cast<std::size_t>(a * kDevices + 1)] = 0;
        liveTable_[static_cast<std::size_t>(a * kDevices + 2)] = 0;
    }
}

void Input::installScheme(int group) {
    if (group < 0 || group >= kGroups) return;
    for (int a = 0; a < kSlots; ++a)
        for (int d = 0; d < kDevices; ++d)
            liveTable_[static_cast<std::size_t>(a * kDevices + d)] =
                bound_[static_cast<std::size_t>(cell(group, a, static_cast<Device>(d)))];
    group_ = group;
}

int Input::live(int slot, Device d) const {
    if (slot < 0 || slot >= kSlots) return 0;
    return liveTable_[static_cast<std::size_t>(slot * kDevices + static_cast<int>(d))];
}

std::uint32_t Input::poll(const DeviceState& st) const {
    std::uint32_t held = 0;
    for (int a = 0; a < kSlots; ++a) {
        for (int d = 0; d < kDevices; ++d) {
            const int c = live(a, static_cast<Device>(d));
            if (c != 0 && st.holds(static_cast<Device>(d), c)) {
                held |= 1u << a;
                break;
            }
        }
    }
    return held;
}

std::uint32_t Input::frame(const DeviceState& st) {
    const std::uint32_t held = poll(st);
    const std::uint32_t edges = held & (held ^ (repeatMask_ & last_));
    last_ = held;
    return edges;
}

bool Input::rebind(int group, int slot, Device d, int code) {
    if (group < 0 || group >= kGroups || slot < 0 || slot >= kSlots) return false;
    // Refused before the scan: 0 and 4 are the joystick axes, 1 is ESC.
    if (code == 0 || code == 1 || code == 4) return false;
    for (int a = 0; a < kSlots; ++a) {
        if (a == slot) continue;
        const std::size_t k = static_cast<std::size_t>(cell(group, a, d));
        if (bound_[k] == code) bound_[k] = 0;   // group-local, same device
    }
    bound_[static_cast<std::size_t>(cell(group, slot, d))] = code;
    if (group == group_) installScheme(group);
    return true;
}

}  // namespace omk

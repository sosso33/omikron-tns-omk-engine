// SPDX-License-Identifier: GPL-3.0-or-later
// THE INPUT PATH - the four control schemes, the live tables, and the one
// line of `Game_Frame` that turns held buttons into the word every other
// subsystem reads.
//
// There is exactly one input word in this engine. The `.CTL` channel matches
// transitions on it, the fight AI injects into it, and the interface reads it
// with three of its bits given a second job. It is fourteen bits wide because
// there are fourteen binding SLOTS, and `Input_Poll` maps slot k to bit
// `1 << k` - which the shipped tables confirm from the other side: all 56 rows
// of `tables/key_bindings.json` carry `bit == 1 << action`.
//
// ------------------------------------------------------------ the schemes
//
// `Input_InstallScheme` (0x0045BFF0) is eight lines and fixes the whole shape:
//
//     v3 = (int *)(56 * group + <base>);          // 56 bytes = 14 actions
//     do {
//         Input_SetUiKeyBinding (v2, *(v3 - 56)); // -224 bytes: device 0
//         Input_SetUiKeyBinding2(v2, *v3);        //             device 1
//         Input_SetUiKeyBinding3(v2++, v3[56]);   // +224 bytes: device 2
//         ++v3;
//     } while (v2 < 0xE);
//     dword_53A7F4 = group;
//
// So: three COMPILED tables 224 bytes apart (0x004C8F90 / 0x004C9070 /
// 0x004C9150), each 4 groups x 14 actions x 4 bytes; a group is 56 bytes; and
// installing one copies all 42 cells into three LIVE tables of 14. The group
// is remembered, because rebinding is group-local.
//
// The four groups are CONTEXTS, and the engine switches them where their
// names say - `Game_Init` (0x0041FA00) installs **0 Aventure**, the swim
// transition **1 Nager**, `Shoot_Enter` (0x004222D0) **2 Tirer**,
// `Fight_Begin` (0x004455B0) **3 Combat**, and `Shoot_Leave` 0 on the way
// back out.
//
// **The live table's static initialiser is not the default scheme, and
// `docs/UI.md` reported it as one.** `dword_4C65B8` ships as
// `{203, 205, 200, 208, 18, 19, 32, 33, 29, 57, 34, 35, 42, 15}` - arrows,
// then **E R D F**, `LCTRL`, `SPACE`, **G H**, `LSHIFT`, `TAB` - a dense
// fourteen with no holes, which is the tell: the real Aventure scheme leaves
// slots 6, 8, 9 and 12 unbound. `Game_Init` calls `Input_InstallScheme(0)`
// before the first frame, so those E and R cells are overwritten and no
// player ever saw them. Asserted in `verify.py: engine input`, which installs
// the scheme and checks they are gone.
//
// ------------------------------------------------------------ the frame
//
// `Game_Frame` polls once and hands one word to everything:
//
//     Input_Poll(&held, 0);
//     edges = held & (held ^ (repeatMask & lastFrame));
//     lastFrame = held;
//
// A bit **in** `repeatMask` fires only on the press; a bit outside it repeats
// every frame while held. `Ui_BeginScreen` sets the mask to **0x203F** - every
// button the interface uses - which is why holding a direction does not scroll
// a menu, and closing the last screen sets it back to 0, so the world gets
// held buttons rather than taps.
//
// ------------------------------------------------------------ the standard
//
// **Tier 3, differential.** No capture can reach this: a menu announces
// nothing to the tag logger, and the world's input never appears in an operand
// at all. What it CAN be tested against is `tools/sim`, which decides the same
// UI questions from an independent implementation - so the acceptance
// criterion (`docs/PORTING.md` B6) is that driving the ported UI by SCANCODE
// through these tables reaches the same answers the simulator reaches when
// handed the words directly. That is a real test of the tables and the edge
// filter together, and it is the strongest available.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace omk {

// The three device tables, in the order `Input_InstallScheme` copies them.
enum class Device { Keyboard = 0, Mouse = 1, Joystick = 2 };
inline constexpr int kDevices = 3;
inline constexpr int kSlots   = 14;   // the binding slots; bit k is `1 << k`
inline constexpr int kGroups  = 4;    // Aventure / Nager / Tirer / Combat

// The interface's repeat mask, set by `Ui_BeginScreen`.
inline constexpr std::uint32_t kUiRepeatMask = 0x203Fu;

// What one frame of the three devices holds down. A code is present iff that
// control is held; the codes live in `Input_ReadOneControl`'s one space -
// a keyboard scan 1..255, a mouse button 12..14, a joystick button index + 48
// (with 0 and 4 the two axes).
struct DeviceState {
    std::vector<int> keyboard, mouse, joystick;
    const std::vector<int>& of(Device d) const;
    bool holds(Device d, int code) const;
};

// The four compiled schemes, lifted to `tables/key_bindings.json`.
class ControlSchemes {
public:
    static ControlSchemes loadJson(const std::string& path);
    bool valid() const { return valid_; }

    int code(int group, int slot, Device d) const;
    const std::string& label(int group, int slot) const;
    const std::string& groupName(int group) const;

private:
    bool valid_ = false;
    std::array<int, kGroups * kSlots * kDevices> code_{};
    std::array<std::string, kGroups * kSlots> label_{};
    std::array<std::string, kGroups> group_{};
};

class Input {
public:
    explicit Input(ControlSchemes s);

    // `Input_InstallScheme`: copy all 42 cells of one group into the live
    // tables and remember the group.
    void installScheme(int group);
    int  group() const { return group_; }

    // The live tables, which are what `Input_Poll` actually reads. Exposed
    // because the whole point of the initialiser finding is that these are
    // observable and the compiled ones are not.
    int live(int slot, Device d) const;

    // `Ui_BeginScreen` sets 0x203F; closing the last screen sets 0.
    void setRepeatMask(std::uint32_t m) { repeatMask_ = m; }

    // The word before the edge filter: slot k is set if any of its three
    // device codes is held.
    std::uint32_t poll(const DeviceState& st) const;

    // One `Game_Frame`: poll, edge-filter, remember. Returns the word the
    // interface and the `.CTL` channel read.
    std::uint32_t frame(const DeviceState& st);

    // `Opt_RebindKey`: group-local, and it refuses codes 0, 1 and 4 - the two
    // joystick axes and ESC - BEFORE the scan. Clears the code from any other
    // slot of the same group and device, which is exactly why `Avancer` is UP
    // in Aventure while `Sauter` is UP in Combat and neither disturbs the
    // other. Returns false when the code is refused.
    bool rebind(int group, int slot, Device d, int code);

    const ControlSchemes& schemes() const { return schemes_; }

private:
    ControlSchemes schemes_;
    std::array<int, kSlots * kDevices> liveTable_{};
    // The scheme tables as they stand after any rebinding - `Opt_RebindKey`
    // writes the table, `Input_InstallScheme` reads it.
    std::array<int, kGroups * kSlots * kDevices> bound_{};
    int group_ = -1;
    std::uint32_t repeatMask_ = 0;
    std::uint32_t last_ = 0;
};

}  // namespace omk

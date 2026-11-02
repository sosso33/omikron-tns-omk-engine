// SPDX-License-Identifier: GPL-3.0-or-later
// THE KEYBINDING ROWS, driven through `OptionsMenu` with raw device states -
// the probe behind `verify.py: engine: rebinding`. A run of the viewer has no
// way to hold a gamepad button, so the pad's half of `Opt_RowInput` case 3 is
// checked here; the keyboard's is also checked through the viewer, end to end.
#include "input/bindings.h"
#include "input/pad.h"
#include "platform/json.h"
#include "ui/keynames.h"
#include "ui/options.h"
#include "ui/widgets.h"

#include <cstdio>
#include <string>

using namespace omk;

namespace {

// The rows' labels are Latin-1, as the interface's fonts are; this file is UTF-8
std::string rowValue(const OptionsMenu& m, const char* label) {
    const std::string want = utf8ToLatin1(label);
    for (const auto& w : m.widgets())
        if (w.label == want) return w.value;
    return "<no row>";
}

int rows(const OptionsMenu& m) { return static_cast<int>(m.widgets().size()); }

DeviceState joy(int code) { DeviceState s; s.joystick.push_back(code); return s; }
DeviceState key(int code) { DeviceState s; s.keyboard.push_back(code); return s; }
DeviceState mouseButton(int code) { DeviceState s; s.mouse.push_back(code); return s; }

constexpr double kOscMs = 500.0;

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: rebind_menu <tables>\n"); return 2; }
    const std::string tb = argv[1];
    const ControlSchemes schemes = ControlSchemes::loadJson(tb + "/key_bindings.json");
    const KeyNames names = KeyNames::loadJson(tb + "/key_names.json");
    const OptionTree tree = OptionTree::fromJson(Json::parseFile(tb + "/ui_widgets.json"),
                                                 Json::parseFile(tb + "/ui.json"));
    SettingsBlock block{};
    Input in(schemes);
    for (int g = 0; g < kGroups; ++g)
        for (int a = 0; a < kSlots; ++a) {
            const auto k = static_cast<std::size_t>(g * kSlots + a);
            block.keyboard[k] = static_cast<std::uint32_t>(in.bound(g, a, Device::Keyboard));
            block.mouse[k] = static_cast<std::uint32_t>(in.bound(g, a, Device::Mouse));
            block.joystick[k] = static_cast<std::uint32_t>(in.bound(g, a, Device::Joystick));
        }
    const auto J = [](const OptionsMenu& m, int g, int a) {
        return m.settings().joystick[static_cast<std::size_t>(g * kSlots + a)]; };
    const auto K = [](const OptionsMenu& m, int g, int a) {
        return m.settings().keyboard[static_cast<std::size_t>(g * kSlots + a)]; };
    const auto M = [](const OptionsMenu& m, int g, int a) {
        return m.settings().mouse[static_cast<std::size_t>(g * kSlots + a)]; };

    OptionsMenu m(tree);
    m.setBindingSources(&schemes, &names);
    m.open(1, block);
    m.focus();
    // the root -> Controles -> Configuration de la manette -> Aventure
    for (int i = 0; i < 3; ++i) m.press(kUiDown);
    m.press(kUiConfirm);
    std::printf("page5 %d\n", m.page());
    m.press(kUiDown);
    m.press(kUiConfirm);
    std::printf("page6 %d device %d\n", m.page(), m.controlDevice());
    m.press(kUiConfirm);

    // ---- the page: its builder starts oscillator 6, and four rows are gone
    std::printf("padpage %d rows %d movement \"%s\" action \"%s\"\n", m.page(), rows(m),
                rowValue(m, "Avancer").c_str(), rowValue(m, "Action / Utiliser").c_str());
    std::printf("entered suspended %d press %d\n", m.suspended() ? 1 : 0, m.press(kUiDown) ? 1 : 0);
    m.tick(kOscMs);
    std::printf("after period suspended %d\n", m.suspended() ? 1 : 0);

    // ---- the first selectable row is Pas de cote (slot 10, button 6). CONFIRM
    // latches and suspends: the confirm press itself cannot bind.
    const bool latched = m.press(kUiConfirm);
    m.pollRebind(joy(48));
    std::printf("latch %d listening %d group %d heldconfirm %u\n", latched ? 1 : 0,
                m.listening() ? 1 : 0, m.listenGroup(), J(m, 0, 10));
    m.tick(kOscMs);
    // the latch: no move, no BACK
    const int pageBefore = m.page();
    const bool moved = m.press(kUiDown) || m.press(kUiBack);
    std::printf("latched press %d page same %d\n", moved ? 1 : 0, m.page() == pageBefore ? 1 : 0);
    // an axis is refused, and keeps waiting
    { DeviceState s; s.joyX = 1000; m.pollRebind(s); }
    std::printf("axis listening %d cell %u\n", m.listening() ? 1 : 0, J(m, 0, 10));

    // ---- button 0, which Action holds: the two SWAP, nothing left unbound
    m.pollRebind(joy(48));
    std::printf("bound listening %d suspended %d slot10 %u slot4 %u dirty %d\n", m.listening() ? 1 : 0,
                m.suspended() ? 1 : 0, J(m, 0, 10), J(m, 0, 4), m.dirty() ? 1 : 0);
    std::printf("group-local combat slot10 %u slot4 %u\n", J(m, 3, 10), J(m, 3, 4));
    std::printf("names sidestep \"%s\" action \"%s\"\n", rowValue(m, "Pas de côté / Demi-tour").c_str(),
                rowValue(m, "Action / Utiliser").c_str());
    std::printf("keyboard untouched %u\n", K(m, 0, 10));
    m.tick(kOscMs);

    // ---- any KEY leaves the row as it was (`Opt_RebindKey(item, -1)`), and
    // START through the frontend's `cancelListen`
    m.press(kUiConfirm); m.tick(kOscMs);
    m.pollRebind(key(0x2F));
    std::printf("key cancels listening %d cell %u\n", m.listening() ? 1 : 0, J(m, 0, 10));
    m.tick(kOscMs);
    m.press(kUiConfirm); m.tick(kOscMs);
    m.cancelListen();
    std::printf("start cancels listening %d cell %u\n", m.listening() ? 1 : 0, J(m, 0, 10));
    m.tick(kOscMs);

    // ---- the pad's layout is the ROW'S GROUP's: SQUARE is button 3 under
    // Aventure and button 2 under Tirer, so a frontend must read it per row
    {
        pad::Pad p; p.buttons = pad::West;
        DeviceState a, s;
        pad::toDevices(p, a, 0);
        pad::toDevices(p, s, pad::kShootGroup);
        std::printf("square aventure %d tirer %d\n", a.joystick.at(0), s.joystick.at(0));
    }

    // ---- the defaults row: the JOYSTICK tables, every group, and nothing else;
    // its page re-enters, so the scheme reinstalls (page 7's leave hook)
    (void)m.takeInstall();
    m.press(kUiDown);   // down the rows to `Revenir a la configuration par defaut`
    for (int i = 0; i < 5; ++i) m.press(kUiDown);
    m.press(kUiConfirm);
    std::printf("defaults slot10 %u slot4 %u install %d\n", J(m, 0, 10), J(m, 0, 4), m.takeInstall() ? 1 : 0);
    m.tick(kOscMs);

    // ---- the keyboard's pages: the movement rows are back, value "key; mouse"
    OptionsMenu k(tree);
    k.setBindingSources(&schemes, &names);
    k.open(1, block);
    k.focus();
    for (int i = 0; i < 3; ++i) k.press(kUiDown);
    k.press(kUiConfirm);
    k.press(kUiConfirm);    // Configuration du clavier et de la souris
    k.press(kUiConfirm);    // Controles Aventure
    k.tick(kOscMs);
    std::printf("keypage rows %d device %d avancer \"%s\" vue \"%s\"\n", rows(k), k.controlDevice(),
                rowValue(k, "Avancer").c_str(), rowValue(k, "Vue première personne").c_str());
    k.press(kUiConfirm); k.tick(kOscMs);
    // ESC is refused, and on this page nothing but a key or a button ends it
    k.pollRebind(key(1));
    std::printf("esc listening %d slot2 %u\n", k.listening() ? 1 : 0, K(k, 0, 2));
    // a joystick button is not this page's device
    k.pollRebind(joy(48));
    std::printf("button listening %d slot2 %u\n", k.listening() ? 1 : 0, K(k, 0, 2));
    // the mouse's middle button lands in the MOUSE table
    k.pollRebind(mouseButton(14));
    std::printf("mouse listening %d kbd %u mouse %u\n", k.listening() ? 1 : 0, K(k, 0, 2), M(k, 0, 2));
    k.tick(kOscMs);
    // ...and the lowest scan code answers when two are down
    k.press(kUiConfirm); k.tick(kOscMs);
    { DeviceState s; s.keyboard = {0x2F, 0x2D}; k.pollRebind(s); }
    std::printf("two keys slot2 %u\n", K(k, 0, 2));
    k.tick(kOscMs);
    // the leave hook of Aventure's page, and of no other
    (void)k.takeInstall();
    k.press(kUiBack);
    const bool leave7 = k.takeInstall();
    k.press(kUiConfirm);
    k.tick(kOscMs);
    k.press(kUiDown);
    k.press(kUiBack);
    (void)k.takeInstall();
    std::printf("install on leaving 7: %d\n", leave7 ? 1 : 0);
    // what the tables were handed to `Input` as: live only at the next install
    {
        Input live(schemes);
        live.installScheme(0);
        live.setTables(k.settings().keyboard, k.settings().mouse, k.settings().joystick);
        const int before = live.live(2, Device::Keyboard);
        live.installScheme(0);
        std::printf("live before install %d after %d\n", before, live.live(2, Device::Keyboard));
    }
    return 0;
}

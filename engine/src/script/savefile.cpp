// SPDX-License-Identifier: GPL-3.0-or-later
#include "script/savefile.h"

#include "platform/datafs.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace omk {
namespace {

std::int32_t i32(std::span<const std::byte> d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    std::uint32_t v = 0;
    for (int k = 3; k >= 0; --k)
        v = (v << 8) | static_cast<std::uint32_t>(d[o + static_cast<std::size_t>(k)]);
    return static_cast<std::int32_t>(v);
}

}  // namespace

std::optional<SettingsBlock> readSettingsBlock(std::span<const std::byte> d) {
    if (d.size() < kSaveHeader) return std::nullopt;
    const auto* b = reinterpret_cast<const unsigned char*>(d.data());
    // the magic `SaveDir_Load` gates on, before anything is believed
    static const char kMagic[] = "OMK_SAVE";
    for (int i = 0; i < 8; ++i)
        if (b[i] != static_cast<unsigned char>(kMagic[i])) return std::nullopt;

    auto u32 = [&](std::size_t o) {
        return static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o + 1]) << 8) |
               (static_cast<std::uint32_t>(b[o + 2]) << 16) | (static_cast<std::uint32_t>(b[o + 3]) << 24);
    };
    auto i16 = [&](std::size_t o) {
        return static_cast<int>(static_cast<std::int16_t>(
            static_cast<std::uint16_t>(b[o]) | (static_cast<std::uint16_t>(b[o + 1]) << 8)));
    };

    SettingsBlock s;
    s.version           = u32(8);
    s.screenX           = i16(12);
    s.screenY           = i16(14);
    s.sky               = b[16] != 0;
    s.shadows           = b[17] != 0;
    s.clipDistance      = static_cast<int>(u32(20));
    s.volumeDialogue    = static_cast<int>(u32(24));
    s.volumeMusic       = static_cast<int>(u32(28));
    s.volumeEffects     = static_cast<int>(u32(32));
    s.sound3d           = b[36] != 0;
    s.subtitles         = b[37] != 0;
    s.fightDifficulty   = i16(38);
    s.shootDifficulty   = i16(40);
    s.combatCamera      = b[42];
    s.mouseSensitivityX = i16(44);
    s.mouseSensitivityY = i16(46);
    s.mouseInverted     = b[48] != 0;
    s.forceFeedback     = b[49] != 0;
    for (std::size_t k = 0; k < 56; ++k) {
        s.keyboard[k] = u32(kBindKeyboard + k * 4);
        s.mouse[k]    = u32(kBindMouse    + k * 4);
        s.joystick[k] = u32(kBindJoystick + k * 4);
    }
    // `dword_90E724`: the low half is the display driver's and `SaveDir_Load`
    // clears it for a 0x10000 file, so only these two bytes persist.
    s.streetActivity = b[1446];
    s.levelOfDetail  = b[1447];
    return s;
}

SettingsBlock defaultSettingsBlock() { return SettingsBlock{}; }

std::optional<SaveSlot> readSaveSlot(std::span<const std::byte> d, int slot) {
    if (slot < 0) return std::nullopt;
    const std::size_t base = kSaveHeader + kSaveSlotSize * static_cast<std::size_t>(slot);
    if (base + kSaveSlotDb + kGameDbSize > d.size()) return std::nullopt;
    SaveSlot s;
    for (std::size_t k = 0; k < 32; ++k) {
        const auto c = static_cast<unsigned char>(d[base + k]);
        if (!c) break;
        s.name.push_back(static_cast<char>(c));
    }
    s.day  = i32(d, base + 32);
    s.time = i32(d, base + 36);
    s.state = GameState::fromBytes(d.subspan(base + kSaveSlotDb, kGameDbSize));
    return s;
}

// ------------------------------------------------------------------ writing
namespace {

void p32(std::vector<std::byte>& d, std::size_t o, std::uint32_t v) {
    if (o + 4 > d.size()) return;
    for (int k = 0; k < 4; ++k)
        d[o + static_cast<std::size_t>(k)] = static_cast<std::byte>((v >> (8 * k)) & 0xFF);
}
void p16(std::vector<std::byte>& d, std::size_t o, std::uint16_t v) {
    if (o + 2 > d.size()) return;
    d[o]     = static_cast<std::byte>(v & 0xFF);
    d[o + 1] = static_cast<std::byte>(v >> 8);
}
std::size_t slotBase(int slot) {
    return kSaveHeader + kSaveSlotSize * static_cast<std::size_t>(slot);
}
bool slotOk(const std::vector<std::byte>& f, int slot) {
    return slot >= 0 && slot < static_cast<int>(kSaveSlots) &&
           slotBase(slot) + kSaveSlotSize <= f.size();
}

}  // namespace

std::vector<std::byte> settingsBytes(const SettingsBlock& s) {
    std::vector<std::byte> d(kSaveHeader, std::byte{0});
    static const char kMagic[] = "OMK_SAVE";
    for (int i = 0; i < 8; ++i) d[static_cast<std::size_t>(i)] = static_cast<std::byte>(kMagic[i]);
    p32(d, 8, s.version);
    p16(d, 12, static_cast<std::uint16_t>(s.screenX));
    p16(d, 14, static_cast<std::uint16_t>(s.screenY));
    d[16] = static_cast<std::byte>(s.sky ? 1 : 0);
    d[17] = static_cast<std::byte>(s.shadows ? 1 : 0);
    p32(d, 20, static_cast<std::uint32_t>(s.clipDistance));
    p32(d, 24, static_cast<std::uint32_t>(s.volumeDialogue));
    p32(d, 28, static_cast<std::uint32_t>(s.volumeMusic));
    p32(d, 32, static_cast<std::uint32_t>(s.volumeEffects));
    d[36] = static_cast<std::byte>(s.sound3d ? 1 : 0);
    d[37] = static_cast<std::byte>(s.subtitles ? 1 : 0);
    p16(d, 38, static_cast<std::uint16_t>(s.fightDifficulty));
    p16(d, 40, static_cast<std::uint16_t>(s.shootDifficulty));
    d[42] = static_cast<std::byte>(s.combatCamera);
    p16(d, 44, static_cast<std::uint16_t>(s.mouseSensitivityX));
    p16(d, 46, static_cast<std::uint16_t>(s.mouseSensitivityY));
    d[48] = static_cast<std::byte>(s.mouseInverted ? 1 : 0);
    d[49] = static_cast<std::byte>(s.forceFeedback ? 1 : 0);
    for (std::size_t k = 0; k < 56; ++k) {
        p32(d, kBindKeyboard + k * 4, s.keyboard[k]);
        p32(d, kBindMouse    + k * 4, s.mouse[k]);
        p32(d, kBindJoystick + k * 4, s.joystick[k]);
    }
    d[1446] = static_cast<std::byte>(s.streetActivity);
    d[1447] = static_cast<std::byte>(s.levelOfDetail);
    return d;
}

std::vector<std::byte> blankSaveFile(const SettingsBlock& s) {
    std::vector<std::byte> f(kSaveFileSize, std::byte{0});
    const auto head = settingsBytes(s);
    std::copy(head.begin(), head.end(), f.begin());
    return f;
}

bool putSettings(std::vector<std::byte>& file, const SettingsBlock& s) {
    if (file.size() < kSaveHeader) return false;
    const auto head = settingsBytes(s);
    std::copy(head.begin(), head.end(), file.begin());
    return true;
}

bool writeSaveSlot(std::vector<std::byte>& file, int slot, const SaveSlot& s,
                   std::span<const std::byte> thumb) {
    if (!slotOk(file, slot)) return false;
    const std::size_t b = slotBase(slot);
    for (std::size_t k = 0; k < 32; ++k)
        file[b + k] = k < s.name.size() ? static_cast<std::byte>(s.name[k]) : std::byte{0};
    p32(file, b + 32, static_cast<std::uint32_t>(s.day));
    p32(file, b + 36, static_cast<std::uint32_t>(s.time));
    // `State_Save` un-relocates the six array pointers before the copy.  Over
    // a base of 0 they are already file-relative here, so what `unrelocate`
    // does is the rest of that function's first half; the bytes are then
    // copied out as they stand.
    GameState st = s.state;
    st.unrelocate();
    const auto db = st.raw();
    for (std::size_t k = 0; k < kGameDbSize; ++k)
        file[b + kSaveSlotDb + k] = k < db.size() ? db[k] : std::byte{0};
    for (std::size_t k = 0; k < kSaveShot; ++k)
        file[b + kSaveSlotDb + kGameDbSize + k] =
            k < thumb.size() ? thumb[k] : std::byte{0};
    return true;
}

bool clearSaveSlot(std::vector<std::byte>& file, int slot) {
    if (!slotOk(file, slot)) return false;
    file[slotBase(slot)] = std::byte{0};
    return true;
}

int deleteProfile(std::vector<std::byte>& file, const std::string& name) {
    int n = 0;
    for (int i = 0; i < static_cast<int>(kSaveSlots); ++i) {
        if (!slotOk(file, i)) break;
        const std::size_t b = slotBase(i);
        std::string have;
        for (std::size_t k = 0; k < 32; ++k) {
            const auto c = static_cast<unsigned char>(file[b + k]);
            if (!c) break;
            have.push_back(static_cast<char>(c));
        }
        if (!have.empty() && have == name) { file[b] = std::byte{0}; ++n; }
    }
    return n;
}

std::vector<std::byte> thumbFromRgb565(std::span<const std::uint16_t> px,
                                       int w, int h) {
    std::vector<std::byte> out(kSaveShot, std::byte{0});
    for (int y = 0; y < kThumbH; ++y) {
        for (int x = 0; x < kThumbW; ++x) {
            // Nearest-neighbour down to 128 x 96 - the engine gets its 128 x 96
            // from the DEVICE, a blit into a rect that size, so the sampling is
            // the driver's and has no reachable tier (`docs/PORTING.md` B).
            std::uint16_t v = 0;
            if (w > 0 && h > 0) {
                const int sx = x * w / kThumbW, sy = y * h / kThumbH;
                const auto i = static_cast<std::size_t>(sy) * static_cast<std::size_t>(w) +
                               static_cast<std::size_t>(sx);
                if (i < px.size()) v = px[i];
            }
            const int r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
            const auto o = static_cast<std::uint16_t>((r << 10) | ((g >> 1) << 5) | b);
            const std::size_t k = 2 * (static_cast<std::size_t>(y) * kThumbW +
                                       static_cast<std::size_t>(x));
            out[k]     = static_cast<std::byte>(o & 0xFF);
            out[k + 1] = static_cast<std::byte>(o >> 8);
        }
    }
    return out;
}

std::vector<std::byte> readSaveFile(const std::string& writablePath,
                                    const std::string& shippedPath) {
    auto d = DataFs::readPath(writablePath);
    if (!d.empty()) return d;
    return shippedPath.empty() ? d : DataFs::readPath(shippedPath);
}

bool writeSaveFile(const std::string& path, std::span<const std::byte> file) {
    if (!safeOutputPath(path)) return false;
    std::error_code ec;
    const auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir, ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::fprintf(stderr, "save: cannot write %s\n", path.c_str());
        return false;
    }
    f.write(reinterpret_cast<const char*>(file.data()),
            static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(f);
}

const std::array<const char*, 13>& monthNames() {
    static const std::array<const char*, 13> m = {
        "Aqed", "Nadim", "Andar", "Xenep", "Nevod", "Ganevat", "Osmydep",
        "Qomivo", "Taznevet", "Ustanevat", "Nivat", "Mozkanep", "Primevat"};
    return m;
}

std::string formatDate(int day) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d %s %d",
                  day % kDaysPerMonth + 1,
                  monthNames()[static_cast<std::size_t>(
                      day / kDaysPerMonth % kMonthsPerYear)],
                  day / kDaysPerMonth / kMonthsPerYear + kYearZero);
    return buf;
}

std::string formatTime(int t) {
    // Integer division throughout, exactly as the formatter at 0x0041E690
    // does it: an hour is 3600000/21, a minute that over 15, a second that
    // over 33 - none of which divide evenly, so a float here drifts.
    const int hour   = kDayUnits / kHoursPerDay;
    const int minute = hour / kMinutesPerHour;
    const int second = minute / kSecondsPerMinute;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d:%02d:%02d",
                  t / hour, t % hour / minute, t % hour % minute / second);
    return buf;
}

}  // namespace omk

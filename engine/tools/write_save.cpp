// SPDX-License-Identifier: GPL-3.0-or-later
// WRITING a save - the differential behind `verify.py: engine: save write`.
//
//     write_save <traces/save-appart.bin> <traces/games-resto.bin> <out.bin>
//
// `Game_WriteSave` (0x00408EF0) is a read-modify-write of one 8402344-byte
// file: the settings over its head, then the slot's name, day, time, DB and
// thumbnail at four offsets that tile the slot exactly.  Everything here is
// that function and the three around it (`SaveDir_ClearSlot`,
// `SaveDir_Delete`, `sub_4092A0`'s create arm), run against the two saves the
// engine itself wrote.
//
// The four things it is worth asking of a writer, and all four can fail:
//
//   * a file it creates is the size the geometry predicts and reads back as
//     an empty directory;
//   * a slot written from a real save's own parts reads back BYTE-IDENTICAL,
//     which is the round trip `read_save` cannot do because it only reads;
//   * the settings serialiser reproduces the fixture's 3496 bytes, which is a
//     real test of GAME_STATE 8a's claim that every non-zero field is named -
//     an unnamed one shows up here as a difference and nowhere else;
//   * `State_Save`'s POSITION quantisation, run against numbers the engine
//     wrote: convert a stored raw back to world and re-quantise it, and the
//     same integer has to come out.
#include "platform/datafs.h"
#include "script/gamestate.h"
#include "script/savefile.h"
#include "ui/widgets.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
            "usage: write_save <save-appart.bin> <games-resto.bin> <out.bin> "
            "[tables dir]\n");
        return 2;
    }
    const auto fixture = omk::DataFs::readPath(argv[1]);
    const auto resto   = omk::DataFs::readPath(argv[2]);

    // --- a file created from nothing --------------------------------------
    const auto settings = omk::readSettingsBlock(fixture);
    const auto blank = omk::blankSaveFile(settings ? *settings
                                                   : omk::defaultSettingsBlock());
    const int blankMagic = blank.size() >= 8 &&
                           std::memcmp(blank.data(), "OMK_SAVE", 8) == 0;
    // every slot of it empty - the name's first byte is what says so
    int blankUsed = 0;
    for (std::size_t k = 0; k < omk::kSaveSlots; ++k)
        if (blank[omk::kSaveHeader + omk::kSaveSlotSize * k] != std::byte{0}) ++blankUsed;

    // --- the settings serialiser against the fixture's own header ----------
    //
    // GAME_STATE 8a says every non-zero byte of the 3496 is a named field.
    // This is that claim as a number: parse the fixture and write it back.
    int headerDiff = 0, headerFirstDiff = -1;
    if (settings && fixture.size() >= omk::kSaveHeader) {
        const auto re = omk::settingsBytes(*settings);
        for (std::size_t k = 0; k < omk::kSaveHeader; ++k)
            if (re[k] != fixture[k]) {
                ++headerDiff;
                if (headerFirstDiff < 0) headerFirstDiff = static_cast<int>(k);
            }
    }

    // --- a slot round trip -------------------------------------------------
    //
    // The fixture is TRUNCATED - the header plus slot 0's first 8232 bytes,
    // no thumbnail - so the comparison is over the 8232 that exist.  Which is
    // the interesting part anyway: the picture is the device's.
    auto out = blank;
    int slotDiff = -1, slotFirstDiff = -1;
    std::string rtName;
    int rtDay = -1, rtTime = -1, rtArea = -1, rtScene = -1;
    if (const auto s = omk::readSaveSlot(fixture, 0)) {
        omk::writeSaveSlot(out, 0, *s);
        slotDiff = 0;
        for (std::size_t k = 0; k < omk::kSaveSlotDb + omk::kGameDbSize; ++k)
            if (out[omk::kSaveHeader + k] != fixture[omk::kSaveHeader + k]) {
                ++slotDiff;
                if (slotFirstDiff < 0) slotFirstDiff = static_cast<int>(k);
            }
        if (const auto back = omk::readSaveSlot(out, 0)) {
            rtName = back->name; rtDay = back->day; rtTime = back->time;
            rtArea = back->state.currentArea(); rtScene = back->state.currentScene();
        }
    }

    // --- the placement quantisation, against three real slots --------------
    //
    // The engine's two conversions are not inverses: the LOAD subtracts a
    // whole world unit the SAVE never added (GAME_STATE 5), so a round trip
    // through `placementWorld` would drift by design.  What is tested here is
    // the save side alone - the world value the engine held, which is the
    // stored raw through the same 0.15378937 its own rounding test uses.
    std::vector<std::int32_t> quant;   // per slot: how many of the 4 fields return
    std::vector<std::int32_t> stored;  // and slot 0's four raws, as evidence
    for (int k : {0, 1, 2}) {
        const auto s = omk::readSaveSlot(resto, k);
        if (!s) { quant.push_back(-1); continue; }
        const auto p = s->state.placement();
        float pos[3];
        for (int i = 0; i < 3; ++i)
            pos[i] = static_cast<float>(static_cast<double>(p.raw[i]) * 0.15378937);
        const float deg = static_cast<float>(p.facing * 0.087890625);
        omk::GameState st = s->state;
        st.setPlacement(pos, deg);
        const auto q = st.placement();
        int same = 0;
        for (int i = 0; i < 3; ++i) same += (q.raw[i] == p.raw[i]);
        same += (q.facing == p.facing);
        quant.push_back(same);
        if (k == 0) { stored = {p.raw[0], p.raw[1], p.raw[2], p.facing}; }
    }
    // ...and the same over a SWEEP, because twelve real fields barely
    // exercise the rounding: the correction arm only fires when the
    // truncation fell on the wrong side of the midpoint, which in
    // games-resto's twelve happens once.  Every raw from -30000 to 30000
    // through the engine's own inverse and back has to return itself, and
    // that includes the negatives, where a truncation towards zero and a
    // `lround` part company.
    int sweep = 0, sweepNeg = 0, badNeg = 0, badPos = 0, badTowardsZero = 0;
    {
        omk::GameState st = omk::GameState::fromBytes(
            std::span<const std::byte>(fixture).subspan(
                omk::kSaveHeader + omk::kSaveSlotDb, omk::kGameDbSize));
        for (int raw = -30000; raw <= 30000; ++raw) {
            const float w = static_cast<float>(static_cast<double>(raw) * 0.15378937);
            const float p[3] = {w, w, w};
            st.setPlacement(p, 0.0f);
            ++sweep;
            if (raw < 0) ++sweepNeg;
            const std::int32_t got = st.placement().raw[0];
            if (got == raw) continue;
            if (raw < 0) ++badNeg; else ++badPos;
            if (got == raw + 1) ++badTowardsZero;
        }
    }

    // The asymmetry itself, in world units: what `State_Apply` gives back for
    // the raw `State_Save` wrote for a known position.  It is -1 per axis.
    float drift = 0.0f;
    {
        omk::GameState st = omk::GameState::fromBytes(
            std::span<const std::byte>(fixture).subspan(
                omk::kSaveHeader + omk::kSaveSlotDb, omk::kGameDbSize));
        // A position that lands EXACTLY on a stored integer, so what comes
        // back carries no quantisation error and the number below is the
        // asymmetry alone.  Measured from a round world value the drift is
        // the same -1 with up to half a raw unit (0.077) of rounding on top,
        // which is a worse way to state a fact that is exact.
        const float p0[3] = {static_cast<float>(6503.0 * 0.15378937),
                             static_cast<float>(1000.0 * 0.15378937),
                             static_cast<float>(-2000.0 * 0.15378937)};
        st.setPlacement(p0, 90.0f);
        float back[3]; float deg = 0;
        st.placementWorld(back, deg);
        drift = back[0] - p0[0];
    }

    // --- clearing and deleting --------------------------------------------
    auto cleared = out;
    omk::clearSaveSlot(cleared, 0);
    int clearDiff = 0;
    for (std::size_t k = 0; k < out.size(); ++k) if (out[k] != cleared[k]) ++clearDiff;
    auto many = out;
    omk::writeSaveSlot(many, 5, *omk::readSaveSlot(fixture, 0));
    omk::writeSaveSlot(many, 9, *omk::readSaveSlot(fixture, 0));
    const int deleted = omk::deleteProfile(many, rtName);

    // --- the directory the interface reads, over a file we wrote -----------
    //
    // End to end: the writer's file goes through `SaveDir_Build`'s own walk
    // and the load panel branches on what it finds.
    int dirNames = 0, dirProfiles = -1;
    std::string dirFirst;
    if (argc > 4) {
        const auto w = omk::UiWidgets::loadJson(std::string(argv[4]) + "/ui_widgets.json");
        const std::string tmp = std::string(argv[3]) + ".games";
        if (omk::writeSaveFile(tmp, many)) {
            // `many` still holds the two extra slots, minus the deleted ones
        }
        auto three = out;
        omk::writeSaveSlot(three, 5, *omk::readSaveSlot(fixture, 0));
        if (omk::writeSaveFile(tmp, three)) {
            const auto dir = omk::saveDirectory(tmp, w);
            for (const auto& e : dir) if (!e.name.empty()) { ++dirNames; if (dirFirst.empty()) dirFirst = e.name; }
            dirProfiles = omk::saveProfiles(dir);
        }
        std::remove(tmp.c_str());
    }

    // --- the thumbnail packer ---------------------------------------------
    // At the thumbnail's own size, so the four words below are the PACKING
    // and not the downscale: a 2x2 source samples its top-left pixel four
    // times over, which is a test every colour passes.
    std::vector<std::uint16_t> px(
        static_cast<std::size_t>(omk::kThumbW) * omk::kThumbH, 0);
    px[0] = 0xF800; px[1] = 0x07E0; px[2] = 0x001F; px[3] = 0xFFFF;
    const auto th = omk::thumbFromRgb565(px, omk::kThumbW, omk::kThumbH);
    const auto word = [&th](int i) {
        return static_cast<int>(static_cast<unsigned char>(th[2 * static_cast<std::size_t>(i)])) |
               (static_cast<int>(static_cast<unsigned char>(th[2 * static_cast<std::size_t>(i) + 1])) << 8);
    };

    std::vector<std::byte> o;
    const auto put32 = [&o](std::int32_t v) {
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::byte>((v >> (8 * k)) & 0xFF));
    };
    const auto putStr = [&o, &put32](const std::string& s) {
        put32(static_cast<std::int32_t>(s.size()));
        for (char c : s) o.push_back(static_cast<std::byte>(c));
    };
    put32(static_cast<std::int32_t>(blank.size()));
    put32(blankMagic); put32(blankUsed);
    put32(headerDiff); put32(headerFirstDiff);
    put32(slotDiff); put32(slotFirstDiff);
    putStr(rtName); put32(rtDay); put32(rtTime); put32(rtArea); put32(rtScene);
    for (std::size_t k = 0; k < 3; ++k) put32(k < quant.size() ? quant[k] : -1);
    for (std::size_t k = 0; k < 4; ++k) put32(k < stored.size() ? stored[k] : 0);
    put32(static_cast<std::int32_t>(drift * 100.0f + (drift < 0 ? -0.5f : 0.5f)));
    put32(sweep); put32(sweepNeg); put32(badNeg); put32(badPos);
    put32(badTowardsZero);
    put32(clearDiff); put32(deleted);
    put32(dirNames); put32(dirProfiles); putStr(dirFirst);
    put32(static_cast<std::int32_t>(th.size()));
    for (int i = 0; i < 4; ++i) put32(word(i));

    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));

    std::printf("write: a created file is %zu bytes (%s), %d of 256 slots in "
                "use\n", blank.size(), blankMagic ? "OMK_SAVE" : "NO MAGIC",
                blankUsed);
    std::printf("settings: the serialiser differs from the fixture's own "
                "header in %d of %zu bytes (first +%d)\n",
                headerDiff, omk::kSaveHeader, headerFirstDiff);
    std::printf("slot: written from the fixture's own parts, %d of %zu bytes "
                "differ (first +%d); it reads back as \"%s\", day %d, time "
                "%d, area %d scene %d\n",
                slotDiff, omk::kSaveSlotDb + omk::kGameDbSize, slotFirstDiff,
                rtName.c_str(), rtDay, rtTime, rtArea, rtScene);
    std::printf("placement: of 4 fields per slot, %d/%d/%d requantise to the "
                "same integers; slot 0 stored (%d, %d, %d) facing %d; a save "
                "and a reload move the player by %.2f world units per axis\n",
                quant.size() > 0 ? quant[0] : -1, quant.size() > 1 ? quant[1] : -1,
                quant.size() > 2 ? quant[2] : -1,
                stored.size() == 4 ? stored[0] : 0, stored.size() == 4 ? stored[1] : 0,
                stored.size() == 4 ? stored[2] : 0, stored.size() == 4 ? stored[3] : 0,
                static_cast<double>(drift));
    std::printf("placement sweep: %d raws (%d of them negative) through the "
                "engine's own inverse and back: %d negatives and %d "
                "positives fail to return, %d of them landing one unit "
                "TOWARDS ZERO\n",
                sweep, sweepNeg, badNeg, badPos, badTowardsZero);
    std::printf("clear: %d byte(s) change; deleteProfile emptied %d slot(s)\n",
                clearDiff, deleted);
    if (dirProfiles >= 0)
        std::printf("directory: a file this tool wrote holds %d named slot(s) "
                    "and %d distinct profile(s), the first \"%s\"\n",
                    dirNames, dirProfiles, dirFirst.c_str());
    std::printf("thumbnail: %zu bytes; RGB565 F800/07E0/001F/FFFF pack to "
                "%04X/%04X/%04X/%04X\n", th.size(),
                word(0), word(1), word(2), word(3));
    return 0;
}

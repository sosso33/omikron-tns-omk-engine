// SPDX-License-Identifier: GPL-3.0-or-later
// The save file and the clock - the differential against tools/gamestate.py
// and verify.py's `save file` / `game clock`.
//
//     read_save <traces/save-appart.bin> <gamedata/IAM/OBJECT> <out.bin>
//
// The save is a truncated fixture: the 3496-byte header plus slot 0's first
// 8232 bytes, which is everything except the screenshot. That is the only real
// save that exists, so the reader has to accept it.
#include "platform/datafs.h"
#include "script/gamestate.h"
#include "script/objects.h"
#include "script/savefile.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

// A dated newspaper's name reads "<day> <Month> <year>". Scanning for the
// month NAME and taking the number before it is what makes this a test the
// calendar could fail: an unknown month simply does not match.
bool parseDate(const std::string& s, int& day, int& month, int& year) {
    const auto& months = omk::monthNames();
    for (std::size_t m = 0; m < months.size(); ++m) {
        const auto at = s.find(months[m]);
        if (at == std::string::npos || at == 0) continue;
        // the day: digits ending just before the space
        std::size_t e = at;
        while (e > 0 && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
        std::size_t b = e;
        while (b > 0 && std::isdigit(static_cast<unsigned char>(s[b - 1]))) --b;
        if (b == e) continue;
        std::size_t y = at + std::strlen(months[m]);
        while (y < s.size() && std::isspace(static_cast<unsigned char>(s[y]))) ++y;
        std::size_t ye = y;
        while (ye < s.size() && std::isdigit(static_cast<unsigned char>(s[ye]))) ++ye;
        if (ye == y) continue;
        day   = std::stoi(s.substr(b, e - b));
        month = static_cast<int>(m);
        year  = std::stoi(s.substr(y, ye - y));
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
            "usage: read_save <save.bin> <IAM/OBJECT> <out.bin> [IAM/START]\n");
        return 2;
    }
    const auto d = omk::DataFs::readPath(argv[1]);

    int hdrNonZero = 0;
    bool magic = false;
    int w = 0, h = 0;
    if (d.size() >= omk::kSaveHeader) {
        magic = std::memcmp(d.data(), "OMK_SAVE", 8) == 0;
        w = static_cast<int>(static_cast<std::uint8_t>(d[12])) |
            (static_cast<int>(static_cast<std::uint8_t>(d[13])) << 8);
        h = static_cast<int>(static_cast<std::uint8_t>(d[14])) |
            (static_cast<int>(static_cast<std::uint8_t>(d[15])) << 8);
        for (std::size_t k = 0; k < omk::kSaveHeader; ++k)
            if (d[k] != std::byte{0}) ++hdrNonZero;
    }
    const auto slot = omk::readSaveSlot(d, 0);

    // --- the clock, tested against the shipped newspapers ------------------
    const auto objs = omk::loadObjects(omk::DataFs::readPath(argv[2]));
    int dated = 0, legal = 0, highestDay = 0;
    for (const auto& o : objs) {
        int day = 0, month = 0, year = 0;
        if (!parseDate(o.name, day, month, year)) continue;
        ++dated;
        if (day >= 1 && day <= omk::kDaysPerMonth) ++legal;
        if (day > highestDay) highestDay = day;
    }
    const auto date = omk::formatDate(omk::kNewGameDay);
    const auto time = omk::formatTime(omk::kNewGameTime);

    // --- IAM\START, the new-game save, walked -----------------------------
    //
    // The layout is `State_Apply`'s own arithmetic; what is worth asserting is
    // the part the code does NOT state - the six u16 counts at +32, which
    // nothing in the runtime reads. The honest test is that the arrays they
    // size TILE the image exactly, and that no bit is set past a count.
    long walkEnd = 0, covered = 0, spare = 0, roundTrip = 0, agree = 0;
    int firstDiff = -1, lastDiff = -1;
    std::size_t imageSize = 0;
    if (argc > 4) {
        auto st = omk::GameState::fromBytes(omk::DataFs::readPath(argv[4]));
        imageSize = st.imageSize();
        for (const auto& sg : st.walk()) {
            walkEnd = std::max(walkEnd, static_cast<long>(sg.end));
            covered += static_cast<long>(sg.end - sg.begin);
        }
        // The three one-bit maps: array 4 declares 791 addresses in 99 bytes
        // (792 bits) and array 5 declares 4558 zones in 570 (4560). If either
        // count were wrong the leftover bits would have to carry state.
        for (int k : {3, 4, 5}) {
            const auto a = static_cast<omk::StateArray>(k);
            for (std::size_t i = st.count(a); i < 8 * st.arrayBytes(a); ++i)
                spare += st.bit(a, static_cast<int>(i));
        }
        // The six counts, against the numbers established elsewhere: 694
        // VARIABLES.TAG entries, 259 AREAS.TAG, 670 distinct prop-state
        // indices, 1032 object ids, 791 ADDRESSES.TAG and the 4558 zone
        // records. Nothing in the runtime reads these counts, so agreeing
        // with six independent sources is what identifies them.
        static const int kWant[6] = {694, 259, 670, 1032, 791, 4558};
        for (int k = 0; k < 6; ++k)
            if (st.count(static_cast<omk::StateArray>(k)) == kWant[k]) ++agree;

        auto rt = omk::GameState::fromBytes(omk::DataFs::readPath(argv[4]));
        rt.relocate();
        rt.unrelocate();
        const auto x = st.raw(), y = rt.raw();
        for (std::size_t i = 0; i < imageSize && i < x.size(); ++i)
            if (x[i] != y[i]) {
                ++roundTrip;
                if (firstDiff < 0) firstDiff = static_cast<int>(i);
                lastDiff = static_cast<int>(i);
            }
    }

    std::vector<std::uint8_t> o;
    const auto put32 = [&o](std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        for (int k = 0; k < 4; ++k) o.push_back(static_cast<std::uint8_t>(u >> (8 * k)));
    };
    const auto putStr = [&o, &put32](const std::string& s) {
        put32(static_cast<std::int32_t>(s.size()));
        for (char c : s) o.push_back(static_cast<std::uint8_t>(c));
    };
    put32(static_cast<std::int32_t>(omk::kSaveFileSize));
    put32(static_cast<std::int32_t>(omk::kSaveHeader));
    put32(static_cast<std::int32_t>(omk::kSaveSlotDb + omk::kGameDbSize));
    put32(magic ? 1 : 0); put32(w); put32(h); put32(hdrNonZero);
    putStr(slot ? slot->name : std::string());
    put32(slot ? slot->day : -1);
    // The DB read as game STATE - the part no literal in the writers could
    // have given, and the reason this fixture settles more than the geometry.
    const int area  = slot ? slot->state.currentArea() : -1;
    const int scene = slot ? slot->state.sceneOfArea(area) : -1;
    put32(area); put32(scene);
    for (int v : {19, 626, 61}) put32(slot ? slot->state.var(v) : -1);
    put32(dated); put32(legal); put32(highestDay);
    putStr(date); putStr(time);
    put32(static_cast<std::int32_t>(imageSize));
    put32(static_cast<std::int32_t>(walkEnd));
    put32(static_cast<std::int32_t>(walkEnd - covered));
    put32(static_cast<std::int32_t>(agree));
    put32(static_cast<std::int32_t>(spare));
    put32(static_cast<std::int32_t>(roundTrip));
    put32(firstDiff); put32(lastDiff);
    if (!omk::safeOutputPath(argv[3])) return 2;
    std::ofstream f(argv[3], std::ios::binary);
    f.write(reinterpret_cast<const char*>(o.data()),
            static_cast<std::streamsize>(o.size()));

    std::printf("save: %zu bytes predicted, header %zu (%s, %dx%d, %d "
                "non-zero bytes), slot head %zu; slot 0 = \"%s\", day %d\n",
                omk::kSaveFileSize, omk::kSaveHeader,
                magic ? "OMK_SAVE" : "NOT OMK_SAVE", w, h, hdrNonZero,
                omk::kSaveSlotDb + omk::kGameDbSize,
                slot ? slot->name.c_str() : "<none>", slot ? slot->day : -1);
    std::printf("state: area %d with scene %d over it; Interface=%d, "
                "premiere impasse=%d, Impasse Finie=%d\n",
                area, scene, slot ? slot->state.var(19) : -1,
                slot ? slot->state.var(626) : -1,
                slot ? slot->state.var(61) : -1);
    std::printf("clock: %d dated newspapers, %d legal in the calendar, "
                "highest day %d; a new game starts %s at %s\n",
                dated, legal, highestDay, date.c_str(), time.c_str());
    if (argc > 4)
        std::printf("IAM\\START: %zu bytes, the walk ends at %ld with %ld "
                    "padding bytes; %ld of six counts agreeing with an "
                    "independent source; %ld bits set past a count; the "
                    "State_Apply/State_Save round trip differs in %ld bytes "
                    "(+%d..+%d)\n",
                    imageSize, walkEnd, walkEnd - covered, agree, spare, roundTrip,
                    firstDiff, lastDiff);
    return 0;
}

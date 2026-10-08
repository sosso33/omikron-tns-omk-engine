// SPDX-License-Identifier: GPL-3.0-or-later
// A DAMAGED SAVE against the game-state accessors and the date formatter.
//
//     hostile_save <gamedata>
//
// The six array offsets at DB +8 come out of the save file as they are, and
// the original relocates them without a look (`Var_Set`, 0x0040E510, is
// `[db+8][i*4] = v`). Here IAM\START's image is given offsets and indices
// whose 32-bit sum WRAPS back into the image - the shape of the audit's
// finding of 2026-10-07 - and every write must change nothing, every read
// answer 0; then the same accessors on the untouched image must still do what
// they did. And a negative day, which only such a save carries, must format
// without reading outside the month table (`Clock_FormatDate`, 0x0041E690).
//
// Prints one `name value` line per result.
#include "platform/datafs.h"
#include "script/gamestate.h"
#include "script/savefile.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> withOffset(std::span<const std::byte> img, int array, std::uint32_t off) {
    std::vector<std::byte> d(img.begin(), img.end());
    const std::size_t at = 8 + 4 * static_cast<std::size_t>(array);
    for (int k = 0; k < 4; ++k) d[at + static_cast<std::size_t>(k)] = static_cast<std::byte>(off >> (8 * k));
    return d;
}

bool same(const omk::GameState& s, std::span<const std::byte> before) {
    const auto now = s.raw();
    return now.size() == before.size() && std::memcmp(now.data(), before.data(), now.size()) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: hostile_save <gamedata>\n");
        return 2;
    }
    const auto start = omk::GameState::fromFile(std::string(argv[1]) + "/IAM/START");
    if (start.imageSize() == 0) {
        std::fprintf(stderr, "hostile_save: %s/IAM/START did not read\n", argv[1]);
        return 2;
    }
    const auto img = start.raw();
    const auto say = [](const char* what, long long v) { std::printf("%s %lld\n", what, v); };

    // The array indices are StateArray's: 0 variables, 1 scene of area,
    // 2 prop state, 3 object shown. Each offset wraps onto byte 0 for the
    // index used, so the old 32-bit guard passed and wrote the header.
    {
        auto d = withOffset(img, 0, 0xFFFFFFFCu);
        auto s = omk::GameState::fromBytes(d);
        say("var.wrapped.read", s.var(1));
        s.setVar(1, static_cast<std::int32_t>(0xDEADBEEF));
        say("var.wrapped.write.unchanged", same(s, d));
    }
    {
        auto s = omk::GameState::fromBytes(img);
        s.setVar(0x40000000, static_cast<std::int32_t>(0xDEADBEEF));   // 4*i wraps to 0
        say("var.index.write.unchanged", same(s, img));
        say("var.index.read", s.var(0x40000000));
    }
    {
        auto d = withOffset(img, 1, 0xFFFFFFFEu);
        auto s = omk::GameState::fromBytes(d);
        say("scene.wrapped.read", s.sceneOfArea(1));
        s.setSceneOfArea(1, 0x1234);
        say("scene.wrapped.write.unchanged", same(s, d));
    }
    {
        auto d = withOffset(img, 2, 0xFFFFFFFFu);
        auto s = omk::GameState::fromBytes(d);
        say("prop.wrapped.read", s.propStateBits(4));
        s.setPropState(4, 0);   // IAM\START's byte 0 is 0x67: a wrapped write of 0 shows
        say("prop.wrapped.write.unchanged", same(s, d));
    }
    {
        auto d = withOffset(img, 3, 0xFFFFFFFFu);
        auto s = omk::GameState::fromBytes(d);
        say("bit.wrapped.read", s.bit(omk::StateArray::ObjectShown, 8));
        s.setBit(omk::StateArray::ObjectShown, 8, 0);   // likewise
        say("bit.wrapped.write.unchanged", same(s, d));
    }

    // ...and on the image as it ships, the same accessors still work.
    {
        auto s = omk::GameState::fromBytes(img);
        s.setVar(5, 77);
        say("var.inrange", s.var(5));
        s.setSceneOfArea(7, 42);
        say("scene.inrange", s.sceneOfArea(7));
        s.setPropState(9, 2);
        say("prop.inrange", s.propStateBits(9));
        s.setBit(omk::StateArray::ObjectShown, 13, 1);
        say("bit.inrange", s.bit(omk::StateArray::ObjectShown, 13));
    }

    // The date: the new game's day, and two that only a damaged save carries.
    std::printf("date.52 %s\n", omk::formatDate(52).c_str());
    std::printf("date.-1 %s\n", omk::formatDate(-1).c_str());
    std::printf("date.-41 %s\n", omk::formatDate(-41).c_str());
    return 0;
}

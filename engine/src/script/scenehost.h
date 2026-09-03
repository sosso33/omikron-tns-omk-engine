// SPDX-License-Identifier: GPL-3.0-or-later
// Which `.SCX` a resident chunk plays its objects from.
//
// A world script's `scx.play*` names an object by an id that is **local to the
// resident scene** - the ids are small and reused, so the same operand means
// different things in different files (CLAUDE.md 1, "a scene-local id means
// nothing outside its scene"). Resolving one therefore needs the scene, and
// the scene is not named where you would expect:
//
//   * an **AREA** chunk names its own set at `+97` - a 9-byte stem, so
//     `AREA 217 +97 = "Re14"` means `SCPTDATA/Re14.SCX`;
//   * a **SCENE** chunk names nothing. It is played over an area, and the
//     area is the one whose script loaded it - so the map is built by scanning
//     every world script for `scene.load(area, scene)` (opcode 71) and taking
//     the FIRST area that loads each scene.
//
// That second half is a corpus scan rather than a field, which is why it lives
// here rather than in the chunk reader.
#pragma once

#include "formats/iam.h"
#include "script/script.h"

#include <map>
#include <span>
#include <string>

namespace omk {

class DataFs;

// {scene id -> the area whose script loads it}, from the opcode 71 sites of
// every AREA and SCENE record script and second-table script.
std::map<int, int> sceneToArea(std::span<const std::byte> areaFile,
                               std::span<const std::byte> sceneFile,
                               const OpcodeTable& table);

// The `.SCX` file name for a resident chunk, resolved through the tree so its
// case is the disc's. Empty when the chunk names none.
std::string resolveScx(const DataFs& scptdata,
                       std::span<const std::byte> areaFile,
                       std::span<const std::byte> sceneFile,
                       const OpcodeTable& table, ChunkKind kind, int chunk);

}  // namespace omk

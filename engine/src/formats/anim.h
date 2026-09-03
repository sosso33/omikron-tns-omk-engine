// SPDX-License-Identifier: GPL-3.0-or-later
// Body-animation libraries - `gamedata/ANIMS/*.ani`, magic "3.0V".
//
// Loaded by Anim_Load (0x00434010), which slurps the file whole and relocates
// its offsets in place, so the layout in memory is the layout on disk:
//
//     +0   char[4]  "3.0V"
//     +4   int32    groupCount
//     +8   group[groupCount], 24 bytes: +0 index, +4 the first clip node
//     clip node, 36 bytes - a singly-linked list:
//              +0  int32   type   the behaviour slot this clip can fill
//              +4  int32   slot
//              +8  int32   the animation descriptor
//              +24 int32   the next node, 0 at the end
//              +28 char[8] the clip's name
//     descriptor:
//              +0  int32 frames
//              +4  int32 boneCount
//              +12 bone[boneCount], 40 bytes:
//                      +0  char[20] name
//                      +20 int32 posKeys, +24 int32 posOffset  (12 B a key)
//                      +28 int32 rotKeys, +32 int32 rotOffset  (16 B a key)
//                      +36 int32 flags
//
// **Track offsets are relative to the DESCRIPTOR, not the file.** Taken as
// file offsets they mostly still land on quaternions - the file is full of
// them - but the root bone's tracks fall inside the bone table and decode to
// garbage, which is the giveaway.
//
// A `.CTL` embeds descriptors of the same shape with **no "3.0V" wrapper**, so
// searching one for the magic finds nothing.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace omk {

struct AnimTrack {
    std::string  name;
    std::int32_t posKeys = 0;
    std::size_t  posOffset = 0;   // already absolute
    std::int32_t rotKeys = 0;
    std::size_t  rotOffset = 0;
    std::int32_t flags = 0;
};

struct AnimDescriptor {
    std::int32_t frames = 0;
    std::vector<AnimTrack> tracks;
};

struct AnimClip {
    std::int32_t group = 0;
    std::int32_t slot  = 0;
    std::int32_t type  = 0;       // how the engine ASKS for an animation: it
                                  // never names a clip - List_PickRandomByType
                                  // walks this list matching +0 and returns a
                                  // RANDOM one of the matches
    std::string  name;
    std::size_t  descriptor = 0;
};

// -> the clips of one library, or empty when `d` is not a "3.0V" file.
std::vector<AnimClip> animClips(std::span<const std::byte> d);

// -> the descriptor at `off`, or nothing when it does not read as one.
std::optional<AnimDescriptor> animDescriptor(std::span<const std::byte> d,
                                             std::size_t off);

struct Quat { float w, x, y, z; };
std::vector<Quat> animRotations(std::span<const std::byte> d, const AnimTrack& t);

}  // namespace omk

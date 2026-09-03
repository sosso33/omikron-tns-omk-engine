// SPDX-License-Identifier: GPL-3.0-or-later
// SCX chunk 10 - the scripted camera "editings" a cutscene is cut with.
//
// The payload is what the engine calls a *camera file*; its loader
// (0x0049EEF0, from Scene_LoadSCX case 0xDEAD000A) rejects any version but 3
// with "Invalid camera file version". Four arrays, each cross-referenced by id
// and resolved to pointers at load - **the loader returns 0 on any miss, so
// "every reference resolves" is the shipped invariant**, not a hope:
//
//     +0   u32 version = 3
//     +4   u32 nCameras  +8 u32 nKeys  +12 u32 nTracks  +16 u32 nEditings
//     +20  16 runtime bytes - the four base pointers, dead on disk
//     +36  camera[52]  x n: id, char[12] name, pos[3], target[3], roll, fov
//     then key[28]     x n: id, camera id, f32 frame, u32 mode
//     then track[24]   x n: id, char[10] name, u16 keyCount
//     then u32 key-id lists, one per track, concatenated in track order
//     then editing[32] x n: u8 id, char[11] name, u16 trackCount,
//                           u32 duration in frames,
//                           u16 target script object id (0 = unlinked)
//     then u32 track-id lists, likewise
//
// An editing is a keyframed camera cut sequence: tracks in order, each a run
// of (frame, camera) keys interpolated linearly by Cam_PlayEditing.
// Scene_LoadSCX links each to the script object its +28 handle names, and
// Script_PlayScript samples it at that object's program clock - which is what
// makes a cutscene's camera follow its beats.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

struct CamCamera {
    std::uint32_t id = 0;
    std::string   name;
    float pos[3] = {0, 0, 0};
    float target[3] = {0, 0, 0};
    float roll = 0, fov = 0;
};

struct CamKey {
    std::uint32_t id = 0;
    std::uint32_t camera = 0;
    float         frame = 0;
    std::uint32_t mode = 0;
};

struct CamTrack {
    std::uint32_t id = 0;
    std::string   name;
    std::vector<std::uint32_t> keys;      // key ids, in order
};

struct CamEditing {
    std::uint8_t  id = 0;
    std::string   name;
    std::uint32_t duration = 0;           // frames
    std::uint16_t objectHandle = 0;       // 0 = shipped unlinked
    std::vector<std::uint32_t> tracks;    // track ids, in order
};

struct CamFile {
    bool valid = false;
    std::size_t end = 0, size = 0;
    bool exact = false;                   // the walk landed on the payload end
    std::vector<CamCamera>  cameras;
    std::vector<CamKey>     keys;
    std::vector<CamTrack>   tracks;
    std::vector<CamEditing> editings;
    // every id a key, track or editing names must exist
    int unresolved = 0;
};

CamFile readCamFile(std::span<const std::byte> payload);

// ---- PLAYING one --------------------------------------------------------
//
// `Script_LinkCamEditing` links each editing to the script object whose
// handle's upper half equals the editing's `+28` target; the object's four
// slots at `+94..97` receive the editing id. No shipped object is the target
// of more than one (docs/CUTSCENES.md 2), so "the editing of object N" is a
// lookup and not a list.
const CamEditing* editingForObject(const CamFile& f, int objectId);
const CamEditing* editingById(const CamFile& f, int id);

// What `Cam_PlayEditing` (0x0049ECE0) writes into the scratch camera at
// frame `t`: every field of the two bracketing cameras interpolated linearly
// by its slope helper (0x0049EC10). The eye and target are the camera
// record's `+16` and `+28`, roll `+40`, fov `+44`.
struct CamSample {
    float eye[3] = {0, 0, 0};
    float at[3]  = {0, 0, 0};
    float roll = 0, fov = 0;
    int   track = -1;             // index into `e.tracks`
    std::uint32_t camera = 0;     // the bracketing pair's FIRST camera id
};

// -> false in exactly the two cases the engine gives up: `t >= duration`
// (it returns without touching the camera - `Script_PlayScript` never calls
// it there anyway) and no track bracketing `t` ("key not found for frame").
//
// Tracks are laid END TO END: each owns [base, base + its last key's frame)
// and `base` accumulates - which is `tools/cutscene.py`'s `sample` and what
// `verify.py: cutscene camera` pins at 24112/24112 frames landing in a pair.
bool sampleCamEditing(const CamFile& f, const CamEditing& e, float t,
                      CamSample& out);

}  // namespace omk

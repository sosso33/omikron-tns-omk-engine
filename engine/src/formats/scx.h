// SPDX-License-Identifier: GPL-3.0-or-later
// `SCPTDATA/*.SCX` - the scene scripts, one per playable location.
//
// Loaded by Scene_LoadSCX (0x00449750). Like a `.CTL` it is a saved memory
// image whose pointer fields hold dead addresses the loader overwrites as it
// walks. Unlike a `.CTL` it is also a **stream**: most of the file sits after
// the structural block and is pulled in by fread as each resource record is
// reached, which is why Aapkayl.SCX is 7 MB with a 39 KB block.
//
//     +0   int32  0x00DEAD00       magic
//     +4   int32  5                version; the loader rejects anything else
//     +12  int32  blockSize
//     +16  the block: chunks tagged 0xDEAD00NN, ended by 0xDEADFFFF
//     then the streamed resources, in the order the chunks reference them
//
// **A dword that is not a known tag is skipped and the walk carries on** -
// that is literally the loader's `default:` case - so the block may contain
// padding between chunks, and does. A reader that treated an unknown tag as
// an error would reject files the engine accepts.
//
// Chunk 2 is the substance: every one of the 220 shipped files has it. Objects
// come in state pairs (CoffreOpen / CoffreClosed) linked by name at load time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace omk {

// in-block record strides, by chunk tag; 2 and 10 are variable
inline const std::map<int, std::size_t> kScxStride = {
    {0, 32}, {1, 36}, {3, 26}, {4, 36}, {5, 28}, {6, 792}, {7, 32}};

struct ScxFunction {
    std::uint32_t id = 0;
    bool  isSync = false;
    std::vector<std::int32_t> params;
    // `sync` indexes the object's SYNC array, NOT `ScxObject::functions`, which
    // holds the main list and the sync list end to end. The loader is explicit:
    // `obj->syncFunctions + fn->sync`, refused past `+ syncCount`. Resolving it
    // against the flattened vector stays in range on every shipped file and
    // still runs, so it fails silently - see `Program::chain`.
    std::int32_t sync = 0, repeat = 0, runs = 0;
};

struct ScxObject {
    int          index = 0;
    std::string  name;
    std::uint32_t handle = 0;
    std::string  link;              // the partner state, empty when none
    bool         hasLink = false;
    std::vector<ScxFunction> functions;
    std::vector<std::string> tables[2];
    int          nfn = 0, nsync = 0;
    std::int32_t loop = 0;
};

struct ScxScene {
    bool valid = false;
    std::size_t size = 0, blockSize = 0, streamed = 0;
    std::vector<ScxObject> objects;
    std::map<int, std::size_t> chunkCounts;   // tag -> record count
    std::size_t blockEnd = 0;                 // where the chunk walk finished
    bool complete = false;                    // it landed inside the block
};

ScxScene readScx(std::span<const std::byte> d);

// ------------------------------------------------------------- the stream

// Most of an .SCX sits AFTER the structural block and is pulled in as each
// resource record is reached. The block declares them (chunk 2 first, then the
// tagged chunks in file order) and the stream carries their payloads in that
// same order.
//
// **Every streamed record's first header word is its own file offset**, which
// makes the walk self-checking - and makes a principled resync possible for
// the record kinds whose declared size does not cover their payload: scan
// forward to the next u32 equal to its own position.
//
// Chunk 4 is the exception that had to be found: its header is THREE words -
// [own offset, MODEL size, TEXTURE size] - and the payload is a whole .3DO
// immediately followed by its .3dt. A walk reading only the second word runs
// short and has to resync; read as 12 + model + texture it is exact, with no
// resync at all in any of the 220 files.
struct ScxStreamAnim {
    std::string  name;
    std::int32_t id = 0;
    std::size_t  offset = 0;     // the payload, past the header
    std::size_t  size = 0;
};

// A chunk-3 sound: the payloads are plain RIFF/WAVE, 16-bit PCM mono - the
// same bytes `Sound_Play3D` hands DirectSound - and the object programs name
// them by INDEX into this array (`Script_PlaySound` / `Script_PlaySyncSound`
// param 0). The in-block record is 26 bytes and carries the name.
struct ScxStreamWav {
    // The in-block record is `char[22] name`, `u16` handle, `u16` id - which
    // `Scene_FindSoundIndex` settles: it walks `base + 24` in 26-byte strides
    // looking for the id and returns that record's `+22`.
    std::string name;
    int         id = -1;         // +24, what a `.CTL` effect matches on
    std::size_t offset = 0;      // the RIFF file inside the .SCX
    std::size_t size = 0;
};

struct ScxStreamSprite {
    std::string name;
    // The registry row's `+32`. An effect's sprite field is this ID, resolved
    // through the SCENE by `sub_4A5800(scene + 8, id)` - not an index into any
    // library, which is what makes a global-index lookup land on the wrong
    // sprite entirely.
    std::int32_t id = -1;
    std::size_t offset = 0;      // where the .3DO starts
    // These two are SIZES, not offsets - the record's three-word header is
    // [own offset, model bytes, texture bytes] and the payload is one whole
    // `.3DO` immediately followed by its `.3DT`. Reading them as offsets
    // decodes nothing and looks like a broken texture rather than a bad span.
    std::size_t model = 0;       // bytes of .3DO at `offset`
    std::size_t texture = 0;     // bytes of .3DT at `offset + model`
};

// Chunk 0's payloads are `.3dp` PATH files - the engine's own Read3DP:
//
//     u32 pathCount, then per path:
//       char[20] name
//       u32      duration     == the last key's frame in all 6756 shipped
//       u32      keyCount
//       key[keyCount], 32 bytes: u32 frame, float pos[3], float quat[4]
//
// `Path_Sample` evaluates one at a time t, and `Script_MoveObjectOnPath`
// drives scene objects and characters along them - so a path is authored
// placement AND motion, which is why a relative body animation is positioned
// by sampling one rather than by a clip root.
struct ScxPathKey {
    std::uint32_t frame = 0;
    float pos[3] = {0, 0, 0};
    float quat[4] = {1, 0, 0, 0};   // w, x, y, z
};

struct ScxPath {
    std::string name;
    std::uint32_t duration = 0;
    std::vector<ScxPathKey> keys;
    // WHICH chunk-0 record this path came out of, and its index INSIDE it.
    // `Script_MoveObjectOnPath` addresses a path in TWO parts - param 1 names
    // the `.3dp` file and param 2 the path within it (`v89 = u32(v6, 4 *
    // GetParamInt(a2, 2))` after the file lookup) - so a flat index is the
    // wrong key and lands on another file's path in any scene with more than
    // one. `Script_SelectRelativeBodyAnimation`'s param 8 is a different
    // question and stays flat.
    int file = 0;
    int index = 0;
};

// `Path_Sample(path, t, ..., 1)` - mode 1 is LINEAR: find the key span holding
// `t` and interpolate. NOT `keys.front()`, which is only the same when the
// path's first key sits at `t`. -> false when no span contains it.
bool pathSample(const ScxPath& p, float t, float out[3]);
// The same sample's ORIENTATION - `Path_Sample`'s sixth out-parameter, the 3x3
// `Script_MoveObjectOnPath` hands to `sub_437160`. Returned as the (w,x,y,z)
// quaternion the keys store; `actor/pose.h`'s `qrot` applies it. An object
// that spins in place - a fan - has NO position change at all and is animated
// entirely by this.
bool pathSampleQuat(const ScxPath& p, float t, float out[3], float quat[4]);

struct ScxStream {
    bool valid = false;
    std::size_t end = 0;
    int  resyncs = 0;            // must be 0 on every shipped file
    std::vector<ScxStreamAnim>   anims;
    std::vector<ScxStreamSprite> sprites;
    std::vector<ScxPath>         paths;
    std::vector<ScxStreamWav>    wavs;
    // chunk 10's payload - the camera EDITINGS. Only 29 of the 220 scenes
    // carry one; it is a streamed block of its own with no in-block count.
    std::size_t camOffset = 0, camSize = 0;
};

ScxStream readScxStream(std::span<const std::byte> d);

}  // namespace omk

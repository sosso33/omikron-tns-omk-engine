// SPDX-License-Identifier: GPL-3.0-or-later
// `.3DM` morph files - a spoken line: the face vertices, the bone rotations
// and the ADPCM audio, interleaved a frame at a time.
//
//     +0   uint32  low 24 bits the AUDIO block size, high 8 the channel
//                  count minus one
//     +4   uint32  vertices
//     +8   uint32  a nominal frame count - NOT the real one
//     +12  uint32  nodes
//     +16  uint32 preamble[nodes]   0,1,2,... in every shipped file
//     then frames, each:
//          24 bytes a vertex, 16 bytes a node, 12 bytes, then the audio block
//
// **The real frame count is derived, not read.** Word 2 is nominal; the count
// that matches the file is `(size - preamble) / record`, and the only
// remainder that ever occurs is a last frame carrying no audio. 582 of the 777
// shipped files land on a record boundary and the other 195 are short by
// exactly one audio block - so a reader that trusted word 2, or that treated
// the remainder as corruption, would be wrong about a quarter of the corpus.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace omk {

struct MorphLayout {
    bool valid = false;
    std::uint32_t audio = 0;      // bytes of ADPCM a frame
    std::uint32_t channels = 1;
    std::uint32_t vertices = 0;
    std::uint32_t nominalFrames = 0;
    std::uint32_t nodes = 0;
    std::size_t   record = 0;
    std::size_t   preamble = 0;
    std::size_t   frames = 0;
    bool          lastFrameHasAudio = true;
    std::size_t   tail = 0;       // 0, or exactly record-audio
    std::size_t   size = 0;
    bool          preambleIsIota = false;   // 0,1,2,... - true in all 777
};

MorphLayout morphLayout(std::span<const std::byte> d);

// The ADPCM blocks, concatenated - what the decoder is handed.
std::vector<std::byte> morphAudio(std::span<const std::byte> d,
                                  const MorphLayout& L);

}  // namespace omk

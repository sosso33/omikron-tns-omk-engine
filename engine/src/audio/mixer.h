// SPDX-License-Identifier: GPL-3.0-or-later
// THE AUDIO PATH - the voices, the bank, the listener, and the mix.
//
// TIER (docs/PORTING.md B1), and it is not one tier for the whole file:
//
//   * tier 2, corpus-constrained - the `.wav` acceptance path
//     (`Wav_LoadToBuffer`, 0x0049F830) walked over all 61 shipped
//     `gamedata/I2D/sounds/*.wav`, which the data could fail; plus the immediates
//     and vtable offsets below, asserted against the IMAGE rather than
//     against this transcription of it.
//   * tier 3, differential - `Sound_LengthMs` (0x0046CC70) and the reference
//     mixer's transparency, both differenced against a Python re-derivation
//     in `verify.py`. B1's warning applies in full and is not softened here:
//     BOTH SIDES WERE WRITTEN FROM ONE READING of the same assembly, so this
//     catches an offset, a signedness slip or an off-by-one and cannot catch
//     a wrong reading applied consistently.
//   * tier 6, read and explained - the bank and voice bookkeeping. It is
//     transcribed from the wrappers listed below; nothing observes it,
//     because every one of them ends in a COM call.
//   * NO TIER AT ALL for the attenuation and panning law. See `render()`.
//
// ---------------------------------------------------------------------------
// WHY THERE IS NO SOFTWARE MIXER TO PORT
//
// `docs/PORTING.md` B6 asked for "audio mixing ... PCM sample-exact against
// tools/adp.py; the mix compared offline", and the first thing reading the
// engine settles is that **the engine does not mix**. `Sound_Init`
// (0x0046C3A0) creates a DirectSound primary buffer, sets its format, and
// calls Play(0,0,DSBPLAY_LOOPING) once; every sound after that is a
// *secondary* buffer that DirectSound's own mixer sums into it. There is no
// loop in the image that adds two samples together.
//
// So the engine's half of audio is entirely DECISIONS - which buffer exists,
// which voice plays it, where it is, how loud, at what rate - and that is
// exactly the boundary A2 draws for the renderer. This file ports the
// decisions. `render()` is a REFERENCE mixer standing where DirectSound
// stood: it is not a port of anything, it is the thing on the far side of the
// line, and its own header says so.
//
// The decode half is already at tier 1 and is not repeated here: OTNS ADPCM is
// sample-identical to `tools/adp.py` over all 777 `.3DM` files
// (`verify.py: engine morph+ADPCM`).
//
// ---------------------------------------------------------------------------
// THE WRAPPERS, AND THE THREE THAT ARE NOT IN THE DECOMPILATION
//
//   0x0046C3A0  Sound_Init            primary buffer + the mix format
//   0x0046C740  Sound_CreateBuffer    a slot of 160, and the caps word
//   0x0046C8B0  Sound_FreeBuffer      and it kills the voices playing it
//   0x0046C990  Sound_Lock            Lock
//   0x0046C9F0  Sound_Unlock          Unlock
//   0x0046CA40  Sound_Play            rewind, loop flag, Play
//   0x0046CAE0  Sound_Stop            Stop
//   0x0046CB20  Sound_GetPosition     GetCurrentPosition
//   0x0046CBB0  Sound_SetVolume       SetVolume
//   0x0046CBF0  Sound_SetFrequency    SetFrequency          <- absent
//   0x0046CC30  Sound_GetFrequency    GetFrequency          <- absent
//   0x0046CC70  Sound_LengthMs        GetFormat/GetCaps     <- absent
//   0x0046CD10  Sound_FindVoice       by (sound id, owner)
//   0x0046CD40  Sound_StopVoice       release one voice
//   0x0046CDC0  Sound_Play3D          allocate a voice, duplicate, place it
//   0x0046CFC0  Sound_SetVoice3D      move a voice
//   0x0046D080  Sound_SetListener     and the 2D fallback basis
//
// The three marked `absent` are in neither `Runtime.exe.c` nor `readable/`,
// and `asmfn.py` anchored on a label snaps past them. Their addresses were
// recovered by disassembling the gap between `Sound_SetVolume` and
// `Sound_FindVoice` and measuring each block back to a 16-byte alignment;
// `verify.py: engine audio` asserts their prologues and vtable offsets against
// the image, so they stay known.
//
// **The reason they are absent is NOT the missing `push` prologue**, which is
// what this comment claimed until it was checked. All four of
// `Sound_SetVolume`, `Sound_SetFrequency`, `Sound_GetFrequency` and
// `Sound_StopVoice` open with the identical `A1 <ppDS>` and no `push` - and
// `Sound_SetVolume` IS decompiled. The difference is measured: scanning every
// `E8` rel32 in the image gives `Sound_SetVolume` **6 direct callers** and the
// three **0**, with **0 dword references** to any of them anywhere, so nothing
// takes their address either. IDA never made functions of them because nothing
// reaches them, which means:
//
//     THESE THREE ARE DEAD CODE. Nothing in the shipped binary calls them.
//
// They are ported anyway - `Sound_LengthMs` carries an exact arithmetic law
// worth having, and the pair completes the wrapper set - but they join
// `pluie.wav`, options page 12 and the X-Tech shoot callback on the
// shipped-and-unreachable list, and no claim here rests on them.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk::audio {

// ---------------------------------------------------------------------------
// The mix format. `Sound_Init` writes these six fields into a WAVEFORMATEX and
// calls SetFormat on the primary buffer, so this is the format everything is
// mixed INTO, whatever a secondary buffer's own rate is.
inline constexpr int kMixRate        = 22050;
inline constexpr int kMixChannels    = 2;
inline constexpr int kMixBits        = 16;
inline constexpr int kMixBlockAlign  = 4;
inline constexpr int kMixBytesPerSec = 88200;

// DSSCL_EXCLUSIVE, and DSBCAPS_PRIMARYBUFFER|DSBCAPS_CTRL3D on the primary.
inline constexpr int      kCoopLevel     = 3;
inline constexpr unsigned kPrimaryFlags  = 0x11;

// The two tables. 160 buffers at dword_53B7E8; 16 voices of 64 bytes at
// unk_53B368 (0x53B368..0x53B768).
inline constexpr int kMaxBuffers       = 160;
inline constexpr int kMaxVoices        = 16;
inline constexpr int kVoiceRecordBytes = 64;

// The caps word `Sound_CreateBuffer` asks for, chosen by dword_4CC85C - the
// 3D-sound switch. With 3D: CTRL3D|CTRLFREQUENCY|CTRLVOLUME. Without:
// CTRLFREQUENCY|CTRLPAN|CTRLVOLUME. Note what swaps - PAN is only asked for
// when 3D is off, which is DirectSound's own rule (a 3D buffer's pan is the
// listener's business), and the engine follows it rather than asking for both.
//
// 0xB0 never appears as an immediate: the choice is BRANCHLESS, and what the
// image holds is the computation, which is what `verify.py` asserts -
//     neg ecx ; sbb ecx, ecx ; and ecx, 0FFFFFFD0h ; add ecx, 0E0h
// giving 0xE0 - 0x30 when the switch is on and 0xE0 when it is off. Searching
// the image for `176` finds nothing and would have read as a wrong address.
inline constexpr unsigned kCapsWith3d = 0xB0;
inline constexpr unsigned kCapsNo3d   = 0xE0;

// `Sound_SetListener`'s two literals: 0x3CD013A9 and 0x3F800000. The first is
// 0.0254 - metres per INCH - so DirectSound is told the game's world unit is
// an inch, which is the same unit `docs/FILE_FORMATS.md` reads in the staging
// offsets. Doppler is left at whatever GetAllParameters returned.
inline constexpr float kDistanceFactor = 0.0254f;
inline constexpr float kRolloffFactor  = 1.0f;

// ---------------------------------------------------------------------------
struct WaveFormat {
    std::uint16_t tag = 0, channels = 0;
    std::uint32_t rate = 0, avgBytes = 0;
    std::uint16_t blockAlign = 0, bits = 0;
};

// `Sound_Play3D`'s third argument. The layout is corroborated three ways: the
// copy into the voice record, `Sound_SetVoice3D`'s copy of the same block into
// SetPosition/SetVelocity/SetMinDistance/SetMaxDistance, and the slider's own
// call site (0x00456B40) which passes the literals **39.0 and 585.0** for the
// last two. 585.0 occurs exactly ONCE in that function and is loaded twice -
// once as `maxDistance` and once as the audibility test `if (d < 585.0)` - so
// the slider is dropped at exactly its own max distance rather than at some
// separate cut-off. Passing NO block means "not a 3D sound" - DS3DMODE_DISABLE.
struct Sound3D {
    float pos[3]{}, vel[3]{};
    float minDistance = 0.0f, maxDistance = 0.0f;
};

// `Sound_SetListener`'s argument, in ITS order - position, top, front,
// velocity - which is not DS3DLISTENER's order (position, velocity, front,
// top). The function is a transposition, and reading it as a straight copy
// swaps the orientation for the velocity.
struct Listener {
    float pos[3]{}, top[3]{}, front[3]{}, vel[3]{};
};

// The voice record's flag word, at +48.
enum VoiceFlags : std::uint32_t {
    kVoiceInUse   = 1,   // set on every successful Play3D
    kVoiceLooping = 2,   // `loop` argument non-zero  (the field is |3, not |2)
    kVoiceNo3d    = 4,   // no Sound3D block was passed
};

// The 64-byte record at unk_53B368 + 64*slot, field for field.
struct Voice {
    bool          used     = false;   // +4  the duplicated buffer, 0 when free
    float         pos[3]{};           // +8
    float         vel[3]{};           // +20
    float         maxDistance = 0.f;  // +32   (a3[7])
    float         minDistance = 0.f;  // +36   (a3[6])
    float         unknown40 = -1.0f;  // +40   written -1.0f, never read here
    float         frequency = 0.f;    // +44   the source buffer's GetFrequency
    std::uint32_t flags   = 0;        // +48
    int           soundId = -1;       // +52   initialised to -1 by Sound_Init
    std::uint32_t owner   = 0;        // +56   Play3D's fourth argument
};

// ---------------------------------------------------------------------------
// A loaded buffer. `data` is the file's `data` chunk copied verbatim - which
// is literally what the engine does: it Locks the buffer and freads into the
// pointer, with no conversion at all.
struct SoundBuffer {
    bool          used = false;
    WaveFormat    fmt{};
    std::uint32_t bytes = 0;
    std::uint32_t frequency = 0;   // SetFrequency overrides fmt.rate
    int           volume = 0;      // hundredths of a dB, 0 = full
    std::vector<std::int16_t> pcm; // the data chunk, as samples
};

// Why `Wav_LoadToBuffer` rejected a file. The engine only ever logs these; the
// enum exists so a sweep can COUNT them and the data can fail.
enum class WavReject {
    Ok = 0, Header, NotRiff, NotWaveFmt, PcmHeader, NotPcm, DummyByte,
    DataHeader, NoBuffer, TooShort,
};

struct WavLoad {
    WavReject   reject = WavReject::Ok;
    WaveFormat  fmt{};
    std::uint32_t dataBytes = 0;
    std::size_t   dataOffset = 0;
    bool        rewoundDummy = false;  // the 2-byte peek was non-zero
    int         skippedChunks = 0;     // non-`data` chunks the loop walked
};

// `Wav_LoadToBuffer`, 0x0049F830, exactly. Reads a 20-byte header and requires
// "RIFF" at +0 and "WAVEfmt " at +8; reads SIXTEEN bytes of WAVEFORMATEX and
// requires tag 1; then PEEKS two bytes and seeks back over them only if they
// are NON-zero - a trick for the 18-byte extended form, which reads the `da`
// of the next chunk id as "not cbSize" and rewinds. Then it walks 8-byte chunk
// headers until it finds `data`.
WavLoad loadWav(std::span<const std::byte> file);

// `Sound_LengthMs`, 0x0046CC70:
//     dwBufferBytes * 1000 / ((wBitsPerSample/8) * frequency * nChannels)
// Integer division, truncating. Note it uses bits/8 * channels and NOT
// nBlockAlign, which is the same number for every shipped file and would not
// be if one were 8-bit.
std::uint32_t lengthMs(std::uint32_t bytes, const WaveFormat& fmt,
                       std::uint32_t frequency);

// The volume law, `Music_SetVolume` (0x0042BE60) and 0x0042BBB0. The argument
// is a PERCENTAGE clamped at 100, and the result is `-10000 * pct / 100`, so
// **0 is full volume and 100 is silence**: it is an attenuation, not a level.
// The global it feeds is initialised to 0.
int volumeFromPercent(int percent);

// ---------------------------------------------------------------------------
class Mixer {
public:
    explicit Mixer(bool enable3d = true) : has3d_(enable3d) {}

    bool has3d() const { return has3d_; }
    unsigned capsWord() const { return has3d_ ? kCapsWith3d : kCapsNo3d; }

    // `Sound_CreateBuffer` - the first free slot of 160, or -1.
    int createBuffer(const WaveFormat& fmt, std::uint32_t bytes,
                     std::span<const std::int16_t> pcm);
    // `Sound_FreeBuffer` - and it stops every voice whose +52 names this id.
    bool freeBuffer(int id);
    int  liveBuffers() const;

    const SoundBuffer* buffer(int id) const;

    bool setVolume(int id, int hundredthsDb);
    bool setFrequency(int id, std::uint32_t hz);
    std::uint32_t getFrequency(int id) const;
    std::uint32_t lengthMs(int id) const;

    // `Sound_Play3D` - allocate the first free voice of 16, or -1. `place` may
    // be null, which is how a 2D sound is played (the interface does exactly
    // that: `Ui_PlaySound` at 0x00482D90 calls Play3D(id, 0, 0, 0)).
    int play3d(int soundId, bool loop, const Sound3D* place, std::uint32_t owner);
    bool setVoice3d(int slot, const Sound3D& place);
    int  findVoice(int soundId, std::uint32_t owner) const;
    bool stopVoice(int slot);
    int  liveVoices() const;
    const Voice& voice(int slot) const { return voices_[slot]; }

    void setListener(const Listener& l);
    const float* listenerBasis() const { return basis_; }   // the normalised front

    // ---------------------------------------------------------------------
    // THE REFERENCE MIXER - the far side of the boundary, NOT a port.
    //
    // DirectSound did this, and nothing in the image describes how. What is
    // written here is DirectSound's DOCUMENTED behaviour, and no check asserts
    // any of it beyond the one property that has to hold for the boundary to
    // be transparent:
    //
    //   a single mono voice, not 3D, at full volume and at the mix rate, must
    //   come out of `render` SAMPLE-IDENTICAL in both channels.
    //
    // That is the acceptance criterion `PORTING` B6 names, and it is the only
    // audio claim this file makes about a waveform. The attenuation curve, the
    // pan law and the resampler are declared unverifiable from here: no golden
    // trace can reach them (the announce log is a VM handler's business and
    // none of these is a VM handler) and no capture rig this repo has records
    // sound.
    void render(std::int16_t* out, int frames);

    // Advance without writing, for a test that only wants the cursors.
    void skip(int frames) { render(nullptr, frames); }

private:
    bool has3d_;
    SoundBuffer buffers_[kMaxBuffers];
    Voice       voices_[kMaxVoices];
    double      cursor_[kMaxVoices]{};   // per-voice read position, in samples
    Listener    listener_{};
    float       basis_[3]{0, 0, 0};
};

}  // namespace omk::audio

# 9. Audio

← [Rendering](08-rendering.md) · [Contents](README.md) · next: [The interface](10-the-interface.md)

---

## In short

The single most useful thing to know about the game's audio is that **the
engine does not mix it**. It hands DirectSound a list of sounds, where they are
in the world and how loud, and DirectSound sums them. There is no loop anywhere
in the executable that adds two samples together.

That changes what "porting the audio" means. There is no mixer to transcribe.
What there is, is a set of **decisions**: which of 160 buffers holds what, which
of 16 voices is playing, where the listener is, and how loud a thing is at a
distance. Those are ported and checkable. The actual attenuation and panning
curve belongs to a Microsoft library from 1997, is described nowhere in the
game's code, and **has no reachable evidence tier at all** — which the port says
in its own header rather than quietly implementing something plausible.

The speech is the interesting format. It is compressed with a custom ADPCM
scheme, and it lives **inside the same file as the character's facial
animation** — which is why the lip sync cannot drift.

## In detail

### The speech codec

OTNS ADPCM, transcribed from `sub_483200`. It is at the top evidence tier:
**sample-identical across all 777 `.3DM` files, 225 441 216 samples**, against
an independent Python decoder.

The `.3DM` file carries, together: the bones, the face morph targets, the root
motion, and the audio. One file, one clock.

### What `Sound_Init` establishes

`Sound_Init` (`0x0046C3A0`) creates a DirectSound **primary** buffer and sets
its format — PCM, 2 channels, **22 050 Hz**, 16 bits, block align 4, 88 200
bytes/second — then calls `Play(0, 0, DSBPLAY_LOOPING)` **once**. Every sound
after that is a *secondary* buffer that DirectSound's own mixer sums in.

That one reading corrected a documented acceptance criterion. The porting
standard originally said the reference mixer would be compared "sample-exact",
which assumed the engine had a mixer to compare against. It does not — so the
criterion could not have been met by anything, and the row was rewritten.

### The engine's half: the decisions

| | |
|---|---|
| the bank | 160 buffers |
| the voice pool | 16 voices, with its flag word |
| the loader | `Wav_LoadToBuffer` (`0x0049F830`) — accepts all **61** shipped `.wav` |
| freeing | `Sound_FreeBuffer` kills whatever voice is playing it |
| the listener | with a `<= 0.0001` guard and a **0.0254** distance factor — the engine is told the world unit is an **inch** |
| the volume law | an **attenuation**: 0 is full, 100 is silent |

The interface sounds are a closed set: ids 0..44, and all 45 `.wav` ship (61
do; 16 are never named). The name table is 45×20 bytes and **the cache is 32
slots**, so 13 of the 45 can never be resident — `Ui_LoadSound` returns silently
when it is full.

### Movie audio is not this path

Sharp enough to write down: the three `FLIS/` streams carry one **44 100 Hz**
audio track each, and the game's own primary buffer is **22 050**. The original
played the movies through DirectShow, which had its own output and never went
through that mixer. A replica that fed movie audio through the ported 22 050
path would be wrong about both the rate and the route.

### Sounds attached to things

Audio is not only played by scripts. Two other paths carry it:

* the `.SFX` scene sound sets — 59 / 59 walking exactly over six sections, with
  the cin-sfx rows tied to `Anim_TickClipSfx`;
* the `.CTL` states' **effect records** at `+28` — bone-attached sprites and
  frame-triggered sounds, footsteps among them; all 590 decode.

So a footstep is not a script event. It is a frame number in a state's own
record.

### Dead code, found by asking the right question

`Sound_SetFrequency`, `Sound_GetFrequency` and `Sound_LengthMs` are absent from
the decompilation. The first explanation offered was that they lack a `push`
prologue. That was wrong, and the correct one generalises: **IDA makes a
function where it sees a call.** `Sound_SetVolume` opens *identically*, has six
callers, and is decompiled. Those three have **0 direct callers and 0 address
references anywhere in the image** — they are dead code.

The practical question about a missing function is therefore never "does it
start with a push" but **"does anything call it"**.

### How the port does it

`engine/src/audio/mixer.*` runs the decisions above; `voiceover.*` resolves
`media.play` to a `VOICEOFF/*.ADP`; `music.*` the music.

The tiers, declared per part rather than for the subsystem:

| part | tier |
|---|---|
| the loader, the immediates and vtable offsets asserted against the image | **2** — corpus-constrained |
| `Sound_LengthMs`, and the reference mixer's transparency | **3** — differential, and B1's warning about two implementations of one reading applies in full |
| the bookkeeping | **6** — read and explained |
| the attenuation and pan law | **none** |

The single waveform claim the reference mixer makes, and meets: a mono voice,
not 3D, at full volume and at the mix rate comes out **sample-identical in both
channels** (`men001.wav`, 32 953 frames), with `pause.wav` at 22 080 Hz as the
control that must differ.

## Where it lives

| | |
|---|---|
| findings | [`docs/ASSETS.md`](../docs/ASSETS.md) (ADPCM, `.SFX`, effect records), [`docs/UI.md`](../docs/UI.md) (the 45 interface sounds), [`docs/PORTING.md`](../docs/PORTING.md) §A5 |
| the port | `engine/src/audio/mixer.*`, `voiceover.*`, `music.*`; `engine/src/formats/adpcm.*`, `sfx.*` |
| the reference decoder | `tools/adp.py`; `adp/pc_adp_otns.c` is the reference C decoder |
| checks | `verify.py: engine audio`, `adpcm`, `sfx sections`, `actor sounds` |

## What is not settled

* **The attenuation and pan law has no reachable tier.** It is DirectSound's, is
  described nowhere in the image, and no rig here records sound. The port's
  `render()` says so in its own header rather than borrowing credibility from
  the checks around it.
* **The twelve per-screen interface sound slots are not fired** by the port's
  widget walk yet.
* **10 of 561 named voice-over files ship.** A property of the data, asserted so
  it stays explained rather than looking like a decode failure.

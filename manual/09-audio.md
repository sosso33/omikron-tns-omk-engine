# 9. Audio

← [Contents](README.md) · prev: [Rendering](08-rendering.md) · next: [The interface](10-the-interface.md)

---

## In short

The most useful thing to know about the game's audio is that **there is no
mixer in it**.

The engine opens one DirectSound primary buffer, sets it to 22 050 Hz stereo,
starts it looping, and after that every sound is a secondary buffer that
DirectSound itself sums. Nothing in the executable ever adds two samples
together. What the engine does is *decide*: which buffer, where in the world,
how loud, and when to stop it.

That changes what a port of it can even claim. The decisions are portable and
checkable — a bank of buffers, a pool of voices, a listener, a volume law. The
sound that comes out is the operating system's, and no rig in this repository
records audio, so the attenuation curve and the panning have **no reachable
tier at all**. They are written down as unverifiable rather than quietly
implemented as though they were established.

The one part that *is* exact is the decoder. The game's speech and its facial
animation ship in the same file, in a variant of ADPCM, and the transcription
of it is sample-identical to an independent reader across all 777 of them —
225 441 216 samples.

## In detail

### What `Sound_Init` actually sets up

```
primary buffer:  PCM, 2 channels, 22050 Hz, 16 bits,
                 block align 4, 88200 bytes/sec
                 Play(0, 0, DSBPLAY_LOOPING)   — once, at startup
```

Everything after that is a secondary buffer. The engine keeps a **160-buffer
bank** and a **16-voice pool** with a flag word, frees a buffer by killing
whatever plays it, and positions sounds against a listener whose distance
factor is `0.0254` — which is the engine telling us, in its own units, that
**the world unit is an inch**.

The volume law is an **attenuation**, not a gain: 0 is full and 100 is silent.

Three wrappers around it — set frequency, get frequency, length in
milliseconds — turn out to be **dead code**: no direct callers and no address
references anywhere in the image. That is why they appear in no decompilation,
and it is not the missing prologue it was first read as.

### The decoder

OTNS ADPCM, transcribed from `sub_483200`. It decodes the audio embedded in
every `.3DM` morph file — the voice recording that the facial animation was
made against — and the transcription is checked against a Python re-derivation
over the whole corpus: **777 of 777 files, sample-identical**.

The voice-over that plays outside a conversation is a separate family:
`media.play` names an object whose stem is a `VOICEOFF/*.ADP`. **Only 10 of the
561 the scripts name are on the disc.** That is a property of the shipped data
rather than a gap in the reading, and the check that counts them exists so it
stays explained.

### The interface sounds

45 of them, ids 0..44, named by a table of 45 × 20 bytes. All 45 resolve to a
shipped `.wav` — 61 ship in total, so 16 are never named by anything — and the
twelve per-screen slots are positional: move, confirm, open.

The cache holds **32 slots**, so 13 of the 45 can never be resident; the loader
simply returns when it is full.

### A sound in the world is 3D

`Script_PlaySound`, the scene programs' own sound call, positions its sound at a
node — it is a 3D call in the engine, and playing it flat is audibly wrong in a
street. Its sibling `Script_PlaySyncSound` looks like the same function and is
not: **parameter 1 is the frame to fire on** in one and **a loop flag** in the
other. Decoding both alike invents a cue time for every call of the second, and
that is exactly what happened once.

The scene sound file `.SFX` carries the ambient effects too, and its cin-sfx
rows are tied to the animation tick that fires them.

### Music, and the movies

Music is started by its own opcode with an area's own music field behind it.
The second operand is **not established** as a loop flag, which is a live
question rather than a settled one — a short track that loops for ever is a
reported symptom with an unproven cause.

The three intro movies carry **44 100 Hz** audio and go straight to the device.
They never went through the game's 22 050 primary buffer — the original played
them through DirectShow, which had its own output — so a replica that fed them
through the ported path would be wrong about both the rate and the route.

## Where it lives

| | |
|---|---|
| the findings | `docs/ASSETS.md` (ADPCM, the `.SFX` chain), `docs/PORTING.md` A5 and B6 |
| the port | `engine/src/audio/` — `mixer.*` (the bank, the voices, the listener), `voiceover.*`, `music.*` |
| the reference decoder | `adp/pc_adp_otns.c`, `tools/adp.py` |
| the checks | `engine audio`, `engine voice over`, `adpcm`, `.3DM files`, `cutscene music` |

## What is not settled

* **The attenuation and pan law is DirectSound's**, is described nowhere in the
  image, and no rig here records sound. It has **no reachable tier**, and the
  port's `render()` says so in its own header.
* **The music opcode's second operand.** Read as a loop flag it explains a
  reported symptom; nothing establishes that reading.
* **551 of the 561 voice-over files are not on the disc.** Explained, not
  missing.
* The one waveform property that *is* asserted is transparency: a mono voice,
  not 3D, at full volume and at the mix rate comes out of the reference mixer
  unchanged in both channels — with a file at a different rate as the control
  that must differ.

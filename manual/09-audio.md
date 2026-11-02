# 9. Audio

← [Contents](README.md) · prev: [Rendering](08-rendering.md) · next: [The interface](10-the-interface.md)

---

## In short

The most useful thing to know about the game's audio is that **there is no
mixer in it**.

The engine opens one DirectSound primary buffer, sets it to 22 050 Hz stereo
and starts it looping. After that, every sound is a secondary buffer that
DirectSound itself sums. Nothing in the executable ever adds two samples
together. What the engine does is *decide*: which buffer plays, where it sits
in the world, how loud it is, and when it stops.

That changes what a port of it can claim:

* **The decisions are portable and checkable**: a bank of buffers, a pool of
  voices, a listener, a volume law.
* **The sound itself is the operating system's.** No rig in this repository
  records audio, so the attenuation curve and the panning have **no reachable
  tier at all**. They are written down as unverifiable rather than implemented
  as though they were established.
* **The decoder is exact.** The game's speech, its music and its facial
  animation all use one variant of ADPCM. The port's decoder is
  sample-identical to an independent reader across all 777 dialogue files.

Two threads run through the port's audio since the last revision of this
chapter. Both bring it closer to how the original holds its sound, and both
were proved without changing a sample:

* **The original's own shape.** The world's sound now plays at the
  original's 22 050 Hz. The music streams through a small ring, as the game's
  fixed buffer does. A spoken line is read and decoded *as it plays*, as the
  game's 15 ms timer does, instead of whole when it starts.
* **Small machines.** The same code now sounds through:
  * SDL 2 and SDL 3 on the desktop;
  * the PS Vita and the Nintendo 3DS;
  * the Meta Quest;
  * the Sound Manager of Mac OS 9 and Mac OS X 10.4 on PowerPC.

The game's voice-overs are settled as well. Of the 561 the scripts can play,
520 play a two-second jingle on purpose.

## In detail

### What `Sound_Init` actually sets up

```
primary buffer:  PCM, 2 channels, 22050 Hz, 16 bits,
                 block align 4, 88200 bytes/sec
                 Play(0, 0, DSBPLAY_LOOPING)   — once, at startup
```

(`docs/ASSETS.md` §3c, `docs/PORTING.md` B6.) Everything after that is a
*secondary* buffer. The engine keeps a **160-buffer bank** and a **16-voice
pool** of 64-byte records.

* A buffer asks for `CTRL3D` when the 3D switch is on, and for `CTRLPAN` only
  when it is off, because a 3D buffer's pan belongs to the listener.
* Freeing a buffer first stops every voice that is a duplicate of it.
* The interface plays its sounds with no placement block at all, which is
  `DS3DMODE_DISABLE`: flat.

The listener is handed a distance factor of **`0.0254`**, metres per inch.
That is the engine telling DirectSound, in its own units, that **the world unit
is an inch**. Its 48-byte struct is in the order position, top, front,
velocity. That is *not* DirectSound's order, so a straight copy would swap the
orientation for the velocity (`docs/ASSETS.md` §3c).

The volume law is an **attenuation**, not a gain: `-10000 * pct / 100`
hundredths of a dB, so 0 is full and 100 is silent.

Three wrappers (set frequency, get frequency, length in milliseconds) are
**dead code**. They have no direct callers and no address held anywhere in the
image, which is why they are in no decompilation. `Wav_LoadToBuffer` accepts
all **61** shipped interface `.wav`. Two of its branches are never taken by
the shipped corpus (engine/README "The audio path"; `engine: audio`).

### Grades, stated once

`docs/PORTING.md` B6 grades the audio row several ways, and the grades matter
more than the list:

| part | tier |
|---|---|
| the loader, and the immediates and vtable offsets asserted against the image | **2** |
| `Sound_LengthMs` and the reference mixer's transparency | **3** — both sides written from one reading |
| the bookkeeping (bank, voices, listener, volume law) | **6** |
| the attenuation and pan law | **none** — DirectSound's, described nowhere in the image |

The one waveform claim that *is* met is transparency. A mono voice, not 3D, at
full volume and at the mix rate comes out of the reference mixer unchanged in
both channels (`men001.wav`, 32 953 frames). `pause.wav`, the only interface
file at 22 080 Hz, is the control that must differ (`engine: audio`).

### The device runs at the original's rate

`Sound_Init`'s primary is 22 050 Hz, and so is what the game ships: 61 of 63
WAVs (the other two are 22 080), the voice ADPCM and the music. The port's
device used to run at 44 100, because the films opened it first, so every
world sound was resampled on the way out.

Since 2026-09-30 the films keep their 44 100 and the world **reopens the
device at 22 050** after them (`todo/optimization.md` step 28 g/i/m). The
original played the films through DirectShow, whose output never met the
primary buffer. A replica that fed them through the 22 050 path would be wrong
about both the rate and the route (`docs/PORTING.md` A5; `engine: movies`).

**The scene's sounds are kept as their files hold them** (2026-10-05,
`todo/ram-vs-original.md` tier B). They stay 16-bit at the file's own rate, as
`sub_46C740` keeps a bank buffer, and are read at the device rate by the host
mixer. Since 2026-10-05 the `.SCX` keeps its clips' and sounds' bytes and drops
the file, as `Scene_LoadSCX` does. The sounds are 94% of those bytes and the
original keeps them too (`engine: device sounds`, which holds all 1 728
shipped sounds and their mixes bitwise; `engine: scx kept`).

### The voice drives the clock

A dialogue line does more than sound. While a line's voice plays, the frame's
delta is taken from **the voice's play position** rather than the wall clock,
so the animation and the face cannot drift from the speech. This was read on
2026-10-06 (`docs/BOOT.md` §4; `todo/drift-audit.md` T2):

* `sub_42CC10` is "a line is loaded";
* `sub_42BC30` is the play position in frames of the 30 fps morph clock;
* this path has **no** three-frame clamp.

The port runs it as `script/linesync.h` (`engine: line sync`).

**And the line streams as it plays** (2026-10-10,
`todo/handoff-classic-mac.md` step 2c). The original never reads a line whole.
`Morph_Open` opens the file and primes its buffer, then a 15 ms timer reads and
decodes the next blocks while the line plays.

The port had read the whole `.3DM` (up to 3.4 MB) and decoded its whole voice
on the frame the line started. Measured on an iBook G3, that was 0.2 to 1.1
seconds of one frame. Now:

* `formats/morphstream` reads the header and the first second;
* the player keeps the stream two seconds ahead of the line's clock;
* the voice is a sound that *grows* while it plays, kept 16-bit.

The threaded read-ahead that preceded it, which read every possible next line
whole, is gone. `engine: line stream` hashes five streamed lines against the
same files read and decoded whole. It turns red when the decoder is reset per
block and when a fill reads a byte short.

### Pausing is a sound decision too

Screen 31, the pause, keeps the world *drawn* but stops it ticking. Its own
open and close callbacks do the sound (`docs/UI.md` §3b, "Which screens stop
the world"):

* the open calls `sub_46C290`, which walks the sound bank, and three more
  suspend routines directly;
* the close calls their partners.

The port reads the four pairs as a *suspend*, not a stop: the two streaming
handles restart from a saved position. It reproduces that by flushing what it
has queued and not pulling while paused.

A second mechanism reaches the same routine. Any screen that hides the world
calls `sub_466B30`, whose arms include the sound-bank suspend. That arm is
**not ported**, because the mixer has no suspend API (`docs/UI.md` §3b).

### The decoder

OTNS ADPCM, transcribed from `sub_483200`. It decodes the audio embedded in
every `.3DM` morph file: the voice recording the facial animation was made
against. It is checked against `tools/adp.py`, an independent Python decoder,
over the whole corpus: **777 of 777 files, 225 441 216 samples,
sample-identical** (`docs/PORTING.md` A5; `engine: morph+ADPCM`).

**The nibble law is a table** (`todo/optimization.md` step 16). A channel's
whole state is its predictor and its step index, so what one nibble does is a
function of (index, nibble) alone. The port builds those **89 × 16** entries
once. `engine/tools/adpcm_equiv` keeps the old branching law verbatim and
shows the two equal over **all 93 323 264 channel states**, every one of the
**145** music tracks and every voice-over. The check is `engine: adpcm table`,
shown to fail. Since 2026-10-10 the decoder carries its state across blocks
(`AdpcmStream`), and decoding a whole file is that stream called once, so the
two cannot differ.

### Music

Music is `music.play`, opcode 103 (`docs/SCRIPT_VM.md` §103; `music.play`):

* `Music_PlayTrack` builds `TRACKS\<n>.ADP` from the first field and streams
  it;
* the second field is the loop flag;
* the handler skips a request for the track already playing;
* **518 of its 521 sites** name one of the **145** shipped tracks, and the
  other 3 are track 0, which the callee refuses.

The area header does the same from **`AREA +142`**, and **189 of 189**
non-zero values name a real file. `music.volume`, opcode 131, ramps the same
attenuation over a number of frames. 52 of 52 sites stay inside 0..100, and
36 of them fade the music out (§131; `music.volume`).

**The shower has its own music** (`docs/ASSETS.md` §4e). When the city weather
begins it posts message 23, and when it ends, message 24. Every city
subscribes to both with a pair of tracks, a shower version and then its own
default back:

| city | shower track / default track |
|---|---|
| Anekbah | 119 / 2 |
| the Impasse | 119 / 88 |
| its airlock | 118 / 20 |
| Jaunpur | 122 / 21 |
| Qalisar | 120 / 56 |
| Lahoreh | 121 / 27 |

The reader listened to tracks 118–122 (2026-10-27). Each is its area's music
mixed with the same rain sound effect. Qalisar and Lahoreh ship weather type
0, so nothing ever posts message 23 there, and tracks 120 and 121 are
**authored and unreachable**. The restored weather (chapter 8), off by
default, makes them reachable. Under a roof the weather stops but posts no
24, so the rain's track plays on. The remaster's "rain sound by cover" was
dropped for that reason: the rain sound *is* the music track
(`todo/remaster.md`).

**How the port plays it.** A stereo `.ADP` has no header and no blocks (one
byte is one frame), so nothing forces a track to be decoded ahead of time.
The port's player went through four stages:

1. the whole track resampled to floats;
2. the whole track kept as 16-bit (`todo/optimization.md` step 6);
3. after the Vita refused the city track's 16 MB, the file kept open and
   decoded forward, read in 32 KB windows (`todo/vita-port.md`);
4. since 2026-10-04, **a ring, as the original's**: 256 KB where 5.4 MB of
   played samples had been held. The original's equivalent is `Morph_Open`'s
   fixed 8-second DirectSound buffer (`docs/RECONSTRUCTION.md`, 2026-10-04;
   `engine: music ring`).

Each stage was proved against the one before by `engine/tools/music_equiv`,
which keeps the old player verbatim: **0 mismatches** over 425, 82 and 56
million samples for the first three changes (`engine: music storage`).

### The interface sounds

There are 45 of them, ids 0..44, named by a table of 45 × 20 bytes that ends
exactly on the next label. All 45 resolve to a shipped `.wav`. 61 ship, so 16
are never named by anything (`docs/UI.md` §3).

The **cache holds 32 slots**, so 13 of the 45 can never be resident together.
The loader takes the first free slot, does not check whether the id is already
cached, and returns silently when it is full (`docs/ASSETS.md` §3c).

Each screen names **twelve** sound ids. Which slot plays when is decided by
`sub_482FE0`, which reads the live input word and picks by bit:

* **slot 0** on confirm;
* **slot 1** on back;
* **slot 2** on a selection move;
* **slot 3** on close.

There is no screen-opening sound at all. That order was corrected by a reader
**playing** the replica, who heard the validation sound on every move. The
names, the counts and the cache agreed with each other whichever meaning was
attached, so no check could have caught it (`ui sound slots`). The sound is not
gated on the dispatch: a frame a widget consumed still clicks (`docs/UI.md`
§3c).

A screen's code also plays sounds **by id**, independently of its slots: the
sneak's consumable arm plays id **13** (`SNK012`) and its successful combine
id **12** (`SNK009`) (`docs/UI.md` §3b). The special screens' own sounds are
still unported (`docs/UI.md` §3k).

### A sound in the world

`Script_PlaySound`, the scene programs' own sound call, positions its sound at
a node. Its sibling `Script_PlaySyncSound` looks like the same function and is
not:

* in the sync call, **parameter 1 is the frame to fire on**. 467 of the 475
  sync cues fire inside their editing's duration;
* in the plain call, parameter 1 is **a loop flag**.

Decoding both alike invents a cue time for every call of the first
(`docs/CUTSCENES.md` §5; `cutscene sound`). The scenes' sounds are plain
16-bit 22 050 Hz RIFF/WAVE inside the `.SCX`: 563 across the 29 cutscene
scenes, all 563 starting with `RIFF`.

A `.CTL` state's footstep and effect sounds name an **id**, which is looked up
in the resident scene's sound records. So the same id names different sounds
in different scenes, and a state's footstep is silent in a scene that does not
carry it. That is the engine's own behaviour (`docs/ASSETS.md` §7;
`engine: actor sounds`).

A fight loads **`fight.scx`** the way the boot loads `aventure.scx`: 21 sounds
(the punches, the kicks, the block, the fall, the cries) and 16 sprites that
exist nowhere else (`todo/fight-mode.md` §15.2; `engine: fight library`).
Since 2026-10-05 it is loaded per mode and released after, as `Game_Start`
swaps it (`todo/ram-vs-original.md`).

In shoot mode the hurt sound when a bolt hits the player is **flat, not
positional** (`todo/handoff-shoot-mode.md` §4). The gunfire *noise*
(`sub_4246E0`) is not a sound at all. Every shot and every impact alerts the
gunmen on that floor within a hearing range in map cells (`todo/shoot-mode.md`).

**The vehicles' motor** is the slider's `SOUNDS\sliderm01.wav`, the only
vehicle sound loaded, so the motos play it too (`docs/STREET_LIFE.md`;
`docs/ASSETS.md` §3c):

* it plays 3D at the body, with min and max distances **39** and **585**;
* it starts inside 585 units of the **listener**, which is the camera's eye
  from the frame before;
* it is re-placed from the drive step and stopped beyond.

The literal 585.0 occurs once in the function and is loaded twice, so a
vehicle drops out at exactly its own max distance. Ported 2026-10-06 in the
drift audit: the port had measured against the player through a set-up
nothing called, so no vehicle had ever sounded. The gain is DirectSound's
documented inverse-distance law, **a labelled reconstruction**. There is no
Doppler, because the host mixer has no pitch (`engine: vehicle sound`).

The directory's other file, `pluie.wav`, **cannot be reached**: the executable
holds no path template that could name it (`docs/ASSETS.md` §3b;
`SOUNDS folder`).

### The voice-overs

Outside a conversation, `media.play` (op 92) names an `IAM\OBJECT` record
whose stem is a `VOICEOFF\*.ADP`. It plays it through the **same streamer** a
`.3DM` line uses (22 080 Hz, mono, 30 fps, with no morph tracks), so only one
media voice sounds at a time (`docs/CUTSCENES.md` §5).

**Only 10 of the 561 such objects have their own file on the disc, and that is
not a gap.** The handler rewrites any name beginning `ZVOT` or `ZVOP` to
`JINGOFF3.ADP`, a 2.08 s jingle that ships. The partition is:

* **10** with their own file;
* **520** that play the jingle;
* **31** genuinely silent.

The question is recorded as closed in `docs/PRACTICE.md` §6 (2026-10-14). The
resolution is tier 4 against the golden traces' own media ids (102
announcements, 56 distinct, 56 of 56 resolving), the decode is tier 3, and
loudness and placement have no tier (engine/README §Coverage;
`engine: voice over`, `cutscene music`). Five more files in `VOICEOFF` are
orphans no buildable path reaches.

### The hosts

The mix the port hands a device is one piece of code, `src/audio/hostmix.*`.
It moved there out of the SDL frontend on 2026-10-03 so that a host without
SDL could play it (`docs/RECONSTRUCTION.md`, 2026-10-03). The hosts are:

* **SDL 2 or SDL 3** on the desktop. SDL 3's audio stream has run since
  2026-10-26. `AUDIO_F32SYS` keeps the samples in the host's byte order on a
  big-endian Mac.
* **The Carbon frontend**, through the Sound Manager on Mac OS 9 and Tiger.
  It plays one sampled-sound channel and a ring of six 100 ms buffers, refilled
  on the main thread because the completion callback runs at interrupt time on
  OS 9. Proved by the buffers coming back: 72.3 s played in a 75 s run on
  Tiger. The music has since been played by hand on both systems, in the
  emulators (2026-10-04, `todo/handoff-classic-mac.md`).
* **The PS Vita, the 3DS and the Quest**, through their own frontends.

None of these is a tier. Each is the operating system's device, behind the
same boundary.

## Where it lives

| | |
|---|---|
| the findings | `docs/ASSETS.md` §3b and §3c (the sound path, the loader, the cache), §4e (the shower's messages and tracks); `docs/SCRIPT_VM.md` §103 and §131; `docs/CUTSCENES.md` §5 (sync cues, voice-overs); `docs/BOOT.md` §4 (the line sync); `docs/STREET_LIFE.md` (the vehicles' motor); `docs/UI.md` §3 (the slots); `docs/PORTING.md` A5 and B6 |
| the port | `engine/src/audio/` — `mixer.*` (the bank, the voices, the listener), `hostmix.*` (the device mix every frontend shares), `voiceover.*`, `music.*`; `engine/src/formats/adpcm.h` (the table, `AdpcmStream`, the stereo stream), `formats/morphstream.*` (a line streamed); `engine/src/script/linesync.h` |
| the equivalence tools | `engine/tools/adpcm_equiv.cpp`, `engine/tools/music_equiv.cpp` |
| the reference decoder | `tools/adp.py` |
| the task records | `todo/optimization.md` steps 6, 16 and 28 (g/i/m), `todo/ram-vs-original.md` (tier B), `todo/handoff-classic-mac.md` (step 2c, the Sound Manager), `todo/drift-audit.md` T2, `todo/slider-drift-audit.md` A9 |
| the checks | `engine: audio`, `engine: morph+ADPCM`, `engine: adpcm table`, `engine: music storage`, `engine: music ring`, `engine: device sounds`, `engine: scx kept`, `engine: line stream`, `engine: line sync`, `engine: audio queue bound`, `engine: voice over`, `engine: stop sound`, `engine: scene sounds`, `engine: actor sounds`, `engine: vehicle sound`, `engine: fight library`, `engine: movies`, `.3DM files`, `music.play`, `music.volume`, `cutscene sound`, `cutscene music`, `ui sound slots`, `SOUNDS folder` |

## What is not settled

* **The attenuation and pan law is DirectSound's.** It is described nowhere in
  the image, and no rig here records sound. It has **no reachable tier**, and
  the port's `render()` says so in its own header. The vehicles' inverse-distance
  gain is the documented law, labelled as such.
* **The mix itself has no check.** Nothing in the tree captures a device's
  output. The equivalence tools prove the samples handed to it, not what a
  speaker plays (`todo/optimization.md` step 33).
* **The per-screen slots are not fired by the headless widget walk**, per the
  coverage audit (engine/README §Coverage). The special screens' own sounds are
  labelled unported in their sources (`docs/UI.md` §3k).
* **The sound-bank arm of `sub_466B30`**, the suspend every world-hiding screen
  triggers, is not ported.
* **A state's footsteps** re-arm on the clip's wrap in the port. What the
  engine's latch reduces to for an open-window record is **not traced**, and
  the port labels its rule a reconstruction (`docs/ASSETS.md` §7).
* **Why Qalisar and Lahoreh never rain** (and so never play their shower
  tracks) is not in the data: forgotten or switched off late
  (`docs/ASSETS.md` §4e).

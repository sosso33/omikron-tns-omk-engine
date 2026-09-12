# The END of a shoot phase — three symptoms a reader reported, 2026-09-12

A reader died in the supermarket phase (`--area 230 --scene-chunk 56`) and
reported three things at once:

1. the shoot phase's **music is still playing** afterwards;
2. **a character from the phase's intro cutscene is still standing there**, in
   a T pose, half inside a pillar;
3. leaving the supermarket puts the game **in cutscene mode, fades out, and
   then nothing happens**.

Their own reading was "some VM variables are not set up correctly at the end of
the shoot phase", which is the right place to look.

## 0. Reproduced headlessly

```
SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables \
    --save ../traces/save-appart.bin --area 230 --scene-chunk 56 --frames 1400
```

The player is shot by robber 77 and dies at frame 689; the death clip ends at
749 and message 1 runs the phase's loss branch. At frame **1400**:

* `audio: music peak ...` is still track **91** — the phase's own track, and
  the log never prints a second `music switch`;
* `staged 13 characters ... 1 on screen at the end` and that one is
  **`actor 65 V5H_FNM`**, at 13046 -140 1149 — the cashier the intro cutscene
  shows. Symptom 2, and the model matches the reader's screenshot.

So 1 and 2 reproduce with nobody at the keyboard. 3 has not been reproduced
yet (it needs the player walked to the exit zone).

## 1. What the script actually does on a death

SCENE 56's **message table entry 0** (`tools/script_dump.py SCENE 56`, the
listing at 16964) is the loss branch, and it is all there:

```
16964  fade.from_color 0,0,50,0
16976  object.release
16977  shoot.end        1
16980  set.var          104   ; 'Shoot ON'            -> 0
16983  set.var2         137   ; 'Supermarché Joué'    -> 1
       ... character.hide x25, zone.disable x30, object.hide x3 ...
17157  set.var.i8       34, 10   ; 'Vie'
17161  set.var.i8       637, 10  ; 'Vie'
17165  set.var          40    ; '1-A Shoot Sup En Cours' -> 0
17168  actor.stat.set   -1, 1, 637
17175  player.anim.hold
17176  fade.to_black
17177  scx.play         obj 0x0125
17184  media.play       251   ; ZVO M011 Méditek Supermarché
17187  scx.play.actor   12, obj 0x00c7
17194  scx.play.player.wait obj 0x0056
17199  player.anim.release
17200  fade.from_black
17201  camera.set       0, 0, 2
17208  zone.enable      3903
17211  scene.unload     230
17214  end
```

`set.var` (op 12) writes **0** and `set.var2` (op 13) writes **1**; the port's
interpreter has that right (`engine/src/script/interp.cpp` 257).

**And the exit gate is variable 40.** AREA 230's own record 2 — the script on
the exit zone — is:

```
1970  push.i8 0 ; push.var 40 ; cmp.eq ; jmp_if_false 7
1979  area.goto 231, 64, 65      ; 'Anekbah Supermarché 2 Sas'
```

so leaving the supermarket only works if `'1-A Shoot Sup En Cours'` is 0, which
is what 17165 writes. Symptom 3 is therefore either that write not happening or
the `area.goto` after it not completing.

## 2. What the port does reach

From the headless log, the branch runs a long way: the Meditek voice-over plays
(`media.play 251`), editing 7 `med2` takes the camera at 750 for its 366
frames, the player's `scx.play.player.wait` program ends at 1116, and
`camera.set 0, 0, 2` lands at 1117 (`follow camera 0`). So 17201 executed,
which means every `set.var` above it did too.

**`scene.unload 230` DOES run, and it is not the fault.** Instrumented, it
prints `scene.unload 230 (caller 2): slot0 area 230 scene 56` at frame 1117, so
the slot matches and the unload proceeds. The resident `.SCX` staying
`shoot.SCX` is *correct*: `Session::reloadScene` resolves the stem from the
AREA chunk's `+97` whichever kind is asked for, and AREA 230's is `shoot.SCX`
already, so there is nothing to swap.

**What leaves actor 65 standing there is the `byLone` fallback**
(`engine/backends/sdl/play.cpp` 11053):

```cpp
if (prog < 0 && loneProgs == 1 && staged.size() == 1) {
    prog = loneProg; run = &sc; byLone = true;   // LABELLED reconstruction
}
```

Its own comment claims it "can never mis-assign once there is more than one -
which is the case the issue is about". After the death branch there IS only
one: every other body has been hidden by the branch's 25 `character.hide`s, and
actor 65 is the only one left. So the frame's one running program — clip 18
`BOIPATH2.3DA`, which names somebody else — is handed to him, teleporting him
from 13283 -92 996 to **13046 -140 1149** and posing him from a stranger's
clip. The log says so in as many words:

```
frame 1117: pose: actor 65 V5H_FNM - clip 18 'BOIPATH2.3DA' (105 frames), no path:
            snapped to its root key 0 at 13046 -140 1149
            [the program names another actor; this is the frame's only body - LABELLED]
```

and he carries **`bank none`**, so the moment that clip runs out he has no idle
to fall back to and draws in the model's REST pose — which is the T pose in the
reader's screenshot, at a position no script ever asked for.

The fallback was built for AREA 118's arrival, on the reading that "the shown
list carries Kay'l as 310 and the program names another id". **That reading
does not survive being checked**: the beat is `character.show 310, 1` then
`scx.play.actor.wait 310, 1`, so the program names the very body it stages and
`drivenBy` matches it with no fallback at all - `engine: intro beat` and
`engine: intro` are both green with the fallback narrowed to the player.

Narrowed to the player it is, since `scx.play.player` names no actor and an id
mismatch there is at least plausible. The proper answer is still to read how
`ScriptObject_StartOnActor` / `Actor_FindById` resolve a CHARACTERS id to an
actor slot, and stop guessing entirely.

## 3. The music

Nothing in the loss branch plays a second track, and `Shoot_Leave`
(0x00422730) does not touch the music either — its `sub_43BB40` is an
`mciSendCommandA(MCI_STOP)` on the **MCI** device, and the music is
`Music_PlayTrack` streaming `TRACKS\<n>.ADP` (0x0041E110), a different path.
What changes the track in the original is the AREA SWAP: the transition
handler's case 9 reads the incoming chunk's `+142` and calls
`Music_SetFadeMode(2); Music_PlayTrack(+142, 1); Music_SetFadeMode(0)`
(`readable/src/01_file.c` 1345).

So the phase's looping track 91 is *meant* to keep playing until you leave the
area — and symptom 1 is most likely downstream of symptom 3: the reader never
gets out, so the track never changes. Do not "fix" the music directly until
the exit works.

## 4. Steps

1. ~~**The `byLone` fallback** (symptom 2)~~ - **DONE 2026-09-12.** Narrowed to
   the PLAYER, which is the AREA 118 case it was written for. Measured on the
   same 1400-frame death route: the "the program names another actor" line is
   gone, and actor 65 ends at **13283 -92 996** - where his own program left
   him at frame 182 - instead of 13046 -140 1149, holding the last pose that
   program gave him rather than a stranger's clip.
2. ~~**The exit** (symptom 3)~~ - **A HARNESS ARTEFACT, 2026-09-13.** The exit
   works. Walking into AREA 230's exit zone 3903 runs `area.goto 231` (doors
   64/65) and the airlock loads. What stalled was AREA 231's **record 1**, which
   is the way IN, not out:

   ```
   544 player.move 100 / 547 player.anim.hold / 551 area.goto 230, -1, -1
   558 zone.disable 3949 / 564 fade.to_black / camera 4378 / fade.to_color
   camera.set.wait 4379 / camera 4380 / scx.play.wait obj 5, obj 6
   615 actor.goto_address 665 'Depart Supermarche 2' / 618 area.arrive 231
   ```

   It never releases the hold - every `player.anim.release` near it is in
   SCENE 56 or 62, the phase that follows - and its zone 3949 sits beside 3903
   (x 12943 against 13061), so walking out crosses it at once. It disables
   ITSELF, and a zone's enabled state is a DB bit (`StateArray::ZoneState`),
   so on the real way in it fires once and stays off. `--area 230
   --scene-chunk 56` starts inside and never runs it; 3949 keeps the bit from
   `save-appart.bin`, made before the supermarket, and walking out after the
   death replays that cutscene with no scene left to release it: the reader's
   "cutscene mode, fade out then nothing". Measured in the post-death state
   (area 230 with no scene, 3903 enabled as the loss branch leaves it): 448 of
   500 frames under a hold never released. With `--zone-disable 3949` added -
   a new harness, the exact mirror of `--zone-enable`, doing what record 1
   does - the same walk goes through the doors into the airlock with **0
   frames under a hold**. The handoff's recipe now carries the flag.

   **And all the way out.** Walked on through the airlock and turned into its
   zone 3948 (record 0), the same post-death state reaches the city: `area.goto
   0` with doors 3/4 at frame 163, Anekbah shown at 180, two areas entered, still
   0 frames under a hold. (Walking in adventure mode turns with the ARROW keys:
   `k203*15` from heading 270 brought him to 313, onto the zone.)
3. ~~**The music** (symptom 1)~~ - **downstream of 2.** Track 91 is AREA 230's
   OWN music (`+142`), which the scene's `music.play 91, 1, 1` also names, so
   staying in the supermarket after a death it plays on in the engine too.
   Leaving changes it: the port's swap arm matches the engine's (`+142` of the
   incoming active slot, only when it differs), and on the working exit the
   track switches to the airlock's 18 the frame his feet land there. One gap
   left unexplained and off the real path: on the artefact path, record 1
   carrying him back into 230 logged no switch back to 91.
4. **The T-posed character** (symptom 2) - fixed in the port by step 1
   (`da000d3`), NOT yet confirmed by eye. What the reader saw was actor 65 at
   ground level half inside a pillar, which is where the lone-program fallback
   teleported him. Note what a plain area-230 start ALSO shows and is a
   different thing: actors 65, 88 and 60 staged in the rest pose from frame 0,
   shown by the save's DB bits with no bank and no program - but at y -1662,
   far above the supermarket floor.

Each step ends in a `--only` run over the shoot checks and a commit.

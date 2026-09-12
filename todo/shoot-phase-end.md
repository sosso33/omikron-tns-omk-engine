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

The fallback was built for AREA 118's arrival, where "the shown list carries
Kay'l as 310 and the program names another id". That case is the PLAYER; this
one is not. Narrowing it to the player is the obvious first move, but the
proper answer is to read how `ScriptObject_StartOnActor` / `Actor_FindById`
resolve a CHARACTERS id to an actor slot and stop guessing at all.

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

1. **The `byLone` fallback** (symptom 2). Narrow it so it cannot hand a
   program to a body the program does not name - the AREA 118 case it exists
   for is the PLAYER - and prove the narrowing with both routes: the intro
   arrival must keep its pose, and the supermarket death must leave actor 65
   where his own program left him.
2. Walk the player to the exit zone after a death and watch `area.goto 231`.
   If it fades and stalls, that is symptom 3 on its own and has nothing to do
   with the variables.
3. Only then re-judge the music.

Each step ends in a `--only` run over the shoot checks and a commit.

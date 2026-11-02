# Lesson 10 — The traps

**Goal:** meet three of this repository's classic mistakes on purpose, so you
recognise them by their SYMPTOM - which is never the error message you would
expect, because in each of them every command succeeds.

Each one cost real time here, and each is in `CLAUDE.md` §1 as a one-line
trap, and in `docs/PRACTICE.md` §1 or §5 with the story. What they share: **a command exits 0 and did not do what you think.**
The defence is the same every time - assert the effect, not the exit status.

## Read

* `docs/PRACTICE.md` §1, "Known extraction traps": the bullet "A mutation
  that does not apply passes, and a stale object file passes", with `make`'s
  whole second. `grep -n` it - never read the file whole.
* `docs/PRACTICE.md` §5, the box that begins "Passing arguments from a shell
  variable: this is zsh".

## Trap 1 — the flags that were one argument

```
A="--frames 5 --nofmv"
printf '[%s]\n' $A
printf '[%s]\n' ${=A}
```

```
[--frames 5 --nofmv]
[--frames]
[5]
[--nofmv]
```

zsh does not split an unquoted variable into words; bash does. Handed `$A`,
`omk-play` receives ONE argument, `--frames 5 --nofmv`, matches no flag, and
ignores it - so the run has no frame limit, plays the films, and you read
it as "slow". It happened four times in one day: a comparison of three
renders that were all the default camera, a render that waited fourteen
minutes, a function that passed one argument for all its flags.

**The symptom:** a run that ignores what you asked, and three outputs that
are identical when they should differ. **The defence:** `${=A}`, or write
the flags out, and check the effect - a file written, a line printed.

## Trap 2 — the restore `make` did not see

Make a probe print something else, build, put it back and build again - all
in the same second, the way a script does it:

```
cd engine
build/vlight_probe | head -1
sed -i '' 's/l.colour = 0x00F4B168;/l.colour = 0x00FFFFFF;/' tools/vlight_probe.cpp && \
    make -s build/vlight_probe && \
    sed -i '' 's/l.colour = 0x00FFFFFF;/l.colour = 0x00F4B168;/' tools/vlight_probe.cpp && \
    make -s build/vlight_probe
build/vlight_probe | head -1
git diff --stat tools/vlight_probe.cpp
ls -l -T tools/vlight_probe.cpp build/obj/tools/vlight_probe.o
```

```
facing 172 125 73 1
facing 180 180 180 1
13 oct. 15:50:39 2026 build/obj/tools/vlight_probe.o
13 oct. 15:50:39 2026 tools/vlight_probe.cpp
```

The source is back - `git diff` is empty - and the probe still prints the
MUTATED light, white instead of orange. macOS ships GNU Make 3.81, which
compares modification times to the whole second: the restored source is not
NEWER than the object file built from the mutation, so `make` has nothing to
do. Every check run after this measures code that is no longer in any file.

```
touch tools/vlight_probe.cpp && make -s build/vlight_probe && build/vlight_probe | head -1
```

```
facing 172 125 73 1
```

**The symptom:** a check that keeps reproducing a mutation's exact numbers
after you restored it. **The defence:** after every restore, `touch` and
rebuild before running anything.

## Trap 3 — the mutation that never applied

Try to lower the walker's step limit (lesson 7) with a quick `sed`:

```
sed -i '' 's/kStepUp = 11.811023622/kStepUp = 7.874015748/' src/actor/walk.h
echo "exit $?"
git diff --stat src/actor/walk.h
```

```
exit 0
```

`sed` succeeded, and `git diff` prints nothing: the file has THREE spaces
after `kStepUp` and the pattern has one. Run `engine: walker falls` now and
it is green - and a green check after a mutation reads as "the check cannot
see this", which is a finding about the CHECK, recorded, and wrong. Four
mutations "passed" in one day this way: two anchors that did not match, an
`xargs rm` that deleted the wrong file so the old object was relinked, and a
probe too weak to see the difference.

**The symptom:** a mutation that changes nothing, everywhere. **The
defence:** before running the check, assert the file changed (`git diff
--stat` names it), and after, that the run's OUTPUT changed - not only that
the check went red.

## Change

Nothing to keep. Put `walk.h` and `vlight_probe.cpp` back as they were
(`git diff --stat` should print nothing), `touch` both, and `make`.

## Prove

The three traps have one thing in common, and it is the habit this lesson is
for: after a command that is meant to change something, look at the change.
Before you go on to the capstone, run lesson 2's mutation once more - the
run length `+ 3` to `+ 2` in `decodeImage` - and this time check the edit
applied, run `engine: 3DT`, restore by editing, `touch`, rebuild, and run it
green, without a single step taken on trust.

## Traps

The rest of the catalogue, by symptom:

| you see | it was |
|---|---|
| `grep` finds nothing in a log you can read | the log carries the game's accented text; without `-a` grep calls it binary and matches nothing |
| a check passes that a mutation should break | the check ran a binary it did not build - name every binary in its `make` |
| a check of a log line passes when the consumer is cut off | the line was printed where the value was handed over, not from what the consumer produced |
| a struct's new field holds another field's value | a positional initialiser of the struct, shifted by one, compiled without a word |
| a rename made the action button dead | a name in `tools/renames.json` had the sense backwards; the instructions said otherwise |
| "the line is not committed" for every commit | zsh read `$c:engine/...` as a modifier; write `${c}:path` |
| a GPU dump that agrees with the last four | the display slept, every run blocked at frame 0, and each was killed after writing frame one |

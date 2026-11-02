# Lesson 3 — Reading the original

**Goal:** find a function of the original engine three ways - by name in the
hand-cleaned decompilation, by address in the listing, and in the executable
itself - know which to trust when they disagree, and find the function's port.

This lesson needs the listing (`Runtime.exe.asm` and `Runtime.exe.c`, or
`omk.conf`'s `asm =` / `decomp =`). `python3 tools/omkpaths.py` says whether
you have it. Without it, read along: every output below is printed.

## Read

* `CLAUDE.md` §1, from the top to the end of "Make the parse self-checking".
  Then, in its long form `docs/PRACTICE.md` §1, the bullet "And the same trap
  is not confined to the VM" under "Known extraction traps" (`grep -n` it;
  `CLAUDE.md` keeps it as the one-line trap "A function with no direct `E8`
  caller gets no `proc` label"). This lesson is that bullet, met for real.
* `CLAUDE.md` §3, the four `@status` values.
* `readable/INDEX.md`: just look at its shape - one row a function.

## The three sources

| source | what it is | trust it for |
|---|---|---|
| `readable/src/*.c` | the Hex-Rays decompilation, cleaned by hand one function at a time; each function opens with a banner `/* @func 0x... Name @status ... */` | reading what a function does - once its status says someone has read it |
| `Runtime.exe.asm`, through `tools/asmfn.py` | IDA's listing, which carries no addresses on its lines - only labels: `sub_XXXXXX proc` and `loc_XXXXXX:` | the instructions, exactly - IF the block is the function you asked for |
| `gamedata/Runtime 2.exe`, through `objdump` | the executable itself | the last word: what is at an address is what is at the address |

## Run

### 1. By name: the interface's input dispatch

Every key press on a screen goes through one function. Find it:

```
grep -n 'Ui_DispatchInput' readable/INDEX.md
grep -n '@func 0x0042A430' readable/src/06_sys.c
```

```
| `0x0042A430` | `Ui_DispatchInput` | named | 06_sys.c | 1 | Dispatch one frame of input down the widget tree. |
768:/* @func 0x0042A430  Ui_DispatchInput  @status NAMED  @lines 100  @callers 1 */
```

Read its banner comment and body (`sed -n 768,870p readable/src/06_sys.c`).
`NAMED` means it was read and named from evidence, its body left as the
decompiler wrote it. The comment says what the input bits mean on a screen:
`0x10` confirm, `0x20` back, `0x2000` close. And in the body, the close is
one store: `screen+8 = 3`. `docs/UI.md` §3b says what that word is - the
screen's state, and 3 is *closing*.

### 2. By address: the button that closes

The terminals, the shops, the save screen and the high-score board share one
"close" button, whose callback the widget table (`tables/ui_widgets.json`)
gives as `0x0042A990`. Look for it in the index:

```
grep -n '0042A990' readable/INDEX.md
```

Nothing. **No function at that address exists in the decompilation at all**:
IDA makes a function where it sees a call, and nothing in the engine *calls*
this one - its address is only stored, as data, in the widget table. So ask
the listing:

```
python3 tools/asmfn.py 0x0042A990
```

```
mov     eax, dword_4E9720
test    eax, eax
jz      short loc_42A9CE
mov     eax, [esp+4]
mov     dword_4E9720, 0
push    eax
push    1F4h
...
loc_42A9CE:
mov     ecx, [esp+4]
...
call    sub_42A750
add     esp, 8
retn
```

Thirty lines, a sound call, a second argument - not a "close" button. **This
is not the function at `0x42A990`.** The listing has no label at that
address, so `asmfn.py` anchored on the first label after it, `loc_42A9CE`,
walked back to the `align` before it and printed THAT function, without a
word. Count the bytes from its first instruction to `loc_42A9CE` and you get
46: it starts at `0x42A9A0`, sixteen bytes past the address you asked for.

### 3. By the executable: what is really there

```
objdump -d --no-show-raw-insn --x86-asm-syntax=intel \
    --start-address=0x42A990 --stop-address=0x42A9A0 "gamedata/Runtime 2.exe"
```

```
  42a990:      	mov	eax, dword ptr [esp + 0x4]
  42a994:      	mov	dword ptr [eax + 0x8], 0x3
  42a99b:      	xor	eax, eax
  42a99d:      	ret
```

Four instructions: take the first argument (the screen), write 3 at its `+8`
(the state: *closing*), return 0. That is the whole button.

### 4. Its port

```
grep -rn '0x0042A990' engine/src
```

```
engine/src/ui/widgets.h:1209:    // carrying 0x10, `0x0042A990`'s button). For the TERMINAL that is
engine/src/ui/widgets.h:1701:inline constexpr std::uint32_t kCbSaveAnnuler    = 0x0042A990u;
engine/src/ui/widgets.cpp:1911:        // `Annuler` on the save screen (0x0042A990): it closes.
engine/src/ui/widgets.cpp:1917:            closeNow("close (0x0042A990)");
```

In `UiWalk::confirm`, the arm for that callback logs and calls
`UiWalk::closeNow`, which runs the screen's own close callback like any other
close. Ports keep the original's address next to the code that reproduces
it, so a grep for an address finds the port.

## Change

Watch the button work. The police network's terminal in area 179 is a
keypad of eleven cells, three wide, and the close button is the last: DOWN
from the first cell visits 0, 3, 6, 9, 10. Walk up to it, press ENTER to use
it, wait for its banner, press DOWN four times and ENTER:

```
cd engine
SDL_VIDEODRIVER=dummy build/omk-play ../gamedata ../tables --software \
    --save ../traces/save-appart.bin --area 179 --stand 5468,336,-12118,120 \
    --frames 420 --nofmv --nodelay --no-crowd \
    --hold 'k*40,k28*2,k*300,k208*2,k*4,k208*2,k*4,k208*2,k*4,k208*2,k*4,k28*2,k*40' \
    | grep -a 'cell 10\|answered\|screen closed\|ui answers'
```

```
terminal family: screen 5 cell 10 - the bar says '{fC}Quitter le terminal'
screen 5 answered 0 -> the script resumes (variable 19 now 0)
frame 369: the last screen closed - 1 key(s) held through it ignored until released (the input flush)
session: 0 areas entered, 1 ui answers, 0 zones turned away by the height band
```

(`grep -a`: the log carries the game's own accented text, which `grep`
otherwise treats as binary and silently stops matching.)

Now break the port. In the `kCbSaveAnnuler` arm of `UiWalk::confirm`, delete
the `closeNow(...)` line, keep the `return true;`, rebuild (`make play`) and
run the same command:

```
terminal family: screen 5 cell 10 - the bar says '{fC}Quitter le terminal'
session: 0 areas entered, 0 ui answers, 0 zones turned away by the height band
```

The bar still says *Quitter le terminal* - the walk got to the button - but
nothing answers and nothing closes: the player is stuck at the terminal.

## Prove

Now look for the check that catches it. With the line still deleted:

```
cd ..
python3 tools/verify.py --jobs 6 --only "terminal family" "ui highscore" "engine: shop" \
    "shop open" "engine: save" "engine: dossier" "engine: delayed close" \
    "engine: screen close" "engine: pause" "engine: high score"
```

```
13 checks, 0 failed
```

**None of them does.** Thirteen checks over the terminals, the shops, the
saves and every close path stay green, and so does `tools/refactor_guard.py`,
the fourteen-run regression recorder - none of them presses this button.
That was true of the whole repository until this course was written: a
behaviour every terminal, shop and save screen depends on had no test. The
check that closes the gap is `engine: close button`, and lesson 9 is how it
was written. Run it now, with the line still deleted:

```
python3 tools/verify.py --only "engine: close button"
```

```
engine: close button FAIL (True, ()) != (True, (('5', '0', '0', 'close (0x0042A990)'),))
```

No answer at all, where one was expected from this button.

Put the `closeNow` line back, `touch src/ui/widgets.cpp`, `make play`, and
run the terminal once more to see it close.

## Traps

* **A block can be the wrong function, silently.** Any address with no
  label in the listing - and that is most of the interface's callbacks,
  because they are only ever stored in tables, never called - gets the NEXT
  function from `asmfn.py`. When the data names an address the decompilation
  does not have, go to the executable.
* **A name in `tools/renames.json` is a hypothesis.** `Perso_SetInputEnabled`
  reads as an enable and is a BLOCK: its argument 1 stops the actor reading
  keys. A port that trusted the name killed the action button after every
  conversation. Before a name decides a behaviour, read the instructions.
* **A negative search is only as good as what it searched.** "Nothing calls
  `0x42A990`" was true of direct calls and said nothing about tables. "No
  script starts the cutscene" stayed true for weeks because the enumeration
  of scripts missed a whole field. Say what was enumerated along with what
  was not found.

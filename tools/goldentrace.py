#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
r"""Golden traces from the original, through CrossOver - the behavioural oracle.

`verify.py` says whether a format is READ correctly. Nothing in this repo could
say whether the simulator DECIDES what the game decides. This is that: run the
real binary, capture what it chose, diff it against `tools/sim`.

    python3 tools/goldentrace.py bootstrap        # NEW MACHINE: build it all
    python3 tools/goldentrace.py setup            # inspect the rig
    python3 tools/goldentrace.py run --seconds 90 # play; capture a trace
    python3 tools/goldentrace.py run --keys return,down,return  # scripted
    python3 tools/goldentrace.py capture          # the engine's own FRAMEBUFFER
    python3 tools/goldentrace.py grab --tag dlg402   # ONE frame, game already running
    python3 tools/goldentrace.py config           # the setup dialog: pick the DRIVER
    python3 tools/goldentrace.py parse LOG        # -> the decision stream
    python3 tools/goldentrace.py attribute LOG    # -> which scripts ran
    python3 tools/goldentrace.py diff  LOG        # against tools/sim/vm.py
    python3 tools/goldentrace.py diff  LOG --evolve          # carry state
    python3 tools/goldentrace.py diff  LOG --save GAMES --slot 3

WHY THIS NEEDS NO SHIM AND NO PATCHING
--------------------------------------
Every VM opcode handler logs its operand, and it does so through the Win32
profile API:

    Dbg_LogTagged(value, "ZONES")
      -> sprintf(file, "IAM\\%s.TAG", section)
      -> GetPrivateProfileStringA(section, "3732", ..., file)

Read the handlers: that call is guarded by ONE thing, `g_ScriptDryRun` - so it
fires on real execution and not on the evaluate pass - and it happens BEFORE the
`if (hWnd)` that would draw the text in the engine's debug listbox. The window
never has to exist. The engine therefore announces every executed instruction's
operand, with its domain, as an ordinary Windows API call, and Wine can log
those. Decision-level trace, binary untouched.

It is also the right level to diff at. The original computes on the x87 stack;
any replica drifts in the low bits, so comparing numbers would drown a real
disagreement in noise. Opcode ids, operand integers and .TAG section names are
integers end to end.

WHICH BINARY
------------
`gamedata/Runtime 2.exe`, NOT `gamedata/Runtime.exe`. The listing in this repo is named for
the latter and is of the former: `Runtime 2.exe` puts .text at 0x401000-0x4BC000
and .data at 0x4C0000, which is where every address here lives, and its VM
dispatch table matches `clean/_vmsummary.json` in all 152 populated entries.
`verify.py: binary identity` pins it, so this cannot drift.

WHAT THE RIG ACTUALLY NEEDED (all of it found the hard way)
-----------------------------------------------------------
* CrossOver runs 32-bit x86 on arm64; Rosetta 2 is x86-64 only, so plain Wine
  cannot host this game on this machine at all.
* CrossOver's `bin/wine` ignores `WINEPREFIX`. It wants `--bottle NAME`, and
  `cxstart --bottle NAME --` is the supported launcher.
* It also ignores `WINEDEBUG`. The channel variable is `CX_DEBUGMSG` and the
  destination is `CX_LOG`. With `WINEDEBUG` the log is silently empty, which
  looks exactly like "the game executed no scripts".
* Unfiltered `+relay` costs 111,283 calls to run `cmd /c ver`. `RelayInclude`
  (semicolon-separated, module name UPPERCASE) cuts that to 110 and makes the
  game playable while recording.
* CrossOver's own licence check calls `GetPrivateProfileStringA` too. The parser
  keys on the filename argument, so only `IAM\*.TAG` reads count.

THE CD CHECK
------------
The engine probes for the disc by `fopen(".\\1")`, `".\\2"`, `".\\3"` -
relative to the WORKING DIRECTORY, not to any drive: the `%s` in its
`"%s.\\%ld"` is an empty buffer (IDA leaves it as the uninitialised
`unk_5C3A4D`). It wants markers named `1`, `2`, `3`; `gamedata/` ships exactly those
markers named `CD1`, `CD2`, `CD3`. Without them it loops on `IMAGES\CD0.BMP`
- "insert any CD", CD 0 meaning any - and never starts.

So `run` launches with its working directory set to a RUN ROOT in the bottle:
symlinks to every entry of `gamedata/` plus the three markers. `gamedata/` stays read-only
input and the engine is not patched - it simply finds what it is looking for.
The bottle also gets a D: drive of type DRIVE_CDROM, because two other probes
in the same family scan for DRIVE_FIXED and DRIVE_CDROM; only the CWD-relative
one was observed to fire, so that drive is belt-and-braces rather than a
requirement.

WHAT IS INVISIBLE
-----------------
Read `Dbg_LogTagged` (0x0040EC70, 02_file.c) and it filters three things out
before it ever reaches the profile API:

    a1 == -1        returns immediately - nothing at all is logged
    "CHARACTERS"    resolved through Actor_FindById, printed from the actor
    "VALUES"        a bare number, sprintf("%d") - there is no table to read

So a capture is evidence about what DOES log and silence about the rest: never
read a missing opcode as proof it did not run. `sim_stream` applies the same
three rules to the simulator, because a diff between a filtered stream and an
unfiltered one would report the filter as a disagreement.

The other side of the mechanism showed up in the first real capture: every
operand line returns to `ret=0040ed42`, which is inside `Dbg_LogTagged`. The
call site is the logger, confirmed from the running process rather than only
from the listing.
"""
import omkpaths
import os, re, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

# --------------------------------------------------------------- the rig
# Nothing here is copied between machines and nothing licensed is vendored.
# CrossOver is ~1 GB of commercial software whose licence lives outside the
# app, and a bottle bakes absolute paths into its dosdevices links, so a copied
# bottle breaks on a different machine or username. The bottle is therefore a
# BUILD ARTIFACT: `bootstrap` rebuilds it, the shim and the run root from
# nothing in about two minutes, and everything that is genuinely this project's
# - the tool, `gamedata/`, the traces - is already in the repo.
CX_CANDIDATES = [
    "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver",
    os.path.expanduser("~/Applications/CrossOver.app/Contents/SharedSupport/CrossOver"),
    "/Applications/CrossOver/CrossOver.app/Contents/SharedSupport/CrossOver",
]


def cx_root():
    """Where CrossOver is on THIS machine. $OMK_CX overrides."""
    env = os.environ.get("OMK_CX")
    for c in ([env] if env else []) + CX_CANDIDATES:
        if c and os.path.exists(os.path.join(c, "bin", "cxstart")):
            return c
    return None


CX      = cx_root() or CX_CANDIDATES[0]
WINE    = os.path.join(CX, "bin", "wine")
CXSTART = os.path.join(CX, "bin", "cxstart")
BOTTLE  = os.environ.get("OMK_BOTTLE", "omikron")
BOTTLE_DIRS = [
    os.path.expanduser("~/Library/Application Support/CrossOver/Bottles"),
    "/Library/Application Support/CrossOver/Bottles",
]
BDIR    = os.path.join(BOTTLE_DIRS[0], BOTTLE)
GAME    = omkpaths.data("Runtime 2.exe")
OUT     = os.path.join(ROOT, "traces")

# Semicolon-separated, module name uppercase, exactly as Wine's load_list wants.
RELAY = "KERNEL32.GetPrivateProfileStringA"


def sh(cmd, **kw):
    # errors="replace": the bottle is French, so `reg` answers in cp1252 and a
    # strict utf-8 decode dies on the accent in "L'operation s'est terminee".
    return subprocess.run(cmd, capture_output=True, text=True,
                          errors="replace", **kw)


# ------------------------------------------------------------------- setup
def setup(create=False):
    ok = True
    print("CrossOver")
    if not os.path.exists(CXSTART):
        print("  MISSING %s" % CXSTART); return 1
    v = sh([WINE, "--version"]).stdout.strip().splitlines()
    print("  " + " / ".join(x.strip() for x in v[:3]))

    print("the binary")
    if not os.path.exists(GAME):
        print("  MISSING %s" % GAME); ok = False
    else:
        print("  %s  (%d bytes) - the one the decompilation describes"
              % (os.path.relpath(GAME, ROOT), os.path.getsize(GAME)))

    print("bottle %r" % BOTTLE)
    if not os.path.isdir(BDIR):
        if create:
            print("  creating (winxp template, 32-bit)...")
            sh([os.path.join(CX, "bin", "cxbottle"), "--bottle", BOTTLE,
                "--create", "--template", "winxp"])
        if not os.path.isdir(BDIR):
            print("  MISSING. create it with:")
            print("      python3 tools/goldentrace.py setup --create")
            ok = False
    if os.path.isdir(BDIR):
        r = sh([CXSTART, "--bottle", BOTTLE, "--", "cmd", "/c", "ver"])
        ver = [l.strip() for l in r.stdout.splitlines() if "Windows" in l]
        print("  ok - 32-bit runs: %s" % (ver[0] if ver else "?"))
        arm(); print("  RelayInclude = %s" % RELAY)
        print("  run root -> %s (gamedata/ by symlink + markers 1 2 3)"
              % os.path.relpath(install_cd(), BDIR))
        p = install_shim()
        print("  PATCH.dll -> %s" % ("forwarder installed in the bottle's "
              "system32 (%d bytes)" % os.path.getsize(p) if p else "NOT INSTALLED"))

    os.makedirs(OUT, exist_ok=True)
    print("output\n  %s" % os.path.relpath(OUT, ROOT))
    print("\n" + ("ready" if ok else "not ready - see above"))
    return 0 if ok else 1


def distil(path):
    r"""Keep only the lines that carry signal.

    Relay logging writes continuously for as long as the process lives, and a
    session that hangs rather than exits will fill a disk: one crashed capture
    here reached 908 MB holding 58 events. Everything this rig reads is a
    GetPrivateProfileStringA line, so the rest is dropped as soon as the run
    ends - losing nothing that `parse` or `attribute` could have used.
    """
    if not os.path.exists(path): return 0
    keep = []
    with open(path, errors="replace") as fh:
        for line in fh:
            if "GetPrivateProfileStringA" in line: keep.append(line)
    with open(path, "w") as fh:
        fh.writelines(keep)
    return len(keep)


def bootstrap(smoke=25):
    r"""Provision the whole rig on a machine that has none of it.

    One command, from a clean checkout to a capture: find CrossOver, build the
    bottle, arm the relay filter, generate the PATCH.dll forwarder, build the
    run root, then actually LAUNCH the game and require operand events to come
    out. The last step is the point - every earlier failure in this rig
    (WINEDEBUG ignored, a stale session, a missing CD marker) produced an empty
    log, which is indistinguishable from "the game ran no scripts" unless
    something asserts otherwise.
    """
    cx = cx_root()
    print("1. CrossOver")
    if not cx:
        print("   NOT FOUND. Install it, or point $OMK_CX at")
        print("   <CrossOver.app>/Contents/SharedSupport/CrossOver")
        return 1
    print("   %s" % cx)

    print("2. the game data")
    if not os.path.exists(GAME):
        print("   MISSING %s - `gamedata/` must be present" % GAME); return 1
    print("   %s" % os.path.relpath(GAME, ROOT))

    print("3. bottle %r" % BOTTLE)
    if os.path.isdir(BDIR):
        print("   already there")
    else:
        print("   creating from the winxp template (about two minutes)...")
        sh([os.path.join(cx, "bin", "cxbottle"), "--bottle", BOTTLE,
            "--create", "--template", "winxp"])
        if not os.path.isdir(BDIR):
            print("   FAILED to create"); return 1
        print("   created")

    print("4. relay filter, shim, run root")
    arm()
    p = install_shim()
    root = install_cd()
    print("   RelayInclude = %s" % RELAY)
    print("   PATCH.dll    = %s" % (p or "FAILED"))
    print("   run root     = %s" % root)

    if not smoke:
        print("\nready (skipped the smoke test)"); return 0
    print("5. smoke test - launching for %ds and requiring operand events" % smoke)
    path = os.path.join(OUT, "bootstrap.log")
    run(smoke, tag="bootstrap")
    n = len(parse(path))
    print("   %d operand events" % n)
    if not n:
        print("\nNOT READY: the game launched but logged nothing. Check, in "
              "order:\n   a stale session (kill_session), the CD markers in "
              "the run root,\n   and that CX_DEBUGMSG - not WINEDEBUG - "
              "reaches the process.")
        return 1
    print("\nready - %d events captured, the rig works end to end" % n)
    return 0


def kill_session():
    r"""End any session still running in the bottle, and wait for it.

    Necessary, not tidiness: `cxstart` hands a second launch to the wineserver
    that is already up and RETURNS IMMEDIATELY, so a stale session makes the
    next capture exit at once with an empty log - which reads exactly like a
    game that ran no scripts. Note wineserver takes WINEPREFIX, not --bottle;
    run it without and it kills the wrong prefix and reports success.
    """
    sh([os.path.join(CX, "bin", "wineserver"), "-k"],
       env=dict(os.environ, WINEPREFIX=BDIR))
    for _ in range(20):
        r = sh(["pgrep", "-f", "desktop=omk|Runtime 2.exe"])
        if r.returncode != 0: return True
        time.sleep(0.5)
    return False


def arm():
    """Point Wine's relay filter at the one call the operand logger makes."""
    sh([WINE, "--bottle", BOTTLE, "reg", "add", r"HKCU\Software\Wine\Debug",
        "/v", "RelayInclude", "/d", RELAY, "/f"])


# -------------------------------------------------------------------- the shim
# `Runtime 2.exe` imports DirectDrawCreate and DirectDrawEnumerateA from
# **PATCH.dll** - and nothing else from it. That is the 2020 re-release's ddraw
# wrapper under a private name, and it is not in `gamedata/`, so the loader stops with
# c0000135 (DLL_NOT_FOUND) before the engine runs a single instruction.
#
# The answer is a PE that forwards those two names to Wine's own ddraw. A
# forwarder is pure data - an export table whose address entries point at the
# strings "ddraw.DirectDrawCreate" inside the export directory - so this file
# contains no code at all, patches nothing, and cannot change what the engine
# decides. It is written into the BOTTLE's system32, never into `gamedata/`, which
# this repo treats as read-only input.
FORWARDS = [("DirectDrawCreate",     "ddraw.DirectDrawCreate"),
            ("DirectDrawEnumerateA", "ddraw.DirectDrawEnumerateA")]


def forwarder_dll(dll_name="PATCH.dll", forwards=FORWARDS):
    """Build a minimal PE32 DLL that forwards each export elsewhere."""
    forwards = sorted(forwards)                 # the name table must be sorted
    RVA, FA, SA = 0x1000, 0x200, 0x1000
    n = len(forwards)
    blob, off = bytearray(), 40 + n * 4 + n * 4 + n * 2

    def put(sv):
        nonlocal off
        r = off
        blob.extend(sv.encode() + b"\0")
        off += len(sv) + 1
        return RVA + r

    body = bytearray(off)                        # dir + the three tables
    dll_rva = put(dll_name)
    fwd = [put(t) for _nm, t in forwards]        # forwarder strings
    nms = [put(nm) for nm, _t in forwards]       # export names
    body.extend(blob)
    struct.pack_into("<10I", body, 0, 0, 0, 0, dll_rva, 1, n, n,
                     RVA + 40, RVA + 40 + n * 4, RVA + 40 + n * 8)
    for i, r in enumerate(fwd):  struct.pack_into("<I", body, 40 + 4 * i, r)
    for i, r in enumerate(nms):  struct.pack_into("<I", body, 40 + n * 4 + 4 * i, r)
    for i in range(n):           struct.pack_into("<H", body, 40 + n * 8 + 2 * i, i)

    raw = bytes(body) + b"\0" * (-len(body) % FA)
    hdr = bytearray(FA)
    hdr[0:2] = b"MZ"; struct.pack_into("<I", hdr, 0x3c, 0x80)
    pe = 0x80
    hdr[pe:pe + 4] = b"PE\0\0"
    # machine i386, 1 section, optional header 0xE0, DLL|EXE|32BIT
    struct.pack_into("<HHIIIHH", hdr, pe + 4, 0x014c, 1, 0, 0, 0, 0xE0, 0x2102)
    o = pe + 24
    struct.pack_into("<HBBIIIIII", hdr, o, 0x10b, 0, 0, 0, len(raw), 0,
                     0, RVA, RVA)                # no entry point: pure data
    struct.pack_into("<IIIHHHHHHIIIIHH", hdr, o + 28,
                     0x10000000, SA, FA, 4, 0, 0, 0, 4, 0,
                     0, SA + len(raw), FA, 0, 2, 0)
    struct.pack_into("<IIIIII", hdr, o + 72,
                     0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    struct.pack_into("<II", hdr, o + 96, RVA, len(body))   # DataDirectory[0]
    so = pe + 24 + 0xE0
    hdr[so:so + 8] = b".edata\0\0"
    struct.pack_into("<IIIIIIHHI", hdr, so + 8, len(body), RVA, len(raw), FA,
                     0, 0, 0, 0, 0x40000040)
    return bytes(hdr) + raw


# The engine WRITES while it runs - `IAM\GAMES`, the 256-slot save directory
# it creates itself. The run root must therefore not be a flat set of symlinks
# into `gamedata/`: a symlinked directory is written straight through, and this repo
# treats `gamedata/` as read-only input. So `IAM` is rebuilt as a real directory of
# per-file symlinks with the writable entries as real files, and everything
# else - meshes, sounds, scripts, none of which the engine opens for writing -
# stays a cheap directory symlink.
WRITABLE = ("GAMES",)


def install_cd():
    """The run root: `gamedata/` by symlink, with IAM overlaid so writes stay here."""
    root = os.path.join(BDIR, "omk-cd")
    os.makedirs(root, exist_ok=True)
    fr = omkpaths.data_root()
    for f in sorted(os.listdir(fr)):
        link = os.path.join(root, f)
        if f.upper() == "IAM":
            if os.path.islink(link): os.unlink(link)
            os.makedirs(link, exist_ok=True)
            for g in sorted(os.listdir(os.path.join(fr, f))):
                sub, src = os.path.join(link, g), os.path.join(fr, f, g)
                if g.upper() in WRITABLE:
                    # a real file, so the engine's saves land in the bottle
                    if os.path.islink(sub): os.unlink(sub)
                    if not os.path.exists(sub):
                        import shutil
                        if os.path.isfile(src): shutil.copy2(src, sub)
                        else: open(sub, "wb").close()
                elif not os.path.islink(sub) and not os.path.exists(sub):
                    os.symlink(src, sub)
            continue
        if not os.path.islink(link) and not os.path.exists(link):
            os.symlink(os.path.join(fr, f), link)
    for n in ("1", "2", "3"):
        open(os.path.join(root, n), "ab").close()
    d = os.path.join(BDIR, "dosdevices", "d:")
    if not os.path.exists(d):
        os.symlink(root, d)
        sh([WINE, "--bottle", BOTTLE, "reg", "add",
            r"HKLM\Software\Wine\Drives", "/v", "d:", "/d", "cdrom", "/f"])
    return root


def install_shim():
    """Drop the forwarder into the bottle's system32. -> the path, or None."""
    d = os.path.join(BDIR, "drive_c", "windows", "system32")
    if not os.path.isdir(d): return None
    p = os.path.join(d, "PATCH.dll")
    with open(p, "wb") as fh:
        fh.write(forwarder_dll())
    return p


# --------------------------------------------------------------------- run
#: Menu captures need a SCRIPTED input, not a hand-played one: the engine
#: announces nothing for `ui.open` (its .TAG domain is None), so a menu is
#: only ever visible as a SUSPENSION plus a BRANCH, and comparing two
#: branches means the two runs differed in exactly the keys and nothing else.
#:
#: The keys go in through macOS System Events, addressed to the Wine process
#: by its own name - the game shows up as a normal foreground process called
#: `Runtime 2.exe`. Two macOS permissions gate this and BOTH fail in ways that
#: look like the game ignoring input:
#:   Accessibility     -> without it osascript returns error 1002,
#:                        "not allowed to send keystrokes". It is granted to
#:                        the TERMINAL, not to this script.
#:   Screen Recording  -> unrelated to keys, but `screencapture` returns
#:                        "could not create image from display" without it, so
#:                        you cannot look at the menu to see what happened.
#: A key that is dropped and a key that had no effect are indistinguishable in
#: the trace, so a capture that expects NO answer needs a positive control -
#: another capture, same rig, whose keys DO answer.
#:
#: Codes are macOS virtual key codes. The game reads DirectInput scancodes and
#: `key bindings` (verify.py) has the map: group 0 binds 200/208 (up/down
#: arrows) and 28 (DIK_RETURN) to the bits the widget walker calls UP, DOWN
#: and CONFIRM.
MACKEY = {"return": 36, "down": 125, "up": 126, "left": 123, "right": 124,
          "esc": 53, "space": 49}


def send_keys(spec, settle=20, gap=2):
    """Play `spec` into the running game: 'return,down,return' or '36,125,36'.

    Returns the number of keys the OS accepted. Raises nothing - a refusal is
    reported, because a silent one would read as the game ignoring the key.
    """
    codes = []
    for k in [x.strip().lower() for x in spec.split(",") if x.strip()]:
        codes.append(MACKEY.get(k, k))
    def osa(script):
        p = subprocess.run(["osascript", "-e", script],
                           capture_output=True, text=True)
        return p.returncode == 0, (p.stderr or "").strip()
    focus = ('tell application "System Events" to tell process '
             '"Runtime 2.exe" to set frontmost to true')
    time.sleep(settle)
    ok, err = osa(focus)
    if not ok:
        print("   cannot focus the game: %s" % err); return 0
    time.sleep(gap)
    n = 0
    for c in codes:
        # Re-focus before EVERY key. Focusing once is not enough: the OS
        # accepted every key of a run the game then acted on none of, and the
        # next run with the same spec acted on all three. A keystroke goes to
        # whatever is frontmost when it fires, so anything that takes focus
        # mid-sequence silently eats the rest - and a dropped key is
        # indistinguishable from a key with no effect in the trace.
        osa(focus)
        ok, err = osa('tell application "System Events" to key code %s' % c)
        if not ok:
            print("   key %s REFUSED: %s" % (c, err))
            print("   (grant Accessibility to the terminal; error 1002 is that)")
            return n
        n += 1
        time.sleep(gap)
    return n


def run(seconds=90, tag=None, twice=False, desktop="1024x768",
        keys=None):
    r"""Launch the game with the operand logger's API call traced.

    It is a GUI game and what gets traced is what gets EXECUTED, so this starts
    it and records while it is played by hand.

    **`seconds=0` records until the player quits** rather than for a fixed
    time, which is what a targeted capture needs - a fight does not happen on
    a schedule. It waits for the launcher and then polls for the game itself,
    because `cxstart` returns while the game runs on under the wineserver. It runs inside a
    Wine virtual desktop rather than fullscreen, so a capture never takes over
    the screen and can always be stopped.
    """
    if not os.path.isdir(BDIR):
        print("no bottle - run `setup --create` first"); return 1
    os.makedirs(OUT, exist_ok=True)
    arm()
    kill_session()
    logs = []
    for i in range(2 if twice else 1):
        base = tag or time.strftime("trace-%Y%m%d-%H%M%S")
        path = os.path.join(OUT, "%s%s.log" % (base, "-b" if i else ""))
        env = dict(os.environ, CX_DEBUGMSG="+relay", CX_LOG=path)
        cmd = [CXSTART, "--bottle", BOTTLE, "--"]
        if desktop:
            cmd += ["explorer", "/desktop=omk,%s" % desktop]
        cmd += [os.path.join(install_cd(), os.path.basename(GAME))]
        print("run %d -> %s   (%ds; play the game while it records)"
              % (i + 1, os.path.relpath(path, ROOT), seconds))
        p = subprocess.Popen(cmd, cwd=install_cd(), env=env,
                             stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
        if keys:
            import threading
            threading.Thread(target=lambda: print("   %d of %d keys played"
                                                  % (send_keys(keys),
                                                     len(keys.split(",")))),
                             daemon=True).start()
        if seconds <= 0:
            # `seconds=0`: record until the PLAYER quits, which is what a
            # targeted capture needs - a fight does not happen on a schedule.
            #
            # Waiting on `p` is not enough and the docstring below says why:
            # `cxstart` is a launcher and returns while the game runs on under
            # the wineserver. So this waits for the launcher, then polls for
            # the game itself to be gone, and only then falls through to the
            # kill-and-distil below. Nothing is terminated here: the capture
            # ends when the player ends it.
            try: p.wait()
            except KeyboardInterrupt: pass
            print("   launcher exited; waiting for the game to close...")
            while True:
                r = sh(["pgrep", "-f", "desktop=omk|Runtime 2.exe"])
                if r.returncode != 0: break
                time.sleep(2.0)
            print("   the game has quit")
        else:
            try:
                p.wait(timeout=seconds)
            except subprocess.TimeoutExpired:
                p.terminate()
                try: p.wait(timeout=10)
                except subprocess.TimeoutExpired: p.kill()
        # Kill the session BEFORE distilling, and wait for it to be gone.
        # `p.terminate()` ends the launcher, not the wineserver's copy of the
        # game: a survivor keeps its CX_LOG open and goes on appending to the
        # trace AFTER `distil` has rewritten it, so the file grows raw lines
        # again behind the count this printed. That is how three menu captures
        # came to hold gameplay nobody played - `menu-noinput` was distilled at
        # 3 events and later read 39.
        sh([WINE, "--bottle", BOTTLE, "wineboot", "-e"])
        if not kill_session():
            print("   WARNING: a game process survived; the log may still grow")
        distil(path)
        n = len(parse(path)) if os.path.exists(path) else 0
        print("   %s bytes, %d operand events"
              % (os.path.getsize(path) if os.path.exists(path) else 0, n))
        logs.append(path)
    if twice and len(logs) == 2:
        a, b = parse(logs[0]), parse(logs[1])
        print("\ndeterminism: %d vs %d events - %s"
              % (len(a), len(b), "IDENTICAL" if a == b else "THEY DIFFER"))
        if a != b:
            print("  two captures of the same play WILL differ if the play "
                  "differed; only\n  a scripted identical input proves "
                  "anything here.")
    return 0


# ------------------------------------------------------------------- parse
# Call KERNEL32.GetPrivateProfileStringA(0049.. "ZONES",0049.. "3732",
#   0049.. "",0032.., 00000100, 0049.. "IAM\ZONES.TAG") ret=0040ec9e
LINE = re.compile(
    r'Call\s+\w+\.GetPrivateProfileStringA\('
    r'\w+ "([^"]*)",\w+ "([^"]*)",'          # section, key
    r'\w+ "[^"]*",\w+,\w+,\w+ "([^"]*)"')    # default, buf, size, filename


def parse(path, everything=False):
    r"""-> [(section, key)] in execution order, .TAG reads only.

    The filename argument is what separates the engine's operand log from the
    other things in a Wine process that read .ini files - CrossOver's own
    licence check among them, which calls this very function.
    """
    out = []
    if not os.path.exists(path): return out
    with open(path, errors="replace") as fh:
        for line in fh:
            m = LINE.search(line)
            if not m: continue
            sec, key, fn = m.groups()
            if everything or fn.upper().endswith(".TAG"):
                out.append((sec, key))
    return out


# -------------------------------------------------------------------- the sim side
def sim_stream(arch="SCENE", chunk=53, zone=3732):
    r"""The same (domain, operand) stream, from tools/sim.

    `dialog_disasm.SECTION` is the opcode -> domain map, taken from the section
    name each handler hands the logger; `FIELD_SECTION` records the handlers
    that log a field other than the first. Together they say, for any executed
    instruction, what the original would have written - which is exactly what
    the capture holds.
    """
    sys.path.insert(0, os.path.join(HERE, "sim"))
    import dialog_disasm as D
    import run as R
    seen = []
    s = R.Session(arch, chunk)
    INVIS = ("CHARACTERS", "VALUES")
    base = s.make_vm

    def wrap():
        vm = base()
        cls = type(vm)
        class Tapped(cls):
            def step(self, op, raw, code, start, pc):
                sec = D.SECTION.get(op)
                fs = D.FIELD_SECTION.get(op) or {}
                idx = next(iter(fs), 0) if (fs and not sec) else 0
                sec = fs.get(idx, sec)
                if sec and sec not in INVIS and len(raw) >= 2 * (idx + 1):
                    import struct as _s
                    v = _s.unpack_from("<h", raw, 2 * idx)[0]
                    if v != -1:                 # the logger's own first test
                        seen.append((sec, str(v)))
                return cls.step(self, op, raw, code, start, pc)
        return Tapped(self_state(s), s.trace)

    def self_state(sess): return sess.state
    s.make_vm = wrap
    s.enter_zone(zone)
    return seen



# ------------------------------------------------------------- attribution
# A capture is a flat stream: it says WHAT was decided, never by which script.
# But an operand is a literal in the bytecode, so a (domain, value) pair can be
# looked up in the corpus - and most pairs are rare. A pair that only one slot
# in all 5785 can emit NAMES the script that ran, and the ordered list of those
# anchors is a thing this repo has never had: ground truth about what the game
# actually executes, in order.
#
# The other half is as valuable and comes free: an event that NO slot can emit
# came from outside the corpus. That is the exact shape of the two standing
# open questions (106 conversations with no launch path; what starts a scene's
# beats), so the unexplained residue is a result, not noise.

_ANNOUNCE = None


def _announce():
    """{op: (domain, field)} from tables/vm_announce.json - the same file the
    C++ port reads, so neither side can drift from the other."""
    global _ANNOUNCE
    if _ANNOUNCE is None:
        import json
        with open(os.path.join(ROOT, "tables", "vm_announce.json")) as fh:
            _ANNOUNCE = {r["op"]: (r["domain"], r["field"])
                         for r in json.load(fh)["rows"]}
    return _ANNOUNCE


def loggable(op, raw, every=False):
    r"""What `Dbg_LogTagged` would be handed for this instruction, filtered by
    its own three rules (see the header): no -1, no CHARACTERS, no VALUES.

    `every` widens it to every domain any field of the instruction indexes.
    Attribution wants that superset - a missed pair loses a match - while the
    per-script diff wants the single predicted pair. Where the two disagree the
    CAPTURE is the authority: it shows which field the handler really logs.
    """
    import dialog_disasm as D
    out, seen = [], set()

    def add(sec, idx):
        if not sec or sec in ("CHARACTERS", "VALUES"): return
        if len(raw) < 2 * (idx + 1): return
        v = struct.unpack_from("<h", raw, 2 * idx)[0]
        if v == -1: return                      # the logger's own first test
        if (sec, str(v)) not in seen:
            seen.add((sec, str(v))); out.append((sec, str(v)))

    sec = D.SECTION.get(op)
    fs = D.FIELD_SECTION.get(op) or {}
    if not every:
        # The TIGHT stream is what a handler really hands the logger, and the
        # authority on that is the ASSEMBLY - `tables/vm_announce.json`, which
        # `tools/vm_announce.py` derives from it. Reading it off `D.SECTION` /
        # `D.FIELD_SECTION` over-includes two ways, and over-inclusion here is
        # the dangerous direction (see `indexes()`): six opcodes have a field
        # map but no section, so they announce NOTHING and were contributing
        # anyway, and op 152's section is `JINGOFF2.ADP` - a filename, picked
        # up from the unbounded handler block CLAUDE.md 1 warns about.
        # Measured: 99 extra pairs in the index and TWO false mismatches on
        # `resto-387.log` (8 -> 6). The wide stream keeps the superset, where
        # over-inclusion is the safe error.
        row = _announce().get(op)
        if not row: return out
        add(row[0], row[1])
        return out
    if sec:
        # WHICH field carries the announced domain is not always field 0.
        # op 71 `scene.load` is the case that proves it: its section is SCENES
        # but field 0 is the AREA and field 1 the scene, and the handler pushes
        # the second (`push offset aScenes / push esi`). Where the field map
        # names the same domain, it is the authority on the index.
        idx = next((i for i, t in sorted(fs.items()) if t == sec), 0)
        add(sec, idx)
    for i, t in fs.items():
        if every or not sec: add(t, i)
    return out


def signatures(every=True):
    r"""Every executable slot -> the pairs it could log, in bytecode order.

    Static decode, so a slot's signature is the union over its branches rather
    than one path. That is the right side to err on for attribution: a superset
    can only add candidates, never lose the true one.
    """
    sys.path.insert(0, os.path.join(HERE, "sim"))
    import dialog_disasm as D
    import vm as V
    sigs = {}
    for arch, chunk, rec, field, code, p in V.world_scripts():
        ins, _st = D.disasm(code, p, len(code))
        pairs = [x for _pc, op, raw in ins for x in loggable(op, raw, every)]
        # the OFFSET is part of the key. A chunk's zone records and its
        # message-subscription table are walked into the same (rec, field)
        # space, so `AREA 222 rec 1 +0` names two different scripts - the zone
        # script at 2323 and the subscription at 2463 - and keying without the
        # offset silently threw one away. That is how `ZONES/3796` came to look
        # unexplained: the `zone.enable 3796` that emits it lives in the script
        # that lost the collision.
        sigs["%s %d rec %d +%d @%d" % (arch, chunk, rec, field, p)] = pairs
    dd = open(omkpaths.data("IAM/DIALOG"), "rb").read()
    for i, b in D.chunks(dd):
        pr = D.parse(b)
        if not pr: continue
        for j, _nid, k, off in D.scripts_of(b, pr[1]):
            ins, _st = D.disasm(b, off, len(b))
            pairs = [x for _pc, op, raw in ins for x in loggable(op, raw, every)]
            sigs["DIALOG %d node %d %s%d"
                 % (i, j, "cond" if k < 4 else "act", k % 4)] = pairs
    return sigs


def window_anchors(got, wide, tight, span=8):
    r"""Name a script from a RUN of events, not only from a unique pair.

    The original rule was: an event whose (domain, value) pair only one slot in
    the corpus can emit NAMES that slot. It is sound, and it throws away most
    of a capture - 150 of `impasse-walk`'s 286 events are individually
    ambiguous and went unused.

    A run is far more discriminating than any of its parts. Grow a window from
    each position, intersecting the sets of slots able to emit each pair; the
    moment the intersection is a singleton, that run names a script. Across the
    four captures this takes the events anchored from 116 to 381 and the
    distinct scripts named from **26 to 38** - and each new script is one more
    the diff can actually verify.

    It is checked against the stronger rule rather than trusted: on every event
    that ALSO has a unique-pair anchor, the two agree - **221 comparisons, 0
    contradictions** (`verify.py: attribution reach`). Where they disagreed the
    unique pair would win, since a single-pair anchor rests on one fact and a
    window on an intersection.

    WHY IT IS OPT-IN in `diff`, correctly diagnosed on the second try. A
    signature is a STATIC decode - the union over a script's branches, in
    bytecode order - while a replay takes ONE path. So a window can name a
    script *correctly* and the replay still predict a different branch's
    events, which the diff then reports as a mismatch rather than as the
    "unreached" it is. That is what `SCENE 57 rec 2 +4` is against
    `impasse-walk`: the `CAMERAS/4434` that named it IS in its signature, on a
    branch this state does not take. (The first explanation tried here - that
    the window had intersected the over-inclusive `wide` index - was wrong; the
    pair is in `tight` too, and the two indexes differ on 84 pairs of 6628.)

    -> [(index, slot)] for every event a window could name.
    """
    out = []
    n = len(got)
    for i in range(n):
        cand = None
        for j in range(i, min(i + span, n)):
            # TIGHT, because `indexes` says anchoring must use the predicted
            # stream rather than the superset: naming a slot that would never
            # log the pair makes it "disagree" for no reason. Here it changes
            # nothing measurable - the two indexes differ on 84 of 6628 pairs,
            # none of them in these captures - but the principle is the tool's
            # own. (It is NOT the explanation for the `SCENE 57 rec 2 +4`
            # mismatch; see `window_anchors`' docstring for what that is.)
            s = tight.get(got[j])
            if not s: break
            cand = set(s) if cand is None else (cand & set(s))
            if not cand: break
            if len(cand) == 1:
                out.append((i, next(iter(cand))))
                break
    # the unique-pair rule wins wherever it applies: one fact beats an
    # intersection, and this keeps the older anchors exactly as they were
    strong = {i: next(iter(tight[e])) for i, e in enumerate(got)
              if len(tight.get(e, ())) == 1}
    return [(i, strong.get(i, slot)) for i, slot in out]


def window_gain(logs=("intro.log", "walkin.log", "impasse-walk.log",
                      "telis-dialog.log", "resto-387.log")):
    r"""What run-based attribution adds, and whether it ever contradicts.

    -> {"events": (unique-pair, window), "scripts": (unique-pair, window),
        "checked": n, "contradictions": n}

    The last two are the validation and the reason to believe the rest: on
    every event that ALSO carries a unique-pair anchor, the two rules are
    compared. They agree on all of them.
    """
    wide, tight = indexes()
    e_old = e_new = 0
    s_old, s_new = set(), set()
    checked = bad = 0
    for f in logs:
        p = os.path.join(OUT, f)
        if not os.path.exists(p): continue
        got = parse(p)
        strong = {i: next(iter(tight[e])) for i, e in enumerate(got)
                  if len(tight.get(e, ())) == 1}
        e_old += len(strong); s_old |= set(strong.values())
        seen = {}
        for i in range(len(got)):
            cand = None
            for j in range(i, min(i + 8, len(got))):
                st = tight.get(got[j])
                if not st: break
                cand = set(st) if cand is None else (cand & set(st))
                if not cand: break
                if len(cand) == 1:
                    nm = next(iter(cand))
                    for k in range(i, j + 1): seen.setdefault(k, nm)
                    break
        e_new += len(seen); s_new |= set(seen.values())
        for i, nm in seen.items():
            if i in strong:
                checked += 1
                if strong[i] != nm: bad += 1
    return {"events": (e_old, e_new), "scripts": (len(s_old), len(s_new)),
            "checked": checked, "contradictions": bad}


def indexes():
    r"""Two indexes, because attribution and residue need opposite errors.

    * `wide` is built from the SUPERSET signature - every domain any field of
      an instruction indexes. Used to decide whether an event is unexplained,
      where over-inclusion is the safe error: claiming "no slot can emit this"
      is a strong claim and must not rest on a narrow reading.
    * `tight` is built from the PREDICTED stream - the single pair each
      instruction actually hands the logger. Used to anchor, where the safe
      error is the other way: naming a slot that would never log that pair
      produces a script that then "disagrees" with the game for no reason.

    Measured on the walk-in capture: anchoring off `wide` made 5 of 10 anchors
    false, and all five reported as mismatches.
    """
    import collections
    wide, tight = collections.defaultdict(set), collections.defaultdict(set)
    for slot, prs in signatures(True).items():
        for pr in set(prs): wide[pr].add(slot)
    for slot, prs in signatures(False).items():
        for pr in set(prs): tight[pr].add(slot)
    return wide, tight


def attribute(path, show=60, start=0):
    import collections
    got = parse(path)[start:]
    if not got:
        print("no operand events in %s%s" % (os.path.relpath(path, ROOT),
              " after #%d" % start if start else "")); return 1
    if start: print("(only events after #%d - one live session, one segment)\n" % start)
    wide, tight = indexes()
    uniq = [(i, e) for i, e in enumerate(got) if len(tight.get(e, ())) == 1]
    none = [(i, e) for i, e in enumerate(got) if not wide.get(e)]
    print("captured %d operand events" % len(got))
    print("  %d name exactly one slot in the corpus  (the anchors)" % len(uniq))
    print("  %d are ambiguous - several slots could emit them"
          % (len(got) - len(uniq) - len(none)))
    print("  %d are emitted by NO slot at all%s"
          % (len(none), "  <-- outside the 5785" if none else ""))

    print("\nthe scripts that ran, in order (anchors, runs collapsed):")
    last, n = None, 0
    order = []
    for i, e in uniq:
        slot = next(iter(tight[e]))
        if slot == last: n += 1; continue
        if last: order.append((last, n))
        last, n = slot, 1
    if last: order.append((last, n))
    for slot, n in order[:show]:
        print("   %-34s %s" % (slot, "x%d" % n if n > 1 else ""))
    if len(order) > show: print("   ... %d more" % (len(order) - show))

    if none:
        print("\nunexplained events - nothing in the corpus can emit these:")
        import dialog_disasm as D
        for e, k in collections.Counter(e for _i, e in none).most_common(30):
            nm = (D.TAGS.get(e[0], {}).get(int(e[1]), "")
                  if e[1].lstrip("-").isdigit() else "")
            print("   %-11s %-6s %-34s %s" % (e[0], e[1], nm,
                                              "x%d" % k if k > 1 else ""))
    return 0


def slot_index():
    """name -> (code block, offset), for replaying a slot in the simulator."""
    sys.path.insert(0, os.path.join(HERE, "sim"))
    import vm as V
    # the same key `signatures` builds, offset included - see the note there:
    # a chunk's zone records and its subscription table share the (rec, field)
    # space, so the offset is what makes a slot name unique
    return {"%s %d rec %d +%d @%d" % (a, c, r, f, p): (code, p)
            for a, c, r, f, code, p in V.world_scripts()}


def replay(code, at, state=None):
    r"""Execute one slot in tools/sim and record what it WOULD have logged.

    This is the predicted single pair per instruction, not the attribution
    superset: here we are asserting what the original wrote, so an extra
    domain would be a false disagreement.

    `state` is the GameState to run against, and it MATTERS: a script whose
    branches read the game DB decides differently at different points in a
    playthrough. Passing None means the new-game state, which is right only
    for scripts the game ran at the very start - see `anchor_state`.
    """
    sys.path.insert(0, os.path.join(HERE, "sim"))
    import vm as V, gamestate
    out = []
    class Tap(V.VM):
        def step(self, op, raw, code, start, pc):
            out.extend(loggable(op, raw, every=False))
            return V.VM.step(self, op, raw, code, start, pc)
    tap = Tap(state if state is not None else gamestate.load(), V.Trace())
    # **`ui.open` PARKS here**, unlike in the static sweeps. A script that
    # reaches it waits on a person, and a replay that runs past it predicts
    # announcements the engine never made.
    #
    # This closes one of `resto-387.log`'s six disagreements outright.
    # `AREA 157 rec 60 +4` opens screen 4 - the LIFT - at instruction 3 of 37
    # and then branches on variable 496, `Etage`. Without the park the replay
    # ran all 33 remaining instructions and predicted BOTH arms of the floor
    # switch, which no capture can contain because the player picks one floor.
    # Six disagreements become five and nothing else moves.
    #
    # Found by porting: `engine/` models the park always, and its diff
    # disagreed with this one until this line existed.
    tap.ui_open_parks = True
    why, _pc = tap.run(code, at)
    return out, why


def anchor_state(save=None, slot=0):
    r"""Where a replay should START from, which is the whole point of this step.

    Two anchors, because they fail in opposite ways:

    * a SAVE (`--save`), read straight out of `IAM\GAMES` slot n. Exact - it is
      the engine's own serialisation of the same 8192-byte DB - but it only
      exists if the player saved, and it is the state at the save, not at the
      moment a given script ran;
    * EVOLVING (`--evolve`), where one state is carried across the whole diff
      and each replayed script writes into it in capture order. No save needed
      and it tracks the playthrough, but it is only as good as the attribution:
      a script the capture did not name never runs, so its writes are missing.

    Neither is the state the engine held - a trace carries decisions, not
    memory - so a disagreement under either is a lead, not a verdict.
    """
    sys.path.insert(0, os.path.join(HERE, "sim"))
    import gamestate
    if save:
        used = gamestate.save_slots(save)
        if not any(n == slot for n, *_ in used):
            print("   slot %d of %s is empty (occupied: %s)"
                  % (slot, os.path.basename(save), [n for n, *_ in used] or "none"))
            return None
        return gamestate.from_save(save, slot)
    return gamestate.load()


def selftest(n=6):
    r"""Round-trip the pipeline without the game.

    Take slots the simulator can replay, render their predicted operands into
    the exact relay-log syntax a capture uses, then read that back through
    `parse` -> `signatures` -> `replay` and require the tool to recover the
    slot it started from. It tests the parser, the attribution index and the
    replay against each other; only the engine is stubbed, and the engine is
    the one part a capture supplies.

    Anchoring only a minority of slots is the CORRECT result, not a weakness:
    world-script records are near-identical, so most of their operand pairs are
    shared and cannot name one slot. What must never happen is a slot being
    anchored to the WRONG name, and that is what this asserts.
    """
    idx = slot_index()
    picked, lines = [], []
    for name, (code, at) in idx.items():
        pred, _why = replay(code, at)
        if len(pred) >= 3:
            picked.append((name, pred)); lines += pred
        if len(picked) == n: break
    sigs = signatures()
    import collections
    where = collections.defaultdict(set)
    for slot, pairs in sigs.items():
        for pr in set(pairs): where[pr].add(slot)
    named = wrong = 0
    for name, pred in picked:
        anchors = {next(iter(where[e])) for e in pred if len(where.get(e, ())) == 1}
        if not anchors: continue
        named += 1
        if anchors != {name}: wrong += 1
    return {"slots": len(picked), "events": len(lines),
            "named": named, "misnamed": wrong}


def diff(path, show=25, save=None, slot=0, evolve=False, quiet=False,
         window=False):
    r"""The per-script diff: for every script the capture NAMES, replay it here
    and compare the two sequences.

    A capture is a playthrough and a simulator run is one entry point, so a
    head-to-head count would be meaningless. What is comparable is a single
    script: the capture says slot X ran, the simulator runs slot X from the
    same `IAM\START` state, and the two operand sequences either agree or they
    do not. Disagreement is the product - it is the first thing in this repo
    that can catch the simulator DECIDING differently from the game.

    TWO LIMITS, both of which produce false disagreements if forgotten:

    * the engine runs scripts CONCURRENTLY, so one script's events are not
      contiguous in the capture. The match is therefore an ordered subsequence,
      not a block; requiring a block reports interleaving as a mismatch.
    * the replay starts from `IAM\START`, but the game was mid-playthrough. A
      script whose branches depend on accumulated state cannot be reproduced
      from the new-game state, and a disagreement there says nothing about the
      simulator. Diffing those needs the state the capture ran under - which
      the trace does not carry, and which is the next thing this rig needs.
    """
    import collections
    say = (lambda *a, **k: None) if quiet else print
    got = parse(path)
    if not got:
        say("no operand events in %s" % os.path.relpath(path, ROOT)); return 1
    wide, tight = indexes()
    idx = slot_index()
    state = anchor_state(save, slot)
    if save and state is None: return 1
    say("replaying against: %s" % ("save %s slot %d" % (os.path.basename(save), slot)
          if save else ("the new-game state, carried forward across the capture"
                        if evolve else "the new-game state (IAM\\START)")))

    # anchors: a captured pair only one slot in the whole corpus can emit
    anchors, last = [], None
    # Unique-pair anchors by default. `window=True` adds the run-based ones,
    # which name ~46% more scripts but are opt-in on purpose. A signature is a
    # STATIC decode - the union over a script's branches - while a replay takes
    # ONE path, so a window can name a script CORRECTLY and the replay still
    # predict a different branch, which lands here as a mismatch rather than as
    # the "unreached" it is. Keeping the standing check on the strong rule
    # keeps "0 disagreements" meaning what it says.
    pairs = (window_anchors(got, wide, tight) if window else
             [(i, next(iter(tight[e]))) for i, e in enumerate(got)
              if len(tight.get(e, ())) == 1])
    for i, slot in pairs:
        if slot != last: anchors.append((i, slot)); last = slot

    say("captured %d events; %d name a single script" % (len(got), len(anchors)))
    ok = bad = skipped = unreached = 0
    for i, slot in anchors[:show]:
        if slot not in idx:
            skipped += 1; continue                  # a DIALOG branch, not a slot
        code, at = idx[slot]
        pred, why = replay(code, at, state if evolve or save else None)
        if not pred:
            skipped += 1; continue
        if got[i] not in pred:
            # The anchor came from a STATIC decode, which walks every branch;
            # the replay takes one. So the anchoring event sits on a path this
            # state does not reach - the slot is probably right and the state
            # is wrong. That is a missing anchor, not a disagreement, and it is
            # exactly what `--save` exists to fix.
            unreached += 1; continue
        # The engine interleaves concurrently-running scripts, so a script's
        # emissions are NOT contiguous in the capture - requiring a contiguous
        # block reports interleaving as disagreement. What must hold is that
        # the prediction appears as an ordered SUBSEQUENCE: same events, same
        # order, other scripts' events allowed in between.
        lo = max(0, i - 2 * len(pred))
        hi = i + 4 * len(pred) + 2
        window, truncated = got[lo:hi], hi >= len(got)
        it, k = iter(window), 0
        for e in pred:
            if any(x == e for x in it): k += 1
            else: break
        # A capture stopped mid-script leaves a PREFIX of the prediction, which
        # is agreement, not disagreement: the events that exist all match, in
        # order, and the rest simply were not recorded. Only a prefix that ends
        # while the capture still has events left is a real divergence.
        hit = (k == len(pred)) or (truncated and k)
        if hit:
            ok += 1
            if k < len(pred):
                print("  %-30s agrees on all %d events captured "
                      "(script continues past the end of the capture)"
                      % (slot, k))
        else:
            bad += 1
            say("\n  MISMATCH  %s   (%s)  agreed on %d of %d predicted"
                  % (slot, why, k, len(pred)))
            say("    game : %s" % " ".join("%s/%s" % p for p in window[:12]))
            say("    sim  : %s" % " ".join("%s/%s" % p for p in pred[:12]))
    say("\n%d scripts replayed and agreeing, %d disagreeing, %d not "
          "replayable,\n%d anchored on a branch this state does not reach "
          "(replay from a save to settle those)" % (ok, bad, skipped, unreached))
    return {"events": len(got), "ok": ok, "bad": bad,
            "skipped": skipped, "unreached": unreached}
    if bad == 0 and ok:
        say("Every script the capture names decides in tools/sim exactly what "
              "it decided\nin the engine.")


# ---------------------------------------------------------------- capturing
#
# The other half of the oracle. `run` records what the engine DECIDES; this
# records what it DRAWS. See `tools/frame.py` for what a frame is and is not
# evidence about - 2D is exact, 3D is exact about geometry and ordering and not
# about the low bits of a pixel.

FRAMES = os.path.join(OUT, "frames")


def window_rect(proc="Runtime 2.exe"):
    """The game window's rect in POINTS, through System Events.

    The same route `send_keys` already uses to focus it, so it needs no
    dependency this rig did not already have. -> (rect, "") or (None, why).
    """
    p = subprocess.run(
        ["osascript", "-e",
         'tell application "System Events" to tell process "%s" '
         'to get {position, size} of window 1' % proc],
        capture_output=True, text=True)
    if p.returncode != 0:
        return None, (p.stderr or "").strip().splitlines()[-1:][0] if p.stderr else "no window"
    try:
        n = [int(x.strip()) for x in (p.stdout or "").split(",")]
        return (n[0], n[1], n[2], n[3]), ""
    except Exception:
        return None, "unparsable: %r" % (p.stdout or "").strip()


def capture(at=(20, 24, 28), tag="menu", keys=None, desktop="1024x768",
            trace=False, out=None, no_raise=False):
    r"""Launch the game and write its own framebuffer to `traces/frames`.

    `at` is when to grab, in seconds from launch. The default lands on the
    start menu: the three FLIS movies play first, and the menu is up by ~14 s.

    **It refuses rather than degrades.** `frame.recover` raises if the display
    interpolated, and a raise means no file is written - a frame that is only
    NEARLY the framebuffer is worse than none, because every pixel diff built
    on it would go quietly wrong while still passing.

    No relay logging by default: `+relay` costs a lot and a capture does not
    need the decision stream. `--trace` turns it back on if both are wanted
    from one run.

    **WHICH DISPLAY DRIVER a capture is taken in matters, and is not set
    here.** `Runtime 2.exe CONFIG` opens a setup dialog whose "Driver video"
    combo lists exactly three entries - `DirectDraw HAL`, `DirectX software
    render` and **`The Nomad Soul software render`** - which are driver modes
    0, 1 and 2 of `sub_43A6D0`. The third is the engine's own rasterizer, and
    it names itself. Choosing one and answering "Oui" writes the config into
    the first 3496 bytes of `IAM\GAMES`; because `install_cd` makes GAMES a
    real file in the bottle rather than a symlink, that write lands in the
    bottle and `gamedata/` is untouched - verified by md5 either side.

    The committed frames are taken in **mode 0**, which is the shipped
    default. `traces/frames/mode2-24.png` is the one exception and exists to
    pin a result: the menu comes out **pixel-identical** in modes 0 and 2,
    because the blit drawers have no mode test. Only the line, triangle and
    quad drawers branch on it.
    """
    import frame as F
    if not os.path.isdir(BDIR):
        print("no bottle - run `setup --create` first"); return 1
    out = out or FRAMES
    os.makedirs(out, exist_ok=True)
    arm()
    kill_session()
    env = dict(os.environ)
    if trace:
        env.update(CX_DEBUGMSG="+relay",
                   CX_LOG=os.path.join(OUT, "%s-capture.log" % tag))
    cmd = [CXSTART, "--bottle", BOTTLE, "--"]
    if desktop:
        cmd += ["explorer", "/desktop=omk,%s" % desktop]
    cmd += [os.path.join(install_cd(), os.path.basename(GAME))]
    print("capture -> %s   (grabs at %s s)"
          % (os.path.relpath(out, ROOT), ", ".join(str(a) for a in at)))
    p = subprocess.Popen(cmd, cwd=install_cd(), env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if keys:
        import threading
        threading.Thread(target=lambda: send_keys(keys), daemon=True).start()
    t0, rect, written = time.time(), None, []
    try:
        for when in at:
            while time.time() - t0 < when:
                time.sleep(0.25)
            if rect is None:
                rect, why = window_rect()
                if rect is None:
                    print("   t=%2ds  no window yet (%s)" % (when, why[:60]))
                    continue
                print("   window %dx%d at %d,%d" % (rect[2], rect[3], rect[0], rect[1]))
            # RAISE THE WINDOW FIRST. `screencapture -R` grabs a screen
            # REGION, not a window, so anything overlapping the game lands in
            # the frame - a capture taken with a terminal on top came back
            # with the terminal's text in it, which is a silently corrupt
            # frame rather than a failed one. Focusing is the same System
            # Events call `send_keys` uses.
            #
            # **`--no-raise` turns that off**, and it exists because raising is
            # not always harmless: the game CRASHES on a window change for at
            # least one player, so a capture that steals focus destroys the
            # thing it is trying to photograph. Without the raise the region
            # grab is only clean while nothing overlaps the game - which is
            # exactly the case when a human is playing it full-screen and the
            # rig touches nothing.
            if not no_raise:
                subprocess.run(["osascript", "-e",
                                'tell application "System Events" to tell process '
                                '"Runtime 2.exe" to set frontmost to true'],
                               capture_output=True)
                time.sleep(0.6)
            # NOT a dotfile: `screencapture` silently refuses to write one -
            # and still exits 0 while doing it, so the existence test below is
            # what catches it, not the return code.
            raw = os.path.join(out, "capture-raw.png")
            r = subprocess.run(["screencapture", "-x", "-o", "-R",
                                "%d,%d,%d,%d" % rect, raw],
                               capture_output=True, text=True)
            if r.returncode != 0 or not os.path.exists(raw):
                print("   t=%2ds  screencapture failed: %s"
                      % (when, (r.stderr or "").strip()[:60]))
                continue
            w, h, rgb = F.read_png(raw)
            os.remove(raw)
            try:
                ow, oh, px = F.recover(w, h, rgb, want=(rect[2], rect[3]))
            except F.NotExact as e:
                print("   t=%2ds  REFUSED: %s" % (when, e))
                continue
            dst = os.path.join(out, "%s-%02d.png" % (tag, when))
            F.write_png(dst, ow, oh, px)
            colours = len(set(px[i:i + 3] for i in range(0, len(px), 3)))
            written.append(dst)
            print("   t=%2ds  %dx%d captured, recovered %dx%d exactly, "
                  "%d colours -> %s"
                  % (when, w, h, ow, oh, colours, os.path.basename(dst)))
    finally:
        kill_session()
    print("%d frame%s" % (len(written), "" if len(written) == 1 else "s"))
    return 0 if written else 1



def grab(tag="shot", out=None):
    r"""Grab ONE frame from a game that is already running, and touch nothing else.

    `capture` launches the game and grabs at fixed offsets from launch, which
    is right for the menu and useless for anything you have to play your way
    to: the timing is unpredictable and the last session established that
    scripted navigation cannot get in-game at all.

    So this is the other half - a human drives the game to the shot and says
    when. It does **not** launch, does **not** kill the session, and raises the
    window exactly ONCE. That last part is deliberate: a closed-loop driver
    that re-focused before every screenshot fought the user for the foreground
    for minutes and froze their machine (docs/RECONSTRUCTION.md 2026-09-01).
    One short announced action, not a loop.

    Everything after the grab is `capture`'s own path, including the refusal:
    `frame.recover` raises if the display interpolated and then nothing is
    written, because a frame that is only NEARLY the framebuffer is worse than
    none.
    """
    import frame as F
    out = out or FRAMES
    os.makedirs(out, exist_ok=True)
    rect, why = window_rect()
    if rect is None:
        print("no game window (%s) - launch it and get to the shot first" % why[:70])
        return 1
    print("window %dx%d at %d,%d - raising it once, then grabbing"
          % (rect[2], rect[3], rect[0], rect[1]))
    subprocess.run(["osascript", "-e",
                    'tell application "System Events" to tell process '
                    '"Runtime 2.exe" to set frontmost to true'],
                   capture_output=True)
    time.sleep(0.8)
    raw = os.path.join(out, "grab-raw.png")
    r = subprocess.run(["screencapture", "-x", "-o", "-R",
                        "%d,%d,%d,%d" % rect, raw], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(raw):
        print("screencapture failed: %s" % (r.stderr or "").strip()[:80])
        return 1
    w, h, rgb = F.read_png(raw)
    os.remove(raw)
    try:
        ow, oh, px = F.recover(w, h, rgb, want=(rect[2], rect[3]))
    except F.NotExact as e:
        print("REFUSED: %s" % e)
        return 1
    dst = os.path.join(out, "%s.png" % tag)
    F.write_png(dst, ow, oh, px)
    colours = len(set(px[i:i + 3] for i in range(0, len(px), 3)))
    print("%dx%d captured, recovered %dx%d exactly, %d colours -> %s"
          % (w, h, ow, oh, colours, dst))
    return 0


def config():
    r"""Open the game's own setup dialog and wait while a human drives it.

    The dialog picks the **video driver**, which is what decides whether the
    line, triangle and quad primitives go through Direct3D or the engine's own
    software rasterizer (`sub_45EF50() == 2`). Its combo lists exactly three:

        DirectDraw HAL                 driver mode 0 - hardware
        DirectX software render        mode 1
        The Nomad Soul software render mode 2 - the engine's own rasterizer

    **This is deliberately not scripted.** Driving the combo with synthetic
    key events was tried and does not work reliably - it silently leaves the
    driver where it was while reporting success, which is worse than failing.
    A human takes three seconds. Answer "Oui" to save.

    The write lands in the first 3496 bytes of the bottle's `IAM\GAMES`,
    which `install_cd` makes a real file rather than a symlink, so `gamedata/` is
    never touched.
    """
    if not os.path.isdir(BDIR):
        print("no bottle - run `setup --create` first"); return 1
    arm(); kill_session()
    root = install_cd()
    print("opening the setup dialog - pick the driver, OK, then Oui to save")
    p = subprocess.Popen([CXSTART, "--bottle", BOTTLE, "--",
                          os.path.join(root, os.path.basename(GAME)), "CONFIG"],
                         cwd=root, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)
    try:
        p.wait()
    except KeyboardInterrupt:
        pass
    while True:
        r = sh(["pgrep", "-f", "Runtime 2.exe"])
        if r.returncode != 0:
            break
        time.sleep(1.0)
    print("dialog closed")
    return 0


def main():
    a = sys.argv[1:] or ["setup"]
    cmd = a[0]
    if cmd == "setup": return setup(create="--create" in a)
    if cmd == "bootstrap":
        return bootstrap(0 if "--no-smoke" in a else
                         (int(a[a.index("--seconds") + 1]) if "--seconds" in a else 25))
    if cmd == "run":
        return run(int(a[a.index("--seconds") + 1]) if "--seconds" in a else 90,
                   tag=(a[a.index("--tag") + 1] if "--tag" in a else None),
                   twice="--twice" in a,
                   desktop=None if "--fullscreen" in a else "1024x768",
                   keys=(a[a.index("--keys") + 1] if "--keys" in a else None))
    if cmd == "config":
        return config()
    if cmd == "grab":
        return grab(tag=(a[a.index("--tag") + 1] if "--tag" in a else "shot"),
                    out=(a[a.index("--out") + 1] if "--out" in a else None))
    if cmd == "capture":
        return capture(
            at=tuple(int(x) for x in a[a.index("--at") + 1].split(","))
               if "--at" in a else (20, 24, 28),
            tag=(a[a.index("--tag") + 1] if "--tag" in a else "menu"),
            keys=(a[a.index("--keys") + 1] if "--keys" in a else None),
            desktop=None if "--fullscreen" in a else "1024x768",
            trace="--trace" in a,
            out=(a[a.index("--out") + 1] if "--out" in a else None),
            no_raise="--no-raise" in a)
    if cmd == "distil" and len(a) > 1:
        print("%d lines kept" % distil(a[1])); return 0
    if cmd == "selftest":
        r = selftest()
        print("round-trip: %d slots -> %d events; %d anchored, %d MISNAMED"
              % (r["slots"], r["events"], r["named"], r["misnamed"]))
        return 1 if r["misnamed"] else 0
    if cmd == "attribute" and len(a) > 1:
        return attribute(a[1], start=int(a[a.index("--from") + 1])
                         if "--from" in a else 0)
    if cmd == "diff" and len(a) > 1:
        r = diff(a[1],
                 window="--window" in a,
                 save=(a[a.index("--save") + 1] if "--save" in a else None),
                 slot=(int(a[a.index("--slot") + 1]) if "--slot" in a else 0),
                 evolve="--evolve" in a)
        return 0 if (r and not r["bad"]) else 1
    if cmd in ("parse", "diff") and len(a) > 1:
        if cmd == "parse":
            k = int(a[a.index("--from") + 1]) if "--from" in a else 0
            import dialog_disasm as D
            for i, (s, v) in enumerate(parse(a[1], everything="--all" in a)):
                if i < k: continue
                nm = (D.TAGS.get(s, {}).get(int(v), "")
                      if v.lstrip("-").isdigit() else "")
                print("%3d  %-12s %-6s %s" % (i, s, v, nm))
            return 0
        return diff(a[1])
    print(__doc__); return 1


if __name__ == "__main__":
    sys.exit(main())

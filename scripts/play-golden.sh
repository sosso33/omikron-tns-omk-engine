#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# THE GOLDEN RECORD for a pure refactor of `play.cpp` (todo/play-split.md S0).
#
#     scripts/play-golden.sh <dir>                 record into <dir>
#     scripts/play-golden.sh <dir> --check         record into <dir>.new and diff
#     ... --gpu                                    add the GLES and Vulkan scenes
#     ... --only street,lift                       just these scenes
#
# Each scene is a headless run with a fixed frame count, and its framebuffer,
# its stdout AND any saves file it wrote are kept. A refactor that moves code
# without changing it leaves every one of them byte for byte - that is the
# strongest check this repo has, and it is what makes a 23000-line file safe
# to cut up.
#
# The stdout is FILTERED of everything that measures time (the phase, span,
# section and slow-frame lines, and the present statistics): those are wall
# clock and differ run to run on the same binary. Everything else - every
# decision the engine prints - must match.
#
# Every run gets its OWN EMPTY saves file. The viewer reads its saves file's
# settings header at every boot, and without `--saves` it reads
# `omk-saves/GAMES` relative to the CWD - so before 2026-10-02 the record
# depended on where it was run from and on the reader's own options.
#
# A run that did not reach its frame count fails AS A RUN (CLAUDE.md 1: a
# killed GLES run still writes its dump, of frame one).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
eng="$here/engine"
sw="$eng/build/omk-play"
gl="$eng/build/omk-play-gles"
data="$(python3 "$here/tools/omkpaths.py" 2>/dev/null | awk '$1=="data" {print $3}')"
save="$here/traces/save-appart.bin"
resto="$here/traces/games-resto.bin"
[ -x "$sw" ] || { echo "no $sw - run 'make play' in engine/" >&2; exit 1; }
[ -n "$data" ] && [ -f "$save" ] && [ -f "$resto" ] || { echo "no data root or saves" >&2; exit 1; }

out="${1:?usage: play-golden.sh <dir> [--check] [--gpu] [--only a,b]}"; shift
check=0; gpu=0; only=""
while [ $# -gt 0 ]; do
    case "$1" in
        --check) check=1 ;;
        --gpu)   gpu=1 ;;
        --only)  only=",$2,"; shift ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac; shift
done
dir="$out"; [ "$check" = 1 ] && dir="$out.new"
mkdir -p "$dir"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

# name|backend|env|mask|extra arguments.
#   backend  sw  = omk-play --software (the default set)
#            vk  = omk-play --world-vulkan (the world through offscreen Vulkan)
#            gl  = omk-play-gles, window hidden (OMK_NO_GPU_PRESENT=1), under
#                  `caffeinate` - a GL run blocks at frame 0 while the display
#                  sleeps (CLAUDE.md 1)
#   vk and gl run only with --gpu: optional, as the GPU backends are (PORTING A1).
# A scene that names no --res runs at 320x240, which also keeps the interface
# SCALED (it is authored at 640x480); the screens run at 640x480, 1:1.
#
# mask: x0,y0,x1,y1 (inclusive, in the run's pixels) that the FRAME comparison
# skips, for an element the game itself drives off the wall clock. The only
# one is the subtitle box's SCROLL ARROWS: `sub_4400D0` pulses their alpha on
# the millisecond tick (`v15 / 0x3E7`) and the port does the same with
# `SDL_GetTicks`, so they differ between two runs of one binary. They sit in
# columns w-32..w-25; at 320x240 that is 288..295, masked over the lower third.
#
# The env column of `shoot`: the shoot HUD turns a ring and the held weapon
# with `SDL_GetTicks`, so those two boxes differ between two runs of the SAME
# binary. `OMK_NOUI=1` keeps the shoot world, the gunmen and the camera and
# drops the HUD, which `engine: shoot fire` and the HUD checks cover.
N="--nofmv --nodelay --no-crowd"
scenes=(
  # --- the original six (S0, 2026-09-22) ---
  "street|sw|||--save $save --area 0 --stand 1804,0,-6890,336 --density 4 --frames 400"
  "flat|sw|||--save $save --area 237 --stand 3054,1071,-753,154 --frames 220"
  "shoot|sw|OMK_NOUI=1||--save $save --area 230 --scene-chunk 56 --zone-disable 3949 --radar always --frames 400"
  "fight|sw|||--save $save --fight-supermarket --frames 500"
  "scene|sw|||--scene Aapkayl --eye 3526,1015,-905 --at 3412,1032,-882 --fov 83 --frames 30"
  "swim|sw|||--save $save --area 1 --stand 10524,-40,10284,270 --frames 370 $N --hold k200*140,k*40,k157+208*320,k*60,k157*6"
  # --- added 2026-10-02: what landed between S0 and S2 ---
  # the cold boot: the start menu, its Options, the Video page, a settings-only
  # save (the saves file it writes is part of the record)
  "boot|sw|||--nofmv --res 640x480 --frames 280 --keydelay 20 --keys 0,0xD0,0xD0,0x1C,0x1C,0xCD,0xD0,0xCD,0x39,0xC8,0x1C,0x39"
  # road traffic: motos and hover taxis on the vehicle lanes, density 3
  "traffic|sw|||--save $save --area 0 --stand 5620,0,-2400,270 --density 3 --nofmv --frames 500"
  # the same street presented at 60 fps (the sixty-fps steps)
  "sixty|sw|||--save $save --area 0 --stand 1804,0,-6890,336 --density 2 --nofmv --framerate 60 --frames 300"
  # every enhancement the software path honours
  "enhance|sw|||--save $save --area 237 --stand 3054,1071,-753,154 --nofmv --enhance-all --frames 120"
  # scripted object motion: the chest's lid on its hinge, and the take
  "chest|sw|||--save $save --slot 0 --area 237 --address 683 $N --hold k*40,k28*2,k*200 --frames 260"
  # the walker's slide under the lift's lintel
  "lintel|sw|||--save $save --slot 0 --area 237 --address 677 $N --hold k*120,k200*240 --frames 360"
  # walking into a conversation: the dialogue cameras, the body standing still
  "dialogue|sw||288,160,295,239|--save $save --slot 0 --stand 3572,1071,-1030,181 $N --hold k*30,k200*200,k*200,k28*2,k*100,k28*2,k*100 --frames 650"
  # the restaurant and the crane (games-resto slot 2; the back-face cull's scene)
  "resto|sw|||--save $resto --slot 2 --stand 2547,22,-6930,314 $N --hold 0*30,k28*2,0*118,k28*2,0*118,k28*2 --frames 300"
  # the SCREENS, one each, at 1:1
  "lift|sw|||--save $save --area 157 --address 446 --res 640x480 $N --hold k*40,k28*2,k*60,k208*2,k*30,k28*2,k*220 --frames 400"
  # the same lift ENDING with the screen open, so the description box is in the frame
  "liftbox|sw|||--save $save --area 157 --address 446 --res 640x480 $N --hold k*40,k28*2,k*60,k208*2,k*30 --frames 200"
  "den|sw|||--save $save --area 146 --scene-chunk 43 --var 482=1 --stand 522,-10,343,178 --zone-disable 2419 --res 640x480 $N --hold k*60,k28*8,k*75,k200*2,k*6,k200*2,k*6,k200*2,k*6,k205*2,k*6,k208*2,k*6,k208*2,k*6,k205*2,k*6,k208*2,k*6,k205*2,k*6,k208*2,k*6,k208*2,k*6,k208*2,k*6,k*60 --frames 400"
  "gandhar|sw|||--save $save --area 81 --address 263 --res 640x480 $N --hold k*50,k28*3,k*40,k205*3,k*6,k205*3,k*6,k205*3,k*6,k205*3,k*6,k205*3,k*6,k205*3,k*10,k28*3,k*20,k208*3,k*6,k203*3,k*6,k203*3,k*10,k28*3,k*20,k*100 --frames 400"
  "terminal|sw|||--save $save --area 237 --address 680 --res 640x480 $N --hold k*40,k28*2,k*80,k28*2,k*300 --frames 450"
  "shop|sw|||--save $save --area 39 --money 300 --res 640x480 --nofmv --no-crowd --keys 28,28,28,28,208,28,28 --keydelay 60 --frames 420"
  "multiplan|sw|||--save $save --area 39 --stand 14591,-251,11771,0 --res 640x480 --nofmv --no-crowd --keys 28,28,0xD0 --keydelay 60 --frames 240"
  "sneak|sw|||--save $save --area 237 --stand 3054,1071,-753,154 --res 640x480 $N --sneak --hold k*120,k205*2,k*40,k205*2,k*40,k205*2,k*60 --frames 320"
  # the slider, flown
  "ride|sw|||--save $save --area 0 --ride --nofmv --no-crowd --hold 0*20,k200*120,0*10,k205*20,0*30 --frames 200"
  # --- the GPU backends, with --gpu ---
  "vk-street|vk|||--save $save --area 0 --stand 1804,0,-6890,336 --density 4 --nofmv --frames 200"
  "vk-fight|vk|||--save $save --fight-supermarket --frames 300"
  "gl-street|gl|||--save $save --area 0 --stand 1804,0,-6890,336 --density 4 --nofmv --frames 200"
  "gl-flat|gl|||--save $save --area 237 --stand 3054,1071,-753,154 --nofmv --frames 150"
  "gl-fight|gl|||--save $save --fight-supermarket --frames 300"
)

want() {   # name backend -> 0 if this scene runs
    [ -n "$only" ] && { case "$only" in *",$1,"*) return 0 ;; *) return 1 ;; esac; }
    [ "$2" = sw ] || [ "$gpu" = 1 ]
}

if [ "$gpu" = 1 ] && [ ! -x "$gl" ]; then
    echo "no $gl - run 'make play-gles' in engine/ (gl scenes skipped)" >&2
fi

set -f          # no globbing: `--hold k200*140` is an argument, not a pattern
ran=(); masks=()
failed=0
for s in "${scenes[@]}"; do
    name="${s%%|*}"; rest="${s#*|}"
    be="${rest%%|*}"; rest="${rest#*|}"
    senv="${rest%%|*}"; rest="${rest#*|}"
    mask="${rest%%|*}"; args="${rest#*|}"
    want "$name" "$be" || continue
    rm -f "$dir/$name.bin" "$dir/$name.saves"
    case "$be" in
        sw) bin="$sw"; pre=(env SDL_VIDEODRIVER=dummy); post=(--software) ;;
        vk) bin="$sw"; pre=(env SDL_VIDEODRIVER=dummy); post=(--world-vulkan) ;;
        gl) [ -x "$gl" ] || continue
            bin="$gl"; pre=(caffeinate -d -u env OMK_NO_GPU_PRESENT=1); post=() ;;
    esac
    case " $args " in *" --res "*) ;; *) post+=(--res 320x240) ;; esac
    saves="$tmp/$name.GAMES"; rm -f "$saves"
    # unquoted so bash splits it into words - and `set -f` above keeps the
    # `--hold` pattern's `*` from globbing. (`${=args}` is the ZSH spelling and
    # this is bash: it is a "bad substitution" here, which is CLAUDE.md 5's
    # word-splitting trap seen from the other side.)
    # shellcheck disable=SC2086
    "${pre[@]}" $senv "$bin" "$data" "$here/tables" $args "${post[@]}" \
        --saves "$saves" --dump "$dir/$name.bin" > "$dir/$name.raw" 2>&1 || true
    [ -f "$saves" ] && cp "$saves" "$dir/$name.saves"
    # the saves file's temporary path is printed by a save; it is not a decision
    LC_ALL=C sed -i '' "s|$tmp/|<saves>/|g" "$dir/$name.raw"
    grep -av -E '^wrote |phases \(ms|gles \(ms|spans \(ms|sections -|SLOW FRAME|gpu present|present:|of which sim\+draw|overlay: |bodies: posed|ms(,| *$)|[0-9]+\.[0-9] *ms' \
        "$dir/$name.raw" > "$dir/$name.log" || true
    # the frame count the run reached, read from its own log
    want_n="$(printf '%s\n' "$args" | sed -nE 's/.*--frames ([0-9]+).*/\1/p')"
    got_n="$(grep -aoE '^[0-9]+ frames presented' "$dir/$name.raw" | tail -1 | awk '{print $1}')"
    note=""
    if [ ! -f "$dir/$name.bin" ]; then note="  NO FRAME"; failed=1
    elif [ -n "$want_n" ] && [ "${got_n:-0}" -lt "$want_n" ]; then
        note="  SHORT RUN: ${got_n:-0} of $want_n frames"; failed=1
    fi
    printf '%-10s %-2s %8s  %5s lines%s%s\n' "$name" "$be" \
        "$( [ -f "$dir/$name.bin" ] && wc -c < "$dir/$name.bin" || echo '-')" \
        "$(wc -l < "$dir/$name.log")" \
        "$( [ -f "$dir/$name.saves" ] && echo '  +saves' )" "$note"
    ran+=("$name"); masks+=("$mask")
done
[ ${#ran[@]} -gt 0 ] || { echo "no scene matched" >&2; exit 2; }

if [ "$check" = 1 ]; then
    bad=0
    for i in "${!ran[@]}"; do
        name="${ran[$i]}"; mask="${masks[$i]}"
        [ -f "$out/$name.log" ] || { echo "NOT IN THE RECORD: $name"; bad=1; continue; }
        if [ -z "$mask" ]; then
            cmp -s "$out/$name.bin" "$dir/$name.bin" || { echo "FRAME DIFFERS: $name"; bad=1; }
        else
            # exact everywhere but the mask; the width from the dump's size
            python3 - "$out/$name.bin" "$dir/$name.bin" "$mask" <<'PY' || { echo "FRAME DIFFERS: $name (outside its mask)"; bad=1; }
import sys
a = open(sys.argv[1], "rb").read(); b = open(sys.argv[2], "rb").read()
x0, y0, x1, y1 = map(int, sys.argv[3].split(","))
w = 320 if len(a) == 320 * 240 * 2 else 640 if len(a) == 640 * 480 * 2 else 0
if len(a) != len(b) or not w: sys.exit(1)
d = [i // 2 for i in range(0, len(a), 2) if a[i:i + 2] != b[i:i + 2]]
out = [p for p in d if not (x0 <= p % w <= x1 and y0 <= p // w <= y1)]
if d: print("  %s: %d pixels differ inside its mask, %d outside" % (sys.argv[2].rsplit("/", 1)[1], len(d) - len(out), len(out)))
sys.exit(1 if out else 0)
PY
        fi
        diff -q "$out/$name.log" "$dir/$name.log" >/dev/null || { echo "STDOUT DIFFERS: $name"; bad=1; }
        if [ -f "$out/$name.saves" ] || [ -f "$dir/$name.saves" ]; then
            cmp -s "$out/$name.saves" "$dir/$name.saves" 2>/dev/null || { echo "SAVES DIFFER: $name"; bad=1; }
        fi
    done
    [ "$bad" = 0 ] && [ "$failed" = 0 ] && echo "all ${#ran[@]} scenes identical" \
        || echo "the record does NOT match"
    [ "$bad" = 0 ] && [ "$failed" = 0 ]; exit $?
fi
exit "$failed"

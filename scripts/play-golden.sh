#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# THE GOLDEN RECORD for a pure refactor of `play.cpp` (todo/play-split.md S0).
#
#     scripts/play-golden.sh <dir>        record into <dir>
#     scripts/play-golden.sh <dir> --check   record into <dir>.new and diff
#
# Each scene is a headless software run with a fixed frame count, and BOTH its
# framebuffer and its stdout are kept. A refactor that moves code without
# changing it leaves every one of them byte for byte - that is the strongest
# check this repo has, and it is what makes a 20000-line file safe to cut up.
#
# The stdout is FILTERED of everything that measures time (the phase, span,
# section and slow-frame lines, and the present statistics): those are wall
# clock and differ run to run on the same binary. Everything else - every
# decision the engine prints - must match.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
eng="$here/engine"
play="$eng/build/omk-play"
data="$(python3 "$here/tools/omkpaths.py" 2>/dev/null | awk '$1=="data" {print $3}')"
save="$here/traces/save-appart.bin"
[ -x "$play" ] || { echo "no $play - run 'make play' in engine/" >&2; exit 1; }
[ -n "$data" ] && [ -f "$save" ] || { echo "no data root or save" >&2; exit 1; }

out="${1:?usage: play-golden.sh <dir> [--check]}"
check="${2:-}"
dir="$out"; [ "$check" = "--check" ] && dir="$out.new"
mkdir -p "$dir"

# name|env|extra arguments. The env column exists for ONE reason: the shoot
# HUD turns a ring and the held weapon with `SDL_GetTicks`, so those two boxes
# differ between two runs of the SAME binary and the scene cannot be a byte
# oracle with them on. `OMK_NOUI=1` leaves the shoot world, the gunmen and the
# camera - everything a refactor of the loop could break - and drops the HUD,
# which `engine: shoot fire` and the HUD checks cover on their own.
scenes=(
  "street||--save $save --area 0 --stand 1804,0,-6890,336 --density 4 --frames 400"
  "flat||--save $save --area 237 --stand 3054,1071,-753,154 --frames 220"
  "shoot|OMK_NOUI=1|--save $save --area 230 --scene-chunk 56 --zone-disable 3949 --radar always --frames 400"
  "fight||--save $save --fight-supermarket --frames 500"
  "scene||--scene Aapkayl --eye 3526,1015,-905 --at 3412,1032,-882 --fov 83 --frames 30"
  "swim||--save $save --area 1 --stand 10524,-40,10284,270 --frames 370 --nofmv --nodelay --no-crowd --hold k200*140,k*40,k157+208*320,k*60,k157*6"
)
set -f          # no globbing: `--hold k200*140` is an argument, not a pattern
for s in "${scenes[@]}"; do
    name="${s%%|*}"; rest="${s#*|}"; senv="${rest%%|*}"; args="${rest#*|}"
    # unquoted so bash splits it into words - and `set -f` above keeps the
    # `--hold` pattern's `*` from globbing. (`${=args}` is the ZSH spelling and
    # this is bash: it is a "bad substitution" here, which is CLAUDE.md 5's
    # word-splitting trap seen from the other side.)
    # shellcheck disable=SC2086
    # shellcheck disable=SC2086
    env SDL_VIDEODRIVER=dummy $senv "$play" "$data" "$here/tables" $args \
        --software --res 320x240 --dump "$dir/$name.bin" > "$dir/$name.raw" 2>&1 || true
    grep -av -E '^wrote |phases \(ms|gles \(ms|spans \(ms|sections -|SLOW FRAME|gpu present|present:|of which sim\+draw|overlay: |bodies: posed|ms(,| *$)|[0-9]+\.[0-9] *ms' \
        "$dir/$name.raw" > "$dir/$name.log" || true
    printf '%-7s %s  %s lines\n' "$name" \
        "$( [ -f "$dir/$name.bin" ] && wc -c < "$dir/$name.bin" || echo 'NO FRAME')" \
        "$(wc -l < "$dir/$name.log")"
done

if [ "$check" = "--check" ]; then
    bad=0
    for s in "${scenes[@]}"; do
        name="${s%%|*}"
        cmp -s "$out/$name.bin" "$dir/$name.bin" || { echo "FRAME DIFFERS: $name"; bad=1; }
        diff -q "$out/$name.log" "$dir/$name.log" >/dev/null || { echo "STDOUT DIFFERS: $name"; bad=1; }
    done
    [ "$bad" = 0 ] && echo "all scenes identical" || echo "the record does NOT match"
    exit "$bad"
fi

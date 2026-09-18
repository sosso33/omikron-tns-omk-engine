#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Convert the game's three intro films to what the PS Vita's HARDWARE decoder
# plays (todo/vita-port.md).
#
#     scripts/vita-movies.sh            -> engine/build/vita-movies/*.mp4
#
# Then copy the .mp4 files to ux0:data/omk/movies/ on the console. The game
# plays EIDOS.mp4 / QUANTIC.mp4 / GAME.mp4 through SceAvPlayer when they are
# there, and falls back to decoding FLIS/*.mpg in software when they are not.
#
# WHY: the films are MPEG-1, which the Vita has no hardware for, and the
# software decoder (pl_mpeg) is far slower than real time on its CPU - a
# console log showed 41 of EIDOS's 386 frames and 1 of QUANTIC's 102. The
# Vita's decoder takes H.264; BASELINE profile is the one every Vita firmware
# plays. The picture stays at the films' own 320x240 (the game's present pass
# scales it to the screen, as it does the software path) and the sound is AAC
# at 44100 Hz stereo, the rate the MPEG streams already have.
#
# READ-ONLY on the game data: the films are read from the data root that
# `tools/omkpaths.py` resolves and written under engine/build/ - never next to
# them (CLAUDE.md §1).
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
out="$here/engine/build/vita-movies"
data="$(python3 "$here/tools/omkpaths.py" 2>/dev/null | awk '$1=="data" {print $3}')"
[ -n "$data" ] || { echo "no data root - set it in omk.conf or \$OMK_DATA" >&2; exit 1; }
command -v ffmpeg >/dev/null || { echo "ffmpeg is needed" >&2; exit 1; }
mkdir -p "$out"

for stem in EIDOS QUANTIC GAME; do
    # the data tree's own spelling, case-insensitively (the game ran on
    # Windows; a copy may have renamed it)
    src="$(find "$data" -maxdepth 2 -ipath "*/FLIS/$stem.mpg" | head -1)"
    [ -n "$src" ] || { echo "no FLIS/$stem.mpg under $data" >&2; exit 1; }
    dst="$out/$stem.mp4"
    ffmpeg -loglevel error -y -i "$src" \
        -c:v libx264 -profile:v baseline -level 3.0 -pix_fmt yuv420p \
        -preset slow -crf 18 \
        -c:a aac -b:a 160k -ar 44100 -ac 2 \
        -movflags +faststart "$dst"
    printf '%-12s %s -> %s (%s bytes)\n' "$stem" "$(basename "$src")" "$dst" \
        "$(stat -f %z "$dst" 2>/dev/null || stat -c %s "$dst")"
done
echo "copy $out/*.mp4 to ux0:data/omk/movies/ on the Vita"

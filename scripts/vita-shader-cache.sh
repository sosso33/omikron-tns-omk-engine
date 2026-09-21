#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Collect vitaGL's compiled shaders into engine/backends/vita/shader_cache/, so
# the next `make vita` carries them in the VPK and a player's console needs no
# libshacccg.suprx (todo/vita-port.md, "precompiled shaders").
#
#     scripts/vita-shader-cache.sh                 from the Vita3K storage
#     scripts/vita-shader-cache.sh <folder>        from a copy of a console's
#                                                  ux0:data/shader_cache
#
# The cache is WRITTEN by a run that has the compiler: the game in Vita3K
# (`scripts/vita3k-run.sh game 60`, with libshacccg.suprx in its ur0/data), or
# the game on a console - then copy ux0:data/shader_cache off it IN BINARY
# MODE. A file is `<hash of the shader's source>.gxp`, so a changed shader
# simply misses; re-run this after touching a shader in glesrender.cpp.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
from="${1:-$here/engine/build/vita3k/fs/ux0/data/shader_cache}"
to="$here/engine/backends/vita/shader_cache"
[ -d "$from" ] || { echo "no $from - run the game once where the compiler is" >&2; exit 1; }
n=0
while IFS= read -r f; do
    rel="${f#"$from"/}"
    mkdir -p "$to/$(dirname "$rel")"
    cp "$f" "$to/$rel"
    n=$((n + 1))
done < <(find "$from" -name '*.gxp' -type f)
[ "$n" -gt 0 ] || { echo "no .gxp under $from" >&2; exit 1; }
echo "$n compiled shader(s) in $to - now 'make vita'"

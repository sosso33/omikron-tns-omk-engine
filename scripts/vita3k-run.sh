#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run one of OMK's Vita builds in the Vita3K emulator (todo/vita-port.md).
#
#     scripts/vita3k-run.sh bench|smoke|game [seconds]
#
# Everything lives under engine/build/vita3k/ (gitignored): the emulator app
# and its emulated storage `fs/`. `fs/ux0/data/omk/gamedata` is a SYMLINK to
# the data tree (`omkpaths`), not a copy.
#
# **Vita3K has ONE storage path, in its GLOBAL config**
# (`~/Library/Application Support/Vita3K/Vita3K/config.yml`, `pref-path`), and
# `-r <TITLEID>` is checked against the apps in it before a `-c` config is
# applied - so a private config cannot hold it, and there is no portable mode.
# This script therefore POINTS THAT GLOBAL SETTING at `fs/` before each run.
# On a machine that uses Vita3K for anything else, that is a change to know
# about; `pref-path: ""` puts it back to the emulator's default.
#
# FIRST TIME: `scripts/vita3k-run.sh setup` downloads the emulator; then run
# any target once and install the firmware from Vita3K's own setup window
# (PSVUPDAT.PUP, from Sony). For smoke / game the GPU path also needs
# `libshacccg.suprx` - Sony's runtime shader compiler, which only comes from a
# console - at engine/build/vita3k/fs/ur0/data/libshacccg.suprx.
#
# A VPK is a zip: it is unpacked straight into ux0:app/<TITLEID>/ (this
# emulator build ignores a VPK named on its command line) and launched with
# `-r`. The run's log is engine/build/vita3k/<target>.log; the programs' own
# output lands in engine/build/vita3k/fs/ux0/data/omk/. With `seconds` the
# emulator is closed after that long; without, it stays until you close it.
#
# `VITA3K_ARGS` passes flags through, e.g. `VITA3K_ARGS="-B OpenGL"` for the
# emulator's OpenGL renderer: its Vulkan one (on MoltenVK) faulted translating
# vitaGL's first program on 2026-09-18.
#
# THE TIMINGS ARE THE EMULATOR'S (a JIT on the host), never the console's.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
v3k="$here/engine/build/vita3k"
app="$v3k/Vita3K.app/Contents/MacOS/Vita3K"
fs="$v3k/fs"
global="$HOME/Library/Application Support/Vita3K/Vita3K/config.yml"

setup() {
    mkdir -p "$v3k"
    if [ ! -x "$app" ]; then
        case "$(uname -s)-$(uname -m)" in
            Darwin-arm64)  dmg=macos-arm64-latest.dmg ;;
            Darwin-x86_64) dmg=macos-latest.dmg ;;
            *) echo "fetch Vita3K for this host by hand into $v3k" >&2; exit 1 ;;
        esac
        curl -sL -o "$v3k/vita3k.dmg" \
            "https://github.com/Vita3K/Vita3K/releases/download/continuous/$dmg"
        hdiutil attach -nobrowse -quiet -mountpoint "$v3k/mnt" "$v3k/vita3k.dmg"
        cp -R "$v3k/mnt/Vita3K.app" "$v3k/"
        hdiutil detach -quiet "$v3k/mnt"
        rm -f "$v3k/vita3k.dmg"
        xattr -dr com.apple.quarantine "$v3k/Vita3K.app" 2>/dev/null || true
    fi
    mkdir -p "$fs/ux0/data/omk" "$fs/ur0/data"
    if [ ! -f "$global" ]; then
        # the emulator writes its defaults on its first start
        "$app" --help >/dev/null 2>&1 || true
    fi
    if grep -q '^pref-path:' "$global"; then
        sed -i '' -e "s|^pref-path:.*|pref-path: \"$fs/\"|" "$global"
    else
        echo "pref-path: \"$fs/\"" >> "$global"
    fi
    sed -i '' -e 's/^initial-setup:.*/initial-setup: true/' "$global"
    data="$(python3 "$here/tools/omkpaths.py" 2>/dev/null | awk '$1=="data" {print $3}')"
    [ -n "$data" ] && ln -sfn "$data" "$fs/ux0/data/omk/gamedata"
    echo "Vita3K ready in $v3k (data -> ${data:-unset})"
}

case "${1:-}" in
    setup) setup; exit 0 ;;
    bench) vpk=omk_bench; id=OMKB00001 ;;
    smoke) vpk=omk_smoke; id=OMKS00001 ;;
    game)  vpk=omk_vita;  id=OMKE00001 ;;
    *) echo "usage: $0 setup|bench|smoke|game [seconds]" >&2; exit 2 ;;
esac
setup >/dev/null   # idempotent: points the global storage path at fs/ every run
file="$here/engine/build/vita/$vpk.vpk"
[ -f "$file" ] || { echo "no $file - run 'make vita' in engine/" >&2; exit 1; }
rm -rf "$fs/ux0/app/$id"
mkdir -p "$fs/ux0/app/$id"
unzip -q -o "$file" -d "$fs/ux0/app/$id"
if [ "$vpk" != omk_bench ] && [ ! -f "$fs/ur0/data/libshacccg.suprx" ]; then
    echo "warning: no $fs/ur0/data/libshacccg.suprx - the GPU path will fault" >&2
fi
log="$v3k/$1.log"
if [ -n "${2:-}" ]; then
    "$app" ${VITA3K_ARGS:-} -r "$id" > "$log" 2>&1 &
    pid=$!
    sleep "$2"
    # Vita3K ignores SIGTERM while a title runs, so a plain kill left one
    # emulator window per run behind (six by 2026-09-18's first hour)
    kill "$pid" 2>/dev/null || true
    sleep 3
    kill -9 "$pid" 2>/dev/null || true
else
    "$app" ${VITA3K_ARGS:-} -r "$id" > "$log" 2>&1 || true
fi
echo "log: $log"
echo "output: $fs/ux0/data/omk/"
ls "$fs/ux0/data/omk/" 2>/dev/null | grep -v gamedata || true

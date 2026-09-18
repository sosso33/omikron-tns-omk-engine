#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build vitaGL the way OMK needs it, into engine/build/vitagl/libvitaGL.a.
#
#     scripts/vita-vitagl.sh            (needs $VITASDK or ~/vitasdk, git, make)
#
# WHY NOT THE SDK'S OWN: `vdpm install vitaGL` ships a library whose boot
# SPLASH SCREEN creates a SECOND sceGxm context. Vita3K allows one, so every
# VPK built on it dies in the emulator at start-up
# (`sceGxmCreateContext returned SCE_GXM_ERROR_ALREADY_INITIALIZED`, then an
# access to 0x78 - found 2026-09-18, todo/vita-port.md). A game has no use for
# vitaGL's logo on a real console either, so ONE build serves both:
# NO_SPLASHSCREEN=1, and HAVE_VITA3K_SUPPORT=1 (which, at this commit, only
# turns off hardware ETC1 textures - OMK uploads none).
#
# PINNED to the commit the SDK's package is built from (vdpm's
# `0.0.0.r1448.gcd3791e`), whose vitaGL.h is byte-identical to the one in
# $VITASDK - so the header the port compiles against and this library agree.
# `backends/vita/CMakeLists.txt` links this library when it exists and the
# SDK's otherwise, and says which.
set -euo pipefail

COMMIT=cd3791e
here="$(cd "$(dirname "$0")/.." && pwd)"
out="$here/engine/build/vitagl"
export VITASDK="${VITASDK:-$HOME/vitasdk}"
export PATH="$VITASDK/bin:$PATH"

if [ ! -f "$VITASDK/share/vita.toolchain.cmake" ]; then
    echo "no VitaSDK at $VITASDK - set \$VITASDK" >&2
    exit 1
fi

src="$out/src"
mkdir -p "$out"
if [ ! -d "$src/.git" ]; then
    git clone -q https://github.com/Rinnegatamante/vitaGL "$src"
fi
git -C "$src" fetch -q origin || true
git -C "$src" checkout -q "$COMMIT"

if ! cmp -s "$src/source/vitaGL.h" "$VITASDK/arm-vita-eabi/include/vitaGL.h"; then
    echo "warning: vitaGL.h at $COMMIT differs from the SDK's - the library and" >&2
    echo "the header the port compiles against may not agree" >&2
fi

make -C "$src" clean >/dev/null 2>&1 || true
make -C "$src" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)" \
    NO_SPLASHSCREEN=1 HAVE_VITA3K_SUPPORT=1 >/dev/null
cp "$src/libvitaGL.a" "$out/libvitaGL.a"
echo "built $out/libvitaGL.a (vitaGL $COMMIT, NO_SPLASHSCREEN=1 HAVE_VITA3K_SUPPORT=1)"

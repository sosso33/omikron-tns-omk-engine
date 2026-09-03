#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# install-deps.sh - check for, and optionally install, the OPTIONAL system
# libraries the viewers use.
#
# READ THIS FIRST: none of it is needed to use this repository.
#
#   make                    works with no SDL and no Vulkan
#   python3 tools/verify.py works with no SDL and no Vulkan
#
# That is a property the build is required to have - `docs/PORTING.md` A1 says
# a bare checkout with no Vulkan SDK must still build and pass, and A8 lists
# SDL and "Vulkan loader + MoltenVK" as OPTIONAL system dependencies whose
# absence disables a frontend and never the suite. `engine/Makefile` enforces
# it: `make play` and `make vulkan` print why and exit 0 rather than failing.
# So this script exists to make the viewers available, not to make the project
# work, and it must never become a prerequisite.
#
# The Python side needs NOTHING. Every tool here is standard library only -
# `verify.py` deliberately has no PIL dependency and `tools/frame.py` carries a
# PIL-free PNG codec to keep it that way. There is no requirements.txt because
# there is nothing to put in it.
#
#   scripts/install-deps.sh            report what is present, install nothing
#   scripts/install-deps.sh --install  install what is missing
#
set -uo pipefail

INSTALL=0
[ "${1:-}" = "--install" ] && INSTALL=1
[ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ] && {
    sed -n '4,27p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

ok=0; warn=0; miss=0
say()  { printf '  %-22s %s\n' "$1" "$2"; }
good() { say "$1" "ok        $2"; ok=$((ok+1)); }
opt()  { say "$1" "absent    $2"; warn=$((warn+1)); }
bad()  { say "$1" "BROKEN    $2"; miss=$((miss+1)); }

OS="$(uname -s)"
echo
echo "omk dependencies   ($OS)"
echo

# ---------------------------------------------------------------- required
echo "required - without these nothing builds:"

if command -v python3 >/dev/null 2>&1; then
    good python3 "$(python3 -V 2>&1 | cut -d' ' -f2)  (standard library only)"
else
    bad python3 "install python 3.9 or newer"
fi

if command -v make >/dev/null 2>&1; then good make "$(command -v make)"
else bad make "no make on PATH"; fi

# A C++20 compiler. Test with a real #include, not an empty main: the failure
# this catches is a missing HEADER search path, and a file with no includes
# compiles happily without one. That false pass cost a diagnosis once.
CXXPROBE="$(mktemp -t omkcxx).cpp"
printf '#include <cstddef>\n#include <span>\nint main(){return 0;}\n' > "$CXXPROBE"
CXX_BIN="${CXX:-c++}"
if ! command -v "$CXX_BIN" >/dev/null 2>&1; then
    bad "c++" "no C++ compiler on PATH"
elif "$CXX_BIN" -std=c++20 -fsyntax-only "$CXXPROBE" >/dev/null 2>&1; then
    good "c++ (C++20)" "$(command -v "$CXX_BIN")"
else
    bad "c++ (C++20)" "present but cannot find the standard headers"
    echo
    echo "    A C++20 compiler is on PATH but \`#include <cstddef>\` fails."
    if [ "$OS" = "Darwin" ]; then
        CLT=/Library/Developer/CommandLineTools
        STALE="$CLT/usr/include/c++/v1"
        echo "    On macOS this is almost always one of two things."
        echo
        echo "    1. xcode-select points at an Xcode that is broken or gone:"
        echo "         xcode-select -p   -> $(xcode-select -p 2>&1)"
        echo "       If that is not $CLT and Xcode does not"
        echo "       work, switch to the standalone tools:"
        echo "         sudo xcode-select --switch $CLT"
        echo
        echo "    2. A STALE libc++ from an old Command Line Tools shadows the"
        echo "       SDK's copy. clang looks in the toolchain's own"
        echo "       include/c++/v1 first; if an ancient install left a few"
        echo "       files there, the search finds that directory, does not"
        echo "       find <cstddef> in it, and never falls through to the SDK -"
        echo "       where the header is present and correct."
        if [ -d "$STALE" ]; then
            n=$(ls -1 "$STALE" 2>/dev/null | wc -l | tr -d ' ')
            echo "         $STALE holds $n entries"
            if [ "$n" -lt 20 ]; then
                echo "       $n is far too few for a real libc++ (a healthy one has"
                echo "       ~190), so this is the stale-leftover case. Remove it:"
                echo "         sudo rm -rf $CLT/usr/include/c++"
                echo "       The SDK copy underneath is the one that gets used."
            fi
        fi
        echo
        echo "    To confirm the diagnosis without changing anything - this"
        echo "    bypasses the shadowing directory and uses the SDK's headers:"
        echo
        echo "      echo '#include <cstddef>' | c++ -std=c++20 -x c++ - \\"
        echo "        -nostdinc++ -isystem \"\$(xcrun --show-sdk-path)/usr/include/c++/v1\" \\"
        echo "        -fsyntax-only && echo 'headers fine - the search path is the fault'"
        echo
        echo "    If that compiles, the SDK is healthy and only the path is wrong,"
        echo "    which is what the removal above fixes."
    else
        echo "    Install a C++20 toolchain (g++ 10+ or clang 12+) and libstdc++ headers."
    fi
    echo
fi
rm -f "$CXXPROBE"

# ---------------------------------------------------------------- optional
echo
echo "optional - each enables a viewer, none is needed for make or verify.py:"

have_pkg() { command -v pkg-config >/dev/null 2>&1 && pkg-config --exists "$1"; }

if command -v pkg-config >/dev/null 2>&1; then good pkg-config "$(command -v pkg-config)"
else opt pkg-config "the Makefile probes SDL and Vulkan through it; without it both stay off"; fi

if   have_pkg sdl3; then good SDL "sdl3 $(pkg-config --modversion sdl3)  -> make play"
elif have_pkg sdl2; then good SDL "sdl2 $(pkg-config --modversion sdl2)  -> make play"
else opt SDL "no window: build/omk-play unavailable (PORTING A8 names SDL3; SDL2 also works)"; fi

if have_pkg vulkan; then good Vulkan "$(pkg-config --modversion vulkan)  -> make vulkan"
else opt Vulkan "software rasterizer only; the GPU backend is optional by design"; fi

if command -v glslc >/dev/null 2>&1; then good glslc "$(command -v glslc)"
else opt glslc "shaderc; without it 'make vulkan' cannot compile the shaders"; fi

# ---------------------------------------------------------------- inputs
echo
echo "inputs - supplied by you, never shipped here:"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$ROOT/tools/omkpaths.py" 2>/dev/null | sed -n '3,7p' | sed 's/^/  /' \
  || echo "  (run python3 tools/omkpaths.py)"

# ---------------------------------------------------------------- install
if [ "$INSTALL" = "1" ]; then
    echo
    echo "installing the optional libraries..."
    case "$OS" in
      Darwin)
        if ! command -v brew >/dev/null 2>&1; then
            echo "  Homebrew not found - see https://brew.sh"; exit 1; fi
        # MoltenVK is the Vulkan implementation on macOS; vulkan-loader is what
        # pkg-config finds, and shaderc provides glslc.
        brew install pkg-config sdl3 molten-vk vulkan-loader shaderc
        ;;
      Linux)
        if   command -v apt-get >/dev/null 2>&1; then
            sudo apt-get update && sudo apt-get install -y \
                build-essential pkg-config libsdl2-dev \
                libvulkan-dev glslc
        elif command -v dnf >/dev/null 2>&1; then
            sudo dnf install -y gcc-c++ make pkgconf-pkg-config \
                SDL2-devel vulkan-loader-devel glslc
        elif command -v pacman >/dev/null 2>&1; then
            sudo pacman -S --needed base-devel pkgconf sdl2 vulkan-icd-loader shaderc
        else
            echo "  unknown package manager - install: pkg-config, SDL2 or SDL3,"
            echo "  the Vulkan loader and shaderc (glslc)"; exit 1
        fi
        ;;
      *) echo "  unsupported platform $OS - install SDL, the Vulkan loader and shaderc by hand";;
    esac
    echo
    echo "re-checking:"; exec "$0"
fi

echo
echo "$ok ok, $warn optional missing, $miss broken"
if [ "$miss" -gt 0 ]; then
    echo "Fix the BROKEN entries above; the optional ones only disable viewers."
    exit 1
fi
[ "$warn" -gt 0 ] && [ "$INSTALL" = "0" ] && \
    echo "Run 'scripts/install-deps.sh --install' to add the optional ones."
exit 0

#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# THE NINTENDO 3DS TOOLCHAIN, built from source - no pacman, no sudo
# (`todo/3ds-port.md` step 0).
#
#     scripts/3ds-toolchain.sh            build what is missing (an hour the
#                                         first time, most of it GCC)
#     scripts/3ds-toolchain.sh --check    say what is there, build nothing
#
# Into $DEVKITPRO, default ~/devkitpro - the layout devkitPro's own install
# has under /opt/devkitpro, so `engine/backends/n3ds/Makefile` takes either:
#
#   devkitARM/        GCC, binutils and newlib for the ARM11, devkitPro's
#                     patches applied - built by devkitPro's OWN buildscripts
#                     (github.com/devkitPro/buildscripts, the tag below), and
#                     with them devkitARM's rules and crt0s
#   tools/bin/        3dsxtool and smdhtool (3dstools), bin2s (general-tools),
#                     picasso (the PICA200 shader assembler, for step 3)
#   libctru/          the system library
#                     ...and citro3d, the GPU library (step 3), installed
#                     beside it as devkitPro's own package does
#
# **What devkitPro asks, and this keeps to it.** devkitPro maintains these
# toolchains and asks users to install them with its pacman where possible
# (devkitpro.org/wiki/devkitPro_pacman); the buildscripts are provided "as a
# courtesy", and a toolchain built with them is "for personal use only and may
# not be distributed by entities other than devkitPro". So: this builds for
# the machine it runs on, nothing it builds is ever committed or shipped, and
# a pacman install (`sudo dkp-pacman -S 3ds-dev`) is the better route for
# anyone who can use one - the Makefile does not care which it finds.
#
# Needs: a C/C++ compiler, make, curl or wget, tar, and autoconf, automake
# and libtool for the three host-tool packages (Homebrew: `brew install
# autoconf automake libtool`). Nothing here is a prerequisite of `make` or of
# verify.py (PORTING A1): a machine without it builds and checks everything
# but the 3DS programs.
#
# Every step leaves a stamp in the work directory and is skipped when its
# stamp is there, so an interrupted run resumes where it stopped.
set -eu

PREFIX="${DEVKITPRO:-$HOME/devkitpro}"
WORK="${OMK_3DS_WORK:-$HOME/.cache/omk-3ds-toolchain}"

# The versions, pinned: a toolchain that moves under the port is a build that
# changes without a commit. Raise them here, deliberately.
DKA_TAG=devkitARM_r67          # devkitPro/buildscripts
GENERAL_TOOLS=v1.4.4           # devkitPro/general-tools (bin2s)
TOOLS_3DS=v1.3.1               # devkitPro/3dstools (3dsxtool, smdhtool)
PICASSO=v2.7.1                 # devkitPro/picasso
LIBCTRU=v2.7.0                 # devkitPro/libctru
CITRO3D=v1.7.1                 # devkitPro/citro3d

export DEVKITPRO="$PREFIX"
export DEVKITARM="$PREFIX/devkitARM"
export PATH="$DEVKITARM/bin:$PREFIX/tools/bin:$PATH"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

have() { [ -e "$1" ]; }
report() {
    printf '%-12s %s\n' "$1" "$2"
}

check() {
    report "prefix" "$PREFIX"
    have "$DEVKITARM/bin/arm-none-eabi-g++" && report devkitARM "$("$DEVKITARM/bin/arm-none-eabi-g++" -dumpversion) ($DEVKITARM)" || report devkitARM "MISSING"
    have "$DEVKITARM/3ds_rules"             && report rules "$DEVKITARM/3ds_rules" || report rules "MISSING"
    for t in 3dsxtool smdhtool bin2s picasso; do
        have "$PREFIX/tools/bin/$t" && report "$t" "$PREFIX/tools/bin/$t" || report "$t" "MISSING"
    done
    have "$PREFIX/libctru/lib/libctru.a"    && report libctru "$PREFIX/libctru" || report libctru "MISSING"
    have "$PREFIX/libctru/lib/libcitro3d.a" && report citro3d "$PREFIX/libctru (beside libctru)" || report citro3d "MISSING"
}

if [ "${1:-}" = "--check" ]; then
    check
    exit 0
fi

fetch() {   # url file
    if [ -f "$2" ]; then return 0; fi
    echo "fetch: $1" >&2
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 -o "$2.part" "$1"
    else
        wget -O "$2.part" "$1"
    fi
    mv "$2.part" "$2"
}

# A GitHub source tag, fetched and unpacked once -> prints the source directory.
github_src() {   # repo tag
    tarball="$WORK/archives/$1-$2.tar.gz"
    fetch "https://github.com/devkitPro/$1/archive/refs/tags/$2.tar.gz" "$tarball"
    dir="$WORK/src/$1-$2"
    if [ ! -d "$dir" ]; then
        mkdir -p "$WORK/src/tmp-$1"
        tar -xzf "$tarball" -C "$WORK/src/tmp-$1"
        mv "$WORK/src/tmp-$1/"* "$dir"
        rmdir "$WORK/src/tmp-$1"
    fi
    echo "$dir"
}

stamp_done() { [ -f "$WORK/stamps/$1" ]; }
stamp() { mkdir -p "$WORK/stamps"; touch "$WORK/stamps/$1"; }

mkdir -p "$WORK/archives" "$WORK/src" "$PREFIX"

# ---- 1. devkitARM, by devkitPro's buildscripts -----------------------------
if ! stamp_done "devkitarm-$DKA_TAG"; then
    src="$(github_src buildscripts "$DKA_TAG")"
    cd "$src"
    # Unattended, devkitARM, the downloads cached beside the sources.
    cat > config.sh <<EOF
BUILD_DKPRO_PACKAGE=1
BUILD_DKPRO_AUTOMATED=1
BUILD_DKPRO_SRCDIR="$WORK/archives"
BUILD_DKPRO_SKIP_CRTLS=1
export MAKEFLAGS="\$MAKEFLAGS -j$JOBS MAKEINFO=true"
EOF
    # The install directory is not a setting - the script writes
    # /opt/devkitpro, which needs root - so it is rewritten here, and the
    # rewrite is CHECKED: an edit that does not apply would leave the build
    # running towards /opt and failing an hour in (CLAUDE.md 1, a mutation
    # that does not apply passes).
    sed "s|^INSTALLDIR=/opt/devkitpro\$|INSTALLDIR=\"$PREFIX\"|" build-devkit.sh > build-devkit.omk.sh
    if ! grep -q "^INSTALLDIR=\"$PREFIX\"\$" build-devkit.omk.sh; then
        echo "3ds-toolchain: build-devkit.sh ($DKA_TAG) no longer sets INSTALLDIR=/opt/devkitpro" \
             "on a line of its own - read it and update this script" >&2
        exit 1
    fi
    chmod +x build-devkit.omk.sh
    # THE SOURCES, FROM WHERE THEY ARE PUBLISHED. The buildscripts fetch every
    # archive from downloads.devkitpro.org, which redirects to a host behind
    # Cloudflare that answered 403 to every request from the machine this was
    # written on (2026-10-06: curl, wget, the scripts' own user agent, IPv4 and
    # IPv6). They skip any archive already in BUILD_DKPRO_SRCDIR, so the same
    # files are put there first from their own origins - binutils and GCC from
    # GNU, newlib from sourceware, devkitPro's rules and crt0s from its GitHub
    # (whose tarballs unpack to the `devkitarm-rules-1.6.1/` the scripts cd
    # into). The versions are READ from this tag's scripts, never restated
    # here, so they cannot drift from the patches the tag carries.
    block() { sed -n '/"1" )/,/;;/p' select_toolchain.sh | sed -n "s/^ *$1=//p" | head -1; }
    GCC_VER="$(block GCC_VER)"
    BINUTILS_VER="$(block BINUTILS_VER)"
    NEWLIB_VER="$(block NEWLIB_VER)"
    RULES_VER="$(sed -n 's/^DKARM_RULES_VER=//p' build-devkit.sh)"
    CRTLS_VER="$(sed -n 's/^DKARM_CRTLS_VER=//p' build-devkit.sh)"
    for v in "$GCC_VER" "$BINUTILS_VER" "$NEWLIB_VER" "$RULES_VER" "$CRTLS_VER"; do
        [ -n "$v" ] || { echo "3ds-toolchain: could not read a version out of $DKA_TAG's scripts" >&2; exit 1; }
    done
    echo "devkitARM $DKA_TAG: gcc $GCC_VER, binutils $BINUTILS_VER, newlib $NEWLIB_VER," \
         "rules $RULES_VER, crtls $CRTLS_VER"
    A="$WORK/archives"
    fetch "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VER.tar.xz" "$A/binutils-$BINUTILS_VER.tar.xz"
    fetch "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VER/gcc-$GCC_VER.tar.xz" "$A/gcc-$GCC_VER.tar.xz"
    fetch "https://sourceware.org/pub/newlib/newlib-$NEWLIB_VER.tar.gz" "$A/newlib-$NEWLIB_VER.tar.gz"
    fetch "https://github.com/devkitPro/devkitarm-rules/archive/refs/tags/v$RULES_VER.tar.gz" \
          "$A/devkitarm-rules-$RULES_VER.tar.gz"
    fetch "https://github.com/devkitPro/devkitarm-crtls/archive/refs/tags/v$CRTLS_VER.tar.gz" \
          "$A/devkitarm-crtls-$CRTLS_VER.tar.gz"
    # GCC'S OWN PREREQUISITES - GMP, MPFR, MPC and isl. This tag's scripts
    # expect them installed on the host (only a cross build passes
    # `--with-gmp`); GCC's supported alternative is `contrib/
    # download_prerequisites`, which fetches their sources from gcc.gnu.org,
    # checks them against GCC's own SHA-512 list, and links them into the
    # source tree so they are built with it - no host package needed. So the
    # GCC tree is unpacked HERE, where the scripts would unpack it (their
    # build directory is `.devkitARM`, their stamp `extracted-gcc-<ver>`, and
    # they skip an unpacked tree), and the prerequisites are put in it.
    BD="$src/.devkitARM"
    mkdir -p "$BD"
    if [ ! -f "$BD/extracted-gcc-$GCC_VER" ]; then
        echo "extracting gcc-$GCC_VER (for its prerequisites)"
        tar -xf "$A/gcc-$GCC_VER.tar.xz" -C "$BD"
        touch "$BD/extracted-gcc-$GCC_VER"
    fi
    if [ ! -e "$BD/gcc-$GCC_VER/gmp" ]; then
        (cd "$BD/gcc-$GCC_VER" && ./contrib/download_prerequisites)
    fi
    [ -e "$BD/gcc-$GCC_VER/gmp" ] && [ -e "$BD/gcc-$GCC_VER/mpfr" ] && [ -e "$BD/gcc-$GCC_VER/mpc" ] || {
        echo "3ds-toolchain: GCC's prerequisites are not in its tree" >&2; exit 1; }
    # THE BUILD MACHINE'S NAME. The scripts configure with `--build=` from
    # their own `config.guess`, which is old enough to call an Apple-silicon
    # Mac `arm-apple-darwin` - and GCC's host table matches `aarch64*-*-darwin*`,
    # so it then builds no `host-aarch64-darwin.o` and stage 1 fails to link
    # with `host_hooks` undefined (2026-10-06, an M1). GCC's own `config.guess`
    # is current; it replaces theirs.
    cp "$BD/gcc-$GCC_VER/config.guess" ./config.guess
    echo "build machine: $(./config.guess)"
    if [ "$(uname -m)" = arm64 ] && ! ./config.guess | grep -q '^aarch64-'; then
        echo "3ds-toolchain: config.guess does not name this Mac aarch64" >&2; exit 1
    fi
    # A MACOS HOST: binutils (and GCC) bundle an old zlib whose `zutil.h`
    # defines `fdopen` as NULL under `TARGET_OS_MAC`, which today's SDK
    # headers set - and the macro then breaks the SDK's own `stdio.h`
    # declaration of it (binutils 2.45.1 on SDK 26.5, 2026-10-06). zlib only
    # defines it `#ifndef fdopen`, so naming it as itself leaves the real
    # function alone. The configure lines take `$CPPFLAGS` from the
    # environment.
    if [ "$(uname -s)" = Darwin ]; then
        export CPPFLAGS="${CPPFLAGS:-} -Dfdopen=fdopen"
    fi
    # THE PATCHES ARE GIT DIFFS, and macOS's `patch` (Apple's 2.0) does not
    # create the EMPTY file newlib's adds (`libgloss/libsysbase/dummy.c`, a
    # git "new file" with no hunk): the build then stops on "no rule to make
    # target libsysbase/dummy.c" (2026-10-06). The scripts take `$PATCH` from
    # the environment and call it as `-p1 -d <dir> -i <patch>`; this hands
    # them `git apply` behind that interface, which reads a git diff whole -
    # and refuses one that does not apply, rather than half-applying it.
    mkdir -p "$WORK/bin"
    cat > "$WORK/bin/git-patch" <<'GITPATCH'
#!/bin/sh
# `patch -p1 -d DIR -i FILE` as `git apply` (scripts/3ds-toolchain.sh)
strip=-p1 dir=. file=
while [ $# -gt 0 ]; do
    case "$1" in
        -p*) strip=$1 ;;
        -d) dir=$2; shift ;;
        -i) file=$2; shift ;;
        *) echo "git-patch: unsupported argument $1" >&2; exit 2 ;;
    esac
    shift
done
case "$file" in /*) ;; *) file="$PWD/$file" ;; esac
cd "$dir" && exec git apply "$strip" --whitespace=nowarn "$file"
GITPATCH
    chmod +x "$WORK/bin/git-patch"
    export PATCH="$WORK/bin/git-patch"
    # no Texinfo is needed: the manuals are not built (MAKEINFO=true is also
    # in MAKEFLAGS above, where it overrides the Makefiles' own variable)
    MAKEINFO=true ./build-devkit.omk.sh
    have "$DEVKITARM/bin/arm-none-eabi-g++" || { echo "3ds-toolchain: devkitARM did not install" >&2; exit 1; }
    # stamped at once: the scripts DELETE their build directory when they
    # finish, so a later step that failed would otherwise rebuild GCC
    stamp "devkitarm-$DKA_TAG"
fi

# ---- 1b. the rules and the crt0s ---------------------------------------------
# Their Makefiles install to a FIXED `$(DESTDIR)/opt/devkitpro/devkitARM` -
# root again - so the scripts' own step is skipped (BUILD_DKPRO_SKIP_CRTLS
# above) and done here the same way, `make install`, into a staging DESTDIR
# whose `opt/devkitpro` is then copied into the prefix. The rules first: the
# crt0s' Makefile includes `$DEVKITARM/base_rules`. The versions are the
# pinned tag's, read again from its scripts.
if ! stamp_done "devkitarm-rules-$DKA_TAG"; then
    src="$(github_src buildscripts "$DKA_TAG")"
    RULES_VER="$(sed -n 's/^DKARM_RULES_VER=//p' "$src/build-devkit.sh")"
    CRTLS_VER="$(sed -n 's/^DKARM_CRTLS_VER=//p' "$src/build-devkit.sh")"
    [ -n "$RULES_VER" ] && [ -n "$CRTLS_VER" ] || {
        echo "3ds-toolchain: could not read the rules / crtls versions" >&2; exit 1; }
    for pkg in "devkitarm-rules-$RULES_VER" "devkitarm-crtls-$CRTLS_VER"; do
        stage="$WORK/stage/$pkg"
        rm -rf "$stage" "$WORK/src/$pkg"
        tar -xzf "$WORK/archives/$pkg.tar.gz" -C "$WORK/src"
        # DEPSDIR: `base_rules` writes each dependency file to `$(DEPSDIR)/`,
        # which devkitPro's project templates set and this Makefile does
        # not - empty, the crt0s' land in `/` and the build stops there
        make -C "$WORK/src/$pkg" install DESTDIR="$stage" DEPSDIR="$WORK/src/$pkg"
        [ -d "$stage/opt/devkitpro" ] || { echo "3ds-toolchain: $pkg installed nothing under opt/devkitpro" >&2; exit 1; }
        cp -R "$stage/opt/devkitpro/." "$PREFIX/"
    done
    have "$DEVKITARM/3ds_rules" && have "$DEVKITARM/arm-none-eabi/lib/3dsx.specs" || {
        echo "3ds-toolchain: the rules or the 3DS crt0 did not install" >&2; exit 1; }
    stamp "devkitarm-rules-$DKA_TAG"
fi

# ---- 2. the host tools (autotools packages) --------------------------------
host_tool() {   # repo tag
    if stamp_done "$1-$2"; then return 0; fi
    src="$(github_src "$1" "$2")"
    cd "$src"
    if [ -x ./autogen.sh ]; then ./autogen.sh; else autoreconf -fi; fi
    ./configure --prefix="$PREFIX/tools"
    make -j"$JOBS"
    make install
    stamp "$1-$2"
}
host_tool general-tools "$GENERAL_TOOLS"
host_tool 3dstools "$TOOLS_3DS"
host_tool picasso "$PICASSO"

# ---- 3. the libraries, with devkitARM's own rules ----------------------------
if ! stamp_done "libctru-$LIBCTRU"; then
    src="$(github_src libctru "$LIBCTRU")"
    make -C "$src/libctru" -j"$JOBS" install
    have "$PREFIX/libctru/lib/libctru.a" || { echo "3ds-toolchain: libctru did not install" >&2; exit 1; }
    stamp "libctru-$LIBCTRU"
fi
if ! stamp_done "citro3d-$CITRO3D"; then
    src="$(github_src citro3d "$CITRO3D")"
    make -C "$src" -j"$JOBS" install
    have "$PREFIX/libctru/lib/libcitro3d.a" || { echo "3ds-toolchain: citro3d did not install" >&2; exit 1; }
    stamp "citro3d-$CITRO3D"
fi

echo
check
echo
echo "Done. To build the 3DS programs:"
echo "    export DEVKITPRO=$PREFIX DEVKITARM=$PREFIX/devkitARM"
echo "    make -C engine/backends/n3ds"

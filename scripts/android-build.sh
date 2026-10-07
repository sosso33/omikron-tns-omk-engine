#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# THE ANDROID / META QUEST APK - `todo/quest-port.md` §5 step 4.
#
#     scripts/android-build.sh            # -> engine/build/android/omk.apk
#     scripts/android-build.sh install    # ...and `adb install -r` it
#
# No Gradle and no Android Studio: CMake with the NDK's toolchain file builds
# libSDL2.so and libmain.so, javac + d8 compile SDL's Java glue and
# OMKActivity, aapt2 links the manifest, zipalign + apksigner finish it with a
# local debug key. Everything it writes is under engine/build/android/.
#
# THE TOOLCHAIN IS FOUND, NOT ASSUMED - it differs per machine (the M1 had no
# NDK at all; the M3 has two Unity installs that each bundle NDK r27c, the SDK
# platforms, build-tools and a JDK). In order, the first that exists:
#   NDK         $ANDROID_NDK_HOME, $ANDROID_SDK/ndk/<newest>, a Unity editor's
#               PlaybackEngines/AndroidPlayer/NDK
#   SDK         $ANDROID_SDK (or $ANDROID_HOME), ~/Library/Android/sdk, Unity's
#               SDK - whichever holds an android.jar AND build-tools
#   JDK         $JAVA_HOME, Unity's OpenJDK, the system java
# SDL2's source (the Mac's version, 2.32.10) is fetched once into
# engine/build/android/.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
ENGINE=$REPO/engine
OUT=$ENGINE/build/android
SDL_VER=2.32.10
API=34                     # compileSdk: the android.jar SDL's Java needs
mkdir -p "$OUT"

shopt -s nullglob
# bash 3.2 (macOS /bin/bash) calls an EMPTY array unbound under `set -u`, so
# every expansion of one that may be empty is written ${a[@]+"${a[@]}"}
unity=(/Applications/Unity/Hub/Editor/*/PlaybackEngines/AndroidPlayer)

# ---- the NDK
NDK=${ANDROID_NDK_HOME:-}
if [[ -z $NDK ]]; then
    for sdk in "${ANDROID_SDK:-${ANDROID_HOME:-}}" "$HOME/Library/Android/sdk"; do
        [[ -n $sdk ]] || continue
        cands=("$sdk"/ndk/*)
        (( ${#cands[@]} )) && { NDK=${cands[${#cands[@]}-1]}; break; }
    done
fi
if [[ -z $NDK ]]; then
    for u in ${unity[@]+"${unity[@]}"}; do [[ -f $u/NDK/build/cmake/android.toolchain.cmake ]] && NDK=$u/NDK; done
fi
[[ -n $NDK && -f $NDK/build/cmake/android.toolchain.cmake ]] || {
    echo "no Android NDK found: set ANDROID_NDK_HOME, or install one (sdkmanager 'ndk;27.2.12479018')" >&2; exit 1; }

# ---- the SDK: an android.jar at $API (or newer) and build-tools
SDK=""; JAR=""; BT=""
for sdk in "${ANDROID_SDK:-${ANDROID_HOME:-}}" "$HOME/Library/Android/sdk" ${unity[@]+"${unity[@]/%//SDK}"}; do
    [[ -n $sdk && -d $sdk ]] || continue
    jars=("$sdk"/platforms/android-*/android.jar)
    bts=("$sdk"/build-tools/*/apksigner)
    for j in ${jars[@]+"${jars[@]}"}; do
        v=${j%/android.jar}; v=${v##*-}
        [[ $v =~ ^[0-9]+$ ]] && (( v >= API )) && { JAR=$j; break; }
    done
    if [[ -n $JAR && ${#bts[@]} -gt 0 ]]; then SDK=$sdk; BT=$(dirname "${bts[${#bts[@]}-1]}"); break; fi
    JAR=""
done
[[ -n $SDK ]] || { echo "no Android SDK with platforms/android-$API+ and build-tools found" >&2; exit 1; }

# ---- the JDK
JAVA_BIN=""
[[ -n ${JAVA_HOME:-} && -x $JAVA_HOME/bin/javac ]] && JAVA_BIN=$JAVA_HOME/bin
if [[ -z $JAVA_BIN ]]; then
    for u in ${unity[@]+"${unity[@]}"}; do [[ -x $u/OpenJDK/bin/javac ]] && JAVA_BIN=$u/OpenJDK/bin; done
fi
# build-tools 34's d8 (R8 8.2) dies with a NullPointerException on JDK 23 (the
# M1's default java, 2026-10-07), so a macOS JDK it was built for comes first
if [[ -z $JAVA_BIN && -x /usr/libexec/java_home ]]; then
    for v in 17 21 11; do
        h=$(/usr/libexec/java_home -F -v $v 2>/dev/null) && [[ -x $h/bin/javac ]] && { JAVA_BIN=$h/bin; break; }
    done
fi
[[ -n $JAVA_BIN ]] || JAVA_BIN=$(dirname "$(command -v javac)")
# d8 and apksigner are shell scripts that run the FIRST `java` on PATH
export PATH="$JAVA_BIN:$PATH"

CMAKE=$(command -v cmake || true)
[[ -n $CMAKE ]] || for u in ${unity[@]+"${unity[@]}"}; do c=("$u"/SDK/cmake/*/bin/cmake); (( ${#c[@]} )) && CMAKE=${c[0]}; done
GEN=(); command -v ninja >/dev/null && GEN=(-G Ninja)

echo "NDK   $NDK ($(grep Pkg.Revision "$NDK/source.properties" | cut -d' ' -f3))"
echo "SDK   $SDK  (android.jar $JAR, build-tools $BT)"
echo "JDK   $JAVA_BIN ($("$JAVA_BIN/java" -version 2>&1 | head -1))"

# ---- SDL2's source
SDL=$OUT/SDL2-$SDL_VER
if [[ ! -f $SDL/CMakeLists.txt ]]; then
    curl -fsSL -o "$OUT/SDL2-$SDL_VER.tar.gz" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz"
    tar xzf "$OUT/SDL2-$SDL_VER.tar.gz" -C "$OUT"
fi

# ---- the OpenXR loader (step 5): Khronos's AAR from Maven Central, unpacked
XR_VER=1.1.63
XR=$OUT/openxr-$XR_VER
if [[ ! -f $XR/prefab/modules/headers/include/openxr/openxr.h ]]; then
    curl -fsSL -o "$OUT/openxr_loader-$XR_VER.aar" \
        "https://repo1.maven.org/maven2/org/khronos/openxr/openxr_loader_for_android/$XR_VER/openxr_loader_for_android-$XR_VER.aar"
    mkdir -p "$XR" && (cd "$XR" && unzip -qo "../openxr_loader-$XR_VER.aar")
fi

# ---- the native libraries
"$CMAKE" -S "$ENGINE/backends/android" -B "$OUT/cmake" ${GEN[@]+"${GEN[@]}"} \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release -DOMK_SDL2_SRC="$SDL" -DOMK_OPENXR_DIR="$XR" >/dev/null
"$CMAKE" --build "$OUT/cmake" -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

STAGE=$OUT/apk
rm -rf "$STAGE"; mkdir -p "$STAGE/lib/arm64-v8a" "$STAGE/classes" "$STAGE/dex"
cp "$OUT/cmake/libmain.so" "$OUT/cmake/sdl2/libSDL2.so" \
   "$XR/prefab/modules/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so" "$STAGE/lib/arm64-v8a/"
# THE C++ RUNTIME IS STATIC, and must stay so: the profiler replaces the global
# `operator new`/`delete` (`platform/profile.cpp`, a header on every block). A
# `libc++_shared.so` loaded before `libmain.so` keeps the system allocator for
# its own out-of-line code (std::string's, among others), so a block made on one
# side was freed on the other - the first device run (2026-10-07) corrupted an
# argument string and died in `free()` inside the first table load.
# stripped in the package; engine/build/android/cmake/libmain.so keeps the
# symbols, for `ndk-stack -sym engine/build/android/cmake` over a logcat crash
STRIP=("$NDK"/toolchains/llvm/prebuilt/*/bin/llvm-strip)
"${STRIP[0]}" --strip-unneeded "$STAGE"/lib/arm64-v8a/lib{main,SDL2}.so

# ---- the Java: SDL's glue and OMKActivity
"$JAVA_BIN/javac" --release 11 -nowarn -encoding UTF-8 -classpath "$JAR" -d "$STAGE/classes" \
    $(find "$SDL/android-project/app/src/main/java" "$ENGINE/backends/android/java" -name '*.java') \
    2> >(grep -v '^Note:' >&2)
"$BT/d8" --release --min-api 29 --lib "$JAR" --output "$STAGE/dex" \
    $(find "$STAGE/classes" -name '*.class')

# ---- the package
"$BT/aapt2" link -o "$STAGE/unsigned.apk" -I "$JAR" \
    --manifest "$ENGINE/backends/android/AndroidManifest.xml" \
    --min-sdk-version 29 --target-sdk-version 32
( cd "$STAGE/dex" && zip -q "$STAGE/unsigned.apk" classes.dex )
( cd "$STAGE" && zip -qr unsigned.apk lib )
"$BT/zipalign" -f -p 4 "$STAGE/unsigned.apk" "$STAGE/aligned.apk"
KEY=$OUT/debug.keystore
[[ -f $KEY ]] || "$JAVA_BIN/keytool" -genkeypair -keystore "$KEY" -storepass android -keypass android \
    -alias omkdebug -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=OMK debug" >/dev/null 2>&1
"$BT/apksigner" sign --ks "$KEY" --ks-pass pass:android --ks-key-alias omkdebug \
    --out "$OUT/omk.apk" "$STAGE/aligned.apk"
echo "built $OUT/omk.apk ($(du -h "$OUT/omk.apk" | cut -f1))"

if [[ ${1:-} == install ]]; then
    adb install -r "$OUT/omk.apk"
    echo "data: adb push gamedata tables /sdcard/Android/data/org.omk.play/files/"
fi

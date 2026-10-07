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
    for u in "${unity[@]}"; do [[ -f $u/NDK/build/cmake/android.toolchain.cmake ]] && NDK=$u/NDK; done
fi
[[ -n $NDK && -f $NDK/build/cmake/android.toolchain.cmake ]] || {
    echo "no Android NDK found: set ANDROID_NDK_HOME, or install one (sdkmanager 'ndk;27.2.12479018')" >&2; exit 1; }

# ---- the SDK: an android.jar at $API (or newer) and build-tools
SDK=""; JAR=""; BT=""
for sdk in "${ANDROID_SDK:-${ANDROID_HOME:-}}" "$HOME/Library/Android/sdk" "${unity[@]/%//SDK}"; do
    [[ -n $sdk && -d $sdk ]] || continue
    jars=("$sdk"/platforms/android-*/android.jar)
    bts=("$sdk"/build-tools/*/apksigner)
    for j in "${jars[@]}"; do
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
    for u in "${unity[@]}"; do [[ -x $u/OpenJDK/bin/javac ]] && JAVA_BIN=$u/OpenJDK/bin; done
fi
[[ -n $JAVA_BIN ]] || JAVA_BIN=$(dirname "$(command -v javac)")

CMAKE=$(command -v cmake || true)
[[ -n $CMAKE ]] || for u in "${unity[@]}"; do c=("$u"/SDK/cmake/*/bin/cmake); (( ${#c[@]} )) && CMAKE=${c[0]}; done
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

# ---- the native libraries
"$CMAKE" -S "$ENGINE/backends/android" -B "$OUT/cmake" "${GEN[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release -DOMK_SDL2_SRC="$SDL" >/dev/null
"$CMAKE" --build "$OUT/cmake" -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

STAGE=$OUT/apk
rm -rf "$STAGE"; mkdir -p "$STAGE/lib/arm64-v8a" "$STAGE/classes" "$STAGE/dex"
cp "$OUT/cmake/libmain.so" "$OUT/cmake/sdl2/libSDL2.so" "$STAGE/lib/arm64-v8a/"
STL=$(find "$NDK/toolchains/llvm/prebuilt" -path '*aarch64-linux-android/libc++_shared.so' | head -1)
cp "$STL" "$STAGE/lib/arm64-v8a/"
# stripped in the package; engine/build/android/cmake/libmain.so keeps the
# symbols, for `ndk-stack -sym engine/build/android/cmake` over a logcat crash
STRIP=("$NDK"/toolchains/llvm/prebuilt/*/bin/llvm-strip)
"${STRIP[0]}" --strip-unneeded "$STAGE"/lib/arm64-v8a/*.so

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

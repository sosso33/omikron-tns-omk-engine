# SPDX-License-Identifier: GPL-3.0-or-later
# THE ANDROID / META QUEST BUILD - `todo/quest-port.md` §5 step 4.
#
#     scripts/android-build.sh          # finds the NDK, fetches SDL, builds the APK
#
# Configured by that script with the NDK's own toolchain file
# (`-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake
#  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DOMK_SDL2_SRC=...`).
#
# THIS IS NOT THE MAKEFILE'S BUILD, and must not become a dependency of it
# (`PORTING` A8 rule 1), exactly as `backends/vita/CMakeLists.txt` is not. It
# compiles the SAME `src/*/*.cpp` the host does, and the viewer's sources by
# the Makefile's own rule: every `backends/sdl/*.cpp` except the other GPU
# windows and the unused half of the instruments, plus `backends/vr/*.cpp`.
#
# Two libraries, the pair `SDLActivity` loads by name: `libSDL2.so` (SDL built
# from its source, `OMK_SDL2_SRC`) and `libmain.so` (the game, through
# `android_main.cpp`, on the GLES backend). OMK_VR=1, as the Makefile defines
# it - the Quest is the VR target - and it changes struct layouts, so every
# object of this build carries it.
cmake_minimum_required(VERSION 3.16)
project(omk_android CXX C)

if(NOT DEFINED OMK_SDL2_SRC OR NOT EXISTS ${OMK_SDL2_SRC}/CMakeLists.txt)
  message(FATAL_ERROR "OMK_SDL2_SRC must name an SDL2 source tree (scripts/android-build.sh fetches one)")
endif()

set(ENGINE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# -O2 as the host builds; `-ffp-contract=off` keeps FMA contraction from
# changing a float result against the host's (the Vita's reason, same here:
# AArch64 has a fused multiply-add the compiler would otherwise use)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -ffp-contract=off")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")

# ---- SDL2, from source: libSDL2.so
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${OMK_SDL2_SRC} ${CMAKE_BINARY_DIR}/sdl2)

add_compile_definitions(OMK_VR=1)

# ---- the engine: every ported source, exactly the Makefile's glob
file(GLOB OMK_SRC ${ENGINE}/src/*/*.cpp)
add_library(omk_engine STATIC ${OMK_SRC})
target_include_directories(omk_engine PUBLIC ${ENGINE}/src ${ENGINE}/third_party)

# ---- the viewer: the Makefile's PLAY_SDL_SRCS rule, GLES variant
option(OMK_INSTRUMENTS "build the viewer's harnesses (--money, --fight, --board...)" ON)
option(OMK_RELEASE "a release build: no profiler, no instruments" OFF)
if(OMK_RELEASE)
  add_compile_definitions(OMK_PROFILE=0)
  set(OMK_INSTRUMENTS OFF CACHE BOOL "" FORCE)
endif()
file(GLOB OMK_PLAY_SRC ${ENGINE}/backends/sdl/*.cpp)
list(FILTER OMK_PLAY_SRC EXCLUDE REGEX "/playgpu_[^/]*\\.cpp$")
if(OMK_INSTRUMENTS)
  list(FILTER OMK_PLAY_SRC EXCLUDE REGEX "/playharness_off\\.cpp$")
else()
  list(FILTER OMK_PLAY_SRC EXCLUDE REGEX "/playharness\\.cpp$")
endif()
file(GLOB OMK_VR_SRC ${ENGINE}/backends/vr/*.cpp)

# ---- OpenXR (step 5): the Khronos loader, from its Android AAR
# (`android-build.sh` fetches and unpacks it into OMK_OPENXR_DIR)
if(NOT DEFINED OMK_OPENXR_DIR OR NOT EXISTS ${OMK_OPENXR_DIR}/prefab/modules/headers/include/openxr/openxr.h)
  message(FATAL_ERROR "OMK_OPENXR_DIR must name the unpacked openxr_loader_for_android AAR")
endif()
add_library(openxr_loader SHARED IMPORTED)
set_target_properties(openxr_loader PROPERTIES
  IMPORTED_LOCATION ${OMK_OPENXR_DIR}/prefab/modules/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so
  INTERFACE_INCLUDE_DIRECTORIES ${OMK_OPENXR_DIR}/prefab/modules/headers/include)

add_library(main SHARED
  ${CMAKE_CURRENT_SOURCE_DIR}/android_main.cpp
  ${ENGINE}/backends/openxr/xrhost.cpp
  ${OMK_PLAY_SRC}
  ${ENGINE}/backends/sdl/playgpu_gles.cpp
  ${OMK_VR_SRC}
  ${ENGINE}/backends/gles/glesrender.cpp)
# `play.cpp`'s `main` becomes `omk_play_main`, which `android_main.cpp` calls
# (the Vita's arrangement); SDL_MAIN_HANDLED keeps SDL_main.h's own
# `#define main SDL_main` out of every file - android_main.cpp defines
# SDL_main by name, the symbol SDLActivity looks up.
set_source_files_properties(${ENGINE}/backends/sdl/play.cpp
  PROPERTIES COMPILE_DEFINITIONS "main=omk_play_main")
target_compile_definitions(main PRIVATE OMK_GLES=1 OMK_OPENXR=1 SDL_MAIN_HANDLED)
target_include_directories(main PRIVATE ${OMK_SDL2_SRC}/include ${ENGINE}/backends/sdl)
target_link_libraries(main omk_engine SDL2 openxr_loader GLESv3 EGL log android)

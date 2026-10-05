# SPDX-License-Identifier: GPL-3.0-or-later
# THE NINTENDO 3DS BUILD - `todo/3ds-port.md`. devkitPro's devkitARM, libctru
# (and citro3d from step 3), through devkitPro's own CMake toolchain file:
#
#   cmake -S engine/backends/n3ds -B engine/build/n3ds \
#         -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake
#   cmake --build engine/build/n3ds -j
#
# Two programs, each a `.3dsx` (the Homebrew Launcher's form; the `.cia`, which
# sets its own memory mode, comes with step 4):
#
#   omk_boot   `tools/omk.cpp` - the engine booted HEADLESS, the `OMKBoot` of
#              the classic Mac build: the boot to `traces/intro.log`, 42 of 42
#              in order, is the first thing every port has proved (step 1)
#   omk_play   `backends/sdl/play.cpp` - THE GAME, through `n3ds_main.cpp` and
#              the libctru frontend (`n3dsfront.cpp`), on the software
#              reference until the citro3d backend exists (step 2)
#
# Both carry the TABLES (`tables/*.json`, the one thing a replica cannot read
# out of gamedata/) in their ROMFS, read as `romfs:/`. The game data is on the
# card (`sdmc:/omk/gamedata/`); `n3ds_main.cpp` has the whole layout.
#
# No threads yet (OMK_THREADS 0): every job runs where it is asked for, as on
# the classic Mac. The New 3DS gives an application a second whole core;
# that is step 6's, measured first.
cmake_minimum_required(VERSION 3.13)
project(omk_3ds CXX C)

if(NOT NINTENDO_3DS)
    message(FATAL_ERROR "configure with devkitPro's toolchain file: "
            "-DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake")
endif()

set(ENGINE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(OMK_TABLES ${ENGINE}/../tables)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# A RELEASE build: no profiler and with it no counting allocator (`OMK_PROFILE
# 0`), as `make release` / `classic-release` / `vita-release`. The default
# keeps them, because step 2b's instrument panel reads them.
option(OMK_3DS_RELEASE "a release build: no profiler, no memory counter" OFF)

# ---- the engine: every ported source, exactly the Makefile's glob
file(GLOB OMK_SRC ${ENGINE}/src/*/*.cpp)
add_library(omk_engine STATIC ${OMK_SRC})
target_include_directories(omk_engine PUBLIC ${ENGINE}/src ${ENGINE}/third_party)
target_compile_definitions(omk_engine PUBLIC OMK_THREADS=0)
if(OMK_3DS_RELEASE)
    target_compile_definitions(omk_engine PUBLIC OMK_PROFILE=0)
endif()
# The engine throws (`bad_alloc` is reported, not a silent abort) and uses
# `dynamic_cast` in places; devkitPro's templates turn both off by default.
target_compile_options(omk_engine PUBLIC -O2 -ffunction-sections -fdata-sections
                       -fexceptions -frtti)

# ---- omk_boot: the engine booted headless
add_executable(omk_boot n3ds_main.cpp ${ENGINE}/tools/omk.cpp)
set_source_files_properties(${ENGINE}/tools/omk.cpp PROPERTIES COMPILE_DEFINITIONS main=omk_tool_main)
target_compile_definitions(omk_boot PRIVATE OMK_3DS_ENTRY=omk_tool_main OMK_3DS_TOOL=1)
target_link_libraries(omk_boot omk_engine ctru m)
target_link_options(omk_boot PRIVATE -Wl,--gc-sections)
ctr_generate_smdh(omk_boot.smdh NAME "OMK boot" DESCRIPTION "the engine booted headless"
                  AUTHOR "OMK")
ctr_create_3dsx(omk_boot SMDH omk_boot.smdh ROMFS ${OMK_TABLES})

# ---- omk_play: the game. The viewer's game code is every `backends/sdl/`
# source but the SDL side of the gateway (`platform/frontend.h`): the SDL
# frontend, the GPU glue, the `--scene` set viewer and the instruments' SDL
# half - the same list the classic Mac build takes (`backends/classic/`).
file(GLOB OMK_PLAY ${ENGINE}/backends/sdl/*.cpp)
list(FILTER OMK_PLAY EXCLUDE REGEX "/(sdlfront|playscene|playharness|playgpu_[a-z0-9]+)\\.cpp$")
add_executable(omk_play
    n3ds_main.cpp
    n3dsfront.cpp
    playscene_off.cpp
    ${OMK_PLAY}
    ${ENGINE}/backends/sdl/playgpu_none.cpp
    ${ENGINE}/backends/sdl/playharness_off.cpp)
set_source_files_properties(${ENGINE}/backends/sdl/play.cpp PROPERTIES COMPILE_DEFINITIONS main=omk_play_main)
target_compile_definitions(omk_play PRIVATE OMK_3DS_ENTRY=omk_play_main)
target_include_directories(omk_play PRIVATE ${ENGINE}/backends/sdl ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(omk_play omk_engine ctru m)
target_link_options(omk_play PRIVATE -Wl,--gc-sections)
ctr_generate_smdh(omk_play.smdh NAME "OMK" DESCRIPTION "Omikron: The Nomad Soul, the OMK replica"
                  AUTHOR "OMK")
ctr_create_3dsx(omk_play SMDH omk_play.smdh ROMFS ${OMK_TABLES})

# SPDX-License-Identifier: GPL-3.0-or-later
# THE PS VITA BUILD - `todo/vita-port.md`. VitaSDK, CMake, one VPK a target.
#
#     export VITASDK=~/vitasdk PATH=$VITASDK/bin:$PATH   # vdpm's bootstrap
#     vdpm install vitaGL vitaShaRK libmathneon sdl2_vitagl
#     cmake -S engine/backends/vita -B build-vita
#     cmake --build build-vita -j
#
# THIS IS NOT THE MAKEFILE'S BUILD, and must not become a dependency of it:
# `make` with nothing installed builds every tool and passes the suite
# (`PORTING` A8 rule 1). It lives under `backends/`, which the Makefile does
# not glob, and it compiles the SAME `src/*/*.cpp` the host does - the port is
# one source tree, not a fork.
#
# Three targets, in the order the plan runs them:
#
#   omk_bench   `bench_main.cpp`  - the device factor. CPU only; needs nothing
#                                   but the SDK. Run it FIRST (`handoff-vita.md`
#                                   §2 item 6: measure before optimising).
#   omk_smoke   `smoke_main.cpp`  - one set through the GLES2 backend on
#                                   vitaGL. Needs vitaGL, and on the device the
#                                   shader compiler, `ur0:data/libshacccg.suprx`.
#   omk_vita    `play.cpp`        - THE GAME. OFF by default
#                                   (`-DOMK_VITA_GAME=ON`): `play.cpp` does not
#                                   yet know the GLES backend or the pad - the
#                                   plan's steps F1-F4 are edits to a file
#                                   another session holds.
#
# Built against VitaSDK 2026.08 (GCC 15.2) on 2026-09-18: `omk_bench` and
# `omk_smoke` link and package. Neither has yet run on a console.
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(omk_vita CXX C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

option(OMK_VITA_GAME "build the game itself (needs the play.cpp edits, F1-F4)" OFF)

set(VITA_APP_NAME "OMK")
set(VITA_VERSION  "00.01")
# A homebrew title id: four letters and five digits, unique on the device.
set(OMK_TITLE_BENCH "OMKB00001")
set(OMK_TITLE_SMOKE "OMKS00001")
set(OMK_TITLE_GAME  "OMKE00001")

set(ENGINE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# -O2 as the host builds; the A9 has NEON, which the compiler may use for the
# float loops. `-ffp-contract=off` keeps FMA contraction from changing a
# float result against the host's - the bench's hash is only comparable across
# machines if both compute the same operations (`bench_main.cpp`).
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra -mfpu=neon -mfloat-abi=hard -ffp-contract=off")

# ---- the engine: every ported source, exactly the Makefile's glob
file(GLOB OMK_SRC ${ENGINE}/src/*/*.cpp)
add_library(omk_engine STATIC ${OMK_SRC})
target_include_directories(omk_engine PUBLIC ${ENGINE}/src ${ENGINE}/third_party)
# `platform/threads.cpp` picks its Vita half on `__vita__`, which the SDK's
# compiler defines; OMK_THREADS stays 1.

# ---- omk_bench: the device factor
add_executable(omk_bench ${CMAKE_CURRENT_SOURCE_DIR}/bench_main.cpp)
target_link_libraries(omk_bench omk_engine ScePower_stub)
vita_create_self(omk_bench.self omk_bench UNSAFE)
vita_create_vpk(omk_bench.vpk ${OMK_TITLE_BENCH} omk_bench.self
  VERSION ${VITA_VERSION} NAME "OMK bench")

# ---- omk_smoke: the GLES2 backend on vitaGL
set(OMK_VITAGL_LIBS
  vitaGL vitashark SceShaccCgExt mathneon taihen_stub
  SceShaccCg_stub SceKernelDmacMgr_stub SceGxm_stub SceDisplay_stub
  SceSysmodule_stub SceAppMgr_stub SceCommonDialog_stub SceIme_stub
  SceCtrl_stub ScePower_stub m)
add_executable(omk_smoke
  ${CMAKE_CURRENT_SOURCE_DIR}/smoke_main.cpp
  ${ENGINE}/backends/gles/glesrender.cpp)
target_link_libraries(omk_smoke omk_engine ${OMK_VITAGL_LIBS})
vita_create_self(omk_smoke.self omk_smoke UNSAFE)
vita_create_vpk(omk_smoke.vpk ${OMK_TITLE_SMOKE} omk_smoke.self
  VERSION ${VITA_VERSION} NAME "OMK smoke")

# ---- omk_vita: the game, once play.cpp can take it
if(OMK_VITA_GAME)
  add_executable(omk_vita
    ${ENGINE}/backends/sdl/play.cpp
    ${ENGINE}/backends/gles/glesrender.cpp)
  # OMK_GLES selects the GLES renderer in play.cpp the way OMK_VULKAN
  # selects Vulkan - the define F1 adds there.
  target_compile_definitions(omk_vita PRIVATE OMK_GLES=1)
  target_link_libraries(omk_vita omk_engine SDL2 ${OMK_VITAGL_LIBS}
    SceAudio_stub SceTouch_stub SceHid_stub SceMotion_stub pthread)
  vita_create_self(eboot.bin omk_vita UNSAFE)
  vita_create_vpk(omk_vita.vpk ${OMK_TITLE_GAME} eboot.bin
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME})
endif()

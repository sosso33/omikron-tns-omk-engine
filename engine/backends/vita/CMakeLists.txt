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
#   omk_vita    `play.cpp`        - THE GAME, through `vita_main.cpp`, on the
#                                   GLES backend and SDL2 on vitaGL. ON by
#                                   default; `-DOMK_VITA_GAME=OFF` skips it.
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

option(OMK_VITA_GAME "build the game itself (omk_vita.vpk)" ON)

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
# `-DOMK_VITA_ASSERTS=ON`: libstdc++'s bounds checks (vector / string / span
# indexing), so an out-of-range write ABORTS at its line with a message instead
# of corrupting newlib's heap silently - the Vita has no sanitizer.
option(OMK_VITA_ASSERTS "libstdc++ bounds checks, for hunting a heap overwrite" OFF)
if(OMK_VITA_ASSERTS)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_ASSERTIONS")
endif()
# ROOM FOR THE SCE DATA. `vita-elf-create` appends the module's import stubs
# at the END of the code segment, and the data segment starts at the next
# 64 KiB boundary after it - so whether they fit is luck of where the code
# ends. The game missed by 3.9 KiB on 2026-09-18 ("Cannot allocate 4280 bytes
# for SCE data at end of segment 0; segment 1 overlaps"; the same fault as
# vitasdk/buildscripts#186). The SDK's own linker script reserves
# `__sce_headroom` bytes there when the symbol is defined; 16 KiB, for every
# target, so a build cannot start failing because some code grew.
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--defsym,__sce_headroom=0x4000")
# `%zu` - the VitaSDK's newlib has no C99 printf formats, so `%zu` prints "zu"
# and shifts every later argument (found in Vita3K, 2026-09-18; a `%s` after
# it reads a size as a pointer). Every target links `printf_c99.cpp` and routes
# the printf family through it (`c99format.h`).
set(OMK_PRINTF_WRAP printf fprintf snprintf sprintf vprintf vfprintf vsnprintf vsprintf puts putchar)
foreach(fn ${OMK_PRINTF_WRAP})
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--wrap=${fn}")
endforeach()
set(OMK_VITA_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/printf_c99.cpp)

# ---- the engine: every ported source, exactly the Makefile's glob
file(GLOB OMK_SRC ${ENGINE}/src/*/*.cpp)
add_library(omk_engine STATIC ${OMK_SRC})
target_include_directories(omk_engine PUBLIC ${ENGINE}/src ${ENGINE}/third_party)
# `platform/threads.cpp` picks its Vita half on `__vita__`, which the SDK's
# compiler defines; OMK_THREADS stays 1.

# ---- omk_bench: the device factor
add_executable(omk_bench ${CMAKE_CURRENT_SOURCE_DIR}/bench_main.cpp ${OMK_VITA_COMMON})
target_link_libraries(omk_bench omk_engine ScePower_stub)
vita_create_self(omk_bench.self omk_bench UNSAFE)
vita_create_vpk(omk_bench.vpk ${OMK_TITLE_BENCH} omk_bench.self
  VERSION ${VITA_VERSION} NAME "OMK bench")

# ---- omk_smoke: the GLES2 backend on vitaGL
# vitaGL: OUR build when `scripts/vita-vitagl.sh` has made one - the SDK's has
# a boot splash that opens a second sceGxm context, which Vita3K refuses, so
# nothing built on it runs in the emulator - and the SDK's otherwise.
set(OMK_VITAGL ${ENGINE}/build/vitagl/libvitaGL.a)
if(EXISTS ${OMK_VITAGL})
  message(STATUS "vitaGL: ${OMK_VITAGL} (no splash screen; runs in Vita3K)")
else()
  set(OMK_VITAGL vitaGL)
  message(WARNING "vitaGL: the SDK's - it will NOT run in Vita3K; run scripts/vita-vitagl.sh")
endif()
set(OMK_VITAGL_LIBS
  ${OMK_VITAGL} vitashark SceShaccCgExt mathneon taihen_stub
  SceShaccCg_stub SceKernelDmacMgr_stub SceGxm_stub SceDisplay_stub
  SceSysmodule_stub SceAppMgr_stub SceCommonDialog_stub SceIme_stub
  SceCtrl_stub ScePower_stub m)
add_executable(omk_smoke ${OMK_VITA_COMMON}
  ${CMAKE_CURRENT_SOURCE_DIR}/smoke_main.cpp
  ${ENGINE}/backends/gles/glesrender.cpp)
target_link_libraries(omk_smoke omk_engine ${OMK_VITAGL_LIBS})
vita_create_self(omk_smoke.self omk_smoke UNSAFE)
vita_create_vpk(omk_smoke.vpk ${OMK_TITLE_SMOKE} omk_smoke.self
  VERSION ${VITA_VERSION} NAME "OMK smoke")

# ---- omk_vita: THE GAME - `play.cpp` through the GLES backend and SDL2 on
# vitaGL (`sdl2_vitagl`). `play.cpp` is compiled with `-Dmain=omk_play_main`
# and `vita_main.cpp` is the real entry point: a Vita title has no command
# line, so it supplies the arguments (see that file). The tables compiled into
# the exe travel IN the VPK as `app0:tables/`; the game data does not.
if(OMK_VITA_GAME)
  add_executable(omk_vita ${OMK_VITA_COMMON}
    ${CMAKE_CURRENT_SOURCE_DIR}/vita_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/avmovie.cpp
    ${ENGINE}/backends/sdl/play.cpp
    ${ENGINE}/backends/gles/glesrender.cpp)
  set_source_files_properties(${ENGINE}/backends/sdl/play.cpp
    PROPERTIES COMPILE_DEFINITIONS "main=omk_play_main")
  target_compile_definitions(omk_vita PRIVATE OMK_GLES=1)
  target_include_directories(omk_vita PRIVATE $ENV{VITASDK}/arm-vita-eabi/include/SDL2)
  target_link_libraries(omk_vita omk_engine SDL2 ${OMK_VITAGL_LIBS}
    SceAudio_stub SceAudioIn_stub SceTouch_stub SceHid_stub SceMotion_stub SceRtc_stub SceAvPlayer_stub
    SceIofilemgr_stub pthread)
  vita_create_self(eboot.bin omk_vita UNSAFE)
  file(GLOB OMK_TABLES RELATIVE ${ENGINE}/../tables ${ENGINE}/../tables/*.json)
  set(OMK_TABLE_FILES)
  foreach(t ${OMK_TABLES})
    list(APPEND OMK_TABLE_FILES FILE ${ENGINE}/../tables/${t} tables/${t})
  endforeach()
  vita_create_vpk(omk_vita.vpk ${OMK_TITLE_GAME} eboot.bin
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    ${OMK_TABLE_FILES})
endif()

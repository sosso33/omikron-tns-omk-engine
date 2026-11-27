# SPDX-License-Identifier: GPL-3.0-or-later
# THE NINTENDO SWITCH BUILD - `todo/switch-port.md` S1. devkitA64 + libnx,
# SDL2 and Mesa (EGL / GLES) from devkitPro's portlibs, one NRO for hbmenu.
#
#     make switch            # from engine/: native devkitA64 or the pinned
#                            # Docker image, whichever is there (the Makefile)
#
# or by hand, inside a shell that has devkitPro:
#
#     cmake -S engine/backends/switch -B build-switch \
#           -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake
#     cmake --build build-switch -j
#
# THIS IS NOT THE MAKEFILE'S BUILD, and must not become a dependency of it
# (`PORTING` A8 rule 1), exactly as `backends/vita/CMakeLists.txt` and
# `backends/android/CMakeLists.txt` are not. It compiles the SAME
# `src/*/*.cpp` the host does, and the viewer's sources by the Makefile's own
# rule: every `backends/sdl/*.cpp` except the other GPU windows and the unused
# half of the instruments - the Android file's glob. No OMK_VR, so
# `playvr_off.cpp` is that seam's stub. OMK_REMASTER is ON, as Android's
# (the reader's report of 2026-11-25: the Remaster page was missing): the
# page, the packs' code and the GLES half - which pack fits in which memory
# mode is still step S6's measurement.
#
# The probe of 2026-11-25 (`todo/switch-port.md` §0) compiled all of it with
# zero engine changes; this file is that probe made permanent.
cmake_minimum_required(VERSION 3.16)

project(omk_switch CXX C)
# (the toolchain file is read by `project`, so the test comes after it)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
  message(FATAL_ERROR "configure with -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake")
endif()

set(ENGINE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# -O2 as the host builds; `-ffp-contract=off` keeps FMA contraction from
# changing a float result against the host's (AArch64 has a fused
# multiply-add the compiler would otherwise use - the Android build's reason)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -ffp-contract=off")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")

# THE REMASTER (`engine/Makefile`'s REMASTER), for the engine and the viewer
# alike - it changes the same layouts' meaning, so every object carries it.
# Its shaders are `#version 100`, which the GLES 2 context runs; the packs
# read as ASTC when Mesa names `GL_KHR_texture_compression_astc_ldr`.
add_compile_definitions(OMK_REMASTER=1)

option(OMK_INSTRUMENTS "build the viewer's harnesses (--money, --fight, --board...)" ON)
option(OMK_RELEASE "a release build: no profiler, no instruments" OFF)
if(OMK_RELEASE)
  add_compile_definitions(OMK_PROFILE=0)
  set(OMK_INSTRUMENTS OFF CACHE BOOL "" FORCE)
endif()

# ---- the engine: every ported source, exactly the Makefile's glob
file(GLOB OMK_SRC ${ENGINE}/src/*/*.cpp)
add_library(omk_engine STATIC ${OMK_SRC})
target_include_directories(omk_engine PUBLIC ${ENGINE}/src ${ENGINE}/third_party)

# ---- the viewer: the Makefile's PLAY_SDL_SRCS rule, GLES variant
file(GLOB OMK_PLAY_SRC ${ENGINE}/backends/sdl/*.cpp)
list(FILTER OMK_PLAY_SRC EXCLUDE REGEX "/playgpu_[^/]*\\.cpp$")
if(OMK_INSTRUMENTS)
  list(FILTER OMK_PLAY_SRC EXCLUDE REGEX "/playharness_off\\.cpp$")
else()
  list(FILTER OMK_PLAY_SRC EXCLUDE REGEX "/playharness\\.cpp$")
endif()

file(GLOB OMK_REMASTER_SRC ${ENGINE}/backends/remaster/*.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(OMK_SDL2 REQUIRED sdl2)

# THE GPU BACKEND: GLES through Mesa (the default), or DEKO3D - the console's
# own API, `todo/deko3d.md` (`make switch-deko`). One or the other is linked,
# as each desktop build links one `playgpu_*.cpp`.
option(OMK_DEKO3D "draw with deko3d instead of GLES through Mesa (todo/deko3d.md)" OFF)
if(OMK_DEKO3D)
  set(OMK_GPU_SRC
    ${ENGINE}/backends/sdl/playgpu_deko.cpp
    ${ENGINE}/backends/deko3d/dkrender.cpp)
else()
  set(OMK_GPU_SRC
    ${ENGINE}/backends/sdl/playgpu_gles.cpp
    ${ENGINE}/backends/gles/glesrender.cpp)
endif()

add_executable(omk_switch
  ${CMAKE_CURRENT_SOURCE_DIR}/switch_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/swkbd.cpp
  ${OMK_PLAY_SRC}
  ${OMK_REMASTER_SRC}
  ${OMK_GPU_SRC})
# `play.cpp`'s `main` becomes `omk_play_main`, which `switch_main.cpp` calls
# (the Vita's arrangement); SDL_MAIN_HANDLED keeps SDL_main.h's own
# `#define main SDL_main` out of every file.
set_source_files_properties(${ENGINE}/backends/sdl/play.cpp
  PROPERTIES COMPILE_DEFINITIONS "main=omk_play_main")
target_include_directories(omk_switch PRIVATE ${OMK_SDL2_INCLUDE_DIRS} ${ENGINE}/backends/sdl)
if(OMK_DEKO3D)
  target_compile_definitions(omk_switch PRIVATE OMK_DEKO3D=1 SDL_MAIN_HANDLED)
  # deko3d's DEBUG library on request (`make switch-deko DEKO_DEBUG=1`): every
  # call validated, a fault said through the device's debug callback rather
  # than a silent GPU hang - but its checks cost the CPU, so a measurement
  # takes the release one (`todo/deko3d.md` D2)
  option(OMK_DEKO3D_DEBUG "link libdeko3dd (validated) instead of libdeko3d" OFF)
  if(OMK_DEKO3D_DEBUG)
    set(OMK_DEKO_LIB deko3dd)
  else()
    set(OMK_DEKO_LIB deko3d)
  endif()
  # SDL2's static libraries name EGL and GLES (its video driver's), so they
  # stay linked; the renderer itself calls deko3d alone
  target_link_libraries(omk_switch omk_engine ${OMK_SDL2_STATIC_LIBRARIES}
    ${OMK_DEKO_LIB} EGL GLESv2 glapi drm_nouveau nx m)
else()
  target_compile_definitions(omk_switch PRIVATE OMK_GLES=1 SDL_MAIN_HANDLED)
  # SDL2's pkg-config names its own dependencies but not the GL the BACKEND
  # calls: Mesa's GLES2 / EGL and what they stand on (glapi, libdrm_nouveau),
  # then libnx again for them - a static link resolves left to right
  target_link_libraries(omk_switch omk_engine ${OMK_SDL2_STATIC_LIBRARIES}
    EGL GLESv2 glapi drm_nouveau nx m)
endif()
target_link_directories(omk_switch PRIVATE ${OMK_SDL2_STATIC_LIBRARY_DIRS})

# ---- THE ROMFS: `tables/*.json` (the tables lifted out of the exe) inside the
# NRO, read as `romfs:/tables`. Staged into the build directory by a command
# whose stamp the link depends on, so a changed table re-links and re-packs.
file(GLOB OMK_TABLES ${ENGINE}/../tables/*.json)
set(OMK_ROMFS ${CMAKE_CURRENT_BINARY_DIR}/romfs)
file(MAKE_DIRECTORY ${OMK_ROMFS}/tables)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/romfs.stamp
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OMK_TABLES} ${OMK_ROMFS}/tables/
  COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/romfs.stamp
  DEPENDS ${OMK_TABLES}
  VERBATIM)
set(OMK_ROMFS_DEPS ${CMAKE_CURRENT_BINARY_DIR}/romfs.stamp)

# ---- THE DEKO3D SHADERS (`todo/deko3d.md` D0): Vulkan's GLSL, translated by
# `backends/deko3d/dkshaders.py` and compiled by devkitPro's `uam` into
# `romfs:/shaders/<name>.dksh`. Each row: the module's name, its stage, its
# source, its defines - the Makefile's VK_SHADERS rules, one for one.
if(OMK_DEKO3D)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  find_program(OMK_UAM uam HINTS $ENV{DEVKITPRO}/tools/bin /opt/devkitpro/tools/bin REQUIRED)
  set(VKSH ${ENGINE}/backends/vulkan/shaders)
  set(DKSH_GLSL ${CMAKE_CURRENT_BINARY_DIR}/dkglsl)
  file(MAKE_DIRECTORY ${OMK_ROMFS}/shaders)
  set(OMK_DKSH_ROWS
    "scene.vert|vert|scene.vert|"
    "scene.frag|frag|scene.frag|"
    "scene_tiled.frag|frag|scene.frag|OMK_TILED"
    "scene_cut.frag|frag|scene.frag|OMK_LATE_DEPTH"
    "scene_tiled_cut.frag|frag|scene.frag|OMK_TILED,OMK_LATE_DEPTH"
    "scene_orig.frag|frag|scene.frag|OMK_ORIGINAL"
    "scene_orig_cut.frag|frag|scene.frag|OMK_ORIGINAL,OMK_LATE_DEPTH"
    "fsq.vert|vert|fsq.vert|"
    "fsq.frag|frag|fsq.frag|"
    "shadow.vert|vert|shadow.vert|"
    "present.frag|frag|present.frag|"
    "easu.frag|frag|easu.frag|"
    "under.frag|frag|under.frag|"
    "post.frag|frag|post.frag|"
    "ao.frag|frag|ao.frag|"
    "ao_ms.frag|frag|ao.frag|OMK_MS"
    "overlay.frag|frag|deko/overlay.frag|")
  foreach(row ${OMK_DKSH_ROWS})
    string(REPLACE "|" ";" f "${row}")
    list(GET f 0 name)
    list(GET f 1 stage)
    list(GET f 2 src)
    list(LENGTH f nf)
    set(defs "")
    if(nf GREATER 3)
      list(GET f 3 dl)
      if(dl)
        string(REPLACE "," ";" dl "${dl}")
        foreach(d ${dl})
          list(APPEND defs -D ${d})
        endforeach()
      endif()
    endif()
    # `deko/x`: a module of deko3d's own (`backends/deko3d/shaders/`), not Vulkan's
    if(src MATCHES "^deko/")
      string(REGEX REPLACE "^deko/" "" rest "${src}")
      set(srcpath ${ENGINE}/backends/deko3d/shaders/${rest})
    else()
      set(srcpath ${VKSH}/${src})
    endif()
    set(out ${OMK_ROMFS}/shaders/${name}.dksh)
    add_custom_command(
      OUTPUT ${out}
      COMMAND ${Python3_EXECUTABLE} ${ENGINE}/backends/deko3d/dkshaders.py
              ${srcpath} ${DKSH_GLSL}/${name}.glsl -I ${ENGINE}/third_party ${defs}
      COMMAND ${OMK_UAM} -s ${stage} -o ${out} ${DKSH_GLSL}/${name}.glsl
      DEPENDS ${srcpath} ${ENGINE}/backends/deko3d/dkshaders.py
              ${ENGINE}/third_party/ffx_a.h ${ENGINE}/third_party/ffx_fsr1.h
      VERBATIM)
    list(APPEND OMK_ROMFS_DEPS ${out})
  endforeach()
endif()

add_custom_target(omk_romfs DEPENDS ${OMK_ROMFS_DEPS})
add_dependencies(omk_switch omk_romfs)
# a changed table or shader re-links and re-packs
set_target_properties(omk_switch PROPERTIES LINK_DEPENDS "${OMK_ROMFS_DEPS}")

nx_generate_nacp(${CMAKE_CURRENT_BINARY_DIR}/omk.nacp
  NAME "OMK" AUTHOR "OMK" VERSION "0.1.0")
# the icon is libnx's default until the port has one of its own
nx_create_nro(omk_switch
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/omk.nro
  NACP ${CMAKE_CURRENT_BINARY_DIR}/omk.nacp
  ROMFS ${OMK_ROMFS})

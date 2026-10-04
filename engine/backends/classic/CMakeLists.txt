# SPDX-License-Identifier: GPL-3.0-or-later
# THE CLASSIC MAC OS BUILD - Carbon, one PEF binary for Mac OS 9 (CarbonLib)
# and Mac OS X (`todo/classic-mac-port-1999.md` step 5). Built with Retro68's
# Carbon toolchain; the machine supplies it:
#
#     cmake -S backends/classic -B build/classic \
#           -DCMAKE_TOOLCHAIN_FILE=<retro68>/powerpc-apple-macos/cmake/retrocarbon.toolchain.cmake
#     cmake --build build/classic
#
# (`make classic RETRO68=<retro68>` in engine/ does both.) What it builds:
#
#   OMKBoot   `tools/omk.cpp`, the engine booted headless - the program
#             `verify.py: engine: boot` holds to traces/intro.log - as an
#             application. A classic Mac application has no command line, so
#             `classic_main.cpp` reads the arguments from `omk.args` beside
#             it, one per line, and writes stdout to `omk-out.txt`.
#
#   OMKPlay   `omk-play`, THE VIEWER, through the Carbon frontend
#             (`carbonfront.cpp`, `omk::Frontend`): the game files of
#             `backends/sdl/` - SDL-free since the gateway - with the software
#             renderer (`playgpu_none`), no instruments (`playharness_off`),
#             and no `--scene` set viewer (an SDL instrument). `omk.args` and
#             `omk-out.txt` as above. Audio through the Sound Manager, and
#             OPENGL through AGL when the inputs below are found.
#
# No threads (`platform/threads.h` selects OMK_THREADS 0 on `macintosh`), and
# every path the engine opens goes through `omk::hostPath` (HFS colons).
cmake_minimum_required(VERSION 3.10)
project(OMKClassic CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ENGINE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
# `%zu`: Retro68's printf has no C99 length modifiers and shifts every later
# argument, so every unit sees the printf family redirected (`classic_printf.h`,
# force-included) to `printf_c99.cpp`, which is compiled without it.
add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/classic_printf.h)
# NO .bss: Retro68's XCOFF linker lays some .bss objects over their
# neighbours - libstdc++'s `num_get<char>::id` over `timepunct_cache_w` (why
# there are no streams, PORTING A10), and our own function-local statics'
# guard variables (`pedNameTable`'s, so its first call threw
# `recursive_init_error`). Zero-initialised objects go to .data instead,
# where every one has its own bytes.
add_compile_options(-fno-zero-initialized-in-bss)
# No fused multiply-add: GCC fuses a*b+c into one PowerPC fmadd, rounding once
# where the host rounds twice, and the frames then differ by float noise
# (ppc-darwin.mk does the same; the original ran on x87, which has no fused op)
add_compile_options(-ffp-contract=off)
set_source_files_properties(printf_c99.cpp PROPERTIES COMPILE_DEFINITIONS OMK_PRINTF_IMPL)

file(GLOB OMK_SRC ${ENGINE}/src/*/*.cpp)

add_library(omkengine STATIC ${OMK_SRC})
target_include_directories(omkengine PUBLIC ${ENGINE}/src ${ENGINE}/third_party)
target_compile_options(omkengine PUBLIC -O2 -ffunction-sections -fdata-sections)

# A COMMAND-LINE TOOL as an application: `tools/<tool>.cpp` with the classic
# entry (`omk.args` in, `omk-out.txt` out). OMKBoot is `omk`; any other tool
# is asked for by name, -DOMK_CLASSIC_TOOLS="ped_probe;veh_probe", for running
# a probe on Mac OS 9 / Tiger against the same probe on the host.
function(omk_classic_tool app tool)
    add_application(${app}
        classic_main.cpp
        heapcount.cpp
        printf_c99.cpp
        xcoff_weak_storage.cpp
        ${ENGINE}/tools/${tool}.cpp
        omk-classic.r
        TYPE APPL CREATOR OMKt)
    set_source_files_properties(${ENGINE}/tools/${tool}.cpp PROPERTIES COMPILE_DEFINITIONS main=omk_tool_main)
    target_link_libraries(${app} omkengine)
endfunction()
omk_classic_tool(OMKBoot omk)
foreach(t ${OMK_CLASSIC_TOOLS})
    omk_classic_tool(OMKTool_${t} ${t})
endforeach()

file(GLOB OMK_PLAY ${ENGINE}/backends/sdl/*.cpp)
list(FILTER OMK_PLAY EXCLUDE REGEX "/(sdlfront|playscene|playharness|playgpu_[a-z0-9]+)\\.cpp$")

# OPENGL, when the machine has what it takes (2026-10-04): the fixed-function
# backend (`backends/gl1/`) through AGL on the window - the same calls on Mac
# OS 9 (OpenGL 1.2.1) and Tiger. Two inputs from outside the tree, both on the
# tools volume next to Retro68 by default:
#   OMK_MACOS_SDK   the 10.4u SDK, for AGL's and OpenGL's headers (AGL's own
#                   non-framework branch: `#include <Carbon.h>`, `<gl.h>`);
#   OMK_OPENGL_PEF  Mac OS 9's `OpenGLLibrary`, WITH its resource fork, which
#                   Retro68's MakeImport turns into the import stub. Tiger's
#                   CFMSupport/OpenGLLib exports the same fragment name.
# Either missing, OMKPlay is built as before: the software reference alone.
get_filename_component(OMK_R68_BIN ${CMAKE_CXX_COMPILER} DIRECTORY)
get_filename_component(OMK_TOOLS_ROOT ${OMK_R68_BIN}/../../.. ABSOLUTE)
set(OMK_MACOS_SDK ${OMK_TOOLS_ROOT}/sdk/MacOSX10.4u.sdk CACHE PATH "the Mac OS X 10.4u SDK (AGL and OpenGL headers)")
set(OMK_OPENGL_PEF ${OMK_TOOLS_ROOT}/sdk/os9-opengl/OpenGLLibrary CACHE FILEPATH "Mac OS 9's OpenGLLibrary, with its resource fork")
set(OMK_AGL_HEADERS ${OMK_MACOS_SDK}/System/Library/Frameworks/AGL.framework/Headers)
find_program(OMK_MAKEIMPORT MakeImport HINTS ${OMK_R68_BIN})
if(EXISTS ${OMK_AGL_HEADERS}/agl.h AND EXISTS ${OMK_OPENGL_PEF} AND OMK_MAKEIMPORT)
    set(OMK_CLASSIC_GL ON)
    # AGL's gl.h includes <OpenGL/glext.h>: give it an OpenGL/ to find
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/glinc)
    file(CREATE_LINK ${OMK_MACOS_SDK}/System/Library/Frameworks/OpenGL.framework/Headers
         ${CMAKE_BINARY_DIR}/glinc/OpenGL SYMBOLIC)
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/libOpenGLLibrary.a
        COMMAND ${CMAKE_COMMAND} -E env "PATH=${OMK_R68_BIN}:$ENV{PATH}"
                ${OMK_MAKEIMPORT} ${OMK_OPENGL_PEF} ${CMAKE_BINARY_DIR}/libOpenGLLibrary.a
        DEPENDS ${OMK_OPENGL_PEF}
        COMMENT "MakeImport: the OpenGLLibrary import stub")
    add_custom_target(omk_opengl_stub DEPENDS ${CMAKE_BINARY_DIR}/libOpenGLLibrary.a)
    set(OMK_PLAY_GPU ${ENGINE}/backends/sdl/playgpu_gl1.cpp ${ENGINE}/backends/gl1/gl1render.cpp)
    message(STATUS "OMKPlay: OpenGL through AGL (${OMK_OPENGL_PEF})")
else()
    set(OMK_CLASSIC_GL OFF)
    set(OMK_PLAY_GPU ${ENGINE}/backends/sdl/playgpu_none.cpp)
    message(STATUS "OMKPlay: no AGL headers or OpenGLLibrary - the software reference alone")
endif()

add_application(OMKPlay
    classic_main.cpp
    heapcount.cpp
    printf_c99.cpp
    xcoff_weak_storage.cpp
    carbonfront.cpp
    playscene_off.cpp
    ${OMK_PLAY}
    ${OMK_PLAY_GPU}
    ${ENGINE}/backends/sdl/playharness_off.cpp
    omk-classic.r
    TYPE APPL CREATOR OMKp)
target_compile_definitions(OMKPlay PRIVATE OMK_CLASSIC_ENTRY=omk_play_main)
if(OMK_CLASSIC_GL)
    add_dependencies(OMKPlay omk_opengl_stub)
    target_compile_definitions(OMKPlay PRIVATE OMK_GL1_AGL)
    target_include_directories(OMKPlay PRIVATE ${OMK_AGL_HEADERS} ${CMAKE_BINARY_DIR}/glinc)
    target_link_libraries(OMKPlay ${CMAKE_BINARY_DIR}/libOpenGLLibrary.a)
endif()
set_source_files_properties(${ENGINE}/backends/sdl/play.cpp PROPERTIES COMPILE_DEFINITIONS main=omk_play_main)
target_include_directories(OMKPlay PRIVATE ${ENGINE}/backends/sdl ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(OMKPlay omkengine)
# no -gc-sections: it strips what libstdc++'s locale set-up needs (2026-10-03)

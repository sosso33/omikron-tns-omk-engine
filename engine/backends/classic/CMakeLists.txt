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
# No threads (`platform/threads.h` selects OMK_THREADS 0 on `macintosh`), and
# every path the engine opens goes through `omk::hostPath` (HFS colons).
cmake_minimum_required(VERSION 3.10)
project(OMKClassic CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ENGINE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
file(GLOB OMK_SRC ${ENGINE}/src/*/*.cpp)

add_library(omkengine STATIC ${OMK_SRC})
target_include_directories(omkengine PUBLIC ${ENGINE}/src ${ENGINE}/third_party)
target_compile_options(omkengine PUBLIC -O2 -ffunction-sections -fdata-sections)

add_application(OMKBoot
    classic_main.cpp
    ${ENGINE}/tools/omk.cpp
    omk-classic.r
    TYPE APPL CREATOR OMKb)
set_source_files_properties(${ENGINE}/tools/omk.cpp PROPERTIES COMPILE_DEFINITIONS main=omk_tool_main)
target_link_libraries(OMKBoot omkengine)
# no -gc-sections: it strips what libstdc++'s locale set-up needs (2026-10-03)

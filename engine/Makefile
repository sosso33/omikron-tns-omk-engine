# engine/ - the portable replica. C++20 (std::span), no dependencies.
#
# Sources are globbed by subsystem, so adding a file to src/<subsystem>/ needs
# no edit here; each tools/*.cpp builds into its own binary the same way.
# Includes are written from the src/ root - "formats/iam.h", "script/script.h" -
# which is what -Isrc is for, and what keeps them stable when a file moves.
#
# ---------------------------------------------------------------------------
# WHY THIS COMPILES TO OBJECTS RATHER THAN WHOLE PROGRAMS
#
# The first version of this file built each tool with one command:
#
#     build/%: tools/%.cpp $(SRC) $(HDRS) ;  $(CXX) -o $@ $< $(SRC)
#
# which is short, correct, and quadratic. Every one of the ~37 tools
# recompiled every one of the ~40 sources, so a build was ~1500 translation
# units; and because each binary depended on `$(HDRS)`, touching ANY header
# rebuilt all of them. Editing one line of one file cost a full rebuild -
# minutes - and `verify.py` runs `make -s` once per engine check, so the test
# suite paid it too. That is where an hour and a half of a session went.
#
# Now each source compiles once into build/obj/**.o and the tools link against
# them, so one edited file is one compile plus the links that need it.
# `-MMD -MP` writes a .d beside every object listing the headers it actually
# included, so a header edit rebuilds exactly what included it and nothing
# else - which is strictly better than the blanket $(HDRS) dependency, not
# just faster. -MP adds phony targets for those headers so a DELETED header
# does not wedge the build with "no rule to make target".
#
# The build is parallel by default because the objects are independent and the
# dependency graph is now complete enough to trust. Override with `make -j1`.
# ---------------------------------------------------------------------------
CXX      ?= c++
# `-Ithird_party` is the VENDORED header (pl_mpeg). A8 distinguishes it from a
# system dependency: it is checked in, so the default build may use it and
# `make` still needs nothing installed - a missing one is a broken checkout,
# not a disabled frontend.
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Isrc -Ithird_party
DEPFLAGS := -MMD -MP

SRC   := $(wildcard src/*/*.cpp)
# `run_vulkan.cpp` is EXCLUDED from the default set on purpose: it links the
# Vulkan backend, and `PORTING` A1/A8 rule 1 says `make` on a machine with
# nothing installed must build every tool and pass the suite. It is built by
# the `vulkan` target instead. Sweeping it in here broke exactly that property
# the first time, with an undefined-symbol link error on a clean build.
TOOLS := $(filter-out tools/run_vulkan.cpp tools/shadow_probe.cpp tools/perpixel_probe.cpp tools/present_probe.cpp,$(wildcard tools/*.cpp))
BINS  := $(patsubst tools/%.cpp,build/%,$(TOOLS))

OBJDIR   ?= build/obj
# the viewer's binary - `make release` puts its own elsewhere
PLAY_BIN ?= build/omk-play
SRCOBJS  := $(patsubst %.cpp,$(OBJDIR)/%.o,$(SRC))
TOOLOBJS := $(patsubst %.cpp,$(OBJDIR)/%.o,$(TOOLS))
DEPS     := $(SRCOBJS:.o=.d) $(TOOLOBJS:.o=.d)

# Parallel unless the caller said otherwise. `nproc` on Linux, `sysctl` on
# macOS, and 4 if neither answers - a wrong guess costs speed, not
# correctness.
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
MAKEFLAGS += -j$(NPROC)

# `omk-play` IS THE DEFAULT, because it is what this project is for: the
# playable replica, not the probes that measure it. It was a separate target
# until 2026-09-06, and the cost of that showed the same day - a change to the
# load panel was built with plain `make`, which left the viewer stale, and a
# reader met a menu that drew nothing.
#
# PORTING A8 rule 1 still holds - "`make` with nothing installed builds every
# tool and passes the suite" - because the `play` target below already prints
# why and exits 0 when SDL is absent rather than failing. So this adds a
# window where one can be built and a one-line note where one cannot.
all: $(BINS) play

# ---------------------------------------------------------------------------
# THE OPTIONAL LIVE FRONTEND
#
# `docs/PORTING.md` A8 rule 1: `make` with nothing installed builds every tool
# and passes the suite. So the window is a SEPARATE target, its sources live
# outside the `src/*/*.cpp` glob, and nothing above depends on it. `make play`
# on a machine without SDL prints why and exits 0 rather than failing a build
# that is supposed to be dependency-free.
#
# A8 names SDL3; SDL2 is what shipped on the machine this was written on, and
# the dozen calls the frontend uses exist in both, so 3 is preferred and 2 is
# accepted. Recorded in A8 rather than left to be discovered.
SDL_PKG   := $(shell pkg-config --exists sdl3 && echo sdl3 || (pkg-config --exists sdl2 && echo sdl2))
SDL_CFLAGS := $(if $(SDL_PKG),$(shell pkg-config --cflags $(SDL_PKG)))
SDL_LIBS   := $(if $(SDL_PKG),$(shell pkg-config --libs $(SDL_PKG)))
SDL_DEFS   := $(if $(filter sdl3,$(SDL_PKG)),-DOMK_SDL3)

# ---------------------------------------------------------------- VULKAN
#
# `PORTING` A1: a bare checkout with no Vulkan SDK must still `make` and pass
# the whole suite, so this is its own target and nothing under src/ or tools/
# refers to it. A8 lists "Vulkan loader + MoltenVK" as an OPTIONAL system
# dependency, the same status as SDL.
#
# The shaders are compiled to SPIR-V by `glslc` and embedded as C arrays, so
# the binary carries them and there is no runtime path to get wrong. The GLSL
# stays in the tree and readable; the .inc files are build output.
VK_PKG    := $(shell pkg-config --exists vulkan && echo vulkan)
VK_CFLAGS := $(if $(VK_PKG),$(shell pkg-config --cflags vulkan))
VK_LIBS   := $(if $(VK_PKG),$(shell pkg-config --libs vulkan))
GLSLC     := $(shell command -v glslc 2>/dev/null)
VK_SHADERS := build/shaders/scene.vert.inc build/shaders/scene.frag.inc \
              build/shaders/fsq.vert.inc build/shaders/fsq.frag.inc \
              build/shaders/shadow.vert.inc \
              build/shaders/present.frag.inc

build/shaders/%.inc: backends/vulkan/shaders/%
	@mkdir -p build/shaders
	$(GLSLC) -mfmt=c -o $@ $<

# Both optional targets depend on $(SRCOBJS) rather than assuming they are
# current. They compile their own source fresh and LINK the prebuilt objects,
# so a header change that alters a struct's layout - `Geometry` growing a field
# did exactly this - gives the two halves different ideas of that layout and
# links them anyway. The symptom was a feature silently not happening, with no
# error and no warning, and it cost a debugging round.
.PHONY: vulkan
vulkan: $(SRCOBJS)
ifeq ($(VK_PKG),)
	@echo "no Vulkan found (pkg-config vulkan) - the GPU renderer is optional;"
	@echo "everything else builds and the suite passes without it (PORTING A1/A8)."
else ifeq ($(GLSLC),)
	@echo "Vulkan found but glslc is not - install shaderc to compile the shaders."
else
	@$(MAKE) -s $(VK_SHADERS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(VK_CFLAGS) -Ibuild/shaders -o build/run_vulkan \
	    tools/run_vulkan.cpp backends/vulkan/vkrender.cpp $(SRCOBJS) $(VK_LIBS)
	$(CXX) $(CXXFLAGS) $(VK_CFLAGS) -Ibuild/shaders -o build/shadow_probe \
	    tools/shadow_probe.cpp backends/vulkan/vkrender.cpp $(SRCOBJS) $(VK_LIBS)
	$(CXX) $(CXXFLAGS) $(VK_CFLAGS) -Ibuild/shaders -o build/perpixel_probe \
	    tools/perpixel_probe.cpp backends/vulkan/vkrender.cpp $(SRCOBJS) $(VK_LIBS)
	$(CXX) $(CXXFLAGS) $(VK_CFLAGS) -Ibuild/shaders -o build/present_probe \
	    tools/present_probe.cpp backends/vulkan/vkrender.cpp $(SRCOBJS) $(VK_LIBS)
	@echo "built build/run_vulkan and build/shadow_probe against $(VK_PKG)"
endif

# THE VIEWER IS BUILT FROM OBJECTS, like everything else (2026-09-29). `play`
# used to compile `play.cpp` (~21 000 lines) and its renderer straight into the
# binary on every invocation - a phony target with no object and no dependency
# file, so `make` had nothing to compare and paid ~50 s each time, and every
# `verify.py` check that renders ran `make -s play` first. Each variant now
# compiles into its OWN object directory (the variants compile `play.cpp` with
# different flags, so their objects must never mix), with `-MMD -MP` like the
# engine objects, and `build/omk-play` / `build/omk-play-gles` are real file
# targets. `play` and `play-gles` stay the entry points and print as before.
PLAY_SW_DIR := $(OBJDIR)/play-sw
PLAY_VK_DIR := $(OBJDIR)/play-vk
PLAY_GL_DIR := $(OBJDIR)/play-gles
PLAY_SW_FLAGS := $(SDL_CFLAGS) $(SDL_DEFS)
PLAY_VK_FLAGS := $(SDL_CFLAGS) $(SDL_DEFS) $(VK_CFLAGS) -DOMK_VULKAN -Ibuild/shaders
PLAY_GL_FLAGS := $(SDL_CFLAGS) $(SDL_DEFS) -DOMK_GLES
PLAY_DEPS := $(wildcard $(PLAY_SW_DIR)/*.d $(PLAY_VK_DIR)/*.d $(PLAY_GL_DIR)/*.d)

# Every `backends/sdl/*.cpp` - `play.cpp`, and since `todo/play-split.md` S1c
# `sdlfront.cpp` - compiles once per variant, into that variant's directory.
# ...except the INSTRUMENTS, which are one of two files (todo/play-split.md S6):
# `playharness.cpp`, the harnesses every check drives, or with `INSTRUMENTS=0`
# `playharness_off.cpp`, the same methods as empty stubs - a build without them.
INSTRUMENTS ?= 1
PLAY_SDL_SRCS := $(filter-out backends/sdl/playharness$(if $(filter 0,$(INSTRUMENTS)),,_off).cpp \
                 backends/sdl/playgpu_%.cpp,$(wildcard backends/sdl/*.cpp))
# A stamp per value, so switching INSTRUMENTS RELINKS: the swapped object is
# older than the binary, and make compares nothing else.
INSTR_STAMP := $(OBJDIR)/instruments-$(INSTRUMENTS).stamp
$(INSTR_STAMP):
	@mkdir -p $(@D)
	@rm -f $(OBJDIR)/instruments-*.stamp
	@touch $@
# ...and THE GPU WINDOW, one file per variant (todo/play-split.md S5): the
# code that talks to a backend lives there, so the rest carries no #if for it
PLAY_SW_OBJS := $(PLAY_SDL_SRCS:backends/sdl/%.cpp=$(PLAY_SW_DIR)/%.o) $(PLAY_SW_DIR)/playgpu_none.o
PLAY_VK_OBJS := $(PLAY_SDL_SRCS:backends/sdl/%.cpp=$(PLAY_VK_DIR)/%.o) $(PLAY_VK_DIR)/playgpu_vulkan.o
PLAY_GL_OBJS := $(PLAY_SDL_SRCS:backends/sdl/%.cpp=$(PLAY_GL_DIR)/%.o) $(PLAY_GL_DIR)/playgpu_gles.o

$(PLAY_SW_DIR)/%.o: backends/sdl/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(PLAY_SW_FLAGS) -c -o $@ $<
$(PLAY_VK_DIR)/%.o: backends/sdl/%.cpp | $(VK_SHADERS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(PLAY_VK_FLAGS) -c -o $@ $<
$(PLAY_VK_DIR)/vkrender.o: backends/vulkan/vkrender.cpp | $(VK_SHADERS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(PLAY_VK_FLAGS) -c -o $@ $<
$(PLAY_GL_DIR)/%.o: backends/sdl/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(PLAY_GL_FLAGS) -c -o $@ $<
$(PLAY_GL_DIR)/glesrender.o: backends/gles/glesrender.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(PLAY_GL_FLAGS) -c -o $@ $<

ifneq ($(SDL_PKG),)
ifeq ($(VK_PKG)$(GLSLC),)
$(PLAY_BIN): $(PLAY_SW_OBJS) $(SRCOBJS) $(INSTR_STAMP)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.o,$^) $(SDL_LIBS)
PLAY_WITH := $(SDL_PKG) (no Vulkan - software renderer only)
else
$(PLAY_BIN): $(PLAY_VK_OBJS) $(PLAY_VK_DIR)/vkrender.o $(SRCOBJS) $(INSTR_STAMP)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.o,$^) $(SDL_LIBS) $(VK_LIBS)
PLAY_WITH := $(SDL_PKG) + $(VK_PKG)
endif
endif

# THE RELEASE BUILD (todo/debug-tools.md): the viewer with EVERY debugging
# aid compiled out - the profiler (`OMK_PROFILE=0`: its probes, the pause,
# the capture) and the instruments (`INSTRUMENTS=0`) - into its own object
# tree and binary, so it never mixes with the development build's objects
# (make does not track flags). `verify.py: engine: release build` holds it to
# having none of them.
# Its flags are STAMPED: make does not track flags, so a changed release flag
# would otherwise link the old objects (it did, 2026-10-04) - a different
# stamp wipes the release object tree first.
RELEASE_FLAGS := $(CXXFLAGS) -DOMK_PROFILE=0
.PHONY: release
release:
	@if [ "$$(cat build/obj-release/.flags 2>/dev/null)" != "$(RELEASE_FLAGS)" ]; then \
	    rm -rf build/obj-release; mkdir -p build/obj-release; \
	    echo "$(RELEASE_FLAGS)" > build/obj-release/.flags; fi
	$(MAKE) play OBJDIR=build/obj-release PLAY_BIN=build/release/omk-play INSTRUMENTS=0 \
	    CXXFLAGS="$(RELEASE_FLAGS)"

.PHONY: play
play: $(if $(SDL_PKG),$(PLAY_BIN))
ifeq ($(SDL_PKG),)
	@echo "no SDL found (pkg-config sdl3 or sdl2) - the frontend is optional;"
	@echo "everything else builds and the suite passes without it (PORTING A8)."
else
	@echo "built $(PLAY_BIN) against $(PLAY_WITH)"
endif

# ------------------------------------------------------------ THE PS VITA
#
# `todo/vita-port.md`. The same rule as SDL and Vulkan: OPTIONAL, never in
# `all`, and a machine without the SDK prints why and exits 0 (A8 rule 1).
# The build itself is CMake, because VitaSDK ships its packaging (SELF, VPK)
# as CMake functions; this target only drives it, so `make vita` is the one
# command every host uses. The build directory persists, so a second run
# recompiles only what changed.
#
# **Keep it compiling.** The Vita's newlib does not pull in what the host's
# headers do transitively (`<cmath>` was missing from two sources the first
# time), so a change can build here and break there without anything saying so
# - which is what `verify.py: engine: vita build` is for.
VITASDK_DIR := $(or $(VITASDK),$(wildcard $(HOME)/vitasdk))

.PHONY: vita
vita:
ifeq ($(wildcard $(VITASDK_DIR)/share/vita.toolchain.cmake),)
	@echo "no VitaSDK found (\$$VITASDK or ~/vitasdk) - the Vita build is optional;"
	@echo "everything else builds and the suite passes without it (todo/vita-port.md)."
else ifeq ($(wildcard $(VITASDK_DIR)/arm-vita-eabi/include/psp2/avplayer.h),)
	@echo "the VitaSDK at $(VITASDK_DIR) is too old (no psp2/avplayer.h, and a vitaGL"
	@echo "whose vglInitExtended predates the port); point \$$VITASDK at a current one,"
	@echo "e.g. VITASDK=~/vitasdk make vita, and rm -rf build/vita-cmake (it caches the toolchain)"
	@exit 1
else
	@mkdir -p build/vita
	VITASDK=$(VITASDK_DIR) PATH="$(VITASDK_DIR)/bin:$$PATH" \
	    cmake -S backends/vita -B build/vita-cmake >/dev/null
	VITASDK=$(VITASDK_DIR) PATH="$(VITASDK_DIR)/bin:$$PATH" \
	    cmake --build build/vita-cmake -j$(NPROC)
	cp build/vita-cmake/omk_bench.vpk build/vita-cmake/omk_smoke.vpk \
	    build/vita-cmake/omk_vita.vpk build/vita/
	@echo "built build/vita/omk_vita.vpk (the game), omk_bench.vpk and omk_smoke.vpk"
endif

# THE CLASSIC MAC OS BUILD (`todo/classic-mac-port-1999.md` step 5): Carbon,
# one PEF binary for Mac OS 9 and Mac OS X on PowerPC, with Retro68's
# toolchain. Optional like the Vita's: `make` and the suite never need it.
# `RETRO68` is Retro68's install (the directory holding bin/ and
# powerpc-apple-macos/); nothing here names a machine's path.
#
#     make classic RETRO68=<retro68>
#
# Today it builds `OMKBoot` - `tools/omk.cpp`, the engine booted headless, as
# an application (`backends/classic/`) - into build/classic/.
.PHONY: classic
classic:
ifeq ($(wildcard $(RETRO68)/powerpc-apple-macos/cmake/retrocarbon.toolchain.cmake),)
	@echo "no Retro68 at RETRO68=$(RETRO68) - the classic Mac build is optional;"
	@echo "everything else builds and the suite passes without it (todo/classic-mac-port-1999.md)."
else
	cmake -S backends/classic -B build/classic \
	    -DCMAKE_TOOLCHAIN_FILE=$(RETRO68)/powerpc-apple-macos/cmake/retrocarbon.toolchain.cmake >/dev/null
	cmake --build build/classic -j$(NPROC)
	@echo "built build/classic/OMKBoot.APPL (and .bin, MacBinary) - the engine booted, for Mac OS 9 and Tiger"
endif

# THE SAME, WITH `OMK_VITA_TUNE` (todo/optimization.md step 20): Cortex-A9
# scheduling and LTO, into build/vita-tuned/ so the two can be laid side by
# side on a console. Same title ids: installing one replaces the other.
.PHONY: vita-tuned
vita-tuned:
ifeq ($(wildcard $(VITASDK_DIR)/share/vita.toolchain.cmake),)
	@echo "no VitaSDK found (\$$VITASDK or ~/vitasdk) - the Vita build is optional."
else
	@mkdir -p build/vita-tuned
	VITASDK=$(VITASDK_DIR) PATH="$(VITASDK_DIR)/bin:$$PATH" \
	    cmake -S backends/vita -B build/vita-tuned-cmake -DOMK_VITA_TUNE=ON >/dev/null
	VITASDK=$(VITASDK_DIR) PATH="$(VITASDK_DIR)/bin:$$PATH" \
	    cmake --build build/vita-tuned-cmake -j$(NPROC)
	cp build/vita-tuned-cmake/omk_bench.vpk build/vita-tuned-cmake/omk_smoke.vpk \
	    build/vita-tuned-cmake/omk_vita.vpk build/vita-tuned/
	@echo "built build/vita-tuned/ (Cortex-A9 scheduling + LTO): omk_vita.vpk, omk_bench.vpk, omk_smoke.vpk"
endif

# The Vita bench on THIS machine - the same source, so a device run divides
# by it (`backends/vita/bench_main.cpp`). Needs nothing installed.
build/vita_bench: backends/vita/bench_main.cpp $(SRCOBJS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ backends/vita/bench_main.cpp $(SRCOBJS)

# The Vita's printf format rewrite (`backends/vita/c99format.h`), on the host.
build/vita_c99format_test: backends/vita/c99format_test.cpp backends/vita/c99format.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Ibackends/vita -o $@ backends/vita/c99format_test.cpp

# The GLES2 backend against the software reference, headless. macOS only: it
# makes its GL context with CGL, and the legacy OpenGL framework is always
# there, so it needs nothing installed either. Elsewhere it says so.
UNAME_S := $(shell uname -s)
.PHONY: gles-probe
ifeq ($(UNAME_S),Darwin)
gles-probe: build/gles_probe
else
gles-probe:
	@echo "gles-probe makes its context with CGL (macOS); on this host it is skipped."
endif

build/gles_probe: backends/gles/gles_probe.cpp backends/gles/glesrender.cpp $(SRCOBJS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ backends/gles/gles_probe.cpp \
	    backends/gles/glesrender.cpp $(SRCOBJS) -framework OpenGL

# THE GAME THROUGH THE GLES2 BACKEND on this machine - the Vita's renderer,
# drawn in a desktop window (macOS's legacy GL 2.1; `-lGLESv2` elsewhere).
# Optional like `play`, and needs SDL the same way. `build/omk-play-gles`.
GL_LIBS := $(if $(filter Darwin,$(UNAME_S)),-framework OpenGL,-lGLESv2)
ifneq ($(SDL_PKG),)
build/omk-play-gles: $(PLAY_GL_OBJS) $(PLAY_GL_DIR)/glesrender.o $(SRCOBJS) $(INSTR_STAMP)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.o,$^) $(SDL_LIBS) $(GL_LIBS)
endif

.PHONY: play-gles
play-gles: $(if $(SDL_PKG),build/omk-play-gles)
ifeq ($(SDL_PKG),)
	@echo "no SDL found - the GLES frontend is optional (PORTING A8)."
else
	@echo "built build/omk-play-gles against $(SDL_PKG) + GL"
endif

# One object per source. The mkdir is in the recipe rather than an order-only
# prerequisite so a new subsystem directory needs no edit here either.
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

# A tool is its own object plus every engine object. Linking them all is what
# the old rule did by recompiling them all; the linker drops what a tool does
# not reference, so the binaries are the same.
build/%: $(OBJDIR)/tools/%.o $(SRCOBJS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ $^

-include $(DEPS) $(PLAY_DEPS)

clean:
	rm -rf build

.PHONY: all clean
.SUFFIXES:

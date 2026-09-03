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
TOOLS := $(filter-out tools/run_vulkan.cpp,$(wildcard tools/*.cpp))
BINS  := $(patsubst tools/%.cpp,build/%,$(TOOLS))

OBJDIR   := build/obj
SRCOBJS  := $(patsubst %.cpp,$(OBJDIR)/%.o,$(SRC))
TOOLOBJS := $(patsubst %.cpp,$(OBJDIR)/%.o,$(TOOLS))
DEPS     := $(SRCOBJS:.o=.d) $(TOOLOBJS:.o=.d)

# Parallel unless the caller said otherwise. `nproc` on Linux, `sysctl` on
# macOS, and 4 if neither answers - a wrong guess costs speed, not
# correctness.
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
MAKEFLAGS += -j$(NPROC)

all: $(BINS)

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
              build/shaders/fsq.vert.inc build/shaders/fsq.frag.inc

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
	@echo "built build/run_vulkan against $(VK_PKG)"
endif

.PHONY: play
play: $(SRCOBJS)
ifeq ($(SDL_PKG),)
	@echo "no SDL found (pkg-config sdl3 or sdl2) - the frontend is optional;"
	@echo "everything else builds and the suite passes without it (PORTING A8)."
else
	@mkdir -p build
ifeq ($(VK_PKG)$(GLSLC),)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $(SDL_DEFS) -o build/omk-play \
	    backends/sdl/play.cpp $(SRCOBJS) $(SDL_LIBS)
	@echo "built build/omk-play against $(SDL_PKG) (no Vulkan - software renderer only)"
else
	@$(MAKE) -s $(VK_SHADERS)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $(SDL_DEFS) $(VK_CFLAGS) -DOMK_VULKAN \
	    -Ibuild/shaders -o build/omk-play \
	    backends/sdl/play.cpp backends/vulkan/vkrender.cpp $(SRCOBJS) \
	    $(SDL_LIBS) $(VK_LIBS)
	@echo "built build/omk-play against $(SDL_PKG) + $(VK_PKG)"
endif
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

-include $(DEPS)

clean:
	rm -rf build

.PHONY: all clean
.SUFFIXES:

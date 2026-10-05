# SPDX-License-Identifier: GPL-3.0-or-later
# THE NINTENDO 3DS BUILD - `todo/3ds-port.md`.
#
#     make -C engine/backends/n3ds            both programs
#     make -C engine/backends/n3ds RELEASE=1  without the profiler
#
# devkitARM + libctru (+ citro3d from step 3), found through $DEVKITPRO /
# $DEVKITARM - devkitPro's pacman install (/opt/devkitpro) or the from-source
# one `scripts/3ds-toolchain.sh` makes (~/devkitpro), the same layout either
# way. A plain Makefile rather than devkitPro's CMake files, because those are
# not published as source and the from-source toolchain has none; the GNU Make
# 3.81 macOS ships is enough.
#
# Two programs, each a `.3dsx` in engine/build/n3ds/ (the `.cia`, which sets
# its own memory mode, comes with step 4):
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

DEVKITPRO ?= $(HOME)/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
ifeq ($(wildcard $(DEVKITARM)/bin/arm-none-eabi-g++),)
$(error no devkitARM at $(DEVKITARM) - run scripts/3ds-toolchain.sh, or install devkitPro's 3ds-dev and set DEVKITPRO)
endif
CTRULIB  ?= $(DEVKITPRO)/libctru
TOOLBIN  := $(DEVKITPRO)/tools/bin

CXX      := $(DEVKITARM)/bin/arm-none-eabi-g++
CC       := $(DEVKITARM)/bin/arm-none-eabi-gcc
SMDHTOOL := $(TOOLBIN)/smdhtool
PICASSO  := $(TOOLBIN)/picasso
BIN2S    := $(TOOLBIN)/bin2s
3DSXTOOL := $(TOOLBIN)/3dsxtool

ENGINE   := ../..
OUT      := $(ENGINE)/build/n3ds
TABLES   := $(ENGINE)/../tables

# devkitPro's own 3DS flags (its templates'), and the engine's
ARCH     := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
DEFS     := -D__3DS__ -DOMK_THREADS=0
ifeq ($(RELEASE),1)
DEFS     += -DOMK_PROFILE=0
endif
# The engine throws (`bad_alloc` is reported, not a silent abort) and uses
# `dynamic_cast` in places; devkitPro's templates turn both off by default.
CXXFLAGS := -std=gnu++20 -O2 -g $(ARCH) -mword-relocations -ffunction-sections -fdata-sections \
            -fexceptions -frtti $(DEFS) -I$(ENGINE)/src -I$(ENGINE)/third_party -I$(CTRULIB)/include \
            -MMD -MP
LDFLAGS  := -specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections
LIBS     := -L$(CTRULIB)/lib -lctru -lm
PLAY_LIBS := -L$(CTRULIB)/lib -lcitro3d -lctru -lm

# ---- the engine: every ported source, exactly the Makefile's glob
ENGINE_SRC := $(wildcard $(ENGINE)/src/*/*.cpp)
ENGINE_OBJ := $(patsubst $(ENGINE)/src/%.cpp,$(OUT)/obj/engine/%.o,$(ENGINE_SRC))

# ---- omk_play: every `backends/sdl/` source but the SDL side of the gateway
# (`platform/frontend.h`) - the SDL frontend, the GPU glue (the 3DS has its
# own, `playgpu_citro3d.cpp`), the `--scene` set
# viewer and the instruments (`playharness.cpp`; the glob already brings
# `playharness_off.cpp`, their empty stubs): the classic Mac build's list.
PLAY_ALL := $(wildcard $(ENGINE)/backends/sdl/*.cpp)
PLAY_SRC := $(filter-out $(addprefix $(ENGINE)/backends/sdl/,sdlfront.cpp playscene.cpp playharness.cpp) \
                         $(wildcard $(ENGINE)/backends/sdl/playgpu_*.cpp),$(PLAY_ALL))
PLAY_OBJ := $(patsubst $(ENGINE)/backends/sdl/%.cpp,$(OUT)/obj/play/%.o,$(PLAY_SRC)) \
            $(OUT)/obj/play/n3ds_main.o $(OUT)/obj/play/n3dsfront.o $(OUT)/obj/play/playscene_off.o \
            $(OUT)/obj/play/n3dshost.o $(OUT)/obj/play/n3dspanel.o \
            $(OUT)/obj/play/c3drender.o $(OUT)/obj/play/playgpu_citro3d.o $(OUT)/obj/play/scene_shbin.o
BOOT_OBJ := $(OUT)/obj/boot/omk.o $(OUT)/obj/boot/n3ds_main.o $(OUT)/obj/boot/n3dshost.o

ICON     := $(CTRULIB)/default_icon.png

.PHONY: all clean
all: $(OUT)/omk_boot.3dsx $(OUT)/omk_play.3dsx

$(OUT)/libomk.a: $(ENGINE_OBJ)
	@mkdir -p $(dir $@)
	$(DEVKITARM)/bin/arm-none-eabi-gcc-ar rcs $@ $^

$(OUT)/obj/engine/%.o: $(ENGINE)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# omk_boot: the tool's `main` under another name, the entry its own
$(OUT)/obj/boot/omk.o: $(ENGINE)/tools/omk.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Dmain=omk_tool_main -c $< -o $@
$(OUT)/obj/boot/n3ds_main.o: n3ds_main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -DOMK_3DS_TOOL=1 -DOMK_3DS_ENTRY=omk_tool_main -c $< -o $@
$(OUT)/obj/boot/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# omk_play: `play.cpp`'s `main` under another name
PLAY_FLAGS := -I$(ENGINE)/backends/sdl -I.
$(OUT)/obj/play/play.o: $(ENGINE)/backends/sdl/play.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(PLAY_FLAGS) -Dmain=omk_play_main -c $< -o $@
$(OUT)/obj/play/%.o: $(ENGINE)/backends/sdl/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(PLAY_FLAGS) -c $< -o $@
$(OUT)/obj/play/n3ds_main.o: n3ds_main.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(PLAY_FLAGS) -DOMK_3DS_ENTRY=omk_play_main -c $< -o $@
# the scene's vertex shader (`scene.v.pica`): assembled by picasso, embedded
# by bin2s - run in the output directory, as it names its symbols after the
# file (`scene_shbin`, `scene_shbin_size`)
$(OUT)/scene.shbin: scene.v.pica
	@mkdir -p $(dir $@)
	$(PICASSO) -o $@ $<
$(OUT)/obj/play/scene_shbin.o: $(OUT)/scene.shbin
	@mkdir -p $(dir $@)
	cd $(OUT) && $(BIN2S) -a 4 scene.shbin > scene_shbin.s
	$(CC) -x assembler-with-cpp $(ARCH) -c $(OUT)/scene_shbin.s -o $@
$(OUT)/obj/play/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(PLAY_FLAGS) -c $< -o $@

$(OUT)/omk_boot.elf: $(BOOT_OBJ) $(OUT)/libomk.a
	$(CXX) $(LDFLAGS) -Wl,-Map,$(OUT)/omk_boot.map $(BOOT_OBJ) $(OUT)/libomk.a $(LIBS) -o $@
$(OUT)/omk_play.elf: $(PLAY_OBJ) $(OUT)/libomk.a
	$(CXX) $(LDFLAGS) -Wl,-Map,$(OUT)/omk_play.map $(PLAY_OBJ) $(OUT)/libomk.a $(PLAY_LIBS) -o $@

$(OUT)/omk_boot.smdh:
	@mkdir -p $(dir $@)
	$(SMDHTOOL) --create "OMK boot" "the engine booted headless" "OMK" $(ICON) $@
$(OUT)/omk_play.smdh:
	@mkdir -p $(dir $@)
	$(SMDHTOOL) --create "OMK" "Omikron: The Nomad Soul, the OMK replica" "OMK" $(ICON) $@

$(OUT)/%.3dsx: $(OUT)/%.elf $(OUT)/%.smdh
	$(3DSXTOOL) $< $@ --smdh=$(OUT)/$*.smdh --romfs=$(TABLES)

clean:
	rm -rf $(OUT)

-include $(ENGINE_OBJ:.o=.d) $(PLAY_OBJ:.o=.d) $(BOOT_OBJ:.o=.d)

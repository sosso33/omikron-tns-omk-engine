# SPDX-License-Identifier: GPL-3.0-or-later
# engine/ppc-darwin.mk - the engine and its command-line tools for Mac OS X
# on POWERPC (Tiger 10.4 and later), cross-compiled. A big-endian build: the
# first use is running the format and runtime tools on a big-endian CPU
# (`todo/classic-mac-port-1999.md` §3a, §4 step 3).
#
#     make -f ppc-darwin.mk                 # every tool, into build/ppc-darwin8/
#     make -f ppc-darwin.mk build/ppc-darwin8/iam_dump
#
# Needs a cross-compiler for powerpc-apple-darwin8 on PATH (or PPC_CXX set),
# with the 10.4u SDK as its sysroot. Where that comes from is the machine's
# business, not this file's: nothing here names a path outside the repo.
#
# A SEPARATE file and a separate object directory on purpose: it shares
# nothing with `Makefile`'s build/obj, so a PowerPC build never touches the
# objects the host build, verify.py and the viewer are using - and the viewer
# (backends/) is not built at all, only src/ and tools/.
#
# The C++ runtime is linked STATICALLY: Tiger's own libstdc++ is GCC 4.0's,
# so a binary that needed GCC 14's at run time would not start there. The
# output then needs only Tiger's libSystem.

PPC_CXX  ?= powerpc-apple-darwin8-g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Isrc -Ithird_party
# -ffp-contract=off: GCC FUSES a*b+c into one PowerPC fmadd by default,
# rounding once where the host rounds twice, and a 300-frame traffic run then
# drifted by 0.1 unit (veh_probe, 2026-10-02) - with it off on both sides the
# outputs are identical. The original ran on x87, which has no fused op.
PPCFLAGS := -mmacosx-version-min=10.4 -ffp-contract=off
LDFLAGS  := -static-libstdc++ -static-libgcc
DEPFLAGS := -MMD -MP

# The same sets as Makefile: every src/ subsystem, every tool but the ones
# that need a GPU backend.
SRC   := $(wildcard src/*/*.cpp)
TOOLS := $(filter-out tools/run_vulkan.cpp tools/shadow_probe.cpp tools/perpixel_probe.cpp tools/present_probe.cpp,$(wildcard tools/*.cpp))

OUT      := build/ppc-darwin8
OBJDIR   := $(OUT)/obj
SRCOBJS  := $(patsubst %.cpp,$(OBJDIR)/%.o,$(SRC))
TOOLOBJS := $(patsubst %.cpp,$(OBJDIR)/%.o,$(TOOLS))
BINS     := $(patsubst tools/%.cpp,$(OUT)/%,$(TOOLS))

NPROC := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
MAKEFLAGS += -j$(NPROC)

.PHONY: all check-cxx
all: check-cxx $(BINS)

check-cxx:
	@command -v $(PPC_CXX) >/dev/null 2>&1 || { \
	  echo "ppc-darwin.mk: no $(PPC_CXX) on PATH - set PPC_CXX to a cross-compiler for powerpc-apple-darwin8"; exit 1; }

# order-only: the check runs BEFORE any compile, not beside them (with -j a
# missing compiler otherwise printed one failure per source)
$(OBJDIR)/%.o: %.cpp | check-cxx
	@mkdir -p $(@D)
	$(PPC_CXX) $(CXXFLAGS) $(PPCFLAGS) $(DEPFLAGS) -c -o $@ $<

$(OUT)/%: $(OBJDIR)/tools/%.o $(SRCOBJS)
	@mkdir -p $(@D)
	$(PPC_CXX) $(CXXFLAGS) $(PPCFLAGS) -o $@ $^ $(LDFLAGS)

-include $(SRCOBJS:.o=.d) $(TOOLOBJS:.o=.d)

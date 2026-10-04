/* SPDX-License-Identifier: GPL-3.0-or-later
 * The memory partition. Retro68's Carbon template asks for 1 MB, which on
 * Mac OS 9 is the whole heap the application gets; the engine reads archives
 * of a megabyte and more whole. Mac OS X ignores the partition. Later
 * resources override earlier ones, so this SIZE replaces the template's.
 *
 * MEASURED on Mac OS 9 (2026-10-04, `heapcount.cpp`): Anekbah's street with
 * its crowd peaks at 79886 KB of live C++ blocks and uses at most 79009 KB of
 * the heap by FreeMem - about 82 MB with the C allocations and the blocks'
 * own overhead; Kay'l's apartment, 49741 KB.
 *
 * THE BUDGET IS 64 MB, and it is the GOAL, not a fit to the measurement (the
 * reader's rule, 2026-10-04): the original game runs correctly in 32 MB, so
 * 64 is already twice what it asked. The street's ~82 MB is how far THIS
 * PORT'S code is over - the list of what to take back is
 * `todo/classic-mac-port-1999.md` 3b - and the minimum stays 64. The
 * preferred 96 MB only lets today's code reach the street on a Mac that has
 * the memory; it comes down as the code does. */
#include "Processes.r"

resource 'SIZE' (-1) {
	reserved,
	acceptSuspendResumeEvents,
	reserved,
	canBackground,
	doesActivateOnFGSwitch,
	backgroundAndForeground,
	dontGetFrontClicks,
	ignoreChildDiedEvents,
	is32BitCompatible,
	isHighLevelEventAware,
	onlyLocalHLEvents,
	notStationeryAware,
	dontUseTextEditServices,
	reserved,
	reserved,
	reserved,
	96 * 1024 * 1024,
	64 * 1024 * 1024
};

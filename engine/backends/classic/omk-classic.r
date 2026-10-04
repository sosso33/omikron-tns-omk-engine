/* SPDX-License-Identifier: GPL-3.0-or-later
 * The memory partition. Retro68's Carbon template asks for 1 MB, which on
 * Mac OS 9 is the whole heap the application gets; the engine reads archives
 * of a megabyte and more whole. Mac OS X ignores the partition. Later
 * resources override earlier ones, so this SIZE replaces the template's.
 *
 * MEASURED on Mac OS 9 (2026-10-04, `heapcount.cpp`): Anekbah's street with
 * its crowd peaks at 79886 KB of live C++ blocks and uses at most 79009 KB of
 * the heap by FreeMem - about 82 MB with the C allocations and the blocks'
 * own overhead; Kay'l's apartment, 49741 KB. So 96 MB is the minimum (the
 * street and some 15% for fragmentation - MaxBlock ran 5 MB under FreeMem -
 * and for scenes not measured), 128 MB what it asks for. It was 192 / 64,
 * a guess: 64 MB would not hold the street. */
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
	128 * 1024 * 1024,
	96 * 1024 * 1024
};

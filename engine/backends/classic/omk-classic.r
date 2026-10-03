/* SPDX-License-Identifier: GPL-3.0-or-later
 * The memory partition. Retro68's Carbon template asks for 1 MB, which on
 * Mac OS 9 is the whole heap the application gets; the engine reads archives
 * of a megabyte and more whole. Mac OS X ignores the partition. Later
 * resources override earlier ones, so this SIZE replaces the template's. */
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
	192 * 1024 * 1024,
	64 * 1024 * 1024
};

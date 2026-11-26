// SPDX-License-Identifier: GPL-3.0-or-later
// THE `OMK_*` SWITCHES - this project's own environment variables, every one
// an instrument or an A/B comparison, none of them the game's.
//
// Every switch is read through this file and NAMED in `env.cpp`'s table with
// a line on what it does, so the whole set is in one place to read. The
// environment is read ONCE: the first read takes a snapshot of every switch
// in the table, and every later one is a lookup in it - no `getenv` in a
// frame loop (`getenv` walks the environment comparing strings, and read
// uncached the frame loop made twenty-five calls a frame, six of them for
// `OMK_NOUI` and one per staged body for `OMK_BODYLOG` - `todo/vita-port.md`),
// and the snapshot is a function-local static, so a worker thread may read a
// switch too. `verify.py: env switches` holds the table and the code
// to each other in both directions.
//
// A name the table does not carry is read straight from the environment,
// uncached, with a line on stderr saying so - a new switch works at once and
// says it wants its row.
#pragma once

namespace omk {

// The switch's value, or nullptr when it is not set.
const char* envStr(const char* name);

// Set at all, whatever its value - `OMK_NOUI=` counts.
bool envFlag(const char* name);

// Set and its value starts with '1' (`OMK_CAMLOG=1`); `OMK_CAMLOG=0` is off.
bool envOn(const char* name);

// Its value as an integer (`atol`), or `unset` when it is not set.
long envInt(const char* name, long unset);

// Its value as a number (`atof`), or `unset` when it is not set.
double envNum(const char* name, double unset);

// AN INSTRUMENT'S OVERRIDE - the performance sweep's (`--bench`), which turns
// a switch on and off between its steps: the snapshot's value replaced
// (nullptr: not set). False for a name the table does not carry. From the
// MAIN thread only, and only for switches read there each frame (a worker
// reading the snapshot meanwhile is not guarded).
bool envOverride(const char* name, const char* value);

}  // namespace omk

# Every RAM-consuming item against the original

The reader's ask (2026-10-04): *"Since good results have been done with
comparing with the original, compare every ram-consuming items with the
original game implementation."* The two first comparisons (the music stream,
the crowd's per-slot copies, `todo/debug-tools.md`) each found the port
keeping far more than the original and gave 14 MB back with no visible
change. This does the same for everything else the street holds.

The target this serves is the classic Mac's **64 MB** - a goal, set by the
original's own 32 MB requirement (`classic-mac-port-1999.md` 3b) - but every
build gains.

## How

1. **An inventory by OWNER, not by moment.** The profiler's categories say
   which zone a block was allocated in ("input: motion", "setup: session"),
   and several large ones are moments. So each block also records the code
   that allocated it (the return address of `operator new`), the profiler
   keeps live bytes per site, the capture carries the table, and
   `tools/omkprof.py --sites` symbolizes it (`atos`): every byte with a
   function's name, nothing tagged by hand. A profiling-build instrument
   like the rest (`OMK_PROFILE=0` has none of it).
2. **Per item, the original's mechanism**, read from the binary - what it
   loads, in what form, when it frees it - and the port's, side by side,
   with the bytes each keeps. Recorded in the table below as each is read.
3. **The cuts**, item by item, where the port keeps more than the original
   and the difference is not one this port chose for a reason (each such
   reason written down - the music's 32 KB file window is one: one 4 MB
   read froze a Vita for 0.7-0.9 s). Each cut proved invisible (frames
   byte-identical) and held by a check.

## The inventory

(step 1 fills this)

## Item by item

(step 2 fills this)

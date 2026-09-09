# Handoff — 2026-09-09, moving to another machine

Written to pick the work up elsewhere. Everything below is **pushed** on
`main` at `862caf0`; nothing is left uncommitted.

---

## 1. Set the machine up first — the input tree is NOT in the repo

`gamedata/`, `Runtime.exe.asm`, `Runtime.exe.c` and `clean/` are inputs and
none of them is committed. Copy `omk.conf.example` to `omk.conf` (gitignored)
and point it at wherever they live on the new machine:

```
data   = /path/to/the/game/tree      # the folder holding IAM, MESHES, SCPTDATA…
asm    = /path/to/Runtime.exe.asm    # optional; 14 checks SKIP without it
decomp = /path/to/Runtime.exe.c      # optional
clean  = /path/to/clean              # optional, and an OUTPUT as well
```

`python3 tools/omkpaths.py` prints what each resolved to and where the answer
came from — run it first, because a tool given the wrong root does not fail,
it reports **zero of everything**. That bit me twice today: `zone_quads` said
"0 zones" for AREA 0 and I read it as a parse bug when the archive had simply
not been found. Its output now prints the archive's size beside the count for
exactly that reason.

> On the machine this was written on the data root is **not** called
> `gamedata` — it is `~/Documents/omk/fr`, an older name — so the commands in
> `CLAUDE.md` §5 that say `../gamedata` need `$OMK_DATA` or the config.

Then `cd engine && make` (≈11 s clean) and
`python3 tools/verify.py --only "engine: shoot mode"` to prove the toolchain.

## 2. Where the work stands

**`todo/next-tasks.md` item 18, shoot mode** — the plan and the record are in
[`todo/shoot-mode.md`](shoot-mode.md), which is the file to read first. Four
of its six steps are done:

| step | state |
|---|---|
| 1 the mode read, and `MAP2D` found to be the AI's navigation grid | done, `6bbc506` |
| 2 the grid decoded, ported, drawn | done, `493efff` |
| 3 the mode ported — ops 80/81, the weapon slot, the HUD, the library | done, `8088453` |
| 4 the frontend (`--shoot`, the three installs) and the weapon tables lifted | **part** done, `862caf0` |
| 5 the brains — the decision of `standing-unknowns` §2 revisited | not started |
| 6 docs, checks, a play test | not started |

**What step 4 still owes**, and it is the next thing to do:

* `Shoot_TickPlayer`'s live arm (0x00427AC0) on the grid — the node at record
  `+188`, the cell at `+136`/`+140`, and the occupancy stamp/restore pair
  (`sub_435970` / `sub_420C10`) that the port now has a reader for but does
  not run;
* `Shoot_StartTargetScripts` (0x0047BEF0), unread;
* `Shoot_InitWeapon`'s event 48 arm, and **what a shot IS** — projectile, ray
  or scripted event. Nothing traced yet; `Projectiles_Tick` is in the frame
  loop and is the place to start.

Only when those are read is step 5 worth opening: the point of it is to ask
how much of the generic shooter's 16 states stops being geometry once the grid
is in hand. **Do not wire the generic brain before that reading** — see
`todo/standing-unknowns.md` §2, whose premise is now half corrected and half
standing.

## 3. What nobody has WATCHED

Three things landed today and none has been judged by a person except the
first:

| | |
|---|---|
| `omk-play` 94, the shop doors | **CONFIRMED IN PLAY** by the reader |
| shoot mode entering, `--shoot` in the gallery | headless only |
| the `MAP2D` grid drawn as ASCII | I looked at `gallery` floor 0; the other 78 floors are unseen |

The two-minute look for the next session:

```
cd engine && make play
build/omk-play "$OMK_DATA" ../tables --save ../traces/save-appart.bin \
    --area 59 --stand 5000,0,-2900,0 --shoot
```

The Shooting gallery, in shoot mode: the player should be in `ACTOR_STATE` 3
on `.CTL` group 200, the camera on his eye aiming 20 m ahead with no lag, and
the gallery's gunmen posed from the area's `.ani`. Nothing shoots — the brains
are not wired and that is deliberate.

## 4. The sweep, and what is NOT covered

The reader ran a full `--slow` sweep at **`f92231a`** — 384 checks, 0 failed,
1h28m — and it is the first fully green run of the whole suite. It measured a
commit BEFORE any of today's work, so **none of the following is covered by
it**, and all five are new since:

`move path file`, `engine: shop door`, `shoot arenas`, `map2d grid`,
`engine: shoot mode`.

`todo/sweep-log.md` says **2 tasks since the last sweep** (the shop doors, and
shoot mode as one task over four steps). The rule is a full `--slow` every 5
to 10 tasks, so one is not owed yet.

## 5. Traps that cost time TODAY, in the order they bit

1. **A tool given a wrong data root answers "0", not an error.** §1 above.
2. **`make` says "up to date" when the header edit and the previous build land
   in the SAME SECOND**, so a mutation test can report the mutated behaviour
   after the fix is restored. `touch` the header. This is CLAUDE.md §1's
   stale-object trap with second-granularity mtimes as the mechanism.
3. **A printf that recomputes its own answer is not a measurement.**
   `engine: shoot mode` printed `shootMode ? 2 : 0` for the input scheme, so
   deleting the `installScheme` call left the check green. It prints
   `in.group()` now — the live table's own group.
4. **Read the docs before re-deriving from the binary.** Both of step 4's
   "findings" — `shoot.begin`'s operand and the two weapon tables — were
   already in `docs/RECONSTRUCTION.md` (2026-08-27 and 08-28). The re-read did
   pay for itself, because it caught `SCRIPT_VM`'s table being shifted by one
   row, but that was luck, not method. `grep -n` the docs first; CLAUDE.md §0
   is about exactly this.

## 6. New instruments, so they are not rebuilt

| | |
|---|---|
| `engine/tools/map2d_probe.cpp` | `--all` for the census; `<map> <floor>` DRAWS a floor as ASCII |
| `engine/tools/shootmode_probe.cpp` | drives shoot mode through AREA 59's own script |
| `engine/tools/shopdoor_probe.cpp` | how far an interior's door leaves travel |
| `engine/src/formats/map2d.{h,cpp}` | the grid: floors, cells, doors, waypoints |
| `engine/src/actor/shootmode.{h,cpp}` | the mode: weapon slot, HUD, library, constants |
| `tables/shoot_weapons.json` | the two compiled weapon tables |
| `zone_quads` | now takes a chunk and `area`/`scene` |
| `scx_paths` | prints `file/index`, the key the data actually uses |
| `omk-play --shoot` | enter shoot mode at the hand-over |

# SPDX-License-Identifier: GPL-3.0-or-later
"""omkpaths - where this repo's three INPUTS live, and how to point elsewhere.

Nothing here is shipped with the repository. Every input is something you
supply, and the three are not equally available:

  * the GAME DATA tree - the contents of your own disc or GOG install.
    Most tools need it. Default `<repo>/gamedata`.
  * the DISASSEMBLY - `Runtime.exe.asm` / `.c` / `.h`, an IDA listing of
    `Runtime 2.exe`. A disassembly is a derivative work of the binary it came
    from, so it is NOT distributed here; if you have produced your own, point
    at it. Default `<repo>/Runtime.exe.asm` and friends.
  * `clean/` - the mechanical pass `tools/clean.py` makes over the
    disassembly. Derived from it, so it has the same status and the same
    default of being absent.

WHY A MODULE RATHER THAN A CONSTANT IN EACH TOOL
------------------------------------------------
Two reasons, and the second is the one that bit.

The data directory used to be spelled `fr` inline in ~30 files - it was named
that during the first tests, for the French release, and then quietly became
the name of the base data folder in the code AND in the docs. It is not a
French thing: the executable is the same for every localisation and the tree is
just the shipped data. Renaming a string that lives in 30 files is a chore that
invites a blind sed; renaming it in one is an edit.

And a dozen tools opened `"Runtime.exe.asm"` as a RELATIVE path, so they worked
from the repo root and silently failed anywhere else. Resolving centrally fixes
that for free.

RESOLUTION ORDER
----------------
For each input, first hit wins:

  1. an explicit override set by a tool's own flag  (`set_data_root()` etc.,
     which `--data` / `--asm` / `--decomp` / `--clean` call)
  2. the environment            OMK_DATA / OMK_ASM / OMK_DECOMP / OMK_CLEAN
  3. `omk.conf` at the repo root   data = ... / asm = ... / ...
  4. the in-tree default        gamedata/ , Runtime.exe.asm , Runtime.exe.c ,
                                clean/

A relative path in the environment or in `omk.conf` is taken relative to the
repo root, not to the current directory, so a config file means the same thing
wherever a tool is run from.

MISSING IS NOT AN ERROR
-----------------------
`asm_path()`, `decomp_path()` and `clean_dir()` return None when their input is
absent, so a caller SKIPS rather than fails - `verify.py` reports `skipped` for
the 14 checks that need the disassembly, which is what lets the suite run at
all on a checkout that legitimately does not have it. `data_root()` raises,
because a tool that cannot find the game has nothing to do.
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The in-tree defaults, relative to ROOT.
_DEFAULTS = {
    "data":   "gamedata",
    "asm":    "Runtime.exe.asm",
    "decomp": "Runtime.exe.c",
    "header": "Runtime.exe.h",
    "clean":  "clean",
}

_ENV = {
    "data":   "OMK_DATA",
    "asm":    "OMK_ASM",
    "decomp": "OMK_DECOMP",
    "header": "OMK_DECOMP_H",
    "clean":  "OMK_CLEAN",
}

# Overrides installed by set_*() - the tools' own flags.
_override = {}

_CONF_NAME = "omk.conf"
_conf_cache = None


def _conf():
    """`omk.conf` at the repo root, as a dict. Absent file -> empty.

    The format is deliberately the least that works: `key = value` lines, `#`
    comments, blank lines ignored. It is not INI and has no sections, because
    there are five keys and adding a parser would be the largest thing in this
    file.
    """
    global _conf_cache
    if _conf_cache is not None:
        return _conf_cache
    _conf_cache = {}
    path = os.path.join(ROOT, _CONF_NAME)
    try:
        with open(path, encoding="utf-8") as fh:
            for n, line in enumerate(fh, 1):
                line = line.split("#", 1)[0].strip()
                if not line:
                    continue
                if "=" not in line:
                    print(f"{_CONF_NAME}:{n}: ignored, expected `key = value`: "
                          f"{line}", file=sys.stderr)
                    continue
                k, v = line.split("=", 1)
                _conf_cache[k.strip().lower()] = v.strip()
    except FileNotFoundError:
        pass
    return _conf_cache


def _abs(value):
    """A configured path, made absolute against ROOT rather than the cwd."""
    value = os.path.expanduser(value)
    return value if os.path.isabs(value) else os.path.join(ROOT, value)


def _resolve(kind):
    """The configured location of `kind`, and where the answer came from.

    -> (path, source). The path is absolute and NOT checked for existence;
    callers that tolerate absence do that themselves.
    """
    if kind in _override:
        return _abs(_override[kind]), "flag"
    env = os.environ.get(_ENV[kind])
    if env:
        return _abs(env), _ENV[kind]
    conf = _conf().get(kind)
    if conf:
        return _abs(conf), _CONF_NAME
    return os.path.join(ROOT, _DEFAULTS[kind]), "default"


# --- the game data ---------------------------------------------------------

class DataNotFound(FileNotFoundError):
    """Raised by `data_root()`. Carries the paths that were tried."""


def data_root(required=True):
    """The game data tree - the directory holding IAM/, MESHES/, SCPTDATA/...

    Raises `DataNotFound` when it is absent and `required`, naming every place
    that was looked at and how to set it. That message is the whole point: a
    bare "No such file or directory: .../gamedata/IAM/DIALOG" fifty frames deep
    in a decoder tells you nothing about what to do next.
    """
    path, source = _resolve("data")
    if os.path.isdir(path):
        return path
    if not required:
        return None
    raise DataNotFound(
        f"game data not found at {path} (from: {source})\n"
        f"\n"
        f"This repository ships no game data. Point it at your own copy of\n"
        f"Omikron: The Nomad Soul - the directory holding IAM/, MESHES/,\n"
        f"SCPTDATA/, MORPH/ and Runtime 2.exe - in any of:\n"
        f"\n"
        f"  a flag         --data /path/to/gamedata   (on tools that take one)\n"
        f"  the environment OMK_DATA=/path/to/gamedata\n"
        f"  {_CONF_NAME} at {ROOT}, a line reading:\n"
        f"                 data = /path/to/gamedata\n"
        f"  or place it at {os.path.join(ROOT, _DEFAULTS['data'])}\n")


def set_data_root(path):
    """Override the data root - what a tool's `--data` flag calls."""
    _override["data"] = path


def data(*parts):
    """Join under the data root: `data(\"IAM\", \"DIALOG\")`.

    Note the tree is case-mangled on disk (Win95 did not care) - the engine's
    `DataFs` resolves case-insensitively and the Python readers rely on the
    host filesystem being case-insensitive too, which is why macOS works and a
    case-sensitive Linux filesystem may need the tree normalised.
    """
    return os.path.join(data_root(), *parts)


def have_data():
    """-> True when the data root exists. For a skip path."""
    return data_root(required=False) is not None


# --- the disassembly, and clean/ -------------------------------------------

def _optional(kind):
    path, _ = _resolve(kind)
    return path if os.path.exists(path) else None


def asm_path():
    """`Runtime.exe.asm`, or None. See the module docstring for why None."""
    return _optional("asm")


def decomp_path():
    """`Runtime.exe.c` - the Hex-Rays output - or None."""
    return _optional("decomp")


def header_path():
    """`Runtime.exe.h`, or None."""
    return _optional("header")


def clean_dir():
    """Where `clean/` is - whether or not it exists yet.

    Unlike `asm_path()` this always returns a path, because `clean/` is an
    OUTPUT as well as an input: `clean.py`, `parse.py`, `vm_extract.py` and
    friends regenerate it. A resolver that returned None for "not there yet"
    would make the generators unable to create the very tree they produce.
    Ask `have_clean()` about existence.
    """
    return _resolve("clean")[0]


def have_clean():
    """-> True when the `clean/` tree exists."""
    return os.path.isdir(clean_dir())


def clean(*parts):
    """Join under `clean/`. Always a path; see `clean_dir()`."""
    return os.path.join(clean_dir(), *parts)


def ensure_clean_dir():
    """Create `clean/` if absent, and return it. For the generators."""
    path = clean_dir()
    os.makedirs(path, exist_ok=True)
    return path


# --- tables/ ---------------------------------------------------------------
#
# Not an input in the same sense: `tables/` IS committed, because it holds the
# tables compiled into the executable and a replica cannot read those out of
# the data files. It lives here so the whole repo has one place that knows
# where things are, and so a caller can override it the same way.

def tables_dir():
    """The `tables/` tree. Committed, so this is a plain path."""
    return _abs(os.environ.get("OMK_TABLES", "tables"))


def tables(*parts):
    """Join under `tables/`: `tables(\"vm_opcodes.json\")`."""
    return os.path.join(tables_dir(), *parts)


def set_asm(path):
    _override["asm"] = path


def set_decomp(path):
    _override["decomp"] = path


def set_clean(path):
    _override["clean"] = path


def have_sources():
    """-> True when BOTH the disassembly and `clean/` are available.

    The pair, because almost every check that wants one wants the other: the
    `clean/` JSONs are a pass over the listing, and a stale `clean/` beside a
    different listing is worse than neither.
    """
    return asm_path() is not None and clean_dir() is not None


_GETTER = {"data": lambda: data_root(required=False),
           "asm": asm_path, "decomp": decomp_path,
           "header": header_path,
           "clean": lambda: clean_dir() if have_clean() else None}


def missing_for(*kinds):
    """-> a one-line reason when any of `kinds` is absent, else None.

    The shape a caller wants for a SKIP. `verify.py` opens the checks that
    read the disassembly with

        s = _need("asm")
        if s: return s

    so a checkout that legitimately has no listing reports `skipped` with the
    variable to set, rather than dying on `open(None)` forty frames down. That
    matters more than it looks: a suite that crashes on a missing optional
    input is a suite nobody without that input can run at all, and the
    disassembly is the one input this repository cannot distribute.
    """
    absent = [k for k in kinds if _GETTER[k]() is None]
    if not absent:
        return None
    where = ", ".join(f"{k} ({_resolve(k)[0]})" for k in absent)
    envs = " / ".join(f"${_ENV[k]}" for k in absent)
    return f"{where} absent - set {envs}, a flag, or {_CONF_NAME}"


def why_no_sources():
    """One line explaining what is missing, for a `skipped` reason."""
    return missing_for("asm", "clean")


def _require(kind, getter):
    got = getter()
    if got is None:
        path, source = _resolve(kind)
        raise FileNotFoundError(
            f"{kind} not found at {path} (from: {source})\n"
            f"This tool reads the disassembly of `Runtime 2.exe`, which is a\n"
            f"derivative work of the game's binary and is NOT distributed with\n"
            f"this repository. Point at your own with ${_ENV[kind]}, the\n"
            f"matching flag, or a line in {_CONF_NAME} at {ROOT}.\n")
    return got


def require_asm():
    """`Runtime.exe.asm`, or a FileNotFoundError that says what to set.

    For tools whose whole purpose is the listing - `vm_table.py`,
    `vm_extract.py`, `clean.py`. A check that merely consults it should use
    `missing_for()` and skip instead.
    """
    return _require("asm", asm_path)


def require_decomp():
    """`Runtime.exe.c`, or a FileNotFoundError that says what to set."""
    return _require("decomp", decomp_path)


# --- flags, and a diagnostic -----------------------------------------------

_FLAGS = {"--data": set_data_root, "--asm": set_asm,
          "--decomp": set_decomp, "--clean": set_clean}


def take_flags(argv):
    """Consume `--data/--asm/--decomp/--clean PATH` from argv, in place.

    Written for the tools here, which parse `sys.argv` by hand rather than
    through argparse; it removes what it recognises and leaves the rest, so a
    tool can call it first and then read its own positional arguments as
    before.

    -> the same list, for chaining.
    """
    i = 1
    while i < len(argv):
        arg = argv[i]
        key, _, inline = arg.partition("=")
        if key in _FLAGS:
            if inline:
                _FLAGS[key](inline)
                del argv[i]
            elif i + 1 < len(argv):
                _FLAGS[key](argv[i + 1])
                del argv[i:i + 2]
            else:
                sys.exit(f"{key} needs a path")
            continue
        i += 1
    return argv


FLAG_HELP = (
    "  --data PATH     the game data tree      (or $OMK_DATA, or omk.conf)\n"
    "  --asm PATH      Runtime.exe.asm         (or $OMK_ASM)\n"
    "  --decomp PATH   Runtime.exe.c           (or $OMK_DECOMP)\n"
    "  --clean PATH    the clean/ tree         (or $OMK_CLEAN)\n")


def describe():
    """Every input, where it resolved to, where that came from, and whether it
    is actually there. `python3 tools/omkpaths.py` prints this."""
    rows = []
    for kind in ("data", "asm", "decomp", "header", "clean"):
        path, source = _resolve(kind)
        there = os.path.isdir(path) if kind in ("data", "clean") \
            else os.path.exists(path)
        rows.append((kind, path, source, there))
    return rows


def main():
    take_flags(sys.argv)
    print(f"repo root   {ROOT}\n")
    for kind, path, source, there in describe():
        print(f"{kind:8s} {'ok     ' if there else 'MISSING'} "
              f"{path}   [{source}]")
    print()
    print("set any of them with a flag, the environment, or omk.conf:")
    print(FLAG_HELP, end="")


if __name__ == "__main__":
    main()

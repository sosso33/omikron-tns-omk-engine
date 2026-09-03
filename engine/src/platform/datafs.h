// SPDX-License-Identifier: GPL-3.0-or-later
// `DataFs` - the one way the engine opens the game's data.
//
// **Why this is a class and not a helper.** The game shipped for Windows
// 95/98, whose filesystem is case-INSENSITIVE, so every name the data
// references itself by was resolved by the OS without anyone noticing the
// spelling: a `.3DO` naming its `.3dt`, an actor record naming its `.CTL`,
// `Scene_FullPath` building a path out of a record's string. The authors typed
// whatever they liked and the disc proves it - eight of the 67 shipped `.SFX`
// files spell the extension `.Sfx` or `.SfX`, and a `.3DM` sweep once reported
// 708 files where there are 777.
//
// On Linux or macOS none of that resolves by concatenation. So the replica
// needs case-insensitive resolution as a **compatibility requirement**, and it
// needs it everywhere - which means the easy path has to be the correct one.
// Everything below takes a path RELATIVE to the data root and resolves it
// component by component; nothing else in the engine should touch
// `std::filesystem` or build a path by concatenation.
//
// Directory names are references too, so they are matched the same way:
// `meshes/DECORS/aapkayl.3do` finds `MESHES/DECORS/Aapkayl.3DO`.
//
// Portability comes with it: separators, listings and reads all live here, so
// a port to another platform changes one file.
//
// NOTE the boundary, because it is easy to over-apply: this is about PATH
// lookup. An in-memory string compare inside the engine - the texture cache's
// 19-character name match, say - is whatever that code does and must be read,
// not inferred from the filesystem's behaviour.
#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace omk {

class DataFs {
public:
    // `root` is the user's game directory - the one holding IAM, MESHES, ...
    explicit DataFs(std::string root) : root_(std::move(root)) {}

    const std::string& root() const { return root_; }

    // The real on-disk path of a data-relative name, or nothing.
    // Every component is matched without regard to case.
    std::optional<std::string> resolve(std::string_view rel) const;

    bool exists(std::string_view rel) const { return resolve(rel).has_value(); }

    // Read a whole file. Empty when it does not resolve - callers are readers
    // of user-supplied data and must not throw on a missing file.
    std::vector<std::byte> read(std::string_view rel) const;

    // The same file with a different extension - the `.3DO` -> `.3dt` case.
    // Written as one call because doing it by hand is where the two-spelling
    // bug keeps reappearing.
    std::vector<std::byte> readSibling(std::string_view rel,
                                       std::string_view ext) const;
    std::optional<std::string> resolveSibling(std::string_view rel,
                                              std::string_view ext) const;

    // Every file in a directory whose extension matches, sorted by real path.
    // This is the enumerating counterpart to `resolve`; a `*.EXT` + `*.ext`
    // pair is the bug this replaces.
    std::vector<std::string> list(std::string_view relDir,
                                  std::string_view ext) const;

    // The subdirectories of `relDir`, as full paths - so a caller can walk a
    // tree without ever concatenating a name itself, which is the whole point
    // of this class. `gamedata/MESHES` needs it: the models are under DECORS,
    // PERSOS and OBJETS.
    std::vector<std::string> subdirs(std::string_view relDir = ".") const;

    // Read an absolute or already-real path, still case-insensitively - for
    // the few callers handed a path from the command line.
    static std::vector<std::byte> readPath(const std::string& path);

private:
    std::string root_;
    // directory -> {lowercased entry -> real entry}, built once per directory
    mutable std::map<std::string, std::map<std::string, std::string>> index_;
    const std::map<std::string, std::string>* indexOf(const std::string& dir) const;
};

// REFUSE TO WRITE OVER SHIPPED GAME DATA.
//
// `gamedata/` is INPUT and CLAUDE.md 2 says it is never edited. Nothing in the tree
// enforced that, and on 2026-09-02 a tool argument in the wrong order -
// `dump_geom <model> <out>` invoked as `dump_geom <root> <model>` - truncated
// `gamedata/MESHES/DECORS/grid.3DO` to an 8-byte header. The file is from the 1999
// disc and there was no second copy on the machine; `verify.py: engine 3DO`
// reported the loss (635 -> 634 models) but only after the write.
//
// So every tool that takes an OUTPUT path calls this on it first. It is a
// refusal, not a warning: a tool that would overwrite a shipped asset has been
// given the wrong arguments, and continuing is never what was meant.
//
// -> true when the path is safe to write. On false it has already explained
// itself on stderr, so a caller can just `return 1`.
bool safeOutputPath(const std::string& path);

}  // namespace omk

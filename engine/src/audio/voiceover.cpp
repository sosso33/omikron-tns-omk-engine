// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/voiceover.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace omk {
namespace {

// The 13 bytes at 0x004C0868 that the handler copies over the first 13 of the
// built name when it begins ZVOT or ZVOP:
//   dd 474E494Ah = 'J','I','N','G'   dd 3346464Fh = 'O','F','F','3'
//   dd 5044412Eh = '.','A','D','P'   db 0
constexpr const char* kJingle = "JINGOFF3.ADP";

// The dword compares are raw, so the test is on the exact bytes.
bool startsWith(const std::string& s, const char* p) {
    const std::size_t n = std::char_traits<char>::length(p);
    return s.size() >= n && s.compare(0, n, p) == 0;
}

}  // namespace

bool VoiceOverLibrary::load(const DataFs& fs) {
    objects_ = loadObjects(fs);
    tagNames_.clear();

    // `IAM\OBJECTS.TAG` - the same ini the engine narrates through
    // (`GetPrivateProfileStringA(section, "<id>", ..., "IAM\\OBJECTS.TAG")`),
    // read here only for the names.  A `[SECTION]` header then `<id>=<name>`,
    // CRLF, cp1252 kept as bytes.
    const auto tag = fs.read("IAM/OBJECTS.TAG");
    if (!tag.empty()) {
        std::string line;
        const auto flush = [&] {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            const auto eq = line.find('=');
            if (eq == std::string::npos || eq == 0) { line.clear(); return; }
            bool digits = true;
            for (std::size_t i = 0; i < eq; ++i)
                if (!std::isdigit(static_cast<unsigned char>(line[i]))) digits = false;
            if (digits) {
                const int id = std::atoi(line.substr(0, eq).c_str());
                if (id >= 0 && id < 100000) {
                    if (static_cast<std::size_t>(id) >= tagNames_.size())
                        tagNames_.resize(static_cast<std::size_t>(id) + 1);
                    tagNames_[static_cast<std::size_t>(id)] = line.substr(eq + 1);
                }
            }
            line.clear();
        };
        for (const auto b : tag) {
            const char c = static_cast<char>(b);
            if (c == '\n') flush(); else line.push_back(c);
        }
        flush();
    }
    return !objects_.empty();
}

bool VoiceOverLibrary::isVoiceObject(int objectId) const {
    if (tagNames_.empty()) return true;      // no .TAG: do not filter anything
    if (objectId < 0 || static_cast<std::size_t>(objectId) >= tagNames_.size())
        return false;
    return startsWith(tagNames_[static_cast<std::size_t>(objectId)], "ZVO");
}

VoiceOver VoiceOverLibrary::resolve(const DataFs& fs, int objectId) const {
    VoiceOver v;
    v.object = objectId;
    // Step 3: the handler returns on -1 before it reads anything.
    if (objectId < 0 || static_cast<std::size_t>(objectId) >= objects_.size())
        return v;
    const auto& r = objects_[static_cast<std::size_t>(objectId)];
    v.stem = r.stem;
    if (!tagNames_.empty() && static_cast<std::size_t>(objectId) < tagNames_.size())
        v.objectName = tagNames_[static_cast<std::size_t>(objectId)];

    // Step 8: kind 16 is a DOCUMENT - the `IMAGES\%s` arm, and no audio.
    if (r.kind == 16) {
        v.image = true;
        return v;
    }

    // Step 6 and 9.  `%s.ADP` first, then the length refusal on the BUILT
    // name, then the substitution on its first four bytes.
    const std::string name = r.stem + ".ADP";
    if (name.size() <= 4) return v;                  // an empty stem: refused
    if (startsWith(name, "ZVOT") || startsWith(name, "ZVOP")) {
        v.file = kJingle;
        v.substituted = true;
    } else {
        v.file = name;
    }
    v.path = "VOICEOFF/" + v.file;
    v.shipped = fs.exists(v.path);
    return v;
}

std::vector<std::int16_t> VoiceOverLibrary::decode(const DataFs& fs,
                                                   const AdpcmTables& t,
                                                   const VoiceOver& v) const {
    if (!v.playable()) return {};
    const auto raw = fs.read(v.path);
    if (raw.empty()) return {};
    return adpcmDecode(raw, /*stereo=*/false, t);    // Morph_SetAudioFormat: 1 channel
}

// --------------------------------------------------------------- the counts
//
// All four partition the ZVO-tagged objects, so all four answer **-1** with no
// `.TAG` loaded rather than a plausible 0: the question is about the tag names
// and without them it has no answer.  A check reading -1 fails loudly, which
// is the point.

bool VoiceOverLibrary::taggedVoice(int objectId) const {
    if (tagNames_.empty()) return false;
    if (objectId < 0 || static_cast<std::size_t>(objectId) >= tagNames_.size())
        return false;
    return startsWith(tagNames_[static_cast<std::size_t>(objectId)], "ZVO");
}

int VoiceOverLibrary::count(const DataFs& fs, int which) const {
    if (tagNames_.empty()) return -1;
    int n = 0;
    for (std::size_t i = 0; i < objects_.size(); ++i) {
        if (!taggedVoice(static_cast<int>(i))) continue;
        if (which == 0) { ++n; continue; }
        const auto v = resolve(fs, static_cast<int>(i));
        if (which == 1 && !v.substituted && v.shipped) ++n;
        if (which == 2 && v.substituted) ++n;
        if (which == 3 && !v.substituted && !v.shipped) ++n;
    }
    return n;
}

int VoiceOverLibrary::voiceFiles(const DataFs& fs) {
    // Both spellings ship (`ZVOD001.adp` beside `ZVOD002.ADP`), which is what
    // `DataFs::list` is for - a `*.ADP` glob would find 12 of the 17.
    return static_cast<int>(fs.list("VOICEOFF", ".ADP").size());
}

}  // namespace omk

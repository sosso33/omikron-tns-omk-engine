// SPDX-License-Identifier: GPL-3.0-or-later
// A TEXTURE'S PIXELS ARE SHARED BY ITS COPIES, AND A WRITE DETACHES.
//
//     pixel_sharing <set.3DO> <set.3DT>
//
// `Texture::rgb` became a `PixelBuffer` (formats/tex3dt.h, todo/optimization.md
// step 6) so that the several lists holding a texture stop holding its pixels
// several times. It is held to behaving as the `std::vector` it replaced did,
// as a VALUE: a copy reads the same bytes, and writing into one copy changes no
// other. This pins both halves, on synthetic buffers and on a real decoded set:
//
//   * a copy shares the storage (the memory saving) and reads identical bytes;
//   * READING a non-const copy with `[]` does not detach anything;
//   * writing into the copy detaches it - the copy changes, the original and a
//     third copy do not;
//   * brace-assignment and `assign` give fresh storage;
//   * every texture of a real `.3DT`, copied into a pool the way the viewer
//     does, shares storage with its source and reads byte-identical.
//
// Prints one line of counts and `failures <n>`. Writes nothing.
#include "formats/tex3dt.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

std::vector<std::byte> slurp(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<std::byte> out(raw.size());
    if (!raw.empty()) std::memcpy(out.data(), raw.data(), raw.size());
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: pixel_sharing <set.3DO> <set.3DT>\n");
        return 2;
    }
    long failures = 0;
    const auto expect = [&](bool ok, const char* what) {
        if (!ok) { ++failures; std::printf("FAILED: %s\n", what); }
    };

    // synthetic: share, detach, independence
    omk::Texture a;
    a.idx.assign(12, 7);
    omk::Texture b = a;
    expect(b.idx.sharesWith(a.idx), "a copy shares its source's storage");
    expect(b.idx.size() == 12 && b.idx[5] == 7, "a copy reads the same bytes");
    omk::Texture c = a;
    // READING a non-const copy must not detach it - the trap the first
    // PixelBuffer fell into with a non-const operator[]
    std::uint8_t readBack = 0;
    for (std::size_t i = 0; i < b.idx.size(); ++i) readBack = static_cast<std::uint8_t>(readBack + b.idx[i]);
    expect(readBack == 84 && b.idx.sharesWith(a.idx) && c.idx.sharesWith(a.idx),
           "reading a non-const copy leaves every copy sharing");
    b.idx.mutableData()[5] = 99;
    expect(!b.idx.sharesWith(a.idx), "a write detaches the copy");
    expect(b.idx[5] == 99, "the written copy holds the write");
    expect(a.idx[5] == 7 && c.idx[5] == 7, "the source and another copy are unchanged");
    expect(c.idx.sharesWith(a.idx), "the untouched copy still shares");
    omk::Texture d = a;
    d.idx = {1, 2, 3};
    expect(!d.idx.sharesWith(a.idx) && d.idx.size() == 3 && a.idx.size() == 12,
           "brace-assignment gives fresh storage");
    omk::Texture e = a;
    e.idx.assign(4, 1);
    expect(!e.idx.sharesWith(a.idx) && a.idx[0] == 7, "assign gives fresh storage");
    const omk::Texture empty;
    expect(empty.idx.empty() && empty.idx.size() == 0 && empty.idx.data() == nullptr,
           "a default buffer is empty");

    // a real set, copied into a pool the way the viewer builds one
    const auto model = slurp(argv[1]);
    const auto tex = slurp(argv[2]);
    const auto src = omk::textures(model, tex);
    std::vector<omk::Texture> pool;
    for (int round = 0; round < 3; ++round) pool.insert(pool.end(), src.begin(), src.end());
    long textures = 0, shared = 0, identical = 0;
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < pool.size(); ++i) {
        const auto& s = src[i % src.size()];
        ++textures;
        shared += pool[i].idx.sharesWith(s.idx) || s.idx.empty();
        identical += pool[i].idx.size() == s.idx.size() &&
                     (s.idx.empty() || std::memcmp(pool[i].idx.data(), s.idx.data(), s.idx.size()) == 0);
        if (i < src.size()) bytes += s.idx.size();
    }
    expect(!src.empty(), "the set decodes");
    expect(shared == textures, "every pooled copy shares its source's storage");
    expect(identical == textures, "every pooled copy reads byte-identical");

    std::printf("set textures %zu pooled copies %ld sharing %ld identical %ld pixel bytes %zu\n",
                src.size(), textures, shared, identical, bytes);
    std::printf("failures %ld\n", failures);
    return failures == 0 ? 0 : 3;
}

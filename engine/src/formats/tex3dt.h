// SPDX-License-Identifier: GPL-3.0-or-later
// The .3DT textures that sit beside each .3DO.
//
// One .3dt holds every texture of the matching .3DO, in material order; each
// is a palette followed by image data:
//
//     palette      3 bytes a colour - 16 colours when bpp == 4, else 256
//     image data   `dataSize` bytes, from the material record
//
// `dataSize == 65536` means a raw 256x256 image. Otherwise it is an LZ scheme
// (see decodeImage). The walk is exact: palette + data per material must land
// on exactly the file's length, which holds for all 635 .3DO/.3dt pairs.
#pragma once

#include <memory>
#include <initializer_list>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace omk {

// ---- THE PIXELS, SHARED BETWEEN COPIES (todo/optimization.md step 6) -------
//
// A `Texture` is copied into every list that holds it - a set's own list, the
// viewer's world list, the renderer's pool, a character's - and with the pixels
// in a `std::vector` each copy was all of them again: a live-allocation snapshot
// of the Anekbah street found the world's textures three times over and the
// texture lists at ~27 MB together. The bytes now live in one reference-counted
// buffer that copies share, and any write DETACHES first (copy on write), so a
// copy still behaves as an independent value: writing into one never changes
// another. Every read the old vector served - `data()`, `size()`, `empty()`,
// `operator[]`, iteration - is here with the same meaning.
// `engine/tools/pixel_sharing.cpp` pins the two rules (`verify.py: engine:
// pixel sharing`).
class PixelBuffer {
public:
    PixelBuffer() = default;
    PixelBuffer(std::initializer_list<std::uint8_t> v)
        : p_(std::make_shared<std::vector<std::uint8_t>>(v)) {}
    PixelBuffer& operator=(std::initializer_list<std::uint8_t> v) {
        p_ = std::make_shared<std::vector<std::uint8_t>>(v);
        return *this;
    }
    // A fresh buffer of n copies of v, never shared with anything.
    void assign(std::size_t n, std::uint8_t v) {
        p_ = std::make_shared<std::vector<std::uint8_t>>(n, v);
    }
    std::size_t size() const { return p_ ? p_->size() : 0; }
    bool empty() const { return size() == 0; }
    const std::uint8_t* data() const { return p_ ? p_->data() : nullptr; }
    const std::uint8_t* begin() const { return data(); }
    const std::uint8_t* end() const { return data() + size(); }
    // READ-ONLY, on a const buffer or not. There is deliberately NO non-const
    // `operator[]`: C++ picks that overload for every `[]` on a non-const
    // Texture, reads included, so a copy-on-write one detached - copied the
    // whole texture - on the first texel a sampler READ through a non-const
    // reference, which undoes the saving silently and costs a full copy
    // mid-frame. The first version had it and `pixel_sharing` caught it.
    const std::uint8_t& operator[](std::size_t i) const { return (*p_)[i]; }
    // WRITING is explicit: detach from any other copy, then hand out the bytes.
    std::uint8_t* mutableData() { detach(); return p_ ? p_->data() : nullptr; }
    // For the check: whether two buffers are the same storage.
    bool sharesWith(const PixelBuffer& o) const { return p_ && p_ == o.p_; }

private:
    void detach() {
        if (p_ && p_.use_count() > 1) p_ = std::make_shared<std::vector<std::uint8_t>>(*p_);
    }
    std::shared_ptr<std::vector<std::uint8_t>> p_;
};

struct Texture {
    std::string              name;
    int                      width  = 0;
    int                      height = 0;
    int                      bpp    = 0;
    // THE FILE'S OWN FORM (todo/ram-vs-original.md tier C): width*height
    // palette INDICES, one byte a texel, and the palette - always 256 RGB
    // entries (768 bytes), zero past a 4-bit file's 16, so an index past the
    // file's palette is black, which is what the RGB expansion this replaced
    // drew. `SetMaterialsMemory` keeps exactly this: an arena of 8-bit pages
    // plus 768 bytes a palette; the RGB form held three times the bytes.
    // Both SHARED between copies of this Texture - see `PixelBuffer` above.
    // Empty only on a malformed file.
    PixelBuffer idx;
    PixelBuffer pal;
    // false if the stream did not produce exactly width*height indices. True
    // for every one of the 2534 textures shipped under gamedata/MESHES; kept because
    // this same reader is used on the .3DO files embedded in the SCX stream.
    bool exact = false;

    // Something to draw: the indices for every texel and a whole palette.
    bool hasPixels() const {
        return width > 0 && height > 0 && pal.size() == 768 &&
               idx.size() >= static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
    // Texel k's colour (k = y * width + x).
    void texel(std::size_t k, int& r, int& g, int& b) const {
        const std::uint8_t* p = pal.data() + 3u * idx.data()[k];
        r = p[0]; g = p[1]; b = p[2];
    }
    // Every texel expanded: RGB, 3 bytes a texel (a tool, a dump)...
    std::vector<std::uint8_t> rgbCopy() const;
    // ...and RGBA for an upload, alpha the COLOUR KEY - 0 where the texel is
    // black, 255 elsewhere - exactly `rgbToRgbaKeyed` of the RGB form, taken
    // from a 256-entry table rather than a test a texel. `rgba` holds
    // width*height*4 bytes.
    void toRgbaKeyed(std::uint8_t* rgba) const;
    std::size_t keptBytes() const { return idx.size() + pal.size(); }
    // A w x h texture of one colour (a probe's): every index 0, entry 0 the
    // colour, the rest of the palette black.
    static Texture solid(std::string name, int w, int h, std::uint8_t r, std::uint8_t g,
                         std::uint8_t b) {
        Texture t;
        t.name = std::move(name); t.width = w; t.height = h; t.bpp = 8; t.exact = true;
        t.idx.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        t.pal.assign(768, 0);
        std::uint8_t* p = t.pal.mutableData();
        p[0] = r; p[1] = g; p[2] = b;
        return t;
    }
};

// Expand `data` until `want` palette indices are produced.
//
// The first byte is always a literal. After it the stream is groups of eight:
// a control byte supplies 8 flags, MSB first. A clear flag copies one literal
// byte; a set flag introduces a token
//
//     size = (token & 0xFC) / 4 + 3        the run length
//     kind =  token & 3
//        0   repeat the previous pixel size-1 times
//        1   back-reference, next byte     + 1 = offset
//        2   back-reference, next two bytes (big-endian) + 1 = offset
//        3   back-reference, next byte * 256 = offset
//
// Always returns exactly `want` bytes: a run that overshoots is trimmed and a
// short stream is padded with its last pixel, so a caller never gets a partial
// image. `exact` reports which happened.
std::vector<std::uint8_t> decodeImage(std::span<const std::byte> data,
                                      std::size_t want, bool& exact);

// Decode every texture of a model, from the two files' bytes.
// `d` is the .3DO, `t` the .3dt. -> one entry a material, in material order.
std::vector<Texture> textures(std::span<const std::byte> d,
                              std::span<const std::byte> t);

// RGB to RGBA WITH THE COLOUR KEY in alpha - 0 where the texel is black, 255
// elsewhere - as a GPU upload wants it (GLES; the Vulkan upload's rule).
// `n` pixels, `rgb` 3n bytes in, `rgba` 4n bytes out. Every texture of a pool
// change goes through it on the frame of a hand-over (2026-09-25: ~4 ms a
// 256x256 atlas on a console), so it has a NEON loop - 16 pixels at a time -
// wherever `__ARM_NEON` is, beside the generic one; integer work, so the two
// are equal by construction, and `vita_bench`'s `texkey` stage hashes both.
void rgbToRgbaKeyed(const std::uint8_t* rgb, std::uint8_t* rgba, std::size_t n);
void rgbToRgbaKeyedGeneric(const std::uint8_t* rgb, std::uint8_t* rgba, std::size_t n);
void rgbToRgbaKeyedNeon(const std::uint8_t* rgb, std::uint8_t* rgba, std::size_t n);

}  // namespace omk

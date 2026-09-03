// SPDX-License-Identifier: GPL-3.0-or-later
// I2D - the engine's 2D layer: the display list, its primitives and its flags.
//
// `LIBI2D` and `libpoly2d/gereaff.c` are the engine's own names for it, out of
// the `C:\Omikron\Sources\...` strings it sprintf's into DirectX error
// messages. It is **not** part of the 3D path: every primitive ends in an
// `IDirectDrawSurface::Blt` onto the back buffer, and `I2D_Flush` (0x00428B00)
// is one line of the frame - clear render state 14
// (`D3DRENDERSTATE_ZWRITEENABLE`), walk the list, set it again, reset the
// pools.
//
// **WHAT IS PORTED, AND WHERE THE LINE IS.** The DirectDraw back end is not
// here and cannot be: this tree has no surface to blit onto, and `Blt` is on
// the far side of the same boundary as the six x87 rasterizers. What IS here
// is everything that decides *what would be drawn and in what order* - the
// display list, the seven pools, the acceptance tests each submitter applies,
// and the three flag banks. That is the same line the renderer port draws
// (`docs/ASSETS.md` §4b: the bucket key and the cache are RUN, the triangles
// are not), and it is where the interesting behaviour actually lives.
//
// **THE STANDARD: read and explained, with ONE data-falsifiable check.** The
// list is code with no shipped data behind it, so there is nothing for a
// corpus to contradict, and pixels cannot be diffed. The exception is the flag
// half: the shipped widget tree carries 139 flag constants, and every one of
// them has to resolve to a bank or `I2D_SetFlag` would silently drop it. That
// check the data can fail, and `verify.py: engine I2D` runs it. Everything
// else asserted below is an invariant of the transcription - useful (it caught
// the ordering below) but not evidence about the original.
//
// **THE ORDERING, WHICH THE DOCS HAD WRONG.** `docs/UI.md` called
// `dword_4E97B8` "a per-layer tail cache so an insert is O(1) once a layer has
// been used". It is a per-layer **head** cache: the assembly writes it on both
// exits of the list walk (`loc_42850C`, `loc_428524`) and **not** on the
// cache-hit path (`loc_428534`), which only does
//
//     new->next = cached->next;  cached->next = new;
//
// So the cache holds the FIRST node of a layer for ever, and every later node
// on that layer is spliced in immediately after it. Within one layer the first
// primitive submitted draws first and the rest draw in **reverse submission
// order**. Across layers the list is still sorted, which is the property the
// layer exists for; inside a layer, submission order is not draw order. A
// port that used a real tail cache would be right about every layer boundary
// and wrong about every overlap inside one, and no screenshot would say which.
#pragma once

#include "ui/surface.h"

#include <cstdint>
#include <string>
#include <vector>

namespace omk {

inline constexpr int kI2dLayers = 16;

// The seven pools, from the submitters' own bounds checks. `kI2dNodeCap` is
// the display list's own limit, and it is **exactly the sum of the seven** -
// 4096 + 200 + 100 + 220 + 220 + 16 + 10 - so the list can never fill before
// the pools do, and the 4862 the docs quote is derived rather than arbitrary.
// Reaching that identity needs the dead pool in the count, which is how the
// two unreferenced primitives below were found.
inline constexpr int kI2dNodeCap = 4862;

enum class I2dPrim {
    Line = 0,        // I2D_DrawLine       0x00428430  -> sub_4822F0
    Triangle,        // I2D_DrawTriangle   0x00428560  -> sub_4806C0
    Quad,            // I2D_SubmitQuad     0x004285E0  -> sub_480BD0
    BlitSurface,     // I2D_BlitSurface    0x00428850  -> sub_480F60
    BlitBitmap,      // I2D_BlitBitmap     0x004287A0  -> sub_4810D0
    View3d,          // I2D_Submit3DView   0x00428900  -> sub_4812E0
    Rect24,          // sub_428660         0x00428660  -> sub_481000   DEAD
    FullScreen,      // I2D_BlitFullScreen 0x00428780  -> sub_481170   no pool
    Count
};

struct I2dPoolInfo {
    const char*   name;
    int           cap;        // 0 = no pool: FullScreen enqueues a null payload
    int           stride;     // the payload size in bytes
    std::uint32_t submitter;  // the address of the function that fills it
    std::uint32_t drawer;     // the address of the callback it enqueues
    bool          referenced; // does anything in the binary call the submitter
};
const I2dPoolInfo& i2dPool(I2dPrim p);

// A 2D point is three ints. A line is two of them, a triangle three, a quad
// four - and a blit's "rectangle" is two corner points, which is why the
// readers take [0], [1], [3], [4] and step over the z at [2] and [5].
struct I2dPoint { int x = 0, y = 0, z = 0; };

// A blit's payload, as far as the back end needs it: the two rectangles, the
// bitmap it reads from, and the flag bits the drawer turns into DDBLT_KEYSRC
// and DDBLT_KEYDEST. The engine's record is 56 bytes and carries a pointer to
// the 264-byte cache entry; here the bitmap is an index into a table the caller
// owns, because nothing in this tree loads a DirectDraw surface.
struct I2dBlit {
    I2dPoint src[2], dst[2];
    int bitmap = -1;
    std::uint32_t flags = 0;      // bit 0 -> key source, bit 1 -> key dest
};

// One node of the display list: 16 bytes, `{drawer, payload, next, layer}`.
struct I2dNode {
    I2dPrim prim  = I2dPrim::Line;
    int     slot  = 0;      // its index in that primitive's pool
    int     layer = 0;
    int     next  = -1;     // an index here; a pointer in the original
};

// Why a submission was refused. The engine returns 0 and drops the primitive -
// a mis-built payload is never drawn - so which test refused it is the whole
// of the diagnosis, and the sweep asserts each one separately.
enum class I2dRefusal {
    Accepted, LayerOutOfRange, ListFull, PoolFull, DegenerateSrc, DegenerateDest,
};

class I2dList {
public:
    // The submitters. Each pushes its payload into its own pool and enqueues
    // the matching drawer; the payload contents beyond the acceptance tests
    // are not modelled, because nothing here can draw them.
    I2dRefusal line(int layer);
    I2dRefusal triangle(int layer);
    I2dRefusal quad(int layer);
    I2dRefusal fullScreen(int layer);
    // The payload carries the SOURCE rectangle first and the DESTINATION
    // second - points 0/1 then 2/3. That is settled by the drawer, not by the
    // submitter: `sub_4810D0` passes points 2/3 as `lpDestRect` and points 0/1
    // as `lpSrcRect` to `Blt`, which is at vtable +20
    // (QueryInterface/AddRef/Release/AddAttachedSurface/AddOverlayDirtyRect
    // /Blt). An earlier version of this header had the two the other way
    // round; reading the function that CONSUMES the payload is what settled
    // it, which is CLAUDE.md 1 in one line.
    //
    // Both reject a degenerate rectangle before submitting, with three tests
    // and not four: `src.x0 >= src.x1`, `src.y0 >= src.y1` and
    // `dst.x0 >= dst.x1`. **There is no test on the DESTINATION Y**, so a
    // destination rectangle inverted only vertically is accepted and
    // submitted. The asymmetry is in both functions identically, so it is the
    // code and not a transcription slip.
    I2dRefusal blitBitmap(int layer, const I2dPoint src[2], const I2dPoint dst[2],
                          int bitmap = -1, std::uint32_t flags = 0);
    I2dRefusal blitSurface(int layer, const I2dPoint src[2], const I2dPoint dst[2],
                           int bitmap = -1, std::uint32_t flags = 0);
    // The odd one: it calls `Scene_SetActiveCamera` and renders a 3D scene
    // into a 2D rectangle, layered against everything else - the videophone
    // and the terminals putting a live view inside a panel. `scene` non-null
    // is one of its own bounds checks.
    I2dRefusal view3d(int layer, bool sceneValid);

    // The list in DRAW order - `I2D_Flush` walks head to tail calling each
    // node. This is the whole observable output of the layer.
    std::vector<int> order() const;

    // `I2D_Flush`'s walk, executed against the software back end: every node
    // in DRAW order, each blit through `blt`. This is the whole of the 2D
    // path's output, and `PORTING` B5 says why it is exact - a `Blt` is a
    // memory copy with an optional colour key, so there is no filtering to
    // differ from the original's.
    //
    // `bitmaps` is indexed by `I2dBlit::bitmap`; a node naming one that is not
    // there is SKIPPED, the way the drawer's `if (v4 < 0)` path drops a blit
    // rather than failing the frame.
    void present(Surface& fb, const std::vector<Surface>& bitmaps,
                 std::uint16_t srcKey = 0, std::uint16_t dstKey = 0) const;
    const std::vector<I2dBlit>& blits() const { return blits_; }

    // `I2D_Flush` (0x00428B00): the D3D state pair around the walk, then every
    // pool counter, the node count, the head and all 16 cache slots to zero.
    // `zwriteToggles` counts the render-state calls, which the original caches
    // in `dword_8F56D8` so the pair costs one call each way.
    void flush();
    int  zwriteToggles() const { return zwrite_; }
    int  nodes() const { return static_cast<int>(nodes_.size()); }
    int  used(I2dPrim p) const { return used_[static_cast<int>(p)]; }
    const I2dNode& node(int i) const { return nodes_[static_cast<std::size_t>(i)]; }

private:
    I2dRefusal enqueue(I2dPrim p, int layer);

    std::vector<I2dNode> nodes_;
    std::vector<I2dBlit> blits_;
    int head_ = -1;
    int cache_[kI2dLayers] = {};   // the per-LAYER HEAD cache; -1 when unused
    int used_[static_cast<int>(I2dPrim::Count)] = {};
    int zwrite_ = 0;
    bool init_ = false;
    void ensure();
};

// ---------------------------------------------------------------- the flags
//
// A flag constant is `bank | bit`, and the bank picks which word it lives in,
// which is what lets one helper serve structures of different shapes. The
// three banks are 0x20000000, 0x40000000 and 0x80000000 - `docs/UI.md`'s table
// abbreviated the first two to 0x20 and 0x40, which is not what
// `I2D_FlagBank` (0x00428EA0) tests.
inline constexpr std::uint32_t kI2dBankA = 0x20000000u;   // -> word 0, +48
inline constexpr std::uint32_t kI2dBankB = 0x40000000u;   // -> word 1, +52
inline constexpr std::uint32_t kI2dBankC = 0x80000000u;   // -> word 2, +56

// -> the bank, or 0 for a constant that names none. The original returns -1
// there, and `I2D_SetFlag`'s three-way `if` simply falls through and does
// nothing - so a constant with no bank is silently dropped, which is exactly
// why "every shipped constant resolves" is worth asserting.
std::uint32_t i2dFlagBank(std::int32_t flag);
int  i2dFlagWord(std::int32_t flag);          // 0/1/2, or -1

// The three words a row widget carries at +48/+52/+56.
struct I2dFlagWords { std::uint32_t w[3] = {0, 0, 0}; };

bool i2dTestFlag(const I2dFlagWords& f, std::int32_t flag);
void i2dSetFlag(I2dFlagWords& f, std::int32_t flag, bool on);
// `I2D_SetFlagOnAllRows` (0x00429140) broadcasts one over every row of a page.
void i2dSetFlagOnAllRows(std::vector<I2dFlagWords>& rows, std::int32_t flag, bool on);

}  // namespace omk

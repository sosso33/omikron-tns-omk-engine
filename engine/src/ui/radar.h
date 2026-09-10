// SPDX-License-Identifier: GPL-3.0-or-later
// THE RADAR - shoot mode's minimap, item 0x4C4388 of screen 34.
//
// It is not a bitmap. It is a WIREFRAME of the arena seen from above and
// behind the player, drawn by the item's own callback 0x42F000 through a
// camera of its own, with the player a blue square and every gunman a red one.
//
// **Where the file comes from.** `Area_TickLoad` copies the AREA's `+106` (the
// MAP2D stem) into a buffer, appends ".MPT" (the five bytes at 0x4C0D34) and
// calls `Map2D_Load`, whose tail - 0x42EE70, which the decompilation calls
// `Ambience_Load`, wrongly - rewrites the last three characters IN PLACE to
// "WRE", clears the radar's enable flag `dword_4EB8C8` and file `4EB8C4`, and
// compares the name against nine literals at 0x4C4884. On a match it sets the
// height `dword_90E0B4` and loads `RADAR\<name>` into `4EB8C4`, and zeroes the
// 100 "seen" dwords at 0x4EB678. The compare is exact and case-sensitive, and
// every shipped `+106` is upper case, so exactly nine areas get a radar - and
// seven that have a map do not, the Shooting gallery (59) among them. Six of
// the fifteen `.WRE` files that ship can never load.
//
// **The file**, read off the draw: `u32 vertices, u32 edges, float3[vertices],
// u16 pair[edges]`. It lands exactly on the file size for 11 of the 15 - all
// nine loadable ones - and the four that fall 4 bytes short are unreachable.
// A vertex's Y is UP (the negated world Y): the draw adds the player's world Y
// to it to get a relative height.
//
// **Enabling.** Screen 34's open callback 0x42E3A0 sets `4EB8C8 = 1`, moves
// the item to (456, 8) 174x131, stores that box display-scaled
// (`I2D_ScaleX/Y`) in `90E0C4` / `90E0BC` / `90E0C0` / `90E0B8` (left, top,
// right, bottom) and sets the camera distance `0x4C4134` to 275.59 (7 m; the
// static value is 236.22, 6 m). 0x42E870 hides the item (`0x40000001`)
// whenever the flag or the file is missing.
//
// **The draw, 0x42F000.** A camera matrix from `sub_442160(pi/2, (facing +
// 180) deg, 0)` - pitch 90, the third angle `0x4EB8CC` never written - with no
// translation; the focal lengths are the LIVE camera's (`sub_4943D0`: half the
// viewport over tan(fov/2), the height's times 4/3) scaled from the viewport
// to the box and by 0.7; the centre is the box's. Every point goes through
//
//     theta = (facing + 180 - 90) deg
//     T = (D cos theta - px,  py - height,  D sin theta - pz)
//     p = (x + T.x,  y + T.y,  -(z + T.z))      for a .WRE vertex
//     p = (ax + T.x, T.y - ay, -(az + T.z))     for an actor (and the player)
//
// then `sub_442F00` (the matrix, row-vector) and `sub_441E50` (divide by the
// depth), and counts only when the depth is at least 10.0 (0x4BC300).
//
// * An EDGE with both ends in front is clipped to the box by `sub_42EC80`
//   (integer, transcribed with its own divisions) and drawn as an opaque
//   GOURAUD line (`I2D_DrawLine` flags 0x18) from one end's grey to the
//   other's. The grey is `0x80 +- 0x60` by the end's height relative to the
//   player (`y + py`), linear inside +-118.11 (3 m) with slope 0x5F; the
//   layer is 4, plus one for each end above him.
// * The PLAYER is an 8x8 quad (`sub_42EBA0`, clipped to the box) in 0x0000FF
//   - BLUE - on layer 6.
// * Each actor slot whose state byte (`+1307`) is 2 - attached to the scene
//   by `Actor_Attach` - and which is not the player: in ACTOR_STATE 3 (set by
//   `Shoot_ActorEnter`, 0x422C10) it is marked seen and gets a RED quad whose
//   red is `0x80 +- 0x60` by `py - ay`, linear inside +-314.96 (8 m) with
//   slope 31; in ACTOR_STATE 0 (the brain's `health <= 0` arm writes it)
//   after being seen, a dim 0x10. Layer 5.
//
// The I2D list draws layers ascending and, inside one, in REVERSE submission
// order (the head cache), so the player's square is under the layer-6 edges
// and the gunmen's under the layer-5 ones.
//
// **Reconstruction, labelled.** The seen flags are kept by actor id here, the
// engine's by slot; the line's pixel rule is Bresenham without its last pixel
// where the D3D rasterizer has its own diamond-exit rule; and the min/max
// height the draw tracks over the edges is computed and never read, so it is
// left out.
#pragma once

#include "platform/datafs.h"
#include "ui/surface.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace omk {

struct RadarWire {
    std::vector<float>         v;   // x, y, z per vertex
    std::vector<std::uint16_t> e;   // index pairs
    bool valid() const { return !v.empty(); }
    int vertices() const { return static_cast<int>(v.size() / 3); }
    int edges() const { return static_cast<int>(e.size() / 2); }
};

// -> an empty wire unless the file lands exactly on its size and every edge
// index names a vertex.
RadarWire readRadarWire(std::span<const std::byte> d);

// 0x42EE70's table: the radar HEIGHT for a name already rewritten to ".WRE",
// or a negative value when the engine loads no radar for it.
float radarHeightFor(const std::string& wreName);

struct RadarActor {
    int   id = -1;
    float pos[3] = {0, 0, 0};
    int   state = 0;               // ACTOR_STATE: 3 in the fight, 0 once killed
};

struct RadarView {
    int   left = 0, top = 0, right = 0, bottom = 0;   // 90E0C4 / BC / C0 / B8
    int   screenW = 640, screenH = 480;               // the live camera's +412/+414
    float fovDeg = 75.0f;                             // the live camera's fov
    float player[3] = {0, 0, 0};                      // actor +244..+252
    float facingDeg = 0.0f;                           // actor +420
};

struct RadarFrame {
    int  edgesInFront = 0;   // both ends at depth >= 10
    int  lines = 0;          // ...and accepted by the clip
    int  blips = 0;
    bool player = false;
    int  playerX = 0, playerY = 0;   // where his square was centred
};

class Radar {
public:
    // `Map2D_Load`'s tail for `mapFile` as `Area_TickLoad` builds it,
    // "<+106>.MPT". -> whether a radar is now loaded.
    bool load(const DataFs& fs, const std::string& mapFile);
    bool loaded() const { return wire_.valid(); }
    const std::string& file() const { return file_; }
    float height() const { return height_; }
    const RadarWire& wire() const { return wire_; }
    // screen 34's open callback, 0x42E3A0
    void open() { enabled_ = true; distance_ = 275.5905456542969f; }
    bool enabled() const { return enabled_; }
    RadarFrame draw(Surface& fb, const RadarView& v, const std::vector<RadarActor>& actors);

    // The box 0x42E3A0 moves the item to, in 640x480 units.
    static constexpr int kBoxX = 456, kBoxY = 8, kBoxW = 174, kBoxH = 131;

private:
    RadarWire   wire_;
    std::string file_;
    float height_   = 0.0f;
    float distance_ = 236.22047424316406f;   // 0x4C4134's static value
    bool  enabled_  = false;
    std::map<int, bool> seen_;               // 0x4EB678, by actor id here
};

}  // namespace omk

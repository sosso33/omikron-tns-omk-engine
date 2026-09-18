// SPDX-License-Identifier: GPL-3.0-or-later
// THE SNEAK'S CITY MAP - `Lire plan`, screen 9's third inventory tile.
//
// Confirming item `0x004DE3C8` runs callback `0x0049BC40`, which is
// `sub_42A370(screen, off_4DF190)` and nothing else: it installs a panel that
// no item's `+44` names and that `tables/ui_widgets.json` did not carry until
// `exetables.py`'s CODE_NAMED was told about it (`ui/widgets.h`,
// `kPanelSneakMap`).
//
// ------------------------------------------------------- what the page IS
//
// `sub_49D9E0`, the panel's `+4`:
//
//   1. `dword_93076C + 0x30` - the resident decor node's own path - through
//      `strrchr(s, '\\')`, then `strncpy(dst, base, strlen(base) - 4)`. So
//      `MESHES\DECORS\ANEKBAH.3DO` becomes `ANEKBAH`;
//   2. `sprintf("%s.bmp")` then `sprintf("Images\\%s")`, `fopen`/`fclose` to
//      test it, `I2D_LoadBitmap` to load it, stored in `dword_4DECB4` - which
//      is item `0x004DEC78`'s own `+0x3C`;
//   3. `_strupr` and a linear scan of the 52-byte table at `0x004DF1F8`,
//      storing the matched row's first dword in `dword_4DECFC` - which is item
//      `0x004DECC0`'s `+0x3C` - or **-1** when nothing matches;
//   4. **and if the `fopen` failed, `sub_42A370(screen, off_4DEE50)` at once**,
//      so a location with no map bounces straight back to the Inventaire page.
//      Only the four cities have one, so that is the usual outcome.
//
// `sub_49DB80`, the `+8` leave, frees the bitmap.
//
// ------------------------------------------------- how a point is PLACED
//
// Item `0x004DECC0`'s draw hook `0x0049E6F0` re-finds the city row from the
// tag and projects every point through it, in the hook's own order:
//
//     px = item.x + (int)((X - row.x0) * item.w / row.xspan)
//     py = item.y + (int)((Z - row.z0) * item.h / row.zspan)
//
// with `item.w/h` the widget's 640x480 and `item.x/y` its 0,0. `row.zspan` is
// negative in all four rows, which is what turns the world's +z into the
// bitmap's -y. The engine then scales the RESULT through `I2D_ScaleX/Y`, so
// the arithmetic happens at the authored 640x480 whatever the display is.
//
// The player is `dword_930724 + 0xF4` and `+0xFC` (his x and z as floats) and
// his facing is `+0x1A4`, the same `actor+420` yaw the walker keeps. The
// markers are the ENABLED slider destinations - `GLOBAL +16` filtered by the
// DB's AddressEnabled bits, the list the slider page already shows - kept when
// the row's name begins with the city's, e.g. `Anekbah - Morgue zone 42`. The
// hook strips the city and the separator (3 characters when the character
// after the name is a space, otherwise 2) and looks the remainder up in the
// **44-byte** table at `0x004DF2C8`; a hit takes that row's `+32`/`+40` and a
// miss falls back on `sub_40E630`, which resolves the destination's bit
// against the resident chunk's ADDRESS table.
//
// **The addresses are in the same space as the city rectangle, and that is a
// result rather than an assumption.** `Area_Load` converts an address record
// through `rawToWorld` and stores the truncated integer back into the field,
// which is what `fild dword ptr [eax]` then reads. Read that way all **39**
// shipped destinations land inside their own city's rectangle; read as the raw
// file value **0 of 39** do.
//
// Both tables are `.data` in the executable, so they are lifted to
// `tables/city_maps.json` like the widget tree and the VM table.
//
// ----------------------------------------------------------- the standard
//
// TIER 6, read and explained. The geometry is transcribed call by call from
// the two hooks and the projection is checked against the shipped data
// (`verify.py: engine: sneak map`), but no capture of the original's map page
// exists, so nothing here is compared against a frame the engine drew.
//
// **What is NOT ported, and why.** `sub_40E630` is not a lookup - it is the
// TRANSPORT, and its first act is to `Area_Load` the destination's area when
// that area is not resident. The port resolves a marker only against the
// ADDRESS TABLE OF THE RESIDENT CHUNK and drops a destination it cannot place,
// rather than loading an area from a draw hook. In the shipped data that costs
// nothing: every destination of a city carries that city's own area id, so the
// engine's own fast path (`cmp ecx, edx; jz loc_40E886`) is the one that runs
// whenever the map is open at all. A marker the port drops is reported.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace omk {

// One row of the 52-byte table at `0x004DF1F8`.
struct CityMapRow {
    int         id = -1;        // `+0`, and it is the row's own index
    std::string name;           // `+4`, the uppercased bitmap stem
    float       x0 = 0, z0 = 0; // `+36`/`+40` - the map's corner, in world units
    float       xspan = 0, zspan = 0;  // `+44`/`+48` - its span; zspan < 0
};

// One row of the 44-byte table at `0x004DF2C8` - a named place with an
// authored position that OVERRIDES the address its destination's bit names.
struct CityPlaceRow {
    std::string name;             // `+0`, 32 bytes
    float       pos[3] = {0, 0, 0};  // `+32`/`+36`/`+40`; the hook uses x and z
};

class CityMaps {
public:
    static CityMaps loadJson(const std::string& path);
    bool valid() const { return !cities_.empty(); }

    // `sub_49D9E0`'s step 3: `_strupr` then a linear scan. -> nullptr when the
    // name is not one of the four, which is `dword_4DECFC = -1`.
    const CityMapRow* findCity(const std::string& setStem) const;
    // `0x0049E6F0`'s inner `strcmp` walk. Exact, and over the raw bytes - the
    // table carries the five shipped languages, so a French name matches its
    // French row. -> nullptr when the place is not one of the fifteen.
    const CityPlaceRow* findPlace(const std::string& name) const;

    const std::vector<CityMapRow>& cities() const { return cities_; }
    const std::vector<CityPlaceRow>& places() const { return places_; }

private:
    std::vector<CityMapRow>   cities_;
    std::vector<CityPlaceRow> places_;
};

// `sub_49D9E0`'s step 1, as its own function so a check can run it: the
// basename after the last `\` or `/`, minus its last four characters. -> ""
// when the path has no separator, which is the one arm that makes the engine
// read an uninitialised buffer; the port declines instead.
std::string mapStemFromNodePath(const std::string& path);

// The projection above. `w`/`h` and `x`/`y` are the ITEM's, at 640x480.
void cityMapProject(const CityMapRow& row, float worldX, float worldZ,
                    int itemX, int itemY, int itemW, int itemH,
                    int* px, int* py);

// Strip `"<City> - "` from a destination name, the way `0x0049E6F0` does:
// the first `len` characters must contain the city name once uppercased, and
// the remainder starts `len + 3` in when `name[len]` is a space and `len + 2`
// otherwise. -> "" when the row does not belong to this city.
std::string cityMapPlaceName(const std::string& destination,
                             const std::string& cityName);

// Oscillator 2 (`sub_42B5E0(2)`), whose `+0x18` is the alpha both the pin and
// the markers take: a triangle between 45 and 200 over a 1000 ms period
// (`sub_42B700`'s two arms). `ui/models.h` reads oscillator 4 the same way and
// for the same reason - the wrap callback is taken to be a modulo.
int cityMapPulse(long clockMs);

}  // namespace omk

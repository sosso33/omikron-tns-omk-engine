// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/citymap.h"

#include "platform/json.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace omk {
namespace {

std::string upper(const std::string& s) {
    std::string o = s;
    for (char& c : o)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return o;
}

}  // namespace

CityMaps CityMaps::loadJson(const std::string& path) {
    CityMaps m;
    const Json j = Json::parseFile(path);
    const Json& cities = j["rows"]["cities"]["rows"];
    for (std::size_t i = 0; i < cities.size(); ++i) {
        const Json& r = cities[i];
        CityMapRow row;
        row.id = static_cast<int>(r["id"].i64(-1));
        row.name = r["name"].str();
        row.x0 = static_cast<float>(r["x0"].num());
        row.z0 = static_cast<float>(r["z0"].num());
        row.xspan = static_cast<float>(r["xspan"].num());
        row.zspan = static_cast<float>(r["zspan"].num());
        // A row with no span cannot divide, and the engine's own loop would
        // have produced a division by zero rather than a silent wrong answer.
        if (row.id < 0 || row.name.empty() || row.xspan == 0 || row.zspan == 0)
            continue;
        m.cities_.push_back(row);
    }
    const Json& places = j["rows"]["places"]["rows"];
    for (std::size_t i = 0; i < places.size(); ++i) {
        const Json& r = places[i];
        CityPlaceRow row;
        row.name = r["name"].str();
        row.pos[0] = static_cast<float>(r["x"].num());
        row.pos[1] = static_cast<float>(r["y"].num());
        row.pos[2] = static_cast<float>(r["z"].num());
        if (row.name.empty()) continue;
        m.places_.push_back(row);
    }
    return m;
}

const CityMapRow* CityMaps::findCity(const std::string& setStem) const {
    const std::string up = upper(setStem);
    for (const CityMapRow& r : cities_)
        if (r.name == up) return &r;
    return nullptr;
}

const CityPlaceRow* CityMaps::findPlace(const std::string& name) const {
    for (const CityPlaceRow& r : places_)
        if (r.name == name) return &r;
    return nullptr;
}

std::string mapStemFromNodePath(const std::string& path) {
    const std::size_t cut = path.find_last_of("\\/");
    // `strrchr(s, '\\')` returning null is the arm that leaves the engine's
    // destination buffer uninitialised; nothing sensible follows, so decline.
    if (cut == std::string::npos) return std::string();
    const std::string base = path.substr(cut + 1);
    if (base.size() <= 4) return std::string();
    return base.substr(0, base.size() - 4);
}

void cityMapProject(const CityMapRow& row, float worldX, float worldZ,
                    int itemX, int itemY, int itemW, int itemH,
                    int* px, int* py) {
    // `fld X; fsub row.x0; fimul item.w; fdiv row.xspan; _ftol; add item.x` -
    // the multiply is by the item's width as an INTEGER pushed through
    // `fimul`, and `_ftol` truncates toward zero.
    const double x = (static_cast<double>(worldX) - row.x0) *
                     static_cast<double>(itemW) / row.xspan;
    const double y = (static_cast<double>(worldZ) - row.z0) *
                     static_cast<double>(itemH) / row.zspan;
    if (px) *px = itemX + static_cast<int>(x);
    if (py) *py = itemY + static_cast<int>(y);
}

std::string cityMapPlaceName(const std::string& destination,
                             const std::string& cityName) {
    const std::size_t len = cityName.size();
    if (len == 0 || destination.size() < len) return std::string();
    // `strncpy(tmp, name, len); _strupr(tmp); strstr(tmp, city)` - the city
    // must appear inside the row's own first `len` characters, which for a
    // name of that length means it must BE them.
    if (upper(destination.substr(0, len)) != cityName) return std::string();
    const std::size_t skip = destination[len] == ' ' ? len + 3 : len + 2;
    if (skip >= destination.size()) return std::string();
    return destination.substr(skip);
}

int cityMapPulse(long clockMs) {
    // Oscillator 2 ships {period 1000, lo 45, hi 200}; `sub_42B700` ramps
    // lo -> hi over the first half of the period and back over the second.
    constexpr long kPeriod = 1000;
    constexpr int  kLo = 45, kHi = 200;
    const long half = kPeriod / 2;
    long phase = clockMs % kPeriod;
    if (phase < 0) phase += kPeriod;
    if (phase < half)
        return kLo + static_cast<int>((kHi - kLo) * phase / half);
    return kHi - static_cast<int>((kHi - kLo) * (phase - half) / half);
}

}  // namespace omk

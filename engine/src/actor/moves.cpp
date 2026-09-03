// SPDX-License-Identifier: GPL-3.0-or-later
#include "actor/moves.h"

#include "platform/json.h"

#include <cstdlib>

namespace omk {

SpecialMoves SpecialMoves::loadJson(const std::string& path) {
    SpecialMoves m;
    const Json j = Json::parseFile(path);
    const Json& rows = j["rows"];
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Json& r = rows[i];
        Row row;
        row.index = static_cast<int>(r["index"].i64(-1));
        row.name = r["name"].str();
        // The handler ships as a "0x00xxxxxx" STRING, not a number - it is
        // an address and the lift keeps it readable. Nothing here calls it;
        // it is carried so a log can name the function the game would run.
        row.handler = static_cast<std::uint32_t>(
            std::strtoul(r["handler"].str().c_str(), nullptr, 0));
        if (row.index < 0 || row.name.empty()) continue;
        m.byName_.emplace(row.name, m.rows_.size());
        m.rows_.push_back(row);
    }
    return m;
}

const SpecialMoves::Row* SpecialMoves::find(const std::string& name) const {
    const auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : &rows_[it->second];
}

}  // namespace omk

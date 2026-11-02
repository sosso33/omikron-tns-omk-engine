// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/keynames.h"

#include "platform/json.h"
#include "ui/options.h"

namespace omk {

KeyNames KeyNames::fromJson(const Json& doc) {
    KeyNames k;
    const Json& rows = doc["rows"]["names"];
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const int type = static_cast<int>(rows[i]["type"].i64());
        const int code = static_cast<int>(rows[i]["code"].i64());
        if (type < 1 || type > 3) continue;   // the table's empty last record
        // the walk takes the FIRST match, so a later duplicate never answers
        k.names_.emplace(std::make_pair(type, code), utf8ToLatin1(rows[i]["name"].str()));
    }
    return k;
}

KeyNames KeyNames::loadJson(const std::string& path) {
    return fromJson(Json::parseFile(path));
}

std::string KeyNames::name(int type, int code) const {
    const auto it = names_.find({type, code});
    if (it == names_.end()) return {};
    if (type == Keyboard && it->second.size() <= 1 && cap_) {
        std::string c = cap_(code);
        if (!c.empty()) return c;
    }
    return it->second;
}

}  // namespace omk

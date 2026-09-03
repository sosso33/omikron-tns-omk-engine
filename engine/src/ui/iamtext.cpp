// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/iamtext.h"

#include "platform/datafs.h"

namespace omk {

std::vector<std::string> iamStrings(const DataFs& iam, const std::string& name) {
    std::vector<std::string> out;
    if (name.empty()) return out;
    const auto d = iam.read(name);
    if (d.empty()) return out;
    std::string cur;
    for (auto b : d) {
        const auto c = static_cast<unsigned char>(b);
        if (c == 0) { out.push_back(cur); cur.clear(); }
        else cur.push_back(static_cast<char>(c));
    }
    if (!cur.empty()) out.push_back(cur);
    // the file ends in a run of empty entries; the reference drops them so an
    // index past the last real string reads as absent rather than as ""
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

}  // namespace omk

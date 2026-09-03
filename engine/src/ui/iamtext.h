// SPDX-License-Identifier: GPL-3.0-or-later
// `IAM\<Screen>` - an interface screen's own strings.
//
// A plain NUL-separated list, and the screen's `+16` names the file. An item's
// `+28` is an index into it, so without this every label the widget walk finds
// is a number.
//
// `IAM\FRENCH\` is a byte-identical duplicate of `IAM\` (same MD5), not a
// second corpus - so there is one language here despite the directory name.
#pragma once

#include <string>
#include <vector>

namespace omk {

class DataFs;

// -> the strings of `IAM/<name>`, trailing blanks dropped. Empty when the file
// does not exist, which is not an error: five of the 37 screens name none.
std::vector<std::string> iamStrings(const DataFs& iam, const std::string& name);

}  // namespace omk

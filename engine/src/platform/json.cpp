// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/json.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace omk {
namespace {
const Json kNull;
const std::string kEmpty;
}

void Json::skipWs(const std::string& s, std::size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
                            s[i] == '\r')) ++i;
}

Json Json::parseValue(const std::string& s, std::size_t& i) {
    Json v;
    skipWs(s, i);
    if (i >= s.size()) return v;
    const char c = s[i];
    if (c == '{') {
        v.type_ = Type::Object;
        ++i;
        skipWs(s, i);
        if (i < s.size() && s[i] == '}') { ++i; return v; }
        while (i < s.size()) {
            skipWs(s, i);
            Json key = parseValue(s, i);           // a string
            skipWs(s, i);
            if (i < s.size() && s[i] == ':') ++i;
            v.object_.emplace(key.string_, parseValue(s, i));
            skipWs(s, i);
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == '}') { ++i; break; }
            break;
        }
        return v;
    }
    if (c == '[') {
        v.type_ = Type::Array;
        ++i;
        skipWs(s, i);
        if (i < s.size() && s[i] == ']') { ++i; return v; }
        while (i < s.size()) {
            v.array_.push_back(parseValue(s, i));
            skipWs(s, i);
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == ']') { ++i; break; }
            break;
        }
        return v;
    }
    if (c == '"') {
        v.type_ = Type::String;
        ++i;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                ++i;
                switch (s[i]) {
                    case 'n': v.string_.push_back('\n'); break;
                    case 't': v.string_.push_back('\t'); break;
                    case 'r': v.string_.push_back('\r'); break;
                    case 'u': {   // the only escapes exetables writes are ASCII
                        if (i + 4 < s.size()) {
                            const auto code = std::strtol(
                                s.substr(i + 1, 4).c_str(), nullptr, 16);
                            v.string_.push_back(static_cast<char>(code & 0x7F));
                            i += 4;
                        }
                        break;
                    }
                    default: v.string_.push_back(s[i]); break;
                }
                ++i;
                continue;
            }
            v.string_.push_back(s[i++]);
        }
        if (i < s.size()) ++i;
        return v;
    }
    if (s.compare(i, 4, "true") == 0)  { v.type_ = Type::Bool; v.bool_ = true;  i += 4; return v; }
    if (s.compare(i, 5, "false") == 0) { v.type_ = Type::Bool; v.bool_ = false; i += 5; return v; }
    if (s.compare(i, 4, "null") == 0)  { i += 4; return v; }
    char* end = nullptr;
    v.number_ = std::strtod(s.c_str() + i, &end);
    if (end && end > s.c_str() + i) {
        v.type_ = Type::Number;
        i = static_cast<std::size_t>(end - s.c_str());
    } else {
        ++i;                                       // do not spin on junk
    }
    return v;
}

Json Json::parse(const std::string& text) {
    std::size_t i = 0;
    return parseValue(text, i);
}

Json Json::parseFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return Json();
    std::stringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
}

const Json& Json::operator[](const std::string& key) const {
    const auto it = object_.find(key);
    return it == object_.end() ? kNull : it->second;
}

const Json& Json::operator[](std::size_t i) const {
    return i < array_.size() ? array_[i] : kNull;
}

std::size_t Json::size() const {
    return type_ == Type::Array ? array_.size()
         : type_ == Type::Object ? object_.size() : 0;
}

double Json::num(double f) const { return type_ == Type::Number ? number_ : f; }
std::int64_t Json::i64(std::int64_t f) const {
    return type_ == Type::Number ? static_cast<std::int64_t>(number_) : f;
}
bool Json::boolean(bool f) const { return type_ == Type::Bool ? bool_ : f; }
const std::string& Json::str() const {
    return type_ == Type::String ? string_ : kEmpty;
}

}  // namespace omk

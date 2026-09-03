// SPDX-License-Identifier: GPL-3.0-or-later
// A minimal JSON reader, for `tables/*.json`.
//
// The other tables are read with a flat `find("\"op\":")` scan because their
// shape is one flat row list. The widget tree is not: panels hold lists hold
// items, and a scan cannot tell which nesting level a key belongs to. So this
// exists - about a hundred lines, no dependency, and it only has to read what
// `tools/exetables.py` writes.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace omk {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    static Json parseFile(const std::string& path);
    static Json parse(const std::string& text);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }

    // Accessors that never throw: a missing key or a wrong type gives the
    // fallback. A table file that changed shape should fail a CHECK, not
    // crash a reader.
    const Json& operator[](const std::string& key) const;
    const Json& operator[](std::size_t i) const;
    std::size_t size() const;
    // The object's key/value pairs, for a map whose keys are data - the name
    // switch is keyed by character code.
    const std::map<std::string, Json>& members() const { return object_; }

    double      num(double fallback = 0) const;
    std::int64_t i64(std::int64_t fallback = 0) const;
    bool        boolean(bool fallback = false) const;
    const std::string& str() const;

private:
    Type type_ = Type::Null;
    double number_ = 0;
    bool   bool_ = false;
    std::string string_;
    std::vector<Json> array_;
    std::map<std::string, Json> object_;

    static Json parseValue(const std::string& s, std::size_t& i);
    static void skipWs(const std::string& s, std::size_t& i);
};

}  // namespace omk

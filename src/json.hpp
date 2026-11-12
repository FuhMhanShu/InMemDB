#pragma once

#include "memdb/document.hpp"
#include "memdb/value.hpp"

#include <string>
#include <string_view>

namespace memdb::detail {

std::string writeJsonString(const std::string& s);
std::string writeJson(const Value& v);
std::string writeJsonDocument(const Document& doc);

Value parseJson(std::string_view s);

} // namespace memdb::detail

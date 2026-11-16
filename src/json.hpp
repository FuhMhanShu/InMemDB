#pragma once

#include "memdb/document.hpp"
#include "memdb/schema.hpp"
#include "memdb/value.hpp"

#include <string>
#include <string_view>

namespace memdb::detail {

std::string writeJsonString(const std::string& s);
// v0.37: optionales depth-Argument (Rekursionsschutz gegen zu tiefe/zyklische Value-Baeume,
// Default 0 – bestehende Aufrufe mit einem Argument kompilieren weiter).
std::string writeJson(const Value& v, int depth = 0);
std::string writeJsonDocument(const Document& doc, int depth = 0);

Value parseJson(std::string_view s);

std::string writeJsonSchema(const Schema& schema);
Schema parseJsonSchema(const Value& v);

} // namespace memdb::detail

#pragma once

#include "memdb/import.hpp"

#include <map>
#include <vector>

namespace memdb::detail {

/// Typ-Regel: Null wird ignoriert, Int+Double -> Real, gleiche Typen bleiben, sonst Unknown.
inline SourceType widenSourceType(SourceType a, SourceType b) {
    if (a == SourceType::Unknown || b == SourceType::Unknown)
        return SourceType::Unknown;
    if (a == SourceType::Null)
        return b;
    if (b == SourceType::Null)
        return a;
    if (a == b)
        return a;
    if ((a == SourceType::Integer && b == SourceType::Real) ||
        (a == SourceType::Real && b == SourceType::Integer))
        return SourceType::Real;
    return SourceType::Unknown;
}

/// Spalten = Vereinigung der Top-Level-Keys aller Zeilen (Reihenfolge des ersten
/// Auftretens); Typ pro Spalte gewichtet. Nur-Null-Spalten -> Unknown.
inline void collectColumns(const std::vector<Document>& rows, std::vector<ColumnInfo>& out) {
    std::vector<std::string> names;
    std::map<std::string, SourceType> types;
    for (const Document& r : rows)
        r.forEachField([&](const std::string& k, const Value& v) {
            if (types.count(k) == 0) {
                names.push_back(k);
                types[k] = SourceType::Null;
            }
            types[k] = widenSourceType(types[k], sourceTypeFromValueType(v.type()));
        });
    out.clear();
    for (const auto& n : names) {
        ColumnInfo c;
        c.name = n;
        SourceType t = types[n];
        c.type = (t == SourceType::Null) ? SourceType::Unknown : t;
        out.push_back(std::move(c));
    }
}

/// Eine Zeile als Value-Vektor aligned auf `columns` (fehlende Felder -> Null).
inline std::vector<Value> rowToValues(const Document& row, const std::vector<ColumnInfo>& columns) {
    std::vector<Value> vals;
    vals.reserve(columns.size());
    for (const auto& c : columns)
        if (const Value* v = row.find(c.name))
            vals.push_back(*v);
        else
            vals.push_back(Value(nullptr));
    return vals;
}

} // namespace memdb::detail

#pragma once

#include "import.hpp"

#include <memory>

namespace memdb {

/// @brief ImportSource für BSON-Dateien (z. B. `mongodump`: `<collection>.bson`)
/// – ohne externe Abhängigkeiten (eigener Little-Endian-Parser).
///
/// Unterstützte BSON-Typen: double, string, embedded document, array, binary
/// (inkl. ObjectId → 12 Bytes Binary, decimal128 → 16 Bytes Binary), bool,
/// UTC datetime/timestamp/int32/int64 → Int, null/undefined/min/max key → Null,
/// regex → String "/pattern/options", code/symbol → String.
/// Unbekannte Elementtypen werfen `Error`.
///
/// - Eine Datei = eine Collection (Name = Dateiname ohne Endung).
/// - Spalten = Vereinigung der Top-Level-Keys; Typ pro Spalte aus den Werten
///   gewichtet (Int+Double → Real, sonst gemischt → Unknown).
///
/// Registriert als `bson` (`.bson`).
class BsonImportSource : public ImportSource {
public:
    explicit BsonImportSource(const std::string& path, std::string tableName = {});
    ~BsonImportSource() override;
    BsonImportSource(const BsonImportSource&) = delete;
    BsonImportSource& operator=(const BsonImportSource&) = delete;

    std::vector<TableInfo> tables() const override;
    void readRows(const std::string& table,
                  const std::function<void(const std::vector<Value>&)>& rowCb) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace memdb

#pragma once

#include "import.hpp"

#include <memory>

namespace memdb {

/// @brief ImportSource für SQL-Dumps (Text) – ohne externe Abhängigkeiten.
///
/// Unterstützt:
/// - `CREATE TABLE` (Spaltennamen/-typen, inline `PRIMARY KEY`/`NOT NULL`/`UNIQUE`,
///   table-level `PRIMARY KEY (col)`, `UNIQUE KEY/INDEX name (col)`, `KEY/INDEX name (col)`),
/// - `INSERT INTO t [(spalten)] VALUES (...), (...);` (mehrere Zeilen, Backtick-/Quote-
///   Identifier, String-Escapes (`\'`, `\"`, `\n`, `\r`, `\t`, `\0`, `\Z`, `\b`, `''`),
///   `NULL`, `TRUE`/`FALSE`, Dezimal-/Hex-Zahlen, `X'...'`-Blobs).
/// Alle anderen Statements (SET, LOCK, DROP, Kommentare ...) werden übersprungen.
/// Spaltentypen ohne CREATE TABLE werden aus den Werten inferiert
/// (alle Int → Integer, alle Double → Real, sonst Text/gemischt → Unknown).
///
/// Registriert als `sqldump` (`.sql`, `.dump`).
class SqlDumpImportSource : public ImportSource {
public:
    explicit SqlDumpImportSource(const std::string& path);
    ~SqlDumpImportSource() override;
    SqlDumpImportSource(const SqlDumpImportSource&) = delete;
    SqlDumpImportSource& operator=(const SqlDumpImportSource&) = delete;

    std::vector<TableInfo> tables() const override;
    void readRows(const std::string& table,
                  const std::function<void(const std::vector<Value>&)>& rowCb) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace memdb

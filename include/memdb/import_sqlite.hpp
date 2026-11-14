#pragma once

#include "import.hpp"

#ifdef INMEMDB_HAS_SQLITE

namespace memdb {

/// @brief ImportSource für SQLite-Dateien (FA-21; nur verfügbar mit
/// `INMEMDB_WITH_SQLITE=ON`, Makro `INMEMDB_HAS_SQLITE`).
///
/// - Tabellen aus `sqlite_master` (ohne interne `sqlite_%`) → Collections
/// - Spalten über `PRAGMA table_info` (Typ-Affinität → SourceType, NOT NULL, PK)
/// - Indizes über `PRAGMA index_list`/`index_info` (unique)
/// - Werte pro Zeile per `sqlite3_column_type` direkt in `Value`
///   (INTEGER→Int, REAL→Double, TEXT→String, BLOB→Binary, NULL→Null)
///
/// sqlite3 wird per Pimpl gekapselt – im öffentlichen Header stehen keine
/// sqlite3-Typen.
class SqliteImportSource : public ImportSource {
public:
    /// @brief Öffnet die SQLite-Datei (auch ":memory:" möglich).
    /// @throws Error bei Öffnen/Lesen-Fehlern.
    explicit SqliteImportSource(const std::string& path);
    ~SqliteImportSource() override;
    SqliteImportSource(const SqliteImportSource&) = delete;
    SqliteImportSource& operator=(const SqliteImportSource&) = delete;

    std::vector<TableInfo> tables() const override;
    void readRows(const std::string& table,
                  const std::function<void(const std::vector<Value>&)>& rowCb) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace memdb

#endif // INMEMDB_HAS_SQLITE

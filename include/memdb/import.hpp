#pragma once

#include "database.hpp"
#include "document.hpp"
#include "schema.hpp"
#include "value.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace memdb {

/// @brief Deklarierter Spaltentyp einer externen Quelle (FA-21).
enum class SourceType { Null, Integer, Real, Text, Blob, Unknown };

/// @brief Metadaten einer Spalte der Quelle.
struct ColumnInfo {
    std::string name;             ///< Spaltenname (= Feldname)
    SourceType type = SourceType::Unknown; ///< deklarierter Typ (für Schema/Indizes)
    bool notNull = false;         ///< NOT NULL in der Quelle
    bool primaryKey = false;      ///< Primärschlüssel-Spalte
};

/// @brief Index-Metadatum der Quelle.
struct IndexInfo {
    std::string column;           ///< Spalte (einfacher Sekundärindex)
    bool unique = false;
};

/// @brief Metadaten einer Quell-"Tabelle" (wird zur Collection).
struct TableInfo {
    std::string name;
    std::vector<ColumnInfo> columns;
    std::vector<IndexInfo> indexes;
};

/// @brief Abstrakte externe Quelle für den Import (FA-21).
///
/// Konkrete Ableitungen (z. B. `SqliteImportSource`) implementieren nur
/// `tables()` (Metadaten) und `readRows()` (Zeilen als bereits in `Value`
/// konvertierte Werte, Reihenfolge = `TableInfo::columns`). Der `Importer`
/// übernimmt den Rest (Collections, Bulk-Batches, Indizes, Schema, Stats).
class ImportSource {
public:
    virtual ~ImportSource() = default;

    /// @brief Alle importierbaren Tabellen mit Spalten-/Index-Metadaten.
    virtual std::vector<TableInfo> tables() const = 0;

    /// @brief Liest alle Zeilen einer Tabelle; pro Zeile wird `rowCb` mit den
    ///        konvertierten Werten aufgerufen (Reihenfolge wie in `columns`).
    virtual void readRows(const std::string& table,
                          const std::function<void(const std::vector<Value>&)>& rowCb) const = 0;
};

/// @brief Optionen für `Importer::import`.
struct ImportOptions {
    std::size_t batchSize = 10000;       ///< Batch-Größe für insertBulk
    bool createIndexes = true;           ///< Quell-Indizes nach dem Laden anlegen
    bool generateSchema = true;          ///< FA-20-Schema aus Spaltentypen ableiten
    bool keepPrimaryKeyField = true;     ///< PK zusätzlich als Feld `pkFieldName` speichern
    std::string pkFieldName = "pk";      ///< Name des PK-Feldes (leer = nicht speichern)
};

/// @brief Ergebnis/Statistik eines Imports.
struct ImportStats {
    std::map<std::string, std::size_t> rowsPerTable; ///< importierte Zeilen pro Collection
    std::size_t totalRows = 0;
    std::vector<std::string> skippedTables;  ///< bereits vorhandene Collections
    std::vector<std::string> skippedSchemas; ///< Tabellen ohne Schema (Typ-Mismatch/Unknown)
};

/// @brief Orchestriert den Import einer `ImportSource` in eine `Database` (FA-21).
class Importer {
public:
    /// @brief Importiert alle Tabellen der Quelle als Collections in `db`.
    /// @return Statistik (Zeilen pro Tabelle, Übersprungene).
    static ImportStats import(Database& db, const ImportSource& src, const ImportOptions& opts = {});
};

/// @brief Default-Mapping deklarierter Quelltypen auf `Value::Type` (Schema).
/// @return nullopt, wenn kein Mapping sinnvoll ist (z. B. Unknown).
std::optional<Value::Type> sourceTypeToValueType(SourceType t);

} // namespace memdb

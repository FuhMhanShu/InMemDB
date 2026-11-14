#pragma once

#include "import.hpp"

#include <memory>

namespace memdb {

/// @brief Modus von `JsonImportSource`.
enum class JsonMode {
    Auto,  ///< ganze Datei parsen (Array ODER einzelnes Objekt); bei Fehler zeilenweise (JSONL)
    Array, ///< Datei ist ein JSON-Array von Objekten
    Lines  ///< JSONL/NDJSON: ein JSON-Objekt pro Zeile
};

/// @brief Optionen für `JsonImportSource`.
struct JsonImportOptions {
    JsonMode mode = JsonMode::Auto;
    std::string tableName; ///< leer = Dateiname ohne Endung
};

/// @brief ImportSource für JSON-Dateien (FA-21, ohne externe Abhängigkeiten;
/// nutzt den internen JSON-Parser).
///
/// - Eine Datei = eine Collection (Name = Dateiname ohne Endung, änderbar).
/// - `Auto`: Array von Objekten oder einzelnes Objekt; schlägt das Parsen der
///   ganzen Datei fehl, wird zeilenweise gelesen (JSONL).
/// - Typen kommen 1:1 aus den JSON-Werten (null/bool/int/double/string/object/array).
/// - Spalten = Vereinigung der Top-Level-Keys aller Zeilen; Typ pro Spalte wird
///   aus den Werten gewichtet (Int+Double → Real, sonst gemischt → Unknown).
///
/// Registriert als `json` (`.json`) und `jsonl` (`.jsonl`, `.ndjson`).
class JsonImportSource : public ImportSource {
public:
    using Options = JsonImportOptions;

    explicit JsonImportSource(const std::string& path, JsonImportOptions opts = {});
    ~JsonImportSource() override;
    JsonImportSource(const JsonImportSource&) = delete;
    JsonImportSource& operator=(const JsonImportSource&) = delete;

    std::vector<TableInfo> tables() const override;
    void readRows(const std::string& table,
                  const std::function<void(const std::vector<Value>&)>& rowCb) const override;

    const JsonImportOptions& options() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace memdb

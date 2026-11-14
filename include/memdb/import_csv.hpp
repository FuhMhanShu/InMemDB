#pragma once

#include "import.hpp"

#include <memory>

namespace memdb {

/// @brief ImportSource für CSV-Dateien (FA-21) – ohne externe Abhängigkeiten.
///
/// - Eine Datei = eine Tabelle/Collection; Name default = Dateiname ohne Endung.
/// - Erste Zeile = Header (Spaltennamen), sofern `Options::hasHeader`.
/// - RFC-4180-ähnliches Quoting: `"..."` mit `""` als Escape; Trennzeichen und
///   Zeilenumbrüche innerhalb von Quotes sind erlaubt; CRLF und LF.
/// - Leere Zelle wird als `Null` importiert.
/// - Optional pro Spalte Typ-Inferenz (`Options::inferTypes`): sind alle
///   nicht-leeren Werte Integer → `Int`, sonst alle Double → `Double`, sonst `Text`.
///   Ohne Inferenz sind alle Spalten `Text`.
///
/// @brief Optionen für `CsvImportSource` (namespace-scope, damit Default-Argumente möglich).
struct CsvImportOptions {
    char delimiter = ',';      ///< Feld-Trennzeichen
    char quote = '"';          ///< Quote-Zeichen
    bool hasHeader = true;     ///< erste Zeile enthält Spaltennamen
    bool inferTypes = false;   ///< Typ pro Spalte aus den Werten ableiten (Int/Double/Text)
    std::string tableName;     ///< leer = Dateiname ohne Endung
};

/// Über `ImportRegistry` als `"csv"` (Endung `.csv`) automatisch registriert.
class CsvImportSource : public ImportSource {
public:
    using Options = CsvImportOptions;

    explicit CsvImportSource(const std::string& path, CsvImportOptions opts = {});
    ~CsvImportSource() override;
    CsvImportSource(const CsvImportSource&) = delete;
    CsvImportSource& operator=(const CsvImportSource&) = delete;

    std::vector<TableInfo> tables() const override;
    void readRows(const std::string& table,
                  const std::function<void(const std::vector<Value>&)>& rowCb) const override;

    const Options& options() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace memdb

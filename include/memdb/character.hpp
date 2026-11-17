#pragma once

#include "collection.hpp"
#include "database.hpp"
#include "discovery.hpp" // DiscoveryOptions (Sampling, maxFieldsPerCollection)
#include "value.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace memdb {

/// @brief Grobe Daten-Kategorie einer Collection (FA-29).
///
/// Vorab-Klassifikation, welche Analyse-Pipeline zur Collection passt:
/// Kategorie 1 (fast ausschließlich numerische Daten), Kategorie 2 (gemischte
/// Daten) oder Kategorie 3 (komplexe Mischformen: Arrays, verschachtelte
/// Dokumente, Binärdaten, starker Typ-Drift).
enum class DataCategory {
    Numeric, ///< Kategorie 1: fast ausschließlich numerische Felder
    Mixed,   ///< Kategorie 2: Mischung aus Zahlen, Strings, Booleans
    Complex  ///< Kategorie 3: komplexe Mischformen (Arrays/Dokumente/Binär/Drift)
};

/// @brief Kategorie-Kurzname für Reports: "numeric" / "mixed" / "complex".
std::string toString(DataCategory cat);

/// @brief Charakteristik eines einzelnen Feldes (FA-29).
///
/// Basiert auf den Nicht-Null-Werten des gesampelten Scans: Typ-Verteilung,
/// dominanter Typ, Typ-Reinheit und die grobe Einordnung `character`
/// ("numeric", "string", "boolean", "complex", "mixed", "empty").
struct FieldCharacter {
    std::string name;
    std::size_t present = 0;      ///< Dokumente mit dem Feld (inkl. Null)
    std::size_t nulls = 0;         ///< davon mit Null-Wert
    std::map<Value::Type, std::size_t> typeCounts; ///< Typ-Verteilung (nur Nicht-Null)
    Value::Type dominantType = Value::Type::Null;  ///< häufigster Nicht-Null-Typ
    double typePurity = 0.0;       ///< Anteil des dominanten Typs an allen Nicht-Null-Werten
    bool complexValues = false;    ///< Document/Array/Binary-Werte vorhanden
    std::string character;        ///< "numeric"/"string"/"boolean"/"complex"/"mixed"/"empty"
};

/// @brief Charakteristik einer Collection (FA-29) inkl. Kategorie-Urteil.
///
/// Grobe Unterscheidung nach Datenlage: `Numeric` ab 80 % numerischen Feldern,
/// `Complex` ab 25 % komplexen/gemischten Feldern oder 40 % Feldern mit Typ-Drift,
/// sonst `Mixed`. `confidence` (0..1) gibt an, wie eindeutig das Urteil ist.
struct CollectionCharacter {
    std::string collection;
    std::size_t docCount = 0;
    std::size_t sampleSize = 0;      ///< tatsächlich untersuchte Dokumente
    std::size_t fieldCount = 0;      ///< aller gesehenen Felder
    std::size_t effectiveFields = 0; ///< Felder mit mind. einem Nicht-Null-Wert
    std::size_t numericFields = 0;
    std::size_t stringFields = 0;
    std::size_t boolFields = 0;
    std::size_t complexFields = 0;   ///< dominanter Typ Document/Array/Binary
    std::size_t mixedFields = 0;     ///< kein dominanter Typ (Purity < 0.6)
    double numericFieldRatio = 0.0;  ///< numericFields / effectiveFields
    double stringFieldRatio = 0.0;   ///< stringFields / effectiveFields
    double complexFieldRatio = 0.0;  ///< (complexFields + mixedFields) / effectiveFields
    double driftFieldRatio = 0.0;    ///< Felder mit > 1 Nicht-Null-Typ / effectiveFields
    DataCategory category = DataCategory::Mixed;
    double confidence = 0.0;         ///< 0..1, wie eindeutig das Urteil ist
    std::string rationale;           ///< Begründung in Klartext (Deutsch)
    std::vector<FieldCharacter> fields;
};

/// @brief Charakteristik einer Collection bestimmen (FA-29), read-only.
///
/// Scan wie bei `profileCollection` (Sampling via `DiscoveryOptions::sampleSize` /
/// `seed`, Feld-Limit via `maxFieldsPerCollection`). Collections ohne Werte-Felder
/// werden als `Mixed` mit Confidence 0 eingestuft.
CollectionCharacter assessCharacter(const Collection& c, const DiscoveryOptions& opts = {});

/// @brief Charakteristik mehrerer Collections (auch database-übergreifend);
/// `__references` wird übersprungen.
std::vector<CollectionCharacter> assessCharacter(const std::vector<Collection>& colls,
                                                 const DiscoveryOptions& opts = {});

/// @brief Charakteristik aller Collections einer Database.
std::vector<CollectionCharacter> assessCharacter(Database& db, const DiscoveryOptions& opts = {});

/// @brief Charakteristik als JSON (maschinenlesbar / LLM-freundlich).
std::string toJson(const CollectionCharacter& cc);

/// @brief Vector-Variante als JSON-Array.
std::string toJson(const std::vector<CollectionCharacter>& ccs);

/// @brief Kategorien-Übersicht als Markdown-Tabelle.
std::string toMarkdown(const std::vector<CollectionCharacter>& ccs);

} // namespace memdb

#pragma once

#include "collection.hpp"
#include "database.hpp"
#include "value.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace memdb {

/// @brief Optionen für die komplexe Metadaten-Analyse (FA-34).
struct ComplexAnalysisOptions {
    std::size_t sampleSize = 0;           ///< 0 = alle Dokumente; sonst reproduktives `sample(seed)`
    std::uint64_t seed = 42;              ///< Seed fürs Sampling
    std::size_t maxPathsPerCollection = 128; ///< Schutz gegen sehr breite/flache Collections
    std::size_t maxDepth = 8;             ///< Verschachtelungs-Guard für die Pfad-Rekursion
    std::size_t maxSchemaVariants = 16;   ///< gemeldete Schema-Varianten (weitere werden nur gezählt)
    std::size_t maxDistinctPerPath = 4096; ///< Speicher-Guard für distinct-Werte je Pfad
    std::size_t minSightingsPerPath = 1;  ///< Pfade mit weniger Sichtungen werden übersprungen
};

/// @brief Wert-Typ eines Pfades (FA-34); Zahlen vereinen Int/Double.
enum class PathKind { Null, Boolean, Number, Text, Binary, Document, Array };

/// @brief Sichtungen eines Wert-Typs an einem Pfad (FA-34).
struct PathKindCount {
    PathKind kind = PathKind::Null;
    std::size_t count = 0;
};

/// @brief Komplexe Metadaten-Analyse eines Pfades (FA-34, Kategorie 3).
///
/// Pfade identifizieren Datenorte in Punktnotation: `"adresse.plz"` für
/// verschachtelte Dokumente, `"tags[]"` für Array-Elemente und
/// `"items[].price"` für Felder von Dokumenten innerhalb von Arrays.
/// Je Pfad werden alle gesichteten Wert-Typen gezählt (Typ-Drift),
/// Skalar-Distinct-Werte, Fehlstellen und eine deutsche `note` ermittelt.
struct ComplexPathAnalysis {
    std::string collection;
    std::string path;
    std::size_t depth = 0;             ///< 1 = Top-Level-Feld
    bool isElementPath = false;         ///< endet auf `"[]"` (Array-Elemente)
    std::vector<PathKindCount> kinds;   ///< Sichtungen je Typ, nach Typ sortiert
    PathKind dominantKind = PathKind::Null;
    std::size_t count = 0;              ///< Sichtungen gesamt
    std::size_t distinct = 0;          ///< distinct skalare Werte (gedeckelt durch maxDistinctPerPath)
    std::size_t missing = 0;           ///< untersuchte Dokumente ohne diesen Pfad
    double missingRatio = 0.0;
    double dominantShare = 1.0;        ///< Anteil des dominanten Typs an den Sichtungen
    double scalarShare = 1.0;          ///< Anteil skalarer Sichtungen (null/boolean/number/text/binary)
    bool typeDrift = false;           ///< mehr als ein Typ gesichtet
    std::string note;                  ///< Interpretations-Hinweise (Deutsch)
};

/// @brief Schema-Variante (FA-34): Gruppe untersuchter Dokumente mit identischem Pfad-Fingerprint.
struct ComplexSchemaVariant {
    std::string fingerprint;           ///< sortierte Pfade, `|`-separiert
    std::vector<std::string> fields;  ///< sortierte Pfade der Variante
    std::size_t docCount = 0;
    double share = 0.0;                ///< Anteil an den untersuchten Dokumenten
};

/// @brief Komplexe Analyse einer Collection (FA-34): Pfad-Profil und Schema-Varianten.
/// Read-only; `__references` wird ignoriert.
struct ComplexCollectionAnalysis {
    std::string collection;
    std::size_t docCount = 0;
    std::size_t sampleSize = 0;        ///< untersuchte Dokumente
    std::size_t fieldCount = 0;        ///< gesichtete Top-Level-Felder
    std::size_t pathCount = 0;         ///< gemeldete Pfade (nach Cap/minSightingsPerPath)
    std::size_t maxDepth = 0;          ///< größte gesichtete Pfadtiefe (nach Tiefen-Guard)
    std::vector<ComplexPathAnalysis> paths; ///< alphabetisch nach Pfad
    std::vector<ComplexSchemaVariant> variants; ///< nach docCount absteigend, dann Fingerprint
    std::size_t variantCount = 0;     ///< alle Varianten (auch über dem Cap liegende)
    double schemaConsistency = 0.0;   ///< Anteil der größten Variante
    std::vector<std::string> notes;   ///< Auffälligkeiten (Deutsch)
};

/// @brief Pfad-Analysen aller Pfade einer Collection.
std::vector<ComplexPathAnalysis> analyzeComplexFields(const Collection& c,
                                                      const ComplexAnalysisOptions& opts = {});

/// @brief Komplexe Analyse einer Collection: Pfad-Profil + Schema-Varianten.
ComplexCollectionAnalysis analyzeComplex(const Collection& c,
                                         const ComplexAnalysisOptions& opts = {});

/// @brief Komplexe Analyse mehrerer Collections (auch database-übergreifend).
std::vector<ComplexCollectionAnalysis> analyzeComplex(const std::vector<Collection>& colls,
                                                     const ComplexAnalysisOptions& opts = {});

/// @brief Komplexe Analyse aller Collections einer Database.
std::vector<ComplexCollectionAnalysis> analyzeComplex(Database& db,
                                                      const ComplexAnalysisOptions& opts = {});

/// @brief Klartext eines Pfad-Typs (`"number"`, `"text"`, …).
std::string pathKindName(PathKind k);

/// @brief Pfad-Analyse als JSON (maschinenlesbar / LLM-freundlich).
std::string toJson(const ComplexPathAnalysis& a);

/// @brief Collection-Analyse als JSON.
std::string toJson(const ComplexCollectionAnalysis& a);

/// @brief Collection-Analyse als Markdown (Tabellen).
std::string toMarkdown(const ComplexCollectionAnalysis& a);

} // namespace memdb

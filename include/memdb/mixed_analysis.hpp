#pragma once

#include "collection.hpp"
#include "database.hpp"
#include "value.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace memdb {

/// @brief Optionen für die gemischte Metadaten-Analyse (FA-33).
struct MixedAnalysisOptions {
    std::size_t sampleSize = 0;           ///< 0 = alle Dokumente; sonst reproduktives `sample(seed)`
    std::uint64_t seed = 42;              ///< Seed fürs Sampling
    std::size_t maxFieldsPerCollection = 64; ///< Schutz gegen sehr breite Collections
    std::size_t minValuesPerField = 5;    ///< Felder mit weniger Werten des dominanten Typs werden übersprungen

    std::size_t maxCategoryDistinct = 12; ///< Text-Feld gilt als kategorisch, wenn distinct <= X
    double identifierUniqueness = 0.99;   ///< Einzigartigkeits-Schwelle für Kennungsfelder
    std::size_t freeTextMinLength = 20;   ///< mittlere String-Länge, ab der Freitext vermutet wird
    double discreteMaxRange = 100.0;      ///< diskret, wenn alle Werte ganzzahlig UND Spannweite <= X

    bool withAssociations = true;         ///< paarweise Assoziationen (Pearson/eta²/Cramérs V)
    std::size_t minPairsForAssociation = 3; ///< Mindest-Gemeinsamkeit je Feldpaar
    std::size_t maxDistinctForAssociation = 32; ///< Kardinalitäts-Guard für eta²/Cramérs V
};

/// @brief Dominanter Wert-Typ eines Feldes in einer Mixed-Collection (FA-33).
enum class MixedKind { Number, Text, Boolean };

/// @brief Semantische Klasse eines Text-Feldes (FA-33).
enum class TextClass { None, Categorical, Identifier, FreeText };

/// @brief Gemischte Metadaten-Analyse eines Feldes (FA-33, Kategorie 2).
///
/// Interpretiert ein Feld gemäß seinem dominanten Typ: Basis-Kennzahlen und
/// Diskretheit für Zahlen, Längen-/Kardinalitäts-Statistik und semantische
/// Klasse (kategorisch, Kennung, Freitext) plus Format-Hinweise (Datum-, Hex-
/// und zahlartige Strings) für Texte, sowie `trueShare` für Booleans.
/// `note` fasst Auffälligkeiten auf Deutsch zusammen.
struct MixedFieldAnalysis {
    std::string collection;
    std::string field;
    MixedKind kind = MixedKind::Text;
    std::size_t count = 0;           ///< Werte des dominanten Typs (ohne null)
    std::size_t distinct = 0;        ///< distinct Werte des dominanten Typs
    std::size_t missing = 0;         ///< Dokumente ohne analysierbaren Wert (abwesend, null oder anderer Typ)
    double missingRatio = 0.0;       ///< missing / docCount
    double dominantShare = 1.0;      ///< Anteil des dominanten Typs an den analysierbaren Werten

    // Zahlenteil (kind == Number)
    double min = 0.0, max = 0.0;
    double mean = 0.0, stddev = 0.0, median = 0.0;
    double integralRatio = 0.0;      ///< Anteil ganzzahliger Werte
    bool discrete = false;          ///< alle ganzzahlig, Spannweite <= discreteMaxRange

    // Textteil (kind == Text)
    std::size_t lenMin = 0, lenMax = 0;
    double lenMean = 0.0;
    double uniqueness = 0.0;        ///< distinct / count
    double emptyShare = 0.0;        ///< Anteil leerer Strings
    TextClass textClass = TextClass::None;
    bool dateLike = false;          ///< alle Werte ISO-Datum-artig (JJJJ-MM-TT)
    bool hexLike = false;           ///< alle Werte Hex-Strings (Ziffern 0-9/a-f/A-F)
    bool numericLike = false;       ///< alle Werte Zahl-Strings (optionales Vorzeichen, Ziffern, Punkt)

    // Boolean-Teil (kind == Boolean)
    double trueShare = 0.0;         ///< Anteil true

    std::string note;                ///< Interpretations-Hinweise (Deutsch)
};

/// @brief Paarweise Assoziation zweier Felder (FA-33).
///
/// Maß je nach Typ-Kombination: `pearson` (Zahl/Zahl, Zahl/Boolean, Boolean/Boolean),
/// `etaSquared` (kategorischer Text gegen Zahl/Boolean), `cramersV` (zwei kategorische
/// Text-Felder). Alle Werte sind auf [0, 1] bzw. Pearson auf [-1, 1] normiert.
struct MixedPairAssociation {
    std::string fieldX, fieldY;
    std::string measure;             ///< "pearson" | "etaSquared" | "cramersV"
    double value = 0.0;
    std::size_t pairs = 0;           ///< gemeinsame Dokumente
};

/// @brief Gemischte Analyse einer Collection (FA-33): Feld-Analysen +
/// paarweise Assoziationen. Read-only; `__references` wird ignoriert.
struct MixedCollectionAnalysis {
    std::string collection;
    std::size_t docCount = 0;
    std::size_t sampleSize = 0;
    std::size_t fieldCount = 0;        ///< aller gesehenen Felder
    std::size_t mixedFieldCount = 0;  ///< analysierter Felder (nach Cap/minValuesPerField)
    std::vector<MixedFieldAnalysis> fields;
    std::vector<MixedPairAssociation> associations;
};

/// @brief Gemischte Analyse aller Felder einer Collection (nur die Feld-Analysen).
std::vector<MixedFieldAnalysis> analyzeMixedFields(const Collection& c,
                                                    const MixedAnalysisOptions& opts = {});

/// @brief Gemischte Analyse einer Collection: Feld-Analysen + Assoziationen.
MixedCollectionAnalysis analyzeMixed(const Collection& c,
                                     const MixedAnalysisOptions& opts = {});

/// @brief Gemischte Analyse mehrerer Collections (auch database-übergreifend).
std::vector<MixedCollectionAnalysis> analyzeMixed(const std::vector<Collection>& colls,
                                                 const MixedAnalysisOptions& opts = {});

/// @brief Gemischte Analyse aller Collections einer Database.
std::vector<MixedCollectionAnalysis> analyzeMixed(Database& db,
                                                  const MixedAnalysisOptions& opts = {});

/// @brief Feld-Analyse als JSON (maschinenlesbar / LLM-freundlich).
std::string toJson(const MixedFieldAnalysis& a);

/// @brief Collection-Analyse als JSON.
std::string toJson(const MixedCollectionAnalysis& a);

/// @brief Collection-Analyse als Markdown (Tabellen).
std::string toMarkdown(const MixedCollectionAnalysis& a);

} // namespace memdb

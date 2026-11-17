#pragma once

#include "collection.hpp"
#include "database.hpp"
#include "value.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace memdb {

/// @brief Optionen für die numerische Metadaten-Analyse (FA-30).
struct NumericAnalysisOptions {
    std::size_t sampleSize = 0;          ///< 0 = alle Dokumente; sonst reproduktives `sample(seed)`
    std::uint64_t seed = 42;             ///< Seed fürs Sampling
    std::size_t maxFieldsPerCollection = 64; ///< Schutz gegen sehr breite Collections
    std::size_t minValuesPerField = 5;   ///< Felder mit weniger numerischen Werten werden übersprungen

    std::size_t histogramBins = 10;     ///< 0 = kein Histogramm
    double discreteMaxRange = 100.0;    ///< alle ganzzahlig UND Spannweite <= X => diskret
    double gridMinRatio = 0.95;          ///< Anteil der Werte exakt auf dem Raster
    std::size_t benfordMinSamples = 30;  ///< Mindestwerte für die Benford-Analyse
    double benfordMadCritical = 0.015;   ///< MAD-Schwelle erste Ziffer (Nigrini)
    bool withCorrelations = true;        ///< Pearson pro numerischem Feldpaar
};

/// @brief Numerische Metadaten-Analyse eines Feldes (FA-30, Kategorie 1).
///
/// Interpretiert ein numerisches Feld statistisch: Lage- und Streuungsmaße,
/// Quantile, Schiefe/Kurtosis (Exzess), Ganzzahl-Anteil, Diskretheit,
/// Rundungs-Raster (z. B. Preisraster), Sequenz-Charakter (monoton, lückenlos,
/// Schrittweite – ID-artige Felder), Benford-Konformität der ersten Ziffer
/// und ein equidistantes Histogramm. `note` fasst Auffälligkeiten auf Deutsch zusammen.
struct NumericFieldAnalysis {
    std::string collection;
    std::string field;
    std::size_t count = 0;          ///< untersuchte numerische Werte
    std::size_t distinct = 0;       ///< distinct Werte
    double min = 0.0, max = 0.0;
    double mean = 0.0, stddev = 0.0;
    double q1 = 0.0, median = 0.0, q3 = 0.0, iqr = 0.0;
    double skewness = 0.0;          ///< Momente-Schätzer; 0 bei n < 3 oder stddev == 0
    double kurtosis = 0.0;          ///< Exzess (Normalverteilung: 0)
    double integralRatio = 0.0;     ///< Anteil ganzzahliger Werte
    bool discrete = false;          ///< alle ganzzahlig, Spannweite <= discreteMaxRange

    bool hasGrid = false;           ///< Rundungs-Raster erkannt
    double grid = 0.0;              ///< Rasterweite (z. B. 0.01, 5, 100)
    double gridRatio = 0.0;         ///< Anteil der Werte exakt auf dem Raster

    bool sequenceAnalyzed = false;  ///< nur im Vollscan (Sequenz braucht Dokumentreihenfolge)
    bool monotonic = false;         ///< streng monoton steigend (in Dokumentreihenfolge)
    bool consecutive = false;       ///< Schritt 1 ohne Lücken (surrogat-ID-artig)
    double stepMedian = 0.0;        ///< mediane Schrittweite (nur bei monotonic)
    std::size_t gaps = 0;           ///< Schritte > stepMedian (nur bei monotonic)

    bool benfordApplicable = false; ///< genug Werte mit erster signifikanter Ziffer
    bool benfordSuspicious = false; ///< MAD > benfordMadCritical
    double benfordMad = 0.0;        ///< mittlere absolute Abweichung zur Benford-Verteilung
    std::vector<double> firstDigitFreq; ///< 9 Einträge (Ziffern 1..9), normiert

    std::vector<std::size_t> histogram; ///< histogramBins Einträge (äquidistant)
    double binWidth = 0.0, binMin = 0.0;

    std::string note;               ///< Interpretations-Hinweise (Deutsch)
};

/// @brief Pearson-Korrelation eines numerischen Feldpaars (FA-30).
struct NumericPairCorrelation {
    std::string fieldX, fieldY;
    double pearson = 0.0;
    std::size_t pairs = 0;          ///< Dokumente mit beiden Feldern
};

/// @brief Numerische Analyse einer Collection (FA-30): Feld-Analysen +
/// paarweise Pearson-Korrelationen. Read-only; `__references` wird ignoriert.
struct NumericCollectionAnalysis {
    std::string collection;
    std::size_t docCount = 0;
    std::size_t sampleSize = 0;
    std::size_t fieldCount = 0;        ///< aller gesehenen Felder
    std::size_t numericFieldCount = 0; ///< Felder mit numerischen Werten (nach Cap)
    double numericFieldRatio = 0.0;    ///< numerische / aller Felder
    std::vector<NumericFieldAnalysis> fields;
    std::vector<NumericPairCorrelation> correlations;
};

/// @brief Numerische Analyse aller Felder einer Collection (nur die Feld-Analysen).
std::vector<NumericFieldAnalysis> analyzeNumericFields(const Collection& c,
                                                       const NumericAnalysisOptions& opts = {});

/// @brief Numerische Analyse einer Collection: Feld-Analysen + Korrelationen.
NumericCollectionAnalysis analyzeNumeric(const Collection& c,
                                         const NumericAnalysisOptions& opts = {});

/// @brief Numerische Analyse mehrerer Collections (auch database-übergreifend).
std::vector<NumericCollectionAnalysis> analyzeNumeric(const std::vector<Collection>& colls,
                                                      const NumericAnalysisOptions& opts = {});

/// @brief Numerische Analyse aller Collections einer Database.
std::vector<NumericCollectionAnalysis> analyzeNumeric(Database& db,
                                                      const NumericAnalysisOptions& opts = {});

/// @brief Feld-Analyse als JSON (maschinenlesbar / LLM-freundlich).
std::string toJson(const NumericFieldAnalysis& a);

/// @brief Collection-Analyse als JSON.
std::string toJson(const NumericCollectionAnalysis& a);

/// @brief Collection-Analyse als Markdown (Tabellen).
std::string toMarkdown(const NumericCollectionAnalysis& a);

} // namespace memdb

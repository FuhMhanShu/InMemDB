#pragma once

#include "collection.hpp"
#include "database.hpp"
#include "numeric_analysis.hpp" // NumericAnalysisOptions (Basis-Scan, Sampling)
#include "value.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace memdb {

/// @brief Optionen für die Einfluss-Analyse (FA-31).
///
/// Die Einfluss-Analyse baut auf dem numerischen Basis-Scan (FA-30) auf und
/// deutet die Werte weiter: Welche Felder dominieren andere? Schwellwerte
/// steuern, ab welchem Zusammenhang ein Feld als Treiber bzw. abhängig gilt.
struct InfluenceOptions {
    NumericAnalysisOptions numeric;      ///< Optionen des numerischen Basis-Scans

    double strongCorrelation = 0.8;      ///< |r| >= X gilt als starker Zusammenhang
    double dependencyRatio = 0.99;       ///< Anteil konsistenter Gruppen für "funktional abhängig" (1.0 = streng)
    std::size_t minDependencyPairs = 5;  ///< Mindest-Dokumente mit beiden Feldern je Abhängigkeitsprüfung
    double rSquaredHigh = 0.9;           ///< R^2 >= X => Feld gilt als von den übrigen erklärt
    std::size_t maxPredictors = 16;      ///< Obergrenze der Prädiktoren je Regression (R^2)
    std::size_t minRegressionRows = 8;   ///< Mindest-Zeilen (Complete Cases) für die R^2-Regression
};

/// @brief Funktionale Abhängigkeit: `source` bestimmt `target` (FA-31).
///
/// Gleiche Werte in `source` führen stets zum selben Wert in `target`
/// (z. B. target = f(source)): der stärkste strukturelle Hinweis auf
/// Ableitung. Evidenz sind nur Gruppen mit wiederholtem source-Wert;
/// `coverage` ist der Anteil dieser Gruppen mit eindeutigem target-Wert,
/// 1.0 bedeutet exakt funktional.
struct FunctionalDependency {
    std::string source;
    std::string target;
    double coverage = 0.0;    ///< Anteil der Evidenz-Gruppen mit eindeutigem target-Wert
    std::size_t groups = 0;   ///< wiederholte source-Werte (Evidenz-Gruppen)
    std::size_t pairs = 0;    ///< Dokumente mit beiden Feldern
};

/// @brief Rolle eines Feldes im Gefüge der Collection (FA-31).
enum class FieldRole {
    Driver,      ///< bestimmt andere Felder, ohne selbst vollständig erklärt zu sein
    Dependent,   ///< wird von anderen Feldern erklärt (R^2 bzw. funktional)
    Mediator,    ///< beides: abhängig und treibt selbst weitere Felder
    Independent  ///< keine starken Zusammenhänge
};

/// @brief Rollen-Kurzname: "driver" / "dependent" / "mediator" / "independent".
std::string toString(FieldRole role);

/// @brief Einfluss-Profil eines numerischen Feldes (FA-31).
///
/// Bündelt die Deutung der statistischen Werte: Hub über die Pearson-
/// Korrelationen (Summe der Beträge), Erklärtheit durch die übrigen Felder
/// (multiples R^2), funktionale Bestimmungen und daraus ein kombinierter
/// `influenceScore` (0..1) samt Rolle und Begründung.
struct FieldInfluence {
    std::string field;
    std::size_t partners = 0;            ///< Feldpaare mit genug gemeinsamen Werten
    std::size_t strongCorrelations = 0;  ///< davon mit |r| >= strongCorrelation
    double hubScore = 0.0;               ///< Summe der |r| über alle Partner
    double hubRatio = 0.0;               ///< hubScore / partners (0..1)
    double maxAbsPearson = 0.0;          ///< stärkster Korrelationsbetrag
    std::string strongestPartner;        ///< Feld mit dem stärksten Zusammenhang

    bool rSquaredComputed = false;       ///< R^2 berechenbar (genug Complete Cases, Varianz > 0)
    double rSquared = 0.0;               ///< Erklärtheit durch die übrigen Felder (multiples R^2)
    std::size_t regressionRows = 0;     ///< Zeilen (Complete Cases) der Regression

    std::size_t determines = 0;         ///< Anzahl Felder, die funktional von diesem bestimmt werden
    std::size_t determinedBy = 0;       ///< Anzahl Felder, die dieses Feld funktional bestimmen

    double influenceScore = 0.0;        ///< kombinierter Score (0..1)
    FieldRole role = FieldRole::Independent;
    std::string rationale;              ///< Begründung in Klartext (Deutsch)
};

/// @brief Einfluss-Analyse einer Collection (FA-31), read-only.
///
/// Reihenfolge der Felder: nach `influenceScore` absteigend. `topDriver` ist
/// das Feld mit dem höchsten Score. `__references` wird ignoriert.
struct InfluenceAnalysis {
    std::string collection;
    std::size_t docCount = 0;
    std::size_t sampleSize = 0;
    std::size_t numericFieldCount = 0;
    std::vector<FieldInfluence> fields;
    std::vector<FunctionalDependency> dependencies;
    std::string topDriver;               ///< Feld mit dem höchsten Einfluss-Score
    std::string note;                    ///< Kurzfassung der Deutung (Deutsch)
};

/// @brief Einfluss-Analyse einer Collection bestimmen (FA-31).
InfluenceAnalysis analyzeInfluence(const Collection& c, const InfluenceOptions& opts = {});

/// @brief Einfluss-Analyse mehrerer Collections (auch database-übergreifend).
std::vector<InfluenceAnalysis> analyzeInfluence(const std::vector<Collection>& colls,
                                                const InfluenceOptions& opts = {});

/// @brief Einfluss-Analyse aller Collections einer Database.
std::vector<InfluenceAnalysis> analyzeInfluence(Database& db, const InfluenceOptions& opts = {});

/// @brief Einfluss-Analyse als JSON (maschinenlesbar / LLM-freundlich).
std::string toJson(const InfluenceAnalysis& a);

/// @brief Vector-Variante als JSON-Array.
std::string toJson(const std::vector<InfluenceAnalysis>& as);

/// @brief Einfluss-Analyse als Markdown (Tabellen).
std::string toMarkdown(const InfluenceAnalysis& a);

/// @brief Kategorien-Übersicht mehrerer Collections als Markdown-Tabelle.
std::string toMarkdown(const std::vector<InfluenceAnalysis>& as);

} // namespace memdb

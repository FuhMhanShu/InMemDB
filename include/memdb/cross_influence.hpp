#pragma once

#include "database.hpp"
#include "discovery.hpp"   // discoverForeignKeys (Discovery-FKs als Join-Quelle)
#include "influence.hpp"   // FieldInfluence, FieldRole, InfluenceOptions (FA-31-Maschinerie)
#include "value.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace memdb {

/// @brief Optionen für die Cross-Collection-Einfluss-Analyse (FA-32).
///
/// Die Analyse verknüpft zwei Collections über deklarierte FA-18-Referenzen
/// (`__references`) und/oder entdeckte Fremdschlüssel (Discovery, FA-27) und
/// wendet auf der verknüpften Zeilenmenge die FA-31-Maschinerie an.
/// Sampling wird nicht angewendet (die Zeilenzahl ist über `minJoinPairs`
/// und die Feld-Kaps begrenzt); `influence.numeric` steuert Feld-Limit und
/// Wert-Mindestzahl, `influence` die Schwellwerte der Einfluss-Analyse.
struct CrossInfluenceOptions {
    InfluenceOptions influence;          ///< FA-31-Schwellwerte + numerischer Basis-Scan

    bool includeDeclared = true;         ///< deklarierte Referenzen (`__references`) auswerten
    bool includeDiscovered = true;       ///< entdeckte Fremdschlüssel (FA-27) auswerten
    double minFkConfidence = 0.9;        ///< Mindest-Confidence für Discovery-FKs
    std::size_t minJoinPairs = 10;        ///< Mindest-Zeilen je Paar-Analyse
    double minJoinCoverage = 0.0;        ///< Mindest-Anteil verknuepfter A-Dokumente (0 = alle)
    bool suppressTrivialJoinDependency = true; ///< Join-Feld-Paar aus Korrelation/FD/R2 ausblenden
};

/// @brief Cross-Collection-Abhängigkeit (FA-32): `source` bestimmt `target`.
///
/// `source`/`target` sind collection-qualifiziert ("collection.field").
/// `coverage` ist der Anteil der Evidenz-Gruppen (wiederholte Quell-Werte)
/// mit eindeutigem Zielwert; 1.0 bedeutet exakt funktional.
struct CrossDependency {
    std::string source;   ///< "collectionA.feld"
    std::string target;   ///< "collectionB.feld"
    double coverage = 0.0;
    std::size_t groups = 0;
    std::size_t pairs = 0;
};

/// @brief Einfluss-Analyse eines Collection-Paares über eine Verknüpfung (FA-32).
///
/// `fields` enthält collection-qualifizierte `FieldInfluence`-Profile
/// (field = "collection.feld", nach influenceScore absteigend) auf der
/// verknüpften Zeilenmenge; `dependencies` nur Abhängigkeiten mit
/// sourceCollection != targetCollection.
struct CrossPairAnalysis {
    std::string origin;          ///< "declared" (FA-18-Referenz) oder "discovered" (FA-27-FK)
    std::string relation;        ///< Beziehungsname (declared) bzw. Join-Beschreibung (discovered)
    std::string collectionA;     ///< Quell-Collection (A-Sicht)
    std::string joinFieldA;      ///< Join-Feld in A ("" bei deklarierter Kanten-Referenz)
    std::string collectionB;     ///< Ziel-Collection
    std::string keyFieldB;       ///< Zielfeld in B ("" bei deklarierter Kanten-Referenz)
    std::size_t docsA = 0;       ///< Dokumente in A (Gesamtbestand)
    std::size_t matchedA = 0;    ///< A-Dokumente mit mind. einem Partner
    std::size_t rows = 0;       ///< verknüpfte Zeilen (A x B, bei 1:n aufgebläht)
    double joinCoverage = 0.0;   ///< matchedA / docsA
    std::string cardinality;     ///< aus A-Sicht: "n:1" (Standard) / "1:n" (A-Doc mit mehreren Partnern)
    std::vector<FieldInfluence> fields;
    std::vector<CrossDependency> dependencies;
    std::string topDriver;       ///< "collection.feld" mit dem höchsten Score (Dominanz-Signal vorausgesetzt)
    std::string note;            ///< Kurzfassung (Deutsch)
};

/// @brief Ergebnis der Cross-Collection-Einfluss-Analyse (FA-32): alle
/// ausgewerteten Collection-Paare.
struct CrossInfluenceResult {
    std::vector<CrossPairAnalysis> pairs;
    std::string note;            ///< Zusammenfassung (Deutsch)
};

/// @brief Cross-Collection-Einfluss-Analyse über eine Database (FA-32), read-only.
///
/// Verknüpfungen: deklarierte FA-18-Referenzen (Dokument-Kanten aus
/// `__references`, sofern vorhanden) und entdeckte Fremdschlüssel
/// (`discoverForeignKeys`, Confidence >= minFkConfidence; Collection-Paare
/// mit derselben Collection auf beiden Seiten werden übersprungen — das ist
/// FA-31-Territorium). `__references` selbst wird nie als Daten-Collection
/// analysiert. Paare unter `minJoinPairs` Zeilen bzw. `minJoinCoverage` werden
/// übersprungen und in `note` genannt.
CrossInfluenceResult analyzeCrossInfluence(Database& db, const CrossInfluenceOptions& opts = {});

/// @brief Paar-Analyse als JSON (maschinenlesbar / LLM-freundlich).
std::string toJson(const CrossPairAnalysis& a);

/// @brief Gesamtergebnis als JSON-Array mit Zusammenfassung.
std::string toJson(const CrossInfluenceResult& r);

/// @brief Paar-Analyse als Markdown (Tabellen).
std::string toMarkdown(const CrossPairAnalysis& a);

/// @brief Gesamtergebnis als Markdown (Übersicht je Paar).
std::string toMarkdown(const CrossInfluenceResult& r);

} // namespace memdb

#pragma once

#include "collection.hpp"
#include "database.hpp"
#include "value.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace memdb {

class ReferenceIndex;

/// @brief Art einer entdeckten Beziehung (Discovery, v0.29).
enum class RelationKind {
    ForeignKey,            ///< A.feld verweist (per Werte-Überlappung) auf B Zielfeld
    FunctionalDependency,  ///< innerhalb einer Collection: X bestimmt Y eindeutig (X → Y)
    Correlation,           ///< numerisch (Pearson |r|) oder kategorial (Assoziation) korreliert
    NearDuplicate          ///< zwei String-Werte (ggf. über Collections hinweg) sind sich sehr ähnlich
};

/// @brief Eine entdeckte Beziehung/Abhängigkeit (Discovery, v0.29).
struct DiscoveredRelation {
    RelationKind kind = RelationKind::ForeignKey;
    std::string fromCollection; ///< Quell-Collection (bei FD/Korrelation: die eine Collection)
    std::string field;          ///< Quell-Feld (X)
    std::string toCollection;   ///< Ziel-Collection (bei FD/Korrelation = fromCollection)
    std::string toField;        ///< Zielfeld (Y)
    double support = 0.0;       ///< Anteil der betrachteten Dokumente, die zum Befund beitragen
    double confidence = 0.0;    ///< FK: Overlap-Ratio | FD: Anteil eindeutiger Gruppen |
                                ///< Korrelation: |r| bzw. Assoziation | Fuzzy: Ähnlichkeit
    std::size_t sampleSize = 0; ///< Anzahl untersuchter Dokumente/Werte
    std::string note;           ///< Zusatzinfo (z. B. "name-heuristik:_id", Wertepaar bei Fuzzy)
    std::string cardinality;    ///< v0.30, nur FK: "1:1" / "n:1" / "1:n" / "n:m" (aus A-Sicht)
};

/// @brief Optionen für die Discovery (v0.29) – alles ohne externe Abhängigkeiten.
struct DiscoveryOptions {
    double minSupport = 0.8;          ///< Mindest-Anteil der Quell-Dokumente mit Befund
    double minConfidence = 0.0;       ///< Mindest-Confidence für FK/FD-Kandidaten
    double minCorrelation = 0.7;      ///< |r| bzw. Assoziation ab der Korrelation gemeldet wird
    double minSimilarity = 0.85;      ///< normalisierte Ähnlichkeit für Near-Duplicates
    std::size_t sampleSize = 0;       ///< 0 = alle Dokumente; sonst reproduktives `sample(seed)`
    std::uint64_t seed = 42;          ///< Seed fürs Sampling
    std::size_t maxFieldsPerCollection = 64; ///< Schutz gegen sehr breite Collections
    std::size_t maxValuesPerField = 2000;    ///< Schutz für Fuzzy-Matching (Werte pro Feld)
    double minSchemaSimilarity = 0.6;         ///< v0.32: Mindest-Jaccard für Merge-Vorschläge

    double outlierZ = 3.0;              ///< v0.40: |z| > outlierZ gilt als Ausreißer (z-Score)
    double outlierIqrK = 1.5;           ///< v0.40: Tukey-Fence Q1-k*IQR / Q3+k*IQR
    std::size_t minOutlierSamples = 8;  ///< v0.40: Felder mit weniger numerischen Werten werden uebersprungen
};

/// @brief Fremdschlüssel-Kandidaten per Werte-Überlappung – **collection- und
/// database-übergreifend** (v0.29). Nimmt beliebige Collection-Handles; Collections
/// werden am Namen gruppiert, auch aus verschiedenen `Database`-Objekten.
/// Liefert Relationen `A.field → B.toField`, wenn die Werte von `A.field` zu
/// `confidence` in den Werten von `B.toField` vorkommen.
std::vector<DiscoveredRelation> discoverForeignKeys(const std::vector<Collection>& colls,
                                                    const DiscoveryOptions& opts = {});

/// @brief Funktionale Abhängigkeiten innerhalb einer Collection (v0.29):
/// `X → Y`, wenn jede X-Gruppe höchstens einen Y-Wert hat (via `groupBy` + `Distinct`).
std::vector<DiscoveredRelation> discoverFunctionalDependencies(const Collection& c,
                                                               const DiscoveryOptions& opts = {});

/// @brief Korrelationen zwischen Feldern einer Collection (v0.29):
/// numerische Paare via Pearson-|r|, kategoriale (String) Paare via
/// Assoziation (Uncertainty-Coefficient-artig).
std::vector<DiscoveredRelation> discoverCorrelations(const Collection& c,
                                                     const DiscoveryOptions& opts = {});

/// @brief Near-Duplicates: ähnliche String-Werte – auch über Collections/Databases
/// hinweg (v0.29, normalisierte Edit-Distanz auf gesampleten Werten).
std::vector<DiscoveredRelation> discoverNearDuplicates(const std::vector<Collection>& colls,
                                                       const DiscoveryOptions& opts = {});

/// @brief Kombiniert FK-Discovery (alle Collections) + FD + Korrelation + Fuzzy
/// pro Collection. `discoverRelations(db, opts)` = alle Collections einer Database;
/// die `vector<Collection>`-Variante arbeitet database-übergreifend.
std::vector<DiscoveredRelation> discoverRelations(Database& db, const DiscoveryOptions& opts = {});
std::vector<DiscoveredRelation> discoverRelations(const std::vector<Collection>& colls,
                                                  const DiscoveryOptions& opts = {});

/// @brief Wendet FK-Relationen mit `confidence ≥ minConfidence` als FA-18-Referenzen
/// an (v0.29). Funktioniert nur innerhalb einer Database – bei database-übergreifenden
/// Befunden werden Collections, die in `db` fehlen, übersprungen.
/// @return Anzahl angelegter Referenzen.
std::size_t applyDiscovered(Database& db, const std::vector<DiscoveredRelation>& rels,
                            ReferenceIndex& refs, double minConfidence = 0.9);

/// @brief Orphan-Report (v0.30): Quell-Werte eines Feldes, die kein Gegenstück im
/// Zielfeld haben (Integritätsprüfung – Gegenstück zu `skippedForeignKeys` beim Import).
struct OrphanReport {
    std::string fromCollection, field, toCollection, toField;
    std::size_t scanned = 0;        ///< Quell-Dokumente mit Feldwert
    std::size_t orphans = 0;        ///< distinct Werte ohne Gegenstück
    std::vector<Value> orphanValues; ///< die Orphan-Werte ( gedeckelt auf 100)
};

/// @brief Orphan-Werte eines einzelnen Feldes gegen ein Zielfeld.
OrphanReport findOrphans(const Collection& source, const std::string& field,
                         const Collection& target, const std::string& targetField,
                         const DiscoveryOptions& opts = {});
/// @brief Orphan-Report für alle FK-Befunde in `rels` (die auf die gegebenen Collections passen).
std::vector<OrphanReport> findOrphansForRelations(const std::vector<Collection>& colls,
                                                  const std::vector<DiscoveredRelation>& rels,
                                                  const DiscoveryOptions& opts = {});

/// @brief Pfad-Suche im Beziehungsgraph (v0.30): alle FK-Pfade von Collection `from`
/// nach Collection `to` (DFS, maxDepth begrenzt). Jeder Pfad ist eine Kette von
/// FK-Relationen – direkt als Multi-Hop-Joins nutzbar.
std::vector<std::vector<DiscoveredRelation>> findPaths(const std::vector<DiscoveredRelation>& rels,
                                                       const std::string& from, const std::string& to,
                                                       std::size_t maxDepth = 4);

/// @brief Datenprofil eines Feldes (v0.31).
struct FieldProfile {
    std::string name;
    std::map<Value::Type, std::size_t> typeCounts; ///< Typ-Verteilung (Enum als int-Schlüssel)
    std::size_t present = 0;      ///< Dokumente mit Feld
    std::size_t nulls = 0;        ///< davon Null-Werte
    std::size_t distinct = 0;     ///< distinct Werte
    std::optional<Value> min, max; ///< geordnetes Min/Max (Zahlen/Strings), sonst nullopt
    std::vector<Value> examples;   ///< bis zu 3 Beispielwerte
};

/// @brief Datenprofil einer Collection (v0.31): docCount + pro Feld Typ-Verteilung,
/// Null-Rate, distinct, Min/Max, Beispiele. Read-only; mit `sampleSize` gesamplelt.
struct CollectionProfile {
    std::string collection;
    std::size_t docCount = 0;
    std::size_t sampleSize = 0;
    std::vector<FieldProfile> fields;
};

/// @brief Profil einer Collection (v0.31).
CollectionProfile profileCollection(const Collection& c, const DiscoveryOptions& opts = {});
/// @brief Profil als JSON (v0.31).
std::string toJson(const CollectionProfile& p);

/// @brief Candidate-Key-Discovery (v0.31): minimale Feldmengen, die Dokumente eindeutig
/// identifizieren (X → alle Felder; einzelne Felder zuerst, dann Paare – ein Paar nur,
/// wenn keiner der beiden Einzelfelder schon Key ist). Ergebnis aufsteigend nach Größe.
std::vector<std::vector<std::string>> discoverCandidateKeys(const Collection& c,
                                                            const DiscoveryOptions& opts = {});

/// @brief Diff zweier Collections (v0.32): Feldmengen, Jaccard-Ähnlichkeit, Row-Counts,
/// Werte-Überlappung pro gemeinsamem Feld.
struct CollectionDiff {
    std::string name;
    bool onlyInA = false, onlyInB = false;
    std::size_t docCountA = 0, docCountB = 0;
    std::vector<std::string> fieldsOnlyInA, fieldsOnlyInB, commonFields;
    double fieldSimilarity = 0.0;              ///< Jaccard über Feldnamen
    std::map<std::string, double> valueOverlap; ///< pro gemeinsamem Feld: |A ∩ B| / |A|
};

/// @brief Diff zweier Databases (v0.32): gleiche Collection-Namen werden paarweise
/// verglichen, Namen nur in A/B separat ausgewiesen (`__references` wird ignoriert).
struct DatabaseDiff {
    std::vector<std::string> collectionsOnlyInA, collectionsOnlyInB;
    std::vector<CollectionDiff> common;
};

CollectionDiff diffCollections(const Collection& a, const Collection& b, const DiscoveryOptions& opts = {});
DatabaseDiff diffDatabases(const Database& a, const Database& b, const DiscoveryOptions& opts = {});
std::string toJson(const DatabaseDiff& d);

/// @brief Merge-Vorschlag (v0.32): zwei Collections mit hoher Schema-Ähnlichkeit
/// (Jaccard über Feldnamen) und Werte-Überlappung – direkter Kandidat für FA-24
/// `Database::mergeCollections`.
struct MergeSuggestion {
    std::string collectionA, collectionB;
    double schemaSimilarity = 0.0; ///< Jaccard der Feldnamen
    double valueOverlap = 0.0;     ///< beste Werte-Überlappung eines gemeinsamen Feldes
    std::string commonField;       ///< das Feld mit der besten Überlappung
    std::string note;
};

/// @brief Paarweise Merge-Vorschläge über beliebige Collections (auch cross-database).
std::vector<MergeSuggestion> suggestMerges(const std::vector<Collection>& colls,
                                            const DiscoveryOptions& opts = {});

/// @brief Ausreißer-Befund pro numerischem Feld (v0.40): z-Score (|z| > outlierZ) und/oder
/// IQR/Tukey-Fence (außerhalb Q1-k*IQR .. Q3+k*IQR). Nur numerische Felder mit mind.
/// `minOutlierSamples` Werten. Read-only.
struct FieldOutliers {
    std::string collection, field;
    std::size_t count = 0;                    ///< untersuchte numerische Werte
    double mean = 0.0, stddev = 0.0;          ///< fuer z-Score
    double q1 = 0.0, median = 0.0, q3 = 0.0, iqr = 0.0; ///< IQR (Quantile per linearer Interpolation)
    std::size_t outliers = 0;
    double outlierRate = 0.0;                 ///< outliers / count
    std::string method;                       ///< "z-score" / "IQR" / "z-score+IQR"
    std::vector<Value> values;                ///< die Ausreißer-Werte (gedeckelt auf 100)
};

/// @brief Ausreißer-Erkennung pro numerischem Feld (v0.40), eine Collection.
std::vector<FieldOutliers> discoverOutliers(const Collection& c, const DiscoveryOptions& opts = {});
/// @brief Wie oben, fuer beliebige Collections (auch database-übergreifend).
std::vector<FieldOutliers> discoverOutliers(const std::vector<Collection>& colls,
                                            const DiscoveryOptions& opts = {});

/// @brief Gesamtergebnis einer automatischen Analyse (v0.33).
struct AnalysisReport {
    std::vector<CollectionProfile> profiles;         ///< Datenprofil pro Collection
    std::vector<DiscoveredRelation> relations;       ///< FK / FD / Korrelation / Near-Dupes
    std::vector<OrphanReport> orphans;               ///< Integritäts-Checks (FK-Orphans)
    std::vector<MergeSuggestion> mergeSuggestions;   ///< Merge-Kandidaten (v0.32)
    std::vector<std::vector<DiscoveredRelation>> paths; ///< wichtigste Multi-Hop-FK-Pfade
    std::vector<FieldOutliers> outliers;             ///< v0.40: Ausreißer pro numerischem Feld
    std::size_t totalCollections = 0;
    std::size_t totalDocs = 0;
    double healthScore = 0.0;                        ///< 0..100 (v0.33, Ampel: >=80 gruen, >=50 gelb)
};

/// @brief Automatische Gesamtanalyse (v0.33): Profile + alle Discovery-Funktionen +
/// Orphans + Merge-Vorschläge + Health-Score – read-only, auch cross-database
/// (einfach `std::vector<Collection>` aus mehreren Databases übergeben).
AnalysisReport analyze(const std::vector<Collection>& colls, const DiscoveryOptions& opts = {});
/// @brief Wie oben für eine ganze Database.
AnalysisReport analyze(Database& db, const DiscoveryOptions& opts = {});

/// @brief Report als JSON (v0.33) – maschinenlesbar/LLM-freundlich.
std::string toJson(const AnalysisReport& r);
/// @brief Report als Markdown (v0.33) – menschenlesbar: Tabellen, Health-Ampel, Empfehlungen.
std::string toMarkdown(const AnalysisReport& r);
/// @brief Beziehungsgraph des Reports als Mermaid (v0.33).
std::string toMermaid(const AnalysisReport& r);
/// @brief Kompakte Konsolen-Zusammenfassung (v0.33).
void printSummary(const AnalysisReport& r, std::ostream& os);

/// @brief Report als JSON (v0.29; inkl. Kardinalität ab v0.30).
std::string toJson(const std::vector<DiscoveredRelation>& rels);
/// @brief Report als Mermaid-Graph (v0.29): Collections als Knoten, Relationen als Kanten.
std::string toMermaid(const std::vector<DiscoveredRelation>& rels);

} // namespace memdb

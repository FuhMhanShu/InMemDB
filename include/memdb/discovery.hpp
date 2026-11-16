#pragma once

#include "collection.hpp"
#include "database.hpp"
#include "value.hpp"

#include <cstddef>
#include <cstdint>
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

/// @brief Report als JSON (v0.29).
std::string toJson(const std::vector<DiscoveredRelation>& rels);
/// @brief Report als Mermaid-Graph (v0.29): Collections als Knoten, Relationen als Kanten.
std::string toMermaid(const std::vector<DiscoveredRelation>& rels);

} // namespace memdb

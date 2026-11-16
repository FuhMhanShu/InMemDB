#pragma once

#include "collection.hpp"
#include "database.hpp"
#include "document_id.hpp"

#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace memdb {

/// @brief Eine gerichtete, typisierte Referenz zwischen zwei Datensätzen/Collections (FA-18).
struct Reference {
    std::string fromCollection;            ///< Quell-Collection
    std::optional<DocumentId> fromId;      ///< Quell-Dokument (nullopt = ganze Collection)
    std::string toCollection;              ///< Ziel-Collection
    std::optional<DocumentId> toId;        ///< Ziel-Dokument (nullopt = ganze Collection)
    std::string relation;                  ///< Beziehungsname (z. B. "kennt", "bestellt")

    bool operator==(const Reference& other) const {
        return fromCollection == other.fromCollection && fromId == other.fromId &&
               toCollection == other.toCollection && toId == other.toId && relation == other.relation;
    }
    bool operator!=(const Reference& other) const { return !(*this == other); }
};

/// @brief Richtung der Referenz-Abfrage.
enum class RefDirection {
    Outgoing, ///< Referenzen vom Datensatz weg
    Incoming, ///< Referenzen zum Datensatz hin
    Both      ///< beide Richtungen
};

/// @brief Ein Treffer der traversierenden Suche (`ReferenceIndex::search`).
struct SearchHit {
    std::string collection;   ///< Collection des Treffers
    DocumentId id;            ///< ID des Treffers
    Document document;        ///< Dokument des Treffers
    std::vector<Reference> path; ///< Kanten-Pfad von der Quelle zum Treffer
};

/// @brief Erweiterungsmodul für Referenzen zwischen Collections/Dokumenten (FA-18).
///
/// Referenzen werden name-basiert in einer normalen (reservierten) Collection
/// gespeichert – ohne Änderung am Kern (NFA-03) und snapshot-fähig (FA-14).
/// Details siehe `docs/de/guides/references.md`.
class ReferenceIndex {
public:
    /// @param db Database, in der gearbeitet wird.
    /// @param storageCollection Name der Speicher-Collection (Default `__references`).
    explicit ReferenceIndex(Database& db, std::string storageCollection = "__references");

    /// @brief Name der Speicher-Collection.
    const std::string& storageCollection() const { return storage_; }

    /// @brief Referenz anlegen (Ringverweise sind erlaubt).
    /// @throws CollectionNotFoundError / DocumentNotFoundError bei ungültigen Zielen,
    ///         ReferenceExistsError bei identischem Duplikat.
    Reference link(const std::string& fromCollection, std::optional<DocumentId> fromId,
                   const std::string& toCollection, std::optional<DocumentId> toId,
                   std::string relation = {});
    /// @brief Referenz entfernen; false, wenn nicht vorhanden.
    bool unlink(const std::string& fromCollection, std::optional<DocumentId> fromId,
                const std::string& toCollection, std::optional<DocumentId> toId,
                const std::string& relation = {});

    /// @brief Referenzen eines Datensatzes/einer Collection abfragen (indexbeschleunigt).
    std::vector<Reference> references(const std::string& collection, RefDirection dir,
                                      std::optional<DocumentId> id = std::nullopt) const;

    /// @brief Referenzen, deren Ziel nicht (mehr) existiert.
    std::vector<Reference> dangling() const;
    /// @brief Alle Dangling-Referenzen entfernen.
    /// @return Anzahl der entfernten Referenzen.
    std::size_t pruneDangling();

    /// @brief DFS-Traversierung ab einem Datensatz; terminiert auch bei Ringverweisen.
    std::vector<Reference> traverse(const std::string& fromCollection, std::optional<DocumentId> fromId,
                                    RefDirection dir = RefDirection::Outgoing,
                                    std::size_t maxDepth = std::numeric_limits<std::size_t>::max()) const;
    /// @brief true, wenn ab dem Datensatz ein Ring erreichbar ist.
    bool hasCycle(const std::string& fromCollection, std::optional<DocumentId> fromId,
                  RefDirection dir = RefDirection::Outgoing) const;

    /// @brief Traversierende Suche: testet jedes erreichte Dokument gegen `q`.
    std::vector<SearchHit> search(const std::string& fromCollection, std::optional<DocumentId> fromId,
                                  const Expression& q = Expression{},
                                  RefDirection dir = RefDirection::Outgoing,
                                  std::size_t maxDepth = std::numeric_limits<std::size_t>::max()) const;

    /// @brief Index-beschleunigter Join bei Abfrage (Ausblick FA-18, v0.17):
    /// IDs der Dokumente in `fromCollection`, die über `relation` mit einem Dokument
    /// in `toCollection` verknüpft sind, das `toQuery` erfüllt.
    /// Ablauf: `toQuery` auf der Ziel-Collection (indexbeschleunigt, falls Index) →
    /// Ziel-IDs → `to_key`-Index der Speicher-Collection → Quell-IDs.
    /// Komplexität O(#Zieltreffer), nicht O(#Quelldokumente) – im Gegensatz zu
    /// `@relation.path` in einer DSL-Query (Traversierung pro Dokument).
    std::vector<DocumentId> joinIds(const std::string& fromCollection, const std::string& relation,
                                    const std::string& toCollection,
                                    const Expression& toQuery = Expression{}) const;

    /// @brief Wie `joinIds`, liefert die Quell-Dokumente selbst.
    std::vector<Document> join(const std::string& fromCollection, const std::string& relation,
                               const std::string& toCollection,
                               const Expression& toQuery = Expression{}) const;

    /// @brief Löst einen relativen Pfad gegen eine Quell-Collection auf
    /// (`./users`, `../orders`, `/abs`; nackter Name bleibt unverändert).
    std::string resolve(const std::string& fromCollection, const std::string& relPath) const;

    /// @brief Low-Level: `from_key`/`to_key` aller Referenzen gemäß `keyMap`
    /// umschreiben (alt → neu, exaktes String-Matching). Wird von
    /// `Database::mergeCollections` (FA-24) genutzt, um beim Löschen von
    /// Quell-Collections keine Dangling-Refs zu erzeugen.
    /// @return Anzahl umgeschriebener Referenz-Dokumente.
    std::size_t rewriteKeys(const std::map<std::string, std::string>& keyMap);

private:
    Database* db_;
    std::string storage_;
    Collection coll_;
};

} // namespace memdb

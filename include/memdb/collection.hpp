#pragma once

#include "aggregate.hpp"
#include "change_event.hpp"
#include "cursor.hpp"
#include "document.hpp"
#include "index.hpp"
#include "query.hpp"
#include "schema.hpp"
#include "transaction.hpp"

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace memdb {

class Database;

namespace detail {
struct CollectionState;
}

/// @brief Benannte, schema-lose Menge von Dokumenten mit CRUD, Queries,
/// Indizes, Transaktionen, Aggregationen, JSON-Snapshot und Observern.
///
/// Handle-Semantik: Kopien zeigen auf dieselbe zugrunde liegende Collection.
/// Thread-sicher (Lesezugriffe parallel via `shared_lock`, Schreibzugriffe
/// serialisiert via `unique_lock`). Instanzen erhält man über
/// `Database::createCollection` / `Database::collection`.
class Collection {
public:
    Collection(const Collection&) = default;
    Collection(Collection&&) noexcept = default;
    Collection& operator=(const Collection&) = default;
    Collection& operator=(Collection&&) noexcept = default;

    /// @brief Name der Collection.
    std::string name() const;

    /// @brief Dokument einfügen; vergibt automatisch eine neue ID.
    /// @return Die vergebene ID.
    /// @throws UniqueConstraintError bei Verletzung eines Unique-Index.
    DocumentId insert(Document doc);
    /// @brief Wie `insert(Document)`, aus Feldliste: `insert({{"name", "Ada"}})`.
    DocumentId insert(std::initializer_list<std::pair<const std::string, Value>> fields);
    /// @brief Mehrere Dokumente in einem Aufruf einfügen (Bulk).
    /// @return Die vergebenen IDs in Reihenfolge.
    std::vector<DocumentId> insertBulk(std::vector<Document> docs);

    /// @brief Dokument nach ID lesen; `nullopt`, wenn nicht vorhanden. O(1).
    std::optional<Document> findById(DocumentId id) const;
    /// @brief Alle Dokumente, die `q` matchen (nutzt Indizes, sonst Vollscan).
    Cursor find(const Expression& q = Expression{}) const;
    /// @brief Erstes Dokument, das `q` matcht, oder `nullopt`.
    std::optional<Document> findOne(const Expression& q = Expression{}) const;
    /// @brief Anzahl der Dokumente, die `q` matchen.
    std::size_t count(const Expression& q = Expression{}) const;

    /// @brief Alle Treffer von `q` feldweise ändern.
    /// @return Anzahl der geänderten Dokumente.
    /// @throws UniqueConstraintError (Änderungen werden dann gar nicht angewendet).
    std::size_t update(const Expression& q, const std::function<void(Document&)>& mutator);
    /// @brief Einzelnes Dokument feldweise ändern; false, wenn ID nicht existiert.
    bool updateById(DocumentId id, const std::function<void(Document&)>& mutator);
    /// @brief Dokument komplett ersetzen (ID bleibt erhalten); false, wenn nicht vorhanden.
    bool replace(DocumentId id, Document doc);
    /// @brief Upsert nach ID (FA-07): existiert die ID, wird ersetzt (ID bleibt),
    /// sonst wird eingefügt (neue ID).
    /// @return Die betroffene ID.
    DocumentId upsertById(DocumentId id, Document doc);
    /// @brief Upsert nach Query (FA-07): erstes Match wird ersetzt (ID bleibt),
    /// sonst Einfügen mit neuer ID.
    /// @return Die betroffene ID.
    DocumentId upsert(const Expression& q, Document doc);

    /// @brief Alle Treffer von `q` löschen.
    /// @return Anzahl der entfernten Dokumente.
    std::size_t remove(const Expression& q = Expression{});
    /// @brief Dokument nach ID löschen; false, wenn nicht vorhanden.
    bool removeById(DocumentId id);

    /// @brief Anzahl der Dokumente.
    std::size_t size() const;

    /// @brief Sekundärindex auf Feldpfad anlegen (FA-10).
    /// @throws IndexExistsError wenn bereits vorhanden; UniqueConstraintError bei Duplikaten (unique=true).
    Index createIndex(const std::string& fieldPath, bool unique = false);
    /// @brief Index löschen; false, wenn nicht vorhanden.
    bool dropIndex(const std::string& fieldPath);
    /// @brief true, wenn ein Index auf dem Feldpfad existiert.
    bool hasIndex(const std::string& fieldPath) const;
    /// @brief Alle Index-Definitionen.
    std::vector<Index> indexes() const;

    /// @brief Diagnose: Kandidatenzahl der letzten find/findOne/count-Abfrage
    /// (Index-Nutzung nachweisbar; Vollscan = size()).
    std::size_t lastQueryCandidateCount() const;

    /// @brief Startet eine atomare Transaktion auf dieser Collection (FA-12).
    Transaction beginTransaction();

    /// @brief Exportiert alle Dokumente als JSON-Array (FA-14; ohne IDs).
    std::string toJson() const;
    /// @brief Ersetzt den gesamten Inhalt durch die Dokumente aus dem JSON-Array.
    /// Index-Definitionen bleiben erhalten und werden neu befüllt.
    /// @throws JsonError bei Parse-/Strukturfehlern (Zustand bleibt unverändert).
    void loadJson(std::string_view json);
    /// @brief Alle Dokumente löschen (Index-Definitionen bleiben erhalten).
    void clear();

    /// @brief Skalares Aggregat über alle Treffer von `q` (FA-13).
    /// @param op Min/Max/Sum/Avg/Count; bei Count wird fieldPath ignoriert.
    /// @param fieldPath Feldpfad, über den aggregiert wird (Punktnotation möglich).
    /// @param q Optionale Query; leer = alle Dokumente.
    /// @return Aggregat; Count auf leerer Menge = 0, sonst `nullopt` bei leerer Menge.
    std::optional<Value> aggregate(AggOp op, const std::string& fieldPath,
                                    const Expression& q = Expression{}) const;
    /// @brief Gruppierung nach Feldwert (FA-13); Ergebnis nach Schlüssel sortiert.
    /// @param keyPath Feldpfad des Gruppenschlüssels.
    /// @param q Optionale Query; leer = alle Dokumente.
    /// @param op Aggregat für `aggPath` pro Gruppe (Count → nur count, value=nullopt).
    /// @param aggPath Feldpfad für das Aggregat (bei op != Count).
    std::vector<GroupAggregate> groupBy(const std::string& keyPath, const Expression& q = Expression{},
                                         AggOp op = AggOp::Count, const std::string& aggPath = "") const;

    /// @brief Observer registrieren (FA-15); Events feuern nach Abschluss der
    /// Operation außerhalb des Locks, in Transaktionen erst bei commit().
    /// @return Handle für `offChange`.
    std::uint64_t onChange(ChangeCallback cb);
    /// @brief Observer entfernen; false, wenn Handle unbekannt.
    bool offChange(std::uint64_t handle);

    /// @brief Striktes Schema setzen (FA-20): prüft zuerst alle vorhandenen
    /// Dokumente; ab dann darf kein Feld undefiniert sein und jeder Typ muss passen.
    /// @throws SchemaError bei Verletzung (Schema wird dann nicht gesetzt).
    void setSchema(Schema schema);
    /// @brief Aktuelles Schema oder `nullopt` (schema-los).
    std::optional<Schema> schema() const;
    /// @brief true, wenn ein Schema gesetzt ist.
    bool hasSchema() const;
    /// @brief Schema entfernen; Collection ist danach wieder schema-los.
    void clearSchema();

private:
    explicit Collection(std::shared_ptr<detail::CollectionState> impl);
    static Collection create(std::string name);
    std::shared_ptr<detail::CollectionState> impl_;

    friend class Database;
    friend class Transaction;
};

} // namespace memdb

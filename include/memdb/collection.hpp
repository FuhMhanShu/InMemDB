#pragma once

#include "aggregate.hpp"
#include "change_event.hpp"
#include "cursor.hpp"
#include "document.hpp"
#include "index.hpp"
#include "limits.hpp"
#include "metrics.hpp"
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

/// @brief Status des Hybrid-/Out-of-Core-Modus einer Collection (v0.18).
struct OverflowInfo {
    bool enabled = false;
    std::string spillFile;      ///< Datei mit den ausgelagerten Dokumenten
    std::size_t maxInMemory = 0;///< Obergrenze Dokumente im RAM
    std::size_t inMemory = 0;   ///< Dokumente aktuell im RAM
    std::size_t spilled = 0;    ///< Dokumente aktuell im Spill-File
    std::uint64_t spillBytes = 0; ///< Dateigroesse des Spill-Files
};

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
    /// @note Kopiert jedes Treffer-Dokument in den Cursor – für große Ergebnismengen
    ///        siehe die kopierfreien Alternativen `findIds` / `forEach`.
    Cursor find(const Expression& q = Expression{}) const;
    /// @brief Nur die IDs der Treffer von `q` – **ohne Dokument-Kopien** (schneller
    ///        für große Ergebnismengen; Dokumente danach einzeln per `findById` laden).
    std::vector<DocumentId> findIds(const Expression& q = Expression{}) const;
    /// @brief Callback pro Treffer von `q` – **ohne Dokument-Kopien**. Read-only:
    ///        Der Callback läuft unter dem Lese-Lock und darf nicht in die Collection
    ///        schreiben (sonst Deadlock); zum Ändern `update`/`find` verwenden.
    /// @return Anzahl der Treffer.
    std::size_t forEach(const Expression& q, const std::function<void(const Document&)>& cb) const;
    /// @brief Erstes Dokument, das `q` matcht, oder `nullopt`.
    std::optional<Document> findOne(const Expression& q = Expression{}) const;
    /// @brief Anzahl der Dokumente, die `q` matchen.
    std::size_t count(const Expression& q = Expression{}) const;

    /// @name String-DSL (NFA-03, siehe `docs/query-dsl.md`)
    /// Gleiche Operationen mit Query als String, z. B.
    /// `find("age >= 18 && name.contains('a')")`; Parsing via `parseQuery`.
    /// @throws QueryError bei DSL-Syntaxfehler.
    /// @{
    Cursor find(std::string_view dsl) const;
    std::vector<DocumentId> findIds(std::string_view dsl) const;
    std::size_t forEach(std::string_view dsl, const std::function<void(const Document&)>& cb) const;
    std::optional<Document> findOne(std::string_view dsl) const;
    std::size_t count(std::string_view dsl) const;
    std::size_t update(std::string_view dsl, const std::function<void(Document&)>& mutator);
    /// @brief Bulk-Update per DSL mit Projektion (v0.21): Query waehlt, Set-DSL beschreibt die
    /// Aenderung, z. B. `update("age >= 18", "adult = true, status = 'adult', age = age + 1")`.
    /// Set-Grammatik siehe `parseUpdateSet` (`docs/query-dsl.md`).
    /// @throws QueryError bei Syntaxfehler, Error bei Typfehlern in der Set-DSL.
    std::size_t update(std::string_view dsl, std::string_view setDsl);
    std::size_t remove(std::string_view dsl);

    /// @brief DSL-Varianten mit Referenz-Kontext: `@relation.path` (FA-18) wird über `db`
    ///        in dieser Collection aufgelöst (z. B. `orders.count("@kunde.total > 100", db)`).
    /// @{
    Cursor find(std::string_view dsl, Database& db) const;
    std::vector<DocumentId> findIds(std::string_view dsl, Database& db) const;
    std::size_t forEach(std::string_view dsl, Database& db,
                        const std::function<void(const Document&)>& cb) const;
    std::optional<Document> findOne(std::string_view dsl, Database& db) const;
    std::size_t count(std::string_view dsl, Database& db) const;
    std::size_t update(std::string_view dsl, Database& db,
                       const std::function<void(Document&)>& mutator);
    /// @brief Wie `update(dsl, setDsl)`, aber mit Referenz-Kontext (`@relation.path`) in der Query.
    std::size_t update(std::string_view dsl, std::string_view setDsl, Database& db);
    std::size_t remove(std::string_view dsl, Database& db);
    /// @}

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

    /// @brief Anzahl der Dokumente (inkl. ausgelagerter, falls Hybrid-Modus aktiv).
    std::size_t size() const;

    /// @name Hybrid / Out-of-Core (v0.18, opt-in)
    /// Ab `maxInMemoryDocs` im RAM werden die aeltesten Dokumente (FIFO) in eine
    /// Spill-Datei ausgelagert (Append-only-Log: `[u64 id][u32 len][JSON]`) und bei
    /// Bedarf wieder eingelesen. Abfragen/Updates/Removes sehen RAM + Spill.
    /// @throws Error waehrend einer aktiven Transaktion.
    /// @{
    void setOverflow(const std::string& spillFile, std::size_t maxInMemoryDocs);
    /// @brief Hybrid-Modus aus: alles zurueck ins RAM laden, Spill-Datei loeschen.
    void disableOverflow();
    OverflowInfo overflowInfo() const;
    /// @brief Spill-Datei kompaktieren (alte/overwritte Records entfernen).
    /// @return freigegebene Bytes.
    std::uint64_t compactSpill();
    /// @}

    /// @brief Sekundärindex auf Feldpfad anlegen (FA-10).
    /// @throws IndexExistsError wenn bereits vorhanden; UniqueConstraintError bei Duplikaten (unique=true).
    Index createIndex(const std::string& fieldPath, bool unique = false);

    /// @brief Optionale Limits setzen (v0.39, FA-28). Default 0 = unbegrenzt.
    /// @throws LimitError bei Überschreitung durch spätere Schreibzugriffe.
    void setLimits(const CollectionLimits& l);
    /// @brief Aktuell gesetzte Limits (v0.39).
    CollectionLimits limits() const;
    /// @brief Index löschen; false, wenn nicht vorhanden.
    bool dropIndex(const std::string& fieldPath);
    /// @brief true, wenn ein Index auf dem Feldpfad existiert.
    bool hasIndex(const std::string& fieldPath) const;
    /// @brief Alle Index-Definitionen.
    std::vector<Index> indexes() const;

    /// @brief Diagnose: Kandidatenzahl der letzten find/findOne/count-Abfrage
    /// (Index-Nutzung nachweisbar; Vollscan = size()).
    std::size_t lastQueryCandidateCount() const;

    /// @brief Metrik-/Diagnose-Snapshot der Collection (v0.22): Zaehler fuer Schreib- und
    /// Lesezugriffe, Index-Nutzung, Query-Dauer, Spill-IO, Transaktionen; plus Ist-Werte
    /// (docs, indexCount, lastCandidateCount). Siehe `docs/metrics.md`.
    CollectionMetrics metrics() const;
    /// @brief Alle Metrik-Zaehler dieser Collection zuruecksetzen (Ist-Werte bleiben).
    void resetMetrics();

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
    /// @param op Aggregat für `aggPath` pro Gruppe (Count → nur count, value=nullopt;
    ///           Distinct → value = Array der distinct Werte, sortiert).
    /// @param aggPath Feldpfad für das Aggregat (bei op != Count).
    std::vector<GroupAggregate> groupBy(const std::string& keyPath, const Expression& q = Expression{},
                                         AggOp op = AggOp::Count, const std::string& aggPath = "") const;

    /// @brief Distinct-Werte eines Feldes, sortiert (Foundation v0.27, z. B. für
    /// Relationship-Discovery). **Index-aware**: bei leerer Query und vorhandenem
    /// Index auf dem Feld werden die Werte direkt aus dem Index gelesen
    /// (O(#distinct) statt Vollscan; Metrik `indexQueries` steigt).
    /// Fehlende Feldwerte werden nicht aufgenommen.
    std::vector<Value> distinctValues(const std::string& fieldPath,
                                       const Expression& q = Expression{}) const;
    /// @brief Wie oben, mit String-DSL als Query.
    std::vector<Value> distinctValues(const std::string& fieldPath, std::string_view dsl) const;

    /// @brief Reproduzierbares Zufalls-Sample von Dokument-IDs (Foundation v0.27):
    /// Reservoir-Sampling (Algorithm R) über RAM + Spill; gleicher `seed` → gleiches
    /// Sample. `maxN >= size()` liefert alle IDs, `maxN == 0` ein leeres Sample.
    std::vector<DocumentId> sample(std::size_t maxN,
                                   std::uint64_t seed = 0x9E3779B97F4A7C15ULL) const;

    /// @brief TTL / Auto-Expiry (FA-25, v0.26), **opt-in – defaultmäßig AUS**:
    /// Dokumente ohne `expiresAt`-Feld verfallen nie. Abgelaufene Dokumente
    /// (`expiresAt <= now`, Unix-Sekunden) werden lazy bei Lesezugriffen
    /// (findById/find/findIds/forEach/findOne/count/aggregate/groupBy/toJson/size)
    /// und manuell per `expireNow()` entfernt (Remove-Event + Metrik `expiredDocs`).
    /// Mitten in einer Transaktion wird nicht gesweept (Snapshot-Isolation).
    /// @param seconds Verfallszeit ab jetzt in Sekunden; setzt `expiresAt = now + seconds`.
    /// @return false, wenn das Dokument nicht existiert.
    bool expiresIn(DocumentId id, std::int64_t seconds);
    /// @brief Wie `expiresIn`, aber absolute Verfallszeit (Unix-Sekunden).
    bool expiresAt(DocumentId id, std::int64_t unixSeconds);
    /// @brief Aktuellen `expiresAt`-Wert eines Dokuments; nullopt = keins/nicht vorhanden.
    std::optional<std::int64_t> expiresAtOf(DocumentId id) const;
    /// @brief Collection-weites Default-TTL für neue Dokumente (insert/upsert):
    /// wird nur gesetzt, wenn das Doc kein `expiresAt` hat und das Schema (FA-20)
    /// das Feld erlaubt. `0` (Default) = aus.
    void setDefaultExpiresIn(std::int64_t seconds);
    /// @brief Aktuelles Default-TTL in Sekunden (0 = aus).
    std::int64_t defaultExpiresIn() const;
    /// @brief Manueller Sweep: alle abgelaufenen Dokumente jetzt entfernen.
    /// @return Anzahl entfernter Dokumente.
    std::size_t expireNow();
    /// @brief Uhr für TTL-Tests injizieren (Default: `std::time(nullptr)`);
    /// liefert Unix-Sekunden.
    void setClockForTesting(std::function<std::int64_t()> fn);

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
    void loadJsonParsed(Value arrayValue);
    std::shared_ptr<detail::CollectionState> impl_;

    friend class Database;
    friend class Transaction;
};

} // namespace memdb

# InMemDB

**InMemDB ist eine Datenbank, die komplett im Arbeitsspeicher (RAM) läuft und direkt in
C++-Programme eingebettet wird** – als objektorientierte C++17-Bibliothek, ohne eigenen
Server, ohne Netzwerk und ohne externe Pflichtabhängigkeiten (Auslieferung als statische
Library, intern über das Pimpl-Idiom entkoppelt).

Statt Tabellen gibt es **Collections** (Sammlungen) von **Dokumenten**. Ein Dokument ist
ein JSON-ähnliches Objekt aus Feldern – und weil InMemDB **schema-los** ist, darf jedes
Dokument beliebige, auch wechselnde Felder haben (z. B. `{"name": "Ada", "age": 36}` und
`{"name": "Bob", "vip": true}` in derselben Collection).

Abgefragt wird auf zwei Arten, die zur selben Query werden: als **typsicher C++-Ausdruck**
(`Field("age") >= 18 && Field("stadt") == "Berlin"`) oder als **String** (`"age >= 18 && stadt == 'Berlin'"`).
Über Felder kann man beliebig **Indizes** legen, damit Abfragen nicht alles durchsuchen müssen.

> **Wichtig:** „In-Memory" heißt: Beim Beenden des Prozesses sind die Daten weg.
> Sichern geht jederzeit als JSON-Snapshot (`saveToFile`) oder verschlüsselt
> (`saveToFileEncrypted(path, passwort)`).

## Quickstart

```cpp
#include <memdb/memdb.hpp>
memdb::Database db("shop");
auto products = db.createCollection("products");
products.createIndex("name", true);                              // eindeutiger Index auf "name"
products.insert({{"name", "Buch"}, {"preis", 10.5}, {"tags", {"neu"}}});
for (const auto& p : products.find("preis < 20 && tags.contains('neu')"))
    std::cout << p.at("name").asString() << '\n';                // DSL-Abfrage -> "Buch"
db.saveToFileEncrypted("shop.enc", "geheim");                    // verschlüsselter Snapshot
```

## Was kann InMemDB? (Features, v0.38)

**Daten & Abfragen**
- Schema-lose Dokumente mit dynamisch erweiterbaren Feldern (auch verschachtelt, Punktnotation `"adresse.plz"`)
- Abfragen als C++-Ausdruckbaum oder String-DSL, inkl. eigener Operatoren (`matches` für Mini-RegEx – Thompson-NFA, gehärtet v0.34 mit harten Limits gegen Stack-Overflow/Hang, kompiliert einmalig pro Query; `in`, eigene via Registry) und `@relation`-Joins (v0.14)
- Sekundärindizes pro Feld, inkl. Unique-Constraints (z. B. eindeutige E-Mail) – beschleunigte Gleichheits- und Bereichsabfragen (FA-10)
- Aggregationen: `count`, `min`, `max`, `sum`, `avg`, `distinct` sowie `groupBy` (FA-13)
- Foundations (v0.27): `memdb::ValueLess` (Value als map/set-Schlüssel), `distinctValues(feld, q)` (index-beschleunigt), `sample(maxN, seed)` (reproduzierbares Zufalls-Sample), `AggOp::Distinct`, `Expression::isMatchAll()`
- Bulk-Operationen (`insertBulk`) und Bulk-Update per DSL mit Projektion: `update("age >= 18", "adult = true, age = age + 1")` (v0.21)
- Upsert: einfügen oder aktualisieren, per ID oder per Query (FA-07)

**Struktur & Beziehungen**
- **Referenzen** zwischen Collections/Dokumenten (`ReferenceIndex`): in beide Richtungen abfragbar, Traversierung inkl. Ringprüfung (`traverse`/`hasCycle`/`search`), Dangling-Erkennung und -Bereinigung (FA-18); index-beschleunigter Join bei Abfrage (`joinIds`/`join`, v0.17); direkt in Queries als `@kunde.name == 'Ada'`
- **Merge:** mehrere Collections in eine zusammenführen – `Database::mergeCollections` bzw. `mergeAllCollections` für die ganze Datenbank, mit neuen IDs, `_source`-Herkunftsfeld und optionalem Löschen der Quellen inkl. Referenz-Umschreiben (FA-24, v0.24/v0.25)
- **Datenbank-Graph** als JSON, Mermaid oder Markdown – auch aus Snapshot-Dateien; CLI-Tool `inmemdb_graph` (FA-22)

**Sicherheit, Persistenz & Betrieb**
- Atomare **Transaktionen** pro Collection: alles oder nichts, commit/rollback, Rollback per Copy-on-Write (schnell auch bei Millionen Dokumenten, v0.19); RAII-Rollback (FA-12)
- **JSON-Snapshot** der ganzen DB oder einzelner Collections: `toJson`/`fromJson`/`saveToFile`/`loadFromFile` (FA-14); eigener Parser gehärtet v0.37 (Verschachtelungs-Limit 1000 gegen Segfault, `$binary` nur bei gültigem Hex, Hex-Float-Fallback nur mit `0x`)
- **Verschlüsselte Snapshots**: `saveToFileEncrypted`/`loadFromFileEncrypted` mit Passwort (PBKDF2 + ChaCha20 + HMAC-SHA256, ohne externe Krypto-Bibliothek, FA-19)
- Optionales **striktes Schema** pro Collection: Feldtypen als Metadaten, undefinierte Felder werden abgelehnt (FA-20)
- **Observer**: Callbacks bei jedem Einfügen/Ändern/Löschen (`onChange`), in Transaktionen erst beim Commit (FA-15)
- **TTL / Auto-Expiry** (abschaltbar, default aus): Dokumente mit Verfallszeit (`expiresIn(sek)` / `expiresAt(unixSekunden)`) werden automatisch entfernt (FA-25, v0.26)
- **Relationship-Discovery** (FA-27, v0.29): `discoverForeignKeys` / `discoverFunctionalDependencies` / `discoverCorrelations` / `discoverNearDuplicates` / `discoverRelations` – collection- und database-übergreifend, ohne Abhängigkeiten; `applyDiscovered` legt FA-18-Referenzen an; Kardinalität (1:1/n:1/1:n/n:m), Orphan-Report, Pfad-Suche (v0.30), Datenprofil + Candidate Keys (v0.31), Cross-DB-Diff + Merge-Vorschläge (v0.32), **automatische Gesamtanalyse + Report** `analyze()` mit Health-Score (Ampel 🟢🟡🔴) und Präsentation als Markdown/JSON/Mermaid/Konsole (`toMarkdown`/`toJson`/`toMermaid`/`printSummary`, v0.33); CLI `inmemdb_discover` mit JSON/Mermaid-Report (`--orphans`, `--path`) und Gesamt-Report (`--report <prefix>`)
- **Hybrid / Out-of-Core**: bei zu wenig RAM lagert `setOverflow(spillFile, maxInMemory)` älteste Dokumente in eine Datei aus und liest sie bei Bedarf nach (v0.18)
- **Metriken & Diagnose** (v0.22): `Collection::metrics()`/`Database::metrics()` – Zugriffe, Index-Nutzung vs. Vollscan, Query-Dauer, Spill-IO, Transaktionen, Events, TTL-Expiry, `evalErrors`/`eventErrors` (v0.35). **Exception-Policy** (v0.35, [docs/error-handling.md](docs/error-handling.md)): Query-Scan und Observer fangen pro Element ab (Zähler in Metriken), Bulk-Load validiert vor Mutation (atomar), CLI beendet sauber mit Exit-Code; echte Fehler (Unique/Schema/Crypto/kaputte Datei) werfen weiterhin. **MessageHandler-Singleton** (v0.36): eigene Handler via `MessageHandler::instance().subscribe(...)` – die Abfang-Stellen melden dorthin (`query-eval`/`observer`/`load`/`import`/`regex`/`cli`), thread-safe, Level-Filter + Zähler. **Härtung** ([docs/hardening.md](docs/hardening.md)): harte Limits in allen Parsern (RegEx/JSON/DSL/BSON – keine Segfaults/Hangs mehr an pathologischen Eingaben), strikte Zahlen, Exception-Policy + MessageHandler
- **Import** aus CSV, JSON/JSONL, SQL-Dump, BSON, eigenen Snapshots und (optional, `INMEMDB_WITH_SQLITE=ON`) SQLite-Dateien; CLI-Tool `inmemdb_import` (FA-21); **Fremdschlüssel werden als FA-18-Referenzen mitimportiert** (SQLite `PRAGMA foreign_key_list`, SQL-Dump `REFERENCES`/`FOREIGN KEY`, abschaltbar via `--no-foreign-keys`, FA-26, v0.28)
- Thread-Safety über `shared_mutex` (parallele Lesezugriffe, serialisierte Schreibzugriffe); Zahlen sind locale-unabhängig (immer Dezimalpunkt, auch bei `de_DE`, Bugfix v0.23.1)
- **Qt/qmake-Einbindung** über `InMemDB.pro`/`InMemDB.pri` plus fertiger `QAbstractTableModel`-Adapter (`memdb::qt::MemDbTableModel`, nur QtCore, ohne harte Qt-Abhängigkeit, FA-23, v0.23)

## Dokumentation

- [Anforderungen](anforderungen.md) – das Lastenheft (fachliche Anforderungen FA-01…FA-27, nicht-funktionale NFA-01…NFA-09)
- [Architektur-Entscheidungen](docs/architecture.md) – warum statische Library mit Pimpl
- [Query-API](docs/query-api.md) – der C++-Ausdruckbaum im Detail
- [String-DSL](docs/query-dsl.md) – `find("age >= 18 && ...")`, eigene Operatoren, `@relation`-Joins
- [Indizes](docs/indexes.md) (FA-10) · [Transaktionen](docs/transactions.md) (FA-12) · [Snapshot/JSON](docs/snapshot-json.md) (FA-14) · [Verschlüsselte Snapshots](docs/encrypted-snapshot.md) (FA-19)
- [Aggregationen](docs/aggregations.md) (FA-13) · [Observer](docs/observer.md) (FA-15) · [Schema](docs/schema.md) (FA-20)
- [Referenzen & Joins](docs/references.md) (FA-18) · [Merge von Collections](docs/merge-collections.md) (FA-24) · [TTL/Auto-Expiry](docs/ttl-expiry.md) (FA-25) · [Relationship-Discovery & Auto-Analyse/Report](docs/discovery.md) (FA-27, v0.29–v0.33) · [Discovery-Guide (erklärend, mit Beispielen & Graphen)](docs/discovery-guide.md)
- [Import externer Daten](docs/import.md) (FA-21, z. B. SQLite/SQL-Dump/CSV) · [Datenbank-Graph](docs/db-graph.md) (FA-22) · [Typ-Mapping zu SQL/MongoDB/JSON](docs/type-mapping.md)
- [Hybrid / Out-of-Core](docs/hybrid-outofcore.md) (v0.18) · [Metriken & Diagnose](docs/metrics.md) (v0.22) · [Fehlerbehandlung/Exception-Policy](docs/error-handling.md) (v0.35) · [Härtung (Limits & Tests)](docs/hardening.md) (v0.34–v0.38) · [Qt/qmake & Modell-Adapter](docs/qt-qmake.md) (FA-23, v0.23) · [Performance & Benchmark](docs/performance.md)

## Build & Test

Voraussetzungen: C++17-Compiler, CMake ≥ 3.20.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Benchmark: `./build/memdb_bench` (InMemDB solo) bzw. mit `-DINMEMDB_WITH_SQLITE=ON` zusätzlich
`./build/memdb_bench_sqlite` (Vergleich gegen SQLite `:memory:`) – gemessene Ergebnisse und
Best Practices in [docs/performance.md](docs/performance.md).

## Einbindung in eigene Projekte

Installieren (Library, Header, CMake-Paket und CLI-Tools) und per `find_package` konsumieren:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix /pfad/zur/installation
```

```cmake
find_package(InMemDB 0.33 REQUIRED)
target_link_libraries(mein_programm PRIVATE memdb::memdb)
```

Oder ohne Installation direkt per FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(InMemDB
    GIT_REPOSITORY https://github.com/FuhMhanShu/InMemDB.git
    GIT_TAG v0.33)
FetchContent_MakeAvailable(InMemDB)
target_link_libraries(mein_programm PRIVATE memdb::memdb)
```

Paketmanager: `vcpkg.json` (vcpkg-Manifest) und `conanfile.py` (Conan-Rezept) liegen im Repo-Wurzelverzeichnis. Der Kern bleibt abhängigkeitsfrei; der optionale SQLite-Import bleibt ein reines CMake-Flag (`INMEMDB_WITH_SQLITE=ON`).

## API-Dokumentation (Doxygen, NFA-08)

Alle öffentlichen Klassen und Methoden in `include/memdb/` sind Doxygen-kommentiert.

```sh
cmake -S . -B build -DINMEMDB_BUILD_DOCS=ON
cmake --build build --target docs   # HTML-Doku nach build/docs/html
```

In der CI läuft dafür der Job **Docs (doxygen)**.

## Ausführliches Beispiel

Ein einziges lauffähiges Programm, das die wichtigsten Bereiche zeigt – schema-lose
Dokumente, Index, Abfragen in beiden Varianten, Referenzen mit Join-in-Query,
Transaktion, Observer, Aggregation, verschlüsselter Snapshot, Import, Graph-Export:

```cpp
#include <memdb/memdb.hpp>
#include <iostream>
using namespace memdb;

int main() {
    Database db("shop");

    // 1) Schema-lose Dokumente: Felder pro Dokument beliebig erweiterbar
    Collection users = db.createCollection("users");
    DocumentId ada = users.insert({{"name", "Ada"}, {"age", 36}, {"stadt", "Berlin"},
                                   {"tags", std::vector<Value>{Value("cpp"), Value("db")}}});
    users.insert({{"name", "Bob"}, {"age", 17}, {"stadt", "Hamburg"}});  // anderes Feld-Set: ok
    users.insert({{"name", "Cy"}, {"age", 40}, {"stadt", "Berlin"}, {"vip", true}});

    // 2) Index + Unique-Constraint (FA-10)
    users.createIndex("name", true);

    // 3) Queries: C++-Ausdruckbaum ODER String-DSL – beides dieselbe Expression
    auto q = (Field("age") >= 18) && (Field("stadt") == "Berlin");
    for (const Document& d : users.find(q))
        std::cout << d.at("name").asString() << "\n";          // Ada, Cy
    std::cout << users.count("age >= 18 && stadt == 'Berlin'") << "\n";   // DSL
    std::cout << users.count("tags.contains('cpp')") << "\n";             // Function-Operator
    std::cout << users.count("name.matches('^[A-C].*')") << "\n";         // Mini-RegEx

    // 4) Referenzen (FA-18) + Join direkt in der Query: '@kunde.name'
    Collection orders = db.createCollection("orders");
    DocumentId o1 = orders.insert({{"total", 500}});
    ReferenceIndex refs(db);
    refs.link("orders", o1, "users", ada, "kunde");
    std::cout << orders.count("@kunde.name == 'Ada' && total >= 100", db) << "\n";  // 1

    // 5) Transaktion (FA-12): alles oder nichts
    {
        auto tx = users.beginTransaction();
        tx.update("age < 18", [](Document& d) { d.set("minor", true); });
        tx.commit();   // sonst rollback() bzw. RAII-Rollback
    }

    // 6) Optionales striktes Schema (FA-20): ab jetzt keine undefinierten Felder mehr
    Schema sch;
    sch.add("name", Value::Type::String).notNull();
    sch.add("age", Value::Type::Int);
    users.setSchema(std::move(sch));

    // 7) Observer (FA-15) + Aggregation (FA-13)
    auto h = users.onChange([](const ChangeEvent& e) { std::cout << "change: " << (int)e.type << "\n"; });
    std::cout << users.aggregate(AggOp::Avg, "age").value().asDouble() << "\n";
    users.offChange(h);

    // 8) Snapshot: JSON (FA-14) oder binär verschlüsselt (FA-19)
    db.saveToFile("shop.inmemdb.json");
    db.saveToFileEncrypted("shop.enc", "geheim");
    Database copy = Database::loadFromFileEncrypted("shop.enc", "geheim");

    // 9) Import externer Quellen (FA-21) + Datenbank-Graph als Mermaid/JSON (FA-22)
    Importer::import(db, "csv", "kunden.csv");                  // oder CLI: inmemdb_import kunden.csv
    std::cout << DbGraph::fromDatabase(db).toMermaid() << "\n"; // rendert direkt als Diagramm
}
```

## Beispiele

Lauffähige Beispiele in `examples/` (werden mitgebaut und als CTest-Smoke-Tests ausgeführt):

| Datei | Thema |
|---|---|
| `01_hello_database.cpp` | Database, Collection, einfügen, per ID lesen |
| `02_dynamic_fields.cpp` | Felder hinzufügen/ändern/umbenennen/löschen, Verschachtelung |
| `03_queries.cpp` | Ausdruckbaum: Vergleiche, `&&`/`||`/`!`, `contains`, `exists`, Punktnotation |
| `04_crud.cpp` | insertBulk, update, updateById, replace, remove, count |
| `05_indexes.cpp` | Indizes anlegen/löschen, Unique-Constraints, beschleunigte Abfragen |
| `06_transactions.cpp` | Atomare Transaktion: commit, rollback, RAII-Rollback |
| `07_snapshot_json.cpp` | JSON-Snapshot speichern/laden (Datei + String) |
| `08_upsert.cpp` | Upsert per Query und per ID |
| `09_aggregations.cpp` | count/sum/avg/min/max/distinct und groupBy |
| `10_observer.cpp` | onChange/offChange, Events inkl. Transaktions-Commit |
| `11_references.cpp` | Referenzen: link/unlink, outgoing/incoming, relative Pfade, dangling/prune, traverse/hasCycle/search (Ringverweis), Snapshot-Roundtrip |
| `12_encrypted_snapshot.cpp` | DB binär verschlüsselt speichern/laden (FA-19) |
| `13_schema.cpp` | Striktes Schema: Feldtypen prüfen, undefinierte Felder ablehnen (FA-20) |
| `14_query_dsl.cpp` | String-DSL: `find("age >= 18 && ...")`, `matches`/`in`, eigener Operator, `@kunde.*`-Join (v0.14) |
| `15_db_graph.cpp` | Datenbank-Graph: Mermaid/JSON/Markdown aus DB oder Snapshot (FA-22) |
| `qt_hello/`, `qt_table/` | Qt/qmake-Einbindung und `QAbstractTableModel`-Adapter (FA-23) |

```sh
cmake --build build -j
./build/03_queries
ctest --test-dir build --output-on-failure   # Unit-Tests + alle Beispiele
```

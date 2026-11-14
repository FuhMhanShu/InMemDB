# InMemDB

In-Memory, non-relationales (dokumentenorientiertes) Datenbanksystem als
objektorientierte C++-Bibliothek. Dokumente sind schema-los: Felder sind
jederzeit pro Dokument beliebig erweiterbar.

- [Anforderungen](anforderungen.md) (v0.15)
- [Query-API](docs/query-api.md) (verbindliche Ausprägung)
- [String-DSL für Queries](docs/query-dsl.md) (NFA-03, v0.14: `find("age >= 18 && ...")`, eigene Operatoren, `@relation`-Joins)
- [Architektur-Entscheidungen](docs/architecture.md) (statische Lib mit Pimpl)
- [Indizes](docs/indexes.md) (FA-10, v0.2)
- [Transaktionen](docs/transactions.md) (FA-12, v0.3)
- [Snapshot/JSON](docs/snapshot-json.md) (FA-14, v0.4)
- [Aggregationen](docs/aggregations.md) (FA-13, v0.6)
- [Observer/Callbacks](docs/observer.md) (FA-15, v0.7)
- [Referenzen zwischen Collections/Dokumenten](docs/references.md) (FA-18, v0.8)
- [Typ-Mapping zu SQL/MongoDB/JSON](docs/type-mapping.md)
- [Import aus externen Quellen (z. B. SQLite)](docs/import.md) (FA-21, v0.13)
- [Datenbank-Graph (JSON/Mermaid/Markdown, LLM-freundlich)](docs/db-graph.md) (FA-22, v0.15)
- [Qt/qmake-Einbindung](docs/qt-qmake.md) (`InMemDB.pro` + `InMemDB.pri`, FA-23, v0.16)

## Features (v0.16)
- Schema-lose Dokumente mit dynamisch erweiterbaren Feldern
- Query-Ausdruckbaum: `(Field("age") >= 18) && Field("tags").contains("cpp")` – oder als String-DSL: `find("age >= 18 && tags.contains('cpp')")` mit registrierbaren Operatoren (`matches`, `in`, eigene) und `@relation.path`-Joins (v0.14)
- Sekundärindizes pro Feld inkl. Unique-Constraints und beschleunigter Abfragen
- Atomare Transaktionen pro Collection (commit/rollback, RAII-Rollback)
- JSON-Snapshot: `toJson`/`fromJson`/`saveToFile`/`loadFromFile` (ohne externe Abhängigkeiten)
- Upsert: `upsertById(id, doc)` / `upsert(query, doc)` (FA-07)
- Aggregationen: `aggregate(Min/Max/Sum/Avg/Count, feld, q)` und `groupBy(...)` (FA-13)
- Observer: `onChange`/`offChange`, Events nach Commit einer Transaktion (FA-15)
- Referenzen: `ReferenceIndex` verknüpft Collections/Dokumente name-basiert (relative Pfade), in beide Richtungen abfragbar, inkl. Traversierung (`traverse`/`hasCycle`/`search`, ringverweis-sicher), Dangling-Bereinigung (FA-18) und index-beschleunigtem Join bei Abfrage (`joinIds`/`join`, v0.17); in Queries als `@kunde.name == 'Ada'`
- Verschlüsselte binäre Snapshots: `saveToFileEncrypted`/`loadFromFileEncrypted` (PBKDF2 + ChaCha20 + HMAC-SHA256, FA-19)
- Striktes optionales Schema pro Collection mit Feldtypen als Metadaten (FA-20)
- Import-Modul: `ImportRegistry` (Factory, Auto-Erkennung per Dateiendung) + `Importer`; Quellen ohne Abhängigkeiten: CSV, JSON/JSONL, Snapshot-Reimport, SQL-Dump, BSON; optional SQLite via `INMEMDB_WITH_SQLITE=ON`; CLI-Tool `inmemdb_import` (FA-21)
- Datenbank-Graph als JSON/Mermaid/Markdown (Collections + Schema/Indizes + FA-18-Referenzen), auch aus Snapshot-Datei; CLI `inmemdb_graph` (FA-22)
- Qt/qmake-Einbindung via `InMemDB.pri` (Lib linken oder Quellen mitkompilieren) + `InMemDB.pro`; Beispiel `examples/qt_hello` (FA-23)
- Thread-Safety via `shared_mutex`

## Build & Test

Voraussetzungen: C++17-Compiler, CMake ≥ 3.20.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## API-Dokumentation (Doxygen, NFA-08)

Alle öffentlichen Klassen/Methoden in `include/memdb/` sind doxygen-kommentiert.

```sh
cmake -S . -B build -DINMEMDB_BUILD_DOCS=ON
cmake --build build --target docs   # HTML-Doku nach build/docs/html
```

In der CI läuft dafür der Job **Docs (doxygen)**.

## Nutzung

Alles in einem lauffähigen Beispiel – schema-lose Dokumente, Index, Query als
C++-Ausdruckbaum **oder** String-DSL, striktes Schema, Referenzen mit Join-in-Query,
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

    // 3) Queries: C++-Ausdruckbaum ODER String-DSL (NFA-03) – beides dieselbe Expression
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

Im Detail: [Query-API & String-DSL](docs/query-api.md) · [String-DSL/Operatoren/@ref-Joins](docs/query-dsl.md) ·
[Indizes](docs/indexes.md) · [Transaktionen](docs/transactions.md) · [Schema](docs/schema.md) ·
[Referenzen](docs/references.md) · [Observer](docs/observer.md) · [Aggregationen](docs/aggregations.md) ·
[Snapshot/JSON](docs/snapshot-json.md) · [Verschlüsselte Snapshots](docs/encrypted-snapshot.md) ·
[Import](docs/import.md) · [Datenbank-Graph](docs/db-graph.md) · [Qt/qmake](docs/qt-qmake.md) ·
[Typ-Mapping](docs/type-mapping.md) · [Architektur](docs/architecture.md)

## Beispiele

Lauffähige Beispiele in `examples/` (werden mitgebaut und als CTest-Smoke-Tests ausgeführt):

| Datei | Thema |
|---|---|
| `01_hello_database.cpp` | Database, Collection, insert, findById |
| `02_dynamic_fields.cpp` | Schema-lose Dokumente: Felder hinzufügen/ändern/umbenennen/löschen, Verschachtelung |
| `03_queries.cpp` | Query-Ausdruckbaum: Vergleiche, `&&`/`||`/`!`, `contains`, `exists`, Punktnotation |
| `04_crud.cpp` | insertBulk, update, updateById, replace, remove, count |
| `05_indexes.cpp` | Indizes anlegen/löschen, Unique-Constraints, beschleunigte Abfragen |
| `06_transactions.cpp` | Atomare Transaktion: commit, rollback, RAII-Rollback |
| `07_snapshot_json.cpp` | JSON-Snapshot speichern/laden (Datei + String) |
| `08_upsert.cpp` | Upsert per Query und per ID |
| `09_aggregations.cpp` | count/sum/avg/min/max und groupBy |
| `10_observer.cpp` | onChange/offChange, Events inkl. Transaktions-Commit |
| `11_references.cpp` | Referenzen: link/unlink, outgoing/incoming, relative Pfade, dangling/prune, traverse/hasCycle/search (Ringverweis), Snapshot-Roundtrip |
| `12_encrypted_snapshot.cpp` | DB binär verschlüsselt speichern/laden (FA-19) |
| `13_schema.cpp` | Striktes Schema: Feldtypen prüfen, undefinierte Felder ablehnen (FA-20) |
| `14_query_dsl.cpp` | String-DSL: `find("age >= 18 && ...")`, `matches`/`in`, eigener Operator, `@kunde.*`-Join (v0.14) |
| `15_db_graph.cpp` | Datenbank-Graph: Mermaid/JSON/Markdown aus DB oder Snapshot (FA-22) |

```sh
cmake --build build -j
./build/03_queries
ctest --test-dir build --output-on-failure   # Unit-Tests + alle Beispiele
```

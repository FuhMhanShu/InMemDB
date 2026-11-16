# Getting Started

Build, Tests, Einbindung in eigene Projekte, CLI-Tools und Beispiele.
(Der schnelle Einstieg mit Minimal-Beispiel steht im [README](../../README.de.md#quickstart).)

## Build & Test

Voraussetzungen: C++17-Compiler, CMake ≥ 3.20.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Benchmark: `./build/memdb_bench` (InMemDB solo) bzw. mit `-DINMEMDB_WITH_SQLITE=ON` zusätzlich
`./build/memdb_bench_sqlite` (Vergleich gegen SQLite `:memory:`) – gemessene Ergebnisse und
Best Practices in [internals/performance.md](internals/performance.md).

## Einbindung in eigene Projekte

Installieren (Library, Header, CMake-Paket und CLI-Tools) und per `find_package` konsumieren:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix /pfad/zur/installation
```

```cmake
find_package(InMemDB 1.0 REQUIRED)
target_link_libraries(mein_programm PRIVATE memdb::memdb)
```

Oder ohne Installation direkt per FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(InMemDB
    GIT_REPOSITORY https://github.com/FuhMhanShu/InMemDB.git
    GIT_TAG v1.0.0)
FetchContent_MakeAvailable(InMemDB)
target_link_libraries(mein_programm PRIVATE memdb::memdb)
```

Paketmanager: `vcpkg.json` (vcpkg-Manifest) und `conanfile.py` (Conan-Rezept) liegen im
Repo-Wurzelverzeichnis. Der Kern bleibt abhängigkeitsfrei; der optionale SQLite-Import
bleibt ein reines CMake-Flag (`INMEMDB_WITH_SQLITE=ON`).

Qt/qmake: siehe [guides/qt-qmake.md](guides/qt-qmake.md) (FA-23) – `InMemDB.pro`/`InMemDB.pri`
plus fertiger `QAbstractTableModel`-Adapter, ohne harte Qt-Abhängigkeit.

## CLI-Tools

Alle Tools liegen nach dem Build in `build/` (und werden mit `cmake --install` installiert):

| Tool | Zweck | Doku |
|---|---|---|
| `inmemdb_import` | Import aus CSV/JSON/JSONL/SQL-Dump/BSON/Snapshot/SQLite in eine InMemDB-Datei | [guides/import.md](guides/import.md) |
| `inmemdb_graph` | Datenbank-Graph als JSON/Mermaid/Markdown aus DB/Snapshot | [guides/db-graph.md](guides/db-graph.md) |
| `inmemdb_discover` | Relationship-Discovery & Auto-Analyse (Relationen, Orphans, Pfade, Diff, Merge-Vorschläge, Ausreißer, Health-Score-Report) | [guides/discovery.md](guides/discovery.md) · [guides/discovery-guide.md](guides/discovery-guide.md) |

```sh
./build/inmemdb_import kunden.csv --out shop.inmemdb.json
./build/inmemdb_graph shop.inmemdb.json --mermaid
./build/inmemdb_discover shop.inmemdb.json --report analyse   # -> analyse.report.md/.relations.json/.graph.mmd
```

## API-Dokumentation (Doxygen, NFA-08)

Alle öffentlichen Klassen und Methoden in `include/memdb/` sind Doxygen-kommentiert.
Das Root-[README](../../README.de.md) dient dabei als Einstiegsseite.

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

# InMemDB

In-Memory, non-relationales (dokumentenorientiertes) Datenbanksystem als
objektorientierte C++-Bibliothek. Dokumente sind schema-los: Felder sind
jederzeit pro Dokument beliebig erweiterbar.

- [Lastenheft](lastenheft.md) (v0.1)
- [Query-API](docs/query-api.md) (verbindliche Ausprägung)
- [Architektur-Entscheidungen](docs/architecture.md) (statische Lib mit Pimpl)
- [Indizes](docs/indexes.md) (FA-10, v0.2)
- [Transaktionen](docs/transactions.md) (FA-12, v0.3)
- [Snapshot/JSON](docs/snapshot-json.md) (FA-14, v0.4)
- [Aggregationen](docs/aggregations.md) (FA-13, v0.6)

## Features (v0.6)
- Schema-lose Dokumente mit dynamisch erweiterbaren Feldern
- Query-Ausdruckbaum: `(Field("age") >= 18) && Field("tags").contains("cpp")`
- Sekundärindizes pro Feld inkl. Unique-Constraints und beschleunigter Abfragen
- Atomare Transaktionen pro Collection (commit/rollback, RAII-Rollback)
- JSON-Snapshot: `toJson`/`fromJson`/`saveToFile`/`loadFromFile` (ohne externe Abhängigkeiten)
- Upsert: `upsertById(id, doc)` / `upsert(query, doc)` (FA-07)
- Aggregationen: `aggregate(Min/Max/Sum/Avg/Count, feld, q)` und `groupBy(...)` (FA-13)
- Thread-Safety via `shared_mutex`

## Build & Test

Voraussetzungen: C++17-Compiler, CMake ≥ 3.20.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Nutzung

```cpp
#include <memdb/memdb.hpp>
using namespace memdb;

Database db("meine-db");
Collection users = db.createCollection("users");

DocumentId id = users.insert({{"name", "Ada"}, {"age", 36}});

auto q = (Field("age") >= 18) && Field("name").contains("d");
for (const Document& d : users.find(q))
    ...
```

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

```sh
cmake --build build -j
./build/03_queries
ctest --test-dir build --output-on-failure   # Unit-Tests + alle Beispiele
```

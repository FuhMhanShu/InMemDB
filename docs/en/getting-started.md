# Getting Started

Build, tests, embedding into your own projects, CLI tools and examples.
(For the minimal quickstart see the [README](../../README.md#quickstart).)

## Build & Test

Requirements: C++17 compiler, CMake ≥ 3.20.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Benchmark: `./build/memdb_bench` (InMemDB solo) or, with `-DINMEMDB_WITH_SQLITE=ON`,
also `./build/memdb_bench_sqlite` (comparison against SQLite `:memory:`) – measured results
and best practices in [internals/performance.md](internals/performance.md).

## Embedding into your own projects

Install (library, headers, CMake package and CLI tools) and consume via `find_package`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix /path/to/installation
```

```cmake
find_package(InMemDB 1.0 REQUIRED)
target_link_libraries(my_program PRIVATE memdb::memdb)
```

Or without installation, directly via FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(InMemDB
    GIT_REPOSITORY https://github.com/FuhMhanShu/InMemDB.git
    GIT_TAG v1.0.0)
FetchContent_MakeAvailable(InMemDB)
target_link_libraries(my_program PRIVATE memdb::memdb)
```

Package managers: `vcpkg.json` (vcpkg manifest) and `conanfile.py` (Conan recipe) live in the
repo root. The core stays dependency-free; the optional SQLite import remains a pure CMake
flag (`INMEMDB_WITH_SQLITE=ON`).

Qt/qmake: see [guides/qt-qmake.md](guides/qt-qmake.md) (FA-23) – `InMemDB.pro`/`InMemDB.pri`
plus a ready-made `QAbstractTableModel` adapter, without a hard Qt dependency.

## CLI tools

All tools are built into `build/` (and installed via `cmake --install`):

| Tool | Purpose | Docs |
|---|---|---|
| `inmemdb_import` | Import from CSV/JSON/JSONL/SQL dump/BSON/snapshot/SQLite into an InMemDB file | [guides/import.md](guides/import.md) |
| `inmemdb_graph` | Database graph as JSON/Mermaid/Markdown from a DB/snapshot | [guides/db-graph.md](guides/db-graph.md) |
| `inmemdb_discover` | Relationship discovery & auto-analysis (relations, orphans, paths, diff, merge suggestions, outliers, health-score report) | [guides/discovery.md](guides/discovery.md) · [guides/discovery-guide.md](guides/discovery-guide.md) |

```sh
./build/inmemdb_import customers.csv --out shop.inmemdb.json
./build/inmemdb_graph shop.inmemdb.json --mermaid
./build/inmemdb_discover shop.inmemdb.json --report analyse   # -> analyse.report.md/.relations.json/.graph.mmd
```

## API documentation (Doxygen, NFA-08)

All public classes and methods in `include/memdb/` are Doxygen-commented.
The root [README](../../README.md) serves as the entry page.

```sh
cmake -S . -B build -DINMEMDB_BUILD_DOCS=ON
cmake --build build --target docs   # HTML docs in build/docs/html
```

CI runs a **Docs (doxygen)** job for this.

## Full example

A single runnable program showing the most important areas – schema-less documents, index,
queries in both forms, references with join-in-query, transaction, observer, aggregation,
encrypted snapshot, import, graph export:

```cpp
#include <memdb/memdb.hpp>
#include <iostream>
using namespace memdb;

int main() {
    Database db("shop");

    // 1) Schema-less documents: fields freely extensible per document
    Collection users = db.createCollection("users");
    DocumentId ada = users.insert({{"name", "Ada"}, {"age", 36}, {"city", "Berlin"},
                                   {"tags", std::vector<Value>{Value("cpp"), Value("db")}}});
    users.insert({{"name", "Bob"}, {"age", 17}, {"city", "Hamburg"}});  // different field set: ok
    users.insert({{"name", "Cy"}, {"age", 40}, {"city", "Berlin"}, {"vip", true}});

    // 2) Index + unique constraint (FA-10)
    users.createIndex("name", true);

    // 3) Queries: C++ expression tree OR string DSL – both the same Expression
    auto q = (Field("age") >= 18) && (Field("city") == "Berlin");
    for (const Document& d : users.find(q))
        std::cout << d.at("name").asString() << "\n";          // Ada, Cy
    std::cout << users.count("age >= 18 && city == 'Berlin'") << "\n";   // DSL
    std::cout << users.count("tags.contains('cpp')") << "\n";             // function operator
    std::cout << users.count("name.matches('^[A-C].*')") << "\n";         // mini regex

    // 4) References (FA-18) + join directly in the query: '@customer.name'
    Collection orders = db.createCollection("orders");
    DocumentId o1 = orders.insert({{"total", 500}});
    ReferenceIndex refs(db);
    refs.link("orders", o1, "users", ada, "customer");
    std::cout << orders.count("@customer.name == 'Ada' && total >= 100", db) << "\n";  // 1

    // 5) Transaction (FA-12): all or nothing
    {
        auto tx = users.beginTransaction();
        tx.update("age < 18", [](Document& d) { d.set("minor", true); });
        tx.commit();   // otherwise rollback() or RAII rollback
    }

    // 6) Optional strict schema (FA-20): from now on no undefined fields
    Schema sch;
    sch.add("name", Value::Type::String).notNull();
    sch.add("age", Value::Type::Int);
    users.setSchema(std::move(sch));

    // 7) Observer (FA-15) + aggregation (FA-13)
    auto h = users.onChange([](const ChangeEvent& e) { std::cout << "change: " << (int)e.type << "\n"; });
    std::cout << users.aggregate(AggOp::Avg, "age").value().asDouble() << "\n";
    users.offChange(h);

    // 8) Snapshot: JSON (FA-14) or binary encrypted (FA-19)
    db.saveToFile("shop.inmemdb.json");
    db.saveToFileEncrypted("shop.enc", "secret");
    Database copy = Database::loadFromFileEncrypted("shop.enc", "secret");

    // 9) Import external sources (FA-21) + database graph as Mermaid/JSON (FA-22)
    Importer::import(db, "csv", "customers.csv");                // or CLI: inmemdb_import customers.csv
    std::cout << DbGraph::fromDatabase(db).toMermaid() << "\n"; // renders directly as a diagram
}
```

## Examples

Runnable examples in `examples/` (built along and run as CTest smoke tests):

| File | Topic |
|---|---|
| `01_hello_database.cpp` | Database, Collection, insert, read by ID |
| `02_dynamic_fields.cpp` | add/change/rename/delete fields, nesting |
| `03_queries.cpp` | expression tree: comparisons, `&&`/`||`/`!`, `contains`, `exists`, dot notation |
| `04_crud.cpp` | insertBulk, update, updateById, replace, remove, count |
| `05_indexes.cpp` | create/delete indexes, unique constraints, accelerated queries |
| `06_transactions.cpp` | atomic transaction: commit, rollback, RAII rollback |
| `07_snapshot_json.cpp` | save/load JSON snapshot (file + string) |
| `08_upsert.cpp` | upsert by query and by ID |
| `09_aggregations.cpp` | count/sum/avg/min/max/distinct and groupBy |
| `10_observer.cpp` | onChange/offChange, events incl. transaction commit |
| `11_references.cpp` | references: link/unlink, outgoing/incoming, relative paths, dangling/prune, traverse/hasCycle/search (cycle), snapshot roundtrip |
| `12_encrypted_snapshot.cpp` | save/load DB encrypted (FA-19) |
| `13_schema.cpp` | strict schema: check field types, reject undefined fields (FA-20) |
| `14_query_dsl.cpp` | string DSL: `find("age >= 18 && ...")`, `matches`/`in`, custom operator, `@customer.*` join (v0.14) |
| `15_db_graph.cpp` | database graph: Mermaid/JSON/Markdown from DB or snapshot (FA-22) |
| `qt_hello/`, `qt_table/` | Qt/qmake integration and `QAbstractTableModel` adapter (FA-23) |

```sh
cmake --build build -j
./build/03_queries
ctest --test-dir build --output-on-failure   # unit tests + all examples
```

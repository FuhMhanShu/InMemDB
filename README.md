# InMemDB

In-Memory, non-relationales (dokumentenorientiertes) Datenbanksystem als
objektorientierte C++-Bibliothek. Dokumente sind schema-los: Felder sind
jederzeit pro Dokument beliebig erweiterbar.

- [Lastenheft](lastenheft.md) (v0.1)
- [Query-API](docs/query-api.md) (verbindliche Ausprägung)
- [Architektur-Entscheidungen](docs/architecture.md) (statische Lib mit Pimpl)

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

# Performance: messen & Best Practices

## Messen

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/memdb_bench                 # InMemDB solo (insert/index/queries/tx/snapshot)

# Vergleich gegen SQLite (:memory:) – braucht libsqlite3 + Header:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DINMEMDB_WITH_SQLITE=ON
cmake --build build -j
./build/memdb_bench_sqlite          # InMemDB vs SQLite, gleiche Workloads, N=100k
```

Gemessene Kennzahlen landen außerdem in `Collection::metrics()` / `Database::metrics()`
(Index-Nutzung vs. Vollscan, Query-Dauer, Kandidatenzahl via `lastQueryCandidateCount()`).

## Gemessene Ergebnisse (N=100.000, min aus 3 Laeufen, g++ 14.2 -O2, 4-Kern-Sandbox)

| Workload | InMemDB | SQLite 3.46 `:memory:` (Bulk-Tx) |
|---|---|---|
| Insert 100k (InMemDB: 4 schemalose Felder; SQLite: 5 Spalten) | 105 ms | 47 ms |
| Index-Aufbau `age` | 25 ms | 28 ms |
| `count(age==42)` mit Index | 0,5 ms | <0,1 ms |
| `count(city='Berlin')` Vollscan | 12 ms | 3,0 ms |
| Bereich `age>=90` (Index) | 2,0 ms | 0,1 ms |
| Punkt-Lookup per ID | <0,1 ms | <0,1 ms |
| `AVG(age)` Vollscan | 8,4 ms | 2,5 ms |
| Update 5k Zeilen | 13,7 ms | 2,6 ms |
| JSON-Export (~6,7 MB) | 56 ms | 26 ms |

Einordnung: SQLite gewinnt scan-lastige Workloads (kompakte Zeilen im B-Baum vs. Dokument-Maps),
InMemDB liegt beim Insert in derselben Groessenordnung und beim Index-Aufbau vorn. Die Staerken von
InMemDB liegen im Modell (schemalos, DSL/C++-Queries, Referenzen/Joins, CoW-Transaktionen mit O(1)-Commit,
verschluesselte Snapshots) – siehe README.

## Best Practices (Nutzung, ohne Code-Aenderung)

- **Indizes auf Hot-Felder** legen (`createIndex`) – `metrics()` bzw. `lastQueryCandidateCount()`
  zeigen, wann eine Query zum Vollscan wird (Kandidaten == Collection-Groesse).
- **`findIds`/`forEach` statt `find`** bei grossen Ergebnismengen: `find` kopiert jedes Treffer-Dokument
  in den Cursor, `findIds`/`forEach` arbeiten kopierfrei.
- **Bulk-Operationen** nutzen: `insertBulk`, `fromJson`/`loadFromFile` (Bulk-Load).
- **DSL-Queries in Schleifen** sind gecacht (Parse-Cache, max. 128 Eintraege, thread-safe;
  invalidiert bei (Un)Register eigener Operatoren).
- **Transaktionen bündeln**: Commit ist dank Copy-on-Write-Undo O(1), auch bei Millionen Dokumenten.
- **Sweeps meiden im Hotpath**: `setOverflow` (Spill) und TTL-Sweeps sind bewusst opt-in/aus.

## Interne Hotpath-Optimierungen (Stand nach den Perf-PRs)

- Dokumente/Felder/Spill-Index als Hash-Maps (`unordered_map`) statt Baeume; `findById` Ø-O(1).
- Query-Evaluierung (`count`/`findIds`/`findOne`/`forEach`/`remove`/`aggregate`/`groupBy`/`distinctValues`)
  arbeitet kopierfrei per `const Document*` auf RAM-Dokumenten.
- `reserve()` beim Bulk-Load; deterministische Snapshot-Reihenfolge (IDs sortiert) trotz Hash-Map.
- DSL-Parse-Cache mit Registry-Version (siehe oben).
- FIFO-Eviction im Hybrid-Modus evictet weiterhin niedrigste IDs zuerst.

Parallele Scans wurden gemessen und **nicht** uebernommen: auf der 4-Kern-Testmaschine waren sie
langsamer als serial (Thread-Overhead/Contention) – `count`/`aggregate` bleiben daher serial.

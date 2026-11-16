# Performance: measure & best practices

## Measuring

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/memdb_bench                 # InMemDB solo (insert/index/queries/tx/snapshot)

# comparison against SQLite (:memory:) – needs libsqlite3 + headers:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DINMEMDB_WITH_SQLITE=ON
cmake --build build -j
./build/memdb_bench_sqlite          # InMemDB vs SQLite, same workloads, N=100k
```

Measured KPIs also land in `Collection::metrics()` / `Database::metrics()`
(index usage vs. full scan, query duration, candidate count via `lastQueryCandidateCount()`).

## Measured results (N=100,000, min of 3 runs, g++ 14.2 -O2, 4-core sandbox)

| Workload | InMemDB | SQLite 3.46 `:memory:` (bulk tx) |
|---|---|---|
| Insert 100k (InMemDB: 4 schema-less fields; SQLite: 5 columns) | 105 ms | 47 ms |
| Index build `age` | 25 ms | 28 ms |
| `count(age==42)` with index | 0.5 ms | <0.1 ms |
| `count(city='Berlin')` full scan | 12 ms | 3.0 ms |
| Range `age>=90` (index) | 2.0 ms | 0.1 ms |
| Point lookup by ID | <0.1 ms | <0.1 ms |
| `AVG(age)` full scan | 8.4 ms | 2.5 ms |
| Update 5k rows | 13.7 ms | 2.6 ms |
| JSON export (~6.7 MB) | 56 ms | 26 ms |

Context: SQLite wins scan-heavy workloads (compact rows in a B-tree vs. document maps),
InMemDB is in the same order of magnitude on insert and ahead on index build. InMemDB's
strengths lie in the model (schema-less, DSL/C++ queries, references/joins, CoW transactions
with O(1) commit, encrypted snapshots) – see README.

## Best practices (usage, no code change)

- **Put indexes on hot fields** (`createIndex`) – `metrics()` / `lastQueryCandidateCount()`
  show when a query degrades to a full scan (candidates == collection size).
- **`findIds`/`forEach` instead of `find`** for large result sets: `find` copies every matched
  document into the cursor, `findIds`/`forEach` work copy-free.
- **Use bulk operations**: `insertBulk`, `fromJson`/`loadFromFile` (bulk load).
- **DSL queries in loops** are cached (parse cache, max 128 entries, thread-safe;
  invalidated on (un)registering custom operators).
- **Bundle transactions**: commit is O(1) thanks to copy-on-write undo, even with millions of documents.
- **Avoid sweeps in the hot path**: `setOverflow` (spill) and TTL sweeps are deliberately opt-in/off.

## Internal hotpath optimizations (state after the perf PRs)

- Documents/fields/spill index as hash maps (`unordered_map`) instead of trees; `findById` O(1).
- Query evaluation (`count`/`findIds`/`findOne`/`forEach`/`remove`/`aggregate`/`groupBy`/`distinctValues`)
  works copy-free via `const Document*` on RAM documents.
- `reserve()` on bulk load; deterministic snapshot order (IDs sorted) despite hash map.
- DSL parse cache with registry version (see above).
- FIFO eviction in hybrid mode still evicts lowest IDs first.

Parallel scans were measured and **not** adopted: on the 4-core test machine they were
slower than serial (thread overhead/contention) – `count`/`aggregate` therefore stay serial.

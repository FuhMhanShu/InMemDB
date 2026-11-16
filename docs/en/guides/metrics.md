# Metrics & diagnostics (v0.22)

| Field | Value |
|---|---|
| Status | implemented (v0.22) |
| Reference | `include/memdb/metrics.hpp`, `Collection::metrics()` / `resetMetrics()`, `Database::metrics()` |

InMemDB keeps per-collection diagnostic counters in the central `detail::CollectionState`
(`src/collection_state.hpp`) – so `Collection` and `Transaction` accesses are counted
automatically, without duplicated logic. All accesses happen under the collection lock,
the counters are `std::atomic`.

## Usage

```cpp
Collection c = db.createCollection("users");
c.createIndex("age");
// ... operation ...
CollectionMetrics m = c.metrics();
// m.inserts, m.updates, m.removes, m.queries, m.indexQueries, m.scannedDocs,
// m.changeEvents, m.spillReads, m.spillWrites, m.txCommits, m.txRollbacks,
// m.docs, m.indexCount, m.lastCandidateCount, m.lastQueryUsedIndex,
// m.lastQueryMs, m.totalQueryMs
c.resetMetrics();                    // reset counters (current values stay)

DatabaseMetrics dm = db.metrics();   // aggregate over all collections
dm.collectionCount; dm.totalDocs; dm.sum.inserts; // ...
CollectionMetrics one = db.metrics("users");
```

## Fields (`CollectionMetrics`)

| Field | Meaning |
|---|---|
| `inserts` / `updates` / `removes` | write accesses (number of affected documents; `insertBulk` counts per doc; rolled-back tx operations also count as ops) |
| `queries` | read accesses: `find`/`findOne`/`findIds`/`forEach`/`count`/`aggregate`/`groupBy` (write scans of `update`/`remove`/`upsert` do **not** count as queries) |
| `indexQueries` | of those, queries that used an index |
| `scannedDocs` | sum of candidates of all queries – measures the full-scan share (`scannedDocs / (queries * docs)` near 1 = mostly full scans) |
| `lastCandidateCount`, `lastQueryUsedIndex`, `lastQueryMs`, `totalQueryMs` | last query: candidate count, index usage, duration (ms), accumulated duration |
| `changeEvents` | emitted change events (observer + tx events; on rollback the events are discarded, the counter stays) |
| `spillReads` / `spillWrites` | hybrid mode (v0.18): records read from / written to the spill file |
| `txCommits` / `txRollbacks` | completed / rolled-back transactions (explicit rollback and RAII destructor) |
| `expiredDocs` | TTL/auto-expiry (FA-25, v0.26): expired and removed documents |
| `evalErrors` | v0.35: eval errors caught per document in the query scan (document counted as non-match, see [error-handling.md](../internals/error-handling.md)) |
| `eventErrors` | v0.35: caught observer exceptions (the write op continued, see [error-handling.md](../internals/error-handling.md)) |
| `docs` / `indexCount` | current values: current document count (RAM + spill), number of indexes |

## Typical diagnostics

- **Full scan despite index?** `indexQueries == 0` with many queries → index missing or the
  query uses no index-capable comparison (`indexHint`).
- **Slow queries?** `totalQueryMs / queries` or `lastQueryMs`; `lastCandidateCount` shows
  how many docs were checked.
- **Lots of spill IO?** high `spillReads`/`spillWrites` → raise `maxInMemory` (v0.18) or run
  `compactSpill()`.
- **Many rollbacks?** `txRollbacks` relative to `txCommits` → check transaction granularity.

## Implementation

- Counters: `detail::Metrics` in `src/collection_state.hpp` (member `CollectionState::metrics`),
  increments directly in the `do*` methods and `candidates(q, metered)` (timing via
  `std::chrono::steady_clock`).
- Snapshot: `Collection::metrics()` (shared_lock, copies the atomics), `resetMetrics()`.
- Aggregation: `Database::metrics()` sums `Collection::metrics()` of all collections.
- Tests: `tests/test_metrics.cpp` (34 checks).

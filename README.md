# InMemDB

> 🇩🇪 **Deutsche Version:** [README.de.md](README.de.md) · Deutsche Doku: [docs/de/](docs/de/README.md)

**InMemDB is a database that runs entirely in memory (RAM) and is embedded directly into
C++ programs** – as an object-oriented C++17 library, with no server of its own, no network,
and no mandatory external dependencies (shipped as a static library, internally decoupled
via the Pimpl idiom). MIT license.

Instead of tables there are **collections** of **documents**. A document is a JSON-like
object of fields – and because InMemDB is **schema-less**, every document may have arbitrary,
changing fields (e.g. `{"name": "Ada", "age": 36}` and `{"name": "Bob", "vip": true}` in the
same collection).

Queries come in two forms that compile to the same query: a **type-safe C++ expression**
(`Field("age") >= 18 && Field("city") == "Berlin"`) or a **string** (`"age >= 18 && city == 'Berlin'"`).
You can put **indexes** on any field so queries don't have to scan everything.

> **Important:** "in-memory" means: when the process exits, the data is gone.
> You can save at any time as a JSON snapshot (`saveToFile`) or encrypted
> (`saveToFileEncrypted(path, password)`).

> **Current release: v1.0.0** (2026-11-16, git tag `v1.0.0`) – first official release,
> SemVer from now on (stable API). History: [CHANGELOG.md](CHANGELOG.md).

## Quickstart

```cpp
#include <memdb/memdb.hpp>
memdb::Database db("shop");
auto products = db.createCollection("products");
products.createIndex("name", true);                              // unique index on "name"
products.insert({{"name", "Book"}, {"price", 10.5}, {"tags", {"new"}}});
for (const auto& p : products.find("price < 20 && tags.contains('new')"))
    std::cout << p.at("name").asString() << '\n';                // DSL query -> "Book"
db.saveToFileEncrypted("shop.enc", "secret");                    // encrypted snapshot
```

## What can InMemDB do? (Features, v1.0)

**Data & queries**
- Schema-less documents with dynamically extensible fields (also nested, dot notation `"address.zip"`)
- Queries as a C++ expression tree or string DSL, incl. custom operators (`matches` for a mini regex – Thompson NFA, hardened in v0.34 with hard limits against stack overflow/hangs, compiled once per query; `in`, custom ones via registry) and `@relation` joins (v0.14)
- Secondary indexes per field, incl. unique constraints (e.g. unique email) – accelerated equality and range queries (FA-10)
- Aggregations: `count`, `min`, `max`, `sum`, `avg`, `distinct` plus `groupBy` (FA-13)
- Foundations (v0.27): `memdb::ValueLess` (Value as map/set key), `distinctValues(field, q)` (index-accelerated), `sample(maxN, seed)` (reproducible random sample), `AggOp::Distinct`, `Expression::isMatchAll()`
- Bulk operations (`insertBulk`) and bulk update via DSL with projection: `update("age >= 18", "adult = true, age = age + 1")` (v0.21)
- Upsert: insert or update, by ID or by query (FA-07)

**Structure & relationships**
- **References** between collections/documents (`ReferenceIndex`): queryable in both directions, traversal incl. cycle detection (`traverse`/`hasCycle`/`search`), dangling detection and cleanup (FA-18); index-accelerated join in queries (`joinIds`/`join`, v0.17); directly in queries as `@customer.name == 'Ada'`
- **Merge:** combine several collections into one – `Database::mergeCollections` or `mergeAllCollections` for the whole database, with new IDs, `_source` provenance field and optional deletion of the sources incl. reference rewriting (FA-24, v0.24/v0.25)
- **Database graph** as JSON, Mermaid or Markdown – also from snapshot files; CLI tool `inmemdb_graph` (FA-22)

**Security, persistence & operations**
- Atomic **transactions** per collection: all or nothing, commit/rollback, rollback via copy-on-write (fast even with millions of documents, v0.19); RAII rollback (FA-12)
- **JSON snapshot** of the whole DB or single collections: `toJson`/`fromJson`/`saveToFile`/`loadFromFile` (FA-14); own parser hardened in v0.37 (nesting limit 1000 against segfaults, `$binary` only for valid hex, hex-float fallback only with `0x`)
- **Encrypted snapshots**: `saveToFileEncrypted`/`loadFromFileEncrypted` with a password (PBKDF2 + ChaCha20 + HMAC-SHA256, no external crypto library, FA-19)
- Optional **strict schema** per collection: field types as metadata, undefined fields are rejected (FA-20)
- **Observer**: callbacks on every insert/update/delete (`onChange`), in transactions only on commit (FA-15)
- **TTL / auto-expiry** (switchable, off by default): documents with an expiry time (`expiresIn(sec)` / `expiresAt(unixSeconds)`) are removed automatically (FA-25, v0.26)
- **Relationship discovery** (FA-27, v0.29): `discoverForeignKeys` / `discoverFunctionalDependencies` / `discoverCorrelations` / `discoverNearDuplicates` / `discoverRelations` – across collections and databases, without dependencies; `applyDiscovered` creates FA-18 references; cardinality (1:1/n:1/1:n/n:m), orphan report, path search (v0.30), data profiling + candidate keys (v0.31), cross-DB diff + merge suggestions (v0.32), **automatic full analysis + report** `analyze()` with health score (traffic light 🟢🟡🔴) and presentation as Markdown/JSON/Mermaid/console (`toMarkdown`/`toJson`/`toMermaid`/`printSummary`, v0.33); CLI `inmemdb_discover` with JSON/Mermaid report (`--orphans`, `--path`) and full report (`--report <prefix>`); **outlier detection** `discoverOutliers` (z-score + IQR per numeric field, as a health-score factor, v0.40), CLI `--outliers`; **confusion matrix** `evaluateConfusion`/`AnalysisReport::confusion` (discovered FKs vs. declared `__references`, v0.42), CLI `--confusion`
- **Data analysis pipeline** (FA-29…FA-34): `assessCharacter` classifies each collection roughly as numeric/mixed/complex and dispatches to the matching pipeline – `analyzeNumeric` (statistics, quantiles, rounding grids, sequences, Benford, correlations), `analyzeInfluence` (dominant fields via correlation hub, multiple R² and functional dependencies, roles, `topDriver`), `analyzeCrossInfluence` (dependencies between fields of different collections, joined via declared references and discovered foreign keys) and `analyzeMixed` (dominant types, text classes, format hints, Pearson/eta²/Cramér's V associations) and `analyzeComplex` (recursive path profile for nested documents, arrays and type drift, schema variants with consistency); reports as JSON/Markdown ([character](docs/en/guides/character.md) · [numeric analysis](docs/en/guides/numeric-analysis.md) · [influence](docs/en/guides/influence.md) · [cross-collection influence](docs/en/guides/cross-influence.md) · [mixed analysis](docs/en/guides/mixed-analysis.md) · [complex analysis](docs/en/guides/complex-analysis.md))
- **Hybrid / out-of-core**: when RAM runs low, `setOverflow(spillFile, maxInMemory)` spills oldest documents to a file and reads them on demand (v0.18)
- **Metrics & diagnostics** (v0.22): `Collection::metrics()`/`Database::metrics()` – accesses, index usage vs. full scan, query duration, spill I/O, transactions, events, TTL expiry, `evalErrors`/`eventErrors` (v0.35). **Exception policy** (v0.35, [docs/en/internals/error-handling.md](docs/en/internals/error-handling.md)): query scans and observers catch per element (counters in metrics), bulk load validates before mutating (atomic), CLI exits cleanly with an exit code; real errors (unique/schema/crypto/broken file) still throw. **MessageHandler singleton** (v0.36): register your own handlers via `MessageHandler::instance().subscribe(...)` – the catch points report there (`query-eval`/`observer`/`load`/`import`/`regex`/`cli`), thread-safe, level filter + counters. **Hardening** ([docs/en/internals/hardening.md](docs/en/internals/hardening.md)): hard limits in all parsers (regex/JSON/DSL/BSON – no more segfaults/hangs on pathological input), strict numbers, NaN ordering (v0.43: NaN > all numbers, Postgres-style), exception policy + message handler, **optional collection limits** (`setLimits`: max docs/bytes/fields/indexes → `LimitError` + metric, [docs/en/guides/limits.md](docs/en/guides/limits.md))
- **Import** from CSV, JSON/JSONL, SQL dump, BSON, own snapshots and (optional, `INMEMDB_WITH_SQLITE=ON`) SQLite files; CLI tool `inmemdb_import` (FA-21); **foreign keys are imported as FA-18 references** (SQLite `PRAGMA foreign_key_list`, SQL dump `REFERENCES`/`FOREIGN KEY`, can be disabled via `--no-foreign-keys`, FA-26, v0.28)
- Thread safety via `shared_mutex` (parallel reads, serialized writes); numbers are locale-independent (always a decimal point, even with `de_DE`, bugfix v0.23.1)
- **Qt/qmake integration** via `InMemDB.pro`/`InMemDB.pri` plus a ready-made `QAbstractTableModel` adapter (`memdb::qt::MemDbTableModel`, QtCore only, no hard Qt dependency, FA-23, v0.23)

## Comparison with SQLite, DuckDB and UnQLite

InMemDB is not a replacement for the established embedded databases; it targets a different
niche: a **schema-less document working set in RAM**, native to C++17, with built-in data
analysis. The following table highlights the differences (based on the public
documentation of the named projects, November 2026):

| | **InMemDB** | **SQLite** | **DuckDB** | **UnQLite** |
|---|---|---|---|---|
| Positioning | Embedded working database for C++ applications: data lives in RAM as native objects | Embedded OLTP database, the standard for relational data on the device | Embedded OLAP engine for analytical SQL over local data | Embedded NoSQL engine for C (key/value and documents) |
| Architecture & storage | Entirely in RAM; optional spill of the oldest documents to a file (hybrid, v0.18) | B-tree file on disk, optional `:memory:` mode | Columnar file, vectorized execution, processes data larger than RAM | Single file or in-memory key/value plus document store |
| Data model | Schema-less JSON-like documents, nested fields, arrays, type drift allowed | Relational tables with a fixed schema and strong SQL typing | Relational (columnar) tables with a fixed schema | Key/value plus JSON documents (accessed via the Jx9 scripting language) |
| Query language | Type-safe C++ expression tree or string DSL, no SQL required | SQL, very complete (window functions, CTEs, …) | SQL, geared toward analytics (window functions, …) | Jx9 scripts, no SQL |
| Indexes | Secondary and unique indexes per field (equality and range) | B-tree indexes, partial and expression indexes | Automatic (zonemaps/ART), few manual index choices | No secondary indexes; cursors for linear traversal |
| Transactions | Atomic per collection, commit/rollback, rollback via copy-on-write (fast with millions of documents) | Full ACID, multi-statement, WAL | ACID, geared toward analytical loads (single writer) | ACID (key/value and documents) |
| Relationships & joins | References between collections, traversal with cycle detection, `@relation` joins, automatic FK discovery (FA-27) | FK constraints and SQL JOINs | SQL JOINs, including external files (CSV, Parquet, …) | None |
| Persistence | JSON snapshots, encrypted snapshots (PBKDF2 + ChaCha20 + HMAC-SHA256), optional spill | One DB file, extremely battle-tested format (stability guaranteed until 2050) | Single file (stable since 1.0, 2024) | Single file |
| Data analysis & discovery | Built in: FK/FD discovery, correlations, outliers, health score, numeric/mixed/complex analysis pipelines (FA-27…FA-34) | None (external tools) | None built in; analysis via hand-written SQL | None |
| Concurrency | `shared_mutex`: parallel reads, serialized writes | Threading modes (serialized), one writer per file | Single process, parallel queries, one writer | Thread modes, one writer |
| API & language | Native, object-oriented C++17 (Pimpl, no external dependencies) | C API (C++ only via wrappers) | C++/C API | C API |
| License | MIT | Public Domain | MIT | Symisc Public License (BSD-style) |
| Typical strength | A structured, fast-to-query working set in RAM plus bundled profiling/analysis features, no infrastructure | Reliable relational OLTP storage for any application and language | Fast aggregations and reports over medium to large local data sets | Very lightweight embedded persistence for C programs (KV or Jx9) |

In short: for durable relational data on disk, **SQLite** remains the choice; for analytical
SQL aggregation over large data sets, pick **DuckDB**; for minimal NoSQL persistence in C,
use **UnQLite**. **InMemDB** fills the gap in between: the RAM data set of a C++
application itself, with queries, indexes, transactions and an analysis pipeline that
profiles the data set without SQL. An existing SQLite database can be imported via
`INMEMDB_WITH_SQLITE=ON` ([import guide](docs/en/guides/import.md)).

## Documentation

The full documentation lives in **[docs/](docs/README.md)** (language picker):

- [Getting Started (English)](docs/en/getting-started.md) · [Getting Started (Deutsch)](docs/de/getting-started.md) – build & tests, embedding into your own projects (CMake/find_package/FetchContent/vcpkg/Conan/qmake), CLI tools (`inmemdb_import`, `inmemdb_graph`, `inmemdb_discover`), runnable example, examples overview
- [Guides](docs/en/README.md) – one document per feature: [Query API](docs/en/guides/query-api.md) · [String DSL](docs/en/guides/query-dsl.md) · [Indexes](docs/en/guides/indexes.md) · [Transactions](docs/en/guides/transactions.md) · [Snapshot/JSON](docs/en/guides/snapshot-json.md) · [Encrypted snapshots](docs/en/guides/encrypted-snapshot.md) · [Aggregations](docs/en/guides/aggregations.md) · [Observer](docs/en/guides/observer.md) · [Schema](docs/en/guides/schema.md) · [References & joins](docs/en/guides/references.md) · [Merging collections](docs/en/guides/merge-collections.md) · [TTL/auto-expiry](docs/en/guides/ttl-expiry.md) · [Import](docs/en/guides/import.md) · [Database graph](docs/en/guides/db-graph.md) · [Discovery (API)](docs/en/guides/discovery.md) · [Discovery guide (explained, with graphs)](docs/en/guides/discovery-guide.md) · [Character](docs/en/guides/character.md) · [Numeric analysis](docs/en/guides/numeric-analysis.md) · [Influence](docs/en/guides/influence.md) · [Cross-collection influence](docs/en/guides/cross-influence.md) · [Mixed analysis](docs/en/guides/mixed-analysis.md) · [Complex analysis](docs/en/guides/complex-analysis.md) · [Hybrid/out-of-core](docs/en/guides/hybrid-outofcore.md) · [Metrics](docs/en/guides/metrics.md) · [Collection limits](docs/en/guides/limits.md) · [Qt/qmake](docs/en/guides/qt-qmake.md) · [Type mapping](docs/en/guides/type-mapping.md)
- [Internals](docs/en/README.md) – [architecture/ADRs](docs/en/internals/architecture.md) · [Error handling](docs/en/internals/error-handling.md) · [Hardening](docs/en/internals/hardening.md) · [Performance](docs/en/internals/performance.md)
- [Requirements / specification (German)](anforderungen.md) (FA-01…FA-34, NFA-01…NFA-09) · [Changelog (German)](CHANGELOG.md) · API reference via Doxygen (see [Getting Started](docs/en/getting-started.md#api-documentation-doxygen-nfa-08))

## Build & Test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Details (benchmark, installation, `find_package`/FetchContent/vcpkg/Conan, examples):
[Getting Started](docs/en/getting-started.md).

## License

[MIT License](LICENSE) – Copyright (c) 2026 FuhMhanShu.

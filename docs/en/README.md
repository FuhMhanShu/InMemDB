# Documentation

> 🇩🇪 **Deutsche Version:** [docs/de/README.md](../de/README.md)

Welcome to the InMemDB documentation. This page is the **index**: it links to all
documents, grouped by purpose.

| | |
|---|---|
| Project | [README](../../README.md) (pitch, quickstart, features) · [Requirements / specification](../../anforderungen.md) (German; FA-01…FA-28, NFA-01…NFA-09) · [Changelog](../../CHANGELOG.md) (German) · [License](../../LICENSE) (MIT) |
| Getting started | [Getting Started](getting-started.md) – build & tests, embedding into your own projects (CMake / vcpkg / Conan / qmake), CLI tools, runnable example, examples overview |

## Guides (`docs/en/guides/`)

Task-oriented guides – one per feature:

| Document | Topic |
|---|---|
| [query-api.md](guides/query-api.md) | Query API: type-safe C++ expression tree (`Field("age") >= 18 && …`) – the binding form of FA-05 |
| [query-dsl.md](guides/query-dsl.md) | String DSL for queries (NFA-03, v0.14): `find("age >= 18 && city == 'Berlin'")`, `matches`/`in`, custom operators, `@relation` joins |
| [indexes.md](guides/indexes.md) | Secondary indexes per field incl. unique constraints (FA-10, v0.2) |
| [transactions.md](guides/transactions.md) | Atomic transactions per collection, commit/rollback, CoW rollback (FA-12, v0.3/v0.19) |
| [snapshot-json.md](guides/snapshot-json.md) | JSON snapshot export/import `toJson`/`fromJson`/`saveToFile`/`loadFromFile` (FA-14, v0.4) |
| [encrypted-snapshot.md](guides/encrypted-snapshot.md) | Encrypted binary snapshots, password-based (FA-19, v0.9) |
| [aggregations.md](guides/aggregations.md) | `count`/`min`/`max`/`sum`/`avg`/`distinct` + `groupBy` (FA-13, v0.6) |
| [observer.md](guides/observer.md) | Change callbacks `onChange`/`offChange`, incl. transaction commit (FA-15, v0.7) |
| [schema.md](guides/schema.md) | Optional strict collection schema (FA-20, v0.10) |
| [references.md](guides/references.md) | References between collections/documents, traversal, dangling, index-accelerated join (FA-18, v0.8/v0.17) |
| [merge-collections.md](guides/merge-collections.md) | Merging collections: `mergeCollections` / `mergeAllCollections` (FA-24, v0.24/v0.25) |
| [ttl-expiry.md](guides/ttl-expiry.md) | TTL / auto-expiry, opt-in (FA-25, v0.26) |
| [import.md](guides/import.md) | Import from CSV/JSON/JSONL/SQL dump/BSON/snapshot/(optional) SQLite incl. foreign-key import (FA-21/FA-26, v0.13/v0.28), CLI `inmemdb_import` |
| [db-graph.md](guides/db-graph.md) | Database graph as JSON/Mermaid/Markdown (FA-22, v0.15), CLI `inmemdb_graph` |
| [discovery.md](guides/discovery.md) | Relationship discovery – API reference (FA-27, v0.29–v0.40) |
| [discovery-guide.md](guides/discovery-guide.md) | Discovery guide: every method explained with examples & graphs (v0.29–v0.40), CLI `inmemdb_discover` |
| [hybrid-outofcore.md](guides/hybrid-outofcore.md) | Hybrid / out-of-core: spill-to-disk on RAM shortage, opt-in (v0.18) |
| [metrics.md](guides/metrics.md) | Metrics & diagnostics: `Collection::metrics()` / `Database::metrics()` (v0.22) |
| [limits.md](guides/limits.md) | Optional collection limits: `setLimits`, `LimitError`, rules of thumb (FA-28, v0.39) |
| [character.md](guides/character.md) | Data character categorization: rough distinction numeric / mixed / complex (FA-29) |
| [numeric-analysis.md](guides/numeric-analysis.md) | Numeric metadata analysis: statistics, grids, sequences, Benford, correlations (FA-30) |
| [influence.md](guides/influence.md) | Influence analysis: dominant fields, R², functional dependencies, roles (FA-31) |
| [cross-influence.md](guides/cross-influence.md) | Cross-collection influence: join from references/FKs, roles per collection pair (FA-32) |
| [qt-qmake.md](guides/qt-qmake.md) | Qt/qmake integration + `QAbstractTableModel` adapter, no hard Qt dependency (FA-23, v0.16/v0.23) |
| [type-mapping.md](guides/type-mapping.md) | Type mapping InMemDB ↔ SQL / MongoDB(BSON) / JSON |

## Internals (`docs/en/internals/`)

Background and decisions for contributors:

| Document | Topic |
|---|---|
| [architecture.md](internals/architecture.md) | Architecture decisions (ADRs): static library with Pimpl (ADR-01), own crypto/regex/JSON implementations, external backends only as opt-in (ADR-02) |
| [error-handling.md](internals/error-handling.md) | Error handling / exception policy (v0.35) + MessageHandler singleton (v0.36) |
| [hardening.md](internals/hardening.md) | Hardening of the framework: all limits (regex/JSON/DSL/BSON/spill/crypto), test strategy (v0.34–v0.39) |
| [performance.md](internals/performance.md) | Performance: `memdb_bench` benchmark, measurements, best practices |

## Images

[images/](../images/) – example graphs (SVG) from the discovery and analysis guides
(pipeline dispatcher, histogram/Benford, evidence/roles, cross-collection join).

---

The **API reference** (all classes/methods) is generated with Doxygen – see
[Getting Started → API documentation](getting-started.md#api-documentation-doxygen-nfa-08).

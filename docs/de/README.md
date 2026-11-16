# Dokumentation

> 🇬🇧 **English version:** [docs/en/README.md](../en/README.md)

Willkommen in der InMemDB-Dokumentation. Diese Seite ist der **Index**: Sie verweist auf
alle Dokumente, gruppiert nach Einsatzzweck.

| | |
|---|---|
| Projekt | [README](../../README.de.md) (Pitch, Quickstart, Features) · [Anforderungen / Lastenheft](../../anforderungen.md) (FA-01…FA-28, NFA-01…NFA-09) · [Changelog](../../CHANGELOG.md) · [Lizenz](../../LICENSE) (MIT) |
| Einstieg | [Getting Started](getting-started.md) – Build & Tests, Einbindung in eigene Projekte (CMake / vcpkg / Conan / qmake), CLI-Tools, lauffähiges Beispiel, Beispiele-Übersicht |

## Guides (`docs/de/guides/`)

Anwendungsorientierte Anleitungen – eine pro Feature:

| Dokument | Thema |
|---|---|
| [query-api.md](guides/query-api.md) | Query-API: typsicher C++-Ausdruckbaum (`Field("age") >= 18 && …`) – die verbindliche Ausprägung von FA-05 |
| [query-dsl.md](guides/query-dsl.md) | String-DSL für Queries (NFA-03, v0.14): `find("age >= 18 && stadt == 'Berlin'")`, `matches`/`in`, eigene Operatoren, `@relation`-Joins |
| [indexes.md](guides/indexes.md) | Sekundärindizes pro Feld inkl. Unique-Constraints (FA-10, v0.2) |
| [transactions.md](guides/transactions.md) | Atomare Transaktionen pro Collection, commit/rollback, CoW-Rollback (FA-12, v0.3/v0.19) |
| [snapshot-json.md](guides/snapshot-json.md) | JSON-Snapshot Export/Import `toJson`/`fromJson`/`saveToFile`/`loadFromFile` (FA-14, v0.4) |
| [encrypted-snapshot.md](guides/encrypted-snapshot.md) | Verschlüsselte binäre Snapshots, passwortbasiert (FA-19, v0.9) |
| [aggregations.md](guides/aggregations.md) | `count`/`min`/`max`/`sum`/`avg`/`distinct` + `groupBy` (FA-13, v0.6) |
| [observer.md](guides/observer.md) | Change-Callbacks `onChange`/`offChange`, inkl. Transaktions-Commit (FA-15, v0.7) |
| [schema.md](guides/schema.md) | Optionales striktes Collection-Schema (FA-20, v0.10) |
| [references.md](guides/references.md) | Referenzen zwischen Collections/Dokumenten, Traversierung, Dangling, index-beschleunigter Join (FA-18, v0.8/v0.17) |
| [merge-collections.md](guides/merge-collections.md) | Collections zusammenführen: `mergeCollections` / `mergeAllCollections` (FA-24, v0.24/v0.25) |
| [ttl-expiry.md](guides/ttl-expiry.md) | TTL / Auto-Expiry, opt-in (FA-25, v0.26) |
| [import.md](guides/import.md) | Import aus CSV/JSON/JSONL/SQL-Dump/BSON/Snapshot/(optional) SQLite inkl. Fremdschlüssel-Import (FA-21/FA-26, v0.13/v0.28), CLI `inmemdb_import` |
| [db-graph.md](guides/db-graph.md) | Datenbank-Graph als JSON/Mermaid/Markdown (FA-22, v0.15), CLI `inmemdb_graph` |
| [discovery.md](guides/discovery.md) | Relationship-Discovery – API-Referenz (FA-27, v0.29–v0.40) |
| [discovery-guide.md](guides/discovery-guide.md) | Discovery-Guide: jede Methode erklärt mit Beispielen & Graphen (v0.29–v0.40), CLI `inmemdb_discover` |
| [hybrid-outofcore.md](guides/hybrid-outofcore.md) | Hybrid / Out-of-Core: Spill-to-Disk bei RAM-Mangel, opt-in (v0.18) |
| [metrics.md](guides/metrics.md) | Metriken & Diagnose: `Collection::metrics()` / `Database::metrics()` (v0.22) |
| [limits.md](guides/limits.md) | Optionale Collection-Limits: `setLimits`, `LimitError`, Faustregeln (FA-28, v0.39) |
| [qt-qmake.md](guides/qt-qmake.md) | Qt/qmake-Einbindung + `QAbstractTableModel`-Adapter, ohne harte Qt-Abhängigkeit (FA-23, v0.16/v0.23) |
| [type-mapping.md](guides/type-mapping.md) | Typ-Mapping InMemDB ↔ SQL / MongoDB(BSON) / JSON |

## Internes (`docs/de/internals/`)

Hintergründe und Entscheidungen für Mitwirkende:

| Dokument | Thema |
|---|---|
| [architecture.md](internals/architecture.md) | Architektur-Entscheidungen (ADRs): statische Library mit Pimpl (ADR-01), eigene Krypto/RegEx/JSON-Implementierungen, externe Backends nur als Opt-in (ADR-02) |
| [error-handling.md](internals/error-handling.md) | Fehlerbehandlung / Exception-Policy (v0.35) + MessageHandler-Singleton (v0.36) |
| [hardening.md](internals/hardening.md) | Härtung des Frameworks: alle Limits (RegEx/JSON/DSL/BSON/Spill/Krypto), Test-Strategie (v0.34–v0.39) |
| [performance.md](internals/performance.md) | Performance: Benchmark `memdb_bench`, Messergebnisse, Best Practices |

## Bilder

[images/](../images/) – Beispiel-Graphen (SVG) aus dem Discovery-Guide.

---

Die **API-Referenz** (alle Klassen/Methoden) wird per Doxygen generiert – siehe
[Getting Started → API-Dokumentation](getting-started.md#api-dokumentation-doxygen-nfa-08).

# Changelog

Alle nennenswerten Änderungen an InMemDB. Format an [Keep a Changelog](https://keepachangelog.com/de/1.1.0/)
angelehnt; Versionen folgen dem Schema `0.x` (bis v1.0 kann sich die API noch ändern).
Jede Version entspricht einem Bereich gemergter Pull Requests – die PR-Nummern sind angegeben.

## [Unreleased]

### Changed
- Dokumentation komplett umstrukturiert (Stil populärer Repositories): `docs/` mit eigenem
  [Index](docs/README.md), neuem [Getting Started](docs/getting-started.md) (Build, Einbindung,
  CLI-Tools, Beispiele – aus der README verschoben), Guides unter `docs/guides/` und Internes
  (Architektur/ADRs, Fehlerbehandlung, Härtung, Performance) unter `docs/internals/`;
  die README ist jetzt eine schlanke Landing-Page und verweist auf die Doku
- `CHANGELOG.md` neu angelegt (dieses Dokument)

## [0.40] – 2026-11-16

### Added
- Ausreißer-Erkennung im Discovery: `discoverOutliers(coll | vector<Collection>, opts)` –
  z-Score (`outlierZ`, Default 3.0) + IQR/Tukey-Fence (`outlierIqrK`, Default 1.5) pro
  numerischem Feld, cross-database, `FieldOutliers` mit Quantilen (Q1/Median/Q3/IQR),
  Mean/Stddev, Outlier-Rate, Methode + bis zu 100 Beispielwerten (#50)
- `AnalysisReport::outliers` + Health-Score als 5. Faktor (Gewichte neu 0.30/0.15/0.20/0.20/0.15,
  ohne numerische Werte neutral 1.0); alle 4 Präsentationen (JSON `"outliers"`, Markdown-Sektion,
  `printSummary` `Outliers: N`, Empfehlung) (#50)
- CLI: `inmemdb_discover --outliers` (#50)

## [0.39] – 2026-11-16

### Added
- Optionale Collection-Limits (FA-28): `Collection::setLimits`/`limits()` mit
  `maxDocuments`/`maxDocumentBytes`/`maxFieldsPerDocument`/`maxIndexes` (0 = unbegrenzt),
  `LimitError`, Metrik `limitViolations`; Durchsetzung in allen Schreibpfaden (insert/bulk/load/
  update/replace/upsert/index, Tx + Spill), Doku mit Faustregeln (#49)

## [0.38] – 2026-11-16

### Security
- Härtung Teil 2: DSL-Parser mit Verschachtelungs-Limit 256 (vorher Segfault ab ~50000 Klammern)
  + strikte Zahlen (int64-Überlauf → double, `"1.2.3"`/`"1e"` → `QueryError`); BSON-Parser mit
  Tiefenlimit 1000 (vorher Segfault ab ~20000 Verschachtelung) (#48)

### Added
- Zentrale Härtungs-Doku `docs/internals/hardening.md` mit allen Limits und Test-Strategie (#48)

## [0.37] – 2026-11-16

### Security
- JSON-Parser/Writer gehärtet: Verschachtelungs-Limit 1000 (Parser + Writer + Schema; vorher
  Segfault ab ~50000 `[[[...` bzw. bei zyklischen Value-Bäumen), `{"$binary":...}` nur bei
  gültigem Hex (sonst normales Dokument), Hex-Float-Fallback nur mit `0x`-Präfix (#47)

## [0.36] – 2026-11-16

### Added
- MessageHandler-Singleton (`MessageHandler::instance()`): eigene Handler via
  `subscribe()`/`unsubscribe()`, Level-Filter `setMinLevel`, thread-sicheres `publish()`
  (throwende Handler werden abgefangen), Zähler `count()/totalCount()/handlerErrors()`;
  die Abfang-Stellen der Exception-Policy (v0.35) melden dorthin (#46)

## [0.35] – 2026-11-16

### Changed
- Exception-Policy (`docs/internals/error-handling.md`): Query-Scan und Observer fangen Exceptions pro
  Element ab (Zähler `evalErrors`/`eventErrors` in den Metriken), `loadJson`/`fromJson`
  validieren vor Mutation (atomar, kein Partial State), alle CLI-Tools fangen `std::exception`
  inkl. Options-Parsing (sauberer Exit-Code statt terminate); echte Fehler (Unique/Schema/
  Crypto/Datei) werfen weiterhin (#45)

## [0.34] – 2026-11-16

### Security
- Mini-RegEx gehärtet: harte Limits (Pattern ≤ 4096 Zeichen, Verschachtelung ≤ 256, NFA ≤ 50000
  Anweisungen, Match-Budget 100 Mio. Schritte → `std::invalid_argument`/`std::runtime_error`
  statt Segfault/Hang); `detail::Regex` kompiliert das Pattern einmalig pro Query; Fuzz-Test (#44)

## [0.33] – 2026-11-16

### Added
- Discovery: automatische Gesamtanalyse `analyze()` → `AnalysisReport` mit **Health-Score
  0–100 (Ampel 🟢🟡🔴)** und vier Präsentationen (`toMarkdown`/`toJson`/`toMermaid`/
  `printSummary`), cross-database; CLI `inmemdb_discover --report <prefix>` schreibt
  `.report.md`/`.relations.json`/`.graph.mmd` (#40)
- Ausführlicher Discovery-Guide mit Beispiel-Graphen (`docs/images/*.svg`) (#41)
- Packaging: CMake `find_package`/FetchContent-Export, `vcpkg.json`, `conanfile.py` (#42)
- ADR-02: externe Krypto/RegEx/JSON-Backends nur als Opt-in hinter CMake-Option, nie Pflicht (NFA-06) (#43)

## [0.32] – 2026-11-16

### Added
- Discovery: Cross-DB-Diff (`diffCollections`/`diffDatabases`/`toJson(DatabaseDiff)`) +
  Merge-Vorschläge (`suggestMerges`, Schema-Jaccard + Werte-Overlap, cross-database, passend
  zu FA-24); CLI `--diff`/`--suggest-merges` (#39)

## [0.31] – 2026-11-16

### Added
- Discovery: Datenprofil (`profileCollection`/`toJson`) + Candidate-Key-Discovery
  (`discoverCandidateKeys`) (#38)

## [0.30] – 2026-11-16

### Added
- Discovery: Kardinalität (1:1/n:1/1:n/n:m) im FK-Report, Orphan-Report
  (`findOrphans`/`findOrphansForRelations`), Pfad-Suche (`findPaths`); CLI `--orphans`/`--path` (#37)

## [0.29] – 2026-11-16

### Added
- Relationship-Discovery (FA-27): `discoverForeignKeys`/`discoverFunctionalDependencies`/
  `discoverCorrelations`/`discoverNearDuplicates`/`discoverRelations` + `applyDiscovered`,
  cross-database, ohne externe Abhängigkeiten; JSON/Mermaid-Report; CLI `inmemdb_discover` (#36)

## [0.28] – 2026-11-16

### Added
- Fremdschlüssel beim Import als FA-18-Referenzen mitimportieren (FA-26): SQLite
  `PRAGMA foreign_key_list`, SQL-Dump `REFERENCES`/`FOREIGN KEY`; CLI `--no-foreign-keys` (#35)

## [0.27] – 2026-11-16

### Added
- Allgemeine Foundations: `memdb::ValueLess`, `distinctValues` (index-aware), `sample(maxN, seed)`,
  `AggOp::Distinct`, `Expression::isMatchAll()` (#33)

### Changed
- README neu formuliert (klarer für externe Leser) (#32, #34)

## [0.26] – 2026-11-15

### Added
- TTL / Auto-Expiry (FA-25, opt-in, default aus): `expiresIn(sek)`/`expiresAt(unixSekunden)`
  pro Dokument, `expireNow()`, Metrik `expiredDocs` (#31)

## [0.25] – 2026-11-15

### Added
- Komfort-Wrapper `Database::mergeAllCollections` – ganze Datenbank in eine Collection mergen (#30)

## [0.24] – 2026-11-15

### Added
- Collections zusammenführen (FA-24): `Database::mergeCollections(target, sources, opts, refs*)`
  mit neuen IDs, `_source`-Herkunftsfeld, Referenz-Umschreiben, atomarem Rollback (#29)

## [0.23.1] – 2026-11-15

### Fixed
- Locale-unabhängige Zahlen: bei `de_DE` wurde das Dezimalkomma statt Punkt geschrieben (Bugfix) (#28)

## [0.23] – 2026-11-14

### Added
- Qt-Modell-Adapter `memdb::qt::MemDbTableModel` (QAbstractTableModel, nur QtCore, ohne harte
  Qt-Abhängigkeit), Beispiel `examples/qt_table` (#27)

## [0.22] – 2026-11-14

### Added
- Metriken & Diagnose: `Collection::metrics()`/`resetMetrics()` und `Database::metrics()`
  (Zugriffszähler, Index-Nutzung, Query-Dauer, Spill-IO, Tx, Events) (#26)

## [0.21] – 2026-11-14

### Added
- Bulk-Update via DSL mit Projektion: `Collection::update(queryDsl, setDsl)` und
  `Transaction::update(dsl, setDsl)` (#25)

## [0.20] – 2026-11-14

### Added
- Bessere `matches`-Regex: `{n}`/`{n,}`/`{n,m}`, `\d \D \w \W \s \S` (auch in Klassen),
  `(?:...)`-Gruppen, Lazy-Quantoren; Visited-Set im NFA beseitigt Hang bei `(a*)*` (#24)

## [0.19] – 2026-11-14

### Changed
- Performance-Reste: Transaktions-Backup per Copy-on-Write-Undo (O(1) Begin/Commit statt
  O(n) Voll-Backup), JSON-Parser-Fastpaths (String-Run-Append, `std::from_chars`),
  Observer-Event-Reservierung bei `insertBulk`; Rollback baut Indizes nur bei Bedarf neu (#23)

## [0.18] – 2026-11-14

### Added
- Hybrid / Out-of-Core (opt-in): `Collection::setOverflow(spillFile, maxInMemory)` lagert
  älteste Dokumente in eine Spill-Datei aus und liest sie on-demand; `overflowInfo()`,
  `compactSpill`, Recovery (#22)

## [0.17] – 2026-11-14

### Added
- Index-beschleunigter Join bei Abfrage: `ReferenceIndex::joinIds`/`join` (#21)

## [0.16] – 2026-11-14

### Added
- Qt/qmake-Einbindung (FA-23): `InMemDB.pro`/`InMemDB.pri`, Beispiel `examples/qt_hello`, CI-Job (#20)

## [0.15] – 2026-11-14

### Added
- Datenbank-Graph (FA-22): `DbGraph` als JSON/Mermaid/Markdown aus laufender DB oder
  Snapshot-Datei, CLI `inmemdb_graph` (#18)

### Changed
- Doku-Überarbeitung: Lastenheft → `anforderungen.md`, `docs/internals/architecture.md` aktualisiert (#19)

## [0.14] – 2026-11-14

### Added
- String-DSL für Queries (NFA-03): `parseQuery`, `QueryFunctionRegistry`, `matches`/`in`,
  `@relation.path`-Joins, Mini-RegEx (Thompson-NFA) (#17)

## [0.13] – 2026-11-14

### Added
- Import-Modul (FA-21): `ImportSource`/`ImportRegistry`/`Importer` + Quellen CSV, JSON/JSONL,
  Snapshot, SQL-Dump, BSON, optional SQLite (`INMEMDB_WITH_SQLITE=ON`); CLI `inmemdb_import` (#16)

## [0.12] – 2026-11-13

### Changed
- Bulk-Load für JSON-Snapshots (`fromJson`/`loadJson`): 1 Mio. Dokumente ~35–40 % schneller (#15)

## [0.11] – 2026-11-13

### Added
- Benchmark `memdb_bench` + Hotpath-Optimierungen (#13)

### Changed
- Kopierfreie Zugriffe `findIds(q)` / `forEach(q, cb)` (Collection + Transaction) (#14)

## [0.10] – 2026-11-13

### Added
- Striktes optionales Collection-Schema mit Feldtypen (FA-20) (#11)
- Doku: Typ-Mapping InMemDB ↔ SQL/MongoDB/JSON (#12)

## [0.9] – 2026-11-13

### Added
- Verschlüsselte binäre Snapshots (FA-19): `saveToFileEncrypted`/`loadFromFileEncrypted` –
  PBKDF2-HMAC-SHA256 + ChaCha20 + HMAC-SHA256 (encrypt-then-MAC), ohne externe Krypto-Bibliothek (#10)

## [0.8] – 2026-11-12

### Added
- Referenzen zwischen Collections/Dokumenten (FA-18): `ReferenceIndex` mit link/unlink,
  outgoing/incoming, Traversierung, Dangling-Erkennung (#8)
- Doxygen-API-Dokumentation (NFA-08) + CI-Docs-Job (#9)

## [0.7] – 2026-11-12

### Added
- Observer/Change-Callbacks (FA-15): `onChange`/`offChange`, Events inkl. Transaktions-Commit (#7)

## [0.6] – 2026-11-12

### Added
- Aggregationen (FA-13): `aggregate()` (count/sum/avg/min/max) und `groupBy()` (#6)

## [0.5] – 2026-11-12

### Added
- Upsert (FA-07): `upsertById` / `upsert(query, doc)` (#5)

## [0.4] – 2026-11-12

### Added
- JSON-Snapshot Export/Import (FA-14): `toJson`/`fromJson`/`saveToFile`/`loadFromFile` (#4)

## [0.3] – 2026-11-12

### Added
- Atomare Transaktionen pro Collection (FA-12): commit/rollback, RAII-Rollback (#3)

## [0.2] – 2026-11-12

### Added
- Sekundärindizes inkl. Unique-Constraints (FA-10) + lauffähige Beispiele (#2)

## [0.1] – 2026-11-12

### Added
- Erstes Release: Lastenheft, Query-Design, C++-Prototyp als statische Library mit
  Pimpl-Idiom; Database/Collection/Document/Value, CRUD, typsicher Query-Ausdruckbaum (#1)

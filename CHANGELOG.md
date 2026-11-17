# Changelog

Alle nennenswerten Änderungen an InMemDB. Format an [Keep a Changelog](https://keepachangelog.com/de/1.1.0/)
angelehnt; Versionierung nach **SemVer** – ab v1.0.0 ist die öffentliche API stabil.
Jede Version entspricht einem Bereich gemergter Pull Requests – die PR-Nummern sind angegeben.

## [Unreleased]

### Added
- Discovery v0.42: **Confusion-Matrix** `evaluateConfusion(rels, db, opts)` + `AnalysisReport::confusion`
  (nur `analyze(Database&)` mit `__references`): entdeckte FKs (gerichtete Collection-Paare, conf ≥
  `confusionMinConfidence` = 0.9) vs. deklarierte Referenzen → TP/FP/FN/TN + Precision/Recall/F1;
  in JSON (`"confusion"`), Markdown-Sektion, `printSummary` (`F1=`); CLI `inmemdb_discover --confusion`
  und `--report` bei einer Datei; Tests `tests/test_discovery7.cpp` (29 Checks)
- **Gemischte Metadaten-Analyse (FA-33):** `analyzeMixedFields`/`analyzeMixed` (Collection,
  mehrere Collections, `Database&`) interpretiert Felder gemäß ihrem dominanten Typ
  (Zahl/Text/Boolean, Tie-Break Zahl>Text>Boolean) – Zahl-Kennzahlen inkl. Ganzzahl-Anteil
  und Diskretheit, Text-Klassen (kategorisch/Kennung/Freitext) mit Format-Hinweisen
  (Datum/Hex/zahlartig), Boolean-Anteile, Missing-Quote und Typ-Drift; paarweise
  Assoziationen (Pearson für Zahl/Boolean-Kombinationen, eta² für kategorisch gegen
  Zahl/Boolean, Cramérs V für kategorisch/kategorisch) mit Guards; Reports
  `toJson`/`toMarkdown`; Tests `tests/test_mixed_analysis.cpp` (129 Checks)

### Fixed (Härtung FA-29 bis FA-32)
- **Numerische Stabilität (FA-30/31/32):** Pearson-Korrelationen zweistufig zentriert und in
  `long double` berechnet – die bisherige Summenformel (`n·Sxy − Sx·Sy`) verlor bei großen
  Werten mit kleinem Variationsbereich durch Auslöschung massiv an Genauigkeit; zusätzlich
  werden die Wert-Slots konsistent zum sortierten Feldpaar zugeordnet (Feldreihenfolge kann
  je Dokument wechseln), das Ergebnis ist auf [−1, 1] geklemmt
- **Extremwerte (FA-30):** Momente (Mittel/Stdabw./Schiefe/Kurtosis) in `long double`
  akkumuliert, Finite-Checks beim Zurückschreiben; Histogramm bei unendlicher Spannweite
  (alle Werte in Bin 0) – Reports bleiben gültiges JSON (kein `inf`/`nan`)
- **R²-Regression (FA-31/32):** Gauss-Solver prüft die Lösung auf Endlichkeit (Extremwerte →
  `rSquaredComputed = false` statt NaN); Normalgleichungen jetzt **mittelwertzentriert**
  ohne Intercept-Spalte – bei großen Offsets (z. B. 1e12) ertränkte die Intercept-Spalte die
  kleine Restvarianz im Rundungsrauschen; `influenceScore` per Finite-Guard – NaN hätte die
  Sortierung (strict weak ordering) gebrochen
- **Cross-Collection (FA-32):** hängende Kanten (gelöschte Dokumente) zählen nicht mehr in
  `rows`/`matchedA`/Join-Abdeckung; ungültige `__references`-Einträge (negative, nicht
  ganzzahlige oder NaN-IDs) werden ignoriert
- **Guards (FA-30):** Felder ohne numerische Werte werden auch bei `minValuesPerField = 0`
  übersprungen (kein Zugriff auf leere Wertmengen)
- Härtungs-Doku: `docs/de/internals/hardening.md` Abschnitt 6 (+ `docs/en/`)

### Added
- **Datencharakter-Kategorisierung (FA-29):** `assessCharacter(coll | vector<Collection> | Database, DiscoveryOptions)` stuft Collections grob in drei Daten-Kategorien ein – `DataCategory::Numeric` (Kategorie 1, >= 80 % numerische Felder), `Mixed` (Kategorie 2, Zahlen/Strings/Booleans) und `Complex` (Kategorie 3, Arrays/Dokumente/Binär oder Typ-Drift >= 40 %) – inkl. `confidence`, Klartext-Begründung, `FieldCharacter` pro Feld (dominanter Typ, Typ-Reinheit, Drift) und Reports `toJson`/`toMarkdown` (`include/memdb/character.hpp`); Vorab-Dispatcher für die passenden Analyse-Pipelines; Guide: `docs/de/guides/character.md` (+ `docs/en/`); Tests `tests/test_character.cpp` (62 Checks)
- **Numerische Metadaten-Analyse (FA-30, Kategorie-1-Pipeline zu FA-29):** `analyzeNumeric(coll | vector<Collection> | Database, NumericAnalysisOptions)` interpretiert numerische Felder statistisch – Basis-Kennzahlen, Quantile, Schiefe/Exzess-Kurtosis, Ganzzahl-Anteil/Diskretheit, Rundungs-Raster-Erkennung (Kandidaten 100000 bis 0.0001, >= 95 % der Werte exakt), Sequenz-Charakter in Einfügereihenfolge (via DocumentId: monoton, ID-artig Schritt 1, Lücken, mediane Schrittweite), Benford-Analyse der ersten signifikanten Ziffer (MAD, Schwelle 0.015 nach Nigrini), äquidistantes Histogramm und paarweise Pearson-Korrelationen; interpretierende `note`-Hinweise pro Feld; Reports `toJson`/`toMarkdown` (`include/memdb/numeric_analysis.hpp`); Guide: `docs/de/guides/numeric-analysis.md` (+ `docs/en/`); Tests `tests/test_numeric_analysis.cpp` (112 Checks)
- **Einfluss-Analyse (FA-31, Vertiefung von FA-30):** `analyzeInfluence(coll | vector<Collection> | Database, InfluenceOptions)` deutet die statistischen Werte weiter und bestimmt, welches Feld dominanten Einfluss auf andere hat – Hub über die Pearson-Korrelationen (Summe/Mittel der |r|, Anzahl starker Zusammenhänge ab 0.8), multiples R² je Feld gegen die übrigen (Complete-Cases-Regression mit Teilpivotisierung und Ridge-Term gegen Kollinearität) und funktionale Abhängigkeiten (gleiche Quell-Werte => eindeutiger Zielwert, Evidenz nur aus wiederholten Quell-Werten, Coverage-Schwelle 0.99) – daraus Rollen `driver`/`dependent`/`mediator`/`independent`, kombinierter `influenceScore` (0..1), `topDriver`, Klartext-Begründung je Feld und Reports `toJson`/`toMarkdown` (`include/memdb/influence.hpp`); Grenze: Korrelation belegt keine Kausalität; Guide: `docs/de/guides/influence.md` (+ `docs/en/`); Tests `tests/test_influence.cpp` (130 Checks)
- **Cross-Collection-Einfluss-Analyse (FA-32):** `analyzeCrossInfluence(Database, CrossInfluenceOptions)` berechnet Abhängigkeiten zwischen Feldern verschiedener Collections – verknüpft Collection-Paare über deklarierte `__references`-Relationen und entdeckte Fremdschlüssel (`discoverForeignKeys`, Konfidenz >= 0.9) per Join, überträgt die FA-31-Maschinerie (Pearson-Hub, multiples R² mit Ridge, funktionale Abhängigkeiten) auf die verknüpften Zeilen und bestimmt Rollen/Scores/Treiber je Collection-Paar; Kardinalität aus A-Sicht (n:1/1:n), Guards (minJoinPairs, Join-Abdeckung), Unterdrückung der trivialen Join-Abhängigkeit, Collection-qualifizierte Feldnamen (`coll.feld`), Reports `toJson`/`toMarkdown` (`include/memdb/cross_influence.hpp`); Guide: `docs/de/guides/cross-influence.md` (+ `docs/en/`); Tests `tests/test_cross_influence.cpp` (193 Checks)
- Englische Übersetzung der kompletten Dokumentation: `docs/en/` (Index, Getting Started,
  21 Guides, 4 Internes) neben der deutschen Original-Doku in `docs/de/`; Root-`README.md`
  ist jetzt Englisch mit Sprach-Hinweis, die deutsche README liegt in `README.de.md`;
  `docs/README.md` ist Sprach-Wahl (de/en); Bilder bleiben geteilt in `docs/images/`;
  anforderungen.md und CHANGELOG.md bleiben deutschsprachig

## [1.0.0] – 2026-11-16

**Erstes offizielles Release** (Git-Tag `v1.0.0`). Ab dieser Version gilt SemVer: Die öffentliche
API (`include/memdb/`) ist stabil; Änderungen daran erfolgen nur noch abwärtskompatibel bzw.
in einem Major-Release.

### Changed
- Versionsnummer auf **1.0.0** gehoben: CMake `project(InMemDB VERSION 1.0.0)` (Paket-Config
  übernimmt `${PROJECT_VERSION}`), `vcpkg.json`, `conanfile.py`, README/anforderungen;
  Doku-Beispiele: `find_package(InMemDB 1.0 REQUIRED)` / FetchContent `GIT_TAG v1.0.0`
- Dokumentation komplett umstrukturiert (Stil populärer Repositories, PR #51): `docs/` mit
  eigenem [Index](docs/README.md), neuem [Getting Started](docs/de/getting-started.md) (Build,
  Einbindung, CLI-Tools, Beispiele – aus der README verschoben), Guides unter `docs/guides/`,
  Internes (Architektur/ADRs, Fehlerbehandlung, Härtung, Performance) unter `docs/internals/`;
  README als schlanke Landing-Page; `CHANGELOG.md` neu angelegt

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
- Zentrale Härtungs-Doku `docs/de/internals/hardening.md` mit allen Limits und Test-Strategie (#48)

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
- Exception-Policy (`docs/de/internals/error-handling.md`): Query-Scan und Observer fangen Exceptions pro
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
- Doku-Überarbeitung: Lastenheft → `anforderungen.md`, `docs/de/internals/architecture.md` aktualisiert (#19)

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

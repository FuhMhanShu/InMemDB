# InMemDB

> 🇬🇧 **English version:** [README.md](README.md) · English docs: [docs/en/](docs/en/README.md)

**InMemDB ist eine Datenbank, die komplett im Arbeitsspeicher (RAM) läuft und direkt in
C++-Programme eingebettet wird** – als objektorientierte C++17-Bibliothek, ohne eigenen
Server, ohne Netzwerk und ohne externe Pflichtabhängigkeiten (Auslieferung als statische
Library, intern über das Pimpl-Idiom entkoppelt). MIT-Lizenz.

Statt Tabellen gibt es **Collections** (Sammlungen) von **Dokumenten**. Ein Dokument ist
ein JSON-ähnliches Objekt aus Feldern – und weil InMemDB **schema-los** ist, darf jedes
Dokument beliebige, auch wechselnde Felder haben (z. B. `{"name": "Ada", "age": 36}` und
`{"name": "Bob", "vip": true}` in derselben Collection).

Abgefragt wird auf zwei Arten, die zur selben Query werden: als **typsicher C++-Ausdruck**
(`Field("age") >= 18 && Field("stadt") == "Berlin"`) oder als **String** (`"age >= 18 && stadt == 'Berlin'"`).
Über Felder kann man beliebig **Indizes** legen, damit Abfragen nicht alles durchsuchen müssen.

> **Wichtig:** „In-Memory" heißt: Beim Beenden des Prozesses sind die Daten weg.
> Sichern geht jederzeit als JSON-Snapshot (`saveToFile`) oder verschlüsselt
> (`saveToFileEncrypted(path, passwort)`).

> **Aktueller Release: v1.0.0** (2026-11-16, Git-Tag `v1.0.0`) – erstes offizielles Release,
> ab jetzt SemVer (API stabil). Historie: [CHANGELOG.md](CHANGELOG.md).

## Quickstart

```cpp
#include <memdb/memdb.hpp>
memdb::Database db("shop");
auto products = db.createCollection("products");
products.createIndex("name", true);                              // eindeutiger Index auf "name"
products.insert({{"name", "Buch"}, {"preis", 10.5}, {"tags", {"neu"}}});
for (const auto& p : products.find("preis < 20 && tags.contains('neu')"))
    std::cout << p.at("name").asString() << '\n';                // DSL-Abfrage -> "Buch"
db.saveToFileEncrypted("shop.enc", "geheim");                    // verschlüsselter Snapshot
```

## Was kann InMemDB? (Features, v1.0)

**Daten & Abfragen**
- Schema-lose Dokumente mit dynamisch erweiterbaren Feldern (auch verschachtelt, Punktnotation `"adresse.plz"`)
- Abfragen als C++-Ausdruckbaum oder String-DSL, inkl. eigener Operatoren (`matches` für Mini-RegEx – Thompson-NFA, gehärtet v0.34 mit harten Limits gegen Stack-Overflow/Hang, kompiliert einmalig pro Query; `in`, eigene via Registry) und `@relation`-Joins (v0.14)
- Sekundärindizes pro Feld, inkl. Unique-Constraints (z. B. eindeutige E-Mail) – beschleunigte Gleichheits- und Bereichsabfragen (FA-10)
- Aggregationen: `count`, `min`, `max`, `sum`, `avg`, `distinct` sowie `groupBy` (FA-13)
- Foundations (v0.27): `memdb::ValueLess` (Value als map/set-Schlüssel), `distinctValues(feld, q)` (index-beschleunigt), `sample(maxN, seed)` (reproduzierbares Zufalls-Sample), `AggOp::Distinct`, `Expression::isMatchAll()`
- Bulk-Operationen (`insertBulk`) und Bulk-Update per DSL mit Projektion: `update("age >= 18", "adult = true, age = age + 1")` (v0.21)
- Upsert: einfügen oder aktualisieren, per ID oder per Query (FA-07)

**Struktur & Beziehungen**
- **Referenzen** zwischen Collections/Dokumenten (`ReferenceIndex`): in beide Richtungen abfragbar, Traversierung inkl. Ringprüfung (`traverse`/`hasCycle`/`search`), Dangling-Erkennung und -Bereinigung (FA-18); index-beschleunigter Join bei Abfrage (`joinIds`/`join`, v0.17); direkt in Queries als `@kunde.name == 'Ada'`
- **Merge:** mehrere Collections in eine zusammenführen – `Database::mergeCollections` bzw. `mergeAllCollections` für die ganze Datenbank, mit neuen IDs, `_source`-Herkunftsfeld und optionalem Löschen der Quellen inkl. Referenz-Umschreiben (FA-24, v0.24/v0.25)
- **Datenbank-Graph** als JSON, Mermaid oder Markdown – auch aus Snapshot-Dateien; CLI-Tool `inmemdb_graph` (FA-22)

**Sicherheit, Persistenz & Betrieb**
- Atomare **Transaktionen** pro Collection: alles oder nichts, commit/rollback, Rollback per Copy-on-Write (schnell auch bei Millionen Dokumenten, v0.19); RAII-Rollback (FA-12)
- **JSON-Snapshot** der ganzen DB oder einzelner Collections: `toJson`/`fromJson`/`saveToFile`/`loadFromFile` (FA-14); eigener Parser gehärtet v0.37 (Verschachtelungs-Limit 1000 gegen Segfault, `$binary` nur bei gültigem Hex, Hex-Float-Fallback nur mit `0x`)
- **Verschlüsselte Snapshots**: `saveToFileEncrypted`/`loadFromFileEncrypted` mit Passwort (PBKDF2 + ChaCha20 + HMAC-SHA256, ohne externe Krypto-Bibliothek, FA-19)
- Optionales **striktes Schema** pro Collection: Feldtypen als Metadaten, undefinierte Felder werden abgelehnt (FA-20)
- **Observer**: Callbacks bei jedem Einfügen/Ändern/Löschen (`onChange`), in Transaktionen erst beim Commit (FA-15)
- **TTL / Auto-Expiry** (abschaltbar, default aus): Dokumente mit Verfallszeit (`expiresIn(sek)` / `expiresAt(unixSekunden)`) werden automatisch entfernt (FA-25, v0.26)
- **Relationship-Discovery** (FA-27, v0.29): `discoverForeignKeys` / `discoverFunctionalDependencies` / `discoverCorrelations` / `discoverNearDuplicates` / `discoverRelations` – collection- und database-übergreifend, ohne Abhängigkeiten; `applyDiscovered` legt FA-18-Referenzen an; Kardinalität (1:1/n:1/1:n/n:m), Orphan-Report, Pfad-Suche (v0.30), Datenprofil + Candidate Keys (v0.31), Cross-DB-Diff + Merge-Vorschläge (v0.32), **automatische Gesamtanalyse + Report** `analyze()` mit Health-Score (Ampel 🟢🟡🔴) und Präsentation als Markdown/JSON/Mermaid/Konsole (`toMarkdown`/`toJson`/`toMermaid`/`printSummary`, v0.33); CLI `inmemdb_discover` mit JSON/Mermaid-Report (`--orphans`, `--path`) und Gesamt-Report (`--report <prefix>`); **Ausreißer-Erkennung** `discoverOutliers` (z-Score + IQR pro numerischem Feld, als Health-Score-Faktor, v0.40), CLI `--outliers`; **Confusion-Matrix** `evaluateConfusion`/`AnalysisReport::confusion` (entdeckte FKs vs. deklarierte `__references`, v0.42), CLI `--confusion`
- **Analyse-Pipeline** (FA-29…FA-34): `assessCharacter` stuft jede Collection grob als numerisch/gemischt/komplex ein und dispatcht in die passende Pipeline – `analyzeNumeric` (Kennzahlen, Quantile, Rundungs-Raster, Sequenzen, Benford, Korrelationen), `analyzeInfluence` (dominante Felder via Korrelations-Hub, multiplen R² und funktionalen Abhängigkeiten, Rollen, `topDriver`), `analyzeCrossInfluence` (Abhängigkeiten zwischen Feldern verschiedener Collections, gejoint über deklarierte Referenzen und entdeckte Fremdschlüssel) und `analyzeMixed` (dominante Typen, Text-Klassen, Format-Hinweise, Assoziationen aus Pearson/eta²/Cramérs V) und `analyzeComplex` (rekursives Pfad-Profil für verschachtelte Dokumente, Arrays und Typ-Drift, Schema-Varianten mit Konsistenz); Reports als JSON/Markdown ([Datencharakter](docs/de/guides/character.md) · [Numerik-Analyse](docs/de/guides/numeric-analysis.md) · [Einfluss-Analyse](docs/de/guides/influence.md) · [Cross-Collection-Einfluss](docs/de/guides/cross-influence.md) · [Gemischte Analyse](docs/de/guides/mixed-analysis.md) · [Komplexe Analyse](docs/de/guides/complex-analysis.md))
- **Hybrid / Out-of-Core**: bei zu wenig RAM lagert `setOverflow(spillFile, maxInMemory)` älteste Dokumente in eine Datei aus und liest sie bei Bedarf nach (v0.18)
- **Metriken & Diagnose** (v0.22): `Collection::metrics()`/`Database::metrics()` – Zugriffe, Index-Nutzung vs. Vollscan, Query-Dauer, Spill-IO, Transaktionen, Events, TTL-Expiry, `evalErrors`/`eventErrors` (v0.35). **Exception-Policy** (v0.35, [docs/de/internals/error-handling.md](docs/de/internals/error-handling.md)): Query-Scan und Observer fangen pro Element ab (Zähler in Metriken), Bulk-Load validiert vor Mutation (atomar), CLI beendet sauber mit Exit-Code; echte Fehler (Unique/Schema/Crypto/kaputte Datei) werfen weiterhin. **MessageHandler-Singleton** (v0.36): eigene Handler via `MessageHandler::instance().subscribe(...)` – die Abfang-Stellen melden dorthin (`query-eval`/`observer`/`load`/`import`/`regex`/`cli`), thread-safe, Level-Filter + Zähler. **Härtung** ([docs/de/internals/hardening.md](docs/de/internals/hardening.md)): harte Limits in allen Parsern (RegEx/JSON/DSL/BSON – keine Segfaults/Hangs mehr an pathologischen Eingaben), strikte Zahlen, Exception-Policy + MessageHandler, **optionale Collection-Limits** (`setLimits`: max. Docs/Bytes/Felder/Indizes → `LimitError` + Metrik, [docs/de/guides/limits.md](docs/de/guides/limits.md))
- **Import** aus CSV, JSON/JSONL, SQL-Dump, BSON, eigenen Snapshots und (optional, `INMEMDB_WITH_SQLITE=ON`) SQLite-Dateien; CLI-Tool `inmemdb_import` (FA-21); **Fremdschlüssel werden als FA-18-Referenzen mitimportiert** (SQLite `PRAGMA foreign_key_list`, SQL-Dump `REFERENCES`/`FOREIGN KEY`, abschaltbar via `--no-foreign-keys`, FA-26, v0.28)
- Thread-Safety über `shared_mutex` (parallele Lesezugriffe, serialisierte Schreibzugriffe); Zahlen sind locale-unabhängig (immer Dezimalpunkt, auch bei `de_DE`, Bugfix v0.23.1)
- **Qt/qmake-Einbindung** über `InMemDB.pro`/`InMemDB.pri` plus fertiger `QAbstractTableModel`-Adapter (`memdb::qt::MemDbTableModel`, nur QtCore, ohne harte Qt-Abhängigkeit, FA-23, v0.23)

## Abgrenzung gegen SQLite, DuckDB und UnQLite

InMemDB ist kein Ersatz für die etablierten eingebetteten Datenbanken, sondern deckt eine
andere Nische ab: ein **schema-loser Dokumenten-Working-Set im RAM**, nativ in C++17,
mit eingebauter Datenanalyse. Die folgende Tabelle stellt die Unterschiede heraus
(Stand: öffentliche Dokumentation der Projekte, November 2026):

| | **InMemDB** | **SQLite** | **DuckDB** | **UnQLite** |
|---|---|---|---|---|
| Positionierung | Eingebettete Arbeits-Datenbank für C++-Anwendungen: Daten leben als native Objekte im RAM | Eingebettete OLTP-Datenbank, Standard für relationale Daten auf dem Gerät | Eingebettete OLAP-Engine für analytisches SQL über lokale Daten | Eingebettete NoSQL-Engine für C (Key/Value und Dokumente) |
| Architektur & Speicher | Komplett im RAM; optionaler Spill der ältesten Dokumente in eine Datei (Hybrid, v0.18) | B-Baum-Datei auf Datenträger, Modus `:memory:` optional | Spaltenorientierte Datei, vektorisierte Ausführung, verarbeitet Daten größer als der RAM | Eine Datei oder In-Memory, Key/Value- plus Dokumenten-Store |
| Datenmodell | Schema-lose JSON-artige Dokumente, verschachtelte Felder, Arrays, Typ-Drift erlaubt | Relationale Tabellen mit festem Schema und starker SQL-Typisierung | Relationale (spaltenorientierte) Tabellen mit festem Schema | Key/Value plus JSON-Dokumente (Zugriff über die Jx9-Skriptsprache) |
| Abfragesprache | Typsicherer C++-Ausdrucksbaum oder String-DSL, kein SQL nötig | SQL, sehr vollständig (Window Functions, CTEs, …) | SQL, auf Analyse ausgelegt (Window Functions, …) | Jx9-Skripte, kein SQL |
| Indizes | Sekundär- und Unique-Indizes pro Feld (Gleichheit und Bereich) | B-Baum-Indizes, Partial- und Ausdrucks-Indizes | Automatisch (Zonemaps/ART), kaum manuelle Indexwahl | Keine Sekundärindizes; Cursoren für lineare Durchläufe |
| Transaktionen | Pro Collection atomar, Commit/Rollback, Rollback per Copy-on-Write (schnell bei Millionen Dokumenten) | Voll-ACID, mehrere Anweisungen, WAL | ACID, auf analytische Last ausgelegt (ein Schreiber) | ACID (Key/Value und Dokumente) |
| Beziehungen & Joins | Referenzen zwischen Collections, Traversierung mit Zykluserkennung, `@relation`-Joins, automatische FK-Entdeckung (FA-27) | FK-Constraints und SQL-JOINs | SQL-JOINs, auch über externe Dateien (CSV, Parquet, …) | Keine |
| Persistenz | JSON-Snapshots, verschlüsselte Snapshots (PBKDF2 + ChaCha20 + HMAC-SHA256), optionaler Spill | Eine DB-Datei, extrem erprobtes Format (Stabilität bis 2050 garantiert) | Eine Datei (stabil seit 1.0, 2024) | Eine Datei |
| Datenanalyse & Discovery | Eingebaut: FK-/FD-Entdeckung, Korrelationen, Ausreißer, Health-Score, numerische/gemischte/komplexe Analyse-Pipelines (FA-27…FA-34) | Keine (externe Werkzeuge) | Keine eingebaut; Analyse per handgeschriebenem SQL | Keine |
| Nebenläufigkeit | `shared_mutex`: parallele Lesezugriffe, serialisierte Schreibzugriffe | Threading-Modi (serialized), ein Schreiber pro Datei | Ein-Prozess, parallele Queries, ein Schreiber | Thread-Modi, ein Schreiber |
| API & Sprache | Natives, objektorientiertes C++17 (Pimpl, ohne externe Abhängigkeiten) | C-API (C++ nur über Wrapper) | C++/C-API | C-API |
| Lizenz | MIT | Public Domain | MIT | Symisc Public License (BSD-artig) |
| Typische Stärke | Strukturierter, schnell abfragbarer Working-Set im RAM plus mitgelieferte Profiling-/Analyse-Funktionen, ohne Infrastruktur | Zuverlässiger relationaler OLTP-Speicher für beliebige Anwendungen und Sprachen | Schnelle Aggregationen und Berichte über mittlere bis große lokale Datenmengen | Sehr leichtgewichtige eingebettete Persistenz für C-Programme (KV oder Jx9) |

Kurz: Wer relationale Daten dauerhaft auf Datenträger ablegt, bleibt bei **SQLite**; wer
analytische SQL-Aggregationen über große Datenmengen fährt, wählt **DuckDB**; wer eine
minimale NoSQL-Persistenz für C sucht, nimmt **UnQLite**. **InMemDB** füllt die Lücke
dazwischen: der RAM-Datensatz einer C++-Anwendung selbst, mit Abfragen, Indizes,
Transaktionen und einer Analyse-Pipeline, die den Bestand ohne SQL profiliert.
Ein bestehender SQLite-Bestand lässt sich über den Import übernehmen
(`INMEMDB_WITH_SQLITE=ON`, [Import-Guide](docs/de/guides/import.md)).

## Dokumentation

Der komplette Einstieg und alle Guides liegen in **[docs/](docs/de/README.md)**:

- [Getting Started](docs/de/getting-started.md) – Build & Tests, Einbindung (CMake/find_package/FetchContent/vcpkg/Conan/qmake), CLI-Tools (`inmemdb_import`, `inmemdb_graph`, `inmemdb_discover`), lauffähiges Beispiel, Beispiele-Übersicht
- [Guides](docs/de/README.md) – ein Dokument pro Feature: [Query-API](docs/de/guides/query-api.md) · [String-DSL](docs/de/guides/query-dsl.md) · [Indizes](docs/de/guides/indexes.md) · [Transaktionen](docs/de/guides/transactions.md) · [Snapshot/JSON](docs/de/guides/snapshot-json.md) · [Verschlüsselte Snapshots](docs/de/guides/encrypted-snapshot.md) · [Aggregationen](docs/de/guides/aggregations.md) · [Observer](docs/de/guides/observer.md) · [Schema](docs/de/guides/schema.md) · [Referenzen & Joins](docs/de/guides/references.md) · [Merge von Collections](docs/de/guides/merge-collections.md) · [TTL/Auto-Expiry](docs/de/guides/ttl-expiry.md) · [Import](docs/de/guides/import.md) · [Datenbank-Graph](docs/de/guides/db-graph.md) · [Discovery (API)](docs/de/guides/discovery.md) · [Discovery-Guide (erklärend, mit Graphen)](docs/de/guides/discovery-guide.md) · [Datencharakter](docs/de/guides/character.md) · [Numerik-Analyse](docs/de/guides/numeric-analysis.md) · [Einfluss-Analyse](docs/de/guides/influence.md) · [Cross-Collection-Einfluss](docs/de/guides/cross-influence.md) · [Gemischte Analyse](docs/de/guides/mixed-analysis.md) · [Komplexe Analyse](docs/de/guides/complex-analysis.md) · [Hybrid/Out-of-Core](docs/de/guides/hybrid-outofcore.md) · [Metriken](docs/de/guides/metrics.md) · [Collection-Limits](docs/de/guides/limits.md) · [Qt/qmake](docs/de/guides/qt-qmake.md) · [Typ-Mapping](docs/de/guides/type-mapping.md)
- [Internes](docs/de/README.md) – [Architektur/ADRs](docs/de/internals/architecture.md) · [Fehlerbehandlung](docs/de/internals/error-handling.md) · [Härtung](docs/de/internals/hardening.md) · [Performance](docs/de/internals/performance.md)
- [Anforderungen / Lastenheft](anforderungen.md) (FA-01…FA-34, NFA-01…NFA-09) · [Changelog](CHANGELOG.md) · API-Referenz per Doxygen (siehe [Getting Started](docs/de/getting-started.md#api-dokumentation-doxygen-nfa-08))

## Build & Test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Details (Benchmark, Installation, `find_package`/FetchContent/vcpkg/Conan, Beispiele):
[Getting Started](docs/de/getting-started.md).

## Lizenz

[MIT License](LICENSE) – Copyright (c) 2026 FuhMhanShu.

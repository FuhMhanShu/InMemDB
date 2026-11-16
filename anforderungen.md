# Anforderungen

## In-Memory Non-Relational Database System (C++, objektorientiert)

| Feld | Wert |
|---|---|
| Projekttitel | InMemDB – In-Memory Document Store |
| Version | 0.15 |
| Status | fortgeschrieben; FA-01…FA-25 und NFA-01…NFA-09 umgesetzt bzw. beschlossen |
| Sprache | Deutsch |
| Technologie | C++ (mindestens C++17, C++20 wünschenswert), CMake |
| Bereitstellungsform | Eingebettete Bibliothek als **kompilierte statische Library mit Pimpl-Idiom** (öffentliche Header unter `include/memdb/`, Implementierung gekapselt in `src/`); kein Server, kein Netzwerk. Header-only wurde verworfen – siehe `docs/architecture.md`. |

---

## 1. Zielbestimmung

### 1.1 Produktzweck
InMemDB ist ein **in-memory, non-relationales (dokumentenorientiertes) Datenbanksystem**, das als C++-Bibliothek in Anwendungen eingebettet wird. Kernmerkmal: **Dokumente besitzen keine feste Struktur – Felder sind jederzeit und pro Dokument beliebig erweiterbar, änderbar und löschbar** (schema-less / schemalos zur Laufzeit).

### 1.2 Nutzen
- Schnelle prototypische und produktive Datenhaltung ohne Schema-Definition oder Migrationen.
- Volle Geschwindigkeit des Arbeitsspeichers (keine Festplatten-I/O im Normalbetrieb).
- Natürliche Abbildung hierarchischer, verschachtelter Daten (Objekte, Listen) ohne JOINs.
- Typsichere, moderne C++-API trotz dynamischer Felder (Laufzeit-Typprüfung).

### 1.3 Abgrenzung (Nicht-Ziele)
- Kein relationales Modell, **kein SQL**, keine Tabellen/Joins im klassischen Sinn.
- Keine Persistenz als Primärziel (optionaler Snapshot/Export als Kann-Anforderung).
- Kein Netzwerkzugriff, kein Client/Server, keine verteilte Replikation, kein Multi-User-Betrieb über Prozesse hinweg.

---

## 2. Produktumfeld / Systemkontext

- **Einsatz**: als Library in C++-Anwendungen (Desktop, Server-seitige Dienste, eingebettete Systeme, Tests).
- **Plattformen**: Linux, Windows, macOS; Compiler: GCC, Clang, MSVC.
- **Randbedingungen**: Single-Process; Daten liegen ausschließlich im RAM und gehen beim Prozessende verloren (außer Snapshot).
- **Keine externen Pflichtabhängigkeiten** im Kern; optionale Abhängigkeiten nur für JSON-Serialisierung und Tests.

---

## 3. Datenmodell

| Konzept | Beschreibung |
|---|---|
| **Database** | Oberste Einheit; enthält benannte Collections. |
| **Collection** | Benannte Menge von Dokumenten (analog zu einer Tabelle, aber ohne Schema). |
| **Document** | Eine Entität als Menge von **Feld → Wert**-Paaren. Jedes Dokument hat eine automatisch vergebene, eindeutige **ID**. |
| **Feld (Field)** | Schlüssel (String) + Wert. Felder sind **pro Dokument individuell**: Hinzufügen, Ändern des Typs, Umbenennen und Entfernen jederzeit möglich, auch nachträglich für bestehende Dokumente. |
| **Wert (Value)** | Dynamischer Typ pro Feld: `null`, `bool`, `int64`, `double`, `string`, `binary`, verschachteltes **Document**, **Array** (Liste von Values). Der Typ eines Feldes kann sich von Dokument zu Dokument und über die Zeit unterscheiden. |
| **Verschachtelung** | Dokumente und Arrays können beliebig tief verschachtelt sein; Zugriff über Punktnotation (z. B. `adresse.stadt`). |
| **Typ-Mapping** | Abbildung der Werttypen auf gängige Datenbanksysteme (SQL, MongoDB/BSON, JSON): siehe [`docs/type-mapping.md`](docs/type-mapping.md). |
| **Schema** | Es gibt **kein verbindliches Schema**. Optional (Kann): pro Collection ein Validierungsschema, das neue/aktualisierte Dokumente prüft. |

---

## 4. Funktionale Anforderungen

Prioritäten: **M** = Muss, **S** = Soll, **K** = Kann.

| ID | Anforderung | Prio |
|---|---|---|
| FA-01 | Database instanziieren, benennen, schließen; alle Daten werden beim Schließen verworfen. | M |
| FA-02 | Collections anlegen, auflisten, umbenennen, löschen (inkl. aller Dokumente). | M |
| FA-03 | Dokument in Collection **einfügen**; automatische, eindeutige ID-Vergabe; Rückgabe der ID. | M |
| FA-04 | Dokument **nach ID lesen** (`findById`); optional: Version/Timestamp des Dokuments. | M |
| FA-05 | **Abfragen (Query)** über Collections: Vergleichsoperatoren (`=`, `!=`, `<`, `<=`, `>`, `>=`), logische Verknüpfung (`AND`, `OR`, `NOT`), Textoperatoren (z. B. `contains`, Präfix), Array-Operatoren (enthält Element), Zugriff auf verschachtelte Felder via Punktnotation. | M |
| FA-06 | **Felder dynamisch verwalten**: Feld setzen/überschreiben (auch mit neuem Typ), Feld löschen, Feld umbenennen – einzeln oder im Batch, für neue und bestehende Dokumente. | M |
| FA-07 | Dokument **aktualisieren** (feldweise, partiell) und **ersetzen** (komplett); **Upsert** (insert-or-update nach ID oder Query). | M |
| FA-08 | Dokument(e) **löschen**: nach ID, nach Query; Rückgabe der Anzahl betroffener Dokumente. | M |
| FA-09 | **Bulk-Operationen**: mehrere Dokumente in einem Aufruf einfügen/löschen/aktualisieren. | S |
| FA-10 | **Indizes** pro Collection und Feld: anlegen, auflisten, löschen; beschleunigte Gleichheits-/Bereichsabfragen. | S |
| FA-11 | **Iteration** über alle Dokumente einer Collection (Iterator/Cursor), inkl. Gefilterter mit Query. | M |
| FA-12 | **Transaktionen** auf Collection-Ebene: mehrere Änderungen atomar (alles oder nichts), mit Rollback. | S |
| FA-13 | **Aggregationen**: `count`, `min`, `max`, `sum`, `avg` über Felder; optional `groupBy`. | K |
| FA-14 | **Snapshot/Export und Import** der gesamten Database oder einzelner Collections in ein serialisiertes Format (JSON oder binär), um In-Memory-Daten zu sichern/wiederherzustellen. | K |
| FA-15 | **Beobachtung**: Callback/Observer, der bei insert/update/delete einer Collection benachrichtigt wird. | K |
| FA-16 | **Thread-Safety**: parallele Lesezugriffe ohne gegenseitigen Ausschluss; Schreibzugriffe serialisiert (RW-Lock); optional konsistente Snapshot-Isolation innerhalb einer Transaktion. | S |
| FA-17 | **Objektorientierte C++-API** (siehe Abschnitt 6): Klassen `Database`, `Collection`, `Document`, `Value`, `Query`, `Index`; RAII, Smart Pointer, `std::optional`, Exceptions statt Fehlercodes. | M |
| FA-18 | **Referenzen zwischen Collections/Dokumenten** als Erweiterungsmodul (`ReferenceIndex`, ohne Kernänderung): name-basiert/relativ auflösbar, in beide Richtungen abfragbar (indexbeschleunigt), inkl. Dangling-Erkennung und -Bereinigung; snapshot-fähig. | K |
| FA-19 | **Verschlüsselte binäre Snapshots** (passwortbasiert): `saveToFileEncrypted`/`loadFromFileEncrypted` – PBKDF2-HMAC-SHA256 + ChaCha20 + HMAC-SHA256 (encrypt-then-MAC), ohne externe Abhängigkeiten; siehe `docs/encrypted-snapshot.md`. | K |
| FA-20 | **Optionales striktes Schema pro Collection**: pro Feld Datentyp als Metadatum (inkl. `notNull`, verschachtelte Unter-Schemas, Array-Elementtyp). Ist ein Schema gesetzt, darf kein Feld undefiniert sein; Prüfung bei insert/update/replace/upsert/loadJson (`SchemaError`). Ohne Schema bleibt die Collection schema-los. Snapshot-fähig; siehe `docs/schema.md`. | K |
| FA-23 | **Qt/qmake-Einbindung**: `InMemDB.pro` (statische Library per qmake, ohne Qt-Abhängigkeit) und `InMemDB.pri` (Einbindung in fremde qmake-Projekte: Variante A vorgebaute Lib linken, Variante B Quellen direkt mitkompilieren); optionales SQLite-Modul; Beispiel `examples/qt_hello/`, CI-Job. Siehe `docs/qt-qmake.md`. | K |
| FA-22 | **Visualisierung des Datenbank-Graphs** (abhängigkeitsfrei, LLM-freundlich): Collections als Knoten (inkl. optionalem FA-20-Schema, FA-10-Indizes, Dokumentanzahl), FA-18-Referenzen als Kanten (inkl. Dangling-Markierung); Ausgabe als JSON, Mermaid und Markdown – aus laufender Database (`DbGraph::fromDatabase`) oder aus FA-14-Snapshot-Datei (`DbGraph::fromSnapshotFile`); CLI-Tool `inmemdb_graph`. Siehe `docs/db-graph.md`. | K |
| FA-21 | **Import aus externen Datenquellen**: abstrakte Quell-Schnittstelle (`ImportSource`: Tabellen-Metadaten + zeilenweises Lesen), zentrale Factory/Registry (`ImportRegistry`: Ableitungen per Name + Dateiendung anmelden/verwalten, Auto-Erkennung per Endung) und generischer `Importer`, der Tabellen auf Collections, Spalten auf Felder, Primärschlüssel auf ein optionales `pk`-Feld und (unique) Indizes abbildet; optionales Schema wird aus den Spaltentypen generiert. Eingebaute Quellen ohne externe Abhängigkeiten: `CsvImportSource`, `JsonImportSource` (JSON/JSONL), `InmemdbSnapshotSource` (Reimport eigener Snapshots inkl. Schema/Indizes), `SqlDumpImportSource`, `BsonImportSource`; optional `SqliteImportSource` (Build-Option `INMEMDB_WITH_SQLITE=ON`); CLI-Tool `inmemdb_import`. Siehe `docs/import.md`. | K |
| FA-24 | **Collections zusammenführen (Merge)**: `Database::mergeCollections(target, sources, opts, refs*)` – jedes Quell-Dokument mit neuer ID ins Ziel (IDs sind pro Collection vergeben), optional Herkunftsfeld `_source`, Index-Übernahme (Union), optional Quell-Collections löschen (`dropSources`) inkl. Umschreiben der FA-18-Referenzen (`ReferenceIndex::rewriteKeys`, sonst dangling). Atomar: Schema-/Unique-Verstoß → kompletter Rollback. Inkl. Spill-Dokumenten (v0.18) und Observer-Events. Siehe `docs/merge-collections.md`. | K |
| FA-25 | **TTL / Auto-Expiry** (opt-in, default aus): Verfallszeit pro Dokument (`expiresAt`, Unix-Sekunden) via `Document::expiresIn(sek)`/`expiresAt(unix)` und `Collection::expiresIn/expiresAt(id, …)`; lazy Sweep bei Lesezugriffen + manueller Sweep `expireNow()`; Collection-Default via `setDefaultExpiresIn(sek)`; inkl. Spill (v0.18), Tx-Guard (kein Sweep mitten in Tx), Snapshot, Metrik `expiredDocs`. Siehe `docs/ttl-expiry.md`. | K |

> **Verbindliche Ausprägung von FA-05 (Query-API):** typsicher Ausdruckbaum mit
> Operator-Overloading, z. B. `(Field("age") >= 18) && Field("tags").contains("cpp")`.
> Details und Semantik siehe [`docs/query-api.md`](docs/query-api.md).
> Eine String-DSL (`find("age >= 18 && ...")`) war Kann-Feature und ist **in v0.14 umgesetzt** –
> siehe [`docs/query-dsl.md`](docs/query-dsl.md).

---

## 5. Nicht-funktionale Anforderungen

| ID | Anforderung | Prio |
|---|---|---|
| NFA-01 | **Performance**: `findById` und indexierte Lookups in O(1)–O(log n); ungefilterte Scans O(n) (akzeptabel für In-Memory). Typische Einzeloperationen im Mikrosekundenbereich. | M |
| NFA-02 | **Speicher**: ausschließlich RAM; Zielordnung: Millionen Dokumente mit typischer Größe (einige KB) auf handelsüblicher Hardware. | S |
| NFA-03 | **Erweiterbarkeit**: neue Werttypen durch Ableitung von einer `Value`-Basisklasse registrierbar; neue Query-Operatoren erweiterbar (v0.14: String-DSL `find("age >= 18 && ...")` via `parseQuery`, `QueryFunctionRegistry` für eigene Function-Operatoren wie `matches`/`in`, `Expression::predicate`, `@relation.path`-Joins über FA-18); keine Änderung am Kern für Erweiterungen nötig (Open/Closed). Siehe `docs/query-dsl.md`. Foundations v0.27 (allgemein nützlich, z. B. für Relationship-Discovery): `memdb::ValueLess`, `Collection::distinctValues` (index-aware), `Collection::sample` (reproduzierbares Sampling), `AggOp::Distinct`, `Expression::isMatchAll`. | M |
| NFA-04 | **API-Ergonomie**: intuitive, moderne C++-Syntax (z. B. Initializer-Listen für Dokumente, `std::string_view`, `std::optional`); sprechende Methodennamen. | M |
| NFA-05 | **Zuverlässigkeit/Speicher**: keine Lecks (RAII, Smart Pointer), starke Exception-Garantie bei fehlgeschlagenen Updates; ungültige Zugriffe werfen typisierte Exceptions. | M |
| NFA-06 | **Portabilität & Build**: plattformunabhängiger Code, Build über CMake; Kern ohne externe Pflichtabhängigkeiten. Auslieferung als **statische Library mit Pimpl** (ABI-stabile öffentliche Header, geringe Compile-Zeiten der Konsumenten) – Details siehe `docs/architecture.md`. | M |
| NFA-07 | **Testbarkeit**: Unit-Tests für alle Kernpfade (Empfehlung: Catch2 oder GoogleTest als Dev-Dependency); Abdeckung der CRUD-, Query- und Nebenläufigkeitsfälle. | S |
| NFA-08 | **Dokumentation**: Doxygen-kommentierte öffentliche API, README mit Quickstart, mindestens ein lauffähiges Beispiel. | S |
| NFA-09 | **Lizenz**: projektverträgliche Open-Source-Lizenz (z. B. MIT). | K |

---

## 6. Schnittstellen

### 6.1 C++-API (Kern)
Namespace `memdb`. Wesentliche Klassen:

| Klasse | Verantwortung |
|---|---|
| `memdb::Database` | Einstiegspunkt; verwaltet Collections; Transaktions-Kontext. |
| `memdb::Collection` | CRUD für Dokumente; Query-Ausführung; Indexverwaltung; Transaktionen. |
| `memdb::Document` | Feld-Container (Key → `Value`); dynamische Felder; ID; verschachtelte Dokumente. |
| `memdb::Value` | Varianten-Typ (`std::variant`-basiert) für alle Feldtypen inkl. Erweiterbarkeit. |
| `memdb::Query` / `Expression` | Typsichere, baumartige Abfrageausdrücke (Operatoren erweiterbar). |
| `memdb::Cursor` / `Iterator` | Sequentielle/ gefilterte Iteration über Dokumente. |
| `memdb::Index` | Sekundärindex pro Feld. |
| `memdb::Transaction` | Atomare Ausführung mehrerer Änderungen. |
| `memdb::DocumentId` | Typsicherer Dokument-Identifikator. |

### 6.2 Serialisierung (Kann)
- Export/Import als JSON (über optionale Abhängigkeit, z. B. nlohmann/json) oder kompaktes binäres Format.
- Optionaler Validierungs-Hook pro Collection (Schema-Check bei insert/update).

### 6.3 Explizit keine Schnittstellen
- Kein SQL-Parser, kein Netzwerkprotokoll, keine CLI (Out of Scope).

---

## 7. Qualitätsanforderungen

- Automatisierte Unit- und Integrationstests in CI (GitHub Actions o. Ä.) für alle unterstützten Plattformen/Compiler.
- Code-Reviews vor Merge; statische Analyse (z. B. clang-tidy) empfohlen.
- Doxygen-Dokumentation der öffentlichen API; README mit Build- und Nutzungsanleitung.
- Beispiele: CRUD-Grundlagen, dynamische Felder, verschachtelte Queries, Transaktion, Snapshot.

---

## 8. Randbedingungen

- Sprache: C++17 minimum, C++20-Features wo sinnvoll.
- Build-System: CMake ≥ 3.20.
- Keine Pflichtabhängigkeiten im Kern; Dev-Dependencen (Test-Framework, JSON) nur für Tests/optionale Features.
- Entwicklung auf Linux; Portierung auf Windows/macOS vorgesehen.

---

## 9. Risiken

| Risiko | Auswirkung | Gegenmaßnahme |
|---|---|---|
| Datenverlust bei Prozessende (In-Memory) | Hoch für produktive Daten | Snapshot/Export (FA-14) dokumentieren und früh umsetzen |
| Unbegrenzter RAM-Verbrauch bei großen Datenmengen | Absturz bei Erschöpfung | Speicherüberwachung, dokumentierte Kapazitätsgrenzen; später Out-of-Core als Ausblick |
| Typsicherheit bei dynamischen Feldern | Laufzeitfehler in der Anwendung | Strikte Laufzeit-Typprüfung in `Value`, typisierte Exceptions, optional Validierungsschema |
| Komplexität der Nebenläufigkeit | Dateninkonsistenz, Deadlocks | Klare RW-Locking-Strategie, kurze kritische Abschnitte, Tests unter `TSan` |
| API-Drift bei Erweiterbarkeit | Breaking Changes | Stabile Basisklassen, Erweiterungen über Ableitung/Registrierung |

---

## 10. Ausblick (nicht in v1)

- Persistente Speicherung (Append-only-Log, Memory-Mapped Files).
- Eingebettete Referenzen zwischen Dokumenten (statt Kopie) mit optionalem "Join" bei Abfrage. Umgesetzt: v0.8 FA-18 (`ReferenceIndex`, `docs/references.md`), v0.17 index-beschleunigter Join bei Abfrage (`ReferenceIndex::joinIds`/`join`; DSL-seitig `@relation.path`, siehe `docs/query-dsl.md`).
- Out-of-Core-/Hybrid-Modus für Daten größer als RAM. **Umgesetzt (v0.18, opt-in):** `Collection::setOverflow(spillFile, maxInMemory)` lagert älteste Dokumente in eine Spill-Datei aus und liest sie on-demand; siehe `docs/hybrid-outofcore.md`.
- Bekannte Performance-Reste. **Umgesetzt (v0.19):** Transaktions-Backup per Copy-on-Write-Undo (O(1) Begin/Commit statt O(n) Voll-Backup: 835 ms → 0,3 ms bei 1 Mio. Docs), JSON-Parser-Fastpaths (String-Run-Append, `std::from_chars`: 80-MB-Snapshot 2,36 s → 1,44 s), Observer-Event-Reservierung bei `insertBulk`; Rollback baut Indizes nur bei Bedarf neu (1,91 s → 0,56 s). Siehe `docs/transactions.md`.
- qmake-Variante mit Qt-Modell-Adapter (ohne harte Abhängigkeiten). **Umgesetzt (v0.23):** `include/memdb/qt/memdb_qt_model.hpp` mit `memdb::qt::MemDbTableModel` (QAbstractTableModel, nur QtCore; DSL-Query-Filter, setData schreibt zurück, Auto-Refresh per Observer via queued Connection); `InMemDB.pri` setzt `INMEMDB_WITH_QT` automatisch bei `QT += core`; Beispiel `examples/qt_table` (CI-Smoke headless). Siehe `docs/qt-qmake.md`.
- Metriken/Diagnose. **Umgesetzt (v0.22):** `Collection::metrics()`/`resetMetrics()` und `Database::metrics()` (Aggregat) mit Zählern für Schreib-/Lesezugriffe, Index-Nutzung (`indexQueries`, `scannedDocs`), Query-Dauer, Spill-IO, Tx-Commits/Rollbacks und Change-Events; siehe `docs/metrics.md`.
- Bulk-Update via DSL mit Projektion. **Umgesetzt (v0.21):** `Collection::update(queryDsl, setDsl)` und `Transaction::update(dsl, setDsl)`; Set-DSL (`parseUpdateSet`) mit Literalen, Feld-Referenzen, Arithmetik (`+ - * / %`, int bleibt int), String-Konkatenation, gepunkteten Zielpfaden via `Document::setPath`; Snapshot-Semantik pro Dokument; `@relation`-Query mit `Database&`-Variante. Siehe `docs/query-dsl.md`.
- Bessere `matches`-Regex. **Umgesetzt (v0.20):** Thompson-NFA um `{n}` `{n,}` `{n,m}`, Kurzschriften `\d \D \s \S \w \W` (auch in Klassen), `(?:...)`-Gruppen und Lazy-Quantoren erweitert; Visited-Set im NFA beseitigt Hang bei verschachtelten Schleifen (`(a*)*`); Fehlermeldungen mit Position; 116 Checks in `tests/test_regex.cpp`. Siehe `docs/query-dsl.md`.
- Netzwerk-Schnittstelle / Server-Modus.

---

## 11. Glossar

| Begriff | Bedeutung |
|---|---|
| Dokument | Eine Entität als Menge von Feld-Wert-Paaren mit eindeutiger ID. |
| Collection | Benannte, schema-lose Menge von Dokumenten. |
| Feld | Schlüssel-Wert-Paar innerhalb eines Dokuments; jederzeit erweiterbar. |
| Value | Dynamisch typisierter Wert eines Feldes. |
| Query | Abfrageausdruck zur Selektion von Dokumenten. |
| Index | Datenstruktur zur Beschleunigung von Abfragen auf einem Feld. |
| Snapshot | Serialisierter Zustand der Database zum Sichern/Wiederherstellen. |
| In-Memory | Datenhaltung ausschließlich im Arbeitsspeicher. |
| Schema-less | Kein vorab definiertes Struktur-Schema; Struktur ergibt sich aus den Daten. |

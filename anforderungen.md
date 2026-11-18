# Anforderungen

## In-Memory Non-Relational Database System (C++, objektorientiert)

| Feld | Wert |
|---|---|
| Projekttitel | InMemDB – In-Memory Document Store |
| Version | 1.0.0 |
| Status | fortgeschrieben; FA-01…FA-34 und NFA-01…NFA-09 umgesetzt bzw. beschlossen |
| Sprache | Deutsch |
| Technologie | C++ (mindestens C++17, C++20 wünschenswert), CMake |
| Bereitstellungsform | Eingebettete Bibliothek als **kompilierte statische Library mit Pimpl-Idiom** (öffentliche Header unter `include/memdb/`, Implementierung gekapselt in `src/`); kein Server, kein Netzwerk. Header-only wurde verworfen – siehe `docs/de/internals/architecture.md`. |

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
| **Typ-Mapping** | Abbildung der Werttypen auf gängige Datenbanksysteme (SQL, MongoDB/BSON, JSON): siehe [`docs/de/guides/type-mapping.md`](docs/de/guides/type-mapping.md). |
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
| FA-16 | **Thread-Safety**: parallele Lesezugriffe ohne gegenseitigen Ausschluss; Schreibzugriffe serialisiert (RW-Lock); Transaktion hält exklusiven Lock bis commit/rollback – volle Serialisierung inkl. Leser, kein MVCC (M3, Doku v0.44 präzisiert); Nebenläufigkeit via `tests/test_concurrency.cpp` + CI-Jobs TSan & ASan/UBSan (v0.44, M2) verankert. | S |
| FA-17 | **Objektorientierte C++-API** (siehe Abschnitt 6): Klassen `Database`, `Collection`, `Document`, `Value`, `Query`, `Index`; RAII, Smart Pointer, `std::optional`, Exceptions statt Fehlercodes. | M |
| FA-18 | **Referenzen zwischen Collections/Dokumenten** als Erweiterungsmodul (`ReferenceIndex`, ohne Kernänderung): name-basiert/relativ auflösbar, in beide Richtungen abfragbar (indexbeschleunigt), inkl. Dangling-Erkennung und -Bereinigung; snapshot-fähig. | K |
| FA-19 | **Verschlüsselte binäre Snapshots** (passwortbasiert): `saveToFileEncrypted`/`loadFromFileEncrypted` – PBKDF2-HMAC-SHA256 + ChaCha20 + HMAC-SHA256 (encrypt-then-MAC), ohne externe Abhängigkeiten; siehe `docs/de/guides/encrypted-snapshot.md`. | K |
| FA-20 | **Optionales striktes Schema pro Collection**: pro Feld Datentyp als Metadatum (inkl. `notNull`, verschachtelte Unter-Schemas, Array-Elementtyp). Ist ein Schema gesetzt, darf kein Feld undefiniert sein; Prüfung bei insert/update/replace/upsert/loadJson (`SchemaError`). Ohne Schema bleibt die Collection schema-los. Snapshot-fähig; siehe `docs/de/guides/schema.md`. | K |
| FA-23 | **Qt/qmake-Einbindung**: `InMemDB.pro` (statische Library per qmake, ohne Qt-Abhängigkeit) und `InMemDB.pri` (Einbindung in fremde qmake-Projekte: Variante A vorgebaute Lib linken, Variante B Quellen direkt mitkompilieren); optionales SQLite-Modul; Beispiel `examples/qt_hello/`, CI-Job. Siehe `docs/de/guides/qt-qmake.md`. | K |
| FA-22 | **Visualisierung des Datenbank-Graphs** (abhängigkeitsfrei, LLM-freundlich): Collections als Knoten (inkl. optionalem FA-20-Schema, FA-10-Indizes, Dokumentanzahl), FA-18-Referenzen als Kanten (inkl. Dangling-Markierung); Ausgabe als JSON, Mermaid und Markdown – aus laufender Database (`DbGraph::fromDatabase`) oder aus FA-14-Snapshot-Datei (`DbGraph::fromSnapshotFile`); CLI-Tool `inmemdb_graph`. Siehe `docs/de/guides/db-graph.md`. | K |
| FA-21 | **Import aus externen Datenquellen**: abstrakte Quell-Schnittstelle (`ImportSource`: Tabellen-Metadaten + zeilenweises Lesen), zentrale Factory/Registry (`ImportRegistry`: Ableitungen per Name + Dateiendung anmelden/verwalten, Auto-Erkennung per Endung) und generischer `Importer`, der Tabellen auf Collections, Spalten auf Felder, Primärschlüssel auf ein optionales `pk`-Feld und (unique) Indizes abbildet; optionales Schema wird aus den Spaltentypen generiert. Eingebaute Quellen ohne externe Abhängigkeiten: `CsvImportSource`, `JsonImportSource` (JSON/JSONL), `InmemdbSnapshotSource` (Reimport eigener Snapshots inkl. Schema/Indizes), `SqlDumpImportSource`, `BsonImportSource`; optional `SqliteImportSource` (Build-Option `INMEMDB_WITH_SQLITE=ON`); CLI-Tool `inmemdb_import`. Siehe `docs/de/guides/import.md`. | K |
| FA-24 | **Collections zusammenführen (Merge)**: `Database::mergeCollections(target, sources, opts, refs*)` – jedes Quell-Dokument mit neuer ID ins Ziel (IDs sind pro Collection vergeben), optional Herkunftsfeld `_source`, Index-Übernahme (Union), optional Quell-Collections löschen (`dropSources`) inkl. Umschreiben der FA-18-Referenzen (`ReferenceIndex::rewriteKeys`, sonst dangling). Atomar: Schema-/Unique-Verstoß → kompletter Rollback. Inkl. Spill-Dokumenten (v0.18) und Observer-Events. Siehe `docs/de/guides/merge-collections.md`. | K |
| FA-25 | **TTL / Auto-Expiry** (opt-in, default aus): Verfallszeit pro Dokument (`expiresAt`, Unix-Sekunden) via `Document::expiresIn(sek)`/`expiresAt(unix)` und `Collection::expiresIn/expiresAt(id, …)`; lazy Sweep bei Lesezugriffen + manueller Sweep `expireNow()`; Collection-Default via `setDefaultExpiresIn(sek)`; inkl. Spill (v0.18), Tx-Guard (kein Sweep mitten in Tx), Snapshot, Metrik `expiredDocs`. Siehe `docs/de/guides/ttl-expiry.md`. | K |
| FA-26 | **Fremdschlüssel beim Import als Referenzen übernehmen**: Quellen liefern FK-Metadaten (`TableInfo::foreignKeys`; SQLite via `PRAGMA foreign_key_list`, SQL-Dump via `REFERENCES`/`FOREIGN KEY`/`CONSTRAINT … FOREIGN KEY`); `Importer` legt bei `ImportOptions::importForeignKeys` (Default an) FA-18-Referenzen an (Match über `refColumn`/`pk`/PK-Spalte, Relation = Constraint-Name bzw. `fk_<spalte>`); Stats `importedReferences`/`skippedForeignKeys`; CLI `--no-foreign-keys`. Siehe `docs/de/guides/import.md`. | K |
| FA-27 | **Relationship-Discovery** (ohne externe Abhängigkeiten): `discoverForeignKeys` (Werte-Überlappung, collection- und database-übergreifend via `std::vector<Collection>`), `discoverFunctionalDependencies` (X→Y via groupBy+Distinct), `discoverCorrelations` (Pearson + kategorial), `discoverNearDuplicates` (normalisierte Edit-Distanz), `discoverRelations` (kombiniert), `applyDiscovered` (FK-Befunde als FA-18-Referenzen), Reports `toJson`/`toMermaid`, CLI `inmemdb_discover`; Sampling via `Collection::sample`; v0.30: Kardinalität, Orphan-Report (`findOrphans`), Pfad-Suche (`findPaths`); v0.31: Datenprofil + Candidate Keys; v0.32: Cross-DB-Diff + Merge-Vorschläge; v0.33: Auto-Analyse `analyze()` + Health-Score + Report-Präsentation (Markdown/JSON/Mermaid/Konsole), CLI `--report`. Siehe `docs/de/guides/discovery.md` (API) und `docs/de/guides/discovery-guide.md` (erklärender Guide mit Beispiel-Graphen in `docs/images/`). | K |
| FA-28 | **Optionale Collection-Limits** (opt-in, Default unbegrenzt): `Collection::setLimits(CollectionLimits{maxDocuments, maxDocumentBytes, maxFieldsPerDocument, maxIndexes})` – Verstoß wirft `LimitError` **vor der Mutation** (insert/insertBulk/loadJson/update/replace/upsert/createIndex, Bulk atomar); Zähler `CollectionMetrics::limitViolations`; wirkt in Tx und inkl. Spill; Faustregeln siehe `docs/de/guides/limits.md`. | K |
| FA-29 | **Datencharakter-Kategorisierung** (ohne externe Abhängigkeiten): `assessCharacter(coll | vector | Database, DiscoveryOptions)` stuft jede Collection grob in eine von drei Daten-Kategorien ein – `DataCategory::Numeric` (Kategorie 1, >= 80 % numerische Felder), `Mixed` (Kategorie 2, Mischung Zahlen/Strings/Booleans) oder `Complex` (Kategorie 3, Arrays/Dokumente/Binär oder Typ-Drift >= 40 %) – inkl. `confidence`, Klartext-Begründung, Feld-Charakteristik (dominanter Typ, Typ-Reinheit, Drift) und Reports `toJson`/`toMarkdown`; als Vorab-Dispatcher für die passenden Analyse-Pipelines. Siehe `docs/de/guides/character.md`. | K |
| FA-30 | **Numerische Metadaten-Analyse** (ohne externe Abhängigkeiten), Kategorie-1-Pipeline zu FA-29: `analyzeNumeric(coll | vector | Database, NumericAnalysisOptions)` interpretiert numerische Felder statistisch – Lage/Streuungsmaße, Quantile, Schiefe/Exzess-Kurtosis, Ganzzahl-Anteil und Diskretheit, Rundungs-Raster-Erkennung (Preis-/Cent-Raster), Sequenz-Charakter in Einfügereihenfolge (monoton, lückenlose ID-Sequenzen, mediane Schrittweite, Lücken), Benford-Analyse der ersten signifikanten Ziffer (MAD nach Nigrini) und paarweise Pearson-Korrelationen – plus Reports `toJson`/`toMarkdown`; interpretierende `note`-Hinweise pro Feld. Siehe `docs/de/guides/numeric-analysis.md`. | K |
| FA-31 | **Einfluss-Analyse** (ohne externe Abhängigkeiten), Vertiefung von FA-30: `analyzeInfluence(coll | vector | Database, InfluenceOptions)` deutet die statistischen Werte weiter und bestimmt dominante Felder – Hub über Pearson-Korrelationen (Summe/Mittel der |r|, starke Zusammenhänge ab 0.8), multiples R² je Feld gegen die übrigen (Complete-Cases-Regression, Ridge-Term gegen Kollinearität) und funktionale Abhängigkeiten (Evidenz aus wiederholten Quell-Werten, Coverage >= 0.99) – daraus Rollen `driver`/`dependent`/`mediator`/`independent`, `influenceScore`, `topDriver`, Klartext-Begründungen und Reports `toJson`/`toMarkdown`; Korrelation belegt keine Kausalität. Siehe `docs/de/guides/influence.md`. | K |
| FA-32 | **Cross-Collection-Einfluss-Analyse** (ohne externe Abhängigkeiten), Fortsetzung von FA-31: `analyzeCrossInfluence(Database, CrossInfluenceOptions)` berechnet Abhängigkeiten zwischen Feldern verschiedener Collections – Collection-Paare werden über deklarierte `__references`-Relationen und entdeckte Fremdschlüssel (FA-27, Konfidenz-Schwelle) per Join verknüpft, die FA-31-Maschinerie (Pearson-Hub, multiples R² mit Ridge, funktionale Abhängigkeiten) läuft auf den verknüpften Zeilen – Rollen/`influenceScore`/Treiber je Collection-Paar, Collection-qualifizierte Feldnamen (`coll.feld`), Kardinalität aus A-Sicht (n:1/1:n), Guards (`minJoinPairs`, Join-Abdeckung), Unterdrückung der trivialen Join-Abhängigkeit und Reports `toJson`/`toMarkdown`. Siehe `docs/de/guides/cross-influence.md`. | K |
| FA-33 | **Gemischte Metadaten-Analyse** (ohne externe Abhängigkeiten), Kategorie-2-Pipeline zu FA-29: `analyzeMixed(coll | vector | Database, MixedAnalysisOptions)` interpretiert Felder gemäß ihrem dominanten Typ (Zahl/Text/Boolean, Tie-Break Zahl>Text>Boolean) – Zahl-Kennzahlen (min/max/mean/stddev/median, Ganzzahl-Anteil, Diskretheit), Text-Statistik (Längen, uniqueness, semantische Klasse kategorisch/Kennung/Freitext, Format-Hinweise Datum/Hex/zahlartig), Boolean-Anteile, Missing-Quote und Typ-Drift – plus paarweise Assoziationen (Pearson für Zahl/Boolean-Kombinationen, eta² für kategorisch gegen Zahl/Boolean, Cramérs V für kategorisch/kategorisch, mit Guards) und Reports `toJson`/`toMarkdown`; interpretierende `note`-Hinweise pro Feld. Siehe `docs/de/guides/mixed-analysis.md`. | K |
| FA-34 | **Komplexe Metadaten-Analyse** (ohne externe Abhängigkeiten), Kategorie-3-Pipeline zu FA-29: `analyzeComplex(coll | vector | Database, ComplexAnalysisOptions)` profiliert verschachtelte Strukturen rekursiv als Pfad-Profil in Punktnotation (`"adresse.plz"`, `"tags[]"`, `"items[].preis"`) – je Pfad Typ-Sichtungen mit dominantem Typ und Typ-Drift, Skalar-Distinct-Werte, Fehlstellen und Missing-Quote – plus Schema-Varianten (Fingerprint aus sortierten Pfaden) mit Schema-Konsistenz und Guards (`maxDepth`, `maxPathsPerCollection`, `maxSchemaVariants`, `maxDistinctPerPath`, `minSightingsPerPath`) und Reports `toJson`/`toMarkdown`; interpretierende `note`-Hinweise. Siehe `docs/de/guides/complex-analysis.md`. | K |

> **Verbindliche Ausprägung von FA-05 (Query-API):** typsicher Ausdruckbaum mit
> Operator-Overloading, z. B. `(Field("age") >= 18) && Field("tags").contains("cpp")`.
> Details und Semantik siehe [`docs/de/guides/query-api.md`](docs/de/guides/query-api.md).
> Eine String-DSL (`find("age >= 18 && ...")`) war Kann-Feature und ist **in v0.14 umgesetzt** –
> siehe [`docs/de/guides/query-dsl.md`](docs/de/guides/query-dsl.md).

---

## 5. Nicht-funktionale Anforderungen

| ID | Anforderung | Prio |
|---|---|---|
| NFA-01 | **Performance**: `findById` und indexierte Lookups in O(1)–O(log n); ungefilterte Scans O(n) (akzeptabel für In-Memory). Typische Einzeloperationen im Mikrosekundenbereich. | M |
| NFA-02 | **Speicher**: ausschließlich RAM; Zielordnung: Millionen Dokumente mit typischer Größe (einige KB) auf handelsüblicher Hardware. | S |
| NFA-03 | **Erweiterbarkeit**: neue Werttypen durch Ableitung von einer `Value`-Basisklasse registrierbar; neue Query-Operatoren erweiterbar (v0.14: String-DSL `find("age >= 18 && ...")` via `parseQuery`, `QueryFunctionRegistry` für eigene Function-Operatoren wie `matches`/`in`, `Expression::predicate`, `@relation.path`-Joins über FA-18); keine Änderung am Kern für Erweiterungen nötig (Open/Closed). Siehe `docs/de/guides/query-dsl.md`. Foundations v0.27 (allgemein nützlich, z. B. für Relationship-Discovery): `memdb::ValueLess`, `Collection::distinctValues` (index-aware), `Collection::sample` (reproduzierbares Sampling), `AggOp::Distinct`, `Expression::isMatchAll`. | M |
| NFA-04 | **API-Ergonomie**: intuitive, moderne C++-Syntax (z. B. Initializer-Listen für Dokumente, `std::string_view`, `std::optional`); sprechende Methodennamen. | M |
| NFA-05 | **Zuverlässigkeit/Speicher**: keine Lecks (RAII, Smart Pointer), starke Exception-Garantie bei fehlgeschlagenen Updates; ungültige Zugriffe werfen typisierte Exceptions. | M |

**Exception-Policy (v0.35, `docs/de/internals/error-handling.md`):** Exceptions bleiben für echte Fehler (Constraints, I/O, API-Missbrauch) sichtbar; abgefangen wird nur dort, wo sonst mitten in einer Operation abgebrochen würde – Query-Scan pro Dokument (`evalErrors`), Observer-Callbacks (`eventErrors`), Bulk-JSON-Load validiert vor Mutation (atomar), CLI-Tools melden sauber mit Exit-Code.
| NFA-06 | **Portabilität & Build**: plattformunabhängiger Code, Build über CMake; Kern ohne externe Pflichtabhängigkeiten. Auslieferung als **statische Library mit Pimpl** (ABI-stabile öffentliche Header, geringe Compile-Zeiten der Konsumenten) – Details siehe `docs/de/internals/architecture.md`. **Interne Eigenimplementierungen** (JSON-Parser `src/json.*`, Krypto PBKDF2/ChaCha20/HMAC-SHA256 `src/crypto.*`, Mini-RegEx `src/regex.*`) bleiben Default – externe Pendants (z. B. OpenSSL, PCRE2, nlohmann/json) sind höchstens als **Opt-in** hinter eigener CMake-Option + Präprozessor-Macro erlaubt (Vorbild: `INMEMDB_WITH_SQLITE`, ADR-02), niemals als Pflichtabhängigkeit; Shared-Lib-Deployment (DLL/SO neben der App) betrifft nur den Opt-in-Fall und liegt beim Anwender. | M |
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

### 6.1b Erweiterungsmodule (alle ohne Kernänderung, NFA-03)

| Modul | Header | Inhalt |
|---|---|---|
| Referenzen & Joins (FA-18) | `references.hpp` | `ReferenceIndex`: bidirektional, index-beschleunigter Join (`joinIds`/`join`, v0.17), Dangling-Bereinigung, snapshot-fähig |
| Import (FA-21/FA-26) | `import*.hpp` | `ImportSource` + `ImportRegistry` + `Importer`; Quellen: CSV, JSON/JSONL, Snapshot, SQL-Dump, BSON (alle ohne Deps), optional SQLite (`INMEMDB_WITH_SQLITE=ON`); FK-Import als Referenzen (v0.28) |
| Datenbank-Graph (FA-22) | `db_graph.hpp` | `DbGraph`: JSON/Mermaid/Markdown aus laufender DB oder Snapshot-Datei |
| Discovery (FA-27) | `discovery.hpp` | `discoverForeignKeys`/`discoverRelations`/`applyDiscovered`, Orphans, Pfade, Profil, Candidate Keys, Diff, Merge-Vorschläge, `analyze()` + Health-Score + Reports (v0.29–v0.33); Guide: `docs/de/guides/discovery-guide.md` |
| Datencharakter (FA-29) | `character.hpp` | `assessCharacter`: grobe Kategorisierung `Numeric`/`Mixed`/`Complex` inkl. confidence, Feld-Charakteristik, `toJson`/`toMarkdown`; Guide: `docs/de/guides/character.md` |
| Numerik-Analyse (FA-30) | `numeric_analysis.hpp` | `analyzeNumeric`: Kennzahlen/Quantile/Skew/Kurtosis, Diskretheit, Raster, Sequenz (ID-artig), Benford, Pearson-Paare, `toJson`/`toMarkdown`; Guide: `docs/de/guides/numeric-analysis.md` |
| Einfluss-Analyse (FA-31) | `influence.hpp` | `analyzeInfluence`: dominante Felder via Hub/R²/funktionale Abhängigkeiten, Rollen driver/dependent/mediator/independent, `topDriver`, `toJson`/`toMarkdown`; Guide: `docs/de/guides/influence.md` |
| Cross-Collection-Einfluss (FA-32) | `cross_influence.hpp` | `analyzeCrossInfluence`: Abhängigkeiten über Collection-Grenzen via Join aus `__references` + entdeckten FKs, FA-31-Rollen je Paar, Kardinalität n:1/1:n, Guards, `toJson`/`toMarkdown`; Guide: `docs/de/guides/cross-influence.md` |
| Gemischte Analyse (FA-33) | `mixed_analysis.hpp` | `analyzeMixed`: dominanter Typ je Feld, Zahl-Kennzahlen, Text-Klassen (kategorisch/Kennung/Freitext), Format-Hinweise, Assoziationen (Pearson/eta²/Cramérs V), `toJson`/`toMarkdown`; Guide: `docs/de/guides/mixed-analysis.md` |
| Komplexe Analyse (FA-34) | `complex_analysis.hpp` | `analyzeComplex`: rekursives Pfad-Profil (Punktnotation, `"[]"`-Elementpfade), Typ-Sichtungen und Typ-Drift, Schema-Varianten mit Konsistenz, Guards, `toJson`/`toMarkdown`; Guide: `docs/de/guides/complex-analysis.md` |
| Metriken (v0.22) | `metrics.hpp` | `Collection::metrics()` / `Database::metrics()` |
| Qt-Adapter (FA-23) | `qt/memdb_qt_model.hpp` | `memdb::qt::MemDbTableModel` (nur QtCore, nur bei `INMEMDB_WITH_QT`) |
| String-DSL (NFA-03) | `query_dsl.hpp` | `parseQuery`, `QueryFunctionRegistry`, `matches`/`in`, `@relation.path` |

### 6.2 Serialisierung & Persistenz (umgesetzt, ohne externe Abhängigkeit)
- **JSON-Snapshot** mit **eigenem Parser** (`src/json.*`, locale-unabhängig via `std::to_chars`/`from_chars` seit v0.23.1): `toJson`/`fromJson`/`saveToFile`/`loadFromFile` (FA-14), inkl. Schema (FA-20) und Indizes; Reimport als Import-Quelle.
- **Verschlüsselte Snapshots** (FA-19): `saveToFileEncrypted`/`loadFromFileEncrypted` mit PBKDF2-HMAC-SHA256 + ChaCha20 + HMAC-SHA256 (encrypt-then-MAC), eigene Implementierung in `src/crypto.*` – siehe `docs/de/guides/encrypted-snapshot.md`.
- **Import-Formate** (FA-21): CSV, JSON/JSONL, SQL-Dump, BSON, eigener Snapshot; optional SQLite. Siehe `docs/de/guides/import.md`.
- **Reports** (FA-22/FA-27): JSON, Mermaid, Markdown – z. B. `inmemdb_discover --report`.
- Optionaler Validierungs-Hook pro Collection: striktes Schema (FA-20, `SchemaError` bei insert/update/replace/upsert/loadJson).

### 6.3 CLI-Tools (separate Binärdateien, nicht Teil der Library) + explizit keine Schnittstellen
- Tools (gebaut aus `tools/`, null Abhängigkeiten außer optional SQLite): `inmemdb_import` (FA-21), `inmemdb_graph` (FA-22), `inmemdb_discover` (FA-27, inkl. `--report` v0.33).
- **Out of Scope bleibt:** kein SQL-Parser, kein Netzwerkprotokoll/Server-Modus, keine Multi-User-Verteilung.

---

## 7. Qualitätsanforderungen

- Automatisierte Unit- und Integrationstests in CI (GitHub Actions o. Ä.) für alle unterstützten Plattformen/Compiler.
- Code-Reviews vor Merge; statische Analyse (z. B. clang-tidy) empfohlen.
- Doxygen-Dokumentation der öffentlichen API; README mit Build- und Nutzungsanleitung.
- Beispiele: CRUD-Grundlagen, dynamische Felder, verschachtelte Queries, Transaktion, Snapshot.
- Projektdokumentation: [README](README.md) (Landing-Page), [Doku-Index](docs/de/README.md) +
  [Getting Started](docs/de/getting-started.md) unter `docs/`, Guides unter `docs/de/guides/`,
  Internes (ADRs, Fehlerbehandlung, Härtung, Performance) unter `docs/de/internals/`;
  Änderungshistorie in [CHANGELOG.md](CHANGELOG.md).

---

## 8. Randbedingungen

- Sprache: C++17 minimum, C++20-Features wo sinnvoll.
- Build-System: CMake ≥ 3.20.
- Keine Pflichtabhängigkeiten im Kern; die Tests sind standalone (eigenes CHECK-Makro, kein Test-Framework). Optionale Deps nur hinter eigener CMake-Option (Default OFF): `libsqlite3` für `-DINMEMDB_WITH_SQLITE=ON`, Doxygen für `-DINMEMDB_BUILD_DOCS=ON`, Qt nur für den optionalen Modell-Adapter. Siehe ADR-02 (`docs/de/internals/architecture.md`) zu Krypto/RegEx/JSON.
- Entwicklung auf Linux; Portierung auf Windows/macOS vorgesehen.

---

## 9. Risiken

| Risiko | Auswirkung | Gegenmaßnahme |
|---|---|---|
| Datenverlust bei Prozessende (In-Memory) | Hoch für produktive Daten | Snapshot/Export (FA-14) dokumentieren und früh umsetzen |
| Unbegrenzter RAM-Verbrauch bei großen Datenmengen | Absturz bei Erschöpfung | Hybrid/Out-of-Core umgesetzt (v0.18, opt-in `setOverflow`): Spill-to-Disk + Metriken (`docs/de/guides/hybrid-outofcore.md`, `docs/de/guides/metrics.md`) |
| Typsicherheit bei dynamischen Feldern | Laufzeitfehler in der Anwendung | Strikte Laufzeit-Typprüfung in `Value`, typisierte Exceptions, optional Validierungsschema |
| Komplexität der Nebenläufigkeit | Dateninkonsistenz, Deadlocks | Klare RW-Locking-Strategie, kurze kritische Abschnitte, Tests unter `TSan` |
| API-Drift bei Erweiterbarkeit | Breaking Changes | Stabile Basisklassen, Erweiterungen über Ableitung/Registrierung |

---

## 10. Ausblick (nicht in v1)

- Persistente Speicherung (Append-only-Log, Memory-Mapped Files).
- Eingebettete Referenzen zwischen Dokumenten (statt Kopie) mit optionalem "Join" bei Abfrage. Umgesetzt: v0.8 FA-18 (`ReferenceIndex`, `docs/de/guides/references.md`), v0.17 index-beschleunigter Join bei Abfrage (`ReferenceIndex::joinIds`/`join`; DSL-seitig `@relation.path`, siehe `docs/de/guides/query-dsl.md`).
- Out-of-Core-/Hybrid-Modus für Daten größer als RAM. **Umgesetzt (v0.18, opt-in):** `Collection::setOverflow(spillFile, maxInMemory)` lagert älteste Dokumente in eine Spill-Datei aus und liest sie on-demand; siehe `docs/de/guides/hybrid-outofcore.md`.
- Bekannte Performance-Reste. **Umgesetzt (v0.19):** Transaktions-Backup per Copy-on-Write-Undo (O(1) Begin/Commit statt O(n) Voll-Backup: 835 ms → 0,3 ms bei 1 Mio. Docs), JSON-Parser-Fastpaths (String-Run-Append, `std::from_chars`: 80-MB-Snapshot 2,36 s → 1,44 s), Observer-Event-Reservierung bei `insertBulk`; Rollback baut Indizes nur bei Bedarf neu (1,91 s → 0,56 s). Siehe `docs/de/guides/transactions.md`.
- qmake-Variante mit Qt-Modell-Adapter (ohne harte Abhängigkeiten). **Umgesetzt (v0.23):** `include/memdb/qt/memdb_qt_model.hpp` mit `memdb::qt::MemDbTableModel` (QAbstractTableModel, nur QtCore; DSL-Query-Filter, setData schreibt zurück, Auto-Refresh per Observer via queued Connection); `InMemDB.pri` setzt `INMEMDB_WITH_QT` automatisch bei `QT += core`; Beispiel `examples/qt_table` (CI-Smoke headless). Siehe `docs/de/guides/qt-qmake.md`.
- Metriken/Diagnose. **Umgesetzt (v0.22):** `Collection::metrics()`/`resetMetrics()` und `Database::metrics()` (Aggregat) mit Zählern für Schreib-/Lesezugriffe, Index-Nutzung (`indexQueries`, `scannedDocs`), Query-Dauer, Spill-IO, Tx-Commits/Rollbacks und Change-Events; siehe `docs/de/guides/metrics.md`.
- Bulk-Update via DSL mit Projektion. **Umgesetzt (v0.21):** `Collection::update(queryDsl, setDsl)` und `Transaction::update(dsl, setDsl)`; Set-DSL (`parseUpdateSet`) mit Literalen, Feld-Referenzen, Arithmetik (`+ - * / %`, int bleibt int), String-Konkatenation, gepunkteten Zielpfaden via `Document::setPath`; Snapshot-Semantik pro Dokument; `@relation`-Query mit `Database&`-Variante. Siehe `docs/de/guides/query-dsl.md`.
- Bessere `matches`-Regex. **Umgesetzt (v0.20, gehärtet v0.34):** Thompson-NFA um `{n}` `{n,}` `{n,m}`, Kurzschriften `\d \D \s \S \w \W` (auch in Klassen), `(?:...)`-Gruppen und Lazy-Quantoren erweitert; Visited-Set im NFA beseitigt Hang bei verschachtelten Schleifen (`(a*)*`); Fehlermeldungen mit Position; **v0.34: harte Limits gegen pathologische Patterns** (max. 4096 Zeichen, Verschachtelung max. 256, NFA max. 50000 Anweisungen, Match-Budget 100 Mio. Schritte → `std::invalid_argument`/`std::runtime_error` statt Segfault/Hang; früher: Segfault ab ~20000 Klammern, Hang bei `((a{100}){100}){100}`)) + `detail::Regex` kompiliert das Pattern einmalig pro Query statt pro Dokument; Fuzz-Test (deterministisch) in `tests/test_regex.cpp` (5921 Checks). Siehe `docs/de/guides/query-dsl.md`.
- Relationship-Discovery. **Umgesetzt (v0.29, FA-27):** `discoverForeignKeys`/`discoverFunctionalDependencies`/`discoverCorrelations`/`discoverNearDuplicates`/`discoverRelations` + `applyDiscovered`, CLI `inmemdb_discover`; Erweiterungen v0.30 (Kardinalität/Orphans/Pfade), v0.31 (Profil/Candidate Keys), v0.32 (Diff/Merge-Vorschläge), **v0.33: automatische Gesamtanalyse `analyze()` → `AnalysisReport` mit Health-Score (Ampel) + Präsentation `toMarkdown`/`toJson`/`toMermaid`/`printSummary`, CLI `--report <prefix>`**, **v0.40: Ausreißer-Erkennung `discoverOutliers` (z-Score + IQR/Tukey pro numerischem Feld, cross-database) als Health-Score-Faktor (Gewichte 0.30/0.15/0.20/0.20/0.15), CLI `--outliers`**, **v0.42: Confusion-Matrix `evaluateConfusion(rels, db, opts)` (FK-Befunde vs. `__references`) in `analyze(Database&)` (`AnalysisReport::confusion`), CLI `--confusion`**; siehe `docs/de/guides/discovery.md` (API) und `docs/de/guides/discovery-guide.md` (Guide mit Beispielen).
- Optionale externe Backends für Krypto/RegEx/JSON (OpenSSL, PCRE2, nlohmann/json). **Beschlossen (ADR-02, `docs/de/internals/architecture.md`):** Default bleiben die eigenen Implementierungen; externe Libs nur als Opt-in hinter CMake-Option + Macro (nie Pflicht, NFA-06); Shared-Lib-Deployment nur im Opt-in-Fall durch den Anwender.
- Sortierung + Limit/Offset in Queries (`orderBy`, `limit`, `offset`).
- SQL-Dump-**Export** + CLI `inmemdb_export` (Gegenstück zu `SqlDumpImportSource`).
- Ausreißer-Erkennung im Discovery (z-Score/IQR pro numerischem Feld, als Health-Score-Faktor). **Umgesetzt (v0.40):** `discoverOutliers(coll | colls, opts)` mit `DiscoveryOptions::outlierZ/outlierIqrK/minOutlierSamples`, `AnalysisReport::outliers`, Health-Score-Gewichte 0.30/0.15/0.20/0.20/0.15, CLI `--outliers`; Tests `tests/test_discovery6.cpp` (26 Checks); siehe `docs/de/guides/discovery.md` §v0.40 und `docs/de/guides/discovery-guide.md` §4.16.
- Netzwerk-Schnittstelle / Server-Modus.

---

## 10.5 Änderungshistorie (Auszug)

| Version | Inhalt |
|---|---|
| v0.13 | Import-Modul (FA-21): Registry + CSV/JSON/JSONL/Snapshot/SQL-Dump/BSON, optional SQLite, CLI `inmemdb_import` |
| v0.14 | String-DSL (`parseQuery`, `QueryFunctionRegistry`, `matches`/`in`, `@relation.path`-Joins) |
| v0.15 | Datenbank-Graph (FA-22): JSON/Mermaid/Markdown, CLI `inmemdb_graph` |
| v0.16 | Qt/qmake-Einbindung (FA-23): `InMemDB.pro`/`InMemDB.pri`, Beispiel `qt_hello` |
| v0.17 | Index-beschleunigter Join bei Abfrage (`ReferenceIndex::joinIds`/`join`) |
| v0.18 | Hybrid / Out-of-Core (Spill-to-Disk, opt-in) |
| v0.19 | Performance-Reste: CoW-Transaktions-Backup, JSON-Parser-Fastpaths, Event-Reservierung |
| v0.20 | Bessere `matches`-Regex (`{n,m}`, `\d\w\s`, `(?:...)`, lazy, kein Hang) |
| v0.21 | Bulk-Update via DSL mit Projektion (`update(queryDsl, setDsl)`) |
| v0.22 | Metriken & Diagnose (`Collection::metrics()`/`Database::metrics()`) |
| v0.23 | Qt-Modell-Adapter `memdb::qt::MemDbTableModel` (nur QtCore, ohne harte Abhängigkeit) |
| v0.23.1 | Bugfix: locale-unabhängige Zahlen (de_DE → immer Dezimalpunkt) |
| v0.24 / v0.25 | Collections mergen (FA-24: `mergeCollections`) + `mergeAllCollections` |
| v0.26 | TTL / Auto-Expiry (FA-25, opt-in: `expiresIn`/`expiresAt`, `expireNow()`) |
| v0.27 | Foundations: `ValueLess`, `distinctValues` (index-aware), `sample`, `AggOp::Distinct`, `Expression::isMatchAll` |
| v0.28 | Fremdschlüssel beim Import als FA-18-Referenzen (FA-26; SQLite `PRAGMA foreign_key_list`, SQL-Dump `REFERENCES`/`FOREIGN KEY`, CLI `--no-foreign-keys`) |
| v0.29 | Relationship-Discovery (FA-27): FK/FD/Korrelation/Near-Duplicates, cross-database, JSON/Mermaid-Report, CLI `inmemdb_discover` |
| v0.30 | Discovery-Erweiterungen: Kardinalität (1:1/n:1/1:n/n:m) im FK-Report, Orphan-Report (`findOrphans`/`findOrphansForRelations`), Pfad-Suche (`findPaths`), CLI `--orphans`/`--path` |
| v0.31 | Discovery: Datenprofil (`profileCollection`/`toJson`) + Candidate-Key-Discovery (`discoverCandidateKeys`) |
| v0.32 | Discovery: Cross-DB-Diff (`diffCollections`/`diffDatabases`/`toJson(DatabaseDiff)`) + Merge-Vorschläge (`suggestMerges`, Jaccard + Werte-Overlap, cross-database, passend zu FA-24), CLI `--diff`/`--suggest-merges` |
| v0.33 | Discovery: automatische Gesamtanalyse (`analyze()` → `AnalysisReport`: Profile + Relationen + Orphans + Merge-Vorschläge + Pfade) mit **Health-Score 0–100 (Ampel 🟢🟡🔴)** und vier Präsentationen (`toMarkdown`/`toJson`/`toMermaid`/`printSummary`), cross-database; CLI `inmemdb_discover --report <prefix>` schreibt `.report.md`/`.relations.json`/`.graph.mmd`; Doku: ausführlicher Guide `docs/de/guides/discovery-guide.md` + Beispiel-Graphen (`docs/images/*.svg`) |
| v0.34 | Mini-RegEx gehärtet: harte Limits (Pattern ≤ 4096 Zeichen, Verschachtelung ≤ 256, NFA ≤ 50000 Anweisungen, Match-Budget 100 Mio. Schritte) — pathologische Patterns werfen `std::invalid_argument`/`std::runtime_error` statt Segfault/Hang; `detail::Regex` (kompiliertes Pattern, thread-sicher) wird von `matches` einmalig pro Query genutzt; Fuzz-Test; `docs/de/guides/query-dsl.md` dokumentiert Syntax, Full-Match-Semantik, Byte/UTF-8-Verhalten und Grenzen (keine Backrefs/Lookaround/`\b`) |
| v0.35 | Exception-Policy (`docs/de/internals/error-handling.md`): Query-Scan fängt Eval-Fehler pro Dokument ab (`metrics().evalErrors`, Dokument = Nicht-Treffer), Observer-Exceptions werden pro Callback abgefangen (`metrics().eventErrors`, Schreib-OP und restliche Observer laufen weiter), `loadJson`/`fromJson` validieren vor Mutation (bei kaputtem Element bleibt alles unverändert, kein Partial State), alle CLI-Tools fangen `std::exception` inkl. Options-Parsing (sauberer Exit-Code statt terminate); echte Fehler (Unique/Schema/Crypto/Datei) werfen weiterhin; Tests `tests/test_error_handling.cpp` (32 Checks) |
| v0.36 | **MessageHandler-Singleton** (`include/memdb/message_handler.hpp` + `src/message_handler.cpp`): eigene Handler via `subscribe()`/`unsubscribe()`, Level-Filter (`setMinLevel`), thread-sicheres `publish()` (throwende Handler werden abgefangen, `handlerErrors()`), Zähler `count()/totalCount()`; die Abfang-Stellen der Exception-Policy v0.35 melden dorthin (`query-eval`, `observer`, `load`, `import`, `regex`, `cli`); CLI-Tools melden Fehler über den Handler (stderr-Abonnement); Tests `tests/test_message_handler.cpp` (30 Checks); Doku `docs/de/internals/error-handling.md` |
| v0.37 | JSON-Parser/Writer gehärtet: Verschachtelungs-Limit 1000 (Parser + Writer + Schema; vorher Segfault ab ~50000 `[[[...` bzw. bei zyklischen Value-Bäumen), `{"$binary":...}` wird nur bei gueltigem Hex zu `Binary` (sonst normales Dokument, kein Parse-Abbruch mehr), Hex-Float-Fallback nur mit `0x`-Präfix (sonst `"1e"` fälschlich 30.0); locale-unabhängige Zahlen bleiben; Tests `tests/test_json_hardening.cpp` (43 Checks inkl. Fuzz-lite); Doku `docs/de/guides/snapshot-json.md`, `docs/de/guides/type-mapping.md` |
| v0.38 | Framework-Härtung Teil 2 (`docs/de/internals/hardening.md`): DSL-Parser mit Verschachtelungs-Limit 256 (`(`/`!`, vorher Segfault ab ~50000 Klammern) + strikte Zahlen (int64-Überlauf → double, „1.2.3“/„1e“ → `QueryError` statt stillschweigend 0); BSON-Parser mit Tiefenlimit 1000 (vorher Segfault ab ~20000 Verschachtelung); zentrale Härtungs-Doku mit allen Limits (RegEx/JSON/DSL/BSON/Spill/Krypto) und Test-Strategie (Probes, Fuzz-lite, ASan, Fault-Injection); Tests `tests/test_hardening.cpp` (13 Checks); ctest 40/40 |
| v0.39 | Optionale Collection-Limits (FA-28, `docs/de/guides/limits.md`): `Collection::setLimits`/`limits()`, `LimitError`, Metrik `limitViolations`; Durchsetzung in allen do*-Pfaden (insert/bulk/load/update/replace/upsert/index, Tx + Spill); Tests `tests/test_limits.cpp` (34 Checks); ctest 41/41 |
| v0.40 | Discovery: **Ausreißer-Erkennung** `discoverOutliers(coll | vector<Collection>, opts)` – z-Score (`outlierZ`, Default 3.0) + IQR/Tukey-Fence (`outlierIqrK`, Default 1.5) pro numerischem Feld, `minOutlierSamples` (Default 8), Sampling respektiert, `FieldOutliers` mit Quantilen (Q1/Median/Q3/IQR), Mean/Stddev, Outlier-Rate, Methode + bis zu 100 Beispielwerten; Integration in `analyze()`: `AnalysisReport::outliers` + Health-Score als 5. Faktor (Gewichte neu 0.30/0.15/0.20/0.20/0.15, ohne numerische Werte neutral 1.0); alle 4 Präsentationen (JSON `"outliers"`, Markdown-Sektion „Ausreißer", `printSummary` `Outliers: N`, Empfehlung); CLI `inmemdb_discover --outliers`; Tests `tests/test_discovery6.cpp` (26 Checks); ctest 42/42; Doku `docs/de/guides/discovery.md` §v0.40, `docs/de/guides/discovery-guide.md` §4.14/§4.16 |
| v0.42 | Discovery: **Confusion-Matrix** `evaluateConfusion(rels, db, opts)` + `AnalysisReport::confusion` (nur `analyze(Database&)` mit `__references`): gerichtete Collection-Paare (A≠B), predicted positive = FK-Befund mit conf ≥ `confusionMinConfidence` (0.9), actual positive = Referenz in `__references` → TP/FP/FN/TN + Precision/Recall/F1; alle Präsentationen (JSON `"confusion"`, Markdown-Sektion, `printSummary` `F1=`); CLI `--confusion`, `--report` bei einer Datei; Tests `tests/test_discovery7.cpp` (29 Checks); Doku `docs/de|en/guides/discovery.md` §v0.42, `discovery-guide.md` §4.17 |
| v0.43 | Befund M1 (Major) gefixt: NaN-Ordnung – `compare()`/`equal()` in `src/value.cpp` ordnen NaN Postgres-Stil (NaN > jede Zahl inkl. +Inf, NaN == NaN; `equal(NaN,NaN)` true, NaN≠Zahl) → strikte schwache Ordnung wieder transitiv, `std::map`-Indizes/Unique-Prüfung/Queries NaN-sicher (vorher UB/Korruption/Endlosschleifen); JSON-Parser akzeptiert tolerante Literale `nan`/`-nan`/`inf`/`-Infinity` tatsächlich (vorher toter Code), Writer schreibt NaN/Inf weiterhin als `null`; Regressionstests `tests/test_nan_ordering.cpp` (100 Checks); Doku `docs/de|en/internals/hardening.md` §7, `guides/indexes.md`, `guides/snapshot-json.md` |
| v0.44 | Befunde M2/M3/M4: **M2** Nebenläufigkeits-Stresstest `tests/test_concurrency.cpp` (parallele Reader/Writer/Tx/Observer) + CI-Jobs TSan & ASan/UBSan (gesamte Suite); zwei echte Data Races gefixt (nicht-atomare `last*`-Metrikfelder + `lastCandidateCount` → atomic, `undo`-Pointer → `std::atomic<TxUndo*>`); **M3** Doku-Präzisierung: Tx hält Exklusiv-Lock bis commit/rollback, blockiert auch Leser, kein MVCC (transactions.md/architecture.md/ttl-expiry.md de+en, FA-16); **M4** PBKDF2 100.000 → 600.000 Iterationen (OWASP), abwärtskompatibel via versioniertem Dateiheader, Test `tests/test_crypto_kdf.cpp` (9 Checks); ctest 52/52; Doku hardening.md §4/§7, encrypted-snapshot.md (de+en), CHANGELOG |
| v1.0.0 | **Erstes offizielles Release**: Version auf 1.0.0 gehoben (CMake `project(VERSION 1.0.0)`, vcpkg.json/conanfile.py, Doku), ab jetzt SemVer – die öffentliche API ist stabil; Git-Tag `v1.0.0`; enthält zusätzlich die Doku-Umstrukturierung aus PR #51 (`docs/de/README.md` Index, `docs/de/getting-started.md`, `docs/de/guides/`, `docs/de/internals/`, `CHANGELOG.md`); ctest 42/42 |

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

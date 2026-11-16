# Import aus externen Quellen (FA-21) – v0.13

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.13); Quellen ohne Abhängigkeiten: CSV, JSON, JSONL, Snapshot, SQL-Dump, BSON; optional: SQLite via `-DINMEMDB_WITH_SQLITE=ON` |
| Bezug | Anforderungen FA-21 (Kann), NFA-06 (keine Pflichtabhängigkeiten), `docs/de/guides/type-mapping.md` |

## Architektur

```
ImportRegistry (Singleton, Factory)          Importer (Orchestrator)              Database
  registerSource(name, factory, {".csv"})      import(db, source, opts)            Collections
  create(name, path) / createForFile(path)  └─> pro Tabelle: createCollection      Indizes (nach Load)
  sourceForFile(path) -> name (per Endung) └─> readRows → Document → insertBulk   Schema (FA-20)
                                                  createIndex / setSchema / Stats
        ▲
        │ registriert von
  CsvImportSource (immer)          SqliteImportSource (nur INMEMDB_HAS_SQLITE)
```

- `ImportSource` (abstrakt, `include/memdb/import.hpp`): `tables()` liefert Metadaten
  (`TableInfo` mit `ColumnInfo`/`IndexInfo`), `readRows()` liefert Zeilen als bereits
  konvertierte `Value`s. **Keine** externen Typen im Header.
- `ImportRegistry::instance()` verwaltet Ableitungen per Factory: `registerSource`
  (Name + Dateiendungen), `unregisterSource`, `hasSource`, `sourceNames`, `create(name, path)`,
  `sourceForFile(path)` / `createForFile(path)` (Auto-Erkennung per Dateiendung per
  Suffix-Matching, case-insensitiv, **längster Suffix gewinnt** – also `shop.inmemdb.json`
  → `snapshot`, nicht `json`). Thread-sicher (interner Mutex).
  Eingebaute Quellen (automatisch registriert):

  | Name | Endungen | Klasse | Abhängigkeiten |
  |---|---|---|---|
  | `csv` | `.csv` | `CsvImportSource` | keine |
  | `json` | `.json` | `JsonImportSource` (Auto: Array/einzelnes Objekt, sonst JSONL-Fallback) | keine (interner Parser) |
  | `jsonl` | `.jsonl`, `.ndjson` | `JsonImportSource` (Mode::Lines) | keine |
  | `snapshot` | `.inmemdb.json` | `InmemdbSnapshotSource` (FA-14-Snapshot inkl. Schema/Indizes) | keine |
  | `sqldump` | `.sql`, `.dump` | `SqlDumpImportSource` (CREATE TABLE + INSERT) | keine |
  | `bson` | `.bson` | `BsonImportSource` (z. B. mongodump) | keine |
  | `sqlite` | `.db`, `.sqlite`, `.sqlite3` | `SqliteImportSource` | nur mit `INMEMDB_WITH_SQLITE=ON` |

  Eigene Quellen melden sich mit zwei Methoden (`tables()`/`readRows()`) + einer
  Factory-Zeile an (siehe Beispiel unten).
- `Importer::import(db, source, opts)` bzw. `Importer::import(db, "csv", "users.csv", opts)`
  übernimmt: Collections anlegen (bestehende werden übersprungen), Bulk-Load in Batches
  (`insertBulk`), Indizes **nach** dem Laden anlegen, optionales Schema aus Spaltentypen
  ableiten (`setSchema`, bei Typ-Mismatch wird das Schema übersprungen), PK als Feld `pk`
  mitführen, Stats (`ImportStats`).

## CSV (`CsvImportSource`, keine Abhängigkeiten)

- Eine Datei = eine Collection; Name default = Dateiname ohne Endung (`Options::tableName` änderbar).
- Erste Zeile = Header (Spaltennamen), abschaltbar (`hasHeader=false` → `col1..colN`).
- RFC-4180-ähnlich: `"..."` mit `""`-Escape, Trennzeichen/Zeilenumbrüche innerhalb von
  Quotes erlaubt, CRLF und LF. Trennzeichen/Quote konfigurierbar (`delimiter`, `quote`).
- Leere Zelle → `Null`. Ohne `inferTypes` sind alle Spalten `Text`; mit `inferTypes=true`
  wird pro Spalte geprüft: alle Werte Integer → `Int`, sonst alle Double → `Double`, sonst `Text`.

## Formate ohne Abhängigkeiten

Alle folgenden Quellen nutzen nur die C++17-Standardbibliothek (JSON via internem
Parser `src/json.*`; BSON/SQL-Dump mit eigenen Parsern). Spaltentypen werden –
wenn die Quelle keine Deklaration liefert – aus den Werten inferiert
(alle Int → `Integer`, Int+Double → `Real`, sonst gemischt → `Unknown`;
`Unknown` überspringt das Schema für die Tabelle, siehe `ImportStats::skippedSchemas`).

- **CSV** (`CsvImportSource`): siehe oben.
- **JSON** (`JsonImportSource`): eine Datei = eine Collection (Name = Dateiname ohne
  Endung). `JsonMode::Auto` parst die ganze Datei (Array von Objekten oder einzelnes
  Objekt); schlägt das fehl, wird zeilenweise gelesen (JSONL). Typen 1:1 aus JSON.
- **Snapshot** (`InmemdbSnapshotSource`, `.inmemdb.json`): re-importiert
  `Database::toJson`/`saveToFile`-Snapshots inkl. FA-20-Schema (`notNull` bleibt
  erhalten) und Indizes; Dokument-IDs werden neu vergeben.
- **SQL-Dump** (`SqlDumpImportSource`): `CREATE TABLE` (Spalten/Typen, inline und
  table-level `PRIMARY KEY`, `NOT NULL`, `UNIQUE KEY`/`KEY` einspaltig) und
  `INSERT INTO ... VALUES (...),(...);` mit Backtick/Quote-Identifiern,
  String-Escapes (`\'`, `\n`, `''` …), `NULL`, `TRUE`/`FALSE`, Hex-Zahlen und
  `X'..'`-Blobs. Andere Statements werden übersprungen.
- **BSON** (`BsonImportSource`): mongodump-Dateien; double/string/document/array/
  binary (ObjectId → 12-Byte-Binary)/bool/datetime/int32/int64/regex/Code u. a.

## Fremdschlüssel-Import als Referenzen (FA-26, v0.28)

Quellen mit FK-Metadaten liefern diese jetzt mit: `TableInfo::foreignKeys`
(`ForeignKeyInfo{column, refTable, refColumn, name}`).

- **SQLite**: `PRAGMA foreign_key_list(tabelle)` (`to` = NULL → PK der Zieltabelle)
- **SQL-Dump**: spalten-level `col ... REFERENCES t(c)` sowie table-level
  `FOREIGN KEY (c) REFERENCES t(c)` und `CONSTRAINT name FOREIGN KEY ...`

Beim `Importer::import` (Default `ImportOptions::importForeignKeys = true`) werden die
FKs nach dem Laden als **FA-18-Referenzen** angelegt: pro Quell-Zeile wird der Wert von
`column` gegen das Zielfeld der Zieltabelle gematcht (`refColumn`, sonst das `pk`-Feld,
sonst die PK-Spalte) und `ReferenceIndex::link(..., relation)` gerufen – Relation-Name
ist der Constraint-Name bzw. `fk_<column>`. Danach sind die Beziehungen join-beschleunigt
(`joinIds`/`join`, `@relation`-DSL) und snapshot-fähig. Unaufösbare Werte/Ziele zählen
in `ImportStats::skippedForeignKeys`, angelegte Referenzen in `importedReferences`.
Abschaltbar mit `importForeignKeys=false` bzw. CLI `--no-foreign-keys`.

```cpp
memdb::ImportStats st = memdb::Importer::import(db, "sqlite", "shop.db");
// st.importedReferences == angelegte FA-18-Referenzen
memdb::ReferenceIndex refs(db);
auto hits = refs.joinIds("orders", "fk_user_id", "users", memdb::Field("name") == "Ada");
```

## SQLite-Mapping (`SqliteImportSource`, nur mit `INMEMDB_WITH_SQLITE=ON`)

| SQLite | InMemDB |
|---|---|
| `INTEGER` (incl. PK/rowid) | `Int` (+ Feld `pk`) |
| `REAL`/`FLOAT`/`DOUBLE` | `Double` |
| `TEXT`/`CHAR`/`CLOB` | `String` |
| `BLOB` | `Binary` |
| `NULL` | `Null` |
| `BOOL`/`BOOLEAN` | `Int` |

Einspaltige Indizes der Quelle werden übernommen (`createIndex(col, unique)`).

## Nutzung

```cpp
#include <memdb/memdb.hpp>   // import.hpp + import_csv.hpp (+ import_sqlite.hpp mit Macro)

memdb::Database db("shop");

// 1) direkt über Ableitung
memdb::CsvImportSource csv("users.csv", {.inferTypes = true});
memdb::ImportStats s1 = memdb::Importer::import(db, csv);

// 2) über Registry per Name
memdb::ImportStats s2 = memdb::Importer::import(db, "csv", "orders.csv");

// 3) über Registry per Dateiendung (Auto; längster Suffix gewinnt)
auto src = memdb::ImportRegistry::instance().createForFile("shop.db");          // -> sqlite (falls gebaut)
auto snap = memdb::ImportRegistry::instance().createForFile("shop.inmemdb.json"); // -> snapshot

// 4) eigene Quelle anmelden
memdb::ImportRegistry::instance().registerSource(
    "meinformat", [](const std::string& p) { return std::make_unique<MeinSource>(p); }, {".mf"});

db.saveToFileEncrypted("shop.enc", "passwort");   // FA-19
```

CLI (immer gebaut, `inmemdb_import`):

```sh
./build/inmemdb_import users.csv --infer-types --out users.json
./build/inmemdb_import shop.db --out shop.enc --password geheim --no-schema --batch 50000
./build/inmemdb_import daten.txt --source csv --delimiter ';' --no-header
./build/inmemdb_import dump.sql                   # -> sqldump per Endung
./build/inmemdb_import shop.inmemdb.json          # -> snapshot (Reimport inkl. Schema/Indizes)
./build/inmemdb_import --list-sources             # registrierte Quellen anzeigen
```

Optionen: `--source NAME`, `--out <datei>`, `--password <pw>` (verschlüsselter Snapshot, FA-19),
`--no-schema`, `--no-indexes`, `--no-pk-field`, `--batch N`, CSV-spezifisch: `--delimiter C`,
`--no-header`, `--infer-types`, sowie `--list-sources`.

## Build & CI
- Default-Build bleibt ohne externe Abhängigkeiten (CSV + Registry + Tool immer aktiv).
- CI-Job **Import (sqlite, INMEMDB_WITH_SQLITE=ON)** installiert `libsqlite3-dev`, baut mit
  `-DINMEMDB_WITH_SQLITE=ON` und führt inkl. `tests/test_import.cpp` alle Tests aus.
- Tests: `tests/test_import_csv.cpp` (CSV + Registry), `tests/test_import_formats.cpp`
  (JSON/JSONL/Snapshot/SQL-Dump/BSON + Endungs-Matching; immer gebaut) und
  `tests/test_import.cpp` (SQLite, nur mit Option).

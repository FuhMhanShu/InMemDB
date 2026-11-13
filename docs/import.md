# Import aus externen Quellen (FA-21) – v0.13

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.13); SQLite-Ableitung optional via `-DINMEMDB_WITH_SQLITE=ON` |
| Bezug | Lastenheft FA-21 (Kann), NFA-06 (keine Pflichtabhängigkeiten), `docs/type-mapping.md` |

## Architektur

```
ImportSource (abstrakt)          Importer (Orchestrator)           Database
  tables() ──TableInfo──▶   ──▶  pro Tabelle: createCollection    Collections
  readRows() ──Values──▶         readRows → Document → insertBulk  Indizes (nach Load)
                               ──▶  createIndex / setSchema / Stats  Schema (FA-20)
```

- `ImportSource` (in `include/memdb/import.hpp`): `tables()` liefert Metadaten
  (`TableInfo` mit `ColumnInfo`/`IndexInfo`), `readRows()` liefert Zeilen als
  bereits konvertierte `Value`s. **Keine** externen Typen im Header.
- `Importer::import(db, source, opts)` übernimmt: Collections anlegen (bestehende
  werden übersprungen), Bulk-Load in Batches (`insertBulk`), Indizes **nach** dem
  Laden anlegen, optionales Schema aus Spaltentypen ableiten (`setSchema`, bei
  Typ-Mismatch wird das Schema übersprungen), PK als Feld `pk` mitführen, Stats.
- Konkrete Ableitungen: `SqliteImportSource` (nur mit `INMEMDB_WITH_SQLITE=ON`).
  Weitere Quellen (JSON/CSV/SQL-Dump) implementieren nur die zwei Methoden.

## SQLite-Mapping

| SQLite | InMemDB |
|---|---|
| `INTEGER` (incl. PK/rowid) | `Int` (+ Feld `pk`) |
| `REAL`/`FLOAT`/`DOUBLE` | `Double` |
| `TEXT`/`CHAR`/`CLOB` | `String` |
| `BLOB` | `Binary` |
| `NULL` | `Null` |
| `BOOL`/`BOOLEAN` | `Int` |

Unique-Indizes der Quelle werden als `createIndex(col, unique=true)` übernommen.

## Nutzung

```cpp
#ifdef INMEMDB_HAS_SQLITE
#include <memdb/import_sqlite.hpp>
#endif

memdb::Database db("shop");
memdb::SqliteImportSource src("shop.db");
memdb::ImportStats stats = memdb::Importer::import(db, src);   // Default-Optionen
db.saveToFileEncrypted("shop.enc", "passwort");                // FA-19
```

CLI (Build mit `-DINMEMDB_WITH_SQLITE=ON`):

```sh
./build/sqlite_import shop.db --out shop.json
./build/sqlite_import shop.db --out shop.enc --password geheim --no-schema --batch 50000
```

## Build & CI
- Default-Build bleibt ohne externe Abhängigkeiten (Option OFF).
- CI-Job **Import (sqlite)** installiert `libsqlite3-dev`, baut mit
  `-DINMEMDB_WITH_SQLITE=ON` und führt inkl. `tests/test_import.cpp` (sqlite
  `:memory:`-basiert) alle Tests aus.

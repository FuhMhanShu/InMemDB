# Import from external sources (FA-21) – v0.13

| Field | Value |
|---|---|
| Status | implemented (v0.13); dependency-free sources: CSV, JSON, JSONL, snapshot, SQL dump, BSON; optional: SQLite via `-DINMEMDB_WITH_SQLITE=ON` |
| Reference | requirements FA-21 (optional), NFA-06 (no mandatory dependencies), `docs/en/guides/type-mapping.md` |

## Architecture

```
ImportRegistry (singleton, factory)          Importer (orchestrator)               Database
  registerSource(name, factory, {".csv"})      import(db, source, opts)            collections
  create(name, path) / createForFile(path)  └─> per table: createCollection       indexes (after load)
  sourceForFile(path) -> name (by extension) └─> readRows → Document → insertBulk  schema (FA-20)
                                                  createIndex / setSchema / stats
        ▲
        │ registered by
  CsvImportSource (always)          SqliteImportSource (only INMEMDB_HAS_SQLITE)
```

- `ImportSource` (abstract, `include/memdb/import.hpp`): `tables()` returns metadata
  (`TableInfo` with `ColumnInfo`/`IndexInfo`), `readRows()` returns rows as already
  converted `Value`s. **No** external types in the header.
- `ImportRegistry::instance()` manages subclasses via factory: `registerSource`
  (name + file extensions), `unregisterSource`, `hasSource`, `sourceNames`, `create(name, path)`,
  `sourceForFile(path)` / `createForFile(path)` (auto-detection by file extension via
  suffix matching, case-insensitive, **longest suffix wins** – so `shop.inmemdb.json`
  → `snapshot`, not `json`). Thread-safe (internal mutex).
  Built-in sources (auto-registered):

  | Name | Extensions | Class | Dependencies |
  |---|---|---|---|
  | `csv` | `.csv` | `CsvImportSource` | none |
  | `json` | `.json` | `JsonImportSource` (auto: array/single object, else JSONL fallback) | none (internal parser) |
  | `jsonl` | `.jsonl`, `.ndjson` | `JsonImportSource` (Mode::Lines) | none |
  | `snapshot` | `.inmemdb.json` | `InmemdbSnapshotSource` (FA-14 snapshot incl. schema/indexes) | none |
  | `sqldump` | `.sql`, `.dump` | `SqlDumpImportSource` (CREATE TABLE + INSERT) | none |
  | `bson` | `.bson` | `BsonImportSource` (e.g. mongodump) | none |
  | `sqlite` | `.db`, `.sqlite`, `.sqlite3` | `SqliteImportSource` | only with `INMEMDB_WITH_SQLITE=ON` |

  Register your own sources with two methods (`tables()`/`readRows()`) + one factory line
  (see example below).
- `Importer::import(db, source, opts)` or `Importer::import(db, "csv", "users.csv", opts)`
  handles: create collections (existing ones are skipped), bulk load in batches
  (`insertBulk`), create indexes **after** loading, derive an optional schema from column
  types (`setSchema`; on type mismatch the schema for that table is skipped), carry the PK
  as field `pk`, stats (`ImportStats`).

## CSV (`CsvImportSource`, no dependencies)

- One file = one collection; name defaults to the file name without extension (`Options::tableName` changeable).
- First line = header (column names), can be disabled (`hasHeader=false` → `col1..colN`).
- RFC-4180-like: `"..."` with `""` escape, delimiter/line breaks inside
  quotes allowed, CRLF and LF. Delimiter/quote configurable (`delimiter`, `quote`).
- Empty cell → `Null`. Without `inferTypes` all columns are `Text`; with `inferTypes=true`
  each column is checked: all values integer → `Int`, else all double → `Double`, else `Text`.

## Dependency-free formats

All following sources use only the C++17 standard library (JSON via the internal
parser `src/json.*`; BSON/SQL dump with own parsers). Column types are –
when the source provides no declaration – inferred from the values
(all int → `Integer`, int+double → `Real`, else mixed → `Unknown`;
`Unknown` skips the schema for the table, see `ImportStats::skippedSchemas`).

- **CSV** (`CsvImportSource`): see above.
- **JSON** (`JsonImportSource`): one file = one collection (name = file name without
  extension). `JsonMode::Auto` parses the whole file (array of objects or a single
  object); if that fails it reads line by line (JSONL). Types 1:1 from JSON.
- **Snapshot** (`InmemdbSnapshotSource`, `.inmemdb.json`): re-imports
  `Database::toJson`/`saveToFile` snapshots incl. FA-20 schema (`notNull` is
  preserved) and indexes; document IDs are reassigned.
- **SQL dump** (`SqlDumpImportSource`): `CREATE TABLE` (columns/types, inline and
  table-level `PRIMARY KEY`, `NOT NULL`, single-column `UNIQUE KEY`/`KEY`) and
  `INSERT INTO ... VALUES (...),(...);` with backtick/quote identifiers,
  string escapes (`\'`, `\n`, `''`, …), `NULL`, `TRUE`/`FALSE`, hex numbers and
  `X'..'` blobs. Other statements are skipped.
- **BSON** (`BsonImportSource`): mongodump files; double/string/document/array/
  binary (ObjectId → 12-byte binary)/bool/datetime/int32/int64/regex/Code etc.

## Foreign-key import as references (FA-26, v0.28)

Sources with FK metadata now provide it: `TableInfo::foreignKeys`
(`ForeignKeyInfo{column, refTable, refColumn, name}`).

- **SQLite**: `PRAGMA foreign_key_list(table)` (`to` = NULL → PK of the target table)
- **SQL dump**: column-level `col ... REFERENCES t(c)` plus table-level
  `FOREIGN KEY (c) REFERENCES t(c)` and `CONSTRAINT name FOREIGN KEY ...`

On `Importer::import` (default `ImportOptions::importForeignKeys = true`) the
FKs are created as **FA-18 references** after loading: per source row the value of
`column` is matched against the target field of the target table (`refColumn`, else the
`pk` field, else the PK column) and `ReferenceIndex::link(..., relation)` is called –
relation name is the constraint name or `fk_<column>`. Afterwards the relations are
index-accelerated joinable (`joinIds`/`join`, `@relation` in the DSL) and snapshot-capable.
Unresolvable values/targets count in `ImportStats::skippedForeignKeys`, created references
in `importedReferences`. Can be disabled with `importForeignKeys=false` or CLI `--no-foreign-keys`.

```cpp
memdb::ImportStats st = memdb::Importer::import(db, "sqlite", "shop.db");
// st.importedReferences == created FA-18 references
memdb::ReferenceIndex refs(db);
auto hits = refs.joinIds("orders", "fk_user_id", "users", memdb::Field("name") == "Ada");
```

## SQLite mapping (`SqliteImportSource`, only with `INMEMDB_WITH_SQLITE=ON`)

| SQLite | InMemDB |
|---|---|
| `INTEGER` (incl. PK/rowid) | `Int` (+ field `pk`) |
| `REAL`/`FLOAT`/`DOUBLE` | `Double` |
| `TEXT`/`CHAR`/`CLOB` | `String` |
| `BLOB` | `Binary` |
| `NULL` | `Null` |
| `BOOL`/`BOOLEAN` | `Int` |

Single-column indexes of the source are adopted (`createIndex(col, unique)`).

## Usage

```cpp
#include <memdb/memdb.hpp>   // import.hpp + import_csv.hpp (+ import_sqlite.hpp with macro)

memdb::Database db("shop");

// 1) directly via subclass
memdb::CsvImportSource csv("users.csv", {.inferTypes = true});
memdb::ImportStats s1 = memdb::Importer::import(db, csv);

// 2) via registry by name
memdb::ImportStats s2 = memdb::Importer::import(db, "csv", "orders.csv");

// 3) via registry by file extension (auto; longest suffix wins)
auto src = memdb::ImportRegistry::instance().createForFile("shop.db");          // -> sqlite (if built)
auto snap = memdb::ImportRegistry::instance().createForFile("shop.inmemdb.json"); // -> snapshot

// 4) register your own source
memdb::ImportRegistry::instance().registerSource(
    "myformat", [](const std::string& p) { return std::make_unique<MySource>(p); }, {".mf"});

db.saveToFileEncrypted("shop.enc", "password");   // FA-19
```

CLI (always built, `inmemdb_import`):

```sh
./build/inmemdb_import users.csv --infer-types --out users.json
./build/inmemdb_import shop.db --out shop.enc --password secret --no-schema --batch 50000
./build/inmemdb_import data.txt --source csv --delimiter ';' --no-header
./build/inmemdb_import dump.sql                   # -> sqldump by extension
./build/inmemdb_import shop.inmemdb.json          # -> snapshot (re-import incl. schema/indexes)
./build/inmemdb_import --list-sources             # show registered sources
```

Options: `--source NAME`, `--out <file>`, `--password <pw>` (encrypted snapshot, FA-19),
`--no-schema`, `--no-indexes`, `--no-pk-field`, `--batch N`, CSV-specific: `--delimiter C`,
`--no-header`, `--infer-types`, plus `--list-sources`.

## Build & CI
- The default build stays free of external dependencies (CSV + registry + tool always active).
- CI job **Import (sqlite, INMEMDB_WITH_SQLITE=ON)** installs `libsqlite3-dev`, builds with
  `-DINMEMDB_WITH_SQLITE=ON` and runs all tests incl. `tests/test_import.cpp`.
- Tests: `tests/test_import_csv.cpp` (CSV + registry), `tests/test_import_formats.cpp`
  (JSON/JSONL/snapshot/SQL dump/BSON + extension matching; always built) and
  `tests/test_import.cpp` (SQLite, only with the option).

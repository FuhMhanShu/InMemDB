# Type mapping: InMemDB ↔ common database systems

Reference: requirements section 3 (data model), FA-14 (JSON snapshot), FA-19 (encrypted snapshots), FA-20 (schema).

## 1. Value types (`memdb::Value::Type`)

| InMemDB | C++ | PostgreSQL | MySQL / MariaDB | SQLite | MongoDB (BSON) | JSON snapshot (`toJson`) |
|---|---|---|---|---|---|---|
| `Null` | `nullptr` | `NULL` | `NULL` | `NULL` | `null` | `null` |
| `Bool` | `bool` | `BOOLEAN` | `TINYINT(1)` / `BOOLEAN` | `INTEGER` 0/1 | `bool` | `true` / `false` |
| `Int` | `int64_t` | `BIGINT` (smaller values: `INTEGER`) | `BIGINT` | `INTEGER` | `long` (int64) | JSON number (no `.`) |
| `Double` | `double` | `DOUBLE PRECISION` (`FLOAT8`) | `DOUBLE` | `REAL` | `double` | JSON number with `.`/`e` |
| `String` | `std::string` (UTF-8) | `TEXT` / `VARCHAR(n)` | `TEXT` / `VARCHAR(n)` | `TEXT` | `string` | JSON string |
| `Binary` | `std::vector<uint8_t>` | `BYTEA` | `BLOB` / `BINARY(n)` | `BLOB` | `binData` | `{"$binary":"<hex>"}` |
| `Document` | nested `Document` | `JSON` / `JSONB` | `JSON` | `TEXT` (JSON) | `object` (embedded) | JSON object |
| `Array` | `std::vector<Value>` | typed arrays `text[]`, `bigint[]`, … | `JSON` | `TEXT` (JSON) | `array` | JSON array |

## 2. Special cases & conventions

- **Int vs. double:** InMemDB strictly distinguishes int64 (`Int`) and double (`Double`).
  On JSON import a number without decimal point/exponent becomes `Int`, with `.`/`e` it
  becomes `Double`. Conversely: SQL `INTEGER`/`BIGINT` → `Int`, SQL `REAL`/`FLOAT`/`DOUBLE`
  → `Double`. Beware: MySQL `FLOAT` is 32-bit – possible loss; use `DOUBLE`/`FLOAT8`.
- **`Double` accepts `Int`** (numeric promotion in comparisons and in a `double` schema
  declaration), never the other way round.
- **Strings are UTF-8.** JSON escapes incl. `\uXXXX` and surrogate pairs are decoded correctly.
- **Binary in the JSON snapshot** as a marker object `{"$binary":"<hex>"}` (collision-poor;
  v0.37: on parsing, `{"$binary":...}` becomes `Binary` only if the value is valid hex – a
  normal document with such a field otherwise stays unchanged).
- **Nested documents** correspond to JSON objects / Mongo sub-documents / JSONB paths
  (`address->>'city'`); access in InMemDB via dot notation (`address.city`).

## 3. Missing SQL types (currently mapped by convention)

| Desired type | Mapping in InMemDB (today) | Recommendation |
|---|---|---|
| `DATE` / `TIMESTAMP` | `String` (ISO-8601, e.g. `2026-11-12`) or `Int` (epoch millis) | ISO-8601 string – sortable and JSON-compatible |
| `DECIMAL` / `NUMERIC` (money amounts) | only `Double` (floating point!) | For exact amounts: use `Int` in the smallest unit (cents); dedicated decimal type as outlook (NFA-03) |
| `UUID` | `String` (canonical form) | String |
| `ENUM` | `String` (+ `Schema` as restriction) | String with allowed values in the schema |

## 4. Schema types (FA-20) in the mapping

| `FieldSchema` | SQL column | MongoDB | JSON |
|---|---|---|---|
| `Value::Type::String` | `TEXT` / `VARCHAR` | `string` | String |
| `Value::Type::Int` | `BIGINT` | `long` | Number |
| `Value::Type::Double` | `DOUBLE PRECISION` | `double` | Number |
| `Value::Type::Bool` | `BOOLEAN` | `bool` | `true/false` |
| `Value::Type::Binary` | `BYTEA` | `binData` | `{"$binary":…}` |
| `Value::Type::Document` (+ `subSchema`) | `JSONB` | `object` | Object |
| `Value::Type::Array` (+ `elements`) | `text[]` etc. / `JSONB` | `array` | Array |

## 5. Exchange formats

- **JSON snapshot** (`Database::toJson`, also encrypted FA-19) is standard JSON →
  directly loadable as `JSONB` in PostgreSQL, via `mongoimport` into MongoDB, as `TEXT` in SQLite.
- **Dot notation** (`address.city`) corresponds to JSONB paths (`address->>'city'`) and
  Mongo dot notation.
- **Document IDs** are InMemDB-internal (uint64) and are not exported; they are reassigned on import.

## 6. Outlook
New types (e.g. `Date`, `Decimal`, `Uuid` as real value types) can be added later via the
extension mechanism (NFA-03) without changing existing data.

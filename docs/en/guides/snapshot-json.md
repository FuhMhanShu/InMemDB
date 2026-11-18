# Snapshot / JSON export & import (FA-14) – implemented in v0.4

| Field | Value |
|---|---|
| Status | implemented (v0.4) |
| Reference | requirements FA-14 (optional), NFA-06 (no mandatory external dependencies) |

## API

```cpp
// whole database
std::string json = db.toJson();
db.fromJson(json);                                  // loads into an existing DB (creates/replaces collections)
Database db2 = Database::fromJson(json, "copy");    // new DB from a JSON string
db.saveToFile("/tmp/snap.json");
Database db3 = Database::loadFromFile("/tmp/snap.json");

// single collection
std::string arr = coll.toJson();        // JSON array of the documents
coll.loadJson(arr);                     // replaces the whole content of the collection
coll.clear();                           // empties the content, index definitions remain
```

## JSON format
- Database: `{"name": "...", "collections": {"users": {"indexes": [{"field": "name", "unique": true}], "documents": [ {...}, ... ]}, ...}}`
- Collection: `Collection::toJson()` returns only the document array `[{...}, ...]`
  (for loading via `Collection::loadJson`); the database format additionally contains
  the index definitions per collection
- When importing into a **new** database the indexes are recreated from the JSON;
  for an **existing** collection its index definitions are kept (documents are replaced,
  indexes refilled incl. unique check)
- Value mapping: `null` → `null`, bool → `true/false`, int → number, double → number
  (always with decimal point/exponent so the type survives; NaN/Inf → `null`),
  string → JSON string (UTF-8, escapes incl. `\uXXXX` + surrogate pairs),
  array → `[...]`, document → `{...}`, binary → `{"$binary": "<hex>"}`
  (v0.37: when parsing, `{"$binary":...}` becomes `Binary` only if the string is valid
  hex – otherwise it stays a normal document; before, e.g. `{"$binary":"hello"}`
  threw a `JsonError` and aborted the whole parse)
- **Tolerant literals (v0.43, finding M1):** `nan`, `-nan`, `inf`, `-Infinity` (any case) are
  parsed → `Double` NaN/±Inf; on write, NaN/Inf still become `null`.
- **Nesting limit (v0.37):** parser and writer accept at most 1000 levels –
  deeper nested JSON (or cyclic Value structures when writing) throws
  `JsonError` instead of stack overflow/segfault (before: crash at ~50000 `[[[...`).
- **Document IDs are not serialized** – new IDs are assigned on import.

## Behavior
- `Database::fromJson` creates missing collections and replaces the content of
  existing ones; index definitions are kept and refilled on import (incl. unique check).
- `Collection::loadJson` parses completely first (parse errors throw `JsonError`
  without changing state), then `clear()` + insert all documents.
- File IO via `saveToFile`/`loadFromFile` (throws `Error` on IO failures).
- Serializer/parser are **self-implemented** (`src/json.cpp`), no external JSON
  library needed.
- **Locale-independent (v0.23.1):** numbers are always written and parsed with a decimal
  **point** (`std::to_chars`/`std::from_chars`, never
  `snprintf`/`strtod`/stream operators) – even with an active `de_DE` locale
  `1.5` stays `1.5` and does not become `1,5`. The same applies to
  `toString(Value)` and DSL number literals. Regression test:
  `tests/test_locale.cpp` (sets the real C locale `de_DE.UTF-8` if available, plus an
  additional comma `numpunct` facet).

## Tests
Round-trip of all value types (incl. Unicode, escapes, binary, double type preservation),
database and collection level, file save/load, error cases (invalid JSON, wrong
structure), `clear()` with retained indexes.

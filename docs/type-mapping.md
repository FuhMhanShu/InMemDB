# Typ-Mapping: InMemDB ↔ gängige Datenbanksysteme

Bezug: Anforderungen Abschnitt 3 (Datenmodell), FA-14 (JSON-Snapshot), FA-19 (verschlüsselte Snapshots), FA-20 (Schema).

## 1. Werttypen (`memdb::Value::Type`)

| InMemDB | C++ | PostgreSQL | MySQL / MariaDB | SQLite | MongoDB (BSON) | JSON-Snapshot (`toJson`) |
|---|---|---|---|---|---|---|
| `Null` | `nullptr` | `NULL` | `NULL` | `NULL` | `null` | `null` |
| `Bool` | `bool` | `BOOLEAN` | `TINYINT(1)` / `BOOLEAN` | `INTEGER` 0/1 | `bool` | `true` / `false` |
| `Int` | `int64_t` | `BIGINT` (kleinere Werte: `INTEGER`) | `BIGINT` | `INTEGER` | `long` (int64) | JSON-Zahl (ohne `.`) |
| `Double` | `double` | `DOUBLE PRECISION` (`FLOAT8`) | `DOUBLE` | `REAL` | `double` | JSON-Zahl mit `.`/`e` |
| `String` | `std::string` (UTF-8) | `TEXT` / `VARCHAR(n)` | `TEXT` / `VARCHAR(n)` | `TEXT` | `string` | JSON-String |
| `Binary` | `std::vector<uint8_t>` | `BYTEA` | `BLOB` / `BINARY(n)` | `BLOB` | `binData` | `{"$binary":"<hex>"}` |
| `Document` | verschachteltes `Document` | `JSON` / `JSONB` | `JSON` | `TEXT` (JSON) | `object` (eingebettet) | JSON-Objekt |
| `Array` | `std::vector<Value>` | typisierte Arrays `text[]`, `bigint[]` … | `JSON` | `TEXT` (JSON) | `array` | JSON-Array |

## 2. Besonderheiten & Konventionen

- **Int vs. Double:** InMemDB unterscheidet strikt int64 (`Int`) und double (`Double`).
  Beim JSON-Import landet eine Zahl ohne Dezimalpunkt/Exponent als `Int`, mit `.`/`e` als `Double`.
  Umgekehrt: SQL-`INTEGER`/`BIGINT` → `Int`, SQL-`REAL`/`FLOAT`/`DOUBLE` → `Double`.
  Achtung: MySQL `FLOAT` ist 32-Bit – Verlust möglich; `DOUBLE`/`FLOAT8` verwenden.
- **Double akzeptiert Int** (numerische Promotion in Vergleichen und Schema-Deklaration `double`), nie umgekehrt.
- **Strings sind UTF-8.** JSON-Escapes inkl. `\uXXXX` und Surrogat-Paaren werden korrekt dekodiert.
- **Binary im JSON-Snapshot** als Marker-Objekt `{"$binary":"<hex>"}` (kollisionsarm; v0.37: beim Parsen wird `{"$binary":...}` nur zu `Binary`, wenn der Wert gueltiges Hex ist – ein normales Dokument mit diesem Feld bleibt sonst unverändert erhalten).
- **Verschachtelte Dokumente** entsprechen JSON-Objekten / Mongo-Subdokumenten / JSONB-Pfaden (`adresse->>'stadt'`); Zugriff in InMemDB per Punktnotation (`adresse.stadt`).

## 3. Fehlende SQL-Typen (aktuell als Konvention abgebildet)

| Gewünschter Typ | Abbildung in InMemDB (heute) | Empfehlung |
|---|---|---|
| `DATE` / `TIMESTAMP` | `String` (ISO-8601, z. B. `2026-11-12`) oder `Int` (Epoch-Millis) | ISO-8601-String – sortierbar und JSON-kompatibel |
| `DECIMAL` / `NUMERIC` (Geldbeträge) | nur `Double` (Fließkomma!) | Für exakte Beträge: `Int` in kleinster Einheit (Cent) verwenden; eigener Dezimal-Typ als Ausblick (NFA-03) |
| `UUID` | `String` (kanonische Form) | String |
| `ENUM` | `String` (+ `Schema` als Einschränkung) | String mit erlaubten Werten im Schema |

## 4. Schema-Typen (FA-20) im Mapping

| `FieldSchema` | SQL-Spalte | MongoDB | JSON |
|---|---|---|---|
| `Value::Type::String` | `TEXT` / `VARCHAR` | `string` | String |
| `Value::Type::Int` | `BIGINT` | `long` | Zahl |
| `Value::Type::Double` | `DOUBLE PRECISION` | `double` | Zahl |
| `Value::Type::Bool` | `BOOLEAN` | `bool` | `true/false` |
| `Value::Type::Binary` | `BYTEA` | `binData` | `{"$binary":…}` |
| `Value::Type::Document` (+ `subSchema`) | `JSONB` | `object` | Objekt |
| `Value::Type::Array` (+ `elements`) | `text[]` etc. / `JSONB` | `array` | Array |

## 5. Austauschformate

- **JSON-Snapshot** (`Database::toJson`, auch verschlüsselt FA-19) ist Standard-JSON →
  direkt als `JSONB` in PostgreSQL ladbar, per `mongoimport` in MongoDB, in SQLite als `TEXT`.
- **Punktnotation** (`adresse.stadt`) entspricht JSONB-Pfaden (`adresse->>'stadt'`) bzw. Mongo-Dot-Notation.
- **Dokument-IDs** sind InMemDB-intern (uint64) und werden nicht exportiert; beim Import neu vergeben.

## 6. Ausblick
Neue Typen (z. B. `Date`, `Decimal`, `Uuid` als echte Werttypen) sind über den
Erweiterungsmechanismus (NFA-03) nachrüstbar, ohne bestehende Daten zu ändern.

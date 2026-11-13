# Optionales striktes Collection-Schema (FA-20) – Umsetzung v0.10

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.10) |
| Bezug | Lastenheft FA-20 (Kann), Abschnitt 3 (Schema) |

## Semantik
- **Ohne Schema bleibt eine Collection schema-los** – beliebige Felder und Typen.
- **Ist ein Schema gesetzt, ist es strikt:** Jedes Feld jedes Dokuments muss im Schema
  deklariert sein und den deklarierten Typ haben. Undefinierte Felder → `SchemaError`.

## API

```cpp
Schema schema;
schema.add("name", Value::Type::String).notNull();          // Pflichtfeld, nicht null
schema.add("age",  Value::Type::Int);                       // nur Int (Double wäre Fehler)
schema.add("score", Value::Type::Double);                   // Int ODER Double erlaubt
Schema adresse;
adresse.add("stadt", Value::Type::String).notNull();
schema.add("adresse", Value::Type::Document).subSchema(adresse); // rekursiv
schema.add("tags", Value::Type::Array).elements(Value::Type::String);

coll.setSchema(schema);   // prüft vorhandene Dokumente zuerst (SchemaError bei Verstoß)
coll.schema();            // optional<Schema>
coll.hasSchema();
coll.clearSchema();       // zurück zu schema-los
```

## Prüfregeln (bei insert / update / updateById / replace / upsert / loadJson)
- Typ-Mismatch → `SchemaError` mit Feldpfad + erwartet/ist. **Ausnahme:** deklariertes
  `Double` akzeptiert auch `Int` (numerische Promotion, konsistent zu `compare()`).
- `null` ist erlaubt, wenn das Feld `nullable` (Default true); `notNull()` verlangt
  Vorhandensein und non-null.
- `Document`-Felder mit `subSchema` werden rekursiv geprüft; `Array`-Felder mit
  `elements(...)` elementweise.
- Prüfung läuft **vor** der Mutation (zusammen mit der Unique-Prüfung) → bei Fehler
  bleibt der Zustand unverändert; in Transaktionen pro Operation, Rollback wie gehabt.

## Snapshot
Das Schema wird im JSON-Snapshot pro Collection mitgespeichert
(`"schema": {"fields": {...}}` neben `"indexes"`) – auch in verschlüsselten Snapshots
(FA-19). Beim Laden in eine **neue** Database wird das Schema übernommen und auf die
geladenen Dokumente angewendet; bestehende Collections behalten ihr Schema.

# Optional strict collection schema (FA-20) – implemented in v0.10

| Field | Value |
|---|---|
| Status | implemented (v0.10) |
| Reference | requirements FA-20 (optional), section 3 (schema) |

## Semantics
- **Without a schema a collection stays schema-less** – arbitrary fields and types.
- **Once a schema is set it is strict:** every field of every document must be declared
  in the schema and have the declared type. Undefined fields → `SchemaError`.

## API

```cpp
Schema schema;
schema.add("name", Value::Type::String).notNull();          // required, not null
schema.add("age",  Value::Type::Int);                       // int only (double would be an error)
schema.add("score", Value::Type::Double);                   // int OR double allowed
Schema address;
address.add("city", Value::Type::String).notNull();
schema.add("address", Value::Type::Document).subSchema(address); // recursive
schema.add("tags", Value::Type::Array).elements(Value::Type::String);

coll.setSchema(schema);   // validates existing documents first (SchemaError on violation)
coll.schema();            // optional<Schema>
coll.hasSchema();
coll.clearSchema();       // back to schema-less
```

## Validation rules (on insert / update / updateById / replace / upsert / loadJson)
- Type mismatch → `SchemaError` with field path + expected/actual. **Exception:** a
  declared `Double` also accepts `Int` (numeric promotion, consistent with `compare()`).
- `null` is allowed if the field is `nullable` (default true); `notNull()` requires
  presence and non-null.
- `Document` fields with `subSchema` are validated recursively; `Array` fields with
  `elements(...)` element-wise.
- Validation runs **before** the mutation (together with the unique check) → on error
  the state remains unchanged; in transactions per operation, rollback as usual.

## Snapshot
The schema is stored per collection in the JSON snapshot
(`"schema": {"fields": {...}}` next to `"indexes"`) – also in encrypted snapshots
(FA-19). When loading into a **new** database the schema is adopted and applied to the
loaded documents; existing collections keep their schema.

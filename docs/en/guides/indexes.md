# Indexes (FA-10) – implemented in v0.2

| Field | Value |
|---|---|
| Status | implemented (v0.2) |
| Reference | requirements FA-10, NFA-01 |

## API

```cpp
Index idx = coll.createIndex("age");          // non-unique
Index uidx = coll.createIndex("mail", true);  // unique
bool ok  = coll.hasIndex("age");
std::vector<Index> all = coll.indexes();
bool dropped = coll.dropIndex("age");
```

Errors: `IndexExistsError` (duplicate creation), `UniqueConstraintError` (violating a
unique index on insert/update or when creating it retroactively on duplicates).

## Internal structure
Per indexed field path (dot notation possible, e.g. `address.zip`) a
`std::map<Value, std::set<DocumentId>, ValueLess>`:

- `ValueLess` is a strict weak ordering: type rank (null < bool < number <
  string < binary < document < array), within number/string via `compare()`.
  **Int and double share the same rank** – so `age == 30.0` also matches `age = 30`.
- Unique index: a key value may belong to only one document.

## Use by the query engine
`Expression::indexHint()` returns the first usable comparison in the
top-level `&&` tree (comparison operator on a field; `!=` is not accelerated).
`Collection::find/findOne/count` use it to fetch candidate IDs from the index
when available:

- `==` → exact lookup (O(log n))
- `<`, `<=`, `>`, `>=` → range via `lower_bound`/`upper_bound` (numbers and strings only;
  otherwise full scan)
- no index / `!=` / OR tree → full scan over all IDs (O(n))

Afterwards the **complete** expression is evaluated on every candidate – so index
results stay semantically correct.

Diagnostics: `Collection::lastQueryCandidateCount()` returns the candidate count of the
last `find`/`findOne`/`count` query (test/performance evidence).

## Maintenance & guarantees
- Index entries are maintained automatically on `insert`, `insertBulk`, `update`,
  `updateById`, `replace`, `remove`, `removeById`.
- `createIndex` on existing data builds the index retroactively and checks `unique`
  for existing duplicates.
- Unique validation runs **before** the mutation; `update(q, ...)` applies changes in
  two phases (check all, then apply) → strong exception guarantee (NFA-05).
- All index operations run under the collection lock (`shared_mutex`).

## Outlook (optional)
- Composite indexes (multiple fields), text/full-text index.

- **v0.27:** `Collection::distinctValues(field, q)` uses an existing index on the field
  directly for an empty query (**O(#distinct)** instead of a full scan; visible in
  `metrics().indexQueries`).

# Aggregations (FA-13) – implemented in v0.6

| Field | Value |
|---|---|
| Status | implemented (v0.6) |
| Reference | requirements FA-13 (optional) |

## API

```cpp
enum class AggOp { Min, Max, Sum, Avg, Count };

// scalar aggregate over all documents matching q
std::optional<Value> coll.aggregate(AggOp::Sum, "amount", Field("city") == "Berlin");

// grouping by field value; per group count + optional aggregate of a second field
std::vector<GroupAggregate> gs = coll.groupBy("city", Expression{}, AggOp::Sum, "amount");
// GroupAggregate { Value key; size_t count; optional<Value> value; }
```

Available on `Collection` and inside `Transaction`.

## Semantics
- `Count`: number of matching documents (`fieldPath` is ignored); empty set → `0`.
- `Sum`/`Avg`: numbers only (int/double); missing/`null`/non-numeric fields are
  skipped. `Sum` returns `Int` as long as all values are int and no overflow occurs,
  otherwise `Double`; `Avg` always returns `Double`. Empty set → `nullopt`.
- `Min`/`Max`: numbers and strings (via `compare()`); other types are skipped.
  Empty set → `nullopt`.
- `groupBy`: group key is the value at `keyPath` (missing → `null` group);
  result is sorted by key (`ValueLess`, int/double equivalent).
  `count` = documents per group; `value` only set when `op != Count`.
- Queries use existing indexes (candidates via `indexHint`); the full expression is
  checked per candidate.

- **v0.27:** `AggOp::Distinct` – returns an **array of the field's distinct values** as the
  aggregate (sorted, missing/null values not counted); with `groupBy` it lands in
  `GroupAggregate::value`. Ideal for FD detection (a group with exactly 1 distinct
  value = functional dependency).

# Optional collection limits (FA-28, v0.39)

| Field | Value |
|---|---|
| Status | implemented (v0.39), **opt-in** |
| Reference | NFA-02 (memory), `docs/en/internals/hardening.md` (resource bounds), hybrid/spill v0.18 as RAM alternative |

InMemDB stays **unlimited** in its core (default: all limits 0). Whoever needs a bound
against memory bombs/unbounded growth sets it per collection:

```cpp
memdb::CollectionLimits lim;
lim.maxDocuments         = 5'000'000; // RAM / avg document size, with headroom
lim.maxDocumentBytes     = 4 * 1024 * 1024;  // 4 MiB – giant docs are the classic RAM killer
lim.maxFieldsPerDocument = 1000;
lim.maxIndexes           = 64;
coll.setLimits(lim);
coll.limits();             // read the currently set limits
```

## Semantics

- Exceeding a limit → **`LimitError`** (subclass of `memdb::Error`) **before the mutation** –
  `insert`, `insertBulk`, `loadJson`, `update`/`updateById`, `replace`, `upsertById`,
  `createIndex` are thus atomic against limit violations (bulk pre-checks all docs).
- Violations are counted in **`CollectionMetrics::limitViolations`** (`Collection::metrics()`,
  `Database::metrics().sum`, `resetMetrics()`).
- Also applies inside **transactions** (rollback restores everything) and incl. **spill docs**
  (`doSize()` counts RAM + spill).
- `maxDocumentBytes` is measured as the serialized JSON size (only when set – otherwise
  no overhead).

## Rules of thumb (heuristics, not hard rules)

| Limit | Rule of thumb |
|---|---|
| `maxDocuments` | available RAM / avg document size; e.g. 8 GB RAM, ~1 KB/doc → ~8M, set 1–5M with headroom |
| `maxDocumentBytes` | 1–16 MiB |
| `maxFieldsPerDocument` | ~1000 (very wide schema-less docs slow down scans/indexes) |
| `maxIndexes` | ~32–64 (each index costs RAM + write amplification) |

For "larger than RAM" the **hybrid/spill mode** (v0.18, `setOverflow`) remains responsible –
limits bound the *count/size*, spill bounds the *RAM usage*.

## Tests
`tests/test_limits.cpp` (34 checks): default unlimited, each limit individually (insert/bulk
atomicity/update/replace/upsert/loadJson/createIndex), metric `limitViolations`, tx rollback,
database aggregation.

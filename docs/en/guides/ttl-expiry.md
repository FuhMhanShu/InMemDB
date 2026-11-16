# TTL / auto-expiry – FA-25, v0.26

| Field | Value |
|---|---|
| Status | implemented (v0.26) |
| Reference | requirements FA-25 (optional) |

Documents can get an **expiry time** (`expiresAt`, unix seconds as `Int`).
Expired documents (`expiresAt <= now`) are removed automatically.
**Opt-in: off by default** – documents without `expiresAt` never expire, existing
databases behave unchanged.

## API

```cpp
// expiry time at insert (chainable Document methods):
coll.insert(Document{{"session", "abc"}}.expiresIn(3600));   // now + 3600 s
coll.insert(Document{{"token", "xyz"}}.expiresAt(1893456000)); // absolute (unix seconds)

// set expiry on an existing document (update event, schema/unique check):
coll.expiresIn(id, 60);        // now + 60 s
coll.expiresAt(id, 1893456000); // absolute
std::optional<std::int64_t> e = coll.expiresAtOf(id); // currently set value

// collection default TTL for new docs (insert/upsert; 0 = off, default):
coll.setDefaultExpiresIn(3600);  // every new doc without expiresAt gets now+3600

// manual sweep (e.g. from your own application timer):
std::size_t n = coll.expireNow(); // number of removed documents

// inject clock for tests (default: std::time(nullptr)):
coll.setClockForTesting([] { return myFakeNow; });
```

## Semantics

- **Lazy sweep:** on read accesses (`findById`, `find`, `findIds`, `forEach`, `findOne`,
  `count`, `aggregate`, `groupBy`, `size`, `toJson`) expired documents are removed first
  (once, with a `ChangeType::Remove` event and the `expiredDocs` metric). Cheap as long as
  no document has `expiresAt` (only one atomic check).
- **No sweep in the middle of transactions/merges** (`txLocks > 0` → snapshot isolation);
  after commit the sweep happens on the next read access.
- **No background thread:** the library starts no threads; `expireNow()` is the manual
  sweep for your own timers.
- **Schema (FA-20):** with a strict schema `expiresAt` must be declared, otherwise it is
  not set automatically (also not via default TTL); setting it manually without
  declaration → `SchemaError` like any other undefined field.
- **Hybrid/spill (v0.18):** spilled documents are checked during the sweep and removed
  from the spill index (file is cleaned up by `compactSpill()`).
- **Snapshot (FA-14):** `expiresAt` is a normal field and is persisted; expired documents
  are not written to the snapshot after loading (sweep before `toJson`).
- **Transaction rollback (CoW):** the expiry count is rebuilt correctly on rollback.
- **Metrics:** new counter `CollectionMetrics::expiredDocs` (also in `DatabaseMetrics::sum`).

## Tests

`tests/test_ttl.cpp` (47 checks): default off, `expiresIn`/`expiresAt` (relative/absolute),
lazy sweep in all read paths, `expireNow`, default TTL (manual `expiresAt` wins, disable
with 0), observer events + metrics, tx guard + sweep after commit, rollback, spill case,
snapshot roundtrip, schema interaction.

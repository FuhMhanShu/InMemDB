# Merging collections (merge) – FA-24, v0.24

| Field | Value |
|---|---|
| Status | implemented (v0.24; convenience wrapper `mergeAllCollections` v0.25) |
| Reference | requirements FA-24 (optional) |

`Database::mergeCollections` merges the documents of several source collections into one
target collection ("union" at collection level).

## API

```cpp
memdb::MergeOptions opts;                 // dropSources=false, sourceField="_source", mergeIndexes=true
memdb::MergeStats st = db.mergeCollections("target", {"old1", "old2"}, opts, &refs /* optional */);
// st.merged, st.perSource["old1"], st.adoptedIndexes, st.droppedSources, st.rewrittenReferences
```

- If `target` does not exist it is created. Duplicates in `sources` are ignored;
  `target` in `sources` or empty `sources` → `Error`; unknown source →
  `CollectionNotFoundError`.
- **New IDs**: document IDs are per collection, so every merged document gets a **new ID**
  in the target. If `sourceField` is non-empty, that field is set to the source collection's
  name (overwriting a same-named field) – provenance stays traceable.
- **Indexes**: with `mergeIndexes=true` (default) the index definitions of the sources are
  adopted where not present in the target (union; existing target indexes stay). A unique
  index violation → `UniqueConstraintError`.
- **Dropping sources**: `dropSources=true` removes the source collections after a successful
  merge. If a `ReferenceIndex*` is also passed, its references are rewritten
  (`from_key`/`from_coll`/`from_id` and `to_*` from `source#id` to `target#newId`,
  `source#` to `target#`) so no dangling refs arise (`ReferenceIndex::rewriteKeys`). Without a
  `ReferenceIndex*`, refs to deleted sources become dangling (`dangling()`/`pruneDangling()`).
  The reference store itself must not be a source.
- **Atomicity**: the merge runs under exclusive locks of all involved collections (in sorted
  name order, deadlock-free). If a document throws `SchemaError` (FA-20 schema of the target)
  or violates a unique index, **all documents inserted so far are rolled back** (`nextId` is
  reset) and the exception is propagated. An active transaction on an involved collection is
  not possible during the merge (the locks exclude it; the merge itself is atomic).
- **Observer**: one `ChangeType::Insert` event fires per inserted document (after releasing
  the locks); metrics (`inserts`, `changeEvents`) count along.
- **Hybrid/out-of-core (v0.18)**: spilled documents of the sources are read along
  (`CollectionState::doSnapshotAll`); eviction is suppressed during the merge via `txLocks`
  so the rollback can stay in RAM. The spill file of a deleted source remains on disk
  (in-memory DB: no file lifecycle management).

## Example

```cpp
memdb::Database db("shop");
memdb::ReferenceIndex refs(db);
// ... db.collection("customers_old"), db.collection("customers_new") filled ...
memdb::MergeOptions opts;
opts.dropSources = true;                 // remove old collections afterwards
auto st = db.mergeCollections("customers", {"customers_old", "customers_new"}, opts, &refs);
// all documents in "customers" with _source="customers_old"/"customers_new", references rewritten

// convenience: merge ALL collections of the database into one target collection (v0.25)
auto all = db.mergeAllCollections("merged", opts, &refs); // default target name: "merged"
// "merged" does not exist? it is created. The reference store (__references)
// is automatically excluded from the sources when refs != nullptr.
```

## Tests

`tests/test_merge.cpp` (68 checks): basic merge with new IDs + `_source`, target is created,
error cases (empty/target=source/unknown), SchemaError + rollback, UniqueConstraintError +
rollback, index adoption incl. index usage (`metrics().indexQueries`), dropSources,
reference rewriting (no dangling refs, `joinIds` works afterwards), observer events,
spill source, `sourceField=""`.

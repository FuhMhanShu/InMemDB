# References between collections/documents (FA-18) – implemented in v0.8

| Field | Value |
|---|---|
| Status | implemented (v0.8) |
| Reference | requirements FA-18 (optional), outlook section 10 |

## Idea

`ReferenceIndex` is an **extension module without core changes** (NFA-03, open/closed): it uses
only the public API (`Database`, `Collection`, `Expression`, indexes). References are stored
as documents in a reserved collection (default `__references`) and are therefore automatically
transaction-, index- and snapshot-capable.

References are **name-based ("relative")**: they store collection names + `DocumentId` as data,
not as handles. They therefore resolve against the respective `Database` at runtime and survive
`toJson`/`fromJson`/`saveToFile`/`loadFromFile`.

## API

```cpp
ReferenceIndex refs(db);                       // creates "__references" incl. indexes

Reference r = refs.link("users", userId, "orders", std::nullopt, "has-orders");
// fromId/toId = nullopt  ->  reference to the collection as a whole
// toCollection may be a relative path: "./users", "../orders", "/abs" (see resolve)

refs.unlink("users", userId, "orders", std::nullopt, "has-orders");   // true on success

auto out = refs.references("users", RefDirection::Outgoing, userId);        // from this record
auto in  = refs.references("orders", RefDirection::Incoming);               // to this collection
auto both = refs.references("users", RefDirection::Both, userId);

auto bad = refs.dangling();        // references whose source/target no longer exists
std::size_t n = refs.pruneDangling();  // removes exactly those

auto edges = refs.traverse("users", userId, RefDirection::Outgoing);   // DFS: all reachable edges
auto few  = refs.traverse("users", userId, RefDirection::Outgoing, 2); // maxDepth = 2 hops
bool ring = refs.hasCycle("users", userId);                            // cycle detection

auto hits = refs.search("users", userId, Field("status") == "open");  // traversing search
// SearchHit { collection, id, document, path } — path = edges from the start node to the hit

// v0.17: index-accelerated join in queries (instead of traversing per document):
auto orderIds = refs.joinIds("orders", "customer", "users", Field("city") == "Berlin");
// -> IDs of the orders whose customer lives in Berlin
// flow: toQuery on users (index!) -> target IDs -> to_key index -> source IDs. O(#target hits).
auto docs     = refs.join("orders", "customer", "users", parseQuery("vip == true")); // documents directly

std::string t = refs.resolve("a/b", "../c");   // -> "c"
```

**Join vs. `@relation.path` in the DSL** (`docs/en/guides/query-dsl.md`): `@customer.city == 'Berlin'`
resolves per source document via the references (correct, but O(n) with one index lookup per
document). `joinIds`/`join` invert the direction and are O(#target hits) – the much faster
variant for large source collections.

`struct Reference { std::string fromCollection; std::optional<DocumentId> fromId;
std::string toCollection; std::optional<DocumentId> toId; std::string relation; }`

## Storage format

Every reference is a document in `__references`:

```json
{ "from_key": "users#5", "from_coll": "users", "from_id": 5,
  "to_key": "orders#",  "to_coll": "orders", "to_id": null,
  "relation": "has-orders" }
```

- `from_key`/`to_key` = `"<collection>#<id>"` (empty without ID) as combined index fields.
- Indexes on `from_key` and `to_key` (non-unique) accelerate both directions:
  outgoing = `Field("from_key") == key`, incoming = `Field("to_key") == key`.

## Semantics

- **Both directions traceable:** outgoing via `from_key`, incoming via `to_key` – each with index.
- **Relative paths (`resolve`)**: `"./x"`, `"../x"`, `"a/b"` are resolved against the directory of
  the source collection; `"/x"` is absolute; a bare name without `/` stays unchanged
  (backward compatible with the flat collection namespace). `..` at the root is cut off;
  an empty result throws `ReferenceError`.
- **Validation on `link`:** source/target collections must exist (`CollectionNotFoundError`),
  given document IDs must exist (`DocumentNotFoundError`). Duplicate (same from/to/relation)
  throws `ReferenceExistsError`.
- **Dangling references:** when source/target documents are deleted or collections dropped,
  references are not removed automatically but reported by `dangling()` and removed by
  `pruneDangling()` (lazy instead of eager – so there are no gaps with newly created collections).
- **Traversal (`traverse`):** DFS from a start node (record or collection level), direction
  selectable via `dir`, `maxDepth` limits the hop count. Visited nodes are deduplicated via a
  visited set (`"coll#id"`), edges additionally via their identity – so **traversal always
  terminates even with cycles**. With `RefDirection::Both` every edge is treated as undirected
  (traversal into the "other end").
- **Cycles:** deliberately allowed on `link` (domain-legitimate; only exact duplicates throw
  `ReferenceExistsError`). `hasCycle()` detects cycles via DFS colors (gray = on the stack); with
  `Both` with parent-skip so the edge back to the predecessor creates no false positive.
  Storage, snapshot and `dangling()` are unaffected by cycles (no recursion).
- **Traversing search (`search`):** DFS from a start node (record or collection level,
  `fromId = nullopt` expands to all documents of the collection). Every reached document is
  tested with query `q` (`Expression{}` matches all); hits contain the edge path from the start.
  Cycle-safe via visited set, `maxDepth` limits hops, dangling targets are skipped.
- **Snapshot:** `__references` is a normal collection and is serialized by `Database::toJson`
  (incl. indexes) — references survive save/load.
- **Transactions:** changes to references are atomic via `__references.beginTransaction()`.
  Limitation: FA-12 transactions are per collection; a reference change together with a
  document change in a *different* collection is not cross-collection atomic.
- **Thread safety:** like the core – all accesses go through the RW locks of the involved collections.

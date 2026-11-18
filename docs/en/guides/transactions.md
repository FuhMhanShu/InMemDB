# Transactions (FA-12) – implemented in v0.3

| Field | Value |
|---|---|
| Status | implemented (v0.3) |
| Reference | requirements FA-12 (should), FA-16 |

## API

```cpp
Collection accounts = db.collection("accounts");
{
    Transaction tx = accounts.beginTransaction();   // exclusive lock on the collection
    tx.insert({{"name", "C"}, {"balance", 5}});
    tx.updateById(id, [](Document& d) { d.set("balance", 0); });
    tx.update(Field("balance") > 1000, [](Document& d) { /* ... */ });
    tx.remove(Field("blocked") == true);
    tx.commit();        // or tx.rollback(); the destructor rolls back automatically without commit
}
```

Inside the transaction all read/write operations are available
(`insert`, `update`, `updateById`, `replace`, `remove`, `removeById`, `find`,
`findById`, `count`, `size`) and see their own not-yet-committed changes
(read-your-own-writes).

## Semantics
- **Isolation:** `beginTransaction()` holds a `unique_lock` on the collection's
  `shared_mutex` – meanwhile all other accesses (reads AND writes) block. The
  transaction is thus fully serialized – stronger than snapshot isolation, but
  without MVCC: **readers block for the whole transaction duration** (finding
  M3, clarified in v0.44: `Transaction::Impl` holds the `unique_lock` from
  `beginTransaction()` until `commit()`/`rollback()`, `src/transaction.cpp`).
  Tradeoff: simple, correct isolation at collection level at the price of
  blocking readers during long transactions.
- **Atomicity/rollback (copy-on-write, v0.19):** On begin only an undo log is
  activated (`CollectionState::undo`); every write operation logs the old state of
  each document before its first change (`noteUndo`). `rollback()` – or the
  destructor without `commit()` – replays the undo log and rebuilds the index
  entries (all-or-nothing). Cost: O(1) for `beginTransaction()`/`commit()`,
  O(touched documents + reindex) only on rollback – instead of an O(collection)
  full backup on every begin (up to v0.18).
- **Commit:** changes persist, the lock is released.
- After `commit()`/`rollback()` any further call throws `Error`
  ("transaction is no longer active").
- Unique constraints (FA-10) also apply inside the transaction; a violation throws
  `UniqueConstraintError`, the transaction stays active and can then be rolled
  back deliberately.
- Index usage (FA-10) works inside the transaction just as outside.

## Implementation note (v0.19: CoW undo)
Rollback via undo log (copy-on-write): `beginTransaction()` costs O(1) + O(#indexes)
(the index structure is saved as a path/unique list, not the entries). Every mutation
calls `CollectionState::noteUndo(id)`/`noteUndoClear()` before it takes effect; the
undo log holds at most one entry per touched document. `restore()` replays the log,
restores `nextId`/`spillIndex` and rebuilds the indexes completely (only on rollback,
never on commit).

Measured (1M documents, Release, x86-64): `beginTransaction()` + 1 change +
`commit()`: 835 ms (v0.18, full backup) → **0.3 ms** (v0.19); rollback of the same tx:
1911 ms → **558 ms** (reindex dominates, only paid on rollback). Hybrid mode: evict is
blocked during the tx (`txLocks`); on rollback the spill file is truncated to the
pre-tx state, on commit it is cleared after a clear.

Internally: `src/collection_state.hpp` encapsulates the collection state
(`memdb::detail::CollectionState`) with lock-free `do*` operations; both `Collection`
(with `shared_lock`/`unique_lock`) and `Transaction` (exclusive lock + CoW undo) call
the same operations.

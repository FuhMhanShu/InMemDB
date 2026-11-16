# Hybrid / out-of-core (v0.18)

| Field | Value |
|---|---|
| Status | implemented (v0.18, opt-in, optional) |
| Reference | requirements outlook section 10 (out-of-core/hybrid mode) |

InMemDB remains an in-memory system; **optionally** a collection can spill part of its
documents to a file when RAM runs low.

## API

```cpp
Collection c = db.createCollection("big");
c.setOverflow("big.spill", 100000);   // max 100k documents in RAM, rest into the spill file

c.insert(...);                        // once exceeded: oldest doc (FIFO) is spilled
c.findById(id);                       // reads from the spill on demand
c.count("i >= 0");                    // scans see RAM + spill
c.updateById(spilledId, ...);         // writes a new version to the spill (append-only)

OverflowInfo info = c.overflowInfo(); // enabled, inMemory, spilled, spillBytes, ...
std::uint64_t freed = c.compactSpill(); // compact the spill file (drop old versions)

c.disableOverflow();                  // everything back into RAM, spill file is deleted
```

## How it works

- Spill file = append-only log of records `[u64 id][u32 len][JSON document]`; in RAM
  only the index `id → offset` lives (small).
- **Evict:** after `insert`/`insertBulk`, when `maxInMemoryDocs` is exceeded the documents
  with the lowest IDs (FIFO) are appended to the spill and removed from RAM + indexes.
- **Load:** `findById` and all scans (`find`/`count`/`forEach`/`findIds`/`aggregate`/`groupBy`)
  read spilled documents on demand from the file. Updates/removes/replaces of spilled
  documents append a new version or remove the index entry.
- **Recovery:** when `setOverflow` is called on an existing file, the index is rebuilt
  from the log (last version per ID wins).
- **Transactions:** no evict happens during an active transaction (`txLocks`); the tx
  backup also covers the spill index, rollback restores both. `setOverflow`/
  `disableOverflow` during an active transaction throw `Error`.

## Notes / limitations
- **Off by default** – without `setOverflow` everything behaves as before (pure RAM).
- Indexes only cover the RAM documents; unique constraints therefore apply in hybrid mode
  only to the "hot" RAM set (documented; for strict unique + hybrid: choose the RAM limit
  accordingly).
- Scans over spilled documents read the file sequentially (O(total)); for pure RAM
  performance, restrict `findIds`/`forEach` to the RAM subset if possible.
- `compactSpill()` cleans overwritten/deleted records from the file.

## Tests

`tests/test_hybrid.cpp` (35 checks): evict/load, scans over RAM+spill, findById/updateById/
remove on spilled docs, tx behavior, compactSpill, recovery on reopen, disableOverflow;
clean under ASan/UBSan.

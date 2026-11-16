# Observer / change callbacks (FA-15) – implemented in v0.7

| Field | Value |
|---|---|
| Status | implemented (v0.7) |
| Reference | requirements FA-15 (optional) |

## API

```cpp
std::uint64_t h = coll.onChange([](const ChangeEvent& e) { /* ... */ });
coll.offChange(h);   // true on success
```

`ChangeEvent { ChangeType type; std::string collection; DocumentId id; Document document; }`
- `Insert`/`Update`/`Replace`: `document` = new state
- `Remove`: `document` = removed document (old state)
- `Clear`: a single event, `document` empty

## Semantics
- Events fire **after the operation completes** and **outside the collection lock**
  (a callback may safely access the same collection again, e.g. `count()`).
- Event order matches execution order; multiple observers are all notified.
- **Transactions:** events are buffered and **fired only on `commit()`** –
  observers therefore only see committed states. On `rollback()` they are discarded.
- `loadJson` produces `Clear` + one `Insert` per document.
- Index changes (`createIndex`/`dropIndex`) produce no events (schema, not data).

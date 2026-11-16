# Error handling / exception policy (v0.35)

| Field | Value |
|---|---|
| Status | implemented (v0.35); MessageHandler v0.36 |
| Reference | NFA-05 (reliability), FA-12/FA-15/FA-20, ADR-01/ADR-02 |

Principle: **exceptions are for real errors** (API misuse, constraint violations,
I/O errors, broken input files) and are deliberately **not** caught at those points –
the caller should see and handle them. Catching happens only where an exception would
otherwise **abruptly break the program flow in the middle of an ongoing operation**
(data already partially written, the rest of the work abandoned).

## Where we do NOT catch (errors stay visible)

| Situation | Exception |
|---|---|
| Unique index violated (`insert`/`update`/merge) | `UniqueConstraintError` (atomic rollback) |
| Schema violation (FA-20) | `SchemaError` |
| Collection already exists / missing | `CollectionExistsError` / `CollectionNotFoundError` |
| Reference to a missing document (`ReferenceIndex::link`) | `DocumentNotFoundError` |
| Query construction with invalid DSL/pattern (`find("name.matches('(')")`) | `QueryError` (with position) |
| JSON syntax error, broken/missing file, wrong password | `JsonError` / `Error` / `CryptoError` |
| `Value::asString()` on a number etc. | `TypeError` |
| Explicit single evaluation `Expression::evaluate(doc)` | throws through (only the **scan loops** catch, see below) |

`findById`, in contrast, deliberately returns `std::optional<Document>` (no throw on "not found").

## Where we DO catch (the program flow continues)

### 1. Query scan per document (`metrics().evalErrors`)
In all scan/update/remove loops (`find`, `findOne`, `findIds`, `forEach`, `count`,
`update`, `remove`, aggregations) the evaluation is encapsulated **per document** in `safeEval`:
if the evaluation throws (e.g. a custom DSL function from the `QueryFunctionRegistry` that
throws, or a regex match budget, v0.34), the document counts as a **non-match** and the error
is counted in `CollectionMetrics::evalErrors` – the scan runs to completion.
```cpp
c.find("v.myFunction()");   // throwing function -> 0 matches, no abort
c.metrics().evalErrors;      // number of caught eval errors
```
At **query construction** (`parseQuery`/`Expression::matches`) a `QueryError` is still thrown
immediately – syntax errors are programming errors and should surface early.

### 2. Observer callbacks (`metrics().eventErrors`)
Change events are delivered **after** the write operation (the data is already written at that
point). A throwing callback must therefore neither abort the write op nor skip the remaining
observers: `fireEvents` catches per callback, all observers run, and the error lands in
`CollectionMetrics::eventErrors`.
```cpp
c.onChange([](const ChangeEvent&) { throw std::runtime_error("log broken"); });
c.insert({{"x", 1}});          // succeeds anyway
c.metrics().eventErrors == 1;
```
In transactions, events are only delivered on commit (FA-15) – the same protection applies there.

### 3. Bulk load: validate before mutating (atomic, no partial state)
`Collection::loadJson` / `Database::fromJson` check **all elements first** and throw
`JsonError` (with collection name) **before** anything is cleared or created. With a
broken element the collection/database therefore remains completely unchanged – the caller
can handle the error (e.g. skip the element, like the importer with `skippedTables`)
and retry the load, instead of having to rescue a half-emptied state.
```cpp
try {
    c.loadJson(json);
} catch (const JsonError& e) {
    // c is unchanged – continue the flow, e.g. filter the broken element and retry
}
```

### 4. CLI tools: clean exit instead of `terminate`
All tools (`inmemdb_import`, `inmemdb_graph`, `inmemdb_discover`) wrap the complete run
incl. option parsing: even non-`memdb::Error` exceptions (e.g. `std::stod`/`std::stoull` on
`--batch xyz`) are reported as `Error: …` on stderr with **exit code 1** – no
`terminate`/stacktrace. Invalid options without a value → exit code 2 with a usage hint.

## Metrics
The two new counters are visible in `CollectionMetrics` / `DatabaseMetrics`
(`docs/en/guides/metrics.md`): `evalErrors`, `eventErrors` (resettable via `resetMetrics()`).

## Tests
`tests/test_error_handling.cpp` (32 checks): throwing observer (op succeeds, `eventErrors`,
all observers run), throwing DSL function in a scan (no abort, `evalErrors`,
`update` without effect), invalid `matches` pattern throws at query construction, `loadJson`/`fromJson`
stay unchanged on a broken element, real errors (unique/schema/missing collection/
wrong password) still throw, `Expression::evaluate` stays throwing.

---

## Message handler (singleton, v0.36)

Additionally to catching, every protected point reports its errors to the central
**`memdb::MessageHandler`** (singleton, thread-safe, `include/memdb/message_handler.hpp`).
Register your own handlers (logging, UI, monitoring, tests) via `subscribe()`:

```cpp
memdb::MessageHandler::instance().subscribe([](const memdb::Message& m) {
    std::cerr << "[" << (int)m.level << "] " << m.source << ": " << m.text << "\n";
});
// level: Debug/Info/Warning/Error; filter via setMinLevel(MessageLevel::Warning)
// counters: count(level), totalCount(), handlerErrors() (throwing handlers are caught themselves)
```

| `source` | When reported |
|---|---|
| `query-eval` | eval error per document in a scan (level Error; + `metrics().evalErrors`) |
| `observer` | throwing observer callback (level Error; + `metrics().eventErrors`) |
| `load` | bulk JSON load with a broken element (level Error; `JsonError` is **still thrown** in addition) |
| `import` | import skips (schema type mismatch, reference duplicate; level Warning) |
| `regex` | `matches` eval: invalid/over-expensive pattern or match budget (level Error; document = non-match) |
| `cli` | CLI tools report errors via the handler (the tools themselves subscribe a stderr printer from Warning up) |

Without registered handlers, messages are only counted. `publish()` is thread-safe, handlers
run outside the lock; a throwing handler callback disturbs neither other handlers nor the
caller (`handlerErrors()`).

Tests: `tests/test_message_handler.cpp` (30 checks: subscribe/unsubscribe, level filter,
MT publish 4×1000, throwing handlers, integration at observer/query-eval/load).

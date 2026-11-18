# Hardening the framework – overview of all protective mechanisms (as of v1.0)

| Field | Value |
|---|---|
| Status | actively maintained; latest stages v0.34 (regex), v0.35 (exception policy), v0.36 (MessageHandler), v0.37 (JSON), v0.38 (DSL/BSON/numbers), v0.39 (collection limits) |
| Principle | **never crash/hang** on external or pathological input – instead typed exceptions (`JsonError`, `QueryError`, `Error`, …) with position/offset; where the flow should continue, errors are caught + counted/reported |

## 1. Hard limits against pathological input (crash/hang protection)

| Component | Limit | Reaction |
|---|---|---|
| Mini regex (`src/regex.cpp`, v0.34) | pattern ≤ 4096 chars; nesting ≤ 256; NFA ≤ 50000 instructions; match ≤ 100M steps | `std::invalid_argument` (pattern, with position) or `std::runtime_error` (match budget) |
| JSON parser/writer (`src/json.cpp`, v0.37) | nesting ≤ 1000 (parser, schema parser, writer) | `JsonError "nesting too deep"` (before: segfault at ~50000 `[`) |
| DSL parser (`src/query_dsl.cpp`, v0.38) | nesting ≤ 256 (`(` and `!`) | `QueryError` with position (before: segfault at ~50000 parens) |
| BSON import (`src/import_bson.cpp`, v0.38) | nesting ≤ 1000; all lengths checked against end of file (`need()`) | `Error "BSON: nesting too deep"` (before: segfault at ~20000) |
| Spill read (hybrid, v0.18) | record length ≤ 64 MiB, ID must match, JSON must be a document | `std::nullopt` (doc treated as missing) |
| Encrypted snapshots (FA-19) | magic + PBKDF2 iterations ≤ 10M from file; HMAC via `constantTimeEqual` (no timing leak) | `CryptoError` |
| JSON numbers | > 63 chars → error; hex-float fallback only with `0x`; locale-independent (`std::from_chars`) | `JsonError` |
| DSL numbers (v0.38) | strict `from_chars` check; int64 overflow → double | `QueryError` (before: `"1.2.3"`/`"1e"` silently 0) |

## 2. Exception policy (v0.35, `docs/en/internals/error-handling.md`)
Exceptions stay visible for **real errors** (unique/schema, missing collection/doc, query construction, crypto/IO). Caught only where an exception would otherwise abort mid-operation:
- **Query scan per document** (`safeEval`): eval error → document = non-match, counter `metrics().evalErrors`
- **Observer** (`fireEvents`): throwing callback aborts neither the write op nor the remaining observers, counter `metrics().eventErrors`
- **Bulk JSON load** (`loadJson`/`fromJson`): validates all elements **before** the mutation → no partial state, `JsonError` remains
- **CLI tools**: whole run in try/catch → `Error: …` + exit 1 instead of `terminate`

## 3. MessageHandler (v0.36, `include/memdb/message_handler.hpp`)
Singleton, thread-safe; register your own handlers via `subscribe()`. The catch points report there: `query-eval`, `observer`, `load`, `import`, `regex`, `cli`. Levels Debug/Info/Warning/Error, filter `setMinLevel`, counters `count()/totalCount()/handlerErrors()` (throwing handlers are caught themselves).

## 4. How do we test that?
- **Attack probes** (reproduced before every fix): deep nesting, nested `{n,m}`, alternation chains, cyclic Values, broken `$binary` markers, random strings
- **Fuzz-lite** in the test suites (deterministic seed): `test_regex.cpp` (2000 patterns), `test_json_hardening.cpp` (4000 strings), `test_hardening.cpp` (3000 DSL strings) — never a crash, only typed exceptions
- **ASan/UBSan** clean on the hardening test suites
- **Fault injection** (recommendation for persistence/WAL, see outlook): truncate/corrupt the file at every byte offset → recovery must fail cleanly

## 5. Tests
`tests/test_regex.cpp` (5921 checks) · `test_json_hardening.cpp` (43) · `test_hardening.cpp` (13) · `test_error_handling.cpp` (32) · `test_message_handler.cpp` (30) – plus the regression suites (40 CTest entries in total).

## 1b. Resource limits per collection (v0.39, FA-28, `docs/en/guides/limits.md`)

Opt-in via `Collection::setLimits(CollectionLimits)` – default unlimited (0):
`maxDocuments`, `maxDocumentBytes` (JSON size), `maxFieldsPerDocument`, `maxIndexes`.
Violation → `LimitError` **before the mutation** (bulk atomic), counter `metrics().limitViolations`.
Rules of thumb: `maxDocuments` ≈ RAM/avg doc size, `maxDocumentBytes` 1–16 MiB, fields ~1000, indexes ~32–64.
RAM protection for "larger than RAM" remains the spill mode (v0.18).

## 6. Analysis pipeline (FA-29 to FA-34)

The analysis modules are deliberately **computationally hardened** instead of throwing – analysis
on pathological data must produce a result, not a crash and no `inf`/`nan` in the report:

| Component | Hardening |
|---|---|
| Pearson correlation (FA-30/31/32) | two-pass **centered** computation in `long double`; the textbook formula (`n·Sxy − Sx·Sy`) loses massive precision for large values with a small variation range (catastrophic cancellation); value slots assigned consistently to the sorted field pair (per-document field order may vary); result clamped to [−1, 1] |
| Moments (mean/stddev/skewness/kurtosis, FA-30) | accumulated in `long double` (no overflow on extreme values), finiteness check on write-back – reports stay valid JSON |
| Histogram (FA-30) | non-finite range (±DBL_MAX) → all values in bin 0 instead of a `static_cast` of NaN |
| Quantiles/IQR/sequence (FA-30) | interpolation in `long double` with a finiteness fallback – the same defect as FA-33: opposing extreme values (±1e308) turned Q1/median/Q3 into `inf` (invalid JSON); IQR with a finiteness check (q3 − q1); non-representable sequence jumps (> DBL_MAX) are skipped, `stepMedian` computed only from finite differences |
| R² regression (FA-31/32) | Gauss solver with partial pivoting + **finiteness check** of the solution; **mean-centred** normal equations without an intercept column – large offsets (e.g. 1e12) would otherwise drown the tiny residual variance in rounding noise; `SST` checks; `R²` either finite or `rSquaredComputed = false` |
| Influence score (FA-31/32) | `influenceScore` guarded to 0 if non-finite – NaN would otherwise break the `std::sort` ordering (strict weak ordering) |
| Join sources (FA-32) | dangling edges (deleted documents) no longer count toward `rows`/`matchedA`/join coverage; `__references` entries with negative, non-integral, or NaN IDs are ignored |
| Field filter (FA-30) | fields without numeric values are skipped even with `minValuesPerField = 0` (no access to empty vectors) |
| Quantile/median (FA-33) | interpolation in `long double` with a finiteness check on write-back – `xs[lo+1] − xs[lo]` overflows to infinity in `double` for opposing extreme values (±1e308) and turned the median into `inf` (invalid JSON) |
| Depth (FA-34) | hard depth cap of 64 independent of `maxDepth` – with generous options (`maxDepth` up to 1e6) the analysis recursion would otherwise overflow the stack on deeply nested structures; cap reported transparently as a note; distinct cap transparent in the path `note`; empty fingerprint (document without paths) without empty field entries |
| Moments/text statistics (FA-33) | number statistics in `long double` with finiteness checks; text classes guarded by thresholds (`maxCategoryDistinct`, `identifierUniqueness`, `freeTextMinLength`); Pearson centered two-pass as in FA-30, eta²/Cramér's V clamped to [0, 1]; association guards (`minPairsForAssociation`, `maxDistinctForAssociation`, constant fields excluded) |

Regression tests: `test_numeric_analysis.cpp` (136 checks), `test_influence.cpp` (130 checks),
`test_cross_influence.cpp` (193 checks), `test_character.cpp` (62 checks),
`test_mixed_analysis.cpp` (184 checks), `test_complex_analysis.cpp` (197 checks) –
each covering extreme-value, offset, and guard scenarios.

## 7. NaN/Inf ordering in the Value order & indexes (v0.43, finding M1)

NaN must not break the strict weak ordering (`ValueLess`) – otherwise equivalence in the
`std::map` indexes is not transitive and the tree is undefined (corruption, endless loops in
`find`, wrong unique checks and query results). **Fixed (Postgres-style):** NaN is greater than
every number (including +Inf), NaN == NaN; `equal(NaN, NaN)` = true, `equal(NaN, x)` = false for
numbers. Implemented centrally in `compare()`/`equal()` (`src/value.cpp`) – all paths (indexes,
`groupBy`, `distinctValues`, min/max, discovery sets) benefit. The JSON parser accepts the
tolerant literals `nan`/`-nan`/`inf`/`-Infinity` (any case); on write, NaN/Inf still become
`null` in the snapshot. Regression tests: `tests/test_nan_ordering.cpp` (100 checks, incl.
ASan/UBSan).

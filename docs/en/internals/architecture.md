# Architecture decisions – v1.0

## ADR-01: compiled static library with Pimpl (instead of header-only)

| Field | Value |
|---|---|
| Status | decided (v0.1), in effect |
| Reference | requirements section 2, NFA-06 |

### Decision
InMemDB is shipped as a **static C++ library**. All stateful classes
(`Database`, `Collection`, `Document`, `Expression`, `Transaction`, the `ImportSource`
subclasses) use the **Pimpl idiom**: the public header contains only the declaration plus
`std::unique_ptr<Impl>` / `std::shared_ptr<Impl>`; the implementation lives in `src/*.cpp`.

Header-only was deliberately rejected (consumer compile times, no ABI stability when the
core changes).

### Layout (v1.0)
```
include/memdb/    public headers (API, ABI-stable)
  memdb.hpp       umbrella header
  value.hpp       value type (variant-based)
  document_id.hpp document ID (value type)
  document.hpp    document with dynamic fields
  query.hpp       Expression/expression tree + operators (+ matches/in/predicate, refField)
  query_dsl.hpp   string DSL: parseQuery + QueryFunctionRegistry (NFA-03)
  cursor.hpp      result cursor
  collection.hpp  Collection (CRUD, queries, DSL overloads)
  transaction.hpp transaction (FA-12)
  database.hpp    Database (manages collections)
  index.hpp / aggregate.hpp / change_event.hpp / schema.hpp
  references.hpp  FA-18 ReferenceIndex (extension module)
  import*.hpp     FA-21 import sources + ImportRegistry/Importer (csv/json/snapshot/sqldump/bson, optional sqlite)
  db_graph.hpp    FA-22 DbGraph (JSON/Mermaid/Markdown)
  discovery.hpp   FA-27 Discovery (v0.29–v0.40: FK/FD/correlation/near-dupes, orphans, paths, profile, diff, analyze+report, outliers)
  metrics.hpp     v0.22 metrics/diagnostics
  limits.hpp      v0.39 optional collection limits
  message_handler.hpp  v0.36 MessageHandler singleton
  qt/             FA-23 Qt model adapter (only with INMEMDB_WITH_QT, QtCore only)
  exceptions.hpp  error types
src/              implementation (not part of the ABI)
  collection_state.hpp  shared lock-free core (do* methods) for Collection + Transaction
  json.* / crypto.* / regex.*  internal parser/crypto/mini-regex (no external deps)
tests/            unit tests (assert-based, no external framework) + CTest
tools/            CLI tools: inmemdb_import, inmemdb_graph, inmemdb_discover (incl. --report v0.33)
examples/         runnable examples 01–15 (also CTest smoke tests)
bench/            memdb_bench benchmark (not part of the tests)
.github/workflows/ci.yml   CI: build+test (ubuntu/windows/macos), sqlite import, doxygen
CMakeLists.txt    library target `memdb` (STATIC) + tests via CTest + optional tools
```

### Pimpl – where and why not everywhere
- **Pimpl:** `Database`, `Collection`, `Document`, `Expression`, `Transaction`, `ReferenceIndex`,
  `ImportSource` subclasses – hide internal data structures (maps, locks, tree nodes),
  allow ABI-stable extensions.
- **No Pimpl (deliberate exception):** `Value`, `DocumentId`, `Cursor`, `Index`, the exceptions,
  the registry/options structs – those are value types. A Pimpl per `Value` would cost a heap
  allocation on every field access – unacceptable in an in-memory DB. `Value` is therefore
  a compact `std::variant` wrapper whose layout is part of the header API.
- **Handle semantics:** `Collection` holds a `std::shared_ptr<Impl>` – copies are handles to
  the same underlying collection.

### Internal core: `detail::CollectionState` (since v0.3)
All state-changing operations live as lock-free `do*` methods in
`src/collection_state.hpp` (`detail::CollectionState`) and are shared by **both** wrappers:
`Collection` (public, `shared_mutex`: shared for reads / unique for writes) and
`Transaction` (exclusive lock + CoW undo log for rollback, FA-12; since v0.19 O(1) instead of
O(n) per begin). The transaction logic is thus exactly the same code base as normal
operation – no duplicated logic.

### Extension modules without core changes (NFA-03, open/closed)
- **FA-18 references** (`references.hpp`): `ReferenceIndex` uses only the public API;
  references live as documents in a reserved collection (`__references`).
- **FA-21 import** (`import*.hpp`): abstract `ImportSource` + `ImportRegistry` (singleton factory,
  auto-detection by file extension) + `Importer`. Dependency-free sources: CSV, JSON/JSONL,
  snapshot re-import, SQL dump, BSON; optional SQLite via `-DINMEMDB_WITH_SQLITE=ON`.
- **NFA-03 string DSL** (`query_dsl.hpp`): `parseQuery` builds the same `Expression` tree as
  the C++ API; custom operators via `QueryFunctionRegistry` + `Expression::predicate`.
- **FA-22 DbGraph** (`db_graph.hpp`): reads only public API / snapshot files.

### Thread safety (FA-16)
`Database::Impl` and `Collection::Impl` each own a `std::shared_mutex`:
read accesses (`find`, `findById`, `count`, `size`) run with `shared_lock`, write accesses
(`insert`, `update`, `remove`, `replace`) with `unique_lock`. `Cursor` is a snapshot copy
(consistent view at query time). Transactions hold an exclusive lock for their duration
(full isolation, read-your-own-writes). The registries (`ImportRegistry`,
`QueryFunctionRegistry`) are thread-safe via a mutex.

### Dependencies
No mandatory dependencies; only the C++17 standard library. CMake ≥ 3.20.
Optional: `libsqlite3-dev` only with `-DINMEMDB_WITH_SQLITE=ON` (SQLite import); Doxygen only with
`-DINMEMDB_BUILD_DOCS=ON`. The default build stays dependency-free (NFA-06) and the
SQLite module is not even linked then.

---

## ADR-02: own crypto/regex/JSON – external counterparts only as opt-in, never mandatory (NFA-06)

| Field | Value |
|---|---|
| Status | decided (v0.33), in effect |
| Reference | NFA-06, FA-19 (crypto), NFA-03/`matches` (regex), FA-14 (JSON); concerns `src/crypto.*`, `src/regex.*`, `src/json.*` |

### Decision
InMemDB ships **its own implementations** and is thus 100 % dependency-free in the default build:

- **Crypto** (FA-19, `src/crypto.*`): PBKDF2-HMAC-SHA256 (key derivation), ChaCha20
  (stream cipher), HMAC-SHA256 (encrypt-then-MAC) – self-implemented, auditable,
  no license issues, identical on all platforms.
- **Mini regex** (`src/regex.*`): Thompson NFA (full pattern match, no backtracking,
  no hang on `(a*)*` thanks to a visited set); syntax: `. * + ? {n,m} [...] \d\w\s (?:...)` lazy etc.
  (v0.20). **Known limit:** no backreferences/lookaround (principally impossible in a pure
  NFA without backtracking).
- **JSON** (`src/json.*`): own parser/writer, locale-independent (`std::to_chars`/`from_chars`
  since v0.23.1), fast paths since v0.19.

**External counterparts (e.g. OpenSSL/libcrypto, PCRE2, nlohmann/json) are only allowed as opt-in:**
they may only be included behind their own CMake option (default **OFF**) + `find_package`
+ preprocessor macro – exactly like the SQLite module
(`option(INMEMDB_WITH_SQLITE ... OFF)` → `find_package(SQLite3 REQUIRED)` → code only under
`#ifdef INMEMDB_HAS_SQLITE`). In the default build none of them is searched for, compiled or
linked. Consequence: **never a mandatory dependency** (NFA-06); building InMemDB requires
only a C++17 compiler.

### Shared-lib deployment ("deploy folder")
Shipping shared libraries next to the application is **only relevant in the opt-in case** and
is the application's responsibility, not the library's:

- **Windows:** put the `.dll` next to the `.exe` (or into a folder on `PATH`) – the loader finds it automatically.
- **Linux:** `.so` next to the binary + link with `-Wl,-rpath,'$ORIGIN'` (or `LD_LIBRARY_PATH` at runtime).
- **macOS:** `.dylib` + `@loader_path` (install_name), possibly `codesign`.
- **Preferred:** link statically (`libcrypto.a`, `libpcre2-8.a`, `libjson.a`) – then there is
  no runtime dependency at all, and our own implementation needs none of them anyway.

Risks of shared-lib deployment (versioning/ABI, license compliance – OpenSSL 3 = Apache-2.0,
PCRE2 = BSD-like with PCRE exception, additional audit obligations, larger binaries): exactly the
reason why the default uses the own implementations.

### When an external backend can still make sense
- **OpenSSL/libcrypto:** hardened, FIPS-capable crypto, AES-NI hardware acceleration, broader
  algorithm selection – at the cost of larger binaries and audit effort.
- **PCRE2:** full regex syntax incl. backreferences, lookaround, Unicode properties – at the
  cost of a much larger footprint; the Thompson NFA stays the default for `matches`.
- **nlohmann/json:** more comfortable API, JSON Schema – at the cost of compile time and binary size.

Implementation (if desired, outlook): options `INMEMDB_WITH_OPENSSL` / `INMEMDB_WITH_PCRE2`
following the ADR-02 pattern; the public APIs (`saveToFileEncrypted`, `Expression::matches`,
`toJson`/`fromJson`) stay identical, only the backend switches via macro.

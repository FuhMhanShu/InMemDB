# Architektur-Entscheidungen – v0.33

## ADR-01: Kompilierte statische Library mit Pimpl (statt header-only)

| Feld | Wert |
|---|---|
| Status | beschlossen (v0.1), gültig |
| Bezug | Anforderungen Abschnitt 2, NFA-06 |

### Entscheidung
InMemDB wird als **statische C++-Library** ausgeliefert. Alle zustandsbehafteten Klassen
(`Database`, `Collection`, `Document`, `Expression`, `Transaction`, die `ImportSource`-Ableitungen)
verwenden das **Pimpl-Idiom**: Der öffentliche Header enthält nur die Deklaration plus
`std::unique_ptr<Impl>` / `std::shared_ptr<Impl>`; die Implementierung liegt in `src/*.cpp`.

Header-only wurde bewusst verworfen (Compile-Zeiten der Konsumenten, fehlende
ABI-Stabilität bei Änderungen am Kern).

### Layout (v0.33)
```
include/memdb/    Öffentliche Header (API, ABI-stabil)
  memdb.hpp       Umbrella-Header
  value.hpp       Wert-Typ (variant-basiert)
  document_id.hpp Dokument-ID (Werttyp)
  document.hpp    Dokument mit dynamischen Feldern
  query.hpp       Expression/Ausdruckbaum + Operatoren (+ matches/in/predicate, refField)
  query_dsl.hpp   String-DSL: parseQuery + QueryFunctionRegistry (NFA-03)
  cursor.hpp      Ergebnis-Cursor
  collection.hpp  Collection (CRUD, Queries, DSL-Überladungen)
  transaction.hpp Transaktion (FA-12)
  database.hpp    Database (verwaltet Collections)
  index.hpp / aggregate.hpp / change_event.hpp / schema.hpp
  references.hpp  FA-18 ReferenceIndex (Erweiterungsmodul)
  import*.hpp     FA-21 Import-Quellen + ImportRegistry/Importer (csv/json/snapshot/sqldump/bson, optional sqlite)
  db_graph.hpp    FA-22 DbGraph (JSON/Mermaid/Markdown)
  discovery.hpp   FA-27 Discovery (v0.29–v0.33: FK/FD/Korrelation/Near-Dupes, Orphans, Pfade, Profil, Diff, analyze+Report)
  metrics.hpp     v0.22 Metriken/Diagnose
  qt/             FA-23 Qt-Modell-Adapter (nur bei INMEMDB_WITH_QT, nur QtCore)
  exceptions.hpp  Fehlertypen
src/              Implementierung (nicht Teil der ABI)
  collection_state.hpp  gemeinsamer Lock-freier Kern (do*-Methoden) für Collection + Transaction
  json.* / crypto.* / regex.*  interne Parser/Krypto/Mini-RegEx (keine externen Deps)
tests/            Unit-Tests (assert-basiert, kein externes Framework) + CTest
tools/            CLI-Tools: inmemdb_import, inmemdb_graph, inmemdb_discover (inkl. --report v0.33)
examples/         Lauffähige Beispiele 01–15 (auch als CTest-Smoke-Tests)
bench/            Benchmark memdb_bench (nicht Teil der Tests)
.github/workflows/ci.yml   CI: Build+Test (ubuntu/windows/macos), sqlite-import, doxygen
CMakeLists.txt    Library-Target `memdb` (STATIC) + Tests via CTest + optionale Tools
```

### Pimpl – wo und warum nicht überall
- **Pimpl:** `Database`, `Collection`, `Document`, `Expression`, `Transaction`, `ReferenceIndex`,
  `ImportSource`-Ableitungen – verstecken interne Datenstrukturen (Maps, Locks, Baumknoten),
  ermöglichen ABI-stabile Erweiterungen.
- **Kein Pimpl (bewusste Ausnahme):** `Value`, `DocumentId`, `Cursor`, `Index`, die Exceptions,
  die Registry-/Options-Strukturen – das sind Value-Typen. Ein Pimpl pro `Value` würde bei jedem
  Feldzugriff eine Heap-Allokation kosten – in einem In-Memory-DB inakzeptabel. `Value` ist dafür
  ein kompakter `std::variant`-Wrapper, dessen Layout Teil der Header-API ist.
- **Handle-Semantik:** `Collection` hält `std::shared_ptr<Impl>` – Kopien sind Handles auf
  dieselbe zugrunde liegende Collection.

### Interner Kern: `detail::CollectionState` (seit v0.3)
Alle zustandsändernden Operationen liegen als lock-freie `do*`-Methoden in
`src/collection_state.hpp` (`detail::CollectionState`) und werden von **beiden** Hüllen
geteilt: `Collection` (public, `shared_mutex`: shared für Lesen / unique für Schreiben) und
`Transaction` (Exklusiv-Lock + CoW-Undo-Log für Rollback, FA-12; seit v0.19 O(1) statt O(n) pro Begin). Dadurch ist die
Transaktionslogik exakt dieselbe Code-Basis wie der Normalbetrieb – keine doppelte Logik.

### Erweiterungsmodule ohne Kernänderung (NFA-03, Open/Closed)
- **FA-18 Referenzen** (`references.hpp`): `ReferenceIndex` nutzt nur die öffentliche API;
  Referenzen liegen als Dokumente in einer reservierten Collection (`__references`).
- **FA-21 Import** (`import*.hpp`): abstrakte `ImportSource` + `ImportRegistry` (Singleton-Factory,
  Auto-Erkennung per Dateiendung) + `Importer`. Quellen ohne Abhängigkeiten: CSV, JSON/JSONL,
  Snapshot-Reimport, SQL-Dump, BSON; optional SQLite via `-DINMEMDB_WITH_SQLITE=ON`.
- **NFA-03 String-DSL** (`query_dsl.hpp`): `parseQuery` baut denselben `Expression`-Baum wie die
  C++-API; eigene Operatoren über `QueryFunctionRegistry` + `Expression::predicate`.
- **FA-22 DbGraph** (`db_graph.hpp`): liest nur öffentliche API / Snapshot-Dateien.

### Thread-Safety (FA-16)
`Database::Impl` und `Collection::Impl` besitzen je eine `std::shared_mutex`:
Lesezugriffe (`find`, `findById`, `count`, `size`) laufen mit `shared_lock`, Schreibzugriffe
(`insert`, `update`, `remove`, `replace`) mit `unique_lock`. `Cursor` ist eine Snapshot-Kopie
(konsistente Sicht zum Abfragezeitpunkt). Transaktionen halten während ihrer Laufzeit einen
Exklusiv-Lock (volle Isolation, read-your-own-writes). Die Registries (`ImportRegistry`,
`QueryFunctionRegistry`) sind per Mutex thread-sicher.

### Abhängigkeiten
Keine Pflichtabhängigkeiten; nur die C++17-Standardbibliothek. CMake ≥ 3.20.
Optional: `libsqlite3-dev` nur bei `-DINMEMDB_WITH_SQLITE=ON` (SQLite-Import); Doxygen nur bei
`-DINMEMDB_BUILD_DOCS=ON`. Der Default-Build bleibt abhängigkeitsfrei (NFA-06) und das
SQLite-Modul wird dann nicht einmal gelinkt.

---

## ADR-02: Eigene Krypto/RegEx/JSON – externe Pendants nur als Opt-in, nie als Pflicht (NFA-06)

| Feld | Wert |
|---|---|
| Status | beschlossen (v0.33), gültig |
| Bezug | NFA-06, FA-19 (Krypto), NFA-03/`matches` (RegEx), FA-14 (JSON); betrifft `src/crypto.*`, `src/regex.*`, `src/json.*` |

### Entscheidung
InMemDB bringt **eigene Implementierungen** mit und bleibt damit im Default-Build zu 100 %
abhängigkeitsfrei:

- **Krypto** (FA-19, `src/crypto.*`): PBKDF2-HMAC-SHA256 (Key Derivation), ChaCha20
  (Stream-Cipher), HMAC-SHA256 (encrypt-then-MAC) – selbst implementiert, auditierbar,
  keine Lizenzfrage, identisch auf allen Plattformen.
- **Mini-RegEx** (`src/regex.*`): Thompson-NFA (vollständiges Pattern-Match, kein Backtracking,
  kein Hang bei `(a*)*` durch Visited-Set); Syntax: `. * + ? {n,m} [...] \d\w\s (?:...)` lazy etc.
  (v0.20). **Bekannte Grenze:** keine Backreferences/Lookaround (prinziell unmöglich in einem
  reinen NFA ohne Backtracking).
- **JSON** (`src/json.*`): eigener Parser/Writer, locale-unabhängig (`std::to_chars`/`from_chars`
  seit v0.23.1), Fastpaths seit v0.19.

**Externe Pendants (z. B. OpenSSL/libcrypto, PCRE2, nlohmann/json) sind nur als Opt-in erlaubt:**
sie dürfen ausschließlich hinter einer eigenen CMake-Option (Default **OFF**) + `find_package`
+ Präprozessor-Macro eingebunden werden – exakt nach dem Vorbild des SQLite-Moduls
(`option(INMEMDB_WITH_SQLITE ... OFF)` → `find_package(SQLite3 REQUIRED)` → Code nur unter
`#ifdef INMEMDB_HAS_SQLITE`). Im Default-Build wird nichts davon gesucht, kompiliert oder gelinkt.
Daraus folgt: **niemals eine Pflichtabhängigkeit** (NFA-06); wer InMemDB baut, braucht nur einen
C++17-Compiler.

### Shared-Lib-Deployment ("Deploy-Ordner")
Das Mitliefern von Shared Libraries neben der Anwendung ist **nur im Opt-in-Fall** relevant und
liegt in der Hand des Anwenders, nicht der Library:

- **Windows:** `.dll` neben die `.exe` legen (oder in einen Ordner im `PATH`) – Loader findet sie automatisch.
- **Linux:** `.so` neben die Binärdatei + Link mit `-Wl,-rpath,'$ORIGIN'` (oder `LD_LIBRARY_PATH` zur Laufzeit).
- **macOS:** `.dylib` + `@loader_path` (install_name), ggf. `codesign`.
- **Bevorzugt:** statisch linken (`libcrypto.a`, `libpcre2-8.a`, `libjson.a`) – dann gibt es gar
  keine Laufzeit-Dependency, und die eigene Implementierung braucht ohnehin nichts davon.

Risiken des Shared-Lib-Deployments (Versionierung/ABI, Lizenz-Compliance – OpenSSL 3 = Apache-2.0,
PCRE2 = BSD-ähnlich mit PCRE-Ausnahme, zusätzliche Audit-Pflichten, größere Binaries): genau der
Grund, warum der Default die eigenen Implementierungen nutzt.

### Wann ein externes Backend trotzdem sinnvoll sein kann
- **OpenSSL/libcrypto:** gehärtete, FIPS-fähige Krypto, AES-NI-Hardwarebeschleunigung, breitere
  Algorithmus-Auswahl – gegen mehr Binary-Größe und Audit-Aufwand.
- **PCRE2:** volle Regex-Syntax inkl. Backreferences, Lookaround, Unicode-Properties – gegen
  deutlich größeren Footprint; die Thompson-NFA bleibt für `matches` im Default.
- **nlohmann/json:** komfortablere API, JSON Schema – gegen Compile-Zeit und Binary-Größe.

Umsetzung (falls gewünscht, Ausblick): Optionen `INMEMDB_WITH_OPENSSL` / `INMEMDB_WITH_PCRE2`
nach ADR-02-Muster; die öffentlichen APIs (`saveToFileEncrypted`, `Expression::matches`,
`toJson`/`fromJson`) bleiben identisch, nur das Backend wechselt per Macro.

# Architektur-Entscheidungen – v0.15

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

### Layout (v0.15)
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
  exceptions.hpp  Fehlertypen
src/              Implementierung (nicht Teil der ABI)
  collection_state.hpp  gemeinsamer Lock-freier Kern (do*-Methoden) für Collection + Transaction
  json.* / crypto.* / regex.*  interne Parser/Krypto/Mini-RegEx (keine externen Deps)
tests/            Unit-Tests (assert-basiert, kein externes Framework) + CTest
tools/            CLI-Tools: inmemdb_import, inmemdb_graph
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
`Transaction` (Exklusiv-Lock + Voll-Backup für Rollback, FA-12). Dadurch ist die
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

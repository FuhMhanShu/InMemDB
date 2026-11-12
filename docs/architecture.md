# Architektur-Entscheidungen

## ADR-01: Kompilierte statische Library mit Pimpl (statt header-only)

| Feld | Wert |
|---|---|
| Status | beschlossen |
| Datum | v0.1 |
| Bezug | Lastenheft Abschnitt 2, NFA-06 |

### Entscheidung
InMemDB wird als **statische C++-Library** ausgeliefert. Alle zustandsbehafteten Klassen
(`Database`, `Collection`, `Document`, `Expression`) verwenden das **Pimpl-Idiom**:
Der öffentliche Header enthält nur die Deklaration plus `std::unique_ptr<Impl>` /
`std::shared_ptr<Impl>`; die Implementierung liegt in `src/*.cpp`.

Header-only wurde bewusst verworfen (Compile-Zeiten der Konsumenten, fehlende
ABI-Stabilität bei Änderungen am Kern).

### Layout
```
include/memdb/    Öffentliche Header (API, ABI-stabil)
  memdb.hpp       Umbrella-Header
  value.hpp       Wert-Typ (variant-basiert)
  document_id.hpp Dokument-ID (Werttyp)
  document.hpp    Dokument mit dynamischen Feldern
  query.hpp       Expression/Ausdruckbaum + Operatoren
  cursor.hpp      Ergebnis-Cursor
  collection.hpp  Collection (CRUD, Queries)
  database.hpp    Database ( verwaltet Collections)
  exceptions.hpp  Fehlertypen
src/              Implementierung (nicht Teil der ABI)
tests/            Unit-Tests (assert-basiert, kein externes Framework)
examples/         Lauffähige Beispiele (auch als CTest-Smoke-Tests registriert)
CMakeLists.txt    Library-Target `memdb` (STATIC) + Tests via CTest
```

### Pimpl – wo und warum nicht überall
- **Pimpl:** `Database`, `Collection`, `Document`, `Expression` – verstecken interne
  Datenstrukturen (Maps, Locks, Ausdruckbaum-Knoten), ermöglichen ABI-stabile
  Erweiterungen.
- **Kein Pimpl (bewusste Ausnahme):** `Value`, `DocumentId`, `Cursor`, die Exceptions –
  das sind Value-Typen. Ein Pimpl pro `Value` würde bei jedem Feldzugriff eine
  Heap-Allokation kosten – in einem In-Memory-DB inakzeptabel. `Value` ist dafür ein
  kompakter `std::variant`-Wrapper, dessen Layout Teil der Header-API ist.
- **Handle-Semantik:** `Collection` hält `std::shared_ptr<Impl>` – Kopien der
  Collection sind Handles auf dieselbe zugrunde liegende Collection.

### Thread-Safety (FA-16, v1)
`Database::Impl` und `Collection::Impl` besitzen je eine `std::shared_mutex`:
Lesezugriffe (`find`, `findById`, `count`, `size`) laufen mit `shared_lock`,
Schreibzugriffe (`insert`, `update`, `remove`, `replace`) mit `unique_lock`.
`Cursor` ist eine Snapshot-Kopie (konsistente Sicht zum Abfragezeitpunkt).

### Abhängigkeiten
Keine Pflichtabhängigkeiten; nur die C++17-Standardbibliothek. CMake ≥ 3.20.

# Referenzen zwischen Collections/Dokumenten (FA-18) – Umsetzung v0.8

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.8) |
| Bezug | Lastenheft FA-18 (Kann), Ausblick Abschnitt 10 |

## Idee

`ReferenceIndex` ist ein **Erweiterungsmodul ohne Kernänderung** (NFA-03, Open/Closed): Es nutzt
ausschließlich die öffentliche API (`Database`, `Collection`, `Expression`, Indizes). Referenzen
werden als Dokumente in einer reservierten Collection (Default `__references`) gespeichert und sind
damit automatisch transaktions-, index- und snapshot-fähig.

Referenzen sind **name-basiert („relativ“)**: Sie speichern Collection-Namen + `DocumentId` als Daten,
nicht als Handle. Dadurch lösen sie sich gegen die jeweilige `Database` zur Laufzeit auf und überleben
`toJson`/`fromJson`/`saveToFile`/`loadFromFile`.

## API

```cpp
ReferenceIndex refs(db);                       // legt "__references" inkl. Indizes an

Reference r = refs.link("users", userId, "orders", std::nullopt, "hat-bestellungen");
// fromId/toId = nullopt  ->  Verweis auf die Collection als Ganzes
// toCollection darf ein relativer Pfad sein: "./users", "../orders", "/abs" (siehe resolve)

refs.unlink("users", userId, "orders", std::nullopt, "hat-bestellungen");   // true bei Erfolg

auto out = refs.references("users", RefDirection::Outgoing, userId);        // von diesem Datensatz
auto in  = refs.references("orders", RefDirection::Incoming);               // auf diese Collection
auto both = refs.references("users", RefDirection::Both, userId);

auto bad = refs.dangling();        // Referenzen, deren Quelle/Ziel nicht mehr existiert
std::size_t n = refs.pruneDangling();  // entfernt genau diese

std::string t = refs.resolve("a/b", "../c");   // -> "c"
```

`struct Reference { std::string fromCollection; std::optional<DocumentId> fromId;
std::string toCollection; std::optional<DocumentId> toId; std::string relation; }`

## Speicherformat

Jede Referenz ist ein Dokument in `__references`:

```json
{ "from_key": "users#5", "from_coll": "users", "from_id": 5,
  "to_key": "orders#",  "to_coll": "orders", "to_id": null,
  "relation": "hat-bestellungen" }
```

- `from_key`/`to_key` = `"<collection>#<id>"` (leer ohne ID) als kombinierte Index-Felder.
- Indizes auf `from_key` und `to_key` (nicht-unique) beschleunigen beide Richtungen:
  outgoing = `Field("from_key") == key`, incoming = `Field("to_key") == key`.

## Semantik

- **Beide Richtungen verfolgbar:** outgoing über `from_key`, incoming über `to_key` — je mit Index.
- **Relative Pfade (`resolve`)**: `"./x"`, `"../x"`, `"a/b"` werden gegen das Verzeichnis der
  Quell-Collection aufgelöst; `"/x"` ist absolut; ein nackter Name ohne `/` bleibt unverändert
  (abwärtskompatibel zum flachen Collection-Namespace). `..` an der Wurzel wird abgeschnitten;
  ein leeres Ergebnis wirft `ReferenceError`.
- **Validierung bei `link`:** Quell-/Ziel-Collection müssen existieren (`CollectionNotFoundError`),
  angegebene Dokument-IDs müssen existieren (`DocumentNotFoundError`). Duplikat
  (gleiches from/to/relation) wirft `ReferenceExistsError`.
- **Dangling References:** Werden Quell-/Ziel-Dokumente gelöscht oder Collections gedroppt, werden
  Referenzen nicht automatisch entfernt, sondern bei `dangling()` gemeldet und per `pruneDangling()`
  entfernt (lazy statt eager — so gibt es keine Lücke bei neu angelegten Collections).
- **Snapshot:** `__references` ist eine normale Collection und wird von `Database::toJson` mit
  serialisiert (inkl. Indizes) — Referenzen überleben Save/Load.
- **Transaktionen:** Änderungen an Referenzen sind über `__references.beginTransaction()` atomar.
  Einschränkung: FA-12-Transaktionen sind pro Collection; eine Referenz-Änderung zusammen mit einer
  Dokument-Änderung in einer *anderen* Collection ist nicht cross-collection-atomar.
- **Thread-Safety:** wie der Kern — alle Zugriffe laufen über die RW-Locks der beteiligten Collections.

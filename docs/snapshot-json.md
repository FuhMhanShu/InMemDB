# Snapshot / JSON-Export & -Import (FA-14) – Umsetzung v0.4

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.4) |
| Bezug | Anforderungen FA-14 (Kann), NFA-06 (keine externen Pflichtabhängigkeiten) |

## API

```cpp
// gesamte Database
std::string json = db.toJson();
db.fromJson(json);                                  // laedt in bestehende DB (Collections anlegen/ersetzen)
Database db2 = Database::fromJson(json, "kopie");   // neue DB aus JSON-String
db.saveToFile("/tmp/snap.json");
Database db3 = Database::loadFromFile("/tmp/snap.json");

// einzelne Collection
std::string arr = coll.toJson();        // JSON-Array der Dokumente
coll.loadJson(arr);                     // ersetzt kompletten Inhalt der Collection
coll.clear();                           // Inhalt leeren, Index-Definitionen bleiben
```

## JSON-Format
- Database: `{"name": "...", "collections": {"users": {"indexes": [{"field": "name", "unique": true}], "documents": [ {...}, ... ]}, ...}}`
- Collection: `Collection::toJson()` liefert nur das Dokument-Array `[{...}, ...]`
  (zum Laden via `Collection::loadJson`); das Database-Format enthält zusätzlich
  die Index-Definitionen pro Collection
- Beim Import in eine **neue** Database werden die Indizes aus dem JSON neu angelegt;
  bei einer **bestehenden** Collection bleiben deren Index-Definitionen erhalten
  (Dokumente werden ersetzt, Indizes neu befüllt inkl. Unique-Prüfung)
- Wert-Mapping: `null` → `null`, Bool → `true/false`, Int → Zahl, Double → Zahl
  (immer mit Dezimalpunkt/Exponent, damit der Typ erhalten bleibt; NaN/Inf → `null`),
  String → JSON-String (UTF-8, Escapes inkl. `\uXXXX` + Surrogat-Paare),
  Array → `[...]`, Document → `{...}`, Binary → `{"$binary": "<hex>"}`
- **Dokument-IDs werden nicht serialisiert** – beim Import werden neue IDs vergeben.

## Verhalten
- `Database::fromJson` legt fehlende Collections an und ersetzt den Inhalt
  vorhandener; Index-Definitionen bleiben erhalten und werden beim Import neu befüllt
  (inkl. Unique-Prüfung).
- `Collection::loadJson` parst zuerst vollständig (Parsefehler werfen `JsonError`,
  ohne den Zustand zu ändern), dann `clear()` + Einfügen aller Dokumente.
- Datei-IO über `saveToFile`/`loadFromFile` (wirft `Error` bei IO-Fehlern).
- Serialisierung/Parser sind **selbst implementiert** (`src/json.cpp`), keine
  externe JSON-Bibliothek nötig.
- **Locale-unabhängig (v0.23.1):** Zahlen werden immer mit Dezimal**punkt**
  geschrieben und geparst (`std::to_chars`/`std::from_chars`, nie
  `snprintf`/`strtod`/Stream-Operatoren) – auch bei aktiver `de_DE`-Locale
  bleibt `1.5` `1.5` und wird nicht zu `1,5`. Gleiches gilt für
  `toString(Value)` und DSL-Zahlliterale. Regressionstest:
  `tests/test_locale.cpp` (setzt, falls vorhanden, die echte C-Locale
  `de_DE.UTF-8` und zusätzlich ein Komma-`numpunct`-Facet).

## Tests
Round-Trip aller Werttypen (inkl. Unicode, Escapes, Binary, Double-Typ-Erhalt),
Database- und Collection-Ebene, Datei-Save/Load, Fehlerfälle (ungültiges JSON,
falsche Struktur), `clear()` mit erhaltenen Indizes.

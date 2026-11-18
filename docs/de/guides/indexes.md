# Indizes (FA-10) – Umsetzung v0.2

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.2) |
| Bezug | Anforderungen FA-10, NFA-01 |

## API

```cpp
Index idx = coll.createIndex("age");          // nicht-unique
Index uidx = coll.createIndex("mail", true);  // unique
bool ok  = coll.hasIndex("age");
std::vector<Index> all = coll.indexes();
bool dropped = coll.dropIndex("age");
```

Fehler: `IndexExistsError` (Doppel-Anlage), `UniqueConstraintError` (Verletzung
eines Unique-Index beim insert/update oder beim nachträglichen Anlegen auf
Duplikaten).

## interne Struktur
Pro indexiertem Feldpfad (Punktnotation möglich, z. B. `adresse.plz`) eine
`std::map<Value, std::set<DocumentId>, ValueLess>`:

- `ValueLess` ist eine strikte schwache Ordnung: Typ-Rang (null < bool < Zahl <
  string < binary < document < array), innerhalb von Zahl/String über `compare()`.
  **Int und Double liegen im selben Rang** – `age == 30.0` matcht also `age = 30`.
- **NaN (v0.43, Befund M1):** NaN ist größer als jede Zahl (auch +Inf), NaN == NaN – die Ordnung
  bleibt transitiv, Indizes und Unique-Prüfung sind damit NaN-sicher (zwei NaN-Werte verletzen
  einen Unique-Index, Postgres-Stil).
- Unique-Index: Schlüsselwert darf nur einem Dokument gehören.

## Nutzung durch die Query-Engine
`Expression::indexHint()` liefert den ersten verwendbaren Vergleich im
Top-Level-`&&`-Baum (Vergleichsoperator auf einem Feld; `!=` wird nicht
beschleunigt). `Collection::find/findOne/count` holen damit Kandidaten-IDs aus
dem Index, falls vorhanden:

- `==` → exakter Lookup (O(log n))
- `<`, `<=`, `>`, `>=` → Bereich über `lower_bound`/`upper_bound` (nur für
  Zahlen und Strings; sonst Vollscan)
- kein Index / `!=` / OR-Baum → Vollscan über alle IDs (O(n))

Danach wird die **komplette** Expression auf jedem Kandidaten ausgewertet –
Index-Ergebnisse bleiben also semantisch korrekt.

Diagnose: `Collection::lastQueryCandidateCount()` liefert die Kandidatenzahl
der letzten `find`/`findOne`/`count`-Abfrage (Test/Performance-Nachweis).

## Wartung & Garantien
- Index-Einträge werden bei `insert`, `insertBulk`, `update`, `updateById`,
  `replace`, `remove`, `removeById` automatisch gepflegt.
- `createIndex` auf bestehenden Daten baut den Index nach und prüft bei
  `unique` auf vorhandene Duplikate.
- Unique-Validierung läuft **vor** der Mutation; `update(q, ...)` wendet
  Änderungen zweiphasig an (erst alle prüfen, dann anwenden) → starke
  Exception-Garantie (NFA-05).
- Alle Index-Operationen laufen unter dem Collection-Lock (`shared_mutex`).

## Ausblick (Kann)
- Zusammengesetzte Indizes (mehrere Felder), Text-/Volltextindex.

- **v0.27:** `Collection::distinctValues(feld, q)` nutzt bei leerer Query einen vorhandenen Index auf dem Feld direkt (**O(#distinct)** statt Vollscan; sichtbar in `metrics().indexQueries`).

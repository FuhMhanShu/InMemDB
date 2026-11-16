# Datencharakter-Kategorisierung (FA-29) – API-Referenz

| Feld | Wert |
|---|---|
| Status | umgesetzt – **ohne externe Abhängigkeiten** (nur InMemDB + C++17) |
| Bezug | Anforderungen FA-29 (Kann); baut auf Discovery v0.27+ (`DiscoveryOptions`, Sampling) auf |
| Modul | `include/memdb/character.hpp` + `src/character.cpp` |

Das Modul beantwortet die Vorfrage jeder tieferen Datenanalyse: **Um welche Art von
Daten handelt es sich grob?** Jede Collection wird anhand ihrer Felder in eine von
drei Kategorien eingestuft:

| Kategorie | Bedeutung | Typische Analyse-Pipeline |
|---|---|---|
| `DataCategory::Numeric` | Kategorie 1: fast ausschließlich numerische Felder (>= 80 %) | numerische Metadaten-Analyse (`numeric_analysis.hpp`, FA-30) |
| `DataCategory::Mixed` | Kategorie 2: Mischung aus Zahlen, Strings, Booleans | Regel-/Assoziations-Mining, semantische Typen |
| `DataCategory::Complex` | Kategorie 3: komplexe Mischformen (Arrays, Dokumente, Binär, Typ-Drift >= 40 %) | rekursive Profilierung, Schema-Varianten, Entity Resolution |

## Funktionen

```cpp
#include <memdb/memdb.hpp>
#include <memdb/character.hpp>

memdb::DiscoveryOptions opts; // Sampling wie im Discovery-Modul (sampleSize, seed)

// Charakteristik einer Collection: Kategorie + Statistik + Begründung
memdb::CollectionCharacter cc = memdb::assessCharacter(coll, opts);
//   cc.category        -> DataCategory::Numeric / Mixed / Complex
//   cc.confidence      -> 0..1 (Anteil der Felder, die das Urteil tragen)
//   cc.rationale       -> Klartext-Begründung (z. B. "3/3 Felder numerisch")
//   cc.numericFields, stringFields, boolFields, complexFields, mixedFields
//   cc.numericFieldRatio, complexFieldRatio, driftFieldRatio
//   cc.fields[i]      -> FieldCharacter je Feld (dominanter Typ, Reinheit, Drift)

// Mehrere Collections (auch database-übergreifend; __references wird ignoriert)
auto ccs = memdb::assessCharacter({db1.collection("sensors"), db2.collection("orders")}, opts);
// Ganze Database
auto all = memdb::assessCharacter(db, opts);

// Kategorien-Namen ("numeric"/"mixed"/"complex")
std::string name = memdb::toString(cc.category);

// Reports
std::string j  = memdb::toJson(cc);    // eine Collection (JSON-Objekt)
std::string j2 = memdb::toJson(ccs);   // mehrere (JSON-Array)
std::string md = memdb::toMarkdown(ccs); // Übersichtstabelle
```

## Entscheidungsregeln

1. Pro Feld wird über die Nicht-Null-Werte der **dominante Typ** bestimmt
   (`FieldCharacter::dominantType`, `typePurity`). Gilt:
   `numeric` (Int/Double), `string`, `boolean`, `complex` (Document/Array/Binary),
   `mixed` (mehrere Typen, Reinheit < 0.6), `empty` (nur Null-Werte).
2. Die Collection-Kategorie folgt aus den Anteilen **effektiver** Felder
   (mind. ein Nicht-Null-Wert):
   - `Complex`, wenn (complex + mixed) >= 25 % oder Felder mit Typ-Drift >= 40 %
   - `Numeric`, wenn numeric >= 80 %
   - sonst `Mixed`
3. `confidence` misst die Eindeutigkeit des Urteils (bei `Numeric`: numerischer
   Anteil, bei `Complex`: Maximum der Auslöser-Anteile, bei `Mixed`: Nähe zur
   50/50-Verteilung, begrenzt durch die kleinere Seite).

## Details

- Read-only; verändert keine Daten. Ein Scan pro Collection (Sampling wie Discovery).
- `maxFieldsPerCollection` (Default 64) schützt vor sehr breiten Collections; die
  Kappung erfolgt alphabetisch wie im Discovery-Modul.
- Leere Collections (keine Werte-Felder) werden als `Mixed` mit `confidence == 0`
  eingestuft.
- Tests: `tests/test_character.cpp` (55 Checks).

Siehe auch: [numeric-analysis.md](numeric-analysis.md) für die Kategorie-1-Pipeline
und [discovery.md](discovery.md) für die zugrunde liegenden Datenprofile.

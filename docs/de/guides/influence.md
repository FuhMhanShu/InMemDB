# Einfluss-Analyse (FA-31) – API-Referenz

| Feld | Wert |
|---|---|
| Status | umgesetzt – **ohne externe Abhängigkeiten** (nur InMemDB + C++17) |
| Bezug | Anforderungen FA-31 (Kann); Vertiefung der numerischen Metadaten-Analyse (FA-30) |
| Modul | `include/memdb/influence.hpp` + `src/influence.cpp` |

Das Modul deutet die statistischen Werte der numerischen Analyse (FA-30) weiter
und beantwortet die Frage: **Welches Feld hat dominanten Einfluss auf andere
Felder?** Es kombiniert drei Evidenzarten zu einer Rollenzuordnung und einem
Einfluss-Score pro Feld. Read-only; Sampling und Feld-Limit laufen wie bei FA-30.

## Evidenzarten

1. **Hub über Pearson-Korrelationen**: Summe der |r| gegen alle Partnerfelder
   (`hubScore`, `hubRatio`) plus Anzahl starker Zusammenhänge
   (|r| >= `strongCorrelation`, Standard 0.8). Felder mit vielen starken
   Korrelationen sind dominante Treiber-Kandidaten.
2. **Multiples R² (Erklärtheit)**: Lineare Regression jedes Feldes gegen die
   übrigen (Complete Cases, Gauß-Elimination mit Teilpivotisierung und minimaler
   Ridge-Term gegen exakte Kollinearität). Hoher R² (>= `rSquaredHigh`, Standard
   0.9): das Feld wird von den übrigen erklärt. Niedriger R² bei hohem Hub:
   Treiber.
3. **Funktionale Abhängigkeit**: Gleiche Werte in Feld A führen stets zum selben
   Wert in Feld B (B = f(A), z. B. `preis = f(menge)`). Evidenz sind nur Gruppen
   mit wiederholtem Quell-Wert; `coverage` ist der Anteil konsistenter Gruppen
   (>= `dependencyRatio`, Standard 0.99). Der stärkste strukturelle Hinweis auf
   Ableitung. Bei beidseitiger Funktionalität gewinnt die Richtung mit mehr
   distinct Quell-Werten.

## Rollen (FieldRole)

| Rolle | Bedeutung | Kriterium |
|---|---|---|
| `driver` | bestimmt andere Felder, ohne selbst erklärt zu sein | `determines > 0` oder >= 2 starke Korrelationen bei R² < rSquaredHigh |
| `dependent` | wird von anderen Feldern erklärt | `determinedBy > 0` oder R² >= rSquaredHigh |
| `mediator` | beides: abhängig und treibt weitere Felder | Treiber- und Abhängigkeitskriterium gleichzeitig |
| `independent` | keine starken Zusammenhänge | sonst |

Einfluss-Score (0..1): `0.5 * hubRatio + 0.3 * (determines / Partner) + 0.2 * (1 - R²)`.
`topDriver` ist das Feld mit dem höchsten Score, sofern ein Dominanz-Signal
vorliegt (funktionale Bestimmung, starke Korrelation oder hubRatio >= 0.8).

## Funktionen

```cpp
#include <memdb/memdb.hpp>
#include <memdb/influence.hpp>

memdb::InfluenceOptions opts;
//   opts.numeric               – NumericAnalysisOptions des Basis-Scans (FA-30)
//   strongCorrelation (0.8)    – |r| >= X gilt als starker Zusammenhang
//   dependencyRatio (0.99)     – Anteil konsistenter Gruppen je Abhängigkeit
//   minDependencyPairs (5)     – Mindest-Dokumente mit beiden Feldern
//   rSquaredHigh (0.9)        – R^2 >= X => Feld gilt als erklärt
//   maxPredictors (16)        – Obergrenze der Prädiktoren je Regression
//   minRegressionRows (8)      – Mindest-Zeilen (Complete Cases) für R^2

memdb::InfluenceAnalysis res = memdb::analyzeInfluence(coll, opts);
//   res.topDriver             – Feld mit dem höchsten Einfluss-Score
//   res.dependencies[i]       – {source, target, coverage, groups, pairs}
//   res.fields[i]             – nach influenceScore absteigend:
//     .role                   – driver / dependent / mediator / independent
//     .hubScore/.hubRatio     – Summe bzw. Mittel der |r| über alle Partner
//     .strongCorrelations     – Anzahl |r| >= strongCorrelation
//     .rSquared               – Erklärtheit durch die übrigen Felder
//     .determines/.determinedBy – Anzahl funktionaler Bestimmungen je Richtung
//     .influenceScore         – kombinierter Score (0..1)
//     .rationale              – Begründung in Klartext (Deutsch)

// Mehrere Collections (auch database-übergreifend; __references wird ignoriert)
auto all = memdb::analyzeInfluence(db, opts);

// Reports
std::string json = memdb::toJson(res);      // maschinenlesbar / LLM-freundlich
std::string md   = memdb::toMarkdown(res);  // Tabellen je Collection
std::string mdAll = memdb::toMarkdown(all); // Übersicht aller Collections
```

## Grenzen

Korrelation belegt keine Kausalität: Das Ergebnis ist eine begründete
Rangfolge aus statistischer Dominanz und struktureller Ableitung, keine
Ursachen-Wahrheit. Bei Sampling (opts.numeric.sampleSize) schrumpft die
Evidenzbasis entsprechend; R² wird erst ab `minRegressionRows` Complete Cases
berechnet.

## Tests

`tests/test_influence.cpp` (103 Checks): Treiber/abhängig/unabhängig-Trennung,
funktional nicht invertierbare Abhängigkeit, exakte Linearität (R² = 1),
Mediatoren in Ableitungsketten, Einzelfeld ohne Zusammenhänge, Sampling,
Database-/Vector-Überladungen, JSON-/Markdown-Reports, Rollennamen.

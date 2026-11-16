# Relationship-Discovery (FA-27, v0.29) – API-Referenz

> **Ausführlicher, erklärender Guide mit Beispielen und Bildern:**
> [`docs/guides/discovery-guide.md`](discovery-guide.md)

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.29; Erweiterungen v0.30/v0.31/v0.32/v0.33) – **ohne externe Abhängigkeiten** (nur InMemDB + C++17) |
| Bezug | Anforderungen FA-27 (Kann); baut auf Foundations v0.27 (`ValueLess`, `distinctValues`, `sample`, `AggOp::Distinct`) und FA-18/FA-26 auf |

Modul `include/memdb/discovery.hpp` + `src/discovery.cpp`: untersucht Daten auf
**Beziehungen und Abhängigkeiten** – collection- **und database-übergreifend** (einfach
`std::vector<Collection>` aus verschiedenen `Database`-Objekten übergeben).

## Funktionen

```cpp
memdb::DiscoveryOptions opts; // minSupport, minConfidence, minCorrelation, minSimilarity, sampleSize, seed, ...

// 1) Fremdschlüssel-Kandidaten per Werte-Überlappung (cross-collection/cross-database)
auto fk  = memdb::discoverForeignKeys({db1.collection("orders"), db2.collection("users")}, opts);
// 2) Funktionale Abhängigkeiten innerhalb einer Collection (X -> Y)
auto fd  = memdb::discoverFunctionalDependencies(coll, opts);        // z. B. plz -> stadt
// 3) Korrelationen: numerisch (Pearson |r|) + kategorial (Assoziation U)
auto cor = memdb::discoverCorrelations(coll, opts);                   // z. B. x ~ y, farbe -> kategorie
// 4) Near-Duplicates (normalisierte Edit-Distanz, auch über Collections hinweg)
auto nd  = memdb::discoverNearDuplicates({collA, collB}, opts);       // z. B. "Schmidt" ~ "Schmidtt"
// 5) alles kombiniert
auto all = memdb::discoverRelations(db, opts);                        // eine Database
auto allX = memdb::discoverRelations({c1, c2, c3}, opts);             // beliebig viele Collections/DBs

// 6) Befunde als FA-18-Referenzen anwenden (nur innerhalb einer Database)
memdb::ReferenceIndex refs(db);
std::size_t n = memdb::applyDiscovered(db, fk, refs, /*minConfidence=*/0.9);

// 7) Reports
std::string j = memdb::toJson(all);
std::string m = memdb::toMermaid(all);   // rendert als Diagramm

// 8) v0.30: Kardinalität steckt in DiscoveredRelation::cardinality (1:1/n:1/1:n/n:m)
auto orph = memdb::findOrphansForRelations(colls, all, opts);      // Verweise ins Leere
auto paths = memdb::findPaths(all, "order_items", "users", 4);     // Multi-Hop-FK-Pfade
// 9) v0.31: Datenprofil + Candidate Keys
auto prof = memdb::profileCollection(coll, opts);  std::string pj = memdb::toJson(prof);
auto keys = memdb::discoverCandidateKeys(coll, opts);
// 10) v0.32: Diff + Merge-Vorschläge (auch cross-database)
auto d = memdb::diffDatabases(db1, db2, opts);     std::string dj = memdb::toJson(d);
auto sug = memdb::suggestMerges(colls, opts);      // Kandidaten für FA-24 mergeCollections
// 11) v0.33: automatische Gesamtanalyse + Health-Score + Report-Präsentation
memdb::AnalysisReport rep = memdb::analyze(db, opts);            // oder analyze({c1, c2}, opts) cross-database
memdb::printSummary(rep, std::cout);
std::ofstream("analyse.md")   << memdb::toMarkdown(rep);
std::ofstream("analyse.json") << memdb::toJson(rep);
std::ofstream("graph.mmd")    << memdb::toMermaid(rep);
// 12) v0.40: Ausreißer-Erkennung (z-Score + IQR) pro numerischem Feld, auch cross-database
auto outs = memdb::discoverOutliers(db, opts);   // oder discoverOutliers(coll, opts) / ({c1, c2}, opts)
// outs fließen in analyze() ein: AnalysisReport::outliers + Health-Score-Faktor (Gewichte 0.30/0.15/0.20/0.20/0.15)
```

Details, Semantik und Beispiele zu jedem Schritt: [discovery-guide.md](discovery-guide.md).

## Semantik

- **FK-Discovery**: Werte von `A.f` werden gegen Werte von `B.g` geschnitten (über
  `std::set<Value, ValueLess>`); `confidence` = Overlap-Ratio, `support` = Anteil der
  Quell-Dokumente mit Feld. Namens-Heuristik: `*_id`/`*_ref` (Quellseite) bzw. `id`/`pk`
  (Zielseite) → Vermerk `name-heuristik`; bei gegenläufigen Befunden gleicher Konfidenz
  gewinnt die `*_id`-Seite. Mindestens 2 gemeinsame Werte nötig.
- **FD**: `X → Y`, wenn jede X-Gruppe genau einen Y-Wert hat (einzige X-Werte als
  „trivial" vermerkt). Schwelle: Konfidenz ≥ max(minConfidence, 0.999).
- **Korrelation**: Pearson für numerische Paare (|r| ≥ minCorrelation, n ≥ 10);
  kategorial für String-X + skalares Y (U = 1 − H(Y|X)/H(Y) ≥ minCorrelation).
- **Near-Duplicates**: normalisierte Levenshtein-Ähnlichkeit ≥ minSimilarity auf
  gesampleten String-Werten (max. `maxValuesPerField` pro Feld).
- **Sampling**: `sampleSize > 0` → reproduktives `Collection::sample(sampleSize, seed)`
  statt Vollscan (gleicher Seed → gleiches Ergebnis).
- **Read-only**: Discovery liest nur (`forEach`/`findById`/`distinctValues`/`sample`),
  verändert nichts. `applyDiscovered` legt nur FK-Befunde mit `confidence ≥ minConfidence`
  als `ReferenceIndex::link(..., "discovered_<feld>")` an – nur wenn beide Collections in
  derselben Database liegen (cross-database-Befunde werden dort übersprungen).

## CLI

```sh
inmemdb_discover shop1.inmemdb.json shop2.inmemdb.json          # cross-database Report (JSON)
inmemdb_discover shop.enc --password geheim --mermaid           # verschlüsselt, Mermaid-Ausgabe
inmemdb_discover a.json b.json --min-support 0.9 --sample 10000 --seed 7
inmemdb_discover a.json --orphans                               # Orphan-Report (v0.30)
inmemdb_discover a.json --outliers                              # Ausreißer-Report (v0.40, z-Score + IQR)
inmemdb_discover a.json --path order_items users                # FK-Pfade A -> B (v0.30)
inmemdb_discover a.json b.json --diff                           # diffDatabases (v0.32)
inmemdb_discover a.json --suggest-merges                        # Merge-Vorschläge (v0.32)
inmemdb_discover a.json --report analyse                         # Gesamtanalyse (v0.33): analyse.report.md/.relations.json/.graph.mmd + Summary
inmemdb_discover a.json --apply                                 # FK-Refs anlegen + Datei zurückschreiben
inmemdb_discover a.json --apply --out report.json
```

Alle Optionen: `--password`, `--min-support`, `--min-confidence`, `--min-correlation`,
`--min-similarity`, `--sample`, `--seed`, `--mermaid`, `--orphans`, `--outliers`,
`--path A B`, `--diff`, `--suggest-merges`, `--report <prefix>`, `--apply`, `--out <datei>`.
Details und Beispiele: [discovery-guide.md](discovery-guide.md) §6.

## Tests

- `tests/test_discovery.cpp` (21 Checks): FK in einer DB + **zwei Databases**, FD (inkl.
  trivial-Vermerk), Pearson + kategorial, Near-Duplicates cross-collection, applyDiscovered
  + `joinIds`, toJson/toMermaid, Sampling-Reproduzierbarkeit
- `tests/test_discovery2.cpp` (17 Checks, v0.30): Kardinalität, `findOrphans`/`findOrphansForRelations`, `findPaths`
- `tests/test_discovery3.cpp` (18 Checks, v0.31): `profileCollection`/`toJson`, `discoverCandidateKeys`
- `tests/test_discovery4.cpp` (19 Checks, v0.32): `diffCollections`/`diffDatabases`, `suggestMerges`
- `tests/test_discovery5.cpp` (23 Checks, v0.33): `analyze` + `AnalysisReport`, alle vier
  Präsentationen (JSON/Markdown/Mermaid/Konsole), Health-Score (saubere DB > DB mit Orphan)
- `tests/test_discovery6.cpp` (26 Checks, v0.40): `discoverOutliers` (z-Score + IQR, Schwellen,
  minOutlierSamples, cross-database), Integration in `analyze()` (Report-Feld `outliers`,
  Health-Score sinkt mit Ausreißer), alle vier Präsentationen

Insgesamt 124 Discovery-Checks.

## Erweiterungen v0.30

- **Kardinalität**: `DiscoveredRelation::cardinality` („1:1“ / „n:1“ / „1:n“ / „n:m“, aus
  Sicht der Quell-Collection, über distinct-vs.-docCount auf beiden Seiten) – auch im JSON-Report.
- **Orphan-Report** (Integritätsprüfung): `findOrphans(source, field, target, targetField)`
  liefert `OrphanReport{scanned, orphans, orphanValues}` – Quell-Werte ohne Gegenstück;
  `findOrphansForRelations(colls, rels)` wendet das auf alle FK-Befunde an.
- **Pfad-Suche**: `findPaths(rels, from, to, maxDepth)` – alle FK-Pfade zwischen zwei
  Collections (DFS, Zyklus-sicher) als Multi-Hop-Join-Vorschläge.
- CLI: `inmemdb_discover … --orphans` und `--path <from> <to>`.

## Erweiterungen v0.31: Datenprofil & Candidate Keys

- `profileCollection(coll, opts)` → `CollectionProfile`: pro Feld Typ-Verteilung, present/nulls,
  distinct, Min/Max (Zahlen/Strings), bis zu 3 Beispiele; `toJson(profile)` als Report.
- `discoverCandidateKeys(coll, opts)`: minimale Feldmengen, die Dokumente eindeutig
  identifizieren (Einzelfelder zuerst; Paare nur, wenn keiner der beiden allein Key ist).

## Erweiterungen v0.32: Cross-DB-Diff & Merge-Vorschläge

- `diffCollections(a, b, opts)` → `CollectionDiff`: Felder nur in A/B, gemeinsame Felder,
  Jaccard-Feldähnlichkeit, Row-Counts, Werte-Überlappung pro gemeinsamem Feld
- `diffDatabases(db1, db2, opts)` → `DatabaseDiff`: gemeinsame Collections paarweise gedifft,
  Namen nur in A/B separat; `toJson(diff)` als Report; `__references` wird ignoriert
- `suggestMerges(colls, opts)` → `MergeSuggestion` pro Paar mit Schema-Jaccard ≥
  `minSchemaSimilarity` (id-artige Felder boosten die Werte-Überlappung) – direkter
  Kandidat für FA-24 `Database::mergeCollections`; auch cross-database
- CLI: `inmemdb_discover a.json b.json --diff` und `--suggest-merges`

## Erweiterung v0.33: automatische Gesamtanalyse + Report (analyze)

`analyze(...)` fasst alle Discovery-Bausteine zu einem `AnalysisReport` zusammen –
auch database-übergreifend (`analyze(std::vector<Collection>, opts)` oder `analyze(Database&, opts)`):

- `profiles` (pro Collection, v0.31), `relations` (v0.29/30), `orphans` (v0.30),
  `mergeSuggestions` (v0.32), wichtigste Multi-Hop-`paths` (v0.30, max. 20)
- **Health-Score 0–100** mit Ampel 🟢(≥80)/🟡(≥50)/🔴(<50): gewichtete Mischung aus
  Integrität (1 − Orphan-Rate), Redundanz (1 − Near-Duplicate-Rate), Struktur
  (Anteil der Collections in mind. einer FK-Beziehung; ohne FKs neutral 1.0 –
  fehlende Beziehungen sind kein Mangel), Typ-Konsistenz (mittlerer Anteil des
  dominanten Typs pro Feld) und ab **v0.40** Ausreißer (1 − Outlier-Rate über alle
  numerischen Werte; ohne numerische Werte neutral 1.0).
  Gewichte ab v0.40: 0.30 / 0.15 / 0.20 / 0.20 / 0.15 (v0.33–v0.39: 0.35 / 0.20 / 0.25 / 0.20).

Präsentation des Reports in vier Formen:

| Funktion | Ausgabe |
|---|---|
| `toMarkdown(report)` | Markdown-Report: Übersicht (Profile), entdeckte Beziehungen, Integrität/Orphans, Merge-Vorschläge, Empfehlungen |
| `toJson(report)` | komplettes JSON (healthScore, totals, profiles, relations, orphans, mergeSuggestions, paths) |
| `toMermaid(report)` | Mermaid-Graph: FK-Beziehungen als solide Kanten (mit Konfidenz), Korrelationen als gestrichelte Self-Loops |
| `printSummary(report, ostream)` | kompakte Konsolen-Ausgabe mit Health-Score + Ampel |

```cpp
memdb::AnalysisReport rep = memdb::analyze(db, opts);          // oder analyze({collA, collB}, opts)
memdb::printSummary(rep, std::cout);
std::ofstream("analyse.md")   << memdb::toMarkdown(rep);
std::ofstream("analyse.json") << memdb::toJson(rep);
std::ofstream("graph.mmd")    << memdb::toMermaid(rep);
```

CLI: `inmemdb_discover a.json b.json --report analyse` schreibt `analyse.report.md`,
`analyse.relations.json` und `analyse.graph.mmd` und druckt die Summary auf die Konsole.

Tests: `tests/test_discovery5.cpp` (23 Checks): Report-Struktur, alle vier Präsentationen,
cross-database-`analyze`, Health-Score (saubere DB ohne Orphans scored höher als DB mit Orphan).

## Erweiterung v0.40: Ausreißer-Erkennung (Outliers)

`discoverOutliers(const Collection&, opts)` bzw. `discoverOutliers(const std::vector<Collection>&, opts)`
(und damit auch `analyze(Database&, opts)`) erkennen statistische Ausreißer pro **numerischem Feld**:

- Ein Durchgang pro Collection sammelt die numerischen Werte jedes Feldes (Sampling via
  `sampleSize`/`seed` wird respektiert, Feld-Cap `maxFieldsPerCollection`).
- Prüfung nur, wenn das Feld mindestens `minOutlierSamples` (Default 8) numerische Werte hat.
- **z-Score:** Wert ist Ausreißer, wenn `|x − mean| / stddev > outlierZ` (Default 3.0).
- **IQR/Tukey-Fence:** Wert ist Ausreißer, wenn er außerhalb `[Q1 − outlierIqrK·IQR, Q3 + outlierIqrK·IQR]`
  liegt (`outlierIqrK` Default 1.5; Quantile per linearer Interpolation).
- Ergebnis `FieldOutliers{collection, field, count, mean, stddev, q1, median, q3, iqr,
  outliers, outlierRate, method, values (max. 100 Beispiele)}` – nur Felder mit
  mindestens einem Ausreißer werden gemeldet; `method` = `"z-score"` / `"IQR"` / `"z-score+IQR"`.
- **Health-Score:** `AnalysisReport::outliers` wird von `analyze()` gefüllt; neuer Faktor
  Ausreißer mit Gewicht 0.15 (`1 − min(1, Outlier-Rate)` über alle numerischen Werte,
  neutral 1.0 ohne numerische Werte). Gewichte ab v0.40: 0.30 / 0.15 / 0.20 / 0.20 / 0.15.
- Präsentationen: JSON (`"outliers":[{...}]` inkl. Quantilen + Beispielwerten),
  Markdown (eigene Sektion „Ausreißer (Outliers, z-Score + IQR)" + Empfehlung),
  `printSummary` (`..., Outliers: N`).
- CLI: `inmemdb_discover a.json --outliers` (Outlier-Report als JSON) und
  `--report <prefix>` (integriert).

Tests: `tests/test_discovery6.cpp` (26 Checks): Erkennung eines klaren Ausreißers,
sauberes Feld, Schwellen konfigurierbar, minOutlierSamples, cross-database,
`analyze()`-Integration (Health-Score sinkt, alle vier Präsentationen).

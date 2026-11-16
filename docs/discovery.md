# Relationship-Discovery (FA-27, v0.29)

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.29; Erweiterungen v0.30/v0.31/v0.32) – **ohne externe Abhängigkeiten** (nur InMemDB + C++17) |
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
```

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
inmemdb_discover a.json --apply                                 # FK-Refs anlegen + Datei zurückschreiben
inmemdb_discover a.json --apply --out report.json
```

## Tests

`tests/test_discovery.cpp` (21 Checks): FK in einer DB + **zwei Databases**, FD (inkl.
trivial-Vermerk), Pearson + kategorial, Near-Duplicates cross-collection, applyDiscovered
+ `joinIds`, toJson/toMermaid, Sampling-Reproduzierbarkeit.

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

# Relationship discovery (FA-27, v0.29) – API reference

> **Detailed, explanatory guide with examples and images:**
> [`docs/en/guides/discovery-guide.md`](discovery-guide.md)

| Field | Value |
|---|---|
| Status | implemented (v0.29; extensions v0.30/v0.31/v0.32/v0.33/v0.40) – **no external dependencies** (only InMemDB + C++17) |
| Reference | requirements FA-27 (optional); builds on foundations v0.27 (`ValueLess`, `distinctValues`, `sample`, `AggOp::Distinct`) and FA-18/FA-26 |

Module `include/memdb/discovery.hpp` + `src/discovery.cpp`: examines data for
**relationships and dependencies** – across collections **and databases** (simply pass a
`std::vector<Collection>` from different `Database` objects).

## Functions

```cpp
memdb::DiscoveryOptions opts; // minSupport, minConfidence, minCorrelation, minSimilarity, sampleSize, seed, ...

// 1) foreign-key candidates via value overlap (cross-collection/cross-database)
auto fk  = memdb::discoverForeignKeys({db1.collection("orders"), db2.collection("users")}, opts);
// 2) functional dependencies within a collection (X -> Y)
auto fd  = memdb::discoverFunctionalDependencies(coll, opts);        // e.g. zip -> city
// 3) correlations: numeric (Pearson |r|) + categorical (association U)
auto cor = memdb::discoverCorrelations(coll, opts);                   // e.g. x ~ y, color -> category
// 4) near-duplicates (normalized edit distance, also across collections)
auto nd  = memdb::discoverNearDuplicates({collA, collB}, opts);       // e.g. "Smith" ~ "Smyth"
// 5) everything combined
auto all = memdb::discoverRelations(db, opts);                        // one database
auto allX = memdb::discoverRelations({c1, c2, c3}, opts);             // any number of collections/DBs

// 6) apply findings as FA-18 references (only within one database)
memdb::ReferenceIndex refs(db);
std::size_t n = memdb::applyDiscovered(db, fk, refs, /*minConfidence=*/0.9);

// 7) reports
std::string j = memdb::toJson(all);
std::string m = memdb::toMermaid(all);   // renders as a diagram

// 8) v0.30: cardinality is in DiscoveredRelation::cardinality (1:1/n:1/1:n/n:m)
// 9) v0.30: orphans (references into the void) + multi-hop paths
auto orph = memdb::findOrphansForRelations(colls, all, opts);      // references into the void
auto paths = memdb::findPaths(all, "order_items", "users", 4);     // multi-hop FK paths
// 10) v0.31: data profile + candidate keys
auto prof = memdb::profileCollection(coll, opts);  std::string pj = memdb::toJson(prof);
auto keys = memdb::discoverCandidateKeys(coll, opts);
// 11) v0.32: diff + merge suggestions (also cross-database)
auto d = memdb::diffDatabases(db1, db2, opts);     std::string dj = memdb::toJson(d);
auto sug = memdb::suggestMerges(colls, opts);      // candidates for FA-24 mergeCollections
// 12) v0.33: automatic full analysis + health score + report presentation
memdb::AnalysisReport rep = memdb::analyze(db, opts);            // or analyze({c1, c2}, opts) cross-database
memdb::printSummary(rep, std::cout);
std::ofstream("analysis.md")   << memdb::toMarkdown(rep);
std::ofstream("analysis.json") << memdb::toJson(rep);
std::ofstream("graph.mmd")     << memdb::toMermaid(rep);
// 13) v0.40: outlier detection (z-score + IQR) per numeric field, also cross-database
auto outs = memdb::discoverOutliers(db, opts);   // or discoverOutliers(coll, opts) / ({c1, c2}, opts)
// outs feed into analyze(): AnalysisReport::outliers + health-score factor (weights 0.30/0.15/0.20/0.20/0.15)
```

Details, semantics and examples for each step: [discovery-guide.md](discovery-guide.md).

## Semantics

- **FK discovery:** values of `A.f` are intersected with values of `B.g` (via
  `std::set<Value, ValueLess>`); `confidence` = overlap ratio, `support` = share of
  source documents carrying the field. Name heuristic: `*_id`/`*_ref` (source side) and `id`/`pk`
  (target side) → note `name-heuristik`; on opposing findings with equal confidence the
  `*_id` side wins. At least 2 common values required.
- **FD:** `X → Y` if every X group has exactly one Y value (unique X values marked as
  "trivial"). Threshold: confidence ≥ max(minConfidence, 0.999).
- **Correlation:** Pearson for numeric pairs (|r| ≥ minCorrelation, n ≥ 10);
  categorical for string-X + scalar Y (U = 1 − H(Y|X)/H(Y) ≥ minCorrelation).
- **Near-duplicates:** normalized Levenshtein similarity ≥ minSimilarity on sampled string
  values (max. `maxValuesPerField` per field).
- **Sampling:** `sampleSize > 0` → reproducible `Collection::sample(sampleSize, seed)`
  instead of a full scan (same seed → same result).
- **Read-only:** discovery only reads (`forEach`/`findById`/`distinctValues`/`sample`),
  changes nothing. `applyDiscovered` only creates FK findings with `confidence ≥ minConfidence`
  as `ReferenceIndex::link(..., "discovered_<field>")` – only if both collections live in
  the same database (cross-database findings are skipped there).

## CLI

```sh
inmemdb_discover shop1.inmemdb.json shop2.inmemdb.json          # cross-database report (JSON)
inmemdb_discover shop.enc --password secret --mermaid           # encrypted, Mermaid output
inmemdb_discover a.json b.json --min-support 0.9 --sample 10000 --seed 7
inmemdb_discover a.json --orphans                               # orphan report (v0.30)
inmemdb_discover a.json --outliers                              # outlier report (v0.40, z-score + IQR)
inmemdb_discover a.json --path order_items users                # FK paths A -> B (v0.30)
inmemdb_discover a.json b.json --diff                           # diffDatabases (v0.32)
inmemdb_discover a.json --suggest-merges                        # merge suggestions (v0.32)
inmemdb_discover a.json --report analysis                       # full analysis (v0.33): analysis.report.md/.relations.json/.graph.mmd + summary
inmemdb_discover a.json --apply                                 # create FK refs + write file back
inmemdb_discover a.json --apply --out report.json
```

All options: `--password`, `--min-support`, `--min-confidence`, `--min-correlation`,
`--min-similarity`, `--sample`, `--seed`, `--mermaid`, `--orphans`, `--outliers`,
`--path A B`, `--diff`, `--suggest-merges`, `--report <prefix>`, `--apply`, `--out <file>`.
Details and examples: [discovery-guide.md](discovery-guide.md) §6.

## Tests

- `tests/test_discovery.cpp` (21 checks): FK in one DB + **two databases**, FD (incl.
  trivial note), Pearson + categorical, near-duplicates cross-collection, applyDiscovered
  + `joinIds`, toJson/toMermaid, sampling reproducibility
- `tests/test_discovery2.cpp` (17 checks, v0.30): cardinality, `findOrphans`/`findOrphansForRelations`, `findPaths`
- `tests/test_discovery3.cpp` (18 checks, v0.31): `profileCollection`/`toJson`, `discoverCandidateKeys`
- `tests/test_discovery4.cpp` (19 checks, v0.32): `diffCollections`/`diffDatabases`, `suggestMerges`
- `tests/test_discovery5.cpp` (23 checks, v0.33): `analyze` + `AnalysisReport`, all four
  presentations (JSON/Markdown/Mermaid/console), health score (clean DB > DB with orphan)
- `tests/test_discovery6.cpp` (26 checks, v0.40): `discoverOutliers` (z-score + IQR, thresholds,
  minOutlierSamples, cross-database), integration in `analyze()` (report field `outliers`,
  health score drops with outliers), all four presentations

124 discovery checks in total.

## Extensions v0.30

- **Cardinality:** `DiscoveredRelation::cardinality` ("1:1" / "n:1" / "1:n" / "n:m", from
  the source collection's perspective, via distinct-vs.-docCount on both sides) – also in the JSON report.
- **Orphan report** (integrity check): `findOrphans(source, field, target, targetField)`
  returns `OrphanReport{scanned, orphans, orphanValues}` – source values without a counterpart;
  `findOrphansForRelations(colls, rels)` applies this to all FK findings.
- **Path search:** `findPaths(rels, from, to, maxDepth)` – all FK paths between two
  collections (DFS, cycle-safe) as multi-hop join suggestions.
- CLI: `inmemdb_discover … --orphans` and `--path <from> <to>`.

## Extensions v0.31: data profile & candidate keys

- `profileCollection(coll, opts)` → `CollectionProfile`: per field type distribution, present/nulls,
  distinct, min/max (numbers/strings), up to 3 example values; `toJson(profile)` as report.
- `discoverCandidateKeys(coll, opts)`: minimal field sets that uniquely identify
  documents (single fields first; pairs only if neither alone is a key).

## Extensions v0.32: cross-DB diff & merge suggestions

- `diffCollections(a, b, opts)` → `CollectionDiff`: fields only in A/B, common fields,
  Jaccard field similarity, row counts, value overlap per common field
- `diffDatabases(db1, db2, opts)` → `DatabaseDiff`: pairwise diff of same-named collections,
  names only in A/B listed separately; `toJson(diff)` as report; `__references` is ignored
- `suggestMerges(colls, opts)` → `MergeSuggestion` per pair with schema Jaccard ≥
  `minSchemaSimilarity` (id-like fields boost the value overlap) – direct candidate for
  FA-24 `Database::mergeCollections`; also cross-database
- CLI: `inmemdb_discover a.json b.json --diff` and `--suggest-merges`

## Extension v0.33: automatic full analysis + report (analyze)

`analyze(...)` bundles all discovery building blocks into an `AnalysisReport` –
also cross-database (`analyze(std::vector<Collection>, opts)` or `analyze(Database&, opts)`):

- `profiles` (per collection, v0.31), `relations` (v0.29/30), `orphans` (v0.30),
  `mergeSuggestions` (v0.32), most important multi-hop `paths` (v0.30, max 20)
- **Health score 0–100** with traffic light 🟢(≥80)/🟡(≥50)/🔴(<50): weighted mix of
  integrity (1 − orphan rate), redundancy (1 − near-duplicate rate), structure
  (share of collections in at least one FK relation; without FKs neutral 1.0 –
  missing relations are not a defect), type consistency (average share of the
  dominant type per field) and since **v0.40** outliers (1 − outlier rate over all
  numeric values; without numeric values neutral 1.0).
  Weights from v0.40: 0.30 / 0.15 / 0.20 / 0.20 / 0.15 (v0.33–v0.39: 0.35 / 0.20 / 0.25 / 0.20).

Presentation of the report in four forms:

| Function | Output |
|---|---|
| `toMarkdown(report)` | Markdown report: overview (profiles), discovered relations, integrity/orphans, merge suggestions, recommendations |
| `toJson(report)` | complete JSON (healthScore, totals, profiles, relations, orphans, mergeSuggestions, paths, outliers) |
| `toMermaid(report)` | Mermaid graph: FK relations as solid edges (with confidence), correlations as dashed self-loops |
| `printSummary(report, ostream)` | compact console output with health score + traffic light |

```cpp
memdb::AnalysisReport rep = memdb::analyze(db, opts);          // or analyze({collA, collB}, opts)
memdb::printSummary(rep, std::cout);
std::ofstream("analysis.md")   << memdb::toMarkdown(rep);
std::ofstream("analysis.json") << memdb::toJson(rep);
std::ofstream("graph.mmd")     << memdb::toMermaid(rep);
```

CLI: `inmemdb_discover a.json b.json --report analysis` writes `analysis.report.md`,
`analysis.relations.json` and `analysis.graph.mmd` and prints the summary to the console.

Tests: `tests/test_discovery5.cpp` (23 checks): report structure, all four presentations,
cross-database `analyze`, health score (clean DB without orphans scores higher than a DB with orphan).

## Extension v0.40: outlier detection (outliers)

`discoverOutliers(const Collection&, opts)` or `discoverOutliers(const std::vector<Collection>&, opts)`
(and therefore also `analyze(Database&, opts)`) detect statistical outliers per **numeric field**:

- One pass per collection collects the numeric values of each field (sampling via
  `sampleSize`/`seed` is respected, field cap `maxFieldsPerCollection`).
- Checked only if the field has at least `minOutlierSamples` (default 8) numeric values.
- **z-score:** a value is an outlier if `|x − mean| / stddev > outlierZ` (default 3.0).
- **IQR/Tukey fence:** a value is an outlier if it lies outside `[Q1 − outlierIqrK·IQR, Q3 + outlierIqrK·IQR]`
  (`outlierIqrK` default 1.5; quantiles via linear interpolation).
- Result `FieldOutliers{collection, field, count, mean, stddev, q1, median, q3, iqr,
  outliers, outlierRate, method, values (max. 100 examples)}` – only fields with
  at least one outlier are reported; `method` = `"z-score"` / `"IQR"` / `"z-score+IQR"`.
- **Health score:** `AnalysisReport::outliers` is filled by `analyze()`; new outlier factor
  with weight 0.15 (`1 − min(1, outlier rate)` over all numeric values,
  neutral 1.0 without numeric values). Weights from v0.40: 0.30 / 0.15 / 0.20 / 0.20 / 0.15.
- Presentations: JSON (`"outliers":[{...}]` incl. quantiles + example values),
  Markdown (own section "Outliers (z-score + IQR)" + recommendation),
  `printSummary` (`..., Outliers: N`).
- CLI: `inmemdb_discover a.json --outliers` (outlier report as JSON) and
  `--report <prefix>` (integrated).

Tests: `tests/test_discovery6.cpp` (26 checks): detection of a clear outlier,
clean field, configurable thresholds, minOutlierSamples, cross-database,
`analyze()` integration (health score drops, all four presentations).

## See also

- **Cross-collection influence analysis (FA-32):** foreign keys discovered with
  `discoverForeignKeys` serve there as a join source to compute dependencies
  between fields of different collections –
  [docs/en/guides/cross-influence.md](cross-influence.md)
- Influence analysis within a single collection (FA-31):
  [docs/en/guides/influence.md](influence.md)

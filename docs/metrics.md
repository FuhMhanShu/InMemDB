# Metriken & Diagnose (v0.22)

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.22) |
| Bezug | `include/memdb/metrics.hpp`, `Collection::metrics()` / `resetMetrics()`, `Database::metrics()` |

InMemDB führt pro Collection Diagnose-Zähler im zentralen `detail::CollectionState`
(`src/collection_state.hpp`) – dadurch werden `Collection`- und `Transaction`-Zugriffe
automatisch mitgezählt, ohne doppelte Logik. Alle Zugriffe erfolgen unter dem Lock der
Collection, die Zähler sind `std::atomic`.

## Nutzung

```cpp
Collection c = db.createCollection("users");
c.createIndex("age");
// ... Betrieb ...
CollectionMetrics m = c.metrics();
// m.inserts, m.updates, m.removes, m.queries, m.indexQueries, m.scannedDocs,
// m.changeEvents, m.spillReads, m.spillWrites, m.txCommits, m.txRollbacks,
// m.docs, m.indexCount, m.lastCandidateCount, m.lastQueryUsedIndex,
// m.lastQueryMs, m.totalQueryMs
c.resetMetrics();                    // Zähler zurücksetzen (Ist-Werte bleiben)

DatabaseMetrics dm = db.metrics();   // Aggregation über alle Collections
dm.collectionCount; dm.totalDocs; dm.sum.inserts; // ...
CollectionMetrics one = db.metrics("users");
```

## Felder (`CollectionMetrics`)

| Feld | Bedeutung |
|---|---|
| `inserts` / `updates` / `removes` | Schreibzugriffe (Anzahl betroffener Dokumente; `insertBulk` zählt pro Doc; auch zurückgerollte Tx-Operationen zählen als Op) |
| `queries` | Lesezugriffe: `find`/`findOne`/`findIds`/`forEach`/`count`/`aggregate`/`groupBy` (Schreib-Scans von `update`/`remove`/`upsert` zählen **nicht** als Query) |
| `indexQueries` | davon Queries, die einen Index benutzt haben |
| `scannedDocs` | Summe der Kandidaten aller Queries – misst den Vollscan-Anteil (`scannedDocs / (queries * docs)` nahe 1 = meist Vollscan) |
| `lastCandidateCount`, `lastQueryUsedIndex`, `lastQueryMs`, `totalQueryMs` | Letzte Query: Kandidatenzahl, Index-Nutzung, Dauer (ms), akkumulierte Dauer |
| `changeEvents` | emittierte Change-Events (Observer + Tx-Events; bei Rollback werden die Events verworfen, der Zähler bleibt) |
| `spillReads` / `spillWrites` | Hybrid-Modus (v0.18): aus der Spill-Datei gelesene / geschriebene Records |
| `txCommits` / `txRollbacks` | abgeschlossene / zurückgerollte Transaktionen (expliziter Rollback und RAII-Destruktor) |
| `docs` / `indexCount` | Ist-Werte: aktuelle Dokumentanzahl (RAM + Spill), Anzahl Indizes |

## Typische Diagnose

- **Vollscan trotz Index?** `indexQueries == 0` bei vielen Queries → Index fehlt oder Query nutzt keinen index-tauglichen Vergleich (`indexHint`).
- **Langsame Queries?** `totalQueryMs / queries` bzw. `lastQueryMs`; `lastCandidateCount` zeigt, wie viele Docs geprüft wurden.
- **Viel Spill-IO?** `spillReads`/`spillWrites` hoch → `maxInMemory` (v0.18) erhöhen oder `compactSpill()` ausführen.
- **Viele Rollbacks?** `txRollbacks` relativ zu `txCommits` → Transaktionsgranularität prüfen.

## Implementierung

- Zähler: `detail::Metrics` in `src/collection_state.hpp` (Member `CollectionState::metrics`),
  Inkremente direkt in den `do*`-Methoden bzw. `candidates(q, metered)` (Timing per
  `std::chrono::steady_clock`).
- Snapshot: `Collection::metrics()` (shared_lock, kopiert die atomics), `resetMetrics()`.
- Aggregation: `Database::metrics()` summiert `Collection::metrics()` aller Collections.
- Tests: `tests/test_metrics.cpp` (34 Checks).

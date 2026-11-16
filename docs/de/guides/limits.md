# Optionale Collection-Limits (FA-28, v0.39)

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.39), **opt-in** |
| Bezug | NFA-02 (Speicher), `docs/de/internals/hardening.md` (Ressourcen-Schranken), Hybrid/Spill v0.18 als RAM-Alternative |

InMemDB bleibt im Kern **unbegrenzt** (Default: alle Limits 0). Wer eine Schranke gegen
Speicherbomben/unbegrenztes Wachstum braucht, setzt sie pro Collection:

```cpp
memdb::CollectionLimits lim;
lim.maxDocuments         = 5'000'000; // RAM / Ø Dokumentgröße, mit Headroom
lim.maxDocumentBytes     = 4 * 1024 * 1024;  // 4 MiB – Riesen-Docs sind der klassische RAM-Killer
lim.maxFieldsPerDocument = 1000;
lim.maxIndexes           = 64;
coll.setLimits(lim);
coll.limits();             // aktuell gesetzte Limits lesen
```

## Semantik

- Überschreitung → **`LimitError`** ( Unterklasse von `memdb::Error`) **vor der Mutation** –
  `insert`, `insertBulk`, `loadJson`, `update`/`updateById`, `replace`, `upsertById`,
  `createIndex` sind dadurch atomar gegen Limit-Verstöße (Bulk prüft alle Docs vorab).
- Verstöße zählen in **`CollectionMetrics::limitViolations`** (`Collection::metrics()`,
  `Database::metrics().sum`, `resetMetrics()`).
- Wirkt auch in **Transaktionen** (Rollback stellt alles wieder her) und inkl. **Spill-Docs**
  (`doSize()` zählt RAM + Spill).
- `maxDocumentBytes` wird als serialisierte JSON-Größe gemessen (nur wenn gesetzt – sonst
  kein Overhead).

## Richtwerte (Faustregeln, keine harten Regeln)

| Limit | Faustregel |
|---|---|
| `maxDocuments` | verfügbarer RAM / Ø Dokumentgröße; z. B. 8 GB RAM, ~1 KB/Doc → ~8 Mio., mit Headroom 1–5 Mio. setzen |
| `maxDocumentBytes` | 1–16 MiB |
| `maxFieldsPerDocument` | ~1000 (breite schema-lose Docs bremsen Scans/Indizes) |
| `maxIndexes` | ~32–64 (jeder Index kostet RAM + Schreib-Amplifikation) |

Für „größer als RAM“ ist weiterhin der **Hybrid-/Spill-Modus** (v0.18, `setOverflow`)
zuständig – Limits begrenzen die *Anzahl/Größe*, Spill begrenzt den *RAM-Verbrauch*.

## Tests
`tests/test_limits.cpp` (34 Checks): Default unbegrenzt, jedes Limit einzeln (insert/bulk-
Atomarität/update/replace/upsert/loadJson/createIndex), Metrik `limitViolations`, Tx-Rollback,
Database-Aggregation.

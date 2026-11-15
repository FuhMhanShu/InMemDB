# Collections zusammenführen (Merge) – FA-24, v0.24

| Feld | Wert |
|---|---|
| Status | umgesetzt (v0.24) |
| Bezug | Anforderungen FA-24 (Kann) |

`Database::mergeCollections` fügt die Dokumente mehrerer Quell-Collections in eine
Ziel-Collection ein ("Union" auf Collection-Ebene).

## API

```cpp
memdb::MergeOptions opts;                 // dropSources=false, sourceField="_source", mergeIndexes=true
memdb::MergeStats st = db.mergeCollections("ziel", {"alt1", "alt2"}, opts, &refs /* optional */);
// st.merged, st.perSource["alt1"], st.adoptedIndexes, st.droppedSources, st.rewrittenReferences
```

- Existiert `ziel` nicht, wird sie angelegt. Duplikate in `sources` werden ignoriert;
  `ziel` in `sources` oder leere `sources` → `Error`; unbekannte Quelle →
  `CollectionNotFoundError`.
- **Neue IDs**: Dokument-IDs sind pro Collection vergeben, daher bekommt jedes
  gemergte Dokument eine **neue ID** im Ziel. Sofern `sourceField` nicht leer ist,
  wird dieses Feld auf den Namen der Quell-Collection gesetzt (überschreibt ein
  gleichnamiges Feld) – so bleibt die Herkunft nachvollziehbar.
- **Indizes**: mit `mergeIndexes=true` (Default) werden Index-Definitionen der
  Quellen übernommen, soweit im Ziel nicht vorhanden (Union; bestehende
  Ziel-Indizes bleiben). Unique-Index-Verletzung dabei → `UniqueConstraintError`.
- **Quellen löschen**: `dropSources=true` entfernt die Quell-Collections nach
  erfolgreichem Merge. Übergibt man zusätzlich einen `ReferenceIndex*`, werden
  dessen Referenzen umgeschrieben (`from_key`/`from_coll`/`from_id` bzw.
  `to_*` von `quelle#id` auf `ziel#neueId`, `quelle#` auf `ziel#`), damit keine
  Dangling-Refs entstehen (`ReferenceIndex::rewriteKeys`). Ohne `ReferenceIndex*`
  werden Refs auf gelöschte Quellen dangling (`dangling()`/`pruneDangling()`).
  Der Referenz-Speicher selbst darf nicht Quelle sein.
- **Atomarität**: der Merge läuft unter exklusiven Locks aller beteiligten
  Collections (in sortierter Namensreihenfolge, deadlock-frei). Wirft ein Dokument
  `SchemaError` (FA-20-Schema des Ziels) oder verletzt einen Unique-Index, werden
  **alle bisher eingefügten Dokumente zurückgerollt** (`nextId` wird
  zurückgesetzt) und die Exception durchgereicht. Eine laufende Transaktion auf
  einer beteiligten Collection ist während des Merge nicht möglich (die Locks
  schließen sich aus; der Merge selbst ist aber atomar).
- **Observer**: pro eingefügtem Dokument feuert ein `ChangeType::Insert`-Event
  (nach Loslassen der Locks); Metriken (`inserts`, `changeEvents`) zählen mit.
- **Hybrid/Out-of-Core (v0.18)**: gespillte Dokumente der Quellen werden
  mitgelesen (`CollectionState::doSnapshotAll`); Eviction ist während des Merge
  per `txLocks` unterdrückt, damit der Rollback im RAM bleiben kann. Die
  Spill-Datei einer gelöschten Quelle bleibt als Datei liegen (In-Memory-DB:
  keine Datei-Lebenszyklen-Verwaltung).

## Beispiel

```cpp
memdb::Database db("shop");
memdb::ReferenceIndex refs(db);
// ... db.collection("kunden_alt"), db.collection("kunden_neu") befüllt ...
memdb::MergeOptions opts;
opts.dropSources = true;                 // Alt-Collections danach entfernen
auto st = db.mergeCollections("kunden", {"kunden_alt", "kunden_neu"}, opts, &refs);
// alle Dokumente in "kunden" mit _source="kunden_alt"/"kunden_neu", Referenzen umgeschrieben
```

## Tests

`tests/test_merge.cpp` (50 Checks): Basis-Merge mit neuen IDs + `_source`, Ziel wird
angelegt, Fehlerfälle (leer/Ziel=Quelle/unbekannt), SchemaError + Rollback,
UniqueConstraintError + Rollback, Index-Uebernahme inkl. Index-Nutzung
(`metrics().indexQueries`), dropSources, Referenz-Umschreiben (keine Dangling-Refs,
`joinIds` funktioniert danach), Observer-Events, Spill-Quelle, `sourceField=""`.
